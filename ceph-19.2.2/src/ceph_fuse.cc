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
 * @file ceph_fuse.cc
 * @brief Ceph FUSE 客户端主程序
 * 
 * 本文件实现了 Ceph 文件系统的 FUSE (Filesystem in Userspace) 客户端。
 * 它允许用户将 Ceph 分布式文件系统挂载为本地文件系统，提供 POSIX 兼容的接口。
 * 
 * 主要功能：
 * - 初始化 Ceph 客户端和 FUSE 接口
 * - 处理命令行参数和配置选项
 * - 管理守护进程模式（daemonize）
 * - 实现文件系统挂载和卸载
 * - 处理信号和错误恢复
 * - 提供 dentry 失效和重新挂载测试
 * 
 * 工作流程：
 * 1. 解析命令行参数和配置
 * 2. 初始化全局 Ceph 上下文
 * 3. 创建消息传递器和客户端
 * 4. 初始化 FUSE 接口
 * 5. 挂载 Ceph 文件系统
 * 6. 启动 FUSE 主循环
 * 7. 处理清理和退出
 */

// 标准 C 库头文件
#include <sys/stat.h>      // 文件状态信息
#include <sys/utsname.h>   // 系统信息
#include <sys/types.h>     // 系统数据类型
#include <fcntl.h>         // 文件控制操作
#include <iostream>        // 输入输出流
#include <string>          // 字符串类
#include <optional>        // 可选值类型

// Ceph 通用组件
#include "common/async/context_pool.h"  // 异步上下文池
#include "common/config.h"              // 配置管理
#include "common/errno.h"               // 错误码定义
#include "common/Timer.h"               // 定时器
#include "common/ceph_argparse.h"       // 命令行参数解析
#include "common/Preforker.h"           // 预分叉处理
#include "common/safe_io.h"             // 安全 I/O 操作

// Ceph 客户端组件
#include "client/Client.h"              // Ceph 客户端核心类
#include "client/fuse_ll.h"             // FUSE 底层接口

// Ceph 消息传递组件
#include "msg/Messenger.h"              // 消息传递器

// Ceph 监控组件
#include "mon/MonClient.h"              // 监控客户端

// 平台特定头文件
#if defined(__linux__)
#include "common/linux_version.h"       // Linux 版本检测
#endif

// Ceph 全局组件
#include "global/global_init.h"         // 全局初始化
#include "global/signal_handler.h"      // 信号处理器

// FUSE 相关头文件
#include "include/ceph_fuse.h"          // Ceph FUSE 接口定义
#include <fuse_lowlevel.h>              // FUSE 底层 API

// 调试输出上下文宏定义
#define dout_context g_ceph_context

using namespace std;

/**
 * @brief 全局异步 I/O 上下文池
 * 
 * 用于管理异步 I/O 操作的上下文池，为 Ceph 客户端提供异步处理能力。
 * 这个池在程序启动时初始化，在程序结束时清理。
 */
ceph::async::io_context_pool icp;

/**
 * @brief 显示 FUSE 使用帮助信息
 * 
 * 此函数用于显示 FUSE 相关的命令行选项帮助信息。
 * 它创建临时的 FUSE 参数结构，调用 FUSE 库的解析函数来获取帮助信息，
 * 并输出标准的 FUSE 选项说明。
 * 
 * 注意：此函数处理 FUSE 2.x 和 3.x 版本之间的兼容性差异。
 */
static void fuse_usage()
{
  // 创建临时的 FUSE 参数数组，用于获取帮助信息
  const char* argv[] = {
    "ceph-fuse",
    "-h",
  };
  struct fuse_args args = FUSE_ARGS_INIT(2, (char**)argv);
  
#if FUSE_VERSION >= FUSE_MAKE_VERSION(3, 0)
  // FUSE 3.x 版本的处理方式
  struct fuse_cmdline_opts opts = {};
  if (fuse_parse_cmdline(&args, &opts) != -1) {
    if (opts.show_help) {
      cout << "usage: " << argv[0] << " [options] <mountpoint>\n\n";
      cout << "FUSE options:\n";
      fuse_cmdline_help();
      fuse_lowlevel_help();
      cout << "\n";
    }
  } else {
#else
  // FUSE 2.x 版本的处理方式
  if (fuse_parse_cmdline(&args, nullptr, nullptr, nullptr) == -1) {
#endif
    derr << "fuse_parse_cmdline failed." << dendl;
  }
  ceph_assert(args.allocated);
  fuse_opt_free_args(&args);
}

/**
 * @brief 显示 ceph-fuse 的完整使用帮助信息
 * 
 * 此函数显示 ceph-fuse 命令的完整使用说明，包括：
 * - 基本语法和参数
 * - 客户端挂载点选项
 * - FUSE 特定选项
 * - 通用客户端选项
 * 
 * 它结合了 FUSE 库的帮助信息和 Ceph 客户端的通用选项。
 */
void usage()
{
  cout <<
"usage: ceph-fuse [-n client.username] [-m mon-ip-addr:mon-port] <mount point> [OPTIONS]\n"
"  --client_mountpoint/-r <sub_directory>\n"
"                    use sub_directory as the mounted root, rather than the full Ceph tree.\n"
"\n";
  fuse_usage();              // 显示 FUSE 相关选项
  generic_client_usage();    // 显示 Ceph 客户端通用选项
}

/**
 * @brief ceph-fuse 主函数
 * 
 * 这是 ceph-fuse 程序的主入口点，负责：
 * 1. 解析命令行参数
 * 2. 初始化 Ceph 全局上下文
 * 3. 创建和配置客户端组件
 * 4. 挂载 Ceph 文件系统
 * 5. 启动 FUSE 主循环
 * 6. 处理清理和退出
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @param envp 环境变量数组
 * @return 程序退出状态码
 */
int main(int argc, const char **argv, const char *envp[]) {
  // ==================== 第一阶段：参数解析和基本检查 ====================
  
  int filer_flags = 0;  // 文件操作标志位，用于设置 OSD 操作选项
  //cerr << "ceph-fuse starting " << myrank << "/" << world << std::endl;
  
  // 将命令行参数转换为向量格式，便于处理
  auto args = argv_to_vec(argc, argv);
  
  // 检查是否有参数，如果没有则显示使用帮助
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
  
  // 设置默认配置值
  std::map<std::string,std::string> defaults = {
    { "pid_file", "" },      // PID 文件路径（空表示不创建）
    { "chdir", "/" }         // 工作目录，FUSE 会切换到根目录
  };

  // 初始化全局 Ceph 上下文
  auto cct = global_init(&defaults, args, CEPH_ENTITY_TYPE_CLIENT,
			 CODE_ENVIRONMENT_DAEMON,
			 CINIT_FLAG_UNPRIVILEGED_DAEMON_DEFAULTS);

  // ==================== 第三阶段：处理特定命令行选项 ====================
  
  // 遍历并处理命令行参数
  for (auto i = args.begin(); i != args.end();) {
    if (ceph_argparse_double_dash(args, i)) {
      // 遇到 "--" 分隔符，停止处理 Ceph 特定选项
      break;
    } else if (ceph_argparse_flag(args, i, "--localize-reads", (char*)nullptr)) {
      // 启用本地化读取优化
      cerr << "setting CEPH_OSD_FLAG_LOCALIZE_READS" << std::endl;
      filer_flags |= CEPH_OSD_FLAG_LOCALIZE_READS;
    } else if (ceph_argparse_flag(args, i, "-V", (char*)nullptr)) {
      // 显示版本信息
      const char* tmpargv[] = {
	"ceph-fuse",
	"-V"
      };

      struct fuse_args fargs = FUSE_ARGS_INIT(2, (char**)tmpargv);
#if FUSE_VERSION >= FUSE_MAKE_VERSION(3, 0)
      // FUSE 3.x 版本处理
      struct fuse_cmdline_opts opts = {};
      if (fuse_parse_cmdline(&fargs, &opts) == -1) {
#else
      // FUSE 2.x 版本处理
      if (fuse_parse_cmdline(&fargs, nullptr, nullptr, nullptr) == -1) {
#endif
       derr << "fuse_parse_cmdline failed." << dendl;
      }
      ceph_assert(fargs.allocated);
      fuse_opt_free_args(&fargs);
      exit(0);
    } else {
      // 未识别的参数，跳过
      ++i;
    }
  }

  // ==================== 第四阶段：准备 FUSE 参数和架构检查 ====================
  
  // 为 FUSE 准备参数数组
  const char **newargv;
  int newargc;
  vec_to_argv(argv[0], args, &newargc, &newargv);

  // 检查 32 位架构兼容性
#ifndef __LP64__
    cerr << std::endl;
    cerr << "WARNING: Ceph inode numbers are 64 bits wide, and FUSE on 32-bit kernels does" << std::endl;
    cerr << "         not cope well with that situation.  Expect to crash shortly." << std::endl;
    cerr << std::endl;
#endif

  // ==================== 第五阶段：守护进程处理 ====================
  
  Preforker forker;  // 预分叉处理器，用于守护进程模式
  auto daemonize = g_conf().get_val<bool>("daemonize");
  
  if (daemonize) {
    // 如果配置为守护进程模式，执行分叉操作
    global_init_prefork(g_ceph_context);
    int r;
    string err;
    r = forker.prefork(err);
    
    if (r < 0 || forker.is_parent()) {
      // 如果是父进程或分叉失败，启动日志系统
      // 这避免了在 Ceph 上下文析构函数中的断言失败
      g_ceph_context->_log->start();
    }
    
    if (r < 0) {
      // 分叉失败，退出
      cerr << "ceph-fuse " << err << std::endl;
      return r;
    }
    
    if (forker.is_parent()) {
      // 父进程等待子进程完成
      r = forker.parent_wait(err);
      if (r < 0) {
	cerr << "ceph-fuse " << err << std::endl;
      }
      return r;
    }
    
    // 子进程继续执行，进行分叉后初始化
    global_init_postfork_start(cct.get());
  }

  // ==================== 第六阶段：核心组件初始化和测试线程 ====================
  
  {
    // 完成通用初始化
    common_init_finish(g_ceph_context);

    // 初始化异步信号处理器
    init_async_signal_handler();
    register_async_signal_handler(SIGHUP, sighup_handler);

    //cout << "child, mounting" << std::endl;
    
    /**
     * @brief 重新挂载测试线程类
     * 
     * 此线程用于测试 dentry 失效和重新挂载功能，确保 FUSE 文件系统
     * 在遇到问题时能够正确恢复。这是 Linux 平台特有的功能。
     */
    class RemountTest : public Thread {
    public:
      CephFuse *cfuse;    // Ceph FUSE 接口对象
      Client *client;     // Ceph 客户端对象
      
      RemountTest() : cfuse(nullptr), client(nullptr) {}
      
      /**
       * @brief 初始化测试线程
       * @param cf Ceph FUSE 接口对象
       * @param cl Ceph 客户端对象
       */
      void init(CephFuse *cf, Client *cl) {
	cfuse = cf;
	client = cl;
      }
      
      ~RemountTest() override {}
      
      /**
       * @brief 测试线程主函数
       * 
       * 在 Linux 平台上执行 dentry 失效和重新挂载测试，
       * 确保文件系统在遇到问题时能够正确恢复。
       * 
       * @return 测试结果状态码
       */
      void *entry() override {
#if defined(__linux__)
        // Linux 平台：执行 dentry 失效和重新挂载测试
        
        // 获取配置参数
        bool can_invalidate_dentries = g_conf().get_val<bool>(
	  "client_try_dentry_invalidate");
        uint64_t max_retries = g_conf().get_val<uint64_t>(
          "client_max_retries_on_remount_failure");
        
        std::pair<int, bool> test_result;
        uint64_t i = 0;
        int tr = 0;
        
        // 重试循环：执行 dentry 处理测试
        do {
          test_result = client->test_dentry_handling(can_invalidate_dentries);
          tr = test_result.first;
          if (tr) {
            sleep(1);  // 测试失败时等待 1 秒后重试
          }
        } while (++i < max_retries && tr);

        // 检查是否需要中止程序
	bool abort_on_failure = test_result.second;
        bool client_die_on_failed_dentry_invalidate = g_conf().get_val<bool>(
          "client_die_on_failed_dentry_invalidate");
          
        // 如果测试失败且配置为失败时中止
	if (tr != 0 && client_die_on_failed_dentry_invalidate) {
	  cerr << "ceph-fuse[" << getpid()
	       << "]: fuse failed dentry invalidate/remount test with error "
	       << cpp_strerror(tr) << ", stopping" << std::endl;

	  // 尝试卸载文件系统
	  char buf[5050];
	  string mountpoint = cfuse->get_mount_point();
	  snprintf(buf, sizeof(buf), "fusermount -u -z %s", mountpoint.c_str());
	  int umount_r = system(buf);
	  
	  if (umount_r) {
	    if (umount_r != -1) {
	      if (WIFEXITED(umount_r)) {
		umount_r = WEXITSTATUS(umount_r);
		cerr << "got error " << umount_r
		     << " when unmounting Ceph on failed remount test!" << std::endl;
	      } else {
		cerr << "attempt to umount on failed remount test failed (on a signal?)" << std::endl;
	      }
	    } else {
	      cerr << "system() invocation failed during remount test" << std::endl;
	    }
	  }
	}
	
	// 如果需要中止，则调用 ceph_abort()
	if(abort_on_failure) {
	  ceph_abort();
	}
	
	return reinterpret_cast<void*>(tr);
#else
        // 非 Linux 平台：不执行测试，直接返回成功
	return reinterpret_cast<void*>(0);
#endif
      }
    } tester;  // 创建测试线程对象

    // ==================== 第七阶段：创建核心组件 ====================
    
    // 声明核心组件变量
    Messenger *messenger = nullptr;  // 消息传递器
    StandaloneClient *client;        // 独立客户端
    CephFuse *cfuse;                 // Ceph FUSE 接口
    UserPerm perms;                  // 用户权限
    int tester_r = 0;                // 测试线程返回值
    void *tester_rp = nullptr;       // 测试线程返回指针

    // 启动异步 I/O 上下文池
    icp.start(cct->_conf.get_val<std::uint64_t>("client_asio_thread_count"));
    
    // 创建监控客户端并构建初始监控映射
    MonClient *mc = new MonClient(g_ceph_context, icp);
    int r = mc->build_initial_monmap();
    
    if (r == -EINVAL) {
      cerr << "failed to generate initial mon list" << std::endl;
      exit(1);
    }
    if (r < 0)
      goto out_mc_start_failed;

    // ==================== 第八阶段：启动网络和客户端组件 ====================
    
    // 创建客户端消息传递器
    messenger = Messenger::create_client_messenger(g_ceph_context, "client");
    messenger->set_default_policy(Messenger::Policy::lossy_client(0));
    messenger->set_policy(entity_name_t::TYPE_MDS,
			  Messenger::Policy::lossless_client(0));

    // 创建独立客户端
    client = new StandaloneClient(messenger, mc, icp);
    if (filer_flags) {
      client->set_filer_flags(filer_flags);  // 设置文件操作标志
    }

    // 创建 Ceph FUSE 接口
    cfuse = new CephFuse(client, forker.get_signal_fd());

    // 初始化 FUSE 接口
    r = cfuse->init(newargc, newargv);
    if (r != 0) {
      cerr << "ceph-fuse[" << getpid() << "]: fuse failed to initialize" << std::endl;
      goto out_messenger_start_failed;
    }

    // ==================== 第九阶段：启动消息传递器和客户端 ====================
    
    cerr << "ceph-fuse[" << getpid() << "]: starting ceph client" << std::endl;
    r = messenger->start();
    if (r < 0) {
      cerr << "ceph-fuse[" << getpid() << "]: ceph messenger failed with " << cpp_strerror(-r) << std::endl;
      goto out_messenger_start_failed;
    }

    // 初始化客户端
    r = client->init();
    if (r < 0) {
      cerr << "ceph-fuse[" << getpid() << "]: ceph client failed with " << cpp_strerror(-r) << std::endl;
      goto out_init_failed;
    }
    
    // 更新客户端元数据
    client->update_metadata("mount_point", cfuse->get_mount_point());
    perms = client->pick_my_perms();
    
    // ==================== 第十阶段：挂载 Ceph 文件系统 ====================
    
    {
      // 获取挂载点配置
      auto client_mountpoint = g_conf().get_val<std::string>(
        "client_mountpoint");
      auto mountpoint = client_mountpoint.c_str();
      auto fuse_require_active_mds = g_conf().get_val<bool>(
        "fuse_require_active_mds");
        
      // 挂载 Ceph 文件系统
      r = client->mount(mountpoint, perms, fuse_require_active_mds);
      if (r < 0) {
        if (r == CEPH_FUSE_NO_MDS_UP) {
          cerr << "ceph-fuse[" << getpid() << "]: probably no MDS server is up?" << std::endl;
        }
        cerr << "ceph-fuse[" << getpid() << "]: ceph mount failed with " << cpp_strerror(-r) << std::endl;
        r = EXIT_FAILURE;
        goto out_shutdown;
      }
    }

    // ==================== 第十一阶段：启动 FUSE 和测试线程 ====================
    
    // 启动 FUSE 接口
    r = cfuse->start();
    if (r != 0) {
      cerr << "ceph-fuse[" << getpid() << "]: fuse failed to start" << std::endl;
      goto out_client_unmount;
    }

    cerr << "ceph-fuse[" << getpid() << "]: starting fuse" << std::endl;
    
    // 初始化并启动测试线程
    tester.init(cfuse, client);
    tester.create("tester");
    
    // 启动 FUSE 主循环（阻塞直到文件系统卸载）
    r = cfuse->loop();
    
    // 等待测试线程完成并获取结果
    tester.join(&tester_rp);
    tester_r = static_cast<int>(reinterpret_cast<uint64_t>(tester_rp));
    cerr << "ceph-fuse[" << getpid() << "]: fuse finished with error " << r
	 << " and tester_r " << tester_r <<std::endl;

    // ==================== 第十二阶段：清理和退出 ====================
    
  out_client_unmount:
    // 卸载客户端
    client->unmount();
    cfuse->finalize();
    
  out_shutdown:
    // 停止异步 I/O 上下文池
    icp.stop();
    client->shutdown();
    
  out_init_failed:
    // 注销信号处理器
    unregister_async_signal_handler(SIGHUP, sighup_handler);
    shutdown_async_signal_handler();

    // 等待消息传递器完成
    messenger->shutdown();
    messenger->wait();
    
  out_messenger_start_failed:
    // 清理 FUSE 接口
    delete cfuse;
    cfuse = nullptr;
    
    // 清理客户端
    delete client;
    client = nullptr;
    
    // 清理消息传递器
    delete messenger;
    messenger = nullptr;
    
  out_mc_start_failed:
    // 清理参数数组和监控客户端
    free(newargv);
    delete mc;
    mc = nullptr;
    
    //cout << "child done" << std::endl;
    return forker.signal_exit(r);
  }
}
