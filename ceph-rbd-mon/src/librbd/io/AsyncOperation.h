// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/**
 * @file AsyncOperation.h
 * @brief librbd异步操作管理类的头文件
 *
 * 本文件定义了AsyncOperation类，用于管理librbd中异步I/O操作的生命周期。
 * 该类提供了操作的启动、完成、刷新机制，确保异步操作的正确执行顺序。
 */

#ifndef LIBRBD_IO_ASYNC_OPERATION_H
#define LIBRBD_IO_ASYNC_OPERATION_H

/// @brief Ceph断言宏头文件，用于运行时断言检查
#include "include/ceph_assert.h"
/// @brief Ceph侵入式链表容器头文件，用于高效的链表操作
#include "include/xlist.h"
/// @brief 标准库列表容器头文件
#include <list>

/// @brief 前向声明：异步操作完成回调上下文类
class Context;

/// @brief librbd 主命名空间，包含RBD（RADOS块设备）库的所有接口
namespace librbd {

/// @brief 前向声明：图像上下文类
class ImageCtx;

/// @brief I/O子系统命名空间，包含异步操作、调度器、请求处理等核心组件
namespace io {

/**
 * @class AsyncOperation
 * @brief 异步操作管理类
 *
 * AsyncOperation类是librbd中用于管理异步I/O操作生命周期的核心类。
 * 它提供了操作的启动、完成和刷新机制，确保异步操作能够正确执行并维护操作顺序。
 *
 * 主要功能：
 * - 跟踪异步操作的执行状态
 * - 管理操作的启动和完成
 * - 处理刷新操作的排队和执行
 * - 维护操作间的依赖关系和执行顺序
 *
 * 使用场景：
 * - 异步读写操作的生命周期管理
 * - 刷新操作的依赖链维护
 * - 操作完成回调的协调
 *
 * 设计特点：
 * - 使用侵入式链表管理操作队列
 * - 支持操作间的刷新上下文传递
 * - 确保操作的内存安全性
 */
class AsyncOperation {
public:
  /**
   * @brief 默认构造函数
   *
   * 初始化异步操作对象，将其加入侵入式链表。
   * 初始状态下操作未启动，未与任何ImageCtx关联。
   */
  AsyncOperation()
    : m_image_ctx(NULL), m_xlist_item(this)
  {
  }

  /**
   * @brief 析构函数
   *
   * 确保操作已从链表中移除，防止内存泄漏和野指针问题。
   * 如果操作仍在链表中，会触发断言失败。
   */
  ~AsyncOperation()
  {
    ceph_assert(!m_xlist_item.is_on_list());
  }

  /**
   * @brief 检查操作是否已启动
   * @return 如果操作已在链表中（已启动）返回true，否则返回false
   */
  inline bool started() const {
    return m_xlist_item.is_on_list();
  }

  /**
   * @brief 启动异步操作
   * @param image_ctx 关联的图像上下文
   *
   * 将操作与指定的ImageCtx关联，并将其加入到图像的异步操作链表中。
   * 操作启动后即可开始执行异步I/O操作。
   */
  void start_op(ImageCtx &image_ctx);

  /**
   * @brief 完成异步操作
   *
   * 标记操作完成，从链表中移除，并处理所有待完成的刷新操作。
   * 如果有排队的刷新操作，会将它们传递给下一个操作或立即执行。
   */
  void finish_op();

  /**
   * @brief 添加刷新操作
   * @param on_finish 刷新完成回调上下文
   *
   * 将刷新操作加入队列。如果当前操作不是最新的操作，会将刷新操作
   * 传递给更新的操作；否则将其加入自己的刷新队列中。
   */
  void flush(Context *on_finish);

private:
  /// @brief 关联的图像上下文指针
  ImageCtx *m_image_ctx;

  /// @brief 侵入式链表项，用于在ImageCtx的异步操作链表中维护位置
  xlist<AsyncOperation *>::item m_xlist_item;

  /// @brief 待完成的刷新操作上下文列表
  /// 当操作完成时，这些刷新操作将被执行或传递给下一个操作
  std::list<Context *> m_flush_contexts;

};

/// @brief 结束I/O子系统命名空间
} // namespace io
/// @brief 结束librbd主命名空间
} // namespace librbd

/// @brief 头文件保护宏结束，防止重复包含
#endif // LIBRBD_IO_ASYNC_OPERATION_H
