/**
 * @file RadosClient.h
 * @brief RADOS客户端核心实现头文件
 *
 * 本文件定义了RadosClient类，这是librados库的核心实现，负责：
 * - 与Ceph集群的连接管理
 * - Monitor和OSD通信协调
 * - 异步IO操作的执行和响应处理
 * - 连接状态监控和错误恢复
 * - 配置管理和动态更新
 *
 * RadosClient是连接librados公共API和底层Ceph集群通信的桥梁，
 * 提供了完整的分布式对象存储客户端功能实现。
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
#ifndef CEPH_LIBRADOS_RADOSCLIENT_H
#define CEPH_LIBRADOS_RADOSCLIENT_H

#include <functional>
#include <memory>
#include <string>

#include "msg/Dispatcher.h"

#include "common/async/context_pool.h"
#include "common/config_fwd.h"
#include "common/Cond.h"
#include "common/ceph_mutex.h"
#include "common/ceph_time.h"
#include "common/config_obs.h"
#include "include/common_fwd.h"
#include "include/rados/librados.h"
#include "include/rados/librados.hpp"
#include "mon/MonClient.h"
#include "mgr/MgrClient.h"

#include "IoCtxImpl.h"

struct Context;
class Message;
class MLog;
class Messenger;
class AioCompletionImpl;

namespace neorados { namespace detail { class RadosClient; }}

/**
 * @brief RADOS客户端核心实现类
 *
 * RadosClient是librados库的核心实现类，继承自Dispatcher和md_config_obs_t，
 * 负责管理与Ceph集群的完整生命周期和通信流程。
 *
 * 主要功能：
 * - 集群连接建立和维护
 * - Monitor和OSD通信协调
 * - 异步IO操作执行和响应处理
 * - 连接状态监控和故障恢复
 * - 配置动态更新和热重载
 * - 服务守护进程注册和管理
 *
 * 设计特点：
 * - 多继承架构：消息分发 + 配置观察
 * - 异步优先：基于Boost.Asio的事件驱动模型
 * - 线程池管理：专用线程池处理异步回调
 * - 状态机管理：明确的连接状态转换
 */
class librados::RadosClient : public Dispatcher,
			      public md_config_obs_t
{
  friend neorados::detail::RadosClient;

public:
  using Dispatcher::cct;

private:
  /**
   * @brief CephContext智能指针和自定义删除器
   *
   * 使用自定义删除器确保CephContext的正确清理，
   * 避免静态初始化顺序问题。
   */
  std::unique_ptr<CephContext,
		  std::function<void(CephContext*)>> cct_deleter;

public:
  /**
   * @brief 配置代理引用
   *
   * 提供对Ceph配置系统的只读访问，
   * 用于获取和监控配置参数变化。
   */
  const ConfigProxy& conf{cct->_conf};

  /**
   * @brief 异步IO上下文线程池
   *
   * 基于Boost.Asio的线程池，负责处理：
   * - 异步IO操作的完成回调
   * - 网络消息的异步处理
   * - 定时器和延时操作
   *
   * 线程数量由librados_thread_count配置参数控制，
   * 默认创建2个工作线程。
   */
  ceph::async::io_context_pool poolctx;

private:
  /**
   * @brief 连接状态枚举
   *
   * 定义客户端与集群连接的生命周期状态：
   * - DISCONNECTED: 未连接状态
   * - CONNECTING: 连接建立中
   * - CONNECTED: 已连接并正常工作
   */
  enum {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
  } state{DISCONNECTED};

  /**
   * @brief Monitor客户端
   *
   * 负责与Ceph Monitor集群的通信：
   * - 集群状态监控和映射获取
   * - 认证和授权处理
   * - 集群配置和元数据查询
   */
  MonClient monclient{cct, poolctx};

  /**
   * @brief Manager客户端
   *
   * 负责与Ceph Manager的通信：
   * - 集群统计信息收集
   * - 服务守护进程管理
   * - 管理命令执行
   */
  MgrClient mgrclient{cct, nullptr, &monclient.monmap};

  /**
   * @brief 网络信使
   *
   * 负责底层网络通信：
   * - 消息编码和解码
   * - 连接管理和复用
   * - 网络协议处理
   */
  Messenger *messenger{nullptr};

  /**
   * @brief 客户端实例ID
   *
   * 唯一标识当前librados客户端实例，
   * 用于集群中的客户端追踪和调试。
   */
  uint64_t instance_id{0};

  bool _dispatch(Message *m);
  bool ms_dispatch(Message *m) override;

  void ms_handle_connect(Connection *con) override;
  bool ms_handle_reset(Connection *con) override;
  void ms_handle_remote_reset(Connection *con) override;
  bool ms_handle_refused(Connection *con) override;

  Objecter *objecter{nullptr};

  ceph::mutex lock = ceph::make_mutex("librados::RadosClient::lock");
  ceph::condition_variable cond;
  int refcnt{1};

  version_t log_last_version{0};
  rados_log_callback_t log_cb{nullptr};
  rados_log_callback2_t log_cb2{nullptr};
  void *log_cb_arg{nullptr};
  std::string log_watch;

  bool service_daemon = false;
  std::string daemon_name, service_name;
  std::map<std::string,std::string> daemon_metadata;
  ceph::timespan rados_mon_op_timeout{};

  int wait_for_osdmap();

public:
  boost::asio::strand<boost::asio::io_context::executor_type>
      finish_strand{poolctx.get_executor()};

  /**
   * @brief 构造函数
   * @param cct Ceph上下文指针
   *
   * 初始化RadosClient实例，建立与Ceph集群通信的基础设施。
   */
  explicit RadosClient(CephContext *cct);

  /**
   * @brief 析构函数
   *
   * 清理所有资源，关闭与集群的连接，确保优雅退出。
   */
  ~RadosClient() override;

  /**
   * @brief 探测Monitor状态
   * @param mon_id Monitor ID
   * @param result 返回结果字符串
   * @return 0表示成功，负值表示错误
   *
   * 向指定Monitor发送ping请求，检查其响应状态。
   */
  int ping_monitor(std::string mon_id, std::string *result);

  /**
   * @brief 连接到Ceph集群
   * @return 0表示成功，负值表示错误
   *
   * 建立与Ceph集群的完整连接，包括：
   * - Monitor连接建立和认证
   * - OSD映射获取和缓存
   * - 服务守护进程注册（如果适用）
   */
  int connect();

  /**
   * @brief 关闭与集群的连接
   *
   * 优雅地关闭所有连接，清理资源，确保数据一致性。
   */
  void shutdown();

  /**
   * @brief 刷新监视器
   * @return 0表示成功，负值表示错误
   *
   * 等待所有监视操作完成，确保监视器状态同步。
   */
  int watch_flush();

  /**
   * @brief 异步刷新监视器
   * @param c 异步完成回调
   * @return 0表示成功，负值表示错误
   *
   * 异步等待所有监视操作完成，非阻塞版本。
   */
  int async_watch_flush(AioCompletionImpl *c);

  /**
   * @brief 获取客户端实例ID
   * @return 客户端实例的唯一标识符
   *
   * 返回当前librados客户端的实例ID，用于调试和追踪。
   */
  uint64_t get_instance_id();

  /**
   * @brief 获取最小兼容OSD版本
   * @param require_osd_release 返回所需的最小OSD版本
   * @return 0表示成功，负值表示错误
   *
   * 查询集群所需的最小OSD版本，确保客户端兼容性。
   */
  int get_min_compatible_osd(int8_t* require_osd_release);

  /**
   * @brief 获取最小兼容客户端版本
   * @param min_compat_client 返回最小兼容客户端版本
   * @param require_min_compat_client 返回所需的最小兼容客户端版本
   * @return 0表示成功，负值表示错误
   *
   * 查询客户端与集群的版本兼容性要求。
   */
  int get_min_compatible_client(int8_t* min_compat_client,
                                int8_t* require_min_compat_client);

  /**
   * @brief 等待最新的OSD映射
   * @return 0表示成功，负值表示错误
   *
   * 等待集群OSD映射更新到最新版本，确保操作的一致性。
   */
  int wait_for_latest_osdmap();

  /**
   * @brief 创建IO上下文（按名称）
   * @param name 池名称
   * @param io 返回创建的IoCtx实现指针
   * @return 0表示成功，负值表示错误
   *
   * 根据池名称创建IO上下文，用于后续的对象操作。
   */
  int create_ioctx(const char *name, IoCtxImpl **io);

  /**
   * @brief 创建IO上下文（按ID）
   * @param pool_id 池ID
   * @param io 返回创建的IoCtx实现指针
   * @return 0表示成功，负值表示错误
   *
   * 根据池ID创建IO上下文，用于后续的对象操作。
   */
  int create_ioctx(int64_t, IoCtxImpl **io);

  /**
   * @brief 获取文件系统ID
   * @param s 返回FSID字符串
   * @return 0表示成功，负值表示错误
   *
   * 获取当前连接的Ceph文件系统的唯一标识符。
   */
  int get_fsid(std::string *s);

  /**
   * @brief 根据名称查找池ID
   * @param name 池名称
   * @return 池ID，负值表示错误
   *
   * 根据池名称查询对应的池ID。
   */
  int64_t lookup_pool(const char *name);

  /**
   * @brief 检查池是否需要对齐
   * @param pool_id 池ID
   * @return true表示需要对齐，false表示不需要
   *
   * 检查指定池是否要求对象对齐访问。
   */
  bool pool_requires_alignment(int64_t pool_id);

  /**
   * @brief 检查池是否需要对齐（带返回值）
   * @param pool_id 池ID
   * @param req 返回是否需要对齐
   * @return 0表示成功，负值表示错误
   *
   * 检查指定池的对齐要求，返回具体结果。
   */
  int pool_requires_alignment2(int64_t pool_id, bool *req);

  /**
   * @brief 获取池所需对齐大小
   * @param pool_id 池ID
   * @return 对齐大小，负值表示错误
   *
   * 获取指定池要求的对象对齐粒度。
   */
  uint64_t pool_required_alignment(int64_t pool_id);

  /**
   * @brief 获取池所需对齐大小（带返回值）
   * @param pool_id 池ID
   * @param alignment 返回对齐大小
   * @return 0表示成功，负值表示错误
   *
   * 获取指定池的对齐要求，返回具体对齐粒度。
   */
  int pool_required_alignment2(int64_t pool_id, uint64_t *alignment);

  /**
   * @brief 根据ID获取池名称
   * @param pool_id 池ID
   * @param name 返回池名称
   * @param wait_latest_map 是否等待最新的映射
   * @return 0表示成功，负值表示错误
   *
   * 根据池ID查询对应的池名称。
   */
  int pool_get_name(uint64_t pool_id, std::string *name,
		    bool wait_latest_map = false);

  /**
   * @brief 列出所有池
   * @param ls 返回池列表（ID和名称对）
   * @return 0表示成功，负值表示错误
   *
   * 获取集群中所有存储池的列表。
   */
  int pool_list(std::list<std::pair<int64_t, std::string> >& ls);

  /**
   * @brief 获取池统计信息
   * @param ls 池名称列表
   * @param result 返回统计结果
   * @param per_pool 返回每个池的统计标志
   * @return 0表示成功，负值表示错误
   *
   * 获取指定池或所有池的统计信息。
   */
  int get_pool_stats(std::list<std::string>& ls, std::map<std::string,::pool_stat_t> *result,
    bool *per_pool);

  /**
   * @brief 获取文件系统统计信息
   * @param result 返回文件系统统计信息
   * @return 0表示成功，负值表示错误
   *
   * 获取整个文件系统的统计信息。
   */
  int get_fs_stats(ceph_statfs& result);

  /**
   * @brief 检查池是否处于自管理快照模式
   * @param pool 池名称
   * @return 0表示成功，负值表示错误
   *
   * 检查指定池是否启用了自管理快照模式。
   */
  int pool_is_in_selfmanaged_snaps_mode(const std::string& pool);

  /**
   * @brief 创建存储池
   * @param name 池名称（引用，会被修改为实际名称）
   * @param crush_rule CRUSH规则ID，-1表示使用默认规则
   * @return 0表示成功，负值表示错误
   *
   * 创建新的存储池。Monitor会按以下优先级选择CRUSH规则：
   * - a) osd pool default crush replicated rule
   * - b) 第一个可用的规则
   * - c) 如果找不到合适规则则报错
   */
  int pool_create(std::string& name, int16_t crush_rule=-1);

  /**
   * @brief 异步创建存储池
   * @param name 池名称（引用，会被修改为实际名称）
   * @param c 异步完成回调
   * @param crush_rule CRUSH规则ID，-1表示使用默认规则
   * @return 0表示成功，负值表示错误
   *
   * 异步版本的池创建操作，非阻塞执行。
   */
  int pool_create_async(std::string& name, PoolAsyncCompletionImpl *c,
			int16_t crush_rule=-1);

  /**
   * @brief 获取池的基础层ID
   * @param pool_id 池ID
   * @param base_tier 返回基础层ID
   * @return 0表示成功，负值表示错误
   *
   * 获取指定池的缓存层配置信息。
   */
  int pool_get_base_tier(int64_t pool_id, int64_t* base_tier);

  /**
   * @brief 删除存储池
   * @param name 池名称
   * @return 0表示成功，负值表示错误
   *
   * 删除指定的存储池及其所有对象。
   */
  int pool_delete(const char *name);

  /**
   * @brief 异步删除存储池
   * @param name 池名称
   * @param c 异步完成回调
   * @return 0表示成功，负值表示错误
   *
   * 异步版本的池删除操作，非阻塞执行。
   */
  int pool_delete_async(const char *name, PoolAsyncCompletionImpl *c);

  /**
   * @brief 添加客户端到黑名单
   * @param client_address 客户端地址
   * @param expire_seconds 过期时间（秒）
   * @return 0表示成功，负值表示错误
   *
   * 将指定客户端添加到集群黑名单，阻止其访问。
   */
  int blocklist_add(const std::string& client_address, uint32_t expire_seconds);

  /**
   * @brief 执行Monitor命令
   * @param cmd 命令参数向量
   * @param inbl 输入数据
   * @param outbl 返回的输出数据
   * @param outs 返回的状态消息
   * @return 0表示成功，负值表示错误
   *
   * 向Monitor发送管理命令并获取响应。
   */
  int mon_command(const std::vector<std::string>& cmd, const bufferlist &inbl,
	          bufferlist *outbl, std::string *outs);

  /**
   * @brief 异步执行Monitor命令
   * @param cmd 命令参数向量
   * @param inbl 输入数据
   * @param outbl 返回的输出数据
   * @param outs 返回的状态消息
   * @param on_finish 完成回调
   *
   * 异步版本的Monitor命令执行，非阻塞操作。
   */
  void mon_command_async(const std::vector<std::string>& cmd, const bufferlist &inbl,
                         bufferlist *outbl, std::string *outs, Context *on_finish);

  /**
   * @brief 执行指定Monitor的命令
   * @param rank Monitor序号
   * @param cmd 命令参数向量
   * @param inbl 输入数据
   * @param outbl 返回的输出数据
   * @param outs 返回的状态消息
   * @return 0表示成功，负值表示错误
   *
   * 向特定Monitor实例发送命令。
   */
  int mon_command(int rank,
		  const std::vector<std::string>& cmd, const bufferlist &inbl,
	          bufferlist *outbl, std::string *outs);

  /**
   * @brief 执行指定Monitor的命令（按名称）
   * @param name Monitor名称
   * @param cmd 命令参数向量
   * @param inbl 输入数据
   * @param outbl 返回的输出数据
   * @param outs 返回的状态消息
   * @return 0表示成功，负值表示错误
   *
   * 向指定名称的Monitor发送命令。
   */
  int mon_command(std::string name,
		  const std::vector<std::string>& cmd, const bufferlist &inbl,
	          bufferlist *outbl, std::string *outs);

  /**
   * @brief 执行Manager命令
   * @param cmd 命令参数向量
   * @param inbl 输入数据
   * @param outbl 返回的输出数据
   * @param outs 返回的状态消息
   * @return 0表示成功，负值表示错误
   *
   * 向Ceph Manager发送管理命令。
   */
  int mgr_command(const std::vector<std::string>& cmd, const bufferlist &inbl,
	          bufferlist *outbl, std::string *outs);

  /**
   * @brief 执行指定Manager的命令
   * @param name Manager名称
   * @param cmd 命令参数向量
   * @param inbl 输入数据
   * @param outbl 返回的输出数据
   * @param outs 返回的状态消息
   * @return 0表示成功，负值表示错误
   *
   * 向指定Manager实例发送命令。
   */
  int mgr_command(
    const std::string& name,
    const std::vector<std::string>& cmd, const bufferlist &inbl,
    bufferlist *outbl, std::string *outs);

  /**
   * @brief 执行OSD命令
   * @param osd OSD编号
   * @param cmd 命令参数向量
   * @param inbl 输入数据
   * @param poutbl 返回的输出数据
   * @param prs 返回的状态消息
   * @return 0表示成功，负值表示错误
   *
   * 向指定OSD发送管理命令。
   */
  int osd_command(int osd, std::vector<std::string>& cmd, const bufferlist& inbl,
                  bufferlist *poutbl, std::string *prs);

  /**
   * @brief 执行PG命令
   * @param pgid PG ID
   * @param cmd 命令参数向量
   * @param inbl 输入数据
   * @param poutbl 返回的输出数据
   * @param prs 返回的状态消息
   * @return 0表示成功，负值表示错误
   *
   * 向指定归置组发送命令。
   */
  int pg_command(pg_t pgid, std::vector<std::string>& cmd, const bufferlist& inbl,
	         bufferlist *poutbl, std::string *prs);

  /**
   * @brief 处理日志消息
   * @param m 日志消息
   *
   * 处理来自Monitor的日志消息，用于日志转发和监控。
   */
  void handle_log(MLog *m);

  /**
   * @brief 注册日志监视器
   * @param level 日志级别
   * @param cb 日志回调函数（旧版本）
   * @param cb2 日志回调函数（新版本）
   * @param arg 回调函数参数
   * @return 0表示成功，负值表示错误
   *
   * 注册日志监视器，接收来自集群的日志消息。
   */
  int monitor_log(const std::string& level, rados_log_callback_t cb,
		  rados_log_callback2_t cb2, void *arg);

  /**
   * @brief 增加引用计数
   *
   * 增加RadosClient对象的引用计数，用于多线程安全管理。
   */
  void get();

  /**
   * @brief 减少引用计数
   * @return true表示引用计数为0，对象可以被销毁
   *
   * 减少RadosClient对象的引用计数，支持RAII资源管理。
   */
  bool put();

  /**
   * @brief 将自己加入黑名单
   * @param set true表示加入黑名单，false表示移出黑名单
   *
   * 将当前客户端自身添加到集群黑名单，防止自连接。
   */
  void blocklist_self(bool set);

  /**
   * @brief 获取连接地址信息
   * @return 当前连接的地址字符串
   *
   * 返回客户端连接到集群的所有地址信息。
   */
  std::string get_addrs() const;

  /**
   * @brief 注册服务守护进程
   * @param service 服务名称（如'rgw'）
   * @param name 守护进程名称（如'gwfoo'）
   * @param metadata 守护进程静态元数据
   * @return 0表示成功，负值表示错误
   *
   * 向集群注册当前进程为服务守护进程，支持服务发现和管理。
   */
  int service_daemon_register(
    const std::string& service,  ///< service name (e.g., 'rgw')
    const std::string& name,     ///< daemon name (e.g., 'gwfoo')
    const std::map<std::string,std::string>& metadata); ///< static metadata about daemon

  /**
   * @brief 更新服务守护进程状态
   * @param status 服务状态信息
   * @return 0表示成功，负值表示错误
   *
   * 更新已注册服务守护进程的运行状态信息。
   */
  int service_daemon_update_status(
    std::map<std::string,std::string>&& status);

  /**
   * @brief 获取所需Monitor特性
   * @return 所需的Monitor特性掩码
   *
   * 查询客户端所需的Monitor特性集合，确保兼容性。
   */
  mon_feature_t get_required_monitor_features() const;

  /**
   * @brief 获取不一致的PG列表
   * @param pool_id 池ID，-1表示所有池
   * @param pgs 返回不一致的PG列表
   * @return 0表示成功，负值表示错误
   *
   * 查询集群中处于不一致状态的归置组。
   */
  int get_inconsistent_pgs(int64_t pool_id, std::vector<std::string>* pgs);

  /**
   * @brief 获取追踪的配置键列表
   * @return 配置键名数组，以nullptr结尾
   *
   * 返回需要追踪的配置参数名称列表，用于配置变更监控。
   */
  const char** get_tracked_conf_keys() const override;

  /**
   * @brief 处理配置变更
   * @param conf 配置代理
   * @param changed 变更的配置键集合
   *
   * 当配置参数发生变化时被调用，支持热配置更新。
   * 实现了md_config_obs_t接口的配置观察者模式。
   */
  void handle_conf_change(const ConfigProxy& conf,
                          const std::set <std::string> &changed) override;
};

#endif
