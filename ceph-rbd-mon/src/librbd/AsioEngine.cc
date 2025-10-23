// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/**
 * @file AsioEngine.cc
 * @brief AsioEngine类的实现文件
 *
 * 本文件实现了AsioEngine类的方法，负责初始化Boost.Asio异步引擎、
 * 管理线程池资源分配以及提供异步任务调度功能。
 */

/// @brief AsioEngine头文件
#include "librbd/AsioEngine.h"
/// @brief Context基类定义
#include "include/Context.h"
/// @brief neorados RADOS封装
#include "include/neorados/RADOS.hpp"
/// @brief librados库接口
#include "include/rados/librados.hpp"
/// @brief Ceph调试输出宏
#include "common/dout.h"
/// @brief librbd异步工作队列
#include "librbd/asio/ContextWQ.h"

/// @brief 定义调试子系统为RBD
#define dout_subsys ceph_subsys_rbd
/// @brief 取消之前的调试前缀定义
#undef dout_prefix
/// @brief 定义本模块的调试前缀
#define dout_prefix *_dout << "librbd::AsioEngine: " \
                           << this << " " << __func__ << ": "

/// @brief librbd主命名空间
namespace librbd {

/// @brief 主构造函数，初始化完整的异步引擎
/// @param rados RADOS集群连接实例的共享指针
///
/// 创建AsioEngine实例，执行以下初始化步骤：
/// 1. 创建neorados::RADOS封装，提供异步RADOS API访问
/// 2. 获取Ceph上下文用于配置和日志访问
/// 3. 获取底层io_context引用作为异步I/O服务核心
/// 4. 创建API串行执行器（strand）确保回调串行化
/// 5. 创建librbd专用工作队列处理Context回调
/// 6. 根据配置动态调整线程池大小，确保资源充足
AsioEngine::AsioEngine(std::shared_ptr<librados::Rados> rados)
  : m_rados_api(std::make_shared<neorados::RADOS>(
      neorados::RADOS::make_with_librados(*rados))),
    m_cct(m_rados_api->cct()),
    m_io_context(m_rados_api->get_io_context()),
    m_api_strand(std::make_unique<boost::asio::strand<executor_type>>(
      boost::asio::make_strand(m_io_context))),
    m_context_wq(std::make_unique<asio::ContextWQ>(m_cct, m_io_context)) {
  ldout(m_cct, 20) << dendl;

  auto rados_threads = m_cct->_conf.get_val<uint64_t>("librados_thread_count");
  auto rbd_threads = m_cct->_conf.get_val<uint64_t>("rbd_op_threads");
  if (rbd_threads > rados_threads) {
    // inherit the librados thread count -- but increase it if librbd wants to
    // utilize more threads
    m_cct->_conf.set_val_or_die("librados_thread_count",
                                std::to_string(rbd_threads));
    m_cct->_conf.apply_changes(nullptr);
  }
}

/// @brief 便利构造函数，使用IoCtx创建AsioEngine
/// @param io_ctx RADOS I/O上下文引用
///
/// 创建临时的RADOS实例，然后委托给主构造函数。
/// 适用于只需要I/O上下文而不需要完整RADOS控制的情况。
AsioEngine::AsioEngine(librados::IoCtx& io_ctx)
  : AsioEngine(std::make_shared<librados::Rados>(io_ctx)) {
}

/// @brief 析构函数，清理异步引擎资源
///
/// 执行清理工作：
/// 1. 重置API串行执行器（strand），确保所有异步操作完成
/// 2. 记录调试日志
/// 3. 所有其他资源由智能指针自动管理
AsioEngine::~AsioEngine() {
  ldout(m_cct, 20) << dendl;
  m_api_strand.reset();
}

/// @brief 调度Context对象立即执行
/// @param ctx 要执行的Context对象指针
/// @param r 传递给Context::complete()的参数
///
/// 使用dispatch模板方法将Context包装成Lambda表达式，
/// 在io_context线程中立即执行。这是同步执行的调度方式。
void AsioEngine::dispatch(Context* ctx, int r) {
  dispatch([ctx, r]() { ctx->complete(r); });
}

/// @brief 投递Context对象异步执行
/// @param ctx 要执行的Context对象指针
/// @param r 传递给Context::complete()的参数
///
/// 使用post模板方法将Context包装成Lambda表达式，
/// 投递到io_context的线程池中异步执行。这是librbd中最常用的异步执行方式，
/// 用于实现跨线程的异步通知机制。
void AsioEngine::post(Context* ctx, int r) {
  post([ctx, r]() { ctx->complete(r); });
}

} // namespace librbd
