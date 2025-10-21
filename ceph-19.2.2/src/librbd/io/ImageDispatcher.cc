// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/io/ImageDispatcher.h"
#include "include/Context.h"
#include "common/AsyncOpTracker.h"
#include "common/dout.h"
#include "librbd/ImageCtx.h"
#include "librbd/io/ImageDispatch.h"
#include "librbd/io/ImageDispatchInterface.h"
#include "librbd/io/ImageDispatchSpec.h"
#include "librbd/io/QueueImageDispatch.h"
#include "librbd/io/QosImageDispatch.h"
#include "librbd/io/RefreshImageDispatch.h"
#include "librbd/io/Utils.h"
#include "librbd/io/WriteBlockImageDispatch.h"
#include <boost/variant.hpp>

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::io::ImageDispatcher: " << this \
                           << " " << __func__ << ": "

namespace librbd {
namespace io {

/**
 * @brief IO请求发送访问器
 *
 * SendVisitor是一个boost::static_visitor的实现类，用于处理不同类型的IO请求。
 * 它根据请求的类型（读、写、废弃、刷新等）调用对应的调度层处理方法。
 *
 * 设计原理：
 * - 使用变体类型（variant）模式匹配不同请求类型
 * - 每个操作符重载对应一种具体的IO请求处理逻辑
 * - 返回值表示请求是否被当前调度层完全处理
 * - 支持请求的继续传递到下一调度层
 */
template <typename I>
struct ImageDispatcher<I>::SendVisitor : public boost::static_visitor<bool> {
  ImageDispatchInterface* image_dispatch;
  ImageDispatchSpec* image_dispatch_spec;

  /**
   * @brief 构造函数
   * @param image_dispatch 目标调度层接口
   * @param image_dispatch_spec IO请求规格
   */
  SendVisitor(ImageDispatchInterface* image_dispatch,
              ImageDispatchSpec* image_dispatch_spec)
    : image_dispatch(image_dispatch),
      image_dispatch_spec(image_dispatch_spec) {
  }

  /**
   * @brief 处理读请求
   * @param read 读请求规格
   * @return true表示请求被处理，false表示继续传递到下一层
   *
   * 将图像级读请求传递给当前调度层处理，包括：
   * - 图像范围到对象范围的转换
   * - 缓存策略的应用
   * - 权限和锁的检查
   */
  bool operator()(ImageDispatchSpec::Read& read) const {
    return image_dispatch->read(
      image_dispatch_spec->aio_comp,
      std::move(image_dispatch_spec->image_extents),
      std::move(read.read_result), image_dispatch_spec->io_context,
      image_dispatch_spec->op_flags, read.read_flags,
      image_dispatch_spec->parent_trace, image_dispatch_spec->tid,
      &image_dispatch_spec->image_dispatch_flags,
      &image_dispatch_spec->dispatch_result,
      &image_dispatch_spec->aio_comp->image_dispatcher_ctx,
      &image_dispatch_spec->dispatcher_ctx);
  }

  /**
   * @brief 处理废弃请求
   * @param discard 废弃请求规格
   * @return true表示请求被处理，false表示继续传递到下一层
   *
   * 处理TRIM/DISCARD操作，用于释放不再需要的存储空间。
   * 支持颗粒度对齐和批量处理优化。
   */
  bool operator()(ImageDispatchSpec::Discard& discard) const {
    return image_dispatch->discard(
      image_dispatch_spec->aio_comp,
      std::move(image_dispatch_spec->image_extents),
      discard.discard_granularity_bytes, image_dispatch_spec->parent_trace,
      image_dispatch_spec->tid, &image_dispatch_spec->image_dispatch_flags,
      &image_dispatch_spec->dispatch_result,
      &image_dispatch_spec->aio_comp->image_dispatcher_ctx,
      &image_dispatch_spec->dispatcher_ctx);
  }

  /**
   * @brief 处理写请求
   * @param write 写请求规格
   * @return true表示请求被处理，false表示继续传递到下一层
   *
   * 处理写操作的核心逻辑，包括：
   * - 写前日志记录（journaling）
   * - 独占锁的获取和协调
   * - 写时复制（copy-on-write）处理
   * - 数据一致性和原子性保证
   */
  bool operator()(ImageDispatchSpec::Write& write) const {
    return image_dispatch->write(
      image_dispatch_spec->aio_comp,
      std::move(image_dispatch_spec->image_extents), std::move(write.bl),
      image_dispatch_spec->op_flags, image_dispatch_spec->parent_trace,
      image_dispatch_spec->tid, &image_dispatch_spec->image_dispatch_flags,
      &image_dispatch_spec->dispatch_result,
      &image_dispatch_spec->aio_comp->image_dispatcher_ctx,
      &image_dispatch_spec->dispatcher_ctx);
  }

  bool operator()(ImageDispatchSpec::WriteSame& write_same) const {
    return image_dispatch->write_same(
      image_dispatch_spec->aio_comp,
      std::move(image_dispatch_spec->image_extents), std::move(write_same.bl),
      image_dispatch_spec->op_flags, image_dispatch_spec->parent_trace,
      image_dispatch_spec->tid, &image_dispatch_spec->image_dispatch_flags,
      &image_dispatch_spec->dispatch_result,
      &image_dispatch_spec->aio_comp->image_dispatcher_ctx,
      &image_dispatch_spec->dispatcher_ctx);
  }

  bool operator()(
      ImageDispatchSpec::CompareAndWrite& compare_and_write) const {
    return image_dispatch->compare_and_write(
      image_dispatch_spec->aio_comp,
      std::move(image_dispatch_spec->image_extents),
      std::move(compare_and_write.cmp_bl), std::move(compare_and_write.bl),
      compare_and_write.mismatch_offset,
      image_dispatch_spec->op_flags, image_dispatch_spec->parent_trace,
      image_dispatch_spec->tid, &image_dispatch_spec->image_dispatch_flags,
      &image_dispatch_spec->dispatch_result,
      &image_dispatch_spec->aio_comp->image_dispatcher_ctx,
      &image_dispatch_spec->dispatcher_ctx);
  }

  /**
   * @brief 处理刷新请求
   * @param flush 刷新请求规格
   * @return true表示请求被处理，false表示继续传递到下一层
   *
   * 处理缓存刷新和数据持久化操作，确保：
   * - 所有待处理的写操作完成
   * - 缓存数据被刷新到持久存储
   * - 日志记录被持久化到稳定存储
   * - 支持不同来源的刷新操作（用户、内部、关闭等）
   */
  bool operator()(ImageDispatchSpec::Flush& flush) const {
    return image_dispatch->flush(
      image_dispatch_spec->aio_comp, flush.flush_source,
      image_dispatch_spec->parent_trace, image_dispatch_spec->tid,
      &image_dispatch_spec->image_dispatch_flags,
      &image_dispatch_spec->dispatch_result,
      &image_dispatch_spec->aio_comp->image_dispatcher_ctx,
      &image_dispatch_spec->dispatcher_ctx);
  }

  bool operator()(ImageDispatchSpec::ListSnaps& list_snaps) const {
    return image_dispatch->list_snaps(
      image_dispatch_spec->aio_comp,
      std::move(image_dispatch_spec->image_extents),
      std::move(list_snaps.snap_ids), list_snaps.list_snaps_flags,
      list_snaps.snapshot_delta, image_dispatch_spec->parent_trace,
      image_dispatch_spec->tid, &image_dispatch_spec->image_dispatch_flags,
      &image_dispatch_spec->dispatch_result,
      &image_dispatch_spec->aio_comp->image_dispatcher_ctx,
      &image_dispatch_spec->dispatcher_ctx);
  }
};

template <typename I>
struct ImageDispatcher<I>::PreprocessVisitor
  : public boost::static_visitor<bool> {
  ImageDispatcher<I>* image_dispatcher;
  ImageDispatchSpec* image_dispatch_spec;

  PreprocessVisitor(ImageDispatcher<I>* image_dispatcher,
                    ImageDispatchSpec* image_dispatch_spec)
    : image_dispatcher(image_dispatcher),
      image_dispatch_spec(image_dispatch_spec) {
  }

  bool clip_request() const {
    auto area = (image_dispatch_spec->image_dispatch_flags &
        IMAGE_DISPATCH_FLAG_CRYPTO_HEADER ? ImageArea::CRYPTO_HEADER :
                                            ImageArea::DATA);
    int r = util::clip_request(image_dispatcher->m_image_ctx,
                               &image_dispatch_spec->image_extents, area);
    if (r < 0) {
      image_dispatch_spec->fail(r);
      return true;
    }
    return false;
  }

  bool operator()(ImageDispatchSpec::Read& read) const {
    if ((read.read_flags & READ_FLAG_DISABLE_CLIPPING) != 0) {
      return false;
    }
    return clip_request();
  }

  bool operator()(ImageDispatchSpec::Flush&) const {
    return clip_request();
  }

  bool operator()(ImageDispatchSpec::ListSnaps&) const {
    return false;
  }

  template <typename T>
  bool operator()(T&) const {
    if (clip_request()) {
      return true;
    }

    std::shared_lock image_locker{image_dispatcher->m_image_ctx->image_lock};
    if (image_dispatcher->m_image_ctx->snap_id != CEPH_NOSNAP ||
        image_dispatcher->m_image_ctx->read_only) {
      image_dispatch_spec->fail(-EROFS);
      return true;
    }
    return false;
  }
};

template <typename I>
ImageDispatcher<I>::ImageDispatcher(I* image_ctx)
  : Dispatcher<I, ImageDispatcherInterface>(image_ctx) {

  /**
   * @brief 配置核心图像调度处理器
   *
   * ImageDispatch是核心的图像级IO调度器，负责：
   * - 将图像范围的IO请求转换为对象级请求
   * - 处理图像到对象的映射和范围转换
   * - 协调底层对象调度器的调用
   * - 最终触发实际的librados API调用
   */
  auto image_dispatch = new ImageDispatch(image_ctx);
  this->register_dispatch(image_dispatch);

  /**
   * @brief 配置队列图像调度处理器
   *
   * QueueImageDispatch负责：
   * - IO请求的排队和调度管理
   * - 防止过多的并发请求导致系统过载
   * - 实现公平的请求调度策略
   * - 支持请求优先级和超时处理
   */
  auto queue_image_dispatch = new QueueImageDispatch(image_ctx);
  this->register_dispatch(queue_image_dispatch);

  /**
   * @brief 配置QOS图像调度处理器
   *
   * QosImageDispatch负责：
   * - 质量服务(Quality of Service)控制
   * - IOPS和带宽限流管理
   * - 读写操作的流量整形
   * - 支持动态的QOS参数调整
   */
  m_qos_image_dispatch = new QosImageDispatch<I>(image_ctx);
  this->register_dispatch(m_qos_image_dispatch);

  /**
   * @brief 配置刷新图像调度处理器
   *
   * RefreshImageDispatch负责：
   * - 镜像元数据的刷新和同步
   * - 快照信息的更新和维护
   * - 父镜像关系的处理和更新
   * - 确保元数据的一致性和时效性
   */
  auto refresh_image_dispatch = new RefreshImageDispatch(image_ctx);
  this->register_dispatch(refresh_image_dispatch);

  /**
   * @brief 配置写阻塞图像调度处理器
   *
   * WriteBlockImageDispatch负责：
   * - 写操作的阻塞和协调管理
   * - 防止写操作之间的冲突和竞态条件
   * - 支持写操作的排队和串行化处理
   * - 维护写操作的顺序性和一致性
   */
  m_write_block_dispatch = new WriteBlockImageDispatch<I>(image_ctx);
  this->register_dispatch(m_write_block_dispatch);
}

template <typename I>
void ImageDispatcher<I>::invalidate_cache(Context* on_finish) {
  auto image_ctx = this->m_image_ctx;
  auto cct = image_ctx->cct;
  ldout(cct, 5) << dendl;

  auto ctx = new C_InvalidateCache(
      this, IMAGE_DISPATCH_LAYER_NONE, on_finish);
  ctx->complete(0);
}

template <typename I>
void ImageDispatcher<I>::shut_down(Context* on_finish) {
  // TODO ensure all IOs are executed via a dispatcher
  // ensure read-ahead / copy-on-read ops are finished since they are
  // currently outside dispatcher tracking
  auto async_op = new AsyncOperation();

  on_finish = new LambdaContext([async_op, on_finish](int r) {
      async_op->finish_op();
      delete async_op;
      on_finish->complete(0);
    });
  on_finish = new LambdaContext([this, on_finish](int r) {
      Dispatcher<I, ImageDispatcherInterface>::shut_down(on_finish);
    });
  async_op->start_op(*this->m_image_ctx);
  async_op->flush(on_finish);
}

template <typename I>
void ImageDispatcher<I>::apply_qos_schedule_tick_min(uint64_t tick) {
  m_qos_image_dispatch->apply_qos_schedule_tick_min(tick);
}

template <typename I>
void ImageDispatcher<I>::apply_qos_limit(uint64_t flag, uint64_t limit,
                                         uint64_t burst, uint64_t burst_seconds) {
  m_qos_image_dispatch->apply_qos_limit(flag, limit, burst, burst_seconds);
}

template <typename I>
void ImageDispatcher<I>::apply_qos_exclude_ops(uint64_t exclude_ops) {
  m_qos_image_dispatch->apply_qos_exclude_ops(exclude_ops);
}

template <typename I>
bool ImageDispatcher<I>::writes_blocked() const {
  return m_write_block_dispatch->writes_blocked();
}

template <typename I>
int ImageDispatcher<I>::block_writes() {
  return m_write_block_dispatch->block_writes();
}

template <typename I>
void ImageDispatcher<I>::block_writes(Context *on_blocked) {
  m_write_block_dispatch->block_writes(on_blocked);
}

template <typename I>
void ImageDispatcher<I>::unblock_writes() {
  m_write_block_dispatch->unblock_writes();
}

template <typename I>
void ImageDispatcher<I>::wait_on_writes_unblocked(Context *on_unblocked) {
  m_write_block_dispatch->wait_on_writes_unblocked(on_unblocked);
}

/**
 * @brief 发送请求到指定调度层进行处理
 * @param image_dispatch 目标调度层接口
 * @param image_dispatch_spec IO请求规格
 * @return true表示请求被完全处理，false表示继续传递到下一层
 *
 * 这是ImageDispatcher的核心调度方法，负责：
 * 1. 为新请求分配唯一的事务ID
 * 2. 执行预处理逻辑（如果需要）
 * 3. 通过SendVisitor将请求传递给具体的调度层处理
 * 4. 根据调度层的处理结果决定是否继续传递请求
 */
template <typename I>
bool ImageDispatcher<I>::send_dispatch(
    ImageDispatchInterface* image_dispatch,
    ImageDispatchSpec* image_dispatch_spec) {

  // 为新请求分配唯一的事务ID，用于跟踪和调试
  if (image_dispatch_spec->tid == 0) {
    image_dispatch_spec->tid = ++m_next_tid;

    // 执行预处理逻辑（如请求裁剪、参数验证等）
    bool finished = preprocess(image_dispatch_spec);
    if (finished) {
      return true;
    }
  }

  // 通过变体类型访问器将请求传递给调度层处理
  return boost::apply_visitor(
    SendVisitor{image_dispatch, image_dispatch_spec},
    image_dispatch_spec->request);
}

/**
 * @brief 预处理IO请求
 * @param image_dispatch_spec IO请求规格
 * @return true表示预处理完成且无需继续，false表示继续正常处理
 *
 * 预处理阶段用于：
 * - 请求参数验证和规范化
 * - 早期错误检测和处理
 * - 请求裁剪和优化（如超出边界的情况）
 * - 特殊情况的快速路径处理
 */
template <typename I>
bool ImageDispatcher<I>::preprocess(
    ImageDispatchSpec* image_dispatch_spec) {
  return boost::apply_visitor(
    PreprocessVisitor{this, image_dispatch_spec},
    image_dispatch_spec->request);
}

} // namespace io
} // namespace librbd

template class librbd::io::ImageDispatcher<librbd::ImageCtx>;
