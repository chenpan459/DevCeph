// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/**
 * @file AsyncOperation.cc
 * @brief AsyncOperation类的实现文件
 *
 * 本文件实现了AsyncOperation类的核心功能，包括异步操作的启动、完成和刷新机制。
 * 提供了操作生命周期管理、刷新操作传递链等关键功能。
 */

/// @brief AsyncOperation头文件
#include "librbd/io/AsyncOperation.h"
/// @brief Ceph断言宏，用于运行时检查
#include "include/ceph_assert.h"
/// @brief Ceph调试输出宏
#include "common/dout.h"
/// @brief librbd异步引擎，用于异步操作执行
#include "librbd/AsioEngine.h"
/// @brief 图像上下文类定义
#include "librbd/ImageCtx.h"

/// @brief 定义调试子系统为RBD
#define dout_subsys ceph_subsys_rbd
/// @brief 取消之前的调试前缀定义
#undef dout_prefix
/// @brief 定义本模块的调试前缀
#define dout_prefix *_dout << "librbd::io::AsyncOperation: "

namespace librbd {
namespace io {

namespace {

/// @brief 完成刷新操作的上下文类
/// @details 当异步操作完成时，使用此上下文来执行所有待完成的刷新操作
struct C_CompleteFlushes : public Context {
  /// @brief 关联的图像上下文
  ImageCtx *image_ctx;
  /// @brief 待完成的刷新操作列表
  std::list<Context *> flush_contexts;

  /**
   * @brief 构造函数
   * @param image_ctx 图像上下文指针
   * @param flush_contexts 待完成的刷新操作列表（右值引用，避免拷贝）
   */
  explicit C_CompleteFlushes(ImageCtx *image_ctx,
                             std::list<Context *> &&flush_contexts)
    : image_ctx(image_ctx), flush_contexts(std::move(flush_contexts)) {
  }

  /**
   * @brief 完成回调函数
   * @param r 完成结果码
   *
   * 遍历所有待完成的刷新操作，依次调用它们的完成回调。
   * 使用图像上下文的所有者锁确保线程安全。
   */
  void finish(int r) override {
    std::shared_lock owner_locker{image_ctx->owner_lock};
    while (!flush_contexts.empty()) {
      Context *flush_ctx = flush_contexts.front();
      flush_contexts.pop_front();

      ldout(image_ctx->cct, 20) << "completed flush: " << flush_ctx << dendl;
      flush_ctx->complete(0);
    }
  }
};

} // anonymous namespace

/**
 * @brief 启动异步操作
 * @param image_ctx 关联的图像上下文
 *
 * 将操作与指定的ImageCtx关联，并将其加入到图像的异步操作链表的头部。
 * 操作启动后即可开始执行异步I/O操作。
 *
 * @note 断言确保操作尚未与任何ImageCtx关联，防止重复启动。
 */
void AsyncOperation::start_op(ImageCtx &image_ctx) {
  ceph_assert(m_image_ctx == NULL);
  m_image_ctx = &image_ctx;

  ldout(m_image_ctx->cct, 20) << this << " " << __func__ << dendl;
  std::lock_guard l{m_image_ctx->async_ops_lock};
  m_image_ctx->async_ops.push_front(&m_xlist_item);
}

/**
 * @brief 完成异步操作
 *
 * 标记操作完成，从链表中移除，并处理所有待完成的刷新操作。
 * 实现了操作间的刷新上下文传递机制，确保刷新操作的正确执行顺序。
 *
 * 刷新操作传递逻辑：
 * 1. 如果有更新的操作，将刷新操作传递给下一个操作
 * 2. 如果没有更新的操作，直接执行所有刷新操作
 * 3. 使用异步引擎确保刷新操作在正确的线程上下文中执行
 */
void AsyncOperation::finish_op() {
  ldout(m_image_ctx->cct, 20) << this << " " << __func__ << dendl;

  {
    std::lock_guard l{m_image_ctx->async_ops_lock};
    xlist<AsyncOperation *>::iterator iter(&m_xlist_item);
    ++iter;
    ceph_assert(m_xlist_item.remove_myself());

    // linked list stored newest -> oldest ops
    if (!iter.end() && !m_flush_contexts.empty()) {
      ldout(m_image_ctx->cct, 20) << "moving flush contexts to previous op: "
                                  << *iter << dendl;
      (*iter)->m_flush_contexts.insert((*iter)->m_flush_contexts.end(),
                                       m_flush_contexts.begin(),
                                       m_flush_contexts.end());
      return;
    }
  }

  if (!m_flush_contexts.empty()) {
    C_CompleteFlushes *ctx = new C_CompleteFlushes(m_image_ctx,
                                                   std::move(m_flush_contexts));
    m_image_ctx->asio_engine->post(ctx, 0);
  }
}

/**
 * @brief 添加刷新操作到队列
 * @param on_finish 刷新完成回调上下文
 *
 * 将刷新操作加入适当的队列中。根据链表顺序（最新->最老的操作），
 * 决定是将刷新操作加入到下一个操作的队列还是立即执行。
 *
 * 刷新操作调度策略：
 * - 如果当前操作不是最新的操作，将刷新操作传递给更新的操作
 * - 如果当前操作是最新的操作，直接通过异步引擎执行刷新操作
 */
void AsyncOperation::flush(Context* on_finish) {
  {
    std::lock_guard locker{m_image_ctx->async_ops_lock};
    xlist<AsyncOperation *>::iterator iter(&m_xlist_item);
    ++iter;

    // linked list stored newest -> oldest ops
    if (!iter.end()) {
      (*iter)->m_flush_contexts.push_back(on_finish);
      return;
    }
  }

  m_image_ctx->asio_engine->post(on_finish, 0);
}

} // namespace io
} // namespace librbd
