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
 * @file ceph_mon.cc
 * @brief Ceph Monitor (MON) 守护进程主程序
 * 
 * 本文件是 Ceph 集群监控服务的主入口程序，负责维护集群的元数据和状态一致性。
 * Monitor 是 Ceph 集群的核心组件，提供 Paxos 协议实现，确保集群状态的一致性。
 * 
 * 主要功能：
 * - 维护集群元数据（MonMap、OSDMap、PGMap、MDSMap 等）
 * - 实现 Paxos 一致性协议，确保集群状态同步
 * - 处理集群成员管理、故障检测和自动恢复
 * - 管理认证、授权和配置信息
 * - 提供集群拓扑和状态查询服务
 * - 协调 OSD、MDS 等组件的状态变更
 * 
 * 核心组件：
 * - Monitor：MON 核心实现类，管理集群状态和 Paxos 协议
 * - MonitorDBStore：MON 数据存储后端，持久化集群元数据
 * - Messenger：网络通信层，处理集群间通信
 * - MonMap：MON 映射表，记录集群拓扑和成员信息
 * 
 * 工作模式：
 * 1. 创建文件系统模式（--mkfs）：初始化 MON 存储，创建初始集群配置
 * 2. 正常服务模式：启动 MON 服务，参与集群状态管理
 * 3. 维护模式：支持压缩存储、强制同步等维护操作
 * 
 * 使用示例：
 * - 创建文件系统：ceph-mon --mkfs -i <id> --mon-data <path>
 * - 启动服务：ceph-mon -i <id> --mon-data <path>
 * - 压缩存储：ceph-mon -i <id> --mon-data <path> --compact
 * - 强制同步：ceph-mon -i <id> --mon-data <path> --force-sync --yes-i-really-mean-it
 * 
 * 安全特性：
 * - 支持多 MON 集群，提供高可用性
 * - 实现 Paxos 协议，确保强一致性
 * - 支持故障检测和自动恢复
 * - 提供数据完整性校验和版本控制
 */


// 标准 C 库头文件
#include <sys/types.h>    // 系统数据类型定义
#include <sys/stat.h>     // 文件状态信息
#include <fcntl.h>        // 文件控制操作

// 标准 C++ 库头文件
#include <iostream>       // 输入输出流
#include <string>         // 字符串类

// Ceph 通用组件
#include "common/config.h"        // 配置管理系统
#include "include/ceph_features.h" // Ceph 功能特性定义

// Ceph Monitor 核心组件
#include "mon/MonMap.h"           // MON 映射表，记录集群拓扑
#include "mon/Monitor.h"          // Monitor 核心实现类
#include "mon/MonitorDBStore.h"   // MON 数据存储后端
#include "mon/MonClient.h"        // MON 客户端

// Ceph 消息传递组件
#include "msg/Messenger.h"        // 网络消息传递器

// Ceph 兼容性组件
#include "include/CompatSet.h"    // 兼容性集合

// Ceph 通用工具组件
#include "common/ceph_argparse.h" // 命令行参数解析
#include "common/pick_address.h"  // 地址选择工具
#include "common/Throttle.h"      // 流量限制器
#include "common/Timer.h"         // 定时器
#include "common/errno.h"         // 错误码定义
#include "common/Preforker.h"     // 预分叉处理器

// Ceph 全局组件
#include "global/global_init.h"   // 全局初始化
#include "global/signal_handler.h" // 信号处理器

// 性能分析组件
#include "perfglue/heap_profiler.h" // 堆内存分析器

// Ceph 断言组件
#include "include/ceph_assert.h"  // 断言宏定义

// 调试输出子系统定义
#define dout_subsys ceph_subsys_mon

// 标准命名空间使用声明
using std::cerr;
using std::cout;
using std::list;
using std::map;
using std::ostringstream;
using std::string;
using std::vector;

// Ceph 命名空间使用声明
using ceph::bufferlist;
using ceph::decode;
using ceph::encode;
using ceph::JSONFormatter;

/**
 * @brief 全局 Monitor 实例指针
 * 
 * 用于信号处理器访问 Monitor 实例，实现信号处理功能。
 * 在程序启动时创建，在程序结束时清理。
 */
Monitor *mon = NULL;

/**
 * @brief MON 进程信号处理函数
 * 
 * 处理发送给 MON 进程的各种信号，如 SIGTERM、SIGINT、SIGHUP 等。
 * 将信号转发给 Monitor 实例进行具体处理。
 * 
 * @param signum 信号编号
 *               - SIGTERM: 终止信号，用于优雅关闭
 *               - SIGINT:  中断信号（Ctrl+C），用于强制关闭
 *               - SIGHUP:  挂起信号，用于重新加载配置
 */
void handle_mon_signal(int signum)
{
  if (mon)
    mon->handle_signal(signum);
}


/**
 * @brief 从存储中获取 MonMap（MON 映射表）
 * 
 * MonMap 记录了集群中所有 MON 节点的拓扑信息和状态。
 * 函数按优先级顺序查找 MonMap，确保获取到最新且有效的映射表。
 * 
 * MonMap 存储位置优先级：
 * 1. 'monmap:<latest_version_no>' - 最新提交的 MonMap（最高优先级）
 * 2. 'mon_sync:temp_newer_monmap' - 临时存储的新版本 MonMap（用于引导）
 * 3. 'mon_sync:latest_monmap' - 上次同步备份的 MonMap
 * 4. 'mkfs:monmap' - mkfs 命令创建的初始 MonMap（最低优先级）
 * 
 * @param store MON 数据存储后端，用于读取 MonMap 数据
 * @param bl 输出参数，存储解码后的 MonMap 数据
 * @return 0 成功获取 MonMap
 * @return -ENOENT 未找到任何有效的 MonMap
 * 
 * @note 函数会检查临时存储的新版本 MonMap，如果其版本号更高，
 *       则使用新版本替代已提交的版本。
 */
int obtain_monmap(MonitorDBStore &store, bufferlist &bl)
{
  dout(10) << __func__ << dendl;
  /*
   * the monmap may be in one of three places:
   *  'mon_sync:temp_newer_monmap' - stashed newer map for bootstrap
   *  'monmap:<latest_version_no>' - the monmap we'd really like to have
   *  'mon_sync:latest_monmap'     - last monmap backed up for the last sync
   *  'mkfs:monmap'                - a monmap resulting from mkfs
   */

  if (store.exists("monmap", "last_committed")) {
    version_t latest_ver = store.get("monmap", "last_committed");
    if (store.exists("monmap", latest_ver)) {
      int err = store.get("monmap", latest_ver, bl);
      ceph_assert(err == 0);
      ceph_assert(bl.length() > 0);
      dout(10) << __func__ << " read last committed monmap ver "
               << latest_ver << dendl;

      // see if there is stashed newer map (see bootstrap())
      if (store.exists("mon_sync", "temp_newer_monmap")) {
	bufferlist bl2;
	int err = store.get("mon_sync", "temp_newer_monmap", bl2);
	ceph_assert(err == 0);
	ceph_assert(bl2.length() > 0);
	MonMap b;
	b.decode(bl2);
	if (b.get_epoch() > latest_ver) {
	  dout(10) << __func__ << " using stashed monmap " << b.get_epoch()
		   << " instead" << dendl;
	  bl = std::move(bl2);
	} else {
	  dout(10) << __func__ << " ignoring stashed monmap " << b.get_epoch()
		   << dendl;
	}
      }
      return 0;
    }
  }

  if (store.exists("mon_sync", "in_sync")
      || store.exists("mon_sync", "force_sync")) {
    dout(10) << __func__ << " detected aborted sync" << dendl;
    if (store.exists("mon_sync", "latest_monmap")) {
      int err = store.get("mon_sync", "latest_monmap", bl);
      ceph_assert(err == 0);
      ceph_assert(bl.length() > 0);
      dout(10) << __func__ << " read backup monmap" << dendl;
      return 0;
    }
  }

  if (store.exists("mon_sync", "temp_newer_monmap")) {
    dout(10) << __func__ << " found temp_newer_monmap" << dendl;
    int err = store.get("mon_sync", "temp_newer_monmap", bl);
    ceph_assert(err == 0);
    ceph_assert(bl.length() > 0);
    return 0;
  }

  if (store.exists("mkfs", "monmap")) {
    dout(10) << __func__ << " found mkfs monmap" << dendl;
    int err = store.get("mkfs", "monmap", bl);
    ceph_assert(err == 0);
    ceph_assert(bl.length() > 0);
    return 0;
  }

  derr << __func__ << " unable to find a monmap" << dendl;
  return -ENOENT;
}

/**
 * @brief 检查 MON 数据目录是否存在
 * 
 * 验证配置的 MON 数据目录是否可访问，这是 MON 启动的前提条件。
 * 
 * @return 0 目录存在且可访问
 * @return -errno 目录不存在或访问错误
 *         - ENOENT: 目录不存在
 *         - 其他错误码: 权限不足或其他系统错误
 */
int check_mon_data_exists()
{
  string mon_data = g_conf()->mon_data;
  struct stat buf;
  if (::stat(mon_data.c_str(), &buf)) {
    if (errno != ENOENT) {
      derr << "stat(" << mon_data << ") " << cpp_strerror(errno) << dendl;
    }
    return -errno;
  }
  return 0;
}

/**
 * @brief 检查 MON 数据目录是否为空
 * 
 * 验证 MON 数据目录是否为空，用于判断是否需要执行 mkfs 操作。
 * 空目录表示尚未初始化 MON 存储，需要先运行 mkfs 命令。
 * 
 * 检查规则：
 * - 忽略 "." 和 ".." 目录项
 * - 忽略 "kv_backend" 目录（存储后端相关）
 * - 其他任何文件或目录都视为非空
 * 
 * @return 0 目录为空，可以执行 mkfs
 * @return -ENOTEMPTY 目录不为空，可能已存在 MON 实例
 * @return -errno 访问目录时发生错误
 * 
 * @note 此函数用于防止意外覆盖已存在的 MON 数据
 */
int check_mon_data_empty()
{
  string mon_data = g_conf()->mon_data;

  DIR *dir = ::opendir(mon_data.c_str());
  if (!dir) {
    derr << "opendir(" << mon_data << ") " << cpp_strerror(errno) << dendl;
    return -errno;
  }
  int code = 0;
  struct dirent *de = nullptr;
  errno = 0;
  while ((de = ::readdir(dir))) {
    if (string(".") != de->d_name &&
	string("..") != de->d_name &&
	string("kv_backend") != de->d_name) {
      code = -ENOTEMPTY;
      break;
    }
  }
  if (!de && errno) {
    derr << "readdir(" << mon_data << ") " << cpp_strerror(errno) << dendl;
    code = -errno;
  }

  ::closedir(dir);

  return code;
}

/**
 * @brief 显示 ceph-mon 使用帮助信息
 * 
 * 输出 ceph-mon 命令的完整使用说明，包括所有支持的选项和参数。
 * 帮助信息包括基本用法、调试选项、维护操作等。
 */
static void usage()
{
  cout << "usage: ceph-mon -i <ID> [flags]\n"
       << "  --debug_mon n\n"
       << "        debug monitor level (e.g. 10)\n"
       << "  --mkfs\n"
       << "        build fresh monitor fs\n"
       << "  --force-sync\n"
       << "        force a sync from another mon by wiping local data (BE CAREFUL)\n"
       << "  --yes-i-really-mean-it\n"
       << "        mandatory safeguard for --force-sync\n"
       << "  --compact\n"
       << "        compact the monitor store\n"
       << "  --osdmap <filename>\n"
       << "        only used when --mkfs is provided: load the osdmap from <filename>\n"
       << "  --inject-monmap <filename>\n"
       << "        write the <filename> monmap to the local monitor store and exit\n"
       << "  --extract-monmap <filename>\n"
       << "        extract the monmap from the local monitor store and exit\n"
       << "  --mon-data <directory>\n"
       << "        where the mon store and keyring are located\n"
       << "  --set-crush-location <bucket>=<foo>"
       << "        sets monitor's crush bucket location (only for stretch mode)"
       << std::endl;
  generic_server_usage();
}

/**
 * @brief 根据单个地址创建 MON 地址向量
 * 
 * 将单个网络地址转换为包含多种协议类型的地址向量，
 * 确保 MON 能够支持不同版本的客户端连接。
 * 
 * 地址转换规则：
 * - 端口为 0：同时支持 MSGR2 和 LEGACY 协议
 * - 端口为 CEPH_MON_PORT_LEGACY：仅支持 LEGACY 协议
 * - 类型为 TYPE_ANY：转换为 MSGR2 协议
 * - 其他情况：保持原地址不变
 * 
 * @param a 输入的网络地址
 * @return entity_addrvec_t 包含多个协议版本的地址向量
 * 
 * @note 此函数确保向后兼容性，支持不同版本的 Ceph 客户端
 */
entity_addrvec_t make_mon_addrs(entity_addr_t a)
{
  entity_addrvec_t addrs;
  if (a.get_port() == 0) {
    a.set_type(entity_addr_t::TYPE_MSGR2);
    a.set_port(CEPH_MON_PORT_IANA);
    addrs.v.push_back(a);
    a.set_type(entity_addr_t::TYPE_LEGACY);
    a.set_port(CEPH_MON_PORT_LEGACY);
    addrs.v.push_back(a);
  } else if (a.get_port() == CEPH_MON_PORT_LEGACY) {
    a.set_type(entity_addr_t::TYPE_LEGACY);
    addrs.v.push_back(a);
  } else if (a.get_type() == entity_addr_t::TYPE_ANY) {
    a.set_type(entity_addr_t::TYPE_MSGR2);
    addrs.v.push_back(a);
  } else {
    addrs.v.push_back(a);
  }
  return addrs;
}

/**
 * @brief Ceph Monitor 守护进程主函数
 * 
 * 这是 ceph-mon 程序的主入口点，负责启动和管理 Ceph 集群的监控服务。
 * 支持多种运行模式：文件系统创建、正常服务、维护操作等。
 * 
 * 主要执行流程：
 * 1. 参数解析：解析命令行参数和配置文件
 * 2. 特殊操作：处理 --mkfs、--compact、--force-sync 等维护操作
 * 3. 初始化：创建存储后端、网络通信、Monitor 实例
 * 4. 启动服务：绑定网络地址、启动消息传递、进入主循环
 * 5. 运行监控：处理集群状态变更、维护一致性
 * 6. 优雅关闭：清理资源、保存状态
 * 
 * 支持的操作模式：
 * - 创建文件系统（--mkfs）：初始化 MON 存储，创建初始集群配置
 * - 正常服务模式：启动 MON 服务，参与集群状态管理
 * - 维护模式：压缩存储、强制同步、注入/提取 MonMap
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 0 程序成功执行
 * @return 非0 程序执行失败，返回相应的错误码
 * 
 * @note 程序支持守护进程模式，可通过配置启用后台运行
 */
int main(int argc, const char **argv)
{
  // ==================== 第一阶段：程序初始化和参数准备 ====================
  
  // 设置进程名，避免重启后进程名变成 "exe"
  ceph_pthread_setname(pthread_self(), "ceph-mon");
  
  dout(0) << "ceph-mon: starting with " << argc << " arguments" << dendl;

  int err;

  // 声明命令行参数变量
  bool mkfs = false;           // 是否创建文件系统
  bool compact = false;        // 是否压缩存储
  bool force_sync = false;     // 是否强制同步
  bool yes_really = false;     // 确认强制同步操作
  std::string osdmapfn, inject_monmap, extract_monmap, crush_loc;

  // ==================== 第二阶段：命令行参数解析 ====================
  
  auto args = argv_to_vec(argc, argv);
  dout(10) << "ceph-mon: parsed " << args.size() << " arguments" << dendl;
  
  // 检查基本参数
  if (args.empty()) {
    cerr << argv[0] << ": -h or --help for usage" << std::endl;
    exit(1);
  }
  if (ceph_argparse_need_usage(args)) {
    usage();
    exit(0);
  }

  // ==================== 第三阶段：配置初始化和标志设置 ====================
  
  // 设置 MON 特定的默认配置值
  // 这些选项也被 OSD 使用，所以不能修改全局默认值
  // 用户定义的选项会覆盖这些默认值
  
  map<string,string> defaults = {
    { "keyring", "$mon_data/keyring" },  // 默认密钥环路径
  };

  int flags = 0;
  {
    vector<const char*> args_copy = args;
    std::string val;
    for (std::vector<const char*>::iterator i = args_copy.begin();
	 i != args_copy.end(); ) {
      if (ceph_argparse_double_dash(args_copy, i)) {
	break;
      } else if (ceph_argparse_flag(args_copy, i, "--mkfs", (char*)NULL)) {
	flags |= CINIT_FLAG_NO_DAEMON_ACTIONS;
      } else if (ceph_argparse_witharg(args_copy, i, &val, "--inject_monmap", (char*)NULL)) {
	flags |= CINIT_FLAG_NO_DAEMON_ACTIONS;
      } else if (ceph_argparse_witharg(args_copy, i, &val, "--extract-monmap", (char*)NULL)) {
	flags |= CINIT_FLAG_NO_DAEMON_ACTIONS;
      } else {
	++i;
      }
    }
  }

  // 启动期间不从 MON 集群获取配置
  flags |= CINIT_FLAG_NO_MON_CONFIG;

  // ==================== 第四阶段：全局初始化和配置解析 ====================
  
  // 全局初始化：创建 CephContext，解析配置
  dout(10) << "ceph-mon: initializing global context" << dendl;
  auto cct = global_init(&defaults, args,
			 CEPH_ENTITY_TYPE_MON, CODE_ENVIRONMENT_DAEMON,
			 flags);
  ceph_heap_profiler_init();  // 初始化堆分析器
  dout(10) << "ceph-mon: global context initialized" << dendl;

  // ==================== 第五阶段：详细参数解析和验证 ====================
  
  std::string val;
  dout(10) << "ceph-mon: parsing command line arguments" << dendl;
  for (std::vector<const char*>::iterator i = args.begin(); i != args.end(); ) {
    if (ceph_argparse_double_dash(args, i)) {
      // 遇到 "--" 分隔符，停止处理 Ceph 特定选项
      break;
    } else if (ceph_argparse_flag(args, i, "--mkfs", (char*)NULL)) {
      // 创建文件系统模式
      mkfs = true;
      dout(10) << "ceph-mon: mkfs flag detected" << dendl;
    } else if (ceph_argparse_flag(args, i, "--compact", (char*)NULL)) {
      // 压缩存储模式
      compact = true;
      dout(10) << "ceph-mon: compact flag detected" << dendl;
    } else if (ceph_argparse_flag(args, i, "--force-sync", (char*)NULL)) {
      // 强制同步模式
      force_sync = true;
      dout(10) << "ceph-mon: force-sync flag detected" << dendl;
    } else if (ceph_argparse_flag(args, i, "--yes-i-really-mean-it", (char*)NULL)) {
      // 确认强制同步操作
      yes_really = true;
      dout(10) << "ceph-mon: yes-i-really-mean-it flag detected" << dendl;
    } else if (ceph_argparse_witharg(args, i, &val, "--osdmap", (char*)NULL)) {
      // OSD 映射文件路径
      osdmapfn = val;
      dout(10) << "ceph-mon: osdmap file: " << osdmapfn << dendl;
    } else if (ceph_argparse_witharg(args, i, &val, "--inject_monmap", (char*)NULL)) {
      // 注入 MonMap 文件路径
      inject_monmap = val;
      dout(10) << "ceph-mon: inject monmap file: " << inject_monmap << dendl;
    } else if (ceph_argparse_witharg(args, i, &val, "--extract-monmap", (char*)NULL)) {
      // 提取 MonMap 文件路径
      extract_monmap = val;
      dout(10) << "ceph-mon: extract monmap file: " << extract_monmap << dendl;
    } else if (ceph_argparse_witharg(args, i, &val, "--set-crush-location", (char*)NULL)) {
      // CRUSH 位置设置
      crush_loc = val;
      dout(10) << "ceph-mon: crush location: " << crush_loc << dendl;
    } else {
      // 未识别的参数，跳过
      ++i;
    }
  }
  // 检查未处理的参数
  if (!args.empty()) {
    cerr << "too many arguments: " << args << std::endl;
    exit(1);
  }

  // ==================== 第六阶段：参数验证和安全性检查 ====================
  
  // 验证强制同步操作的安全性
  if (force_sync && !yes_really) {
    cerr << "are you SURE you want to force a sync?  this will erase local data and may\n"
	 << "break your mon cluster.  pass --yes-i-really-mean-it if you do." << std::endl;
    exit(1);
  }

  // 验证必要的配置参数
  if (g_conf()->mon_data.empty()) {
    cerr << "must specify '--mon-data=foo' data path" << std::endl;
    exit(1);
  }
  dout(10) << "ceph-mon: mon_data path: " << g_conf()->mon_data << dendl;

  if (g_conf()->name.get_id().empty()) {
    cerr << "must specify id (--id <id> or --name mon.<id>)" << std::endl;
    exit(1);
  }
  dout(10) << "ceph-mon: monitor name: " << g_conf()->name << dendl;

  // 创建 MON 数据存储后端
  MonitorDBStore store(g_conf()->mon_data);
  dout(10) << "ceph-mon: created MonitorDBStore for path: " << g_conf()->mon_data << dendl;

  // ==================== 第七阶段：创建 MON 文件系统 (mkfs) ====================
  if (mkfs) {
    dout(0) << "ceph-mon: starting mkfs operation" << dendl;

    // 检查并创建 MON 数据目录
    int err = check_mon_data_exists();
    if (err == -ENOENT) {
      if (::mkdir(g_conf()->mon_data.c_str(), 0755)) {
	derr << "mkdir(" << g_conf()->mon_data << ") : "
	     << cpp_strerror(errno) << dendl;
	exit(1);
      }
    } else if (err < 0) {
      derr << "error opening '" << g_conf()->mon_data << "': "
           << cpp_strerror(-err) << dendl;
      exit(-err);
    }

    // 检查数据目录是否为空
    err = check_mon_data_empty();
    if (err == -ENOTEMPTY) {
      // MON 可能已存在，告知用户并优雅退出
      derr << "'" << g_conf()->mon_data << "' already exists and is not empty"
           << ": monitor may already exist" << dendl;
      exit(0);
    } else if (err < 0) {
      derr << "error checking if '" << g_conf()->mon_data << "' is empty: "
           << cpp_strerror(-err) << dendl;
      exit(-err);
    }
    dout(10) << "ceph-mon: data directory is empty, proceeding with mkfs" << dendl;

    // resolve public_network -> public_addr
    dout(10) << "ceph-mon: resolving public network addresses" << dendl;
    pick_addresses(g_ceph_context, CEPH_PICK_ADDRESS_PUBLIC);

    dout(10) << "public_network " << g_conf()->public_network << dendl;
    dout(10) << "public_addr " << g_conf()->public_addr << dendl;
    dout(10) << "public_addrv " << g_conf()->public_addrv << dendl;

    common_init_finish(g_ceph_context);
    dout(10) << "ceph-mon: common initialization finished" << dendl;

    bufferlist monmapbl, osdmapbl;
    std::string error;
    MonMap monmap;
    dout(10) << "ceph-mon: loading or generating monmap" << dendl;

    // load or generate monmap
    const auto monmap_fn = g_conf().get_val<string>("monmap");
    if (monmap_fn.length()) {
      int err = monmapbl.read_file(monmap_fn.c_str(), &error);
      if (err < 0) {
	derr << argv[0] << ": error reading " << monmap_fn << ": " << error << dendl;
	exit(1);
      }
      try {
	monmap.decode(monmapbl);

	// always mark seed/mkfs monmap as epoch 0
	monmap.set_epoch(0);
      } catch (const ceph::buffer::error& e) {
	derr << argv[0] << ": error decoding monmap " << monmap_fn << ": " << e.what() << dendl;
	exit(1);
      }

      dout(1) << "imported monmap:\n";
      monmap.print(*_dout);
      *_dout << dendl;
      
    } else {
      ostringstream oss;
      int err = monmap.build_initial(g_ceph_context, true, oss);
      if (oss.tellp())
        derr << oss.str() << dendl;
      if (err < 0) {
	derr << argv[0] << ": warning: no initial monitors; must use admin socket to feed hints" << dendl;
      }

      dout(1) << "initial generated monmap:\n";
      monmap.print(*_dout);
      *_dout << dendl;

      // am i part of the initial quorum?
      if (monmap.contains(g_conf()->name.get_id())) {
	// hmm, make sure the ip listed exists on the current host?
	// maybe later.
      } else if (!g_conf()->public_addrv.empty()) {
	entity_addrvec_t av = g_conf()->public_addrv;
	string name;
	if (monmap.contains(av, &name)) {
	  monmap.rename(name, g_conf()->name.get_id());
	  dout(0) << argv[0] << ": renaming mon." << name << " " << av
		  << " to mon." << g_conf()->name.get_id() << dendl;
	}
      } else if (!g_conf()->public_addr.is_blank_ip()) {
	entity_addrvec_t av = make_mon_addrs(g_conf()->public_addr);
	string name;
	if (monmap.contains(av, &name)) {
	  monmap.rename(name, g_conf()->name.get_id());
	  dout(0) << argv[0] << ": renaming mon." << name << " " << av
		  << " to mon." << g_conf()->name.get_id() << dendl;
	}
      } else {
	// is a local address listed without a name?  if so, name myself.
	list<entity_addr_t> ls;
	monmap.list_addrs(ls);
	dout(0) << " monmap addrs are " << ls << ", checking if any are local"
		<< dendl;

	entity_addr_t local;
	if (have_local_addr(g_ceph_context, ls, &local)) {
	  dout(0) << " have local addr " << local << dendl;
	  string name;
	  local.set_type(entity_addr_t::TYPE_MSGR2);
	  if (!monmap.get_addr_name(local, name)) {
	    local.set_type(entity_addr_t::TYPE_LEGACY);
	    if (!monmap.get_addr_name(local, name)) {
	      dout(0) << "no local addresses appear in bootstrap monmap"
		      << dendl;
	    }
	  }
	  if (name.compare(0, 7, "noname-") == 0) {
	    dout(0) << argv[0] << ": mon." << name << " " << local
		    << " is local, renaming to mon." << g_conf()->name.get_id()
		    << dendl;
	    monmap.rename(name, g_conf()->name.get_id());
	  } else if (name.size()) {
	    dout(0) << argv[0] << ": mon." << name << " " << local
		    << " is local, but not 'noname-' + something; "
		    << "not assuming it's me" << dendl;
	  }
	} else {
	  dout(0) << " no local addrs match monmap" << dendl;
	}
      }
    }

    const auto fsid = g_conf().get_val<uuid_d>("fsid");
    if (!fsid.is_zero()) {
      monmap.fsid = fsid;
      dout(0) << argv[0] << ": set fsid to " << fsid << dendl;
    }
    
    if (monmap.fsid.is_zero()) {
      derr << argv[0] << ": generated monmap has no fsid; use '--fsid <uuid>'" << dendl;
      exit(10);
    }

    //monmap.print(cout);

    // osdmap
    if (osdmapfn.length()) {
      err = osdmapbl.read_file(osdmapfn.c_str(), &error);
      if (err < 0) {
	derr << argv[0] << ": error reading " << osdmapfn << ": "
	     << error << dendl;
	exit(1);
      }
    }

    // 创建并打开存储
    dout(10) << "ceph-mon: creating and opening monitor store" << dendl;
    ostringstream oss;
    int r = store.create_and_open(oss);
    if (oss.tellp())
      derr << oss.str() << dendl;
    if (r < 0) {
      derr << argv[0] << ": error opening mon data directory at '"
           << g_conf()->mon_data << "': " << cpp_strerror(r) << dendl;
      exit(1);
    }
    ceph_assert(r == 0);
    dout(10) << "ceph-mon: monitor store created successfully" << dendl;

    // 创建 Monitor 实例并执行 mkfs
    dout(10) << "ceph-mon: creating Monitor instance for mkfs" << dendl;
    Monitor mon(g_ceph_context, g_conf()->name.get_id(), &store, 0, 0, &monmap);
    r = mon.mkfs(osdmapbl);
    if (r < 0) {
      derr << argv[0] << ": error creating monfs: " << cpp_strerror(r) << dendl;
      exit(1);
    }
    store.close();
    dout(0) << argv[0] << ": created monfs at " << g_conf()->mon_data 
	    << " for " << g_conf()->name << dendl;
    dout(0) << "ceph-mon: mkfs operation completed successfully" << dendl;
    return 0;
  }

  // ==================== 第八阶段：正常启动 MON 服务 ====================
  dout(0) << "ceph-mon: starting normal monitor service" << dendl;
  
  // 检查 MON 数据目录是否存在
  err = check_mon_data_exists();
  if (err < 0 && err == -ENOENT) {
    derr << "monitor data directory at '" << g_conf()->mon_data << "'"
         << " does not exist: have you run 'mkfs'?" << dendl;
    exit(1);
  } else if (err < 0) {
    derr << "error accessing monitor data directory at '"
         << g_conf()->mon_data << "': " << cpp_strerror(-err) << dendl;
    exit(1);
  }
  dout(10) << "ceph-mon: monitor data directory exists" << dendl;

  err = check_mon_data_empty();
  if (err == 0) {
    derr << "monitor data directory at '" << g_conf()->mon_data
      << "' is empty: have you run 'mkfs'?" << dendl;
    exit(1);
  } else if (err < 0 && err != -ENOTEMPTY) {
    // we don't want an empty data dir by now
    derr << "error accessing '" << g_conf()->mon_data << "': "
         << cpp_strerror(-err) << dendl;
    exit(1);
  }
  dout(10) << "ceph-mon: monitor data directory is not empty" << dendl;

  {
    // 检查文件系统统计信息，如果可用空间严重不足则不启动
    dout(10) << "ceph-mon: checking filesystem statistics" << dendl;
    ceph_data_stats_t stats;
    int err = get_fs_stats(stats, g_conf()->mon_data.c_str());
    if (err < 0) {
      derr << "error checking monitor data's fs stats: " << cpp_strerror(err)
           << dendl;
      exit(-err);
    }
    dout(10) << "ceph-mon: filesystem available space: " << stats.avail_percent 
             << "% (" << byte_u_t(stats.byte_avail) << ")" << dendl;
    if (stats.avail_percent <= g_conf()->mon_data_avail_crit) {
      derr << "error: monitor data filesystem reached concerning levels of"
           << " available storage space (available: "
           << stats.avail_percent << "% " << byte_u_t(stats.byte_avail)
           << ")\nyou may adjust 'mon data avail crit' to a lower value"
           << " to make this go away (default: " << g_conf()->mon_data_avail_crit
           << "%)\n" << dendl;
      exit(ENOSPC);
    }
  }

  Preforker prefork;
  if (!(flags & CINIT_FLAG_NO_DAEMON_ACTIONS)) {
    dout(10) << "ceph-mon: initializing preforker for daemonization" << dendl;
    if (global_init_prefork(g_ceph_context) >= 0) {
      string err_msg;
      err = prefork.prefork(err_msg);
      if (err < 0) {
        derr << err_msg << dendl;
        prefork.exit(err);
      }
      if (prefork.is_parent()) {
        err = prefork.parent_wait(err_msg);
        if (err < 0)
          derr << err_msg << dendl;
        prefork.exit(err);
      }
      setsid();
      global_init_postfork_start(g_ceph_context);
      dout(10) << "ceph-mon: prefork completed, continuing in child process" << dendl;
    }
    common_init_finish(g_ceph_context);
    global_init_chdir(g_ceph_context);
    if (global_init_preload_erasure_code(g_ceph_context) < 0)
      prefork.exit(1);
    dout(10) << "ceph-mon: daemon initialization completed" << dendl;
  }

  // set up signal handlers, now that we've daemonized/forked.
  init_async_signal_handler();

  // make sure we aren't upgrading too fast
  {
    string val;
    int r = store.read_meta("min_mon_release", &val);
    if (r >= 0 && val.size()) {
      ceph_release_t from_release = ceph_release_from_name(val);
      ostringstream err;
      if (!can_upgrade_from(from_release, "min_mon_release", err)) {
	derr << err.str() << dendl;
	prefork.exit(1);
      }
    }
  }

  {
    ostringstream oss;
    err = store.open(oss);
    if (oss.tellp())
      derr << oss.str() << dendl;
    if (err < 0) {
      derr << "error opening mon data directory at '"
           << g_conf()->mon_data << "': " << cpp_strerror(err) << dendl;
      prefork.exit(1);
    }
  }

  bufferlist magicbl;
  err = store.get(Monitor::MONITOR_NAME, "magic", magicbl);
  if (err || !magicbl.length()) {
    derr << "unable to read magic from mon data" << dendl;
    prefork.exit(1);
  }
  string magic(magicbl.c_str(), magicbl.length()-1);  // ignore trailing \n
  if (strcmp(magic.c_str(), CEPH_MON_ONDISK_MAGIC)) {
    derr << "mon fs magic '" << magic << "' != current '" << CEPH_MON_ONDISK_MAGIC << "'" << dendl;
    prefork.exit(1);
  }

  err = Monitor::check_features(&store);
  if (err < 0) {
    derr << "error checking features: " << cpp_strerror(err) << dendl;
    prefork.exit(1);
  }

  // inject new monmap?
  if (!inject_monmap.empty()) {
    bufferlist bl;
    std::string error;
    int r = bl.read_file(inject_monmap.c_str(), &error);
    if (r) {
      derr << "unable to read monmap from " << inject_monmap << ": "
	   << error << dendl;
      prefork.exit(1);
    }

    // get next version
    version_t v = store.get("monmap", "last_committed");
    dout(0) << "last committed monmap epoch is " << v << ", injected map will be " << (v+1)
            << dendl;
    v++;

    // set the version
    MonMap tmp;
    tmp.decode(bl);
    if (tmp.get_epoch() != v) {
      dout(0) << "changing monmap epoch from " << tmp.get_epoch()
           << " to " << v << dendl;
      tmp.set_epoch(v);
    }
    bufferlist mapbl;
    tmp.encode(mapbl, CEPH_FEATURES_ALL);
    bufferlist final;
    encode(v, final);
    encode(mapbl, final);

    auto t(std::make_shared<MonitorDBStore::Transaction>());
    // save it
    t->put("monmap", v, mapbl);
    t->put("monmap", "latest", final);
    t->put("monmap", "last_committed", v);
    store.apply_transaction(t);

    dout(0) << "done." << dendl;
    prefork.exit(0);
  }

  // monmap?
  MonMap monmap;
  {
    // note that even if we don't find a viable monmap, we should go ahead
    // and try to build it up in the next if-else block.
    bufferlist mapbl;
    int err = obtain_monmap(store, mapbl);
    if (err >= 0) {
      try {
        monmap.decode(mapbl);
      } catch (const ceph::buffer::error& e) {
        derr << "can't decode monmap: " << e.what() << dendl;
      }
    } else {
      derr << "unable to obtain a monmap: " << cpp_strerror(err) << dendl;
    }

    dout(10) << __func__ << " monmap:\n";
    JSONFormatter jf(true);
    jf.dump_object("monmap", monmap);
    jf.flush(*_dout);
    *_dout << dendl;

    if (!extract_monmap.empty()) {
      int r = mapbl.write_file(extract_monmap.c_str());
      if (r < 0) {
	r = -errno;
	derr << "error writing monmap to " << extract_monmap << ": " << cpp_strerror(r) << dendl;
	prefork.exit(1);
      }
      derr << "wrote monmap to " << extract_monmap << dendl;
      prefork.exit(0);
    }
  }

  // this is what i will bind to
  entity_addrvec_t ipaddrs;

  if (monmap.contains(g_conf()->name.get_id())) {
    ipaddrs = monmap.get_addrs(g_conf()->name.get_id());

    // print helpful warning if the conf file doesn't match
    std::vector<std::string> my_sections = g_conf().get_my_sections();
    std::string mon_addr_str;
    if (g_conf().get_val_from_conf_file(my_sections, "mon addr",
				       mon_addr_str, true) == 0) {
      entity_addr_t conf_addr;
      if (conf_addr.parse(mon_addr_str)) {
	entity_addrvec_t conf_addrs = make_mon_addrs(conf_addr);
        if (ipaddrs != conf_addrs) {
	  derr << "WARNING: 'mon addr' config option " << conf_addrs
	       << " does not match monmap file" << std::endl
	       << "         continuing with monmap configuration" << dendl;
        }
      } else
	derr << "WARNING: invalid 'mon addr' config option" << std::endl
	     << "         continuing with monmap configuration" << dendl;
    }
  } else {
    dout(0) << g_conf()->name << " does not exist in monmap, will attempt to join an existing cluster" << dendl;

    pick_addresses(g_ceph_context, CEPH_PICK_ADDRESS_PUBLIC);
    if (!g_conf()->public_addrv.empty()) {
      ipaddrs = g_conf()->public_addrv;
      dout(0) << "using public_addrv " << ipaddrs << dendl;
    } else if (!g_conf()->public_addr.is_blank_ip()) {
      ipaddrs = make_mon_addrs(g_conf()->public_addr);
      dout(0) << "using public_addr " << g_conf()->public_addr << " -> "
	      << ipaddrs << dendl;
    } else {
      MonMap tmpmap;
      ostringstream oss;
      int err = tmpmap.build_initial(g_ceph_context, true, oss);
      if (oss.tellp())
        derr << oss.str() << dendl;
      if (err < 0) {
	derr << argv[0] << ": error generating initial monmap: "
             << cpp_strerror(err) << dendl;
	prefork.exit(1);
      }
      if (tmpmap.contains(g_conf()->name.get_id())) {
	ipaddrs = tmpmap.get_addrs(g_conf()->name.get_id());
      } else {
	derr << "no public_addr or public_network specified, and "
	     << g_conf()->name << " not present in monmap or ceph.conf" << dendl;
	prefork.exit(1);
      }
    }
  }

  // ==================== 第九阶段：创建网络通信和 Monitor 实例 ====================
  dout(10) << "ceph-mon: creating network communication and Monitor instance" << dendl;
  
  // 获取当前 MON 在集群中的排名
  int rank = monmap.get_rank(g_conf()->name.get_id());
  dout(10) << "ceph-mon: monitor rank: " << rank << dendl;
  
  // 选择消息传输类型（公共网络类型优先）
  std::string public_msgr_type = g_conf()->ms_public_type.empty() ? 
    g_conf().get_val<std::string>("ms_type") : g_conf()->ms_public_type;
  dout(10) << "ceph-mon: messenger type: " << public_msgr_type << dendl;
  
  // 创建 Messenger 用于网络通信
  Messenger *msgr = Messenger::create(g_ceph_context, public_msgr_type,
				      entity_name_t::MON(rank), "mon", 0);
  if (!msgr) {
    derr << "ceph-mon: failed to create messenger" << dendl;
    exit(1);
  }
  dout(10) << "ceph-mon: messenger created successfully" << dendl;
    
  // 设置集群协议和消息优先级
  msgr->set_cluster_protocol(CEPH_MON_PROTOCOL);
  msgr->set_default_send_priority(CEPH_MSG_PRIO_HIGH);
  dout(10) << "ceph-mon: messenger protocol and priority configured" << dendl;

  // 设置通信策略：定义与不同类型实体的通信方式
  dout(10) << "ceph-mon: configuring communication policies" << dendl;
  msgr->set_default_policy(Messenger::Policy::stateless_server(0));
  msgr->set_policy(entity_name_t::TYPE_MON,
                   Messenger::Policy::lossless_peer_reuse(
		     CEPH_FEATURE_SERVER_LUMINOUS));
  msgr->set_policy(entity_name_t::TYPE_OSD,
                   Messenger::Policy::stateless_server(
		     CEPH_FEATURE_SERVER_LUMINOUS));
  msgr->set_policy(entity_name_t::TYPE_CLIENT,
                   Messenger::Policy::stateless_server(0));
  msgr->set_policy(entity_name_t::TYPE_MDS,
                   Messenger::Policy::stateless_server(0));
  dout(10) << "ceph-mon: communication policies configured" << dendl;

  // 设置流量限制：防止客户端和守护进程流量过大
  dout(10) << "ceph-mon: configuring traffic throttling" << dendl;
  // 客户端流量限制
  Throttle client_throttler(g_ceph_context, "mon_client_bytes",
                            g_conf()->mon_client_bytes);
  msgr->set_policy_throttlers(entity_name_t::TYPE_CLIENT,
                              &client_throttler, NULL);

  // 守护进程流量限制
  // 注意：在领导者上，实际使用量可能乘以 MON 数量（如果转发大型更新消息）
  Throttle daemon_throttler(g_ceph_context, "mon_daemon_bytes",
                            g_conf()->mon_daemon_bytes);
  msgr->set_policy_throttlers(entity_name_t::TYPE_OSD, &daemon_throttler,
                              NULL);
  msgr->set_policy_throttlers(entity_name_t::TYPE_MDS, &daemon_throttler,
                              NULL);
  dout(10) << "ceph-mon: traffic throttling configured" << dendl;

  entity_addrvec_t bind_addrs = ipaddrs;
  entity_addrvec_t public_addrs = ipaddrs;

  // check if the public_bind_addr option is set
  if (!g_conf()->public_bind_addr.is_blank_ip()) {
    bind_addrs = make_mon_addrs(g_conf()->public_bind_addr);
  }

  dout(0) << "starting " << g_conf()->name << " rank " << rank
	  << " at public addrs " << public_addrs
	  << " at bind addrs " << bind_addrs
	  << " mon_data " << g_conf()->mon_data
	  << " fsid " << monmap.get_fsid()
	  << dendl;

  Messenger *mgr_msgr = Messenger::create(g_ceph_context, public_msgr_type,
					  entity_name_t::MON(rank), "mon-mgrc",
					  Messenger::get_random_nonce());
  if (!mgr_msgr) {
    derr << "unable to create mgr_msgr" << dendl;
    prefork.exit(1);
  }

  // 创建 Monitor 实例
  dout(10) << "ceph-mon: creating Monitor instance" << dendl;
  mon = new Monitor(g_ceph_context, g_conf()->name.get_id(), &store,
		    msgr, mgr_msgr, &monmap);

  // 保存原始参数，用于可能的重新启动
  mon->orig_argc = argc;
  mon->orig_argv = argv;
  dout(10) << "ceph-mon: Monitor instance created successfully" << dendl;

  if (force_sync) {
    derr << "flagging a forced sync ..." << dendl;
    ostringstream oss;
    JSONFormatter jf(true);
    mon->sync_force(&jf);
    derr << "out:\n";
    jf.flush(*_dout);
    *_dout << dendl;
  }

  // ==================== 第十阶段：启动和运行 MON 服务 ====================
  dout(0) << "ceph-mon: starting monitor service" << dendl;
  
  // 预初始化 Monitor
  dout(10) << "ceph-mon: preinitializing Monitor" << dendl;
  err = mon->preinit();
  if (err < 0) {
    derr << "failed to initialize" << dendl;
    prefork.exit(1);
  }
  dout(10) << "ceph-mon: Monitor preinitialized successfully" << dendl;

  // 如果需要压缩存储
  if (compact || g_conf()->mon_compact_on_start) {
    derr << "compacting monitor store ..." << dendl;
    mon->store->compact();
    derr << "done compacting" << dendl;
  }

  // 绑定网络地址
  dout(10) << "ceph-mon: binding network addresses" << dendl;
  err = msgr->bindv(bind_addrs, public_addrs);
  if (err < 0) {
    derr << "unable to bind monitor to " << bind_addrs << dendl;
    prefork.exit(1);
  }
  dout(10) << "ceph-mon: network addresses bound successfully" << dendl;

  if (g_conf()->daemonize) {
    global_init_postfork_finish(g_ceph_context);
    prefork.daemonize();
  }

  // 启动网络通信
  dout(10) << "ceph-mon: starting network communication" << dendl;
  msgr->start();
  mgr_msgr->start();
  dout(10) << "ceph-mon: network communication started" << dendl;

  // 设置 CRUSH 位置（如果指定）
  mon->set_mon_crush_location(crush_loc);
  
  // 初始化 Monitor 并进入主循环
  dout(10) << "ceph-mon: initializing Monitor" << dendl;
  mon->init();
  dout(0) << "ceph-mon: Monitor initialized and ready" << dendl;

  // 注册信号处理器
  dout(10) << "ceph-mon: registering signal handlers" << dendl;
  register_async_signal_handler_oneshot(SIGINT, handle_mon_signal);
  register_async_signal_handler_oneshot(SIGTERM, handle_mon_signal);
  register_async_signal_handler(SIGHUP, handle_mon_signal);

  // 测试早期 SIGTERM 注入（用于测试）
  if (g_conf()->inject_early_sigterm)
    kill(getpid(), SIGTERM);

  // 等待网络通信结束（主循环）
  dout(0) << "ceph-mon: entering main event loop" << dendl;
  msgr->wait();
  mgr_msgr->wait();
  dout(0) << "ceph-mon: main event loop ended" << dendl;

  // ==================== 第十一阶段：清理和退出 ====================
  dout(0) << "ceph-mon: starting cleanup and shutdown" << dendl;
  
  // 关闭存储
  dout(10) << "ceph-mon: closing monitor store" << dendl;
  store.close();

  // 注销信号处理器
  dout(10) << "ceph-mon: unregistering signal handlers" << dendl;
  unregister_async_signal_handler(SIGHUP, handle_mon_signal);
  unregister_async_signal_handler(SIGINT, handle_mon_signal);
  unregister_async_signal_handler(SIGTERM, handle_mon_signal);
  shutdown_async_signal_handler();

  // 清理资源
  dout(10) << "ceph-mon: cleaning up resources" << dendl;
  delete mon;
  delete msgr;
  delete mgr_msgr;

  // 切换到 gmon 目录，便于性能分析文件分离
  char s[20];
  snprintf(s, sizeof(s), "gmon/%d", getpid());
  if ((mkdir(s, 0755) == 0) && (chdir(s) == 0)) {
    dout(0) << "ceph-mon: gmon.out should be in " << s << dendl;
  }

  dout(0) << "ceph-mon: shutdown completed successfully" << dendl;
  prefork.signal_exit(0);
  return 0;
}
