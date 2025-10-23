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
 * @file ceph_osd.cc
 * @brief Ceph Object Storage Daemon (OSD) 守护进程主程序
 * 
 * 本文件是 Ceph 对象存储守护进程的主入口程序，负责启动和管理 Ceph OSD 服务。
 * OSD 是 Ceph 集群的核心存储组件，负责实际的数据存储和检索操作。
 * 
 * 主要功能：
 * - 启动 OSD 守护进程，提供对象存储服务
 * - 管理本地存储后端（FileStore、BlueStore 等）
 * - 处理客户端读写请求和集群间数据复制
 * - 维护 PG（Placement Group）状态和数据一致性
 * - 执行数据恢复、重新平衡和故障检测
 * - 支持多种存储后端和优化策略
 * 
 * 核心组件：
 * - OSD：OSD 核心实现类，管理存储和 PG
 * - ObjectStore：存储后端抽象层，支持多种存储类型
 * - Messenger：网络通信层，处理集群间通信
 * - MonClient：与 MON 的通信客户端
 * - Throttle：流量控制，防止过载
 * 
 * 存储后端支持：
 * - FileStore：基于文件系统的存储后端
 * - BlueStore：高性能的块设备存储后端
 * - MemStore：内存存储后端（用于测试）
 * 
 * 工作模式：
 * - 正常服务模式：提供对象存储服务
 * - 创建文件系统模式（--mkfs）：初始化 OSD 存储
 * - 维护模式：支持各种维护操作
 * 
 * 使用示例：
 * - 启动服务：ceph-osd -i <id> --osd-data <path>
 * - 创建文件系统：ceph-osd -i <id> --osd-data <path> --mkfs
 * - 维护操作：支持 --flush-journal、--dump-journal 等
 * 
 * 特性：
 * - 支持多种存储后端和优化策略
 * - 提供高可用性和数据一致性保证
 * - 支持自动故障检测和恢复
 * - 提供丰富的监控和管理接口
 * - 支持 NUMA 优化和性能调优
 */

// 标准 C 库头文件
#include <sys/types.h>    // 系统数据类型定义
#include <sys/stat.h>     // 文件状态信息
#include <fcntl.h>        // 文件控制操作

// Boost 库头文件
#include <boost/scoped_ptr.hpp>  // 智能指针，自动管理资源

// 标准 C++ 库头文件
#include <iostream>       // 输入输出流
#include <string>         // 字符串类

// Ceph 认证组件
#include "auth/KeyRing.h" // 密钥环管理

// Ceph OSD 核心组件
#include "osd/OSD.h"      // OSD 核心实现类
#include "os/ObjectStore.h" // 对象存储后端抽象层

// Ceph 监控组件
#include "mon/MonClient.h" // MON 客户端
#include "mon/MonMap.h"    // MON 映射表

// Ceph 消息传递组件
#include "msg/Messenger.h" // 网络消息传递器

// Ceph 通用组件
#include "include/ceph_features.h" // Ceph 功能特性定义
#include "common/config.h"         // 配置管理系统
#include "common/Throttle.h"       // 流量限制器
#include "common/Timer.h"          // 定时器
#include "common/TracepointProvider.h" // 跟踪点提供者
#include "common/ceph_argparse.h"  // 命令行参数解析
#include "common/numa.h"           // NUMA 支持
#include "common/errno.h"          // 错误码定义
#include "common/pick_address.h"   // 地址选择工具
#include "common/Preforker.h"      // 预分叉处理器

// Ceph 全局组件
#include "global/global_init.h"    // 全局初始化
#include "global/signal_handler.h" // 信号处理器

// Ceph 扩展组件
#include "extblkdev/ExtBlkDevPlugin.h" // 外部块设备插件

// Ceph 工具组件
#include "include/color.h"         // 颜色输出
#include "perfglue/heap_profiler.h" // 堆内存分析器
#include "include/ceph_assert.h"   // 断言宏定义

// 调试输出上下文和子系统定义
#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_osd

// 标准命名空间使用声明
using std::cerr;
using std::cout;
using std::map;
using std::ostringstream;
using std::string;
using std::vector;

// Ceph 命名空间使用声明
using ceph::bufferlist;

namespace {

/**
 * @brief 跟踪点提供者配置
 * 
 * 定义各种跟踪点提供者，用于性能分析和调试：
 * - osd_tracepoint_traits: OSD 核心跟踪点
 * - os_tracepoint_traits: 对象存储跟踪点
 * - bluestore_tracepoint_traits: BlueStore 跟踪点
 * - cyg_profile_traits: 函数调用跟踪点（可选）
 */
TracepointProvider::Traits osd_tracepoint_traits("libosd_tp.so",
                                                 "osd_tracing");
TracepointProvider::Traits os_tracepoint_traits("libos_tp.so",
                                                "osd_objectstore_tracing");
TracepointProvider::Traits bluestore_tracepoint_traits("libbluestore_tp.so",
						       "bluestore_tracing");
#ifdef WITH_OSD_INSTRUMENT_FUNCTIONS
TracepointProvider::Traits cyg_profile_traits("libcyg_profile_tp.so",
                                                 "osd_function_tracing");
#endif

} // anonymous namespace

/**
 * @brief 全局 OSD 实例指针
 * 
 * 用于信号处理器访问 OSD 实例，实现信号处理功能。
 * 在程序启动时创建，在程序结束时清理。
 */
OSD *osdptr = nullptr;

/**
 * @brief OSD 进程信号处理函数
 * 
 * 处理发送给 OSD 进程的各种信号，如 SIGTERM、SIGINT、SIGHUP 等。
 * 将信号转发给 OSD 实例进行具体处理。
 * 
 * @param signum 信号编号
 *               - SIGTERM: 终止信号，用于优雅关闭
 *               - SIGINT:  中断信号（Ctrl+C），用于强制关闭
 *               - SIGHUP:  挂起信号，用于重新加载配置
 */
void handle_osd_signal(int signum)
{
  if (osdptr)
    osdptr->handle_signal(signum);
}

/**
 * @brief 显示 ceph-osd 使用帮助信息
 * 
 * 输出 ceph-osd 命令的完整使用说明，包括所有支持的选项和参数。
 * 帮助信息包括基本用法、存储选项、维护操作等。
 */
static void usage()
{
  cout << "usage: ceph-osd -i <ID> [flags]\n"
       << "  --osd-data PATH data directory\n"
       << "  --osd-journal PATH\n"
       << "                    journal file or block device\n"
       << "  --mkfs            create a [new] data directory\n"
       << "  --mkkey           generate a new secret key. This is normally used in combination with --mkfs\n"
       << "  --monmap          specify the path to the monitor map. This is normally used in combination with --mkfs\n"
       << "  --osd-uuid        specify the OSD's fsid. This is normally used in combination with --mkfs\n"
       << "  --keyring         specify a path to the osd keyring. This is normally used in combination with --mkfs\n"
       << "  --convert-filestore\n"
       << "                    run any pending upgrade operations\n"
       << "  --flush-journal   flush all data out of journal\n"
       << "  --osdspec-affinity\n"
       << "                    set affinity to an osdspec\n"
       << "  --dump-journal    dump all data of journal\n"
       << "  --mkjournal       initialize a new journal\n"
       << "  --check-wants-journal\n"
       << "                    check whether a journal is desired\n"
       << "  --check-allows-journal\n"
       << "                    check whether a journal is allowed\n"
       << "  --check-needs-journal\n"
       << "                    check whether a journal is required\n"
       << "  --debug_osd <N>   set debug level (e.g. 10)\n"
       << "  --get-device-fsid PATH\n"
       << "                    get OSD fsid for the given block device\n"
       << std::endl;
  generic_server_usage();
}

/**
 * @brief Ceph OSD 守护进程主函数
 * 
 * 这是 ceph-osd 程序的主入口点，负责启动和管理 Ceph OSD 守护进程。
 * 支持多种运行模式：正常服务、文件系统创建、维护操作等。
 * 
 * 主要执行流程：
 * 1. 参数解析：解析命令行参数和配置文件
 * 2. 特殊操作：处理各种维护和工具操作
 * 3. 存储初始化：创建和配置存储后端
 * 4. 网络设置：创建消息传递器和绑定地址
 * 5. OSD 启动：创建 OSD 实例并启动服务
 * 6. 主循环：处理请求和事件
 * 7. 清理退出：清理资源和优雅关闭
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 0 程序成功执行
 * @return 非0 程序执行失败，返回相应的错误码
 * 
 * @note OSD 是 Ceph 集群的核心存储组件，负责实际的数据存储和检索
 */
int main(int argc, const char **argv)
{
  // ==================== 第一阶段：程序初始化和参数解析 ====================
  
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
  auto cct = global_init(
    nullptr,
    args, CEPH_ENTITY_TYPE_OSD,
    CODE_ENVIRONMENT_DAEMON, 0);
  ceph_heap_profiler_init();  // 初始化堆分析器

  // 创建预分叉处理器，用于守护进程模式
  Preforker forker;

  // ==================== 第三阶段：OSD 特定参数解析 ====================
  
  // 声明 OSD 特定的命令行参数变量
  bool mkfs = false;              // 是否创建文件系统
  bool mkjournal = false;         // 是否创建日志
  bool check_wants_journal = false;   // 是否检查需要日志
  bool check_allows_journal = false;  // 是否检查允许日志
  bool check_needs_journal = false;   // 是否检查必需日志
  bool mkkey = false;             // 是否生成密钥
  bool flushjournal = false;      // 是否刷新日志
  bool dump_journal = false;      // 是否转储日志
  bool convertfilestore = false;  // 是否转换文件存储
  bool get_osd_fsid = false;      // 是否获取 OSD FSID
  bool get_cluster_fsid = false;  // 是否获取集群 FSID
  bool get_journal_fsid = false;  // 是否获取日志 FSID
  bool get_device_fsid = false;   // 是否获取设备 FSID
  string device_path;             // 设备路径
  std::string dump_pg_log;        // 转储 PG 日志路径
  std::string osdspec_affinity;   // OSD 规格亲和性

  // 遍历并处理命令行参数
  std::string val;
  for (std::vector<const char*>::iterator i = args.begin(); i != args.end(); ) {
    if (ceph_argparse_double_dash(args, i)) {
      // 遇到 "--" 分隔符，停止处理 Ceph 特定选项
      break;
    } else if (ceph_argparse_flag(args, i, "--mkfs", (char*)NULL)) {
      // 创建文件系统模式
      mkfs = true;
    } else if (ceph_argparse_witharg(args, i, &val, "--osdspec-affinity", (char*)NULL)) {
      // OSD 规格亲和性设置
      osdspec_affinity = val;
    } else if (ceph_argparse_flag(args, i, "--mkjournal", (char*)NULL)) {
      // 创建日志模式
      mkjournal = true;
    } else if (ceph_argparse_flag(args, i, "--check-allows-journal", (char*)NULL)) {
      // 检查是否允许日志
      check_allows_journal = true;
    } else if (ceph_argparse_flag(args, i, "--check-wants-journal", (char*)NULL)) {
      // 检查是否需要日志
      check_wants_journal = true;
    } else if (ceph_argparse_flag(args, i, "--check-needs-journal", (char*)NULL)) {
      // 检查是否必需日志
      check_needs_journal = true;
    } else if (ceph_argparse_flag(args, i, "--mkkey", (char*)NULL)) {
      // 生成密钥模式
      mkkey = true;
    } else if (ceph_argparse_flag(args, i, "--flush-journal", (char*)NULL)) {
      // 刷新日志模式
      flushjournal = true;
    } else if (ceph_argparse_flag(args, i, "--convert-filestore", (char*)NULL)) {
      // 转换文件存储模式
      convertfilestore = true;
    } else if (ceph_argparse_witharg(args, i, &val, "--dump-pg-log", (char*)NULL)) {
      // 转储 PG 日志
      dump_pg_log = val;
    } else if (ceph_argparse_flag(args, i, "--dump-journal", (char*)NULL)) {
      // 转储日志模式
      dump_journal = true;
    } else if (ceph_argparse_flag(args, i, "--get-cluster-fsid", (char*)NULL)) {
      // 获取集群 FSID
      get_cluster_fsid = true;
    } else if (ceph_argparse_flag(args, i, "--get-osd-fsid", "--get-osd-uuid", (char*)NULL)) {
      // 获取 OSD FSID
      get_osd_fsid = true;
    } else if (ceph_argparse_flag(args, i, "--get-journal-fsid", "--get-journal-uuid", (char*)NULL)) {
      // 获取日志 FSID
      get_journal_fsid = true;
    } else if (ceph_argparse_witharg(args, i, &device_path,
				     "--get-device-fsid", (char*)NULL)) {
      // 获取设备 FSID
      get_device_fsid = true;
    } else {
      // 未识别的参数，跳过
      ++i;
    }
  }
  // 检查未处理的参数
  if (!args.empty()) {
    cerr << "unrecognized arg " << args[0] << std::endl;
    exit(1);
  }

  // ==================== 第四阶段：守护进程处理 ====================
  
  // 执行预分叉处理（如果支持）
  if (global_init_prefork(g_ceph_context) >= 0) {
    std::string err;
    int r = forker.prefork(err);
    if (r < 0) {
      cerr << err << std::endl;
      return r;
    }
    if (forker.is_parent()) {
      // 父进程：启动日志并等待子进程完成
      g_ceph_context->_log->start();
      if (forker.parent_wait(err) != 0) {
        return -ENXIO;
      }
      return 0;
    }
    // 子进程：继续执行
    setsid();
    global_init_postfork_start(g_ceph_context);
  }
  
  // 完成通用初始化和目录切换
  common_init_finish(g_ceph_context);
  global_init_chdir(g_ceph_context);

  // ==================== 第五阶段：特殊操作处理 ====================
  
  // 处理获取设备 FSID 的操作
  if (get_journal_fsid) {
    device_path = g_conf().get_val<std::string>("osd_journal");
    get_device_fsid = true;
  }
  if (get_device_fsid) {
    uuid_d uuid;
    int r = ObjectStore::probe_block_device_fsid(g_ceph_context, device_path,
						 &uuid);
    if (r < 0) {
      cerr << "failed to get device fsid for " << device_path
	   << ": " << cpp_strerror(r) << std::endl;
      forker.exit(1);
    }
    cout << uuid << std::endl;
    forker.exit(0);
  }

  // 处理转储 PG 日志的操作
  if (!dump_pg_log.empty()) {
    common_init_finish(g_ceph_context);
    bufferlist bl;
    std::string error;

    if (bl.read_file(dump_pg_log.c_str(), &error) >= 0) {
      pg_log_entry_t e;
      auto p = bl.cbegin();
      while (!p.end()) {
	uint64_t pos = p.get_off();
	try {
	  decode(e, p);
	}
	catch (const ceph::buffer::error &e) {
	  derr << "failed to decode LogEntry at offset " << pos << dendl;
	  forker.exit(1);
	}
	derr << pos << ":\t" << e << dendl;
      }
    } else {
      derr << "unable to open " << dump_pg_log << ": " << error << dendl;
    }
    forker.exit(0);
  }

  // ==================== 第六阶段：OSD 身份验证和路径检查 ====================
  
  // 解析 OSD ID
  char *end;
  const char *id = g_conf()->name.get_id().c_str();
  int whoami = strtol(id, &end, 10);
  std::string data_path = g_conf().get_val<std::string>("osd_data");
  
  // 验证 OSD ID 格式
  if (*end || end == id || whoami < 0) {
    derr << "must specify '-i #' where # is the osd number" << dendl;
    forker.exit(1);
  }

  // 验证数据路径
  if (data_path.empty()) {
    derr << "must specify '--osd-data=foo' data path" << dendl;
    forker.exit(1);
  }

  // ==================== 第七阶段：存储后端类型检测 ====================
  
  // 检测存储后端类型
  std::string store_type;
  {
    char fn[PATH_MAX];
    snprintf(fn, sizeof(fn), "%s/type", data_path.c_str());
    int fd = ::open(fn, O_RDONLY|O_CLOEXEC);
    if (fd >= 0) {
      // 从 type 文件读取存储类型
      bufferlist bl;
      bl.read_fd(fd, 64);
      if (bl.length()) {
	store_type = string(bl.c_str(), bl.length() - 1);  // drop \n
	dout(5) << "object store type is " << store_type << dendl;
      }
      ::close(fd);
    } else if (mkfs) {
      // 创建文件系统时使用配置的存储类型
      store_type = g_conf().get_val<std::string>("osd_objectstore");
    } else {
      // 尝试推断存储类型
      snprintf(fn, sizeof(fn), "%s/current", data_path.c_str());
      struct stat st;
      if (::stat(fn, &st) == 0 &&
	  S_ISDIR(st.st_mode)) {
	derr << "missing 'type' file, inferring filestore from current/ dir"
	     << dendl;
	store_type = "filestore";
      } else {
	snprintf(fn, sizeof(fn), "%s/block", data_path.c_str());
	if (::stat(fn, &st) == 0 &&
	    S_ISLNK(st.st_mode)) {
	  derr << "missing 'type' file, inferring bluestore from block symlink"
	       << dendl;
	  store_type = "bluestore";
	} else {
	  derr << "missing 'type' file and unable to infer osd type" << dendl;
	  forker.exit(1);
	}
      }
    }
  }

  // ==================== 第八阶段：存储后端创建 ====================
  
  // 获取日志路径和存储标志
  std::string journal_path = g_conf().get_val<std::string>("osd_journal");
  uint32_t flags = g_conf().get_val<uint64_t>("osd_os_flags");
  
  // 创建存储后端实例
  std::unique_ptr<ObjectStore> store = ObjectStore::create(g_ceph_context,
							   store_type,
							   data_path,
							   journal_path,
							   flags);
  if (!store) {
    derr << "unable to create object store" << dendl;
    forker.exit(-ENODEV);
  }


  // ==================== 第九阶段：维护操作处理 ====================
  
  // 处理生成密钥的操作
  if (mkkey) {
    common_init_finish(g_ceph_context);
    KeyRing keyring;

    EntityName ename{g_conf()->name};
    EntityAuth eauth;

    std::string keyring_path = g_conf().get_val<std::string>("keyring");
    int ret = keyring.load(g_ceph_context, keyring_path);
    if (ret == 0 &&
	keyring.get_auth(ename, eauth)) {
      derr << "already have key in keyring " << keyring_path << dendl;
    } else {
      eauth.key.create(g_ceph_context, CEPH_CRYPTO_AES);
      keyring.add(ename, eauth);
      bufferlist bl;
      keyring.encode_plaintext(bl);
      int r = bl.write_file(keyring_path.c_str(), 0600);
      if (r)
	derr << TEXT_RED << " ** ERROR: writing new keyring to "
             << keyring_path << ": " << cpp_strerror(r) << TEXT_NORMAL
             << dendl;
      else
	derr << "created new key in keyring " << keyring_path << dendl;
    }
  }

  // 处理创建文件系统的操作
  if (mkfs) {
    common_init_finish(g_ceph_context);

    if (g_conf().get_val<uuid_d>("fsid").is_zero()) {
      derr << "must specify cluster fsid" << dendl;
      forker.exit(-EINVAL);
    }

    int err = OSD::mkfs(g_ceph_context, std::move(store), g_conf().get_val<uuid_d>("fsid"),
                        whoami, osdspec_affinity);
    if (err < 0) {
      derr << TEXT_RED << " ** ERROR: error creating empty object store in "
	   << data_path << ": " << cpp_strerror(-err) << TEXT_NORMAL << dendl;
      forker.exit(1);
    }
    dout(0) << "created object store " << data_path
	    << " for osd." << whoami
	    << " fsid " << g_conf().get_val<uuid_d>("fsid")
	    << dendl;
  }
  // 如果执行了创建文件系统或生成密钥操作，则退出
  if (mkfs || mkkey) {
    forker.exit(0);
  }
  
  // 处理创建日志的操作
  if (mkjournal) {
    common_init_finish(g_ceph_context);
    int err = store->mkjournal();
    if (err < 0) {
      derr << TEXT_RED << " ** ERROR: error creating fresh journal "
           << journal_path << " for object store " << data_path << ": "
           << cpp_strerror(-err) << TEXT_NORMAL << dendl;
      forker.exit(1);
    }
    derr << "created new journal " << journal_path
	 << " for object store " << data_path << dendl;
    forker.exit(0);
  }
  // 处理日志检查操作
  if (check_wants_journal) {
    if (store->wants_journal()) {
      cout << "wants journal: yes" << std::endl;
      forker.exit(0);
    } else {
      cout << "wants journal: no" << std::endl;
      forker.exit(1);
    }
  }
  if (check_allows_journal) {
    if (store->allows_journal()) {
      cout << "allows journal: yes" << std::endl;
      forker.exit(0);
    } else {
      cout << "allows journal: no" << std::endl;
      forker.exit(1);
    }
  }
  if (check_needs_journal) {
    if (store->needs_journal()) {
      cout << "needs journal: yes" << std::endl;
      forker.exit(0);
    } else {
      cout << "needs journal: no" << std::endl;
      forker.exit(1);
    }
  }
  // 处理刷新日志的操作
  if (flushjournal) {
    common_init_finish(g_ceph_context);
    int err = store->mount();
    if (err < 0) {
      derr << TEXT_RED << " ** ERROR: error flushing journal " << journal_path
	   << " for object store " << data_path
	   << ": " << cpp_strerror(-err) << TEXT_NORMAL << dendl;
      goto flushjournal_out;
    }
    store->umount();
    derr << "flushed journal " << journal_path
	 << " for object store " << data_path
	 << dendl;
flushjournal_out:
    store.reset();
    forker.exit(err < 0 ? 1 : 0);
  }
  
  // 处理转储日志的操作
  if (dump_journal) {
    common_init_finish(g_ceph_context);
    int err = store->dump_journal(cout);
    if (err < 0) {
      derr << TEXT_RED << " ** ERROR: error dumping journal " << journal_path
	   << " for object store " << data_path
	   << ": " << cpp_strerror(-err) << TEXT_NORMAL << dendl;
      forker.exit(1);
    }
    derr << "dumped journal " << journal_path
	 << " for object store " << data_path
	 << dendl;
    forker.exit(0);
  }

  // 处理转换文件存储的操作
  if (convertfilestore) {
    int err = store->mount();
    if (err < 0) {
      derr << TEXT_RED << " ** ERROR: error mounting store " << data_path
	   << ": " << cpp_strerror(-err) << TEXT_NORMAL << dendl;
      forker.exit(1);
    }
    err = store->upgrade();
    store->umount();
    if (err < 0) {
      derr << TEXT_RED << " ** ERROR: error converting store " << data_path
	   << ": " << cpp_strerror(-err) << TEXT_NORMAL << dendl;
      forker.exit(1);
    }
    forker.exit(0);
  }
  
  // ==================== 第十阶段：存储验证和元数据检查 ====================
  
  // 预加载外部块设备插件
  {
    int r = extblkdev::preload(g_ceph_context);
    if (r < 0) {
      derr << "Failed preloading extblkdev plugins, error code: " << r << dendl;
      forker.exit(1);
    }
  }

  // 读取 OSD 元数据
  string magic;
  uuid_d cluster_fsid, osd_fsid;
  ceph_release_t require_osd_release = ceph_release_t::unknown;
  int w;
  int r = OSD::peek_meta(store.get(), &magic, &cluster_fsid, &osd_fsid, &w,
			 &require_osd_release);
  if (r < 0) {
    derr << TEXT_RED << " ** ERROR: unable to open OSD superblock on "
	 << data_path << ": " << cpp_strerror(-r)
	 << TEXT_NORMAL << dendl;
    if (r == -ENOTSUP) {
      derr << TEXT_RED << " **        please verify that underlying storage "
	   << "supports xattrs" << TEXT_NORMAL << dendl;
    }
    forker.exit(1);
  }
  
  // 验证 OSD ID 匹配
  if (w != whoami) {
    derr << "OSD id " << w << " != my id " << whoami << dendl;
    forker.exit(1);
  }
  
  // 验证 OSD 魔数
  if (strcmp(magic.c_str(), CEPH_OSD_ONDISK_MAGIC)) {
    derr << "OSD magic " << magic << " != my " << CEPH_OSD_ONDISK_MAGIC
	 << dendl;
    forker.exit(1);
  }

  // 处理获取 FSID 的操作
  if (get_cluster_fsid) {
    cout << cluster_fsid << std::endl;
    forker.exit(0);
  }
  if (get_osd_fsid) {
    cout << osd_fsid << std::endl;
    forker.exit(0);
  }

  // 检查版本兼容性
  {
    ostringstream err;
    if (!can_upgrade_from(require_osd_release, "require_osd_release", err)) {
      derr << err.str() << dendl;
      forker.exit(1);
    }
  }

  // ==================== 第十一阶段：NUMA 优化和网络设置 ====================
  
  // 考虑对象存储的 NUMA 节点
  int os_numa_node = -1;
  r = store->get_numa_node(&os_numa_node, nullptr, nullptr);
  if (r >= 0 && os_numa_node >= 0) {
    dout(1) << " objectstore numa_node " << os_numa_node << dendl;
  }
  int iface_preferred_numa_node = -1;
  if (g_conf().get_val<bool>("osd_numa_prefer_iface")) {
    iface_preferred_numa_node = os_numa_node;
  }

  // 创建消息传递器
  std::string msg_type = g_conf().get_val<std::string>("ms_type");
  std::string public_msg_type =
    g_conf().get_val<std::string>("ms_public_type");
  std::string cluster_msg_type =
    g_conf().get_val<std::string>("ms_cluster_type");

  public_msg_type = public_msg_type.empty() ? msg_type : public_msg_type;
  cluster_msg_type = cluster_msg_type.empty() ? msg_type : cluster_msg_type;
  uint64_t nonce = Messenger::get_random_nonce();
  
  // 创建各种消息传递器
  Messenger *ms_public = Messenger::create(g_ceph_context, public_msg_type,
					   entity_name_t::OSD(whoami), "client", nonce);
  Messenger *ms_cluster = Messenger::create(g_ceph_context, cluster_msg_type,
					    entity_name_t::OSD(whoami), "cluster", nonce);
  Messenger *ms_hb_back_client = Messenger::create(g_ceph_context, cluster_msg_type,
					     entity_name_t::OSD(whoami), "hb_back_client", nonce);
  Messenger *ms_hb_front_client = Messenger::create(g_ceph_context, public_msg_type,
					     entity_name_t::OSD(whoami), "hb_front_client", nonce);
  Messenger *ms_hb_back_server = Messenger::create(g_ceph_context, cluster_msg_type,
						   entity_name_t::OSD(whoami), "hb_back_server", nonce);
  Messenger *ms_hb_front_server = Messenger::create(g_ceph_context, public_msg_type,
						    entity_name_t::OSD(whoami), "hb_front_server", nonce);
  Messenger *ms_objecter = Messenger::create(g_ceph_context, public_msg_type,
					     entity_name_t::OSD(whoami), "ms_objecter", nonce);
  if (!ms_public || !ms_cluster || !ms_hb_front_client || !ms_hb_back_client || !ms_hb_back_server || !ms_hb_front_server || !ms_objecter)
    forker.exit(1);
  
  // 设置集群协议
  ms_cluster->set_cluster_protocol(CEPH_OSD_PROTOCOL);
  ms_hb_front_client->set_cluster_protocol(CEPH_OSD_PROTOCOL);
  ms_hb_back_client->set_cluster_protocol(CEPH_OSD_PROTOCOL);
  ms_hb_back_server->set_cluster_protocol(CEPH_OSD_PROTOCOL);
  ms_hb_front_server->set_cluster_protocol(CEPH_OSD_PROTOCOL);

  // 输出启动信息
  dout(0) << "starting osd." << whoami
          << " osd_data " << data_path
          << " " << ((journal_path.empty()) ?
		    "(no journal)" : journal_path)
          << dendl;

  // ==================== 第十二阶段：流量控制和策略设置 ====================
  
  // 创建客户端消息大小限制器
  uint64_t message_size =
    g_conf().get_val<Option::size_t>("osd_client_message_size_cap");
  boost::scoped_ptr<Throttle> client_byte_throttler(
    new Throttle(g_ceph_context, "osd_client_bytes", message_size));
  
  // 创建客户端消息数量限制器
  uint64_t message_cap = g_conf().get_val<uint64_t>("osd_client_message_cap");
  boost::scoped_ptr<Throttle> client_msg_throttler(
    new Throttle(g_ceph_context, "osd_client_messages", message_cap));

  // 设置 OSD 必需的功能特性
  // 所有功能位 0-34 应该从 dumpling v0.67 开始存在
  uint64_t osd_required =
    CEPH_FEATURE_UID |
    CEPH_FEATURE_PGID64 |
    CEPH_FEATURE_OSDENC;

  // 设置公共消息传递器策略
  ms_public->set_default_policy(Messenger::Policy::stateless_registered_server(0));
  ms_public->set_policy_throttlers(entity_name_t::TYPE_CLIENT,
				   client_byte_throttler.get(),
				   client_msg_throttler.get());
  ms_public->set_policy(entity_name_t::TYPE_MON,
                        Messenger::Policy::lossy_client(osd_required));
  ms_public->set_policy(entity_name_t::TYPE_MGR,
                        Messenger::Policy::lossy_client(osd_required));

  // 设置集群消息传递器策略
  ms_cluster->set_default_policy(Messenger::Policy::stateless_server(0));
  ms_cluster->set_policy(entity_name_t::TYPE_MON, Messenger::Policy::lossy_client(0));
  ms_cluster->set_policy(entity_name_t::TYPE_OSD,
			 Messenger::Policy::lossless_peer(osd_required));
  ms_cluster->set_policy(entity_name_t::TYPE_CLIENT,
			 Messenger::Policy::stateless_server(0));

  // 设置心跳消息传递器策略
  ms_hb_front_client->set_policy(entity_name_t::TYPE_OSD,
			  Messenger::Policy::lossy_client(0));
  ms_hb_back_client->set_policy(entity_name_t::TYPE_OSD,
			  Messenger::Policy::lossy_client(0));
  ms_hb_back_server->set_policy(entity_name_t::TYPE_OSD,
				Messenger::Policy::stateless_server(0));
  ms_hb_front_server->set_policy(entity_name_t::TYPE_OSD,
				 Messenger::Policy::stateless_server(0));

  // 设置对象器消息传递器策略
  ms_objecter->set_default_policy(Messenger::Policy::lossy_client(CEPH_FEATURE_OSDREPLYMUX));

  // ==================== 第十三阶段：网络地址选择和绑定 ====================
  
  // 选择网络地址
  entity_addrvec_t public_addrs, public_bind_addrs, cluster_addrs;
  r = pick_addresses(g_ceph_context, CEPH_PICK_ADDRESS_PUBLIC, &public_addrs,
		     iface_preferred_numa_node);
  if (r < 0) {
    derr << "Failed to pick public address." << dendl;
    forker.exit(1);
  } else {
    dout(10) << "picked public_addrs " << public_addrs << dendl;
  }
  
  // 选择公共绑定地址
  r = pick_addresses(g_ceph_context, CEPH_PICK_ADDRESS_PUBLIC_BIND,
		     &public_bind_addrs, iface_preferred_numa_node);
  if (r == -ENOENT) {
    dout(10) << "there is no public_bind_addrs, defaulting to public_addrs"
	     << dendl;
    public_bind_addrs = public_addrs;
  } else if (r < 0) {
    derr << "Failed to pick public bind address." << dendl;
    forker.exit(1);
  } else {
    dout(10) << "picked public_bind_addrs " << public_bind_addrs << dendl;
  }
  
  // 选择集群地址
  r = pick_addresses(g_ceph_context, CEPH_PICK_ADDRESS_CLUSTER, &cluster_addrs,
		     iface_preferred_numa_node);
  if (r < 0) {
    derr << "Failed to pick cluster address." << dendl;
    forker.exit(1);
  }

  // 绑定公共消息传递器
  if (ms_public->bindv(public_bind_addrs, public_addrs) < 0) {
    derr << "Failed to bind to " << public_bind_addrs << dendl;
    forker.exit(1);
  }

  // 绑定集群消息传递器
  if (ms_cluster->bindv(cluster_addrs) < 0)
    forker.exit(1);

  // 设置心跳消息传递器的延迟优化
  bool is_delay = g_conf().get_val<bool>("osd_heartbeat_use_min_delay_socket");
  if (is_delay) {
    ms_hb_front_client->set_socket_priority(SOCKET_PRIORITY_MIN_DELAY);
    ms_hb_back_client->set_socket_priority(SOCKET_PRIORITY_MIN_DELAY);
    ms_hb_back_server->set_socket_priority(SOCKET_PRIORITY_MIN_DELAY);
    ms_hb_front_server->set_socket_priority(SOCKET_PRIORITY_MIN_DELAY);
  }

  // 绑定前端心跳消息传递器
  entity_addrvec_t hb_front_addrs = public_bind_addrs;
  for (auto& a : hb_front_addrs.v) {
    a.set_port(0);
  }
  if (ms_hb_front_server->bindv(hb_front_addrs) < 0)
    forker.exit(1);
  if (ms_hb_front_client->client_bind(hb_front_addrs.front()) < 0)
    forker.exit(1);

  // 绑定后端心跳消息传递器
  entity_addrvec_t hb_back_addrs = cluster_addrs;
  for (auto& a : hb_back_addrs.v) {
    a.set_port(0);
  }
  if (ms_hb_back_server->bindv(hb_back_addrs) < 0)
    forker.exit(1);
  if (ms_hb_back_client->client_bind(hb_back_addrs.front()) < 0)
    forker.exit(1);

  // ==================== 第十四阶段：信号处理和跟踪点初始化 ====================
  
  // 安装信号处理器
  init_async_signal_handler();
  register_async_signal_handler(SIGHUP, sighup_handler);

  // 初始化跟踪点提供者
  TracepointProvider::initialize<osd_tracepoint_traits>(g_ceph_context);
  TracepointProvider::initialize<os_tracepoint_traits>(g_ceph_context);
  TracepointProvider::initialize<bluestore_tracepoint_traits>(g_ceph_context);
#ifdef WITH_OSD_INSTRUMENT_FUNCTIONS
  TracepointProvider::initialize<cyg_profile_traits>(g_ceph_context);
#endif

  // 初始化随机数生成器
  srand(time(NULL) + getpid());

  // ==================== 第十五阶段：OSD 实例创建和初始化 ====================
  
  // 创建异步 I/O 上下文池
  ceph::async::io_context_pool poolctx(
    cct->_conf.get_val<std::uint64_t>("osd_asio_thread_count"));

  // 创建 MON 客户端
  MonClient mc(g_ceph_context, poolctx);
  if (mc.build_initial_monmap() < 0)
    return -1;
  global_init_chdir(g_ceph_context);

  // 预加载纠删码
  if (global_init_preload_erasure_code(g_ceph_context) < 0) {
    forker.exit(1);
  }

  // 创建 OSD 实例
  osdptr = new OSD(g_ceph_context,
		   std::move(store),
		   whoami,
		   ms_cluster,
		   ms_public,
		   ms_hb_front_client,
		   ms_hb_back_client,
		   ms_hb_front_server,
		   ms_hb_back_server,
		   ms_objecter,
		   &mc,
		   data_path,
		   journal_path,
		   poolctx);

  // 预初始化 OSD
  int err = osdptr->pre_init();
  if (err < 0) {
    derr << TEXT_RED << " ** ERROR: osd pre_init failed: " << cpp_strerror(-err)
	 << TEXT_NORMAL << dendl;
    forker.exit(1);
  }

  // 启动所有消息传递器
  ms_public->start();
  ms_hb_front_client->start();
  ms_hb_back_client->start();
  ms_hb_front_server->start();
  ms_hb_back_server->start();
  ms_cluster->start();
  ms_objecter->start();

  // 初始化 OSD
  err = osdptr->init();
  if (err < 0) {
    derr << TEXT_RED << " ** ERROR: osd init failed: " << cpp_strerror(-err)
         << TEXT_NORMAL << dendl;
    forker.exit(1);
  }

  // ==================== 第十六阶段：守护进程化和最终初始化 ====================
  
  // 守护进程化处理
  if (g_conf()->daemonize) {
    global_init_postfork_finish(g_ceph_context);
    forker.daemonize();
  }

  // 注册信号处理器
  register_async_signal_handler_oneshot(SIGINT, handle_osd_signal);
  register_async_signal_handler_oneshot(SIGTERM, handle_osd_signal);

  // 最终初始化 OSD
  osdptr->final_init();

  // 测试早期终止信号（用于测试）
  if (g_conf().get_val<bool>("inject_early_sigterm"))
    kill(getpid(), SIGTERM);

  // ==================== 第十七阶段：主循环和等待 ====================
  
  // 等待所有消息传递器完成
  ms_public->wait();
  ms_hb_front_client->wait();
  ms_hb_back_client->wait();
  ms_hb_front_server->wait();
  ms_hb_back_server->wait();
  ms_cluster->wait();
  ms_objecter->wait();

  // ==================== 第十八阶段：清理和退出 ====================
  
  // 注销信号处理器
  unregister_async_signal_handler(SIGHUP, sighup_handler);
  unregister_async_signal_handler(SIGINT, handle_osd_signal);
  unregister_async_signal_handler(SIGTERM, handle_osd_signal);
  shutdown_async_signal_handler();

  // 停止异步 I/O 上下文池
  poolctx.stop();
  
  // 删除 OSD 实例
  delete osdptr;
  
  // 删除所有消息传递器
  delete ms_public;
  delete ms_hb_front_client;
  delete ms_hb_back_client;
  delete ms_hb_front_server;
  delete ms_hb_back_server;
  delete ms_cluster;
  delete ms_objecter;

  // 重置限制器
  client_byte_throttler.reset();
  client_msg_throttler.reset();

  // 创建性能分析输出目录
  char s[20];
  snprintf(s, sizeof(s), "gmon/%d", getpid());
  if ((mkdir(s, 0755) == 0) && (chdir(s) == 0)) {
    dout(0) << "ceph-osd: gmon.out should be in " << s << dendl;
  }

  return 0;
}
