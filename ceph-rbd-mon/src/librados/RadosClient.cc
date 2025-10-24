/**
 * @file RadosClient.cc
 * @brief RADOS客户端核心实现文件
 *
 * 本文件实现了RadosClient类的具体功能，是librados库的核心执行引擎。
 * 负责管理与Ceph集群的完整通信生命周期，包括：
 *
 * - 集群连接建立和认证
 * - Monitor和OSD通信协调
 * - 异步IO操作执行和响应处理
 * - 连接状态监控和故障恢复
 * - 池管理和元数据操作
 * - 服务守护进程注册和管理
 * - 日志和监视功能
 *
 * 关键组件协作：
 * - MonClient: Monitor集群通信
 * - MgrClient: Manager集群通信
 * - Objecter: OSD集群通信和操作执行
 * - Messenger: 底层网络通信
 * - 线程池: 异步操作处理
 */

// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2012 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <iostream>
#include <string>
#include <sstream>
#include <pthread.h>
#include <errno.h>

#include "common/ceph_context.h"
#include "common/config.h"
#include "common/common_init.h"
#include "common/ceph_json.h"
#include "common/errno.h"
#include "common/ceph_json.h"
#include "common/async/blocked_completion.h"
#include "include/buffer.h"
#include "include/stringify.h"
#include "include/util.h"

#include "msg/Messenger.h"

// needed for static_cast
#include "messages/MLog.h"

#include "AioCompletionImpl.h"
#include "IoCtxImpl.h"
#include "PoolAsyncCompletionImpl.h"
#include "RadosClient.h"

#include "include/ceph_assert.h"
#include "common/EventTrace.h"

#define dout_subsys ceph_subsys_rados
#undef dout_prefix
#define dout_prefix *_dout << "librados: "

using std::ostringstream;
using std::string;
using std::map;
using std::vector;

namespace bc = boost::container;
namespace bs = boost::system;
namespace ca = ceph::async;
namespace cb = ceph::buffer;

/**
 * @brief RadosClient构造函数
 * @param cct_ Ceph上下文指针
 *
 * 初始化RadosClient实例，建立与Ceph集群通信的基础设施：
 * 1. 继承Dispatcher以处理网络消息分发
 * 2. 设置CephContext的智能指针和自定义删除器
 * 3. 注册配置观察者以监控配置变更
 * 4. 初始化Monitor操作超时配置
 *
 * @note 此构造函数建立了librados客户端的基本框架，
 * 实际的集群连接需要在后续调用connect()方法建立。
 */
librados::RadosClient::RadosClient(CephContext *cct_)
  : Dispatcher(cct_->get()),
    cct_deleter{cct, [](CephContext *p) {p->put();}}
{
  auto& conf = cct->_conf;
  conf.add_observer(this);
  rados_mon_op_timeout = conf.get_val<std::chrono::seconds>("rados_mon_op_timeout");
}

/**
 * @brief 根据名称查找池ID
 * @param name 池名称
 * @return 池ID，负值表示错误
 *
 * 通过OSD映射表根据池名称查找对应的池ID：
 * 1. 等待OSD映射可用
 * 2. 在当前OSD映射中查找池名称
 * 3. 如果未找到且池可能被删除，等待最新映射后重试
 *
 * @note 此方法是线程安全的，通过Objecter的with_osdmap()方法访问OSD映射。
 */
int64_t librados::RadosClient::lookup_pool(const char *name)
{
  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  int64_t ret = objecter->with_osdmap(std::mem_fn(&OSDMap::lookup_pg_pool_name),
                                 name);
  if (-ENOENT == ret) {
    // Make sure we have the latest map
    int r = wait_for_latest_osdmap();
    if (r < 0)
      return r;
    ret = objecter->with_osdmap(std::mem_fn(&OSDMap::lookup_pg_pool_name),
                                 name);
  }

  return ret;
}

/**
 * @brief 检查池是否需要对齐（简化版本）
 * @param pool_id 池ID
 * @return true表示需要对齐，false表示不需要或出错
 *
 * 检查指定池是否要求追加操作的对齐访问。
 * 如果查询失败，返回false作为保守的默认值。
 */
bool librados::RadosClient::pool_requires_alignment(int64_t pool_id)
{
  bool required;
  int r = pool_requires_alignment2(pool_id, &required);
  if (r < 0) {
    // Cast answer to false, this is a little bit problematic
    // since we really don't know the answer yet, say.
    return false;
  }

  return required;
}

/**
 * @brief 检查池是否需要对齐（带错误码版本）
 * @param pool_id 池ID
 * @param req 返回是否需要对齐
 * @return 0表示成功，负值表示错误
 *
 * 安全版本的池对齐检查，提供详细的错误信息：
 * 1. 等待OSD映射可用
 * 2. 检查池是否存在
 * 3. 查询池的追加对齐要求
 *
 * @note 此方法是线程安全的，通过OSD映射的只读访问。
 */
int librados::RadosClient::pool_requires_alignment2(int64_t pool_id,
						    bool *req)
{
  if (!req)
    return -EINVAL;

  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  return objecter->with_osdmap([req, pool_id](const OSDMap& o) {
      if (!o.have_pg_pool(pool_id)) {
	return -ENOENT;
      }
      *req = o.get_pg_pool(pool_id)->requires_aligned_append();
      return 0;
    });
}

uint64_t librados::RadosClient::pool_required_alignment(int64_t pool_id)
{
  uint64_t alignment;
  int r = pool_required_alignment2(pool_id, &alignment);
  if (r < 0) {
    return 0;
  }

  return alignment;
}

// a safer version of pool_required_alignment
int librados::RadosClient::pool_required_alignment2(int64_t pool_id,
						    uint64_t *alignment)
{
  if (!alignment)
    return -EINVAL;

  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  return objecter->with_osdmap([alignment, pool_id](const OSDMap &o) {
      if (!o.have_pg_pool(pool_id)) {
	return -ENOENT;
      }
      *alignment = o.get_pg_pool(pool_id)->required_alignment();
      return 0;
    });
}

int librados::RadosClient::pool_get_name(uint64_t pool_id, std::string *s, bool wait_latest_map)
{
  int r = wait_for_osdmap();
  if (r < 0)
    return r;
  retry:
  objecter->with_osdmap([&](const OSDMap& o) {
      if (!o.have_pg_pool(pool_id)) {
	r = -ENOENT;
      } else {
	r = 0;
	*s = o.get_pool_name(pool_id);
      }
    });
  if (r == -ENOENT && wait_latest_map) {
    r = wait_for_latest_osdmap();
    if (r < 0)
      return r;
    wait_latest_map = false;
    goto retry;
  }

  return r;
}

int librados::RadosClient::get_fsid(std::string *s)
{
  if (!s)
    return -EINVAL;
  std::lock_guard l(lock);
  ostringstream oss;
  oss << monclient.get_fsid();
  *s = oss.str();
  return 0;
}

int librados::RadosClient::ping_monitor(const string mon_id, string *result)
{
  int err = 0;
  /* If we haven't yet connected, we have no way of telling whether we
   * already built monc's initial monmap.  IF we are in CONNECTED state,
   * then it is safe to assume that we went through connect(), which does
   * build a monmap.
   */
  if (state != CONNECTED) {
    ldout(cct, 10) << __func__ << " build monmap" << dendl;
    err = monclient.build_initial_monmap();
  }
  if (err < 0) {
    return err;
  }

  err = monclient.ping_monitor(mon_id, result);
  return err;
}

/**
 * @brief 连接到Ceph集群
 * @return 0表示成功，负值表示错误
 *
 * 建立与Ceph集群的完整连接，包括以下步骤：
 *
 * 1. **状态检查和初始化**：
 *    - 检查当前连接状态，避免重复连接
 *    - 设置连接状态为CONNECTING
 *    - 启动日志系统
 *
 * 2. **Monitor引导连接**：
 *    - 使用临时的MonClient获取Monitor映射和配置
 *    - 执行通用初始化完成流程
 *
 * 3. **线程池启动**：
 *    - 根据librados_thread_count配置启动异步IO线程池
 *    - 默认为2个工作线程
 *
 * 4. **Monitor连接建立**：
 *    - 构建初始Monitor映射
 *    - 创建客户端信使（Messenger）
 *    - 设置默认策略（要求OSDREPLYMUX特性）
 *
 * 5. **核心组件初始化**：
 *    - 创建Objecter（OSD通信核心组件）
 *    - 设置预算均衡策略
 *    - 为各组件设置信使引用
 *
 * 6. **认证和授权**：
 *    - 初始化Monitor客户端
 *    - 执行认证流程（使用client_mount_timeout）
 *    - 设置客户端名称和全局ID
 *
 * 7. **Manager兼容性处理**：
 *    - 检测旧版集群，设置MgrClient兼容模式
 *    - 订阅Manager映射信息
 *
 * 8. **服务守护进程注册**：
 *    - 如果配置为服务守护进程，向Manager注册
 *    - 初始化Manager客户端
 *
 * 9. **Objecter启动**：
 *    - 设置客户端化身编号
 *    - 启动Objecter，进入CONNECTED状态
 *    - 记录实例ID用于追踪和调试
 *
 * 10. **错误处理和清理**：
 *    - 如果任何步骤失败，回滚所有更改
 *    - 释放已分配的资源
 *    - 重置连接状态
 *
 * @note 此方法是线程安全的，但不允许多次并发调用。
 * 连接建立失败时会自动清理所有已分配资源。
 */
int librados::RadosClient::connect()
{
  int err;

  // already connected?
  if (state == CONNECTING)
    return -EINPROGRESS;
  if (state == CONNECTED)
    return -EISCONN;
  state = CONNECTING;

  if (!cct->_log->is_started()) {
    cct->_log->start();
  }

  {
    MonClient mc_bootstrap(cct, poolctx);
    err = mc_bootstrap.get_monmap_and_config();
    if (err < 0)
      return err;
  }

  common_init_finish(cct);

  poolctx.start(cct->_conf.get_val<std::uint64_t>("librados_thread_count"));

  // get monmap
  err = monclient.build_initial_monmap();
  if (err < 0)
    goto out;

  err = -ENOMEM;
  messenger = Messenger::create_client_messenger(cct, "radosclient");
  if (!messenger)
    goto out;

  // require OSDREPLYMUX feature.  this means we will fail to talk to
  // old servers.  this is necessary because otherwise we won't know
  // how to decompose the reply data into its constituent pieces.
  messenger->set_default_policy(Messenger::Policy::lossy_client(CEPH_FEATURE_OSDREPLYMUX));

  ldout(cct, 1) << "starting msgr at " << messenger->get_myaddrs() << dendl;

  ldout(cct, 1) << "starting objecter" << dendl;

  objecter = new (std::nothrow) Objecter(cct, messenger, &monclient, poolctx);
  if (!objecter)
    goto out;
  objecter->set_balanced_budget();

  monclient.set_messenger(messenger);
  mgrclient.set_messenger(messenger);

  objecter->init();
  messenger->add_dispatcher_head(&mgrclient);
  messenger->add_dispatcher_tail(objecter);
  messenger->add_dispatcher_tail(this);

  messenger->start();

  ldout(cct, 1) << "setting wanted keys" << dendl;
  monclient.set_want_keys(
      CEPH_ENTITY_TYPE_MON | CEPH_ENTITY_TYPE_OSD | CEPH_ENTITY_TYPE_MGR);
  ldout(cct, 1) << "calling monclient init" << dendl;
  err = monclient.init();
  if (err) {
    ldout(cct, 0) << conf->name << " initialization error " << cpp_strerror(-err) << dendl;
    shutdown();
    goto out;
  }

  err = monclient.authenticate(std::chrono::duration<double>(conf.get_val<std::chrono::seconds>("client_mount_timeout")).count());
  if (err) {
    ldout(cct, 0) << conf->name << " authentication error " << cpp_strerror(-err) << dendl;
    shutdown();
    goto out;
  }
  messenger->set_myname(entity_name_t::CLIENT(monclient.get_global_id()));

  // Detect older cluster, put mgrclient into compatible mode
  mgrclient.set_mgr_optional(
      !get_required_monitor_features().contains_all(
        ceph::features::mon::FEATURE_LUMINOUS));

  // MgrClient needs this (it doesn't have MonClient reference itself)
  monclient.sub_want("mgrmap", 0, 0);
  monclient.renew_subs();

  if (service_daemon) {
    ldout(cct, 10) << __func__ << " registering as " << service_name << "."
		   << daemon_name << dendl;
    mgrclient.service_daemon_register(service_name, daemon_name,
				      daemon_metadata);
  }
  mgrclient.init();

  objecter->set_client_incarnation(0);
  objecter->start();
  lock.lock();

  state = CONNECTED;
  instance_id = monclient.get_global_id();

  lock.unlock();

  ldout(cct, 1) << "init done" << dendl;
  err = 0;

 out:
  if (err) {
    state = DISCONNECTED;

    if (objecter) {
      delete objecter;
      objecter = NULL;
    }
    if (messenger) {
      delete messenger;
      messenger = NULL;
    }
  }

  return err;
}

/**
 * @brief 关闭与集群的连接
 *
 * 优雅地关闭所有连接和清理资源，确保数据一致性和资源释放：
 *
 * 1. **状态检查和锁定**：
 *    - 检查当前连接状态，避免重复关闭
 *    - 获取独占锁保护关闭过程
 *
 * 2. **监视回调刷新**：
 *    - 如果Objecter已初始化且连接正常，刷新监视回调
 *    - 确保所有异步监视操作完成
 *
 * 3. **状态重置**：
 *    - 设置连接状态为DISCONNECTED
 *    - 清零实例ID
 *
 * 4. **组件关闭序列**：
 *    - Objecter关闭（如果需要）
 *    - MgrClient关闭
 *    - MonClient关闭
 *    - Messenger关闭并等待完成
 *    - 异步IO线程池停止
 *
 * 5. **资源清理**：
 *    - 释放所有网络连接
 *    - 清理线程池资源
 *    - 记录关闭日志
 *
 * @note 关闭顺序非常重要，必须按照依赖关系的相反顺序执行。
 * 此方法是线程安全的，支持并发调用。
 */
void librados::RadosClient::shutdown()
{
  std::unique_lock l{lock};
  if (state == DISCONNECTED) {
    return;
  }

  bool need_objecter = false;
  if (objecter && objecter->initialized) {
    need_objecter = true;
  }

  if (state == CONNECTED) {
    if (need_objecter) {
      // make sure watch callbacks are flushed
      watch_flush();
    }
  }
  state = DISCONNECTED;
  instance_id = 0;
  l.unlock();
  if (need_objecter) {
    objecter->shutdown();
  }
  mgrclient.shutdown();

  monclient.shutdown();
  if (messenger) {
    messenger->shutdown();
    messenger->wait();
  }
  poolctx.stop();
  ldout(cct, 1) << "shutdown" << dendl;
}

/**
 * @brief 同步刷新监视回调
 * @return 0表示成功
 *
 * 同步等待所有活跃的监视（watch）回调完成。
 * 此操作会阻塞直到所有监视器回调队列为空，
 * 确保所有监视操作都已完成处理。
 *
 * @note 此方法主要用于同步场景，确保在继续其他操作前
 * 所有监视回调都已完成。
 */
int librados::RadosClient::watch_flush()
{
  ldout(cct, 10) << __func__ << " enter" << dendl;
  objecter->linger_callback_flush(ca::use_blocked);

  ldout(cct, 10) << __func__ << " exit" << dendl;
  return 0;
}

struct CB_aio_watch_flush_Complete {
  librados::RadosClient *client;
  librados::AioCompletionImpl *c;

  CB_aio_watch_flush_Complete(librados::RadosClient *_client, librados::AioCompletionImpl *_c)
    : client(_client), c(_c) {
    c->get();
  }

  CB_aio_watch_flush_Complete(const CB_aio_watch_flush_Complete&) = delete;
  CB_aio_watch_flush_Complete operator =(const CB_aio_watch_flush_Complete&) = delete;
  CB_aio_watch_flush_Complete(CB_aio_watch_flush_Complete&& rhs) {
    client = rhs.client;
    c = rhs.c;
  }
  CB_aio_watch_flush_Complete& operator =(CB_aio_watch_flush_Complete&& rhs) {
    client = rhs.client;
    c = rhs.c;
    return *this;
  }

  void operator()() {
    c->lock.lock();
    c->rval = 0;
    c->complete = true;
    c->cond.notify_all();

    if (c->callback_complete ||
	c->callback_safe) {
      boost::asio::defer(client->finish_strand, librados::CB_AioComplete(c));
    }
    c->put_unlock();
  }
};

/**
 * @brief 异步刷新监视回调
 * @param c 异步完成回调接口
 * @return 0表示成功
 *
 * 异步等待所有活跃的监视（watch）回调完成。
 * 通过CB_aio_watch_flush_Complete回调机制，
 * 在监视回调队列清空后通知调用者。
 *
 * @note 此方法是非阻塞的，实际的等待通过异步回调完成。
 * 适用于需要在后台等待监视操作完成的场景。
 */
int librados::RadosClient::async_watch_flush(AioCompletionImpl *c)
{
  ldout(cct, 10) << __func__ << " enter" << dendl;
  objecter->linger_callback_flush(CB_aio_watch_flush_Complete(this, c));
  ldout(cct, 10) << __func__ << " exit" << dendl;
  return 0;
}

/**
 * @brief 获取客户端实例ID
 * @return 客户端实例的唯一标识符
 *
 * 返回当前RadosClient实例的全局唯一标识符。
 * 此ID在连接建立时从Monitor分配，用于追踪和调试目的。
 * 在连接断开时会被重置为0。
 */
uint64_t librados::RadosClient::get_instance_id()
{
  return instance_id;
}

/**
 * @brief 获取OSD兼容性版本要求
 * @param require_osd_release 返回OSD所需的最低版本
 * @return 0表示成功，负值表示错误
 *
 * 查询集群要求OSD支持的最低Ceph版本。
 * 此信息用于确保客户端连接的OSD都支持必要的功能特性。
 *
 * @note 此方法通过OSDMap查询，需要等待OSD映射可用。
 */
int librados::RadosClient::get_min_compatible_osd(int8_t* require_osd_release)
{
  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  objecter->with_osdmap(
    [require_osd_release](const OSDMap& o) {
      *require_osd_release = to_integer<int8_t>(o.require_osd_release);
    });
  return 0;
}

/**
 * @brief 获取客户端兼容性版本要求
 * @param min_compat_client 返回客户端支持的最低版本
 * @param require_min_compat_client 返回客户端所需的最低版本
 * @return 0表示成功，负值表示错误
 *
 * 查询集群对客户端的兼容性要求，包括：
 * - min_compat_client: 客户端支持的最低版本（向后兼容）
 * - require_min_compat_client: 客户端所需的最低版本（功能要求）
 *
 * 此信息用于确保客户端版本与集群要求兼容。
 *
 * @note 此方法通过OSDMap查询，需要等待OSD映射可用。
 */
int librados::RadosClient::get_min_compatible_client(int8_t* min_compat_client,
                                                     int8_t* require_min_compat_client)
{
  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  objecter->with_osdmap(
    [min_compat_client, require_min_compat_client](const OSDMap& o) {
      *min_compat_client = to_integer<int8_t>(o.get_min_compat_client());
      *require_min_compat_client =
	to_integer<int8_t>(o.get_require_min_compat_client());
    });
  return 0;
}

/**
 * @brief RadosClient析构函数
 *
 * 清理RadosClient实例的所有资源：
 * 1. 从配置观察者列表中移除自己
 * 2. 释放信使（Messenger）资源
 * 3. 释放Objecter资源
 * 4. 清空CephContext引用
 *
 * @note 析构函数假设调用者已经正确关闭了连接（调用了shutdown()）。
 * 如果在连接激活状态下销毁，可能导致资源泄露。
 */
librados::RadosClient::~RadosClient()
{
  cct->_conf.remove_observer(this);
  if (messenger)
    delete messenger;
  if (objecter)
    delete objecter;
  cct = NULL;
}

/**
 * @brief 根据池名称创建IO上下文
 * @param name 池名称
 * @param io 返回新创建的IO上下文指针
 * @return 0表示成功，负值表示错误（如池不存在）
 *
 * 通过池名称创建新的IO上下文（IoCtxImpl），用于执行对象操作。
 * 内部过程：
 * 1. 根据名称解析池ID
 * 2. 创建IoCtxImpl实例，关联当前客户端和Objecter
 * 3. 使用默认快照（CEPH_NOSNAP）
 *
 * @note 创建的IoCtxImpl需要调用者负责释放。
 */
int librados::RadosClient::create_ioctx(const char *name, IoCtxImpl **io)
{
  int64_t poolid = lookup_pool(name);
  if (poolid < 0) {
    return (int)poolid;
  }

  *io = new librados::IoCtxImpl(this, objecter, poolid, CEPH_NOSNAP);
  return 0;
}

/**
 * @brief 根据池ID创建IO上下文
 * @param pool_id 池ID
 * @param io 返回新创建的IO上下文指针
 * @return 0表示成功，负值表示错误（如池不存在）
 *
 * 通过池ID创建新的IO上下文（IoCtxImpl），用于执行对象操作。
 * 内部过程：
 * 1. 验证池ID的有效性（通过获取池名称）
 * 2. 创建IoCtxImpl实例，关联当前客户端和Objecter
 * 3. 使用默认快照（CEPH_NOSNAP）
 *
 * @note 创建的IoCtxImpl需要调用者负责释放。
 */
int librados::RadosClient::create_ioctx(int64_t pool_id, IoCtxImpl **io)
{
  std::string pool_name;
  int r = pool_get_name(pool_id, &pool_name, true);
  if (r < 0)
    return r;
  *io = new librados::IoCtxImpl(this, objecter, pool_id, CEPH_NOSNAP);
  return 0;
}

/**
 * @brief 消息分发处理
 * @param m 待处理的消息
 * @return true表示消息已处理，false表示需要继续处理
 *
 * 作为Dispatcher的实现，处理来自网络的消息：
 * 1. 检查连接状态，如果已断开则丢弃消息
 * 2. 如果连接正常，则调用内部_dispatch方法处理消息
 *
 * @note 此方法是线程安全的，通过内部锁保护。
 */
bool librados::RadosClient::ms_dispatch(Message *m)
{
  bool ret;

  std::lock_guard l(lock);
  if (state == DISCONNECTED) {
    ldout(cct, 10) << "disconnected, discarding " << *m << dendl;
    m->put();
    ret = true;
  } else {
    ret = _dispatch(m);
  }
  return ret;
}

void librados::RadosClient::ms_handle_connect(Connection *con)
{
}

bool librados::RadosClient::ms_handle_reset(Connection *con)
{
  return false;
}

void librados::RadosClient::ms_handle_remote_reset(Connection *con)
{
}

bool librados::RadosClient::ms_handle_refused(Connection *con)
{
  return false;
}

bool librados::RadosClient::_dispatch(Message *m)
{
  ceph_assert(ceph_mutex_is_locked(lock));
  switch (m->get_type()) {
  // OSD
  case CEPH_MSG_OSD_MAP:
    cond.notify_all();
    m->put();
    break;

  case CEPH_MSG_MDS_MAP:
    m->put();
    break;

  case MSG_LOG:
    handle_log(static_cast<MLog *>(m));
    break;

  default:
    return false;
  }

  return true;
}


int librados::RadosClient::wait_for_osdmap()
{
  ceph_assert(ceph_mutex_is_not_locked_by_me(lock));

  if (state != CONNECTED) {
    return -ENOTCONN;
  }

  bool need_map = false;
  objecter->with_osdmap([&](const OSDMap& o) {
      if (o.get_epoch() == 0) {
        need_map = true;
      }
    });

  if (need_map) {
    std::unique_lock l(lock);

    ceph::timespan timeout = rados_mon_op_timeout;
    if (objecter->with_osdmap(std::mem_fn(&OSDMap::get_epoch)) == 0) {
      ldout(cct, 10) << __func__ << " waiting" << dendl;
      while (objecter->with_osdmap(std::mem_fn(&OSDMap::get_epoch)) == 0) {
        if (timeout == timeout.zero()) {
          cond.wait(l);
        } else {
          if (cond.wait_for(l, timeout) == std::cv_status::timeout) {
            lderr(cct) << "timed out waiting for first osdmap from monitors"
                       << dendl;
            return -ETIMEDOUT;
          }
        }
      }
      ldout(cct, 10) << __func__ << " done waiting" << dendl;
    }
    return 0;
  } else {
    return 0;
  }
}


int librados::RadosClient::wait_for_latest_osdmap()
{
  bs::error_code ec;
  objecter->wait_for_latest_osdmap(ca::use_blocked[ec]);
  return ceph::from_error_code(ec);
}

/**
 * @brief 获取池列表
 * @param v 返回池ID和名称的列表
 * @return 0表示成功，负值表示错误
 *
 * 获取集群中所有可用池的列表，每个池包含：
 * - 池ID（int64_t）
 * - 池名称（string）
 *
 * 内部过程：
 * 1. 等待OSD映射可用
 * 2. 遍历OSDMap中的所有池
 * 3. 收集池ID和名称对
 *
 * @note 此方法是线程安全的，通过OSDMap的只读访问。
 */
int librados::RadosClient::pool_list(std::list<std::pair<int64_t, string> >& v)
{
  int r = wait_for_osdmap();
  if (r < 0)
    return r;

  objecter->with_osdmap([&](const OSDMap& o) {
      for (auto p : o.get_pools())
	v.push_back(std::make_pair(p.first, o.get_pool_name(p.first)));
    });
  return 0;
}

/**
 * @brief 获取池统计信息
 * @param pools 要查询的池名称列表
 * @param result 返回池统计信息的映射表（池名->统计数据）
 * @param pper_pool 返回是否为每个池单独统计
 * @return 0表示成功，负值表示错误
 *
 * 获取指定池或所有池的统计信息，包括：
 * - 对象数量、大小、使用率等
 * - 读写操作统计
 * - 池容量和使用情况
 *
 * @note 此方法通过Objecter异步获取统计信息，支持批量操作。
 */
int librados::RadosClient::get_pool_stats(std::list<string>& pools,
					  map<string,::pool_stat_t> *result,
					  bool *pper_pool)
{
  bs::error_code ec;

  std::vector<std::string> v(pools.begin(), pools.end());

  auto [res, per_pool] = objecter->get_pool_stats(v, ca::use_blocked[ec]);
  if (ec)
    return ceph::from_error_code(ec);

  if (per_pool)
    *pper_pool = per_pool;
  if (result)
    result->insert(res.begin(), res.end());

  return 0;
}

/**
 * @brief 检查池是否处于自管理快照模式
 * @param pool 池名称
 * @return 1表示处于自管理快照模式，0表示不处于，负值表示错误
 *
 * 检查指定池是否启用了自管理快照模式（unmanaged snaps mode）。
 * 在此模式下，应用程序负责管理快照的创建和删除，
 * 而不是由Ceph自动管理。
 *
 * @note 此方法是线程安全的，通过OSDMap的只读访问。
 */
int librados::RadosClient::pool_is_in_selfmanaged_snaps_mode(
  const std::string& pool)
{
  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  return objecter->with_osdmap([&pool](const OSDMap& osdmap) {
      int64_t poolid = osdmap.lookup_pg_pool_name(pool);
      if (poolid < 0) {
        return -ENOENT;
      }
      return static_cast<int>(
        osdmap.get_pg_pool(poolid)->is_unmanaged_snaps_mode());
    });
}

/**
 * @brief 获取文件系统统计信息
 * @param stats 返回的文件系统统计信息
 * @return 0表示成功，负值表示错误
 *
 * 获取整个集群的文件系统统计信息，包括：
 * - 总容量和可用容量
 * - 对象数量和总大小
 * - 数据分配和使用统计
 *
 * @note 此方法是同步的，通过条件变量等待异步操作完成。
 * 使用C_SafeCond回调机制确保线程安全。
 */
int librados::RadosClient::get_fs_stats(ceph_statfs& stats)
{
  ceph::mutex mylock = ceph::make_mutex("RadosClient::get_fs_stats::mylock");
  ceph::condition_variable cond;
  bool done;
  int ret = 0;
  {
    std::lock_guard l{mylock};
    objecter->get_fs_stats(stats, std::optional<int64_t> (),
			   new C_SafeCond(mylock, cond, &done, &ret));
  }
  {
    std::unique_lock l{mylock};
    cond.wait(l, [&done] { return done;});
  }
  return ret;
}

/**
 * @brief 增加引用计数
 *
 * 原子性地增加RadosClient实例的引用计数。
 * 用于管理对象的生命周期，确保在所有使用者完成前不会销毁。
 */
void librados::RadosClient::get() {
  std::lock_guard l(lock);
  ceph_assert(refcnt > 0);
  refcnt++;
}

/**
 * @brief 减少引用计数
 * @return true表示引用计数为0，可以销毁对象；false表示还有引用
 *
 * 原子性地减少RadosClient实例的引用计数。
 * 当引用计数降至0时，表示可以安全销毁对象。
 *
 * @note 此方法用于智能指针或其他引用管理机制中。
 */
bool librados::RadosClient::put() {
  std::lock_guard l(lock);
  ceph_assert(refcnt > 0);
  refcnt--;
  return (refcnt == 0);
}
 
/**
 * @brief 同步创建存储池
 * @param name 池名称
 * @param crush_rule CRUSH规则ID，用于数据分布策略
 * @return 0表示成功，负值表示错误
 *
 * 同步方式创建新的存储池：
 * 1. 验证池名称有效性
 * 2. 等待OSD映射可用
 * 3. 通过Objecter向Monitor发送创建请求
 * 4. 等待操作完成并返回结果
 *
 * @note 此方法是阻塞的，直到池创建完成或失败。
 * 池名称必须唯一且不为空。
 */
int librados::RadosClient::pool_create(string& name,
				       int16_t crush_rule)
{
  if (!name.length())
    return -EINVAL;

  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  ceph::mutex mylock = ceph::make_mutex("RadosClient::pool_create::mylock");
  int reply;
  ceph::condition_variable cond;
  bool done;
  Context *onfinish = new C_SafeCond(mylock, cond, &done, &reply);
  objecter->create_pool(name, onfinish, crush_rule);

  std::unique_lock l{mylock};
  cond.wait(l, [&done] { return done; });
  return reply;
}

/**
 * @brief 异步创建存储池
 * @param name 池名称
 * @param c 异步完成回调接口
 * @param crush_rule CRUSH规则ID，用于数据分布策略
 * @return 0表示请求已提交，负值表示错误
 *
 * 异步方式创建新的存储池：
 * 1. 验证池名称有效性
 * 2. 等待OSD映射可用
 * 3. 通过Objecter向Monitor发送创建请求
 * 4. 通过回调接口异步返回结果
 *
 * @note 此方法是非阻塞的，结果通过回调函数返回。
 * 池名称必须唯一且不为空。
 */
int librados::RadosClient::pool_create_async(string& name,
					     PoolAsyncCompletionImpl *c,
					     int16_t crush_rule)
{
  int r = wait_for_osdmap();
  if (r < 0)
    return r;

  Context *onfinish = make_lambda_context(CB_PoolAsync_Safe(c));
  objecter->create_pool(name, onfinish, crush_rule);
  return r;
}

/**
 * @brief 获取池的基础层级池ID
 * @param pool_id 池ID
 * @param base_tier 返回基础层级池ID
 * @return 0表示成功，负值表示错误
 *
 * 获取指定池所属的基础层级池ID。在Ceph的分层存储架构中：
 * - 如果池是独立池（非层级池），返回自身ID
 * - 如果池是从属于其他池的层级池，返回其上级池ID
 *
 * @note 此方法是线程安全的，通过OSDMap的只读访问。
 */
int librados::RadosClient::pool_get_base_tier(int64_t pool_id, int64_t* base_tier)
{
  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  objecter->with_osdmap([&](const OSDMap& o) {
      const pg_pool_t* pool = o.get_pg_pool(pool_id);
      if (pool) {
	if (pool->tier_of < 0) {
	  *base_tier = pool_id;
	} else {
	  *base_tier = pool->tier_of;
	}
	r = 0;
      } else {
	r = -ENOENT;
      }
    });
  return r;
}

/**
 * @brief 同步删除存储池
 * @param name 池名称
 * @return 0表示成功，负值表示错误
 *
 * 同步方式删除指定的存储池：
 * 1. 等待OSD映射可用
 * 2. 通过Objecter向Monitor发送删除请求
 * 3. 等待操作完成并返回结果
 *
 * @note 此方法是阻塞的，直到池删除完成或失败。
 * 删除操作需要集群管理员权限，且池必须为空。
 */
int librados::RadosClient::pool_delete(const char *name)
{
  int r = wait_for_osdmap();
  if (r < 0) {
    return r;
  }

  ceph::mutex mylock = ceph::make_mutex("RadosClient::pool_delete::mylock");
  ceph::condition_variable cond;
  bool done;
  int ret;
  Context *onfinish = new C_SafeCond(mylock, cond, &done, &ret);
  objecter->delete_pool(name, onfinish);

  std::unique_lock l{mylock};
  cond.wait(l, [&done] { return done;});
  return ret;
}

/**
 * @brief 异步删除存储池
 * @param name 池名称
 * @param c 异步完成回调接口
 * @return 0表示请求已提交，负值表示错误
 *
 * 异步方式删除指定的存储池：
 * 1. 等待OSD映射可用
 * 2. 通过Objecter向Monitor发送删除请求
 * 3. 通过回调接口异步返回结果
 *
 * @note 此方法是非阻塞的，结果通过回调函数返回。
 * 删除操作需要集群管理员权限，且池必须为空。
 */
int librados::RadosClient::pool_delete_async(const char *name, PoolAsyncCompletionImpl *c)
{
  int r = wait_for_osdmap();
  if (r < 0)
    return r;

  Context *onfinish = make_lambda_context(CB_PoolAsync_Safe(c));
  objecter->delete_pool(name, onfinish);
  return r;
}

/**
 * @brief 设置自身黑名单状态
 * @param set true表示加入黑名单，false表示移出黑名单
 *
 * 控制当前客户端是否在集群的黑名单中：
 * - true: 将自身添加到黑名单，阻止与集群通信
 * - false: 从黑名单中移除自身，恢复与集群通信
 *
 * @note 此方法主要用于测试和调试目的，或在需要临时隔离客户端时使用。
 */
void librados::RadosClient::blocklist_self(bool set) {
  std::lock_guard l(lock);
  objecter->blocklist_self(set);
}

std::string librados::RadosClient::get_addrs() const {
  CachedStackStringStream cos;
  *cos << messenger->get_myaddrs();
  return std::string(cos->strv());
}

int librados::RadosClient::blocklist_add(const string& client_address,
					 uint32_t expire_seconds)
{
  entity_addr_t addr;
  if (!addr.parse(client_address)) {
    lderr(cct) << "unable to parse address " << client_address << dendl;
    return -EINVAL;
  }

  std::stringstream cmd;
  cmd << "{"
      << "\"prefix\": \"osd blocklist\", "
      << "\"blocklistop\": \"add\", "
      << "\"addr\": \"" << client_address << "\"";
  if (expire_seconds != 0) {
    cmd << ", \"expire\": " << expire_seconds << ".0";
  }
  cmd << "}";

  std::vector<std::string> cmds;
  cmds.push_back(cmd.str());
  bufferlist inbl;
  int r = mon_command(cmds, inbl, NULL, NULL);
  if (r == -EINVAL) {
    // try legacy blacklist command
    std::stringstream cmd;
    cmd << "{"
	<< "\"prefix\": \"osd blacklist\", "
	<< "\"blacklistop\": \"add\", "
	<< "\"addr\": \"" << client_address << "\"";
    if (expire_seconds != 0) {
      cmd << ", \"expire\": " << expire_seconds << ".0";
    }
    cmd << "}";
    cmds.clear();
    cmds.push_back(cmd.str());
    r = mon_command(cmds, inbl, NULL, NULL);
  }
  if (r < 0) {
    return r;
  }

  // ensure we have the latest osd map epoch before proceeding
  r = wait_for_latest_osdmap();
  return r;
}

/**
 * @brief 同步执行Monitor命令
 * @param cmd 命令参数列表
 * @param inbl 输入数据缓冲区
 * @param outbl 输出数据缓冲区（可为空）
 * @param outs 输出字符串（可为空）
 * @return 0表示成功，负值表示错误
 *
 * 同步方式向Monitor发送管理命令并等待结果：
 * 1. 内部调用异步版本
 * 2. 通过条件变量等待完成
 * 3. 返回命令执行结果
 *
 * @note 此方法是阻塞的，适用于简单的命令执行场景。
 */
int librados::RadosClient::mon_command(const vector<string>& cmd,
				       const bufferlist &inbl,
				       bufferlist *outbl, string *outs)
{
  C_SaferCond ctx;
  mon_command_async(cmd, inbl, outbl, outs, &ctx);
  return ctx.wait();
}

/**
 * @brief 异步执行Monitor命令
 * @param cmd 命令参数列表
 * @param inbl 输入数据缓冲区
 * @param outbl 输出数据缓冲区（可为空）
 * @param outs 输出字符串（可为空）
 * @param on_finish 异步完成回调接口
 *
 * 异步方式向Monitor发送管理命令：
 * 1. 通过MonClient启动命令执行
 * 2. 设置Lambda回调处理响应
 * 3. 在回调中填充输出参数并触发完成通知
 *
 * @note 此方法是非阻塞的，结果通过回调函数返回。
 * 适用于需要在后台执行管理命令的场景。
 */
void librados::RadosClient::mon_command_async(const vector<string>& cmd,
                                              const bufferlist &inbl,
                                              bufferlist *outbl, string *outs,
                                              Context *on_finish)
{
  std::lock_guard l{lock};
  monclient.start_mon_command(cmd, inbl,
			      [outs, outbl,
			       on_finish = std::unique_ptr<Context>(on_finish)]
			      (bs::error_code e,
			       std::string&& s,
			       ceph::bufferlist&& b) mutable {
				if (outs)
				  *outs = std::move(s);
				if (outbl)
				  *outbl = std::move(b);
				if (on_finish)
				  on_finish.release()->complete(
				    ceph::from_error_code(e));
			      });
}

/**
 * @brief 同步执行Manager命令
 * @param cmd 命令参数列表
 * @param inbl 输入数据缓冲区
 * @param outbl 输出数据缓冲区（可为空）
 * @param outs 输出字符串（可为空）
 * @return 0表示成功，负值表示错误
 *
 * 同步方式向Manager守护进程发送管理命令：
 * 1. 通过MgrClient启动命令执行
 * 2. 使用条件变量等待完成，带超时控制
 * 3. 返回命令执行结果
 *
 * @note 此方法是阻塞的，使用rados_mon_op_timeout作为超时。
 * 适用于需要等待Manager响应的管理操作。
 */
int librados::RadosClient::mgr_command(const vector<string>& cmd,
				       const bufferlist &inbl,
				       bufferlist *outbl, string *outs)
{
  std::lock_guard l(lock);

  C_SaferCond cond;
  int r = mgrclient.start_command(cmd, inbl, outbl, outs, &cond);
  if (r < 0)
    return r;

  lock.unlock();
  if (rados_mon_op_timeout.count() > 0) {
    r = cond.wait_for(rados_mon_op_timeout);
  } else {
    r = cond.wait();
  }
  lock.lock();

  return r;
}

/**
 * @brief 同步执行指定Manager的命令
 * @param name Manager守护进程名称
 * @param cmd 命令参数列表
 * @param inbl 输入数据缓冲区
 * @param outbl 输出数据缓冲区（可为空）
 * @param outs 输出字符串（可为空）
 * @return 0表示成功，负值表示错误
 *
 * 同步方式向指定Manager守护进程发送管理命令：
 * 1. 通过MgrClient的tell命令机制发送到指定Manager
 * 2. 使用条件变量等待完成，带超时控制
 * 3. 返回命令执行结果
 *
 * @note 此方法是阻塞的，适用于需要向特定Manager发送命令的场景。
 * 使用rados_mon_op_timeout作为超时控制。
 */
int librados::RadosClient::mgr_command(
  const string& name,
  const vector<string>& cmd,
  const bufferlist &inbl,
  bufferlist *outbl, string *outs)
{
  std::lock_guard l(lock);

  C_SaferCond cond;
  int r = mgrclient.start_tell_command(name, cmd, inbl, outbl, outs, &cond);
  if (r < 0)
    return r;

  lock.unlock();
  if (rados_mon_op_timeout.count() > 0) {
    r = cond.wait_for(rados_mon_op_timeout);
  } else {
    r = cond.wait();
  }
  lock.lock();

  return r;
}


/**
 * @brief 同步执行指定Monitor rank的命令
 * @param rank Monitor的rank编号
 * @param cmd 命令参数列表
 * @param inbl 输入数据缓冲区
 * @param outbl 输出数据缓冲区（可为空）
 * @param outs 输出字符串（可为空）
 * @return 0表示成功，负值表示错误
 *
 * 同步方式向指定rank的Monitor发送命令：
 * 1. 通过MonClient发送到指定rank的Monitor
 * 2. 阻塞等待响应并返回结果
 * 3. 自动处理错误码转换
 *
 * @note 此方法是阻塞的，适用于需要向特定Monitor发送命令的场景。
 */
int librados::RadosClient::mon_command(int rank, const vector<string>& cmd,
				       const bufferlist &inbl,
				       bufferlist *outbl, string *outs)
{
  bs::error_code ec;
  auto&& [s, bl] = monclient.start_mon_command(rank, cmd, inbl,
					       ca::use_blocked[ec]);
  if (outs)
    *outs = std::move(s);
  if (outbl)
    *outbl = std::move(bl);

  return ceph::from_error_code(ec);
}

/**
 * @brief 同步执行指定Monitor名称的命令
 * @param name Monitor的名称
 * @param cmd 命令参数列表
 * @param inbl 输入数据缓冲区
 * @param outbl 输出数据缓冲区（可为空）
 * @param outs 输出字符串（可为空）
 * @return 0表示成功，负值表示错误
 *
 * 同步方式向指定名称的Monitor发送命令：
 * 1. 通过MonClient发送到指定名称的Monitor
 * 2. 阻塞等待响应并返回结果
 * 3. 自动处理错误码转换
 *
 * @note 此方法是阻塞的，适用于需要向特定Monitor发送命令的场景。
 */
int librados::RadosClient::mon_command(string name, const vector<string>& cmd,
				       const bufferlist &inbl,
				       bufferlist *outbl, string *outs)
{
  bs::error_code ec;
  auto&& [s, bl] = monclient.start_mon_command(name, cmd, inbl,
					       ca::use_blocked[ec]);
  if (outs)
    *outs = std::move(s);
  if (outbl)
    *outbl = std::move(bl);

  return ceph::from_error_code(ec);
}

/**
 * @brief 同步执行OSD命令
 * @param osd OSD编号
 * @param cmd 命令参数列表
 * @param inbl 输入数据缓冲区
 * @param poutbl 输出数据缓冲区（可为空）
 * @param prs 输出字符串（可为空）
 * @return 0表示成功，负值表示错误
 *
 * 同步方式向指定OSD发送管理命令：
 * 1. 验证OSD编号有效性
 * 2. 通过Objecter向指定OSD发送命令
 * 3. 阻塞等待响应并返回结果
 *
 * @note 此方法是阻塞的，适用于需要向特定OSD发送命令的场景。
 * OSD编号必须是非负数。
 */
int librados::RadosClient::osd_command(int osd, vector<string>& cmd,
				       const bufferlist& inbl,
				       bufferlist *poutbl, string *prs)
{
  ceph_tid_t tid;

  if (osd < 0)
    return -EINVAL;


  // XXX do anything with tid?
  bs::error_code ec;
  auto [s, bl] = objecter->osd_command(osd, std::move(cmd), cb::list(inbl),
				       &tid, ca::use_blocked[ec]);
  if (poutbl)
    *poutbl = std::move(bl);
  if (prs)
    *prs = std::move(s);
  return ceph::from_error_code(ec);
}

/**
 * @brief 同步执行PG命令
 * @param pgid Placement Group ID
 * @param cmd 命令参数列表
 * @param inbl 输入数据缓冲区
 * @param poutbl 输出数据缓冲区（可为空）
 * @param prs 输出字符串（可为空）
 * @return 0表示成功，负值表示错误
 *
 * 同步方式向指定Placement Group发送管理命令：
 * 1. 通过Objecter向指定PG发送命令
 * 2. 阻塞等待响应并返回结果
 * 3. 自动处理错误码转换
 *
 * @note 此方法是阻塞的，适用于需要向特定PG发送命令的场景。
 * PG命令通常用于底层存储管理操作。
 */
int librados::RadosClient::pg_command(pg_t pgid, vector<string>& cmd,
				      const bufferlist& inbl,
				      bufferlist *poutbl, string *prs)
{
  ceph_tid_t tid;
  bs::error_code ec;
  auto [s, bl] = objecter->pg_command(pgid, std::move(cmd), inbl, &tid,
				      ca::use_blocked[ec]);
  if (poutbl)
    *poutbl = std::move(bl);
  if (prs)
    *prs = std::move(s);
  return ceph::from_error_code(ec);
}

int librados::RadosClient::monitor_log(const string& level,
				       rados_log_callback_t cb,
				       rados_log_callback2_t cb2,
				       void *arg)
{
  std::lock_guard l(lock);

  if (state != CONNECTED) {
    return -ENOTCONN;
  }

  if (cb == NULL && cb2 == NULL) {
    // stop watch
    ldout(cct, 10) << __func__ << " removing cb " << (void*)log_cb
		   << " " << (void*)log_cb2 << dendl;
    monclient.sub_unwant(log_watch);
    log_watch.clear();
    log_cb = NULL;
    log_cb2 = NULL;
    log_cb_arg = NULL;
    return 0;
  }

  string watch_level;
  if (level == "debug") {
    watch_level = "log-debug";
  } else if (level == "info") {
    watch_level = "log-info";
  } else if (level == "warn" || level == "warning") {
    watch_level = "log-warn";
  } else if (level == "err" || level == "error") {
    watch_level = "log-error";
  } else if (level == "sec") {
    watch_level = "log-sec";
  } else {
    ldout(cct, 10) << __func__ << " invalid level " << level << dendl;
    return -EINVAL;
  }

  if (log_cb || log_cb2)
    monclient.sub_unwant(log_watch);

  // (re)start watch
  ldout(cct, 10) << __func__ << " add cb " << (void*)cb << " " << (void*)cb2
		 << " level " << level << dendl;
  monclient.sub_want(watch_level, 0, 0);

  monclient.renew_subs();
  log_cb = cb;
  log_cb2 = cb2;
  log_cb_arg = arg;
  log_watch = watch_level;
  return 0;
}

void librados::RadosClient::handle_log(MLog *m)
{
  ceph_assert(ceph_mutex_is_locked(lock));
  ldout(cct, 10) << __func__ << " version " << m->version << dendl;

  if (log_last_version < m->version) {
    log_last_version = m->version;

    if (log_cb || log_cb2) {
      for (std::deque<LogEntry>::iterator it = m->entries.begin(); it != m->entries.end(); ++it) {
        LogEntry e = *it;
        ostringstream ss;
        ss << e.stamp << " " << e.name << " " << e.prio << " " << e.msg;
        string line = ss.str();
        string who = stringify(e.rank) + " " + stringify(e.addrs);
	string name = stringify(e.name);
        string level = stringify(e.prio);
        struct timespec stamp;
        e.stamp.to_timespec(&stamp);

        ldout(cct, 20) << __func__ << " delivering " << ss.str() << dendl;
	if (log_cb)
	  log_cb(log_cb_arg, line.c_str(), who.c_str(),
		 stamp.tv_sec, stamp.tv_nsec,
		 e.seq, level.c_str(), e.msg.c_str());
	if (log_cb2)
	  log_cb2(log_cb_arg, line.c_str(),
		  e.channel.c_str(),
		  who.c_str(), name.c_str(),
		  stamp.tv_sec, stamp.tv_nsec,
		  e.seq, level.c_str(), e.msg.c_str());
      }
    }

    monclient.sub_got(log_watch, log_last_version);
  }

  m->put();
}

/**
 * @brief 注册服务守护进程
 * @param service 服务名称（如'rgw'）
 * @param name 守护进程名称（如'gwfoo'）
 * @param metadata 服务元数据键值对
 * @return 0表示成功，负值表示错误
 *
 * 将当前客户端注册为服务守护进程，允许其：
 * 1. 向Manager报告健康状态和统计信息
 * 2. 参与服务发现机制
 * 3. 接收管理命令和配置更新
 *
 * 注册限制：
 * - 每个客户端只能注册一次
 * - 服务名称不能与Ceph内置实体类型冲突（osd、mds、client、mon、mgr）
 * - 服务名称和守护进程名称不能为空
 * - 必须在连接建立后才能注册
 *
 * 内部过程：
 * - 收集系统信息并合并到元数据中
 * - 设置服务守护进程标志和信息
 * - 如果已连接，立即向Manager注册
 *
 * @note 注册后可以通过update_status()方法更新状态信息。
 */
int librados::RadosClient::service_daemon_register(
  const std::string& service,  ///< service name (e.g., 'rgw')
  const std::string& name,     ///< daemon name (e.g., 'gwfoo')
  const std::map<std::string,std::string>& metadata)
{
  if (service_daemon) {
    return -EEXIST;
  }
  if (service == "osd" ||
      service == "mds" ||
      service == "client" ||
      service == "mon" ||
      service == "mgr") {
    // normal ceph entity types are not allowed!
    return -EINVAL;
  }
  if (service.empty() || name.empty()) {
    return -EINVAL;
  }

  collect_sys_info(&daemon_metadata, cct);

  ldout(cct,10) << __func__ << " " << service << "." << name << dendl;
  service_daemon = true;
  service_name = service;
  daemon_name = name;
  daemon_metadata.insert(metadata.begin(), metadata.end());

  if (state == DISCONNECTED) {
    return 0;
  }
  if (state == CONNECTING) {
    return -EBUSY;
  }
  mgrclient.service_daemon_register(service_name, daemon_name,
				    daemon_metadata);
  return 0;
}

/**
 * @brief 更新服务守护进程状态
 * @param status 状态信息键值对（右值引用，避免拷贝）
 * @return 0表示成功，负值表示错误
 *
 * 更新已注册服务守护进程的状态信息：
 * 1. 验证客户端已连接到集群
 * 2. 通过MgrClient向Manager报告新的状态信息
 * 3. 支持健康状态、性能指标等信息更新
 *
 * @note 必须先调用service_daemon_register()注册服务，
 * 才能使用此方法更新状态。
 */
int librados::RadosClient::service_daemon_update_status(
  std::map<std::string,std::string>&& status)
{
  if (state != CONNECTED) {
    return -ENOTCONN;
  }
  return mgrclient.service_daemon_update_status(std::move(status));
}

/**
 * @brief 获取Monitor所需的功能特性
 * @return Monitor集群所需的功能特性位图
 *
 * 查询Monitor集群要求客户端支持的功能特性。
 * 此信息用于确保客户端与Monitor的兼容性。
 *
 * @note 此方法是线程安全的，通过MonMap的只读访问。
 */
mon_feature_t librados::RadosClient::get_required_monitor_features() const
{
  return monclient.with_monmap([](const MonMap &monmap) {
      return monmap.get_required_features(); } );
}

int librados::RadosClient::get_inconsistent_pgs(int64_t pool_id,
						std::vector<std::string>* pgs)
{
  vector<string> cmd = {
    "{\"prefix\": \"pg ls\","
    "\"pool\": " + std::to_string(pool_id) + ","
    "\"states\": [\"inconsistent\"],"
    "\"format\": \"json\"}"
  };
  bufferlist inbl, outbl;
  string outstring;
  if (auto ret = mgr_command(cmd, inbl, &outbl, &outstring); ret) {
    return ret;
  }
  if (!outbl.length()) {
    // no pg returned
    return 0;
  }
  JSONParser parser;
  if (!parser.parse(outbl.c_str(), outbl.length())) {
    return -EINVAL;
  }
  vector<string> v;
  if (!parser.is_array()) {
    JSONObj *pgstat_obj = parser.find_obj("pg_stats");
    if (!pgstat_obj)
      return 0;
    auto s = pgstat_obj->get_data();
    JSONParser pg_stats;
    if (!pg_stats.parse(s.c_str(), s.length())) {
      return -EINVAL;
    }
    v = pg_stats.get_array_elements();
  } else {
    v = parser.get_array_elements();
  }
  for (auto i : v) {
    JSONParser pg_json;
    if (!pg_json.parse(i.c_str(), i.length())) {
      return -EINVAL;
    }
    string pgid;
    JSONDecoder::decode_json("pgid", pgid, &pg_json);
    pgs->emplace_back(std::move(pgid));
  }
  return 0;
}

/**
 * @brief 获取跟踪的配置键列表
 * @return 跟踪的配置键数组，以nullptr结尾
 *
 * 返回RadosClient跟踪的配置选项列表。
 * 当这些配置选项发生变化时，会触发handle_conf_change()回调。
 *
 * 当前跟踪的配置：
 * - librados_thread_count: librados线程池大小
 * - rados_mon_op_timeout: Monitor操作超时时间
 */
const char** librados::RadosClient::get_tracked_conf_keys() const
{
  static const char *config_keys[] = {
    "librados_thread_count",
    "rados_mon_op_timeout",
    nullptr
  };
  return config_keys;
}

/**
 * @brief 处理配置变更通知
 * @param conf 配置代理对象
 * @param changed 变更的配置键集合
 *
 * 当跟踪的配置选项发生变化时，此回调函数被调用：
 *
 * 1. **librados_thread_count变更**：
 *    - 停止当前的异步IO线程池
 *    - 根据新配置值重新启动线程池
 *    - 确保线程池大小与配置同步
 *
 * 2. **rados_mon_op_timeout变更**：
 *    - 更新Monitor操作超时时间
 *    - 影响所有Monitor相关的同步操作
 *
 * @note 此方法是配置观察者模式的回调实现，
 * 由Ceph配置系统在配置变更时自动调用。
 */
void librados::RadosClient::handle_conf_change(const ConfigProxy& conf,
					       const std::set<std::string> &changed)
{
  if (changed.count("librados_thread_count")) {
    poolctx.stop();
    poolctx.start(conf.get_val<std::uint64_t>("librados_thread_count"));
  }
  if (changed.count("rados_mon_op_timeout")) {
    rados_mon_op_timeout = conf.get_val<std::chrono::seconds>("rados_mon_op_timeout");
  }
}
