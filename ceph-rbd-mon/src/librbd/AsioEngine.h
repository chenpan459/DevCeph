// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/**
 * @file AsioEngine.h
 * @brief librbd异步引擎类的头文件
 *
 * 本文件定义了AsioEngine类，它是librbd中基于Boost.Asio的异步I/O引擎。
 * 负责管理异步操作的调度、执行和回调，提供线程安全的异步处理机制。
 */

#ifndef CEPH_LIBRBD_ASIO_ENGINE_H
#define CEPH_LIBRBD_ASIO_ENGINE_H

/// @brief 通用前向声明头文件
#include "include/common_fwd.h"
/// @brief RADOS库前向声明头文件
#include "include/rados/librados_fwd.hpp"
/// @brief 智能指针支持
#include <memory>
/// @brief Boost.Asio调度功能
#include <boost/asio/dispatch.hpp>
/// @brief Boost.Asio I/O上下文
#include <boost/asio/io_context.hpp>
/// @brief Boost.Asio strand（串行执行器）
#include <boost/asio/strand.hpp>
/// @brief Boost.Asio投递功能
#include <boost/asio/post.hpp>

/// @brief 前向声明：异步操作完成回调上下文
struct Context;
/// @brief 前向声明：neorados的RADOS接口
namespace neorados { struct RADOS; }

/// @brief librbd主命名空间
namespace librbd {

/// @brief librbd异步I/O子系统命名空间
namespace asio { struct ContextWQ; }

/**
 * @class AsioEngine
 * @brief 基于Boost.Asio的异步引擎类
 *
 * AsioEngine是librbd中负责异步I/O操作的核心引擎。它封装了Boost.Asio的io_context、
 * strand和相关组件，为librbd提供高性能的异步处理能力。
 *
 * 主要功能：
 * - 管理Boost.Asio的io_context生命周期
 * - 提供线程安全的API回调串行化（strand）
 * - 支持异步任务调度和执行
 * - 集成RADOS异步API
 * - 维护工作队列（ContextWQ）
 *
 * 设计特点：
 * - 单例模式，一个ImageCtx对应一个AsioEngine实例
 * - 线程安全，支持多线程并发访问
 * - 自动管理线程池资源分配
 * - 与librados的异步机制无缝集成
 */
class AsioEngine {
public:
  /**
   * @brief 构造函数（使用RADOS实例）
   * @param rados RADOS集群连接实例的共享指针
   *
   * 创建AsioEngine实例，初始化Boost.Asio的io_context和相关组件。
   * 会根据配置自动调整线程池大小，确保与librados的线程数匹配。
   */
  explicit AsioEngine(std::shared_ptr<librados::Rados> rados);

  /**
   * @brief 构造函数（使用IoCtx）
   * @param io_ctx RADOS I/O上下文引用
   *
   * 便利构造函数，内部创建临时的RADOS实例然后调用主构造函数。
   */
  explicit AsioEngine(librados::IoCtx& io_ctx);

  /// @brief 析构函数，清理所有异步资源
  ~AsioEngine();

  /// @brief 删除移动构造函数，防止对象被移动
  AsioEngine(AsioEngine&&) = delete;
  /// @brief 删除拷贝构造函数，防止对象被拷贝
  AsioEngine(const AsioEngine&) = delete;
  /// @brief 删除赋值运算符，防止对象被赋值
  AsioEngine& operator=(const AsioEngine&) = delete;

  /**
   * @brief 获取RADOS API接口引用
   * @return neorados::RADOS对象的引用
   *
   * 提供对底层neorados API的访问，支持与RADOS的直接交互。
   */
  inline neorados::RADOS& get_rados_api() {
    return *m_rados_api;
  }

  /**
   * @brief 获取Boost.Asio I/O上下文引用
   * @return boost::asio::io_context对象的引用
   *
   * 提供对底层io_context的访问，支持直接使用Boost.Asio功能。
   */
  inline boost::asio::io_context& get_io_context() {
    return m_io_context;
  }

  /**
   * @brief 类型转换运算符，重载为io_context引用
   * @return boost::asio::io_context对象的引用
   *
   * 允许AsioEngine对象直接作为io_context使用，提供语法糖。
   */
  inline operator boost::asio::io_context&() {
    return m_io_context;
  }

  /// @brief 执行器类型别名，方便使用
  using executor_type = boost::asio::io_context::executor_type;

  /**
   * @brief 获取关联的执行器
   * @return io_context的执行器对象
   *
   * 提供对io_context执行器的访问，支持更高级的异步编程模式。
   */
  inline executor_type get_executor() {
    return m_io_context.get_executor();
  }

  /**
   * @brief 获取API串行执行器（strand）
   * @return boost::asio::strand对象的引用
   *
   * API客户端回调应该永远不会并发执行，通过strand确保串行化。
   * 防止多个异步操作同时修改共享状态导致的竞态条件。
   */
  inline boost::asio::strand<executor_type>& get_api_strand() {
    // API client callbacks should never fire concurrently
    return *m_api_strand;
  }

  /**
   * @brief 获取工作队列指针
   * @return ContextWQ对象的指针
   *
   * 提供对librbd专用工作队列的访问，用于处理Context回调。
   */
  inline asio::ContextWQ* get_work_queue() {
    return m_context_wq.get();
  }

  /**
   * @brief 调度任务立即执行（模板版本）
   * @tparam T 可调用对象类型
   * @param t 要立即执行的可调用对象
   *
   * 使用boost::asio::dispatch确保任务在io_context的线程中立即执行。
   * 适用于需要在io_context线程中同步执行的任务。
   */
  template <typename T>
  void dispatch(T&& t) {
    boost::asio::dispatch(m_io_context, std::forward<T>(t));
  }

  /**
   * @brief 调度Context对象立即执行
   * @param ctx 要执行的Context对象指针
   * @param r 传递给Context::complete()的参数
   *
   * 将Context对象包装成Lambda并立即调度执行。
   * 这是librbd中最常用的异步执行方式。
   */
  void dispatch(Context* ctx, int r);

  /**
   * @brief 投递任务异步执行（模板版本）
   * @tparam T 可调用对象类型
   * @param t 要异步执行的可调用对象
   *
   * 使用boost::asio::post将任务投递到io_context的线程池中异步执行。
   * 适用于不需要立即执行的后台任务。
   */
  template <typename T>
  void post(T&& t) {
    boost::asio::post(m_io_context, std::forward<T>(t));
  }

  /**
   * @brief 投递Context对象异步执行
   * @param ctx 要执行的Context对象指针
   * @param r 传递给Context::complete()的参数
   *
   * 将Context对象包装成Lambda并投递到线程池中异步执行。
   * 这是AsyncOperation等组件最常用的异步通知机制。
   */
  void post(Context* ctx, int r);

private:
  /// @brief neorados RADOS API封装的共享指针
  /// @details 提供对RADOS集群的异步访问接口，与librados集成
  std::shared_ptr<neorados::RADOS> m_rados_api;

  /// @brief Ceph上下文指针，用于配置和日志访问
  CephContext* m_cct;

  /// @brief Boost.Asio I/O上下文的引用
  /// @details 核心异步I/O服务，管理所有异步操作的调度和执行
  boost::asio::io_context& m_io_context;

  /// @brief API串行执行器（strand）的独占指针
  /// @details 确保API客户端回调串行执行，防止竞态条件
  std::unique_ptr<boost::asio::strand<executor_type>> m_api_strand;

  /// @brief librbd专用工作队列的独占指针
  /// @details 处理Context回调的工作队列，与Boost.Asio集成
  std::unique_ptr<asio::ContextWQ> m_context_wq;
};

/// @brief 结束librbd主命名空间
} // namespace librbd

/// @brief 头文件保护宏结束，防止重复包含
#endif // CEPH_LIBRBD_ASIO_ENGINE_H
