// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_IO_IMAGE_DISPATCHER_H
#define CEPH_LIBRBD_IO_IMAGE_DISPATCHER_H

#include "include/int_types.h"
#include "common/ceph_mutex.h"
#include "librbd/io/Dispatcher.h"
#include "librbd/io/ImageDispatchInterface.h"
#include "librbd/io/ImageDispatchSpec.h"
#include "librbd/io/ImageDispatcherInterface.h"
#include "librbd/io/Types.h"
#include <atomic>
#include <map>

struct Context;

namespace librbd {

struct ImageCtx;

namespace io {

template <typename> struct QosImageDispatch;
template <typename> struct WriteBlockImageDispatch;

/**
 * @brief 图像调度器类
 *
 * ImageDispatcher是librbd库的核心组件，实现了分层调度架构，用于处理图像级别的IO请求。
 * 它采用插件化的设计模式，支持多个调度层的动态注册和协作处理。
 *
 * 调度层体系结构（按处理顺序）：
 * 1. IMAGE_DISPATCH_LAYER_QUEUE - 请求排队和调度管理
 * 2. IMAGE_DISPATCH_LAYER_QOS - 质量服务限流控制
 * 3. IMAGE_DISPATCH_LAYER_EXCLUSIVE_LOCK - 独占锁协调
 * 4. IMAGE_DISPATCH_LAYER_REFRESH - 元数据刷新和同步
 * 5. IMAGE_DISPATCH_LAYER_MIGRATION - 镜像迁移处理
 * 6. IMAGE_DISPATCH_LAYER_JOURNAL - 日志记录和持久化
 * 7. IMAGE_DISPATCH_LAYER_WRITE_BLOCK - 写操作阻塞管理
 * 8. IMAGE_DISPATCH_LAYER_WRITEBACK_CACHE - 写回缓存处理
 * 9. IMAGE_DISPATCH_LAYER_CORE - 核心IO处理（最终调用librados）
 *
 * 设计特点：
 * - 支持同步和异步IO操作模式
 * - 提供细粒度的流量控制和QOS管理
 * - 支持写操作的串行化和一致性保证
 * - 可扩展的插件化架构，支持新调度层的动态添加
 * - 完善的错误处理和恢复机制
 */
template <typename ImageCtxT = ImageCtx>
class ImageDispatcher : public Dispatcher<ImageCtxT, ImageDispatcherInterface> {
public:
  /**
   * @brief 构造函数，初始化图像调度器并注册所有调度层
   * @param image_ctx 图像上下文指针
   */
  ImageDispatcher(ImageCtxT* image_ctx);

  /**
   * @brief 失效缓存
   * @param on_finish 完成回调函数
   *
   * 通知所有调度层失效其缓存数据，确保数据一致性。
   * 主要用于镜像元数据变更后刷新缓存。
   */
  void invalidate_cache(Context* on_finish) override;

  /**
   * @brief 关闭调度器
   * @param on_finish 完成回调函数
   *
   * 优雅地关闭所有调度层，等待所有IO操作完成。
   * 按照调度层注册的相反顺序进行关闭。
   */
  void shut_down(Context* on_finish) override;

  /**
   * @brief 应用QOS调度时钟最小值
   * @param tick 时钟刻度值
   *
   * 更新QOS调度器的时钟基准，用于精确的流量控制。
   */
  void apply_qos_schedule_tick_min(uint64_t tick) override;

  /**
   * @brief 应用QOS限制参数
   * @param flag QOS标志位
   * @param limit 限制值
   * @param burst 突发限制值
   * @param burst_seconds 突发时间窗口（秒）
   *
   * 动态调整QOS参数，支持运行时的流量控制策略变更。
   */
  void apply_qos_limit(uint64_t flag, uint64_t limit, uint64_t burst,
                       uint64_t burst_seconds) override;

  /**
   * @brief 应用QOS排除操作
   * @param exclude_ops 要排除的操作标志
   *
   * 配置哪些操作不受QOS限制影响，通常用于内部管理操作。
   */
  void apply_qos_exclude_ops(uint64_t exclude_ops) override;

  /**
   * @brief 检查写操作是否被阻塞
   * @return true表示写操作被阻塞，false表示允许写操作
   *
   * 查询当前写操作的阻塞状态，用于上层决策。
   */
  bool writes_blocked() const override;

  /**
   * @brief 阻塞写操作
   * @return 0表示成功，其他值表示错误码
   *
   * 立即阻塞所有写操作，常用于快照创建等需要一致性保证的操作。
   */
  int block_writes() override;

  /**
   * @brief 阻塞写操作（异步版本）
   * @param on_blocked 阻塞完成回调函数
   *
   * 异步阻塞写操作，当所有待处理的写操作完成后调用回调。
   */
  void block_writes(Context *on_blocked) override;

  /**
   * @brief 取消写操作阻塞
   *
   * 恢复写操作的正常执行，允许新的写请求进入处理流程。
   */
  void unblock_writes() override;

  /**
   * @brief 等待写操作解除阻塞
   * @param on_unblocked 解除阻塞完成回调函数
   *
   * 等待当前被阻塞的写操作全部完成，然后调用回调函数。
   */
  void wait_on_writes_unblocked(Context *on_unblocked) override;

protected:
  bool send_dispatch(
    ImageDispatchInterface* image_dispatch,
    ImageDispatchSpec* image_dispatch_spec) override;

private:
  /**
   * @brief IO请求发送访问器结构体
   *
   * 负责将不同类型的IO请求（读、写、废弃、刷新等）
   * 分发到对应的调度层处理方法。
   */
  struct SendVisitor;

  /**
   * @brief 预处理访问器结构体
   *
   * 在正式处理IO请求之前执行预处理逻辑，
   * 如参数验证、边界检查、请求裁剪等。
   */
  struct PreprocessVisitor;

  // 继承缓存失效处理器
  using typename Dispatcher<ImageCtxT, ImageDispatcherInterface>::C_InvalidateCache;

  /**
   * @brief 下一个事务ID生成器
   *
   * 为每个IO请求分配唯一的递增事务ID，
   * 用于跟踪、调试和错误诊断。
   */
  std::atomic<uint64_t> m_next_tid{0};

  /**
   * @brief QOS图像调度器指针
   *
   * 负责质量服务控制，包括IOPS和带宽限流。
   * 支持动态的参数调整和流量整形。
   */
  QosImageDispatch<ImageCtxT>* m_qos_image_dispatch = nullptr;

  /**
   * @brief 写阻塞图像调度器指针
   *
   * 负责写操作的协调和阻塞管理，
   * 防止写操作冲突和维护数据一致性。
   */
  WriteBlockImageDispatch<ImageCtxT>* m_write_block_dispatch = nullptr;

  /**
   * @brief 预处理IO请求
   * @param image_dispatch_spec IO请求规格
   * @return true表示预处理完成且无需继续，false表示继续正常处理
   */
  bool preprocess(ImageDispatchSpec* image_dispatch_spec);

};

} // namespace io
} // namespace librbd

extern template class librbd::io::ImageDispatcher<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_IO_IMAGE_DISPATCHER_H
