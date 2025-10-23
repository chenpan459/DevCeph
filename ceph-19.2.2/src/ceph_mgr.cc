// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Red Hat Inc
 *
 * Author: John Spray <john.spray@redhat.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

/**
 * @file ceph_mgr.cc
 * @brief Ceph Manager (MGR) 守护进程主程序
 * 
 * 本文件是 Ceph 集群管理服务的主入口程序，负责启动和管理 Ceph Manager 守护进程。
 * MGR 是 Ceph 集群的管理层组件，提供集群监控、管理和扩展功能。
 * 
 * 主要功能：
 * - 启动 MGR 守护进程，提供集群管理服务
 * - 支持 Python 模块扩展，提供丰富的管理功能
 * - 提供集群状态监控和性能指标收集
 * - 支持 Dashboard、REST API 等管理接口
 * - 管理集群配置和策略
 * - 提供插件化的管理功能扩展
 * 
 * 核心组件：
 * - MgrStandby：MGR 待机实例，负责初始化和主循环
 * - Python 解释器：支持 Python 模块扩展
 * - 消息传递：与 MON、OSD 等组件通信
 * - 配置管理：处理 MGR 相关配置
 * 
 * 工作模式：
 * - 待机模式：等待成为活跃 MGR
 * - 活跃模式：提供集群管理服务
 * - 故障转移：支持 MGR 高可用性
 * 
 * 使用示例：
 * - 启动服务：ceph-mgr -i <id>
 * - 查看帮助：ceph-mgr -h
 * 
 * 特性：
 * - 支持多 MGR 实例，提供高可用性
 * - 模块化架构，支持功能扩展
 * - 与 Ceph 集群深度集成
 * - 提供丰富的管理接口和工具
 */

// Python 解释器头文件
#include <Python.h>        // Python C API，用于支持 Python 模块扩展

// 系统头文件
#include <pthread.h>       // POSIX 线程库

// Ceph 通用类型和兼容性
#include "include/types.h"  // Ceph 基本类型定义
#include "include/compat.h" // 兼容性支持

// Ceph 通用组件
#include "common/config.h"        // 配置管理系统
#include "common/ceph_argparse.h" // 命令行参数解析
#include "common/errno.h"         // 错误码定义
#include "common/pick_address.h"  // 地址选择工具

// Ceph 全局组件
#include "global/global_init.h"   // 全局初始化

// Ceph MGR 核心组件
#include "mgr/MgrStandby.h"       // MGR 待机实例实现

/**
 * @brief 显示 ceph-mgr 使用帮助信息
 * 
 * 输出 ceph-mgr 命令的基本使用说明，包括必需的参数和通用服务器选项。
 * 这是 MGR 守护进程的简洁帮助信息。
 */
static void usage()
{
  std::cout << "usage: ceph-mgr -i <ID> [flags]\n"
	    << std::endl;
  generic_server_usage();
}

/**
 * @brief Ceph Manager 守护进程主函数
 * 
 * 这是 ceph-mgr 程序的主入口点，负责启动 Ceph Manager 守护进程。
 * 程序采用简洁的设计，主要工作是创建 MgrStandby 实例并将控制权移交给它。
 * 
 * 主要执行流程：
 * 1. 设置进程名和线程名
 * 2. 解析命令行参数和配置
 * 3. 初始化全局 Ceph 上下文
 * 4. 选择网络地址
 * 5. 执行守护进程初始化
 * 6. 创建 MgrStandby 实例
 * 7. 初始化 MGR 并进入主循环
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 0 程序成功执行
 * @return 非0 程序执行失败，返回相应的错误码
 * 
 * @note MGR 支持高可用性，多个 MGR 实例中只有一个处于活跃状态
 */
int main(int argc, const char **argv)
{
  // ==================== 第一阶段：程序初始化和参数解析 ====================
  
  // 设置线程名，便于调试和监控
  ceph_pthread_setname(pthread_self(), "ceph-mgr");

  // 将命令行参数转换为向量格式，便于处理
  auto args = argv_to_vec(argc, argv);
  
  // 检查基本参数
  if (args.empty()) {
    std::cerr << argv[0] << ": -h or --help for usage" << std::endl;
    exit(1);
  }
  
  // 检查是否需要显示使用帮助
  if (ceph_argparse_need_usage(args)) {
    usage();
    exit(0);
  }

  // ==================== 第二阶段：全局初始化和配置 ====================
  
  // 设置 MGR 特定的默认配置值
  std::map<std::string,std::string> defaults = {
    { "keyring", "$mgr_data/keyring" }  // 默认密钥环路径
  };
  
  // 初始化全局 Ceph 上下文
  auto cct = global_init(&defaults, args, CEPH_ENTITY_TYPE_MGR,
			 CODE_ENVIRONMENT_DAEMON, 0);

  // ==================== 第三阶段：网络地址选择和守护进程初始化 ====================
  
  // 选择公共网络地址
  pick_addresses(g_ceph_context, CEPH_PICK_ADDRESS_PUBLIC);

  // 执行守护进程初始化
  global_init_daemonize(g_ceph_context);
  global_init_chdir(g_ceph_context);
  common_init_finish(g_ceph_context);

  // ==================== 第四阶段：创建和启动 MGR 实例 ====================
  
  // 创建 MgrStandby 实例
  MgrStandby mgr(argc, argv);
  
  // 初始化 MGR
  int rc = mgr.init();
  if (rc != 0) {
      std::cerr << "Error in initialization: " << cpp_strerror(rc) << std::endl;
      return rc;
  }

  // 进入 MGR 主循环
  return mgr.main(args);
}

