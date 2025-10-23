// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

/**
 * @file ceph_mds.cc
 * @brief Ceph Metadata Server (MDS) 守护进程主程序
 * 
 * 本文件是 Ceph 元数据服务器守护进程的主入口程序，负责启动和管理 Ceph MDS 服务。
 * MDS 是 CephFS 的核心组件，负责管理文件系统的元数据，包括目录结构、文件属性、
 * 权限信息等，为客户端提供 POSIX 兼容的文件系统接口。
 * 
 * 主要功能：
 * - 启动 MDS 守护进程，提供元数据服务
 * - 管理文件系统命名空间和目录树结构
 * - 处理客户端的元数据操作请求（创建、删除、重命名等）
 * - 维护文件系统的一致性和完整性
 * - 支持多 MDS 集群，提供高可用性和负载均衡
 * - 处理文件系统快照和克隆操作
 * - 管理客户端会话和缓存一致性
 * 
 * 核心组件：
 * - MDSDaemon：MDS 核心实现类，管理元数据服务
 * - Messenger：网络通信层，处理客户端和集群间通信
 * - MonClient：与 MON 的通信客户端
 * - 异步 I/O 上下文池：提供异步处理能力
 * 
 * 工作模式：
 * - 正常服务模式：提供元数据服务
 * - 热备模式：作为备用 MDS 等待接管
 * - 恢复模式：从故障中恢复元数据状态
 * 
 * 使用示例：
 * - 启动服务：ceph-mds -i <name> -m <monitor-ip:port>
 * - 调试模式：ceph-mds -i <name> --debug_mds 10
 * 
 * 特性：
 * - 支持 POSIX 兼容的文件系统接口
 * - 提供高可用性和故障恢复能力
 * - 支持多 MDS 集群和负载均衡
 * - 提供丰富的监控和管理接口
 * - 支持 NUMA 优化和性能调优
 * - 支持文件系统快照和克隆
 */

// 标准 C 库头文件
#include <sys/types.h>    // 系统数据类型定义
#include <sys/stat.h>     // 文件状态信息
#include <fcntl.h>        // 文件控制操作
#include <pthread.h>      // POSIX 线程库

// 标准 C++ 库头文件
#include <iostream>       // 输入输出流
#include <string>         // 字符串类

// Ceph 异步组件
#include "common/async/context_pool.h" // 异步上下文池

// Ceph 核心组件
#include "include/ceph_features.h" // Ceph 功能特性定义
#include "include/compat.h"        // 兼容性支持
#include "include/random.h"        // 随机数生成

// Ceph 通用组件
#include "common/config.h"         // 配置管理系统
#include "common/strtol.h"         // 字符串转换工具
#include "common/numa.h"           // NUMA 支持
#include "common/Timer.h"          // 定时器
#include "common/ceph_argparse.h"  // 命令行参数解析
#include "common/pick_address.h"   // 地址选择工具
#include "common/Preforker.h"      // 预分叉处理器

// Ceph 监控组件
#include "mon/MonMap.h"    // MON 映射表
#include "mon/MonClient.h" // MON 客户端

// Ceph MDS 核心组件
#include "mds/MDSDaemon.h" // MDS 守护进程实现

// Ceph 消息传递组件
#include "msg/Messenger.h" // 网络消息传递器

// Ceph 全局组件
#include "global/global_init.h"    // 全局初始化
#include "global/signal_handler.h" // 信号处理器
#include "global/pidfile.h"        // PID 文件管理

// Ceph 认证组件
#include "auth/KeyRing.h" // 密钥环管理

// Ceph 工具组件
#include "perfglue/heap_profiler.h" // 堆内存分析器
#include "include/ceph_assert.h"   // 断言宏定义

// 调试输出上下文和子系统定义
#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_mds

// 标准命名空间使用声明
using std::cerr;
using std::cout;
using std::vector;

/**
 * @brief 显示 ceph-mds 使用帮助信息
 * 
 * 输出 ceph-mds 命令的完整使用说明，包括所有支持的选项和参数。
 * 帮助信息包括基本用法、监控器连接、调试选项等。
 */
static void usage()
{
  cout << "usage: ceph-mds -i <ID> [flags]\n"
       << "  -m monitorip:port\n"
       << "        connect to monitor at given address\n"
       << "  --debug_mds n\n"
       << "        debug MDS level (e.g. 10)\n"
       << std::endl;
  generic_server_usage();
}


/**
 * @brief 全局 MDS 守护进程实例指针
 * 
 * 用于信号处理器访问 MDS 实例，实现信号处理功能。
 * 在程序启动时创建，在程序结束时清理。
 */
MDSDaemon *mds = NULL;

/**
 * @brief MDS 进程信号处理函数
 * 
 * 处理发送给 MDS 进程的各种信号，如 SIGTERM、SIGINT、SIGHUP 等。
 * 将信号转发给 MDS 实例进行具体处理。
 * 
 * @param signum 信号编号
 *               - SIGTERM: 终止信号，用于优雅关闭
 *               - SIGINT:  中断信号（Ctrl+C），用于强制关闭
 *               - SIGHUP:  挂起信号，用于重新加载配置
 */
static void handle_mds_signal(int signum)
{
  if (mds)
    mds->handle_signal(signum);
}

/**
 * @brief Ceph MDS 守护进程主函数
 * 
 * 这是 ceph-mds 程序的主入口点，负责启动和管理 Ceph 元数据服务器守护进程。
 * MDS 是 CephFS 的核心组件，负责管理文件系统的元数据。
 * 
 * 主要执行流程：
 * 1. 参数解析：解析命令行参数和配置文件
 * 2. 初始化：创建全局上下文、NUMA 优化、消息传递器
 * 3. 网络设置：绑定地址、设置通信策略
 * 4. MDS 启动：创建 MDS 实例并启动服务
 * 5. 主循环：处理元数据请求和事件
 * 6. 清理退出：清理资源和优雅关闭
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 0 程序成功执行
 * @return 非0 程序执行失败，返回相应的错误码
 * 
 * @note MDS 是 CephFS 的元数据服务器，负责管理文件系统命名空间
 */
int main(int argc, const char **argv)
{
  // ==================== 第一阶段：程序初始化和参数解析 ====================
  
  // 设置线程名称，便于调试和监控
  ceph_pthread_setname(pthread_self(), "ceph-mds");

  // 将命令行参数转换为向量格式，便于处理
  auto args = argv_to_vec(argc, argv);
  
  // 检查基本参数
  if (args.empty()) {
    cerr << argv[0] << ": -h or --help for usage" << std::endl;
    exit(1);
  }
  
  // 检查是否需要显示使用帮助
  if (ceph_argparse_need_usage(args)) {
    usage();
    exit(0);
  }

  // ==================== 第二阶段：全局初始化和配置 ====================
  
  // 初始化全局 Ceph 上下文
  auto cct = global_init(NULL, args,
			 CEPH_ENTITY_TYPE_MDS, CODE_ENVIRONMENT_DAEMON, 0);
  ceph_heap_profiler_init();  // 初始化堆分析器

  // ==================== 第三阶段：NUMA 优化设置 ====================
  
  // 配置 NUMA 节点亲和性，优化性能
  int numa_node = g_conf().get_val<int64_t>("mds_numa_node");
  size_t numa_cpu_set_size = 0;
  cpu_set_t numa_cpu_set;
  if (numa_node >= 0) {
    int r = get_numa_node_cpu_set(numa_node, &numa_cpu_set_size, &numa_cpu_set);
    if (r < 0) {
      dout(1) << __func__ << " unable to determine mds numa node " << numa_node
              << " CPUs" << dendl;
      numa_node = -1;
    } else {
      r = set_cpu_affinity_all_threads(numa_cpu_set_size, &numa_cpu_set);
      if (r < 0) {
        derr << __func__ << " failed to set numa affinity: " << cpp_strerror(r)
        << dendl;
      }
    }
  } else {
    dout(1) << __func__ << " not setting numa affinity" << dendl;
  }
  // ==================== 第四阶段：MDS 特定参数解析 ====================
  
  // 处理 MDS 特定的命令行参数
  std::string val, action;
  for (std::vector<const char*>::iterator i = args.begin(); i != args.end(); ) {
    if (ceph_argparse_double_dash(args, i)) {
      // 遇到 "--" 分隔符，停止处理 Ceph 特定选项
      break;
    }
    else if (ceph_argparse_witharg(args, i, &val, "--hot-standby", (char*)NULL)) {
      // 热备模式选项（已废弃）
      dout(0) << "--hot-standby is obsolete and has no effect" << dendl;
    }
    else {
      // 未识别的参数
      derr << "Error: can't understand argument: " << *i << "\n" << dendl;
      exit(1);
    }
  }

  // ==================== 第五阶段：守护进程处理和地址选择 ====================
  
  // 创建预分叉处理器，用于守护进程模式
  Preforker forker;

  // 选择网络地址
  entity_addrvec_t addrs;
  pick_addresses(g_ceph_context, CEPH_PICK_ADDRESS_PUBLIC, &addrs);

  // ==================== 第六阶段：MDS 身份验证 ====================
  
  // 验证 MDS 实例名称
  if (g_conf()->name.has_default_id()) {
    derr << "must specify '-i name' with the ceph-mds instance name" << dendl;
    exit(1);
  }

  // 验证 MDS ID 格式（不能以数字开头）
  if (g_conf()->name.get_id().empty() ||
      (g_conf()->name.get_id()[0] >= '0' && g_conf()->name.get_id()[0] <= '9')) {
    derr << "MDS id '" << g_conf()->name << "' is invalid. "
      "MDS names may not start with a numeric digit." << dendl;
    exit(1);
  }

  // ==================== 第七阶段：守护进程化处理 ====================
  
  // 执行预分叉处理（如果支持）
  if (global_init_prefork(g_ceph_context) >= 0) {
    std::string err;
    int r = forker.prefork(err);
    if (r < 0) {
      cerr << err << std::endl;
      return r;
    }
    if (forker.is_parent()) {
      // 父进程：等待子进程完成
      if (forker.parent_wait(err) != 0) {
        return -ENXIO;
      }
      return 0;
    }
    // 子进程：继续执行
    global_init_postfork_start(g_ceph_context);
  }
  
  // 完成通用初始化和目录切换
  common_init_finish(g_ceph_context);
  global_init_chdir(g_ceph_context);

  // ==================== 第八阶段：消息传递器创建和配置 ====================
  
  // 创建消息传递器
  std::string public_msgr_type = g_conf()->ms_public_type.empty() ? g_conf().get_val<std::string>("ms_type") : g_conf()->ms_public_type;
  Messenger *msgr = Messenger::create(g_ceph_context, public_msgr_type,
				      entity_name_t::MDS(-1), "mds",
				      Messenger::get_random_nonce());
  if (!msgr)
    forker.exit(1);
  
  // 设置 MDS 协议
  msgr->set_cluster_protocol(CEPH_MDS_PROTOCOL);

  // 输出启动信息
  cout << "starting " << g_conf()->name << " at " << msgr->get_myaddrs()
       << std::endl;
  
  // 设置必需的功能特性
  uint64_t required = CEPH_FEATURE_OSDREPLYMUX;

  // 设置消息传递器策略
  msgr->set_default_policy(Messenger::Policy::lossy_client(required));
  msgr->set_policy(entity_name_t::TYPE_MON,
                   Messenger::Policy::lossy_client(CEPH_FEATURE_UID |
                                                   CEPH_FEATURE_PGID64));
  msgr->set_policy(entity_name_t::TYPE_MDS,
                   Messenger::Policy::lossless_peer(CEPH_FEATURE_UID));
  msgr->set_policy(entity_name_t::TYPE_CLIENT,
                   Messenger::Policy::stateful_server(0));

  // 绑定消息传递器到网络地址
  int r = msgr->bindv(addrs);
  if (r < 0)
    forker.exit(1);

  // ==================== 第九阶段：信号处理和 MON 客户端初始化 ====================
  
  // 设置信号处理器（在守护进程化/分叉后）
  init_async_signal_handler();
  register_async_signal_handler(SIGHUP, sighup_handler);
  
  // 创建 MON 客户端
  ceph::async::io_context_pool ctxpool(2);
  MonClient mc(g_ceph_context, ctxpool);
  if (mc.build_initial_monmap() < 0)
    forker.exit(1);
  global_init_chdir(g_ceph_context);

  // 启动消息传递器
  msgr->start();

  // ==================== 第十阶段：MDS 实例创建和初始化 ====================
  
  // 创建 MDS 守护进程实例
  mds = new MDSDaemon(g_conf()->name.get_id().c_str(), msgr, &mc, ctxpool);

  // 保存原始参数，以便重新启动时使用
  mds->orig_argc = argc;
  mds->orig_argv = argv;

  // ==================== 第十一阶段：守护进程化和 MDS 启动 ====================
  
  // 守护进程化处理
  if (g_conf()->daemonize) {
    global_init_postfork_finish(g_ceph_context);
    forker.daemonize();
  }

  // 初始化 MDS
  r = mds->init();
  if (r < 0) {
    msgr->wait();
    goto shutdown;
  }

  register_async_signal_handler_oneshot(SIGINT, handle_mds_signal);
  register_async_signal_handler_oneshot(SIGTERM, handle_mds_signal);

  if (g_conf()->inject_early_sigterm)
    kill(getpid(), SIGTERM);

  msgr->wait();

  unregister_async_signal_handler(SIGHUP, sighup_handler);
  unregister_async_signal_handler(SIGINT, handle_mds_signal);
  unregister_async_signal_handler(SIGTERM, handle_mds_signal);
  shutdown_async_signal_handler();

 shutdown:
  ctxpool.stop();
  // yuck: grab the mds lock, so we can be sure that whoever in *mds
  // called shutdown finishes what they were doing.
  mds->mds_lock.lock();
  mds->mds_lock.unlock();

  pidfile_remove();

  // only delete if it was a clean shutdown (to aid memory leak
  // detection, etc.).  don't bother if it was a suicide.
  if (mds->is_clean_shutdown()) {
    delete mds;
    delete msgr;
  }

  // cd on exit, so that gmon.out (if any) goes into a separate directory for each node.
  char s[20];
  snprintf(s, sizeof(s), "gmon/%d", getpid());
  if ((mkdir(s, 0755) == 0) && (chdir(s) == 0)) {
    cerr << "ceph-mds: gmon.out should be in " << s << std::endl;
  }

  return 0;
}
