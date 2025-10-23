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
 * @file ceph_syn.cc
 * @brief Ceph 同步客户端工具主程序
 * 
 * 本文件是 Ceph 同步客户端工具的主入口程序，用于创建和管理多个同步客户端，
 * 执行文件系统操作和性能测试。该工具主要用于测试 Ceph 文件系统的性能和稳定性。
 * 
 * 主要功能：
 * - 创建多个同步客户端实例，模拟并发访问
 * - 执行文件系统操作（创建、读取、写入、删除等）
 * - 进行性能测试和压力测试
 * - 验证 Ceph 文件系统的并发处理能力
 * - 支持自定义测试模式和参数
 * 
 * 核心组件：
 * - SyntheticClient：合成客户端，执行测试操作
 * - StandaloneClient：独立客户端，提供文件系统接口
 * - Messenger：网络通信层，处理与集群的通信
 * - MonClient：与 MON 的通信客户端
 * - 异步 I/O 上下文池：提供异步处理能力
 * 
 * 工作模式：
 * - 多客户端并发模式：创建多个客户端同时执行操作
 * - 性能测试模式：测量文件系统操作的性能指标
 * - 压力测试模式：验证系统在高负载下的稳定性
 * 
 * 使用示例：
 * - 基本测试：ceph-syn
 * - 自定义客户端数量：ceph-syn --num-client 10
 * - 性能测试：ceph-syn --test-type performance
 * 
 * 特性：
 * - 支持多客户端并发测试
 * - 提供丰富的测试模式和参数
 * - 支持性能指标收集和分析
 * - 提供详细的测试结果输出
 * - 支持自定义测试场景和操作
 */

// 标准 C 库头文件
#include <sys/stat.h>    // 文件状态信息
#include <sys/types.h>   // 系统数据类型定义
#include <fcntl.h>       // 文件控制操作

// 标准 C++ 库头文件
#include <iostream>      // 输入输出流
#include <string>        // 字符串类

// Ceph 通用组件
#include "common/config.h"         // 配置管理系统
#include "common/Timer.h"          // 定时器
#include "common/ceph_argparse.h"  // 命令行参数解析
#include "common/pick_address.h"   // 地址选择工具

// Ceph 异步组件
#include "common/async/context_pool.h" // 异步上下文池

// Ceph 客户端组件
#include "client/SyntheticClient.h" // 合成客户端
#include "client/Client.h"          // 客户端基类

// Ceph 消息传递组件
#include "msg/Messenger.h" // 网络消息传递器

// Ceph 监控组件
#include "mon/MonClient.h" // MON 客户端

// Ceph 全局组件
#include "global/global_init.h" // 全局初始化

// 标准命名空间使用声明
using namespace std;

/**
 * @brief 外部声明的同步文件标志
 * 
 * 用于配置同步客户端的文件操作标志，定义在 SyntheticClient 相关模块中。
 * 这些标志控制文件操作的特定行为，如缓存策略、同步模式等。
 */
extern int syn_filer_flags;

/**
 * @brief Ceph 同步客户端工具主函数
 * 
 * 这是 ceph-syn 程序的主入口点，负责创建和管理多个同步客户端，
 * 执行文件系统操作和性能测试。
 * 
 * 主要执行流程：
 * 1. 参数解析：解析命令行参数和配置文件
 * 2. 初始化：创建全局上下文、解析同步选项
 * 3. 网络设置：选择网络地址、创建 MON 客户端
 * 4. 客户端创建：创建多个同步客户端实例
 * 5. 测试执行：启动客户端线程执行测试操作
 * 6. 清理退出：等待完成并清理资源
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @param envp 环境变量数组
 * @return 0 程序成功执行
 * @return 非0 程序执行失败，返回相应的错误码
 * 
 * @note 该工具主要用于测试 Ceph 文件系统的性能和稳定性
 */
int main(int argc, const char **argv, char *envp[]) 
{
  // ==================== 第一阶段：程序初始化和参数解析 ====================
  
  // 将命令行参数转换为向量格式，便于处理
  auto args = argv_to_vec(argc, argv);

  // 初始化全局 Ceph 上下文
  auto cct = global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT,
			 CODE_ENVIRONMENT_UTILITY, 0);
  common_init_finish(g_ceph_context);

  // 解析同步客户端特定选项
  parse_syn_options(args);   // for SyntheticClient

  // 选择网络地址
  pick_addresses(g_ceph_context, CEPH_PICK_ADDRESS_PUBLIC);

  // ==================== 第二阶段：网络和 MON 客户端初始化 ====================
  
  // 创建异步 I/O 上下文池
  ceph::async::io_context_pool  poolctx(1);
  
  // 创建 MON 客户端并获取 MON 映射
  MonClient mc(g_ceph_context, poolctx);
  if (mc.build_initial_monmap() < 0)
    return -1;

  // ==================== 第三阶段：客户端容器初始化 ====================
  
  // 创建客户端和合成客户端的容器
  list<Client*> clients;                    // 客户端列表
  list<SyntheticClient*> synclients;        // 合成客户端列表
  vector<Messenger*> messengers{static_cast<unsigned>(num_client), nullptr};  // 消息传递器向量
  vector<MonClient*> mclients{static_cast<unsigned>(num_client), nullptr};    // MON 客户端向量

  // ==================== 第四阶段：创建多个同步客户端 ====================
  
  // 输出启动信息
  cout << "ceph-syn: starting " << num_client << " syn client(s)" << std::endl;
  
  // 为每个客户端创建必要的组件
  for (int i=0; i<num_client; i++) {
    // 创建客户端消息传递器
    messengers[i] = Messenger::create_client_messenger(g_ceph_context,
						       "synclient");
    
    // 创建 MON 客户端
    mclients[i] = new MonClient(g_ceph_context, poolctx);
    mclients[i]->build_initial_monmap();
    
    // 创建独立客户端
    auto client = new StandaloneClient(messengers[i], mclients[i], poolctx);
    client->set_filer_flags(syn_filer_flags);
    
    // 创建合成客户端
    SyntheticClient *syn = new SyntheticClient(client);
    
    // 添加到容器中
    clients.push_back(client);
    synclients.push_back(syn);
    
    // 启动消息传递器
    messengers[i]->start();
  }

  // ==================== 第五阶段：启动测试线程 ====================
  
  // 启动所有合成客户端的测试线程
  for (list<SyntheticClient*>::iterator p = synclients.begin(); 
       p != synclients.end();
       ++p)
    (*p)->start_thread();

  // 停止异步 I/O 上下文池
  poolctx.stop();

  // ==================== 第六阶段：等待测试完成和清理 ====================
  
  // 等待所有客户端完成测试
  while (!clients.empty()) {
    Client *client = clients.front();
    SyntheticClient *syn = synclients.front();
    clients.pop_front();
    synclients.pop_front();
    
    // 等待合成客户端线程完成
    syn->join_thread();
    
    // 删除合成客户端和客户端
    delete syn;
    delete client;
  }

  // ==================== 第七阶段：清理消息传递器和 MON 客户端 ====================
  
  // 清理所有消息传递器和 MON 客户端
  for (int i = 0; i < num_client; ++i) {
    // 删除 MON 客户端
    delete mclients[i];
    
    // 关闭并等待消息传递器完成
    messengers[i]->shutdown();
    messengers[i]->wait();
    delete messengers[i];
  }
  
  return 0;
}
