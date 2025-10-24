/**
 * @file librados.hpp
 * @brief RADOS (Reliable Autonomic Distributed Object Store) 库的 C++ 接口头文件
 *
 * 本文件定义了 RADOS 库的 C++ 接口，提供对 Ceph 集群中分布式对象存储的完整访问。
 * RADOS 是 Ceph 存储系统的核心组件，librados 提供了访问 RADOS 的客户端库接口。
 *
 * 主要功能包括：
 * - 对象操作：创建、读取、写入、删除对象
 * - 异步 I/O：支持高性能异步操作
 * - 对象迭代：遍历池中的对象
 * - 监视器：监听对象变更通知
 * - 集群操作：池管理、集群统计等
 * - 缓存管理：智能缓存策略
 * - 故障恢复：自动故障检测和恢复
 *
 * C++ 接口在 C 接口基础上提供了：
 * - 类型安全和异常处理
 * - 标准库容器集成
 * - 更好的资源管理（RAII）
 * - 面向对象的设计模式
 *
 * 使用示例：
 * @code
 * #include <rados/librados.hpp>
 * librados::Rados cluster;
 * cluster.init("admin");
 * cluster.connect();
 * librados::IoCtx io_ctx;
 * cluster.ioctx_create("mypool", io_ctx);
 * io_ctx.write("myobject", "hello world", 11, 0);
 * @endcode
 *
 * @author Ceph 项目开发团队
 * @version 14.2.0 (内联命名空间版本)
 * @see librados.h 对应的 C 接口头文件
 * @see rados_types.hpp 类型定义头文件
 */

#ifndef __LIBRADOS_HPP
#define __LIBRADOS_HPP

#include <string>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <utility>
#include "buffer.h"

#include "librados.h"
#include "librados_fwd.hpp"
#include "rados_types.hpp"

namespace libradosstriper
{
  class RadosStriper;
}

namespace neorados { class RADOS; }

/**
 * @namespace librados
 * @brief librados 命名空间，包含 RADOS 库的所有 C++ 接口
 *
 * librados 命名空间提供了完整的 Ceph RADOS (Reliable Autonomic Distributed Object Store)
 * C++ 接口。所有类、函数和类型都定义在此命名空间中，为 Ceph 集群的对象存储操作
 * 提供类型安全、面向对象的访问方式。
 *
 * 主要组件包括：
 * - Rados：集群连接和管理的核心类
 * - IoCtx：I/O 上下文，用于特定存储池的操作
 * - ObjectOperation：复合对象操作，支持原子性批量操作
 * - AioCompletion：异步 I/O 完成回调管理
 * - 各种数据类型：用于表示对象、快照、锁等概念
 *
 * 使用该命名空间的典型流程：
 * 1. 创建 Rados 实例并连接到集群
 * 2. 创建 IoCtx 指定目标存储池
 * 3. 执行对象读写、快照、锁等操作
 * 4. 清理资源
 */
namespace librados {

/// @brief 使用 ceph 的 bufferlist 类型
using ceph::bufferlist;

/// @brief 异步 I/O 完成实现（内部使用）
struct AioCompletionImpl;
/// @brief I/O 上下文实现（内部使用）
struct IoCtxImpl;
/// @brief 对象列表实现（内部使用）
struct ListObjectImpl;
/// @brief N 对象迭代器实现（内部使用）
class NObjectIteratorImpl;
/// @brief 对象列表上下文（内部使用）
struct ObjListCtx;
/// @brief 对象操作实现（内部使用）
class ObjectOperationImpl;
/// @brief 放置组实现（内部使用）
struct PlacementGroupImpl;
/// @brief 池异步完成实现（内部使用）
struct PoolAsyncCompletionImpl;

/// @brief 集群统计信息类型
/// @details 表示整个 Ceph 集群的统计信息，包括存储容量、使用情况等
typedef struct rados_cluster_stat_t cluster_stat_t;

/// @brief 池统计信息类型
/// @details 表示特定存储池的统计信息，包括对象数量、大小等
typedef struct rados_pool_stat_t pool_stat_t;

/// @brief 对象列表上下文类型（内部使用）
typedef void *list_ctx_t;

/// @brief 认证用户 ID 类型
/// @details 用于标识在 Ceph 集群中进行身份验证的用户
typedef uint64_t auid_t;

/// @brief 配置类型（内部使用）
typedef void *config_t;

/**
 * @brief 锁持有者信息结构
 * @details 表示当前持有对象锁的客户端信息，用于对象锁管理
 */
typedef struct {
  std::string client;   ///< 客户端标识符，唯一标识持有锁的客户端
  std::string cookie;   ///< 锁 cookie，用于验证锁操作的合法性
  std::string address;  ///< 客户端地址，网络位置信息
} locker_t;

/// @brief 池统计映射类型
/// @details 将池名称映射到池统计信息的字典类型
typedef std::map<std::string, pool_stat_t> stats_map;

/// @brief 完成回调类型（内部使用）
typedef void *completion_t;

/// @brief 回调函数类型
/// @details 异步操作完成时的回调函数签名
/// @param cb 完成回调句柄
/// @param arg 用户提供的回调参数
typedef void (*callback_t)(completion_t cb, void *arg);

/**
 * @namespace v14_2_0
 * @brief v14.2.0 版本的内联命名空间
 *
 * 内联命名空间用于版本管理，确保 API 兼容性。
 * 所有 v14.2.0 版本的声明都包含在此命名空间中。
 */
inline namespace v14_2_0 {

  /// @brief 前向声明：I/O 上下文类
  class IoCtx;
  /// @brief 前向声明：RADOS 客户端类
  class RadosClient;

  /**
   * @brief RADOS 对象列表项类
   *
   * 表示 RADOS 池中的一个对象，包含对象的命名空间、对象 ID 和定位器信息。
   * 主要用于对象迭代和列表操作。
   */
  class CEPH_RADOS_API ListObject
  {
  public:
    /// @brief 获取对象的命名空间
    const std::string& get_nspace() const;
    /// @brief 获取对象 ID
    const std::string& get_oid() const;
    /// @brief 获取对象定位器
    const std::string& get_locator() const;

    /// @brief 默认构造函数
    ListObject();
    /// @brief 析构函数
    ~ListObject();
    /// @brief 拷贝构造函数
    ListObject( const ListObject&);
    /// @brief 赋值运算符
    ListObject& operator=(const ListObject& rhs);
  private:
    /// @brief 私有构造函数（仅供内部使用）
    ListObject(ListObjectImpl *impl);

    /// @brief 友元类：N 对象迭代器实现
    friend class librados::NObjectIteratorImpl;
    /// @brief 友元函数：输出流运算符
    friend std::ostream& operator<<(std::ostream& out, const ListObject& lop);

    /// @brief 实现指针（内部使用）
    ListObjectImpl *impl;
  };
  CEPH_RADOS_API std::ostream& operator<<(std::ostream& out, const librados::ListObject& lop);

  class CEPH_RADOS_API NObjectIterator;

  /**
   * @brief RADOS 对象游标类
   *
   * 用于在对象列表中定位和导航的对象游标。
   * 支持基于字符串和二进制游标的序列化和反序列化。
   */
  class CEPH_RADOS_API ObjectCursor
  {
    public:
    /// @brief 默认构造函数
    ObjectCursor();
    /// @brief 拷贝构造函数
    ObjectCursor(const ObjectCursor &rhs);
    /// @brief 从 C API 游标构造
    explicit ObjectCursor(rados_object_list_cursor c);
    /// @brief 析构函数
    ~ObjectCursor();
    /// @brief 赋值运算符
    ObjectCursor& operator=(const ObjectCursor& rhs);
    /// @brief 小于比较运算符
    bool operator<(const ObjectCursor &rhs) const;
    /// @brief 等于比较运算符
    bool operator==(const ObjectCursor &rhs) const;
    /// @brief 设置 C API 游标
    void set(rados_object_list_cursor c);

    /// @brief 友元类：IoCtx
    friend class IoCtx;
    /// @brief 友元类：N 对象迭代器实现
    friend class librados::NObjectIteratorImpl;
    /// @brief 友元函数：输出流运算符
    friend std::ostream& operator<<(std::ostream& os, const librados::ObjectCursor& oc);

    /// @brief 转换为字符串表示
    std::string to_str() const;
    /// @brief 从字符串表示构造
    bool from_str(const std::string& s);

    protected:
    /// @brief C API 游标
    rados_object_list_cursor c_cursor;
  };
  CEPH_RADOS_API std::ostream& operator<<(std::ostream& os, const librados::ObjectCursor& oc);

  class CEPH_RADOS_API NObjectIterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = ListObject;
    using difference_type = std::ptrdiff_t;
    using pointer = ListObject*;
    using reference = ListObject&;
    static const NObjectIterator __EndObjectIterator;
    NObjectIterator(): impl(NULL) {}
    ~NObjectIterator();
    NObjectIterator(const NObjectIterator &rhs);
    NObjectIterator& operator=(const NObjectIterator& rhs);

    bool operator==(const NObjectIterator& rhs) const;
    bool operator!=(const NObjectIterator& rhs) const;
    const ListObject& operator*() const;
    const ListObject* operator->() const;
    NObjectIterator &operator++(); //< Preincrement; errors are thrown as exceptions
    NObjectIterator operator++(int); //< Postincrement; errors are thrown as exceptions
    friend class IoCtx;
    friend class librados::NObjectIteratorImpl;

    /// get current hash position of the iterator, rounded to the current pg
    uint32_t get_pg_hash_position() const;

    /// move the iterator to a given hash position. this may (will!) be rounded
    /// to the nearest pg. errors are thrown as exceptions
    uint32_t seek(uint32_t pos);

    /// move the iterator to a given cursor position. errors are thrown as exceptions
    uint32_t seek(const ObjectCursor& cursor);

    /// get current cursor position
    ObjectCursor get_cursor();

    /**
     * Configure PGLS filter to be applied OSD-side (requires caller
     * to know/understand the format expected by the OSD)
     */
    void set_filter(const bufferlist &bl);

  private:
    NObjectIterator(ObjListCtx *ctx_);
    void get_next();
    NObjectIteratorImpl *impl;
  };

  class CEPH_RADOS_API ObjectItem
  {
    public:
    std::string oid;
    std::string nspace;
    std::string locator;
  };

  /**
   * @class WatchCtx
   * @brief 对象监视上下文类（已废弃）
   * @deprecated 请使用 WatchCtx2，不要使用此类
   */
  class CEPH_RADOS_API WatchCtx {
  public:
    /// @brief 虚析构函数
    virtual ~WatchCtx();

    /**
     * @brief 通知回调函数（已废弃）
     * @param opcode 操作码
     * @param ver 版本号
     * @param bl 通知负载
     */
    virtual void notify(uint8_t opcode, uint64_t ver, bufferlist& bl) = 0;
  };

  /**
   * @class WatchCtx2
   * @brief 对象监视上下文类（推荐使用）
   *
   * WatchCtx2 是用于对象监视的回调接口类。当监视的对象发生变化或出现错误时，
   * 相应的回调函数将被调用。
   *
   * 使用示例：
   * @code
   * class MyWatchCtx : public WatchCtx2 {
   *     void handle_notify(uint64_t notify_id, uint64_t cookie,
   *                        uint64_t notifier_id, bufferlist& bl) override {
   *         // 处理通知事件
   *     }
   *     void handle_error(uint64_t cookie, int err) override {
   *         // 处理监视错误
   *     }
   * };
   * @endcode
   */
  class CEPH_RADOS_API WatchCtx2 {
  public:
    /// @brief 虚析构函数
    virtual ~WatchCtx2();

    /**
     * @brief 通知事件处理回调
     * @details 当接收到来自其他客户端的通知时被调用
     * @param notify_id 此通知事件的唯一ID
     * @param cookie 接收通知的监视器句柄
     * @param notifier_id 发送通知的客户端的唯一ID
     * @param bl 来自通知者的不透明通知负载
     */
    virtual void handle_notify(uint64_t notify_id,
			       uint64_t cookie,
			       uint64_t notifier_id,
			       bufferlist& bl) = 0;

    /**
     * @brief 监视错误处理回调
     * @details 当监视出现错误时被调用
     *
     * 可能出现的错误：
     * - -ENOTCONN: 监视连接已断开
     * - -ETIMEDOUT: 监视仍然有效，但可能错过了一个通知事件
     *
     * @param cookie 有问题的监视器句柄
     * @param err 错误码
     */
    virtual void handle_error(uint64_t cookie, int err) = 0;
  };

  /**
   * @struct AioCompletion
   * @brief 异步I/O完成回调结构体
   *
   * AioCompletion 用于管理异步I/O操作的完成状态和回调。
   * 它提供了等待操作完成、检查状态、获取返回值等多种功能。
   *
   * 主要功能：
   * - 设置完成和安全回调函数
   * - 等待操作完成
   * - 检查操作状态
   * - 获取操作返回值和版本信息
   * - 资源释放
   */
  struct CEPH_RADOS_API AioCompletion {
    /**
     * @brief 构造函数
     * @param pc_ 内部实现指针
     */
    AioCompletion(AioCompletionImpl *pc_) : pc(pc_) {}

    /// @brief 析构函数
    ~AioCompletion();

    /**
     * @brief 设置完成回调函数
     * @param cb_arg 回调参数
     * @param cb 回调函数指针
     * @return 0表示成功，负数表示错误
     */
    int set_complete_callback(void *cb_arg, callback_t cb);

    /**
     * @brief 设置安全回调函数（已废弃）
     * @param cb_arg 回调参数
     * @param cb 回调函数指针
     * @return 0表示成功，负数表示错误
     * @deprecated 请使用 set_complete_callback
     */
    int set_safe_callback(void *cb_arg, callback_t cb)
      __attribute__ ((deprecated));

    /**
     * @brief 等待操作完成
     * @return 0表示成功，负数表示错误
     */
    int wait_for_complete();

    /**
     * @brief 等待操作安全完成（已废弃）
     * @return 0表示成功，负数表示错误
     * @deprecated 请使用 wait_for_complete
     */
    int wait_for_safe() __attribute__ ((deprecated));

    /**
     * @brief 等待操作完成并执行回调
     * @return 0表示成功，负数表示错误
     */
    int wait_for_complete_and_cb();

    /**
     * @brief 等待操作安全完成并执行回调（已废弃）
     * @return 0表示成功，负数表示错误
     * @deprecated 请使用 wait_for_complete_and_cb
     */
    int wait_for_safe_and_cb() __attribute__ ((deprecated));

    /**
     * @brief 检查操作是否完成
     * @return 如果操作已完成返回true，否则返回false
     */
    bool is_complete();

    /**
     * @brief 检查操作是否安全完成（已废弃）
     * @return 如果操作已安全完成返回true，否则返回false
     * @deprecated 请使用 is_complete
     */
    bool is_safe() __attribute__ ((deprecated));

    /**
     * @brief 检查操作是否完成且回调已执行
     * @return 如果操作完成且回调已执行返回true，否则返回false
     */
    bool is_complete_and_cb();

    /**
     * @brief 检查操作是否安全完成且回调已执行（已废弃）
     * @return 如果操作安全完成且回调已执行返回true，否则返回false
     * @deprecated 请使用 is_complete_and_cb
     */
    bool is_safe_and_cb() __attribute__ ((deprecated));

    /**
     * @brief 获取操作返回值
     * @return 操作的返回值，0表示成功，负数表示错误
     */
    int get_return_value();

    /**
     * @brief 获取对象版本号（已废弃）
     * @return 对象版本号
     * @deprecated 请使用 get_version64
     */
    int get_version() __attribute__ ((deprecated));

    /**
     * @brief 获取对象版本号（64位）
     * @return 对象版本号（64位）
     */
    uint64_t get_version64();

    /// @brief 释放资源
    void release();

    /// @brief 内部实现指针
    AioCompletionImpl *pc;
  };

  /**
   * @struct PoolAsyncCompletion
   * @brief 存储池异步操作完成回调结构体
   *
   * PoolAsyncCompletion 专门用于管理存储池相关的异步操作完成状态，
   * 如创建池、删除池等操作的异步执行。
   */
  struct CEPH_RADOS_API PoolAsyncCompletion {
    /**
     * @brief 构造函数
     * @param pc_ 内部实现指针
     */
    PoolAsyncCompletion(PoolAsyncCompletionImpl *pc_) : pc(pc_) {}

    /// @brief 析构函数
    ~PoolAsyncCompletion();

    /**
     * @brief 设置完成回调函数
     * @param cb_arg 回调参数
     * @param cb 回调函数指针
     * @return 0表示成功，负数表示错误
     */
    int set_callback(void *cb_arg, callback_t cb);

    /**
     * @brief 等待操作完成
     * @return 0表示成功，负数表示错误
     */
    int wait();

    /**
     * @brief 检查操作是否完成
     * @return 如果操作已完成返回true，否则返回false
     */
    bool is_complete();

    /**
     * @brief 获取操作返回值
     * @return 操作的返回值，0表示成功，负数表示错误
     */
    int get_return_value();

    /// @brief 释放资源
    void release();

    /// @brief 内部实现指针
    PoolAsyncCompletionImpl *pc;
  };

  /**
   * @brief 对象操作标志枚举
   * @details 这些是针对每个单独操作的标志，可以在添加到 ObjectOperation 的不同操作之间有所区别
   *
   * 这些标志影响特定操作的行为，例如：
   * - OP_EXCL: 操作失败如果对象已存在
   * - OP_FAILOK: 操作失败时不返回错误
   * - OP_FADVISE_* : 文件访问模式建议，用于优化存储性能
   */
  enum ObjectOperationFlags {
    OP_EXCL =   LIBRADOS_OP_FLAG_EXCL,           ///< 操作失败如果对象已存在
    OP_FAILOK = LIBRADOS_OP_FLAG_FAILOK,         ///< 操作失败时不返回错误，继续处理其他操作
    OP_FADVISE_RANDOM = LIBRADOS_OP_FLAG_FADVISE_RANDOM,     ///< 随机访问模式建议
    OP_FADVISE_SEQUENTIAL = LIBRADOS_OP_FLAG_FADVISE_SEQUENTIAL,  ///< 顺序访问模式建议
    OP_FADVISE_WILLNEED = LIBRADOS_OP_FLAG_FADVISE_WILLNEED,   ///< 数据即将被访问的建议
    OP_FADVISE_DONTNEED = LIBRADOS_OP_FLAG_FADVISE_DONTNEED,   ///< 数据短期内不会被访问的建议
    OP_FADVISE_NOCACHE = LIBRADOS_OP_FLAG_FADVISE_NOCACHE,     ///< 不使用缓存的建议
  };

  class CEPH_RADOS_API ObjectOperationCompletion {
  public:
    virtual ~ObjectOperationCompletion() {}
    virtual void handle_completion(int r, bufferlist& outbl) = 0;
  };

  /**
   * @brief 对象操作全局标志枚举
   * @details 这些标志适用于整个 ObjectOperation 操作，影响复合操作的整体行为
   *
   * 主要标志说明：
   * - OPERATION_BALANCE_READS 和 OPERATION_LOCALIZE_READS：在 octopus 版本前，只应用于
   *   不会更改的数据（如快照）或可以接受最终一致性的场景。从 octopus 版本开始，
   *   这两个标志可以安全地用于一般用途。
   *
   * - OPERATION_ORDER_READS_WRITES：使读操作以与写操作相同的方式排序（例如，等待
   *   降级的对象）。特别地，它将确保写操作后跟读操作的序列得到保持。
   *
   * - OPERATION_IGNORE_CACHE：跳过 OSD 上通常处理层间对象提升的缓存逻辑。这允许
   *   操作处理缓存的（或未缓存的）对象，即使它不是连贯的。
   *
   * - OPERATION_IGNORE_OVERLAY：忽略池覆盖层元数据，直接在目标池上处理操作。
   *   这对于 CACHE_FLUSH 和 CACHE_EVICT 操作很有用。
   *
   * - OPERATION_FULL_TRY 和 OPERATION_FULL_FORCE：即使集群或池被标记为满，也
   *   向集群发送请求。操作要么成功（例如，删除），要么返回 EDQUOT 或 ENOSPC。
   *
   * - OPERATION_RETURNVEC：启用/允许返回值和每个操作的返回代码/缓冲区。
   */
  enum ObjectOperationGlobalFlags {
    OPERATION_NOFLAG             = LIBRADOS_OPERATION_NOFLAG,           ///< 无特殊标志
    OPERATION_BALANCE_READS      = LIBRADOS_OPERATION_BALANCE_READS,     ///< 平衡读取负载到多个 OSD
    OPERATION_LOCALIZE_READS     = LIBRADOS_OPERATION_LOCALIZE_READS,    ///< 将读取本地化到最近的 OSD
    OPERATION_ORDER_READS_WRITES = LIBRADOS_OPERATION_ORDER_READS_WRITES, ///< 按写操作的顺序排序读操作
    OPERATION_IGNORE_CACHE       = LIBRADOS_OPERATION_IGNORE_CACHE,     ///< 忽略缓存逻辑
    OPERATION_SKIPRWLOCKS        = LIBRADOS_OPERATION_SKIPRWLOCKS,      ///< 跳过读写锁
    OPERATION_IGNORE_OVERLAY     = LIBRADOS_OPERATION_IGNORE_OVERLAY,   ///< 忽略覆盖层元数据
    OPERATION_FULL_TRY           = LIBRADOS_OPERATION_FULL_TRY,         ///< 尝试操作即使池已满
    OPERATION_FULL_FORCE         = LIBRADOS_OPERATION_FULL_FORCE,       ///< 强制操作即使池已满（主要用于删除）
    OPERATION_IGNORE_REDIRECT    = LIBRADOS_OPERATION_IGNORE_REDIRECT,  ///< 忽略重定向
    OPERATION_ORDERSNAP          = LIBRADOS_OPERATION_ORDERSNAP,        ///< 按快照顺序排列操作
    OPERATION_RETURNVEC          = LIBRADOS_OPERATION_RETURNVEC,        ///< 启用详细的返回值和错误代码
  };

  /**
   * @brief 分配提示标志枚举
   * @details 用于 alloc_hint 操作的标志，为存储系统提供关于对象预期使用模式的提示
   *
   * 这些标志帮助 Ceph 优化对象在存储后端的分配和放置：
   * - 读写模式标志：SEQUENTIAL vs RANDOM
   * - 生存期标志：SHORTLIVED vs LONGLIVED
   * - 压缩性标志：COMPRESSIBLE vs INCOMPRESSIBLE
   * - 特殊模式标志：APPEND_ONLY, IMMUTABLE
   */
  enum AllocHintFlags {
    ALLOC_HINT_FLAG_SEQUENTIAL_WRITE = 1,     ///< 顺序写模式提示
    ALLOC_HINT_FLAG_RANDOM_WRITE = 2,         ///< 随机写模式提示
    ALLOC_HINT_FLAG_SEQUENTIAL_READ = 4,      ///< 顺序读模式提示
    ALLOC_HINT_FLAG_RANDOM_READ = 8,          ///< 随机读模式提示
    ALLOC_HINT_FLAG_APPEND_ONLY = 16,         ///< 仅追加模式提示（对象只会被追加，不会被覆盖）
    ALLOC_HINT_FLAG_IMMUTABLE = 32,           ///< 不可变模式提示（对象创建后不会被修改）
    ALLOC_HINT_FLAG_SHORTLIVED = 64,          ///< 短期生存对象提示（对象很快会被删除）
    ALLOC_HINT_FLAG_LONGLIVED = 128,          ///< 长期生存对象提示（对象会长时间保留）
    ALLOC_HINT_FLAG_COMPRESSIBLE = 256,       ///< 可压缩对象提示（对象数据可以被有效压缩）
    ALLOC_HINT_FLAG_INCOMPRESSIBLE = 512,     ///< 不可压缩对象提示（对象数据不适合压缩）
  };

  /**
   * @class ObjectOperation
   * @brief 复合对象操作类
   *
   * ObjectOperation 允许将多个对象操作批量组合成单个请求，并原子性地应用这些操作。
   * 这提高了效率并确保了一致性，因为要么所有操作都成功，要么所有操作都失败。
   *
   * 支持的操作类型包括：
   * - 读操作：stat, read, getxattr, omap_get_* 等
   * - 写操作：write, append, setxattr, omap_set 等
   * - 管理操作：assert_version, assert_exists, cmpext 等
   * - 类方法调用：exec
   *
   * 使用示例：
   * @code
   * ObjectOperation op;
   * bufferlist bl("data");
   * op.write(0, bl);
   * op.setxattr("key", bl);
   * int ret = io_ctx.operate("myobject", &op);
   * @endcode
   */
  class CEPH_RADOS_API ObjectOperation
  {
  public:
    /// @brief 默认构造函数
    ObjectOperation();

    /// @brief 虚析构函数
    virtual ~ObjectOperation();

    /// @brief 删除拷贝构造函数，防止意外拷贝
    ObjectOperation(const ObjectOperation&) = delete;

    /// @brief 删除赋值运算符，防止意外拷贝
    ObjectOperation& operator=(const ObjectOperation&) = delete;

    /**
     * @brief 移动构造函数
     * @warning 被移动的 ObjectOperation 对象无效，不能用于任何目的。这是一个硬性约定，违反将导致程序崩溃。
     */
    ObjectOperation(ObjectOperation&&);

    /// @brief 移动赋值运算符
    ObjectOperation& operator =(ObjectOperation&&);

    /// @brief 获取操作数量
    /// @return 当前操作队列中的操作数量
    size_t size();

    /// @brief 设置操作标志（已废弃，请使用 set_op_flags2）
    /// @deprecated 请使用 set_op_flags2(int flags)
    void set_op_flags(ObjectOperationFlags flags) __attribute__((deprecated));

    /// @brief 设置操作标志（推荐使用）
    /// @param flags 操作标志，ObjectOperationFlags 类型的标志
    void set_op_flags2(int flags);

    /**
     * @brief 比较对象范围与缓冲区内容
     * @param off 比较起始偏移量
     * @param cmp_bl 要比较的缓冲区内容
     * @param prval 返回值指针，如果比较失败，返回不匹配的偏移量
     */
    void cmpext(uint64_t off, const bufferlist& cmp_bl, int *prval);

    /**
     * @brief 比较扩展属性值
     * @param name 属性名称
     * @param op 比较操作类型
     * @param val 要比较的缓冲区值
     */
    void cmpxattr(const char *name, uint8_t op, const bufferlist& val);

    /**
     * @brief 比较扩展属性值（数值版本）
     * @param name 属性名称
     * @param op 比较操作类型
     * @param v 要比较的数值
     */
    void cmpxattr(const char *name, uint8_t op, uint64_t v);

    /**
     * @brief 执行类方法
     * @param cls 类名称
     * @param method 方法名称
     * @param inbl 输入缓冲区
     */
    void exec(const char *cls, const char *method, bufferlist& inbl);

    /**
     * @brief 执行类方法（带输出缓冲区）
     * @param cls 类名称
     * @param method 方法名称
     * @param inbl 输入缓冲区
     * @param obl 输出缓冲区指针
     * @param prval 返回值指针
     */
    void exec(const char *cls, const char *method, bufferlist& inbl, bufferlist *obl, int *prval);

    /**
     * @brief 执行类方法（带完成回调）
     * @param cls 类名称
     * @param method 方法名称
     * @param inbl 输入缓冲区
     * @param completion 完成回调对象
     */
    void exec(const char *cls, const char *method, bufferlist& inbl, ObjectOperationCompletion *completion);

    /**
     * @brief 断言对象版本
     * @details 守卫操作，检查对象版本是否等于指定版本
     * @param ver 要检查的版本号
     */
    void assert_version(uint64_t ver);

    /**
     * @brief 断言对象存在
     * @details 守卫操作，检查对象必须已经存在
     */
    void assert_exists();

    /**
     * get key/value pairs for specified keys
     *
     * @param assertions [in] comparison assertions
     * @param prval [out] place error code in prval upon completion
     *
     * assertions has the form of mappings from keys to (comparison rval, assertion)
     * The assertion field may be CEPH_OSD_CMPXATTR_OP_[GT|LT|EQ].
     *
     * That is, to assert that the value at key 'foo' is greater than 'bar':
     *
     * ObjectReadOperation op;
     * int r;
     * map<string, pair<bufferlist, int> > assertions;
     * bufferlist bar(string('bar'));
     * assertions['foo'] = make_pair(bar, CEPH_OSD_CMP_XATTR_OP_GT);
     * op.omap_cmp(assertions, &r);
     */
    void omap_cmp(
      const std::map<std::string, std::pair<bufferlist, int> > &assertions,
      int *prval);

  protected:
    ObjectOperationImpl* impl;
    friend class IoCtx;
    friend class Rados;
  };

  /**
   * @class ObjectWriteOperation
   * @brief 复合对象写操作类
   *
   * ObjectWriteOperation 是 ObjectOperation 的子类，专门用于复合写操作。
   * 它在基类基础上增加了写特定的操作，如创建对象、写入数据、设置属性等。
   *
   * 所有操作都是原子性的，要么全部成功，要么全部失败。这确保了数据的一致性。
   *
   * 典型用途：
   * - 创建新对象并设置初始数据
   * - 原子性地更新多个属性
   * - 执行复杂的写操作序列
   *
   * @note 继承自 ObjectOperation，支持所有基类操作加上写特定操作
   */
  class CEPH_RADOS_API ObjectWriteOperation : public ObjectOperation
  {
  protected:
    time_t *unused;  ///< 未使用的字段（为兼容性保留）

  public:
    /// @brief 默认构造函数
    ObjectWriteOperation() : unused(NULL) {}

    /// @brief 析构函数
    ~ObjectWriteOperation() override {}

    /// @brief 默认移动构造函数
    ObjectWriteOperation(ObjectWriteOperation&&) = default;

    /// @brief 默认移动赋值运算符
    ObjectWriteOperation& operator =(ObjectWriteOperation&&) = default;

    /**
     * @brief 设置修改时间（已废弃）
     * @param pt 时间戳指针
     * @deprecated 不再推荐使用
     */
    void mtime(time_t *pt);

    /**
     * @brief 设置修改时间（高精度版本）
     * @param pts 高精度时间戳指针
     */
    void mtime2(struct timespec *pts);

    /**
     * @brief 创建对象
     * @param exclusive 如果为true，则只有当对象不存在时才创建成功
     */
    void create(bool exclusive);

    /**
     * @brief 创建对象（带类别，已废弃）
     * @param exclusive 如果为true，则只有当对象不存在时才创建成功
     * @param category 对象类别（未使用）
     * @deprecated 类别参数未使用，请使用 create(bool exclusive)
     */
    void create(bool exclusive, const std::string& category); ///< NOTE: category is unused

    /**
     * @brief 在指定偏移量写入数据
     * @param off 写入起始偏移量
     * @param bl 要写入的数据缓冲区（函数会获取缓冲区内容的所有权）
     * @note 此调用会获取 @param bl 内容的所有权
     */
    void write(uint64_t off, const bufferlist& bl);

    /**
     * @brief 完全替换对象内容
     * @param bl 要写入的数据缓冲区（函数会获取缓冲区内容的所有权）
     * @note 此调用会获取 @param bl 内容的所有权
     */
    void write_full(const bufferlist& bl);

    /**
     * @brief 重复写入相同数据
     * @param off 写入起始偏移量
     * @param write_len 要写入的数据长度
     * @param bl 要重复写入的数据缓冲区
     */
    void writesame(uint64_t off, uint64_t write_len, const bufferlist& bl);

    /**
     * @brief 追加数据到对象末尾
     * @param bl 要追加的数据缓冲区（函数会获取缓冲区内容的所有权）
     * @note 此调用会获取 @param bl 内容的所有权
     */
    void append(const bufferlist& bl);

    /// @brief 删除对象
    void remove();

    /**
     * @brief 截断对象到指定大小
     * @param off 截断位置，对象大小将被设置为此值
     */
    void truncate(uint64_t off);

    /**
     * @brief 在指定范围内写入零数据
     * @param off 起始偏移量
     * @param len 要写入零数据的长度
     */
    void zero(uint64_t off, uint64_t len);

    /**
     * @brief 删除扩展属性
     * @param name 要删除的属性名称
     */
    void rmxattr(const char *name);

    /**
     * @brief 设置扩展属性
     * @param name 属性名称
     * @param bl 属性值缓冲区
     */
    void setxattr(const char *name, const bufferlist& bl);

    /**
     * @brief 设置扩展属性（右值引用版本）
     * @param name 属性名称
     * @param bl 属性值缓冲区（右值引用）
     */
    void setxattr(const char *name, const bufferlist&& bl);

    /**
     * @brief 更新跟踪映射（tmap）
     * @param cmdbl 命令缓冲区（函数会获取缓冲区内容的所有权）
     * @note 此调用会获取 @param cmdbl 内容的所有权
     */
    void tmap_update(const bufferlist& cmdbl);

    /**
     * @brief 设置跟踪映射（tmap）
     * @param bl 数据缓冲区
     */
    void tmap_put(const bufferlist& bl);

    /**
     * @brief 自管理快照回滚
     * @param snapid 要回滚到的快照ID
     */
    void selfmanaged_snap_rollback(uint64_t snapid);

    /**
     * @brief 回滚对象到指定快照
     * @details 将对象回滚到指定的快照版本，与池快照一起使用
     * @param snapid 要回滚到的快照ID
     */
    void snap_rollback(uint64_t snapid);

    /**
     * set keys and values according to map
     *
     * @param map [in] keys and values to set
     */
    void omap_set(const std::map<std::string, bufferlist> &map);

    /**
     * set header
     *
     * @param bl [in] header to set
     */
    void omap_set_header(const bufferlist &bl);

    /**
     * Clears omap contents
     */
    void omap_clear();

    /**
     * Clears keys in to_rm
     *
     * @param to_rm [in] keys to remove
     */
    void omap_rm_keys(const std::set<std::string> &to_rm);

    /**
     * Copy an object
     *
     * Copies an object from another location.  The operation is atomic in that
     * the copy either succeeds in its entirety or fails (e.g., because the
     * source object was modified while the copy was in progress).
     *
     * @param src source object name
     * @param src_ioctx ioctx for the source object
     * @param src_version current version of the source object
     * @param src_fadvise_flags the fadvise flags for source object
     */
    void copy_from(const std::string& src, const IoCtx& src_ioctx,
		   uint64_t src_version, uint32_t src_fadvise_flags);

    /**
     * Copy an object
     *
     * Copies an object from another location.  The operation is atomic in that
     * the copy either succeeds in its entirety or fails (e.g., because the
     * source object was modified while the copy was in progress).  Instead of
     * copying truncate_seq and truncate_size from the source object it receives
     * these values as parameters.
     *
     * @param src source object name
     * @param src_ioctx ioctx for the source object
     * @param src_version current version of the source object
     * @param truncate_seq truncate sequence for the destination object
     * @param truncate_size truncate size for the destination object
     * @param src_fadvise_flags the fadvise flags for source object
     */
    void copy_from2(const std::string& src, const IoCtx& src_ioctx,
		    uint64_t src_version, uint32_t truncate_seq,
		    uint64_t truncate_size, uint32_t src_fadvise_flags);

    /**
     * undirty an object
     *
     * Clear an objects dirty flag
     */
    void undirty();

    /**
     * Set allocation hint for an object
     *
     * @param expected_object_size expected size of the object, in bytes
     * @param expected_write_size expected size of writes to the object, in bytes
     * @param flags flags ()
     */
    void set_alloc_hint(uint64_t expected_object_size,
                        uint64_t expected_write_size);
    void set_alloc_hint2(uint64_t expected_object_size,
			 uint64_t expected_write_size,
			 uint32_t flags);

    /**
     * Pin/unpin an object in cache tier
     *
     * @returns 0 on success, negative error code on failure
     */
    void cache_pin();
    void cache_unpin();

    /**
     * Extensible tier
     *
     * Set redirect target
     */
    void set_redirect(const std::string& tgt_obj, const IoCtx& tgt_ioctx,
		      uint64_t tgt_version, int flag = 0);
    void tier_promote();
    void unset_manifest();

    friend class IoCtx;
  };

  /**
   * @class ObjectReadOperation
   * @brief 复合对象读操作类
   *
   * ObjectReadOperation 是 ObjectOperation 的子类，专门用于复合读操作。
   * 它在基类基础上增加了读特定的操作，如读取对象数据、获取属性、计算校验和等。
   *
   * 读操作可以返回各种类型的数据，包括：
   * - 对象元数据（大小、修改时间等）
   * - 对象数据内容
   * - 扩展属性和omap数据
   * - 校验和和快照信息
   *
   * 所有操作都是原子性的，要么全部成功，要么全部失败。
   *
   * @note 继承自 ObjectOperation，支持所有基类操作加上读特定操作
   */
  class CEPH_RADOS_API ObjectReadOperation : public ObjectOperation
  {
  public:
    /// @brief 默认构造函数
    ObjectReadOperation() {}

    /// @brief 析构函数
    ~ObjectReadOperation() override {}

    /// @brief 默认移动构造函数
    ObjectReadOperation(ObjectReadOperation&&) = default;

    /// @brief 默认移动赋值运算符
    ObjectReadOperation& operator =(ObjectReadOperation&&) = default;

    /**
     * @brief 获取对象状态信息
     * @param psize 返回对象大小的指针
     * @param pmtime 返回修改时间的指针
     * @param prval 返回操作结果的指针
     */
    void stat(uint64_t *psize, time_t *pmtime, int *prval);

    /**
     * @brief 获取对象状态信息（高精度时间版本）
     * @param psize 返回对象大小的指针
     * @param pts 返回高精度修改时间的指针
     * @param prval 返回操作结果的指针
     */
    void stat2(uint64_t *psize, struct timespec *pts, int *prval);

    /**
     * @brief 获取扩展属性值
     * @param name 属性名称
     * @param pbl 返回属性值的缓冲区指针
     * @param prval 返回操作结果的指针
     */
    void getxattr(const char *name, bufferlist *pbl, int *prval);

    /**
     * @brief 获取所有扩展属性
     * @param pattrs 返回属性映射的指针（属性名->属性值）
     * @param prval 返回操作结果的指针
     */
    void getxattrs(std::map<std::string, bufferlist> *pattrs, int *prval);

    /**
     * @brief 读取对象数据
     * @param off 读取起始偏移量
     * @param len 要读取的字节数
     * @param pbl 返回数据的缓冲区指针
     * @param prval 返回操作结果的指针
     */
    void read(size_t off, uint64_t len, bufferlist *pbl, int *prval);

    /**
     * @brief 计算对象数据校验和
     * @param type 校验和类型
     * @param init_value_bl 初始值缓冲区
     * @param off 计算起始偏移量
     * @param len 计算长度
     * @param chunk_size 分块大小
     * @param pbl 返回校验和结果的缓冲区指针
     * @param prval 返回操作结果的指针
     */
    void checksum(rados_checksum_type_t type, const bufferlist &init_value_bl,
		  uint64_t off, size_t len, size_t chunk_size, bufferlist *pbl,
		  int *prval);

    /**
     * see aio_sparse_read()
     */
    void sparse_read(uint64_t off, uint64_t len, std::map<uint64_t,uint64_t> *m,
                     bufferlist *data_bl, int *prval,
                     uint64_t truncate_size = 0,
                     uint32_t truncate_seq = 0);

    /**
     * omap_get_vals: keys and values from the object omap
     *
     * Get up to max_return keys and values beginning after start_after
     *
     * @param start_after [in] list no keys smaller than start_after
     * @param max_return [in] list no more than max_return key/value pairs
     * @param out_vals [out] place returned values in out_vals on completion
     * @param prval [out] place error code in prval upon completion
     */
    void omap_get_vals(
      const std::string &start_after,
      uint64_t max_return,
      std::map<std::string, bufferlist> *out_vals,
      int *prval) __attribute__ ((deprecated));  // use v2

    /**
     * omap_get_vals: keys and values from the object omap
     *
     * Get up to max_return keys and values beginning after start_after
     *
     * @param start_after [in] list no keys smaller than start_after
     * @param max_return [in] list no more than max_return key/value pairs
     * @param out_vals [out] place returned values in out_vals on completion
     * @param prval [out] place error code in prval upon completion
     */
    void omap_get_vals2(
      const std::string &start_after,
      uint64_t max_return,
      std::map<std::string, bufferlist> *out_vals,
      bool *pmore,
      int *prval);

    /**
     * omap_get_vals: keys and values from the object omap
     *
     * Get up to max_return keys and values beginning after start_after
     *
     * @param start_after [in] list keys starting after start_after
     * @param filter_prefix [in] list only keys beginning with filter_prefix
     * @param max_return [in] list no more than max_return key/value pairs
     * @param out_vals [out] place returned values in out_vals on completion
     * @param prval [out] place error code in prval upon completion
     */
    void omap_get_vals(
      const std::string &start_after,
      const std::string &filter_prefix,
      uint64_t max_return,
      std::map<std::string, bufferlist> *out_vals,
      int *prval) __attribute__ ((deprecated));  // use v2

    /**
     * omap_get_vals2: keys and values from the object omap
     *
     * Get up to max_return keys and values beginning after start_after
     *
     * @param start_after [in] list keys starting after start_after
     * @param filter_prefix [in] list only keys beginning with filter_prefix
     * @param max_return [in] list no more than max_return key/value pairs
     * @param out_vals [out] place returned values in out_vals on completion
     * @param pmore [out] pointer to bool indicating whether there are more keys
     * @param prval [out] place error code in prval upon completion
     */
    void omap_get_vals2(
      const std::string &start_after,
      const std::string &filter_prefix,
      uint64_t max_return,
      std::map<std::string, bufferlist> *out_vals,
      bool *pmore,
      int *prval);


    /**
     * omap_get_keys: keys from the object omap
     *
     * Get up to max_return keys beginning after start_after
     *
     * @param start_after [in] list keys starting after start_after
     * @param max_return [in] list no more than max_return keys
     * @param out_keys [out] place returned values in out_keys on completion
     * @param prval [out] place error code in prval upon completion
     */
    void omap_get_keys(const std::string &start_after,
                       uint64_t max_return,
                       std::set<std::string> *out_keys,
                       int *prval) __attribute__ ((deprecated)); // use v2

    /**
     * omap_get_keys2: keys from the object omap
     *
     * Get up to max_return keys beginning after start_after
     *
     * @param start_after [in] list keys starting after start_after
     * @param max_return [in] list no more than max_return keys
     * @param out_keys [out] place returned values in out_keys on completion
     * @param pmore [out] pointer to bool indicating whether there are more keys
     * @param prval [out] place error code in prval upon completion
     */
    void omap_get_keys2(const std::string &start_after,
			uint64_t max_return,
			std::set<std::string> *out_keys,
			bool *pmore,
			int *prval);

    /**
     * omap_get_header: get header from object omap
     *
     * @param header [out] place header here upon completion
     * @param prval [out] place error code in prval upon completion
     */
    void omap_get_header(bufferlist *header, int *prval);

    /**
     * get key/value pairs for specified keys
     *
     * @param keys [in] keys to get
     * @param map [out] place key/value pairs found here on completion
     * @param prval [out] place error code in prval upon completion
     */
    void omap_get_vals_by_keys(const std::set<std::string> &keys,
			       std::map<std::string, bufferlist> *map,
			       int *prval);

    /**
     * list_watchers: Get list watchers of object
     *
     * @param out_watchers [out] place returned values in out_watchers on completion
     * @param prval [out] place error code in prval upon completion
     */
    void list_watchers(std::list<obj_watch_t> *out_watchers, int *prval);

    /**
     * list snapshot clones associated with a logical object
     *
     * This will include a record for each version of the object,
     * include the "HEAD" (which will have a cloneid of SNAP_HEAD).
     * Each clone includes a vector of snap ids for which it is
     * defined to exist.
     *
     * NOTE: this operation must be submitted from an IoCtx with a
     * read snapid of SNAP_DIR for reliable results.
     *
     * @param out_snaps [out] pointer to resulting snap_set_t
     * @param prval [out] place error code in prval upon completion
     */
    void list_snaps(snap_set_t *out_snaps, int *prval);

    /**
     * query dirty state of an object
     *
     * @param isdirty [out] pointer to resulting bool
     * @param prval [out] place error code in prval upon completion
     */
    void is_dirty(bool *isdirty, int *prval);

    /**
     * flush a cache tier object to backing tier; will block racing
     * updates.
     *
     * This should be used in concert with OPERATION_IGNORE_CACHE to avoid
     * triggering a promotion.
     */
    void cache_flush();

    /**
     * Flush a cache tier object to backing tier; will EAGAIN if we race
     * with an update.  Must be used with the SKIPRWLOCKS flag.
     *
     * This should be used in concert with OPERATION_IGNORE_CACHE to avoid
     * triggering a promotion.
     */
    void cache_try_flush();

    /**
     * evict a clean cache tier object
     *
     * This should be used in concert with OPERATION_IGNORE_CACHE to avoid
     * triggering a promote on the OSD (that is then evicted).
     */
    void cache_evict();

    /**
     * Extensible tier
     *
     * set_chunk: make a chunk pointing a part of the source object at the target 
     * 		  object
     *
     * @param src_offset [in] source offset to indicate the start position of 
     * 				a chunk in the source object
     * @param src_length [in] source length to set the length of the chunk
     * @param tgt_oid    [in] target object's id to set a chunk
     * @param tgt_offset [in] the start position of the target object
     * @param flag       [in] flag for the source object
     *
     */
    void set_chunk(uint64_t src_offset, uint64_t src_length, const IoCtx& tgt_ioctx,
                   std::string tgt_oid, uint64_t tgt_offset, int flag = 0);
    /**
     * flush a manifest tier object to backing tier, performing deduplication;
     * will block racing updates.
     *
     * Invoking tier_flush() implicitly makes a manifest object even if
     * the target object is not manifest. 
     */
    void tier_flush();
    /**
     * evict a manifest tier object to backing tier; will block racing
     * updates.
     */
    void tier_evict();
  };

  /**
   * @class IoCtx
   * @brief I/O上下文类
   *
   * IoCtx 是librados中用于执行I/O操作的核心类。它代表一个到特定Ceph存储池的连接上下文，
   * 通过该上下文可以对池中的对象执行各种操作，包括读、写、删除、快照管理等。
   *
   * 典型使用流程（错误检查省略）：
   * @code
   * IoCtx io_ctx;
   * rados.ioctx_create("my_pool", io_ctx);
   * io_ctx.write("myobject", bufferlist("data"), 4, 0);
   * io_ctx.read("myobject", bl, 1024, 0);
   * @endcode
   *
   * @note 使用监视（watch）事件的IoCtx在销毁前必须调用watch_flush()，以确保竞态回调已完成。
   *
   * 主要功能：
   * - 对象操作：创建、读取、写入、删除对象
   * - 异步I/O：支持高性能异步操作
   * - 快照管理：创建、删除、回滚快照
   * - 对象锁：共享锁和排他锁管理
   * - 对象监视：监听对象变更通知
   * - 元数据操作：扩展属性和omap操作
   * - 对象列表：遍历池中的对象
   */
  class CEPH_RADOS_API IoCtx
  {
  public:
    /// @brief 默认构造函数
    IoCtx();

    /**
     * @brief 从rados_ioctx_t构造
     * @param p C API的I/O上下文
     * @param pool 目标IoCtx对象
     */
    static void from_rados_ioctx_t(rados_ioctx_t p, IoCtx &pool);

    /// @brief 拷贝构造函数
    IoCtx(const IoCtx& rhs);

    /// @brief 拷贝赋值运算符
    IoCtx& operator=(const IoCtx& rhs);

    /// @brief 移动构造函数
    IoCtx(IoCtx&& rhs) noexcept;

    /// @brief 移动赋值运算符
    IoCtx& operator=(IoCtx&& rhs) noexcept;

    /// @brief 析构函数
    ~IoCtx();

    /**
     * @brief 检查上下文是否有效
     * @return 如果上下文有效返回true，否则返回false
     */
    bool is_valid() const;

    /// @brief 关闭池连接句柄
    void close();

    // deep copy
    void dup(const IoCtx& rhs);

    // set pool auid
    int set_auid(uint64_t auid_)
      __attribute__ ((deprecated));

    // set pool auid
    int set_auid_async(uint64_t auid_, PoolAsyncCompletion *c)
      __attribute__ ((deprecated));

    // get pool auid
    int get_auid(uint64_t *auid_)
      __attribute__ ((deprecated));

    uint64_t get_instance_id() const;

    std::string get_pool_name();

    bool pool_requires_alignment();
    int pool_requires_alignment2(bool * req);
    uint64_t pool_required_alignment();
    int pool_required_alignment2(uint64_t * alignment);

    // create an object
    int create(const std::string& oid, bool exclusive);
    int create(const std::string& oid, bool exclusive,
	       const std::string& category); ///< category is unused

    /**
     * write bytes to an object at a specified offset
     *
     * NOTE: this call steals the contents of @param bl.
     */
    int write(const std::string& oid, bufferlist& bl, size_t len, uint64_t off);
    /**
     * append bytes to an object
     *
     * NOTE: this call steals the contents of @param bl.
     */
    int append(const std::string& oid, bufferlist& bl, size_t len);
    /**
     * replace object contents with provided data
     *
     * NOTE: this call steals the contents of @param bl.
     */
    int write_full(const std::string& oid, bufferlist& bl);
    int writesame(const std::string& oid, bufferlist& bl,
		  size_t write_len, uint64_t off);
    int read(const std::string& oid, bufferlist& bl, size_t len, uint64_t off);
    int checksum(const std::string& o, rados_checksum_type_t type,
		 const bufferlist &init_value_bl, size_t len, uint64_t off,
		 size_t chunk_size, bufferlist *pbl);
    int remove(const std::string& oid);
    int remove(const std::string& oid, int flags);
    int trunc(const std::string& oid, uint64_t size);
    int mapext(const std::string& o, uint64_t off, size_t len, std::map<uint64_t,uint64_t>& m);
    int cmpext(const std::string& o, uint64_t off, bufferlist& cmp_bl);
    int sparse_read(const std::string& o, std::map<uint64_t,uint64_t>& m, bufferlist& bl, size_t len, uint64_t off);
    int getxattr(const std::string& oid, const char *name, bufferlist& bl);
    int getxattrs(const std::string& oid, std::map<std::string, bufferlist>& attrset);
    int setxattr(const std::string& oid, const char *name, bufferlist& bl);
    int rmxattr(const std::string& oid, const char *name);
    int stat(const std::string& oid, uint64_t *psize, time_t *pmtime);
    int stat2(const std::string& oid, uint64_t *psize, struct timespec *pts);
    int exec(const std::string& oid, const char *cls, const char *method,
	     bufferlist& inbl, bufferlist& outbl);
    /**
     * modify object tmap based on encoded update sequence
     *
     * NOTE: this call steals the contents of @param bl
     */
    int tmap_update(const std::string& oid, bufferlist& cmdbl);

    int omap_get_vals(const std::string& oid,
                      const std::string& start_after,
                      uint64_t max_return,
                      std::map<std::string, bufferlist> *out_vals);
    int omap_get_vals2(const std::string& oid,
		       const std::string& start_after,
		       uint64_t max_return,
		       std::map<std::string, bufferlist> *out_vals,
		       bool *pmore);
    int omap_get_vals(const std::string& oid,
                      const std::string& start_after,
                      const std::string& filter_prefix,
                      uint64_t max_return,
                      std::map<std::string, bufferlist> *out_vals);
    int omap_get_vals2(const std::string& oid,
		       const std::string& start_after,
		       const std::string& filter_prefix,
		       uint64_t max_return,
		       std::map<std::string, bufferlist> *out_vals,
		       bool *pmore);
    int omap_get_keys(const std::string& oid,
                      const std::string& start_after,
                      uint64_t max_return,
                      std::set<std::string> *out_keys);
    int omap_get_keys2(const std::string& oid,
		       const std::string& start_after,
		       uint64_t max_return,
		       std::set<std::string> *out_keys,
		       bool *pmore);
    int omap_get_header(const std::string& oid,
                        bufferlist *bl);
    int omap_get_vals_by_keys(const std::string& oid,
                              const std::set<std::string>& keys,
                              std::map<std::string, bufferlist> *vals);
    int omap_set(const std::string& oid,
                 const std::map<std::string, bufferlist>& map);
    int omap_set_header(const std::string& oid,
                        const bufferlist& bl);
    int omap_clear(const std::string& oid);
    int omap_rm_keys(const std::string& oid,
                     const std::set<std::string>& keys);

    void snap_set_read(snap_t seq);
    int selfmanaged_snap_set_write_ctx(snap_t seq, std::vector<snap_t>& snaps);

    // Create a snapshot with a given name
    int snap_create(const char *snapname);

    // Look up a snapshot by name.
    // Returns 0 on success; error code otherwise
    int snap_lookup(const char *snapname, snap_t *snap);

    // Gets a timestamp for a snap
    int snap_get_stamp(snap_t snapid, time_t *t);

    // Gets the name of a snap
    int snap_get_name(snap_t snapid, std::string *s);

    // Remove a snapshot from this pool
    int snap_remove(const char *snapname);

    int snap_list(std::vector<snap_t> *snaps);

    int snap_rollback(const std::string& oid, const char *snapname);

    // Deprecated name kept for backward compatibility - same as snap_rollback()
    int rollback(const std::string& oid, const char *snapname)
      __attribute__ ((deprecated));

    int selfmanaged_snap_create(uint64_t *snapid);
    void aio_selfmanaged_snap_create(uint64_t *snapid, AioCompletion *c);

    int selfmanaged_snap_remove(uint64_t snapid);
    void aio_selfmanaged_snap_remove(uint64_t snapid, AioCompletion *c);

    int selfmanaged_snap_rollback(const std::string& oid, uint64_t snapid);

    // Advisory locking on rados objects.
    int lock_exclusive(const std::string &oid, const std::string &name,
		       const std::string &cookie,
		       const std::string &description,
		       struct timeval * duration, uint8_t flags);

    int lock_shared(const std::string &oid, const std::string &name,
		    const std::string &cookie, const std::string &tag,
		    const std::string &description,
		    struct timeval * duration, uint8_t flags);

    int unlock(const std::string &oid, const std::string &name,
	       const std::string &cookie);

    int break_lock(const std::string &oid, const std::string &name,
		   const std::string &client, const std::string &cookie);

    int list_lockers(const std::string &oid, const std::string &name,
		     int *exclusive,
		     std::string *tag,
		     std::list<librados::locker_t> *lockers);


    /// Start enumerating objects for a pool. Errors are thrown as exceptions.
    NObjectIterator nobjects_begin(const bufferlist &filter=bufferlist());
    /// Start enumerating objects for a pool starting from a hash position.
    /// Errors are thrown as exceptions.
    NObjectIterator nobjects_begin(uint32_t start_hash_position,
                                   const bufferlist &filter=bufferlist());
    /// Start enumerating objects for a pool starting from cursor. Errors are
    /// thrown as exceptions.
    NObjectIterator nobjects_begin(const librados::ObjectCursor& cursor,
                                   const bufferlist &filter=bufferlist());
    /// Iterator indicating the end of a pool
    const NObjectIterator& nobjects_end() const;

    /// Get cursor for pool beginning
    ObjectCursor object_list_begin();

    /// Get cursor for pool end
    ObjectCursor object_list_end();

    /// Check whether a cursor is at the end of a pool
    bool object_list_is_end(const ObjectCursor &oc);

    /// List some objects between two cursors
    int object_list(const ObjectCursor &start, const ObjectCursor &finish,
                    const size_t result_count,
                    const bufferlist &filter,
                    std::vector<ObjectItem> *result,
                    ObjectCursor *next);

    /// Generate cursors that include the N out of Mth slice of the pool
    void object_list_slice(
        const ObjectCursor start,
        const ObjectCursor finish,
        const size_t n,
        const size_t m,
        ObjectCursor *split_start,
        ObjectCursor *split_finish);

    /**
     * List available hit set objects
     *
     * @param uint32_t [in] hash position to query
     * @param c [in] completion
     * @param pls [out] list of available intervals
     */
    int hit_set_list(uint32_t hash, AioCompletion *c,
		     std::list< std::pair<time_t, time_t> > *pls);

    /**
     * Retrieve hit set for a given hash, and time
     *
     * @param hash [in] hash position
     * @param c [in] completion
     * @param stamp [in] time interval that falls within the hit set's interval
     * @param pbl [out] buffer to store the result in
     */
    int hit_set_get(uint32_t hash, AioCompletion *c, time_t stamp,
		    bufferlist *pbl);

    uint64_t get_last_version();

    int aio_read(const std::string& oid, AioCompletion *c,
		 bufferlist *pbl, size_t len, uint64_t off);
    /**
     * Asynchronously read from an object at a particular snapshot
     *
     * This is the same as normal aio_read, except that it chooses
     * the snapshot to read from from its arguments instead of the
     * internal IoCtx state.
     *
     * The return value of the completion will be number of bytes read on
     * success, negative error code on failure.
     *
     * @param oid the name of the object to read from
     * @param c what to do when the read is complete
     * @param pbl where to store the results
     * @param len the number of bytes to read
     * @param off the offset to start reading from in the object
     * @param snapid the id of the snapshot to read from
     * @returns 0 on success, negative error code on failure
     */
    int aio_read(const std::string& oid, AioCompletion *c,
		 bufferlist *pbl, size_t len, uint64_t off, uint64_t snapid);
    int aio_sparse_read(const std::string& oid, AioCompletion *c,
			std::map<uint64_t,uint64_t> *m, bufferlist *data_bl,
			size_t len, uint64_t off);
    /**
     * Asynchronously read existing extents from an object at a
     * particular snapshot
     *
     * This is the same as normal aio_sparse_read, except that it chooses
     * the snapshot to read from from its arguments instead of the
     * internal IoCtx state.
     *
     * m will be filled in with a map of extents in the object,
     * mapping offsets to lengths (in bytes) within the range
     * requested. The data for all of the extents are stored
     * back-to-back in offset order in data_bl.
     *
     * @param oid the name of the object to read from
     * @param c what to do when the read is complete
     * @param m where to store the map of extents
     * @param data_bl where to store the data
     * @param len the number of bytes to read
     * @param off the offset to start reading from in the object
     * @param snapid the id of the snapshot to read from
     * @returns 0 on success, negative error code on failure
     */
    int aio_sparse_read(const std::string& oid, AioCompletion *c,
			std::map<uint64_t,uint64_t> *m, bufferlist *data_bl,
			size_t len, uint64_t off, uint64_t snapid);
    /**
     * Asynchronously compare an on-disk object range with a buffer
     *
     * @param oid the name of the object to read from
     * @param c what to do when the read is complete
     * @param off object byte offset at which to start the comparison
     * @param cmp_bl buffer containing bytes to be compared with object contents
     * @returns 0 on success, negative error code on failure,
     *  (-MAX_ERRNO - mismatch_off) on mismatch
     */
    int aio_cmpext(const std::string& oid,
		   librados::AioCompletion *c,
		   uint64_t off,
		   bufferlist& cmp_bl);
    int aio_write(const std::string& oid, AioCompletion *c, const bufferlist& bl,
		  size_t len, uint64_t off);
    int aio_append(const std::string& oid, AioCompletion *c, const bufferlist& bl,
		  size_t len);
    int aio_write_full(const std::string& oid, AioCompletion *c, const bufferlist& bl);
    int aio_writesame(const std::string& oid, AioCompletion *c, const bufferlist& bl,
		      size_t write_len, uint64_t off);

    /**
     * Asynchronously remove an object
     *
     * Queues the remove and returns.
     *
     * The return value of the completion will be 0 on success, negative
     * error code on failure.
     *
     * @param oid the name of the object
     * @param c what to do when the remove is safe and complete
     * @returns 0 on success, -EROFS if the io context specifies a snap_seq
     * other than SNAP_HEAD
     */
    int aio_remove(const std::string& oid, AioCompletion *c);
    int aio_remove(const std::string& oid, AioCompletion *c, int flags);

    /**
     * Wait for all currently pending aio writes to be safe.
     *
     * @returns 0 on success, negative error code on failure
     */
    int aio_flush();

    /**
     * Schedule a callback for when all currently pending
     * aio writes are safe. This is a non-blocking version of
     * aio_flush().
     *
     * @param c what to do when the writes are safe
     * @returns 0 on success, negative error code on failure
     */
    int aio_flush_async(AioCompletion *c);
    int aio_getxattr(const std::string& oid, AioCompletion *c, const char *name, bufferlist& bl);
    int aio_getxattrs(const std::string& oid, AioCompletion *c, std::map<std::string, bufferlist>& attrset);
    int aio_setxattr(const std::string& oid, AioCompletion *c, const char *name, bufferlist& bl);
    int aio_rmxattr(const std::string& oid, AioCompletion *c, const char *name);
    int aio_stat(const std::string& oid, AioCompletion *c, uint64_t *psize, time_t *pmtime);
    int aio_stat2(const std::string& oid, AioCompletion *c, uint64_t *psize, struct timespec *pts);

    /**
     * Cancel aio operation
     *
     * @param c completion handle
     * @returns 0 on success, negative error code on failure
     */
    int aio_cancel(AioCompletion *c);

    int aio_exec(const std::string& oid, AioCompletion *c, const char *cls, const char *method,
	         bufferlist& inbl, bufferlist *outbl);

    /*
     * asynchronous version of unlock
     */
    int aio_unlock(const std::string &oid, const std::string &name,
	           const std::string &cookie, AioCompletion *c);

    // compound object operations
    int operate(const std::string& oid, ObjectWriteOperation *op);
    int operate(const std::string& oid, ObjectWriteOperation *op, int flags);
    int operate(const std::string& oid, ObjectWriteOperation *op, int flags, const jspan_context *trace_info);
    int operate(const std::string& oid, ObjectReadOperation *op, bufferlist *pbl);
    int operate(const std::string& oid, ObjectReadOperation *op, bufferlist *pbl, int flags);
    int aio_operate(const std::string& oid, AioCompletion *c, ObjectWriteOperation *op);
    int aio_operate(const std::string& oid, AioCompletion *c, ObjectWriteOperation *op, int flags);
    int aio_operate(const std::string& oid, AioCompletion *c, ObjectWriteOperation *op, int flags, const jspan_context *trace_info);
    /**
     * Schedule an async write operation with explicit snapshot parameters
     *
     * This is the same as the first aio_operate(), except that it
     * gets the snapshot context from its arguments instead of the
     * IoCtx internal state.
     *
     * @param oid the object to operate on
     * @param c what to do when the operation is complete and safe
     * @param op which operations to perform
     * @param seq latest selfmanaged snapshot sequence number for this object
     * @param snaps currently existing selfmanaged snapshot ids for this object
     * @returns 0 on success, negative error code on failure
     */
    int aio_operate(const std::string& oid, AioCompletion *c,
		    ObjectWriteOperation *op, snap_t seq,
		    std::vector<snap_t>& snaps);
    int aio_operate(const std::string& oid, AioCompletion *c,
        ObjectWriteOperation *op, snap_t seq,
        std::vector<snap_t>& snaps,
        const blkin_trace_info *trace_info);
    int aio_operate(const std::string& oid, AioCompletion *c,
        ObjectWriteOperation *op, snap_t seq,
        std::vector<snap_t>& snaps, int flags,
        const blkin_trace_info *trace_info);
    int aio_operate(const std::string& oid, AioCompletion *c,
		    ObjectReadOperation *op, bufferlist *pbl);

    int aio_operate(const std::string& oid, AioCompletion *c,
		    ObjectReadOperation *op, snap_t snapid, int flags,
		    bufferlist *pbl)
      __attribute__ ((deprecated));

    int aio_operate(const std::string& oid, AioCompletion *c,
		    ObjectReadOperation *op, int flags,
		    bufferlist *pbl);
    int aio_operate(const std::string& oid, AioCompletion *c,
        ObjectReadOperation *op, int flags,
        bufferlist *pbl, const blkin_trace_info *trace_info);

    // watch/notify
    int watch2(const std::string& o, uint64_t *handle,
	       librados::WatchCtx2 *ctx);
    int watch3(const std::string& o, uint64_t *handle,
	       librados::WatchCtx2 *ctx, uint32_t timeout);
    int aio_watch(const std::string& o, AioCompletion *c, uint64_t *handle,
	       librados::WatchCtx2 *ctx);
    int aio_watch2(const std::string& o, AioCompletion *c, uint64_t *handle,
	       librados::WatchCtx2 *ctx, uint32_t timeout);
    int unwatch2(uint64_t handle);
    int aio_unwatch(uint64_t handle, AioCompletion *c);
    /**
     * Send a notify event to watchers
     *
     * Upon completion the pbl bufferlist reply payload will be
     * encoded like so:
     *
     *    le32 num_acks
     *    {
     *      le64 gid     global id for the client (for client.1234 that's 1234)
     *      le64 cookie  cookie for the client
     *      le32 buflen  length of reply message buffer
     *      u8 * buflen  payload
     *    } * num_acks
     *    le32 num_timeouts
     *    {
     *      le64 gid     global id for the client
     *      le64 cookie  cookie for the client
     *    } * num_timeouts
     *
     *
     */
    int notify2(const std::string& o,   ///< object
		bufferlist& bl,         ///< optional broadcast payload
		uint64_t timeout_ms,    ///< timeout (in ms)
		bufferlist *pbl);       ///< reply buffer
    int aio_notify(const std::string& o,   ///< object
                   AioCompletion *c,       ///< completion when notify completes
                   bufferlist& bl,         ///< optional broadcast payload
                   uint64_t timeout_ms,    ///< timeout (in ms)
                   bufferlist *pbl);       ///< reply buffer
   /*
    * Decode a notify response into acks and timeout vectors.
    */
    void decode_notify_response(bufferlist &bl,
                                std::vector<librados::notify_ack_t> *acks,
                                std::vector<librados::notify_timeout_t> *timeouts);

    int list_watchers(const std::string& o, std::list<obj_watch_t> *out_watchers);
    int list_snaps(const std::string& o, snap_set_t *out_snaps);
    void set_notify_timeout(uint32_t timeout);

    /// acknowledge a notify we received.
    void notify_ack(const std::string& o, ///< watched object
		    uint64_t notify_id,   ///< notify id
		    uint64_t cookie,      ///< our watch handle
		    bufferlist& bl);      ///< optional reply payload

    /***
     * check on watch validity
     *
     * Check if a watch is valid.  If so, return the number of
     * milliseconds since we last confirmed its liveness.  If there is
     * a known error, return it.
     *
     * If there is an error, the watch is no longer valid, and should
     * be destroyed with unwatch().  The user is still interested in
     * the object, a new watch should be created with watch().
     *
     * @param cookie watch handle
     * @returns ms since last confirmed valid, or error
     */
    int watch_check(uint64_t cookie);

    // old, deprecated versions
    int watch(const std::string& o, uint64_t ver, uint64_t *cookie,
	      librados::WatchCtx *ctx) __attribute__ ((deprecated));
    int notify(const std::string& o, uint64_t ver, bufferlist& bl)
      __attribute__ ((deprecated));
    int unwatch(const std::string& o, uint64_t cookie)
      __attribute__ ((deprecated));

    /**
     * Set allocation hint for an object
     *
     * This is an advisory operation, it will always succeed (as if it
     * was submitted with a OP_FAILOK flag set) and is not guaranteed
     * to do anything on the backend.
     *
     * @param o the name of the object
     * @param expected_object_size expected size of the object, in bytes
     * @param expected_write_size expected size of writes to the object, in bytes
     * @returns 0 on success, negative error code on failure
     */
    int set_alloc_hint(const std::string& o,
                       uint64_t expected_object_size,
                       uint64_t expected_write_size);
    int set_alloc_hint2(const std::string& o,
			uint64_t expected_object_size,
			uint64_t expected_write_size,
			uint32_t flags);

    // assert version for next sync operations
    void set_assert_version(uint64_t ver);

    /**
     * Pin/unpin an object in cache tier
     *
     * @param o the name of the object
     * @returns 0 on success, negative error code on failure
     */
    int cache_pin(const std::string& o);
    int cache_unpin(const std::string& o);

    std::string get_pool_name() const;

    void locator_set_key(const std::string& key);
    void set_namespace(const std::string& nspace);
    std::string get_namespace() const;

    int64_t get_id();

    // deprecated versions
    uint32_t get_object_hash_position(const std::string& oid)
      __attribute__ ((deprecated));
    uint32_t get_object_pg_hash_position(const std::string& oid)
      __attribute__ ((deprecated));

    int get_object_hash_position2(const std::string& oid, uint32_t *hash_position);
    int get_object_pg_hash_position2(const std::string& oid, uint32_t *pg_hash_position);

    config_t cct();

    void set_osdmap_full_try()
      __attribute__ ((deprecated));
    void unset_osdmap_full_try()
      __attribute__ ((deprecated));

    bool get_pool_full_try();
    void set_pool_full_try();
    void unset_pool_full_try();

    int application_enable(const std::string& app_name, bool force);
    int application_enable_async(const std::string& app_name,
                                 bool force, PoolAsyncCompletion *c);
    int application_list(std::set<std::string> *app_names);
    int application_metadata_get(const std::string& app_name,
                                 const std::string &key,
                                 std::string *value);
    int application_metadata_set(const std::string& app_name,
                                 const std::string &key,
                                 const std::string& value);
    int application_metadata_remove(const std::string& app_name,
                                    const std::string &key);
    int application_metadata_list(const std::string& app_name,
                                  std::map<std::string, std::string> *values);

  private:
    /* You can only get IoCtx instances from Rados */
    IoCtx(IoCtxImpl *io_ctx_impl_);

    friend class Rados; // Only Rados can use our private constructor to create IoCtxes.
    friend class libradosstriper::RadosStriper; // Striper needs to see our IoCtxImpl
    friend class ObjectWriteOperation;  // copy_from needs to see our IoCtxImpl
    friend class ObjectReadOperation;  // set_chunk needs to see our IoCtxImpl

    IoCtxImpl *io_ctx_impl;
  };

  struct CEPH_RADOS_API PlacementGroup {
    PlacementGroup();
    PlacementGroup(const PlacementGroup&);
    ~PlacementGroup();
    bool parse(const char*);
    std::unique_ptr<PlacementGroupImpl> impl;
  };

  CEPH_RADOS_API std::ostream& operator<<(std::ostream&, const PlacementGroup&);

  /**
   * @class Rados
   * @brief RADOS集群客户端类
   *
   * Rados 是librados库的核心类，负责连接和管理Ceph集群。它提供了：
   * - 集群连接和认证
   * - 存储池管理（创建、删除、查询）
   * - 配置管理
   * - 集群监控和命令执行
   * - I/O上下文创建
   *
   * 使用Rados的典型流程：
   * 1. 创建Rados实例
   * 2. 初始化连接（指定集群ID或名称）
   * 3. 连接到集群
   * 4. 创建I/O上下文操作特定池
   * 5. 执行清理和关闭
   *
   * @code
   * Rados rados;
   * rados.init("admin");
   * rados.connect();
   * IoCtx io_ctx;
   * rados.ioctx_create("mypool", io_ctx);
   * // ... 执行I/O操作 ...
   * rados.shutdown();
   * @endcode
   */
  class CEPH_RADOS_API Rados
  {
  public:
    /**
     * @brief 获取librados版本信息
     * @param major 主版本号指针
     * @param minor 次版本号指针
     * @param extra 额外版本号指针
     */
    static void version(int *major, int *minor, int *extra);

    /// @brief 默认构造函数
    Rados();

    /**
     * @brief 从现有IoCtx构造Rados（已废弃）
     * @param ioctx 现有的I/O上下文
     * @deprecated 不再推荐使用此构造函数
     */
    explicit Rados(IoCtx& ioctx);

    /// @brief 析构函数
    ~Rados();

    /**
     * @brief 从rados_t构造
     * @param cluster C API的集群句柄
     * @param rados 目标Rados对象
     */
    static void from_rados_t(rados_t cluster, Rados &rados);

    /**
     * @brief 初始化集群连接（使用默认集群名称）
     * @param id 客户端标识符（如"admin"）
     * @return 0表示成功，负数表示错误
     */
    int init(const char * const id);

    /**
     * @brief 初始化集群连接（指定集群名称）
     * @param name 用户名称
     * @param clustername 集群名称
     * @param flags 初始化标志
     * @return 0表示成功，负数表示错误
     */
    int init2(const char * const name, const char * const clustername,
	      uint64_t flags);

    /**
     * @brief 使用现有配置上下文初始化
     * @param cct_ 配置上下文
     * @return 0表示成功，负数表示错误
     */
    int init_with_context(config_t cct_);

    /// @brief 获取配置上下文
    config_t cct();

    /**
     * @brief 连接到集群
     * @return 0表示成功，负数表示错误
     */
    int connect();

    /// @brief 关闭集群连接
    void shutdown();

    /**
     * @brief 刷新监视事件
     * @return 0表示成功，负数表示错误
     */
    int watch_flush();

    /**
     * @brief 异步刷新监视事件
     * @param c 异步完成回调
     * @return 0表示成功，负数表示错误
     */
    int aio_watch_flush(AioCompletion* c);
    int conf_read_file(const char * const path) const;
    int conf_parse_argv(int argc, const char ** argv) const;
    int conf_parse_argv_remainder(int argc, const char ** argv,
				  const char ** remargv) const;
    int conf_parse_env(const char *env) const;
    int conf_set(const char *option, const char *value);
    int conf_get(const char *option, std::string &val);

    int service_daemon_register(
      const std::string& service,  ///< service name (e.g., 'rgw')
      const std::string& name,     ///< daemon name (e.g., 'gwfoo')
      const std::map<std::string,std::string>& metadata); ///< static metadata about daemon
    int service_daemon_update_status(
      std::map<std::string,std::string>&& status);

    int pool_create(const char *name);
    int pool_create(const char *name, uint64_t auid)
      __attribute__ ((deprecated));
    int pool_create(const char *name, uint64_t auid, uint8_t crush_rule)
      __attribute__ ((deprecated));
    int pool_create_with_rule(const char *name, uint8_t crush_rule);
    int pool_create_async(const char *name, PoolAsyncCompletion *c);
    int pool_create_async(const char *name, uint64_t auid, PoolAsyncCompletion *c)
      __attribute__ ((deprecated));
    int pool_create_async(const char *name, uint64_t auid, uint8_t crush_rule, PoolAsyncCompletion *c)
      __attribute__ ((deprecated));
    int pool_create_with_rule_async(const char *name, uint8_t crush_rule, PoolAsyncCompletion *c);
    int pool_get_base_tier(int64_t pool, int64_t* base_tier);
    int pool_delete(const char *name);
    int pool_delete_async(const char *name, PoolAsyncCompletion *c);
    int64_t pool_lookup(const char *name);
    int pool_reverse_lookup(int64_t id, std::string *name);

    uint64_t get_instance_id();

    int get_min_compatible_osd(int8_t* require_osd_release);
    int get_min_compatible_client(int8_t* min_compat_client,
                                  int8_t* require_min_compat_client);

    int mon_command(std::string cmd, const bufferlist& inbl,
		    bufferlist *outbl, std::string *outs);
    int mgr_command(std::string cmd, const bufferlist& inbl,
		    bufferlist *outbl, std::string *outs);
    int osd_command(int osdid, std::string cmd, const bufferlist& inbl,
                    bufferlist *outbl, std::string *outs);
    int pg_command(const char *pgstr, std::string cmd, const bufferlist& inbl,
                   bufferlist *outbl, std::string *outs);

    int ioctx_create(const char *name, IoCtx &pioctx);
    int ioctx_create2(int64_t pool_id, IoCtx &pioctx);

    // Features useful for test cases
    void test_blocklist_self(bool set);

    /* pool info */
    int pool_list(std::list<std::string>& v);
    int pool_list2(std::list<std::pair<int64_t, std::string> >& v);
    int get_pool_stats(std::list<std::string>& v,
		       stats_map& result);
    /// deprecated; use simpler form.  categories no longer supported.
    int get_pool_stats(std::list<std::string>& v,
		       std::map<std::string, stats_map>& stats);
    /// deprecated; categories no longer supported
    int get_pool_stats(std::list<std::string>& v,
                       std::string& category,
		       std::map<std::string, stats_map>& stats);

    /// check if pool has or had selfmanaged snaps
    bool get_pool_is_selfmanaged_snaps_mode(const std::string& poolname)
      __attribute__ ((deprecated));
    int pool_is_in_selfmanaged_snaps_mode(const std::string& poolname);

    int cluster_stat(cluster_stat_t& result);
    int cluster_fsid(std::string *fsid);

    /**
     * List inconsistent placement groups in the given pool
     *
     * @param pool_id the pool id
     * @param pgs [out] the inconsistent PGs
     */
    int get_inconsistent_pgs(int64_t pool_id,
                             std::vector<PlacementGroup>* pgs);
    /**
     * List the inconsistent objects found in a given PG by last scrub
     *
     * @param pg the placement group returned by @c pg_list()
     * @param start_after the first returned @c objects
     * @param max_return the max number of the returned @c objects
     * @param c what to do when the operation is complete and safe
     * @param objects [out] the objects where inconsistencies are found
     * @param interval [in,out] an epoch indicating current interval
     * @returns if a non-zero @c interval is specified, will return -EAGAIN i
     *          the current interval begin epoch is different.
     */
    int get_inconsistent_objects(const PlacementGroup& pg,
                                 const object_id_t &start_after,
                                 unsigned max_return,
                                 AioCompletion *c,
                                 std::vector<inconsistent_obj_t>* objects,
                                 uint32_t* interval);
    /**
     * List the inconsistent snapsets found in a given PG by last scrub
     *
     * @param pg the placement group returned by @c pg_list()
     * @param start_after the first returned @c objects
     * @param max_return the max number of the returned @c objects
     * @param c what to do when the operation is complete and safe
     * @param snapsets [out] the objects where inconsistencies are found
     * @param interval [in,out] an epoch indicating current interval
     * @returns if a non-zero @c interval is specified, will return -EAGAIN i
     *          the current interval begin epoch is different.
     */
    int get_inconsistent_snapsets(const PlacementGroup& pg,
                                  const object_id_t &start_after,
                                  unsigned max_return,
                                  AioCompletion *c,
                                  std::vector<inconsistent_snapset_t>* snapset,
                                  uint32_t* interval);

    /// get/wait for the most recent osdmap
    int wait_for_latest_osdmap();

    int blocklist_add(const std::string& client_address,
                      uint32_t expire_seconds);

    std::string get_addrs() const;

    /*
     * pool aio
     *
     * It is up to the caller to release the completion handler, even if the pool_create_async()
     * and/or pool_delete_async() fails and does not send the async request
     */
    static PoolAsyncCompletion *pool_async_create_completion();

   // -- aio --
    static AioCompletion *aio_create_completion();
    static AioCompletion *aio_create_completion(void *cb_arg, callback_t cb_complete,
						callback_t cb_safe)
      __attribute__ ((deprecated));
    static AioCompletion *aio_create_completion(void *cb_arg, callback_t cb_complete);

    friend std::ostream& operator<<(std::ostream &oss, const Rados& r);
  private:
    friend class neorados::RADOS;

    // We don't allow assignment or copying
    Rados(const Rados& rhs);
    const Rados& operator=(const Rados& rhs);
    RadosClient *client;
  };

} // namespace v14_2_0
} // namespace librados

#endif

