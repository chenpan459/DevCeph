// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - 可扩展的分布式文件系统
 *
 * 版权所有 (C) 2011 New Dream Network
 *
 * 这是一个自由软件；您可以在自由软件基金会发布的GNU Lesser General Public
 * License version 2.1条款下重新分发和/或修改它。请参阅COPYING文件。
 *
 */

/**
 * @file librbd.h
 * @brief RBD (RADOS Block Device) 库的公共 API 接口头文件
 *
 * 本文件定义了 RBD 库的 C 语言接口，包括：
 * - 数据类型定义：镜像、完成回调、选项等
 * - 常量定义：版本、标志位、特性支持等
 * - 函数声明：镜像管理、I/O 操作、快照管理等
 *
 * RBD 提供了对 Ceph 集群中块设备镜像的完整访问接口，支持：
 * - 镜像的创建、删除、重命名、调整大小
 * - 同步和异步 I/O 操作（读、写、比较写入、写零等）
 * - 快照的创建、删除、回滚、保护管理
 * - 镜像克隆和复制操作
 * - 镜像镜像（镜像）功能
 * - 镜像元数据和统计信息获取
 * - 加密和压缩支持
 *
 * 所有函数都遵循 C 语言调用约定，可被 C 和 C++ 程序使用。
 */

#ifndef CEPH_LIBRBD_H
#define CEPH_LIBRBD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <netinet/in.h>
#if defined(__linux__)
#include <linux/types.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#endif
#include <stdbool.h>
#include <string.h>
#include <sys/uio.h>
#include "../rados/librados.h"
#include "features.h"

/// @brief RBD 库主版本号
#define LIBRBD_VER_MAJOR 1
/// @brief RBD 库次版本号
#define LIBRBD_VER_MINOR 19
/// @brief RBD 库额外版本号
#define LIBRBD_VER_EXTRA 0

/// @brief 构造 RBD 库版本号宏
/// @param maj 主版本号
/// @param min 次版本号
/// @param extra 额外版本号
#define LIBRBD_VERSION(maj, min, extra) ((maj << 16) + (min << 8) + extra)

/// @brief RBD 库当前版本代码
#define LIBRBD_VERSION_CODE LIBRBD_VERSION(LIBRBD_VER_MAJOR, LIBRBD_VER_MINOR, LIBRBD_VER_EXTRA)

// RBD 库功能支持标志（值为 1 表示支持，0 表示不支持）
/// @brief 支持异步刷新操作
#define LIBRBD_SUPPORTS_AIO_FLUSH 1
/// @brief 支持异步打开镜像
#define LIBRBD_SUPPORTS_AIO_OPEN 1
/// @brief 支持比较和写入操作
#define LIBRBD_SUPPORTS_COMPARE_AND_WRITE 1
/// @brief 支持比较和写入 iovec 操作
#define LIBRBD_SUPPORTS_COMPARE_AND_WRITE_IOVEC 1
/// @brief 支持锁定操作
#define LIBRBD_SUPPORTS_LOCKING 1
/// @brief 支持失效操作
#define LIBRBD_SUPPORTS_INVALIDATE 1
/// @brief 支持 iovec I/O 操作
#define LIBRBD_SUPPORTS_IOVEC 1
/// @brief 支持监视操作（当前不支持）
#define LIBRBD_SUPPORTS_WATCH 0
/// @brief 支持写相同数据操作
#define LIBRBD_SUPPORTS_WRITESAME 1
/// @brief 支持写零操作
#define LIBRBD_SUPPORTS_WRITE_ZEROES 1
/// @brief 支持加密功能
#define LIBRBD_SUPPORTS_ENCRYPTION 1
/// @brief 支持加密加载 v2 功能
#define LIBRBD_SUPPORTS_ENCRYPTION_LOAD2 1

#if __GNUC__ >= 4
  #define CEPH_RBD_API          __attribute__ ((visibility ("default")))
  #define CEPH_RBD_DEPRECATED   __attribute__((deprecated))
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#else
  #define CEPH_RBD_API
  #define CEPH_RBD_DEPRECATED
#endif

// RBD 标志位定义
/// @brief 对象映射无效标志位
#define RBD_FLAG_OBJECT_MAP_INVALID   (1<<0)
/// @brief 快速差异无效标志位
#define RBD_FLAG_FAST_DIFF_INVALID    (1<<1)

/// @brief 本地镜像 UUID（用于镜像镜像状态）
#define RBD_MIRROR_IMAGE_STATUS_LOCAL_MIRROR_UUID ""

// RBD 核心数据类型定义
/// @brief RBD 镜像对象句柄
typedef void *rbd_image_t;
/// @brief RBD 镜像选项对象句柄
typedef void *rbd_image_options_t;
/// @brief RBD 池统计对象句柄
typedef void *rbd_pool_stats_t;

/// @brief RBD 完成回调对象句柄
typedef void *rbd_completion_t;
/// @brief RBD 异步操作完成回调函数类型
/// @param cb 完成回调对象
/// @param arg 用户自定义参数
typedef void (*rbd_callback_t)(rbd_completion_t cb, void *arg);

/// @brief RBD 进度回调函数类型
/// @param offset 当前进度偏移量
/// @param total 总大小
/// @param ptr 用户自定义参数
/// @return 0 继续操作，负值表示错误并中止操作
typedef int (*librbd_progress_fn_t)(uint64_t offset, uint64_t total, void *ptr);

/// @brief RBD 更新通知回调函数类型
/// @param arg 用户自定义参数
typedef void (*rbd_update_callback_t)(void *arg);

/// @brief RBD 快照命名空间类型枚举
typedef enum {
  /// @brief 用户快照命名空间（默认）
  RBD_SNAP_NAMESPACE_TYPE_USER   = 0,
  /// @brief 分组快照命名空间
  RBD_SNAP_NAMESPACE_TYPE_GROUP  = 1,
  /// @brief 垃圾回收箱快照命名空间
  RBD_SNAP_NAMESPACE_TYPE_TRASH  = 2,
  /// @brief 镜像快照命名空间
  RBD_SNAP_NAMESPACE_TYPE_MIRROR = 3,
} rbd_snap_namespace_type_t;

/// @brief RBD 镜像规格信息结构
/// @details 包含镜像的 ID 和名称信息
typedef struct {
  char *id;    ///< 镜像 ID
  char *name;  ///< 镜像名称
} rbd_image_spec_t;

/// @brief RBD 链接镜像规格信息结构
/// @details 包含完整池和镜像路径信息的镜像规格
typedef struct {
  int64_t pool_id;        ///< 池 ID
  char *pool_name;        ///< 池名称
  char *pool_namespace;   ///< 池命名空间
  char *image_id;         ///< 镜像 ID
  char *image_name;       ///< 镜像名称
  bool trash;             ///< 是否在垃圾回收箱中
} rbd_linked_image_spec_t;

/// @brief RBD 快照规格信息结构
/// @details 包含快照的 ID、命名空间类型和名称信息
typedef struct {
  uint64_t id;                           ///< 快照 ID
  rbd_snap_namespace_type_t namespace_type; ///< 快照命名空间类型
  char *name;                            ///< 快照名称
} rbd_snap_spec_t;

/// @brief RBD 快照信息结构
/// @details 包含快照的基本信息
typedef struct {
  uint64_t id;           ///< 快照 ID
  uint64_t size;         ///< 快照大小
  const char *name;      ///< 快照名称
} rbd_snap_info_t;

/// @brief RBD 子镜像信息结构
/// @details 包含子镜像（克隆镜像）的父镜像信息
typedef struct {
  const char *pool_name;   ///< 父镜像池名称
  const char *image_name;  ///< 父镜像名称
  const char *image_id;    ///< 父镜像 ID
  bool trash;              ///< 父镜像是否在垃圾回收箱中
} rbd_child_info_t;

/// @brief 镜像名称最大长度（包括空字符）
#define RBD_MAX_IMAGE_NAME_SIZE 96
/// @brief 块名称前缀最大长度（已废弃）
#define RBD_MAX_BLOCK_NAME_SIZE 24

// 快照创建标志位定义
/// @brief 跳过镜像静默阶段标志
#define RBD_SNAP_CREATE_SKIP_QUIESCE		(1 << 0)
/// @brief 忽略静默错误标志
#define RBD_SNAP_CREATE_IGNORE_QUIESCE_ERROR	(1 << 1)

// 快照删除标志位定义
/// @brief 取消保护快照标志
#define RBD_SNAP_REMOVE_UNPROTECT	(1 << 0)
/// @brief 扁平化父镜像标志
#define RBD_SNAP_REMOVE_FLATTEN		(1 << 1)
/// @brief 强制删除标志（取消保护并扁平化）
#define RBD_SNAP_REMOVE_FORCE		(RBD_SNAP_REMOVE_UNPROTECT | RBD_SNAP_REMOVE_FLATTEN)

/**
 * 事件通知套接字类型枚举（用于 set_image_notification）
 * @details 这些类型用于指示传递的事件套接字类型
 */
enum {
  /// @brief 管道类型事件套接字
  EVENT_TYPE_PIPE = 1,
  /// @brief 事件文件描述符类型套接字
  EVENT_TYPE_EVENTFD = 2
};

/// @brief RBD 镜像信息结构
/// @details 包含镜像的基本信息和布局参数
typedef struct {
  uint64_t size;                                    ///< 镜像大小（字节）
  uint64_t obj_size;                               ///< 对象大小（字节）
  uint64_t num_objs;                               ///< 对象数量
  int order;                                       ///< 对象大小阶数（2^order = obj_size）
  char block_name_prefix[RBD_MAX_BLOCK_NAME_SIZE]; /* 已废弃 */  ///< 块名称前缀（已废弃）
  int64_t parent_pool;                             /* 已废弃 */  ///< 父镜像池 ID（已废弃）
  char parent_name[RBD_MAX_IMAGE_NAME_SIZE];       /* 已废弃 */  ///< 父镜像名称（已废弃）
} rbd_image_info_t;

/// @brief RBD 镜像镜像模式枚举
typedef enum {
  /// @brief 镜像镜像已禁用
  RBD_MIRROR_MODE_DISABLED, /* mirroring is disabled */
  /// @brief 按镜像启用镜像镜像
  RBD_MIRROR_MODE_IMAGE,    /* mirroring enabled on a per-image basis */
  /// @brief 对所有带日志的镜像启用镜像镜像
  RBD_MIRROR_MODE_POOL      /* mirroring enabled on all journaled images */
} rbd_mirror_mode_t;

/// @brief RBD 镜像镜像对等体方向枚举
typedef enum {
  /// @brief 只接收方向
  RBD_MIRROR_PEER_DIRECTION_RX    = 0,
  /// @brief 只发送方向
  RBD_MIRROR_PEER_DIRECTION_TX    = 1,
  /// @brief 接收和发送双向
  RBD_MIRROR_PEER_DIRECTION_RX_TX = 2
} rbd_mirror_peer_direction_t;

/// @brief RBD 镜像镜像对等体结构（已废弃）
typedef struct {
  char *uuid;          ///< 对等体 UUID
  char *cluster_name;  ///< 集群名称
  char *client_name;   ///< 客户端名称
} rbd_mirror_peer_t CEPH_RBD_DEPRECATED;

/// @brief RBD 镜像镜像对等体站点结构
typedef struct {
  char *uuid;                          ///< 对等体站点 UUID
  rbd_mirror_peer_direction_t direction; ///< 镜像方向
  char *site_name;                     ///< 站点名称
  char *mirror_uuid;                   ///< 镜像 UUID
  char *client_name;                   ///< 客户端名称
  time_t last_seen;                    ///< 最后见到时间
} rbd_mirror_peer_site_t;

// RBD 镜像镜像对等体属性名称常量
/// @brief 监控主机属性名称
#define RBD_MIRROR_PEER_ATTRIBUTE_NAME_MON_HOST "mon_host"
/// @brief 密钥属性名称
#define RBD_MIRROR_PEER_ATTRIBUTE_NAME_KEY      "key"

/// @brief RBD 镜像镜像模式枚举
typedef enum {
  /// @brief 日志模式镜像
  RBD_MIRROR_IMAGE_MODE_JOURNAL  = 0,
  /// @brief 快照模式镜像
  RBD_MIRROR_IMAGE_MODE_SNAPSHOT = 1,
} rbd_mirror_image_mode_t;

typedef enum {
  RBD_MIRROR_IMAGE_DISABLING = 0,
  RBD_MIRROR_IMAGE_ENABLED = 1,
  RBD_MIRROR_IMAGE_DISABLED = 2
} rbd_mirror_image_state_t;

typedef struct {
  char *global_id;
  rbd_mirror_image_state_t state;
  bool primary;
} rbd_mirror_image_info_t;

typedef enum {
  MIRROR_IMAGE_STATUS_STATE_UNKNOWN         = 0,
  MIRROR_IMAGE_STATUS_STATE_ERROR           = 1,
  MIRROR_IMAGE_STATUS_STATE_SYNCING         = 2,
  MIRROR_IMAGE_STATUS_STATE_STARTING_REPLAY = 3,
  MIRROR_IMAGE_STATUS_STATE_REPLAYING       = 4,
  MIRROR_IMAGE_STATUS_STATE_STOPPING_REPLAY = 5,
  MIRROR_IMAGE_STATUS_STATE_STOPPED         = 6,
} rbd_mirror_image_status_state_t;

typedef struct {
  char *name;
  rbd_mirror_image_info_t info;
  rbd_mirror_image_status_state_t state;
  char *description;
  time_t last_update;
  bool up;
} rbd_mirror_image_status_t CEPH_RBD_DEPRECATED;

typedef struct {
  char *mirror_uuid;
  rbd_mirror_image_status_state_t state;
  char *description;
  time_t last_update;
  bool up;
} rbd_mirror_image_site_status_t;

typedef struct {
  char *name;
  rbd_mirror_image_info_t info;
  uint32_t site_statuses_count;
  rbd_mirror_image_site_status_t *site_statuses;
} rbd_mirror_image_global_status_t;

typedef enum {
  RBD_GROUP_IMAGE_STATE_ATTACHED,
  RBD_GROUP_IMAGE_STATE_INCOMPLETE
} rbd_group_image_state_t;

typedef struct {
  char *name;
  int64_t pool;
  rbd_group_image_state_t state;
} rbd_group_image_info_t;

typedef struct {
  char *name;
  int64_t pool;
} rbd_group_info_t;

typedef enum {
  RBD_GROUP_SNAP_STATE_INCOMPLETE,
  RBD_GROUP_SNAP_STATE_COMPLETE
} rbd_group_snap_state_t;

typedef struct {
  char *name;
  rbd_group_snap_state_t state;
} rbd_group_snap_info_t;

typedef struct {
  int64_t group_pool;
  char *group_name;
  char *group_snap_name;
} rbd_snap_group_namespace_t;

typedef struct {
  rbd_snap_namespace_type_t original_namespace_type;
  char *original_name;
} rbd_snap_trash_namespace_t;

typedef enum {
  RBD_SNAP_MIRROR_STATE_PRIMARY,
  RBD_SNAP_MIRROR_STATE_PRIMARY_DEMOTED,
  RBD_SNAP_MIRROR_STATE_NON_PRIMARY,
  RBD_SNAP_MIRROR_STATE_NON_PRIMARY_DEMOTED
} rbd_snap_mirror_state_t;

typedef struct {
  rbd_snap_mirror_state_t state;
  size_t mirror_peer_uuids_count;
  char *mirror_peer_uuids;
  bool complete;
  char *primary_mirror_uuid;
  uint64_t primary_snap_id;
  uint64_t last_copied_object_number;
} rbd_snap_mirror_namespace_t;

typedef enum {
  RBD_LOCK_MODE_EXCLUSIVE = 0,
  RBD_LOCK_MODE_SHARED = 1,
} rbd_lock_mode_t;

CEPH_RBD_API void rbd_version(int *major, int *minor, int *extra);

/* image options */
enum {
  RBD_IMAGE_OPTION_FORMAT = 0,
  RBD_IMAGE_OPTION_FEATURES = 1,
  RBD_IMAGE_OPTION_ORDER = 2,
  RBD_IMAGE_OPTION_STRIPE_UNIT = 3,
  RBD_IMAGE_OPTION_STRIPE_COUNT = 4,
  RBD_IMAGE_OPTION_JOURNAL_ORDER = 5,
  RBD_IMAGE_OPTION_JOURNAL_SPLAY_WIDTH = 6,
  RBD_IMAGE_OPTION_JOURNAL_POOL = 7,
  RBD_IMAGE_OPTION_FEATURES_SET = 8,
  RBD_IMAGE_OPTION_FEATURES_CLEAR = 9,
  RBD_IMAGE_OPTION_DATA_POOL = 10,
  RBD_IMAGE_OPTION_FLATTEN = 11,
  RBD_IMAGE_OPTION_CLONE_FORMAT = 12,
  /// @brief 镜像镜像模式选项
  RBD_IMAGE_OPTION_MIRROR_IMAGE_MODE = 13,
};

/// @brief 垃圾回收箱镜像来源类型枚举
typedef enum {
  /// @brief 用户删除
  RBD_TRASH_IMAGE_SOURCE_USER = 0,
  /// @brief 镜像镜像删除
  RBD_TRASH_IMAGE_SOURCE_MIRRORING = 1,
  /// @brief 迁移删除
  RBD_TRASH_IMAGE_SOURCE_MIGRATION = 2,
  /// @brief 移除操作删除
  RBD_TRASH_IMAGE_SOURCE_REMOVING = 3,
  /// @brief 用户父镜像删除
  RBD_TRASH_IMAGE_SOURCE_USER_PARENT = 4,
} rbd_trash_image_source_t;

/// @brief 垃圾回收箱镜像信息结构
typedef struct {
  char *id;                              ///< 镜像 ID
  char *name;                            ///< 镜像名称
  rbd_trash_image_source_t source;       ///< 删除来源类型
  time_t deletion_time;                  ///< 删除时间
  time_t deferment_end_time;             ///< 延期结束时间
} rbd_trash_image_info_t;

/// @brief 镜像监视器信息结构
typedef struct {
  char *addr;           ///< 监视器地址
  int64_t id;           ///< 监视器 ID
  uint64_t cookie;      ///< 监视器 cookie
} rbd_image_watcher_t;

/// @brief 镜像迁移状态枚举
typedef enum {
  /// @brief 未知状态
  RBD_IMAGE_MIGRATION_STATE_UNKNOWN = -1,
  /// @brief 错误状态
  RBD_IMAGE_MIGRATION_STATE_ERROR = 0,
  /// @brief 准备中状态
  RBD_IMAGE_MIGRATION_STATE_PREPARING = 1,
  /// @brief 已准备状态
  RBD_IMAGE_MIGRATION_STATE_PREPARED = 2,
  /// @brief 执行中状态
  RBD_IMAGE_MIGRATION_STATE_EXECUTING = 3,
  /// @brief 已执行状态
  RBD_IMAGE_MIGRATION_STATE_EXECUTED = 4,
  /// @brief 中止中状态
  RBD_IMAGE_MIGRATION_STATE_ABORTING = 5,
} rbd_image_migration_state_t;

/// @brief 镜像迁移状态结构
typedef struct {
  int64_t source_pool_id;              ///< 源池 ID
  char *source_pool_namespace;         ///< 源池命名空间
  char *source_image_name;             ///< 源镜像名称
  char *source_image_id;               ///< 源镜像 ID
  int64_t dest_pool_id;                ///< 目标池 ID
  char *dest_pool_namespace;           ///< 目标池命名空间
  char *dest_image_name;               ///< 目标镜像名称
  char *dest_image_id;                 ///< 目标镜像 ID
  rbd_image_migration_state_t state;   ///< 迁移状态
  char *state_description;             ///< 状态描述
} rbd_image_migration_status_t;

/// @brief 配置源类型枚举
typedef enum {
  /// @brief 来自配置文件
  RBD_CONFIG_SOURCE_CONFIG = 0,
  /// @brief 来自池配置
  RBD_CONFIG_SOURCE_POOL = 1,
  /// @brief 来自镜像配置
  RBD_CONFIG_SOURCE_IMAGE = 2,
} rbd_config_source_t;

/// @brief 配置选项结构
typedef struct {
  char *name;                   ///< 配置选项名称
  char *value;                  ///< 配置选项值
  rbd_config_source_t source;   ///< 配置来源
} rbd_config_option_t;

/// @brief 池统计选项枚举
typedef enum {
  /// @brief 镜像数量统计
  RBD_POOL_STAT_OPTION_IMAGES,
  /// @brief 镜像已分配字节数统计
  RBD_POOL_STAT_OPTION_IMAGE_PROVISIONED_BYTES,
  /// @brief 镜像最大分配字节数统计
  RBD_POOL_STAT_OPTION_IMAGE_MAX_PROVISIONED_BYTES,
  /// @brief 镜像快照数量统计
  RBD_POOL_STAT_OPTION_IMAGE_SNAPSHOTS,
  /// @brief 垃圾回收箱镜像数量统计
  RBD_POOL_STAT_OPTION_TRASH_IMAGES,
  /// @brief 垃圾回收箱镜像已分配字节数统计
  RBD_POOL_STAT_OPTION_TRASH_PROVISIONED_BYTES,
  /// @brief 垃圾回收箱镜像最大分配字节数统计
  RBD_POOL_STAT_OPTION_TRASH_MAX_PROVISIONED_BYTES,
  /// @brief 垃圾回收箱快照数量统计
  RBD_POOL_STAT_OPTION_TRASH_SNAPSHOTS
} rbd_pool_stat_option_t;

/// @brief 写零操作标志枚举（用于 rbd_write_zeroes / rbd_aio_write_zeroes）
enum {
  /// @brief 厚置备标志 - 完全分配零化范围
  RBD_WRITE_ZEROES_FLAG_THICK_PROVISION = (1U<<0), /* fully allocated zeroed extent */
};

/// @brief 加密格式类型枚举
typedef enum {
  /// @brief LUKS1 加密格式
  RBD_ENCRYPTION_FORMAT_LUKS1 = 0,
  /// @brief LUKS2 加密格式
  RBD_ENCRYPTION_FORMAT_LUKS2 = 1,
  /// @brief 通用 LUKS 加密格式
  RBD_ENCRYPTION_FORMAT_LUKS  = 2
} rbd_encryption_format_t;

/// @brief 加密算法类型枚举
typedef enum {
  /// @brief AES128 加密算法
  RBD_ENCRYPTION_ALGORITHM_AES128 = 0,
  /// @brief AES256 加密算法
  RBD_ENCRYPTION_ALGORITHM_AES256 = 1
} rbd_encryption_algorithm_t;

/// @brief 加密选项句柄类型
typedef void *rbd_encryption_options_t;

/// @brief 加密规格结构
typedef struct {
  rbd_encryption_format_t format;    ///< 加密格式
  rbd_encryption_options_t opts;     ///< 加密选项
  size_t opts_size;                  ///< 选项大小
} rbd_encryption_spec_t;

/// @brief LUKS1 加密格式选项结构
typedef struct {
  rbd_encryption_algorithm_t alg;    ///< 加密算法
  const char* passphrase;            ///< 密码
  size_t passphrase_size;            ///< 密码大小
} rbd_encryption_luks1_format_options_t;

/// @brief LUKS2 加密格式选项结构
typedef struct {
  rbd_encryption_algorithm_t alg;    ///< 加密算法
  const char* passphrase;            ///< 密码
  size_t passphrase_size;            ///< 密码大小
} rbd_encryption_luks2_format_options_t;

/// @brief LUKS 加密格式选项结构（通用）
typedef struct {
  const char* passphrase;     ///< 密码
  size_t passphrase_size;     ///< 密码大小
} rbd_encryption_luks_format_options_t;

/// @brief 创建镜像选项对象
/// @param opts 镜像选项对象指针
CEPH_RBD_API void rbd_image_options_create(rbd_image_options_t* opts);

/// @brief 销毁镜像选项对象
/// @param opts 镜像选项对象
CEPH_RBD_API void rbd_image_options_destroy(rbd_image_options_t opts);

/// @brief 设置镜像选项的字符串值
/// @param opts 镜像选项对象
/// @param optname 选项名称（见 RBD_IMAGE_OPTION_* 常量）
/// @param optval 选项字符串值
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_image_options_set_string(rbd_image_options_t opts,
					      int optname, const char* optval);

/// @brief 设置镜像选项的无符号 64 位整数值
/// @param opts 镜像选项对象
/// @param optname 选项名称（见 RBD_IMAGE_OPTION_* 常量）
/// @param optval 选项无符号 64 位整数值
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_image_options_set_uint64(rbd_image_options_t opts,
					      int optname, uint64_t optval);

/// @brief 获取镜像选项的字符串值
/// @param opts 镜像选项对象
/// @param optname 选项名称（见 RBD_IMAGE_OPTION_* 常量）
/// @param optval 存储选项值的缓冲区
/// @param maxlen 缓冲区最大长度
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_image_options_get_string(rbd_image_options_t opts,
					      int optname, char* optval,
					      size_t maxlen);

/// @brief 获取镜像选项的无符号 64 位整数值
/// @param opts 镜像选项对象
/// @param optname 选项名称（见 RBD_IMAGE_OPTION_* 常量）
/// @param optval 存储选项值的指针
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_image_options_get_uint64(rbd_image_options_t opts,
					      int optname, uint64_t* optval);

/// @brief 检查镜像选项是否已设置
/// @param opts 镜像选项对象
/// @param optname 选项名称（见 RBD_IMAGE_OPTION_* 常量）
/// @param is_set [输出] 选项是否已设置
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_image_options_is_set(rbd_image_options_t opts,
                                          int optname, bool* is_set);

/// @brief 取消设置镜像选项
/// @param opts 镜像选项对象
/// @param optname 选项名称（见 RBD_IMAGE_OPTION_* 常量）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_image_options_unset(rbd_image_options_t opts, int optname);

/// @brief 清空所有镜像选项
/// @param opts 镜像选项对象
CEPH_RBD_API void rbd_image_options_clear(rbd_image_options_t opts);

/// @brief 检查镜像选项是否为空
/// @param opts 镜像选项对象
/// @return 选项为空返回 1，否则返回 0
CEPH_RBD_API int rbd_image_options_is_empty(rbd_image_options_t opts);

/// @brief 辅助函数集合
/* helpers */
/// @brief 清理镜像规格对象（释放内存）
/// @param image 镜像规格对象指针
CEPH_RBD_API void rbd_image_spec_cleanup(rbd_image_spec_t *image);

/// @brief 清理镜像规格列表（释放内存）
/// @param images 镜像规格对象数组
/// @param num_images 数组大小
CEPH_RBD_API void rbd_image_spec_list_cleanup(rbd_image_spec_t *images,
                                              size_t num_images);

/// @brief 清理链接镜像规格对象（释放内存）
/// @param image 链接镜像规格对象指针
CEPH_RBD_API void rbd_linked_image_spec_cleanup(rbd_linked_image_spec_t *image);

/// @brief 清理链接镜像规格列表（释放内存）
/// @param images 链接镜像规格对象数组
/// @param num_images 数组大小
CEPH_RBD_API void rbd_linked_image_spec_list_cleanup(
    rbd_linked_image_spec_t *images, size_t num_images);

/// @brief 清理快照规格对象（释放内存）
/// @param snap 快照规格对象指针
CEPH_RBD_API void rbd_snap_spec_cleanup(rbd_snap_spec_t *snap);

/// @brief 镜像管理函数集合
/* images */
/// @brief 列出池中的镜像（已废弃）
/// @param io I/O 上下文
/// @param names 镜像名称缓冲区
/// @param size 缓冲区大小（输入时），实际需要的缓冲区大小（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_list(rados_ioctx_t io, char *names, size_t *size)
  CEPH_RBD_DEPRECATED;

/// @brief 列出池中的镜像（新版本）
/// @param io I/O 上下文
/// @param images 镜像规格数组缓冲区
/// @param max_images 缓冲区大小（输入时），实际镜像数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_list2(rados_ioctx_t io, rbd_image_spec_t* images,
                           size_t *max_images);

/// @brief 创建 RBD 镜像（基本版本）
/// @param io I/O 上下文
/// @param name 镜像名称
/// @param size 镜像大小（字节）
/// @param order [输出] 对象大小阶数（2^order = 对象大小）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_create(rados_ioctx_t io, const char *name, uint64_t size,
                            int *order);

/// @brief 创建 RBD 镜像（带特性版本）
/// @param io I/O 上下文
/// @param name 镜像名称
/// @param size 镜像大小（字节）
/// @param features 初始功能特性位
/// @param order [输出] 对象大小阶数（2^order = 对象大小）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_create2(rados_ioctx_t io, const char *name, uint64_t size,
		             uint64_t features, int *order);

/**
 * @brief 创建新的 RBD 镜像（完整版本）
 *
 * 条带单元必须是对象大小的因子（1 << order）。
 * 条带计数可以是 1（无对象内条带化）或大于 1。
 * 如果条带单元 != 对象大小且条带计数 != 1，则必须指定 RBD_FEATURE_STRIPINGV2。
 *
 * @param io I/O 上下文
 * @param name 镜像名称
 * @param size 镜像大小（字节）
 * @param features 初始功能特性位
 * @param order [输出] 对象大小阶数（2^order = 对象大小）
 * @param stripe_unit 条带单元大小（字节）
 * @param stripe_count 循环前跨越的对象数量
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_create3(rados_ioctx_t io, const char *name, uint64_t size,
		             uint64_t features, int *order,
		             uint64_t stripe_unit, uint64_t stripe_count);

/// @brief 创建 RBD 镜像（使用选项对象版本）
/// @param io I/O 上下文
/// @param name 镜像名称
/// @param size 镜像大小（字节）
/// @param opts 镜像选项对象
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_create4(rados_ioctx_t io, const char *name, uint64_t size,
			     rbd_image_options_t opts);
/// @brief 从快照克隆镜像（基本版本）
/// @param p_ioctx 父镜像的 I/O 上下文
/// @param p_name 父镜像名称
/// @param p_snapname 父镜像快照名称
/// @param c_ioctx 子镜像的 I/O 上下文
/// @param c_name 子镜像名称
/// @param features 子镜像功能特性
/// @param c_order [输出] 子镜像对象大小阶数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_clone(rados_ioctx_t p_ioctx, const char *p_name,
	                   const char *p_snapname, rados_ioctx_t c_ioctx,
	                   const char *c_name, uint64_t features, int *c_order);

/// @brief 从快照克隆镜像（带条带化版本）
/// @param p_ioctx 父镜像的 I/O 上下文
/// @param p_name 父镜像名称
/// @param p_snapname 父镜像快照名称
/// @param c_ioctx 子镜像的 I/O 上下文
/// @param c_name 子镜像名称
/// @param features 子镜像功能特性
/// @param c_order [输出] 子镜像对象大小阶数
/// @param stripe_unit 条带单元大小（字节）
/// @param stripe_count 条带计数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_clone2(rados_ioctx_t p_ioctx, const char *p_name,
	                    const char *p_snapname, rados_ioctx_t c_ioctx,
	                    const char *c_name, uint64_t features, int *c_order,
	                    uint64_t stripe_unit, int stripe_count);

/// @brief 从快照克隆镜像（使用选项对象版本）
/// @param p_ioctx 父镜像的 I/O 上下文
/// @param p_name 父镜像名称
/// @param p_snapname 父镜像快照名称
/// @param c_ioctx 子镜像的 I/O 上下文
/// @param c_name 子镜像名称
/// @param c_opts 子镜像选项对象
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_clone3(rados_ioctx_t p_ioctx, const char *p_name,
	                    const char *p_snapname, rados_ioctx_t c_ioctx,
	                    const char *c_name, rbd_image_options_t c_opts);

/// @brief 从快照 ID 克隆镜像（使用选项对象版本）
/// @param p_ioctx 父镜像的 I/O 上下文
/// @param p_name 父镜像名称
/// @param p_snap_id 父镜像快照 ID
/// @param c_ioctx 子镜像的 I/O 上下文
/// @param c_name 子镜像名称
/// @param c_opts 子镜像选项对象
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_clone4(rados_ioctx_t p_ioctx, const char *p_name,
                            uint64_t p_snap_id, rados_ioctx_t c_ioctx,
                            const char *c_name, rbd_image_options_t c_opts);

/// @brief 删除镜像
/// @param io I/O 上下文
/// @param name 镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_remove(rados_ioctx_t io, const char *name);

/// @brief 删除镜像（带进度回调）
/// @param io I/O 上下文
/// @param name 镜像名称
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_remove_with_progress(rados_ioctx_t io, const char *name,
			                  librbd_progress_fn_t cb,
                                          void *cbdata);

/// @brief 重命名镜像
/// @param src_io_ctx 源 I/O 上下文
/// @param srcname 源镜像名称
/// @param destname 目标镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_rename(rados_ioctx_t src_io_ctx, const char *srcname,
                            const char *destname);

/// @brief 将镜像移动到垃圾回收箱
/// @param io I/O 上下文
/// @param name 镜像名称
/// @param delay 延期删除时间（秒）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_trash_move(rados_ioctx_t io, const char *name,
                                uint64_t delay);

/// @brief 获取垃圾回收箱中的镜像信息
/// @param io I/O 上下文
/// @param id 镜像 ID
/// @param info [输出] 镜像信息结构
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_trash_get(rados_ioctx_t io, const char *id,
                               rbd_trash_image_info_t *info);

/// @brief 清理垃圾回收箱镜像信息（释放内存）
/// @param info 镜像信息结构指针
CEPH_RBD_API void rbd_trash_get_cleanup(rbd_trash_image_info_t *info);

/// @brief 列出垃圾回收箱中的镜像
/// @param io I/O 上下文
/// @param trash_entries [输出] 镜像信息数组缓冲区
/// @param num_entries [输入/输出] 缓冲区大小 / 实际镜像数量
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_trash_list(rados_ioctx_t io,
                                rbd_trash_image_info_t *trash_entries,
                                size_t *num_entries);

/// @brief 清理垃圾回收箱镜像列表（释放内存）
/// @param trash_entries 镜像信息数组
/// @param num_entries 数组大小
CEPH_RBD_API void rbd_trash_list_cleanup(rbd_trash_image_info_t *trash_entries,
                                         size_t num_entries);

/// @brief 清理过期的垃圾回收箱镜像
/// @param io I/O 上下文
/// @param expire_ts 过期时间戳（早于此时间的镜像将被清理）
/// @param threshold 清理阈值（0.0-1.0，表示清理比例）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_trash_purge(rados_ioctx_t io, time_t expire_ts, float threshold);

/// @brief 清理过期的垃圾回收箱镜像（带进度回调）
/// @param io I/O 上下文
/// @param expire_ts 过期时间戳
/// @param threshold 清理阈值
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_trash_purge_with_progress(rados_ioctx_t io, time_t expire_ts,
                                               float threshold, librbd_progress_fn_t cb,
                                               void* cbdata);

/// @brief 从垃圾回收箱中移除镜像
/// @param io I/O 上下文
/// @param id 镜像 ID
/// @param force 是否强制删除（忽略延期时间）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_trash_remove(rados_ioctx_t io, const char *id, bool force);

/// @brief 从垃圾回收箱中移除镜像（带进度回调）
/// @param io I/O 上下文
/// @param id 镜像 ID
/// @param force 是否强制删除
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_trash_remove_with_progress(rados_ioctx_t io,
                                                const char *id,
                                                bool force,
                                                librbd_progress_fn_t cb,
                                                void *cbdata);

/// @brief 从垃圾回收箱中恢复镜像
/// @param io I/O 上下文
/// @param id 镜像 ID
/// @param name 恢复后的镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_trash_restore(rados_ioctx_t io, const char *id,
                                   const char *name);

/// @brief 镜像迁移函数集合
/* migration */
/// @brief 准备镜像迁移（导出模式）
/// @param ioctx 源 I/O 上下文
/// @param image_name 源镜像名称
/// @param dest_ioctx 目标 I/O 上下文
/// @param dest_image_name 目标镜像名称
/// @param opts 镜像选项
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_prepare(rados_ioctx_t ioctx,
                                       const char *image_name,
                                       rados_ioctx_t dest_ioctx,
                                       const char *dest_image_name,
                                       rbd_image_options_t opts);

/// @brief 准备镜像迁移（导入模式）
/// @param source_spec 源镜像规格字符串
/// @param dest_ioctx 目标 I/O 上下文
/// @param dest_image_name 目标镜像名称
/// @param opts 镜像选项
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_prepare_import(
    const char *source_spec, rados_ioctx_t dest_ioctx,
    const char *dest_image_name, rbd_image_options_t opts);

/// @brief 执行镜像迁移
/// @param ioctx I/O 上下文
/// @param image_name 镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_execute(rados_ioctx_t ioctx,
                                       const char *image_name);

/// @brief 执行镜像迁移（带进度回调）
/// @param ioctx I/O 上下文
/// @param image_name 镜像名称
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_execute_with_progress(rados_ioctx_t ioctx,
                                                     const char *image_name,
                                                     librbd_progress_fn_t cb,
                                                     void *cbdata);

/// @brief 中止镜像迁移
/// @param ioctx I/O 上下文
/// @param image_name 镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_abort(rados_ioctx_t ioctx,
                                     const char *image_name);

/// @brief 中止镜像迁移（带进度回调）
/// @param ioctx I/O 上下文
/// @param image_name 镜像名称
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_abort_with_progress(rados_ioctx_t ioctx,
                                                   const char *image_name,
                                                   librbd_progress_fn_t cb,
                                                   void *cbdata);

/// @brief 提交镜像迁移
/// @param ioctx I/O 上下文
/// @param image_name 镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_commit(rados_ioctx_t ioctx,
                                      const char *image_name);

/// @brief 提交镜像迁移（带进度回调）
/// @param ioctx I/O 上下文
/// @param image_name 镜像名称
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_commit_with_progress(rados_ioctx_t ioctx,
                                                    const char *image_name,
                                                    librbd_progress_fn_t cb,
                                                    void *cbdata);

/// @brief 获取镜像迁移状态
/// @param ioctx I/O 上下文
/// @param image_name 镜像名称
/// @param status [输出] 迁移状态结构
/// @param status_size 状态结构大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_migration_status(rados_ioctx_t ioctx,
                                      const char *image_name,
                                      rbd_image_migration_status_t *status,
                                      size_t status_size);

/// @brief 清理镜像迁移状态（释放内存）
/// @param status 迁移状态结构指针
CEPH_RBD_API void rbd_migration_status_cleanup(
    rbd_image_migration_status_t *status);

/// @brief 池镜像功能集合
/* pool mirroring */
/// @brief 获取镜像站点名称
/// @param cluster RADOS 集群句柄
/// @param name 存储站点名称的缓冲区
/// @param max_len 缓冲区大小（输入时），实际需要的缓冲区大小（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_site_name_get(rados_t cluster,
                                          char *name, size_t *max_len);

/// @brief 设置镜像站点名称
/// @param cluster RADOS 集群句柄
/// @param name 站点名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_site_name_set(rados_t cluster,
                                          const char *name);

/// @brief 获取镜像模式
/// @param io_ctx I/O 上下文
/// @param mirror_mode [输出] 镜像模式
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_mode_get(rados_ioctx_t io_ctx,
                                     rbd_mirror_mode_t *mirror_mode);

/// @brief 设置镜像模式
/// @param io_ctx I/O 上下文
/// @param mirror_mode 镜像模式
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_mode_set(rados_ioctx_t io_ctx,
                                     rbd_mirror_mode_t mirror_mode);

/// @brief 获取镜像 UUID
/// @param io_ctx I/O 上下文
/// @param uuid 存储镜像 UUID 的缓冲区
/// @param max_len 缓冲区大小（输入时），实际需要的缓冲区大小（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_uuid_get(rados_ioctx_t io_ctx,
                                     char *uuid, size_t *max_len);

/// @brief 创建镜像对等体引导令牌
/// @param io_ctx I/O 上下文
/// @param token 存储令牌的缓冲区
/// @param max_len 缓冲区大小（输入时），实际令牌长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_bootstrap_create(
    rados_ioctx_t io_ctx, char *token, size_t *max_len);

/// @brief 导入镜像对等体引导令牌
/// @param io_ctx I/O 上下文
/// @param direction 镜像方向
/// @param token 引导令牌字符串
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_bootstrap_import(
    rados_ioctx_t io_ctx, rbd_mirror_peer_direction_t direction,
    const char *token);

/// @brief 添加镜像对等体站点
/// @param io_ctx I/O 上下文
/// @param uuid 存储生成的站点 UUID 的缓冲区
/// @param uuid_max_length UUID 缓冲区大小
/// @param direction 镜像方向
/// @param site_name 站点名称
/// @param client_name 客户端名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_site_add(
    rados_ioctx_t io_ctx, char *uuid, size_t uuid_max_length,
    rbd_mirror_peer_direction_t direction, const char *site_name,
    const char *client_name);

/// @brief 设置镜像对等体站点名称
/// @param io_ctx I/O 上下文
/// @param uuid 站点 UUID
/// @param site_name 新站点名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_site_set_name(
    rados_ioctx_t io_ctx, const char *uuid, const char *site_name);

/// @brief 设置镜像对等体站点客户端名称
/// @param io_ctx I/O 上下文
/// @param uuid 站点 UUID
/// @param client_name 新客户端名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_site_set_client_name(
    rados_ioctx_t io_ctx, const char *uuid, const char *client_name);

/// @brief 设置镜像对等体站点方向
/// @param io_ctx I/O 上下文
/// @param uuid 站点 UUID
/// @param direction 新镜像方向
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_site_set_direction(
    rados_ioctx_t io_ctx, const char *uuid,
    rbd_mirror_peer_direction_t direction);

/// @brief 移除镜像对等体站点
/// @param io_ctx I/O 上下文
/// @param uuid 站点 UUID
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_site_remove(
    rados_ioctx_t io_ctx, const char *uuid);

/// @brief 列出镜像对等体站点
/// @param io_ctx I/O 上下文
/// @param peers 对等体站点数组缓冲区
/// @param max_peers 缓冲区大小（输入时），实际站点数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_site_list(
    rados_ioctx_t io_ctx, rbd_mirror_peer_site_t *peers, int *max_peers);

/// @brief 清理镜像对等体站点列表（释放内存）
/// @param peers 对等体站点数组
/// @param max_peers 数组大小
CEPH_RBD_API void rbd_mirror_peer_site_list_cleanup(
    rbd_mirror_peer_site_t *peers, int max_peers);

/// @brief 获取镜像对等体站点属性
/// @param p I/O 上下文
/// @param uuid 站点 UUID
/// @param keys 属性键缓冲区
/// @param max_key_len 键缓冲区大小（输入时），实际键长度（输出时）
/// @param values 属性值缓冲区
/// @param max_value_len 值缓冲区大小（输入时），实际值长度（输出时）
/// @param key_value_count 属性数量（输入时），实际数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_site_get_attributes(
    rados_ioctx_t p, const char *uuid, char *keys, size_t *max_key_len,
    char *values, size_t *max_value_len, size_t *key_value_count);

/// @brief 设置镜像对等体站点属性
/// @param p I/O 上下文
/// @param uuid 站点 UUID
/// @param keys 属性键数组
/// @param values 属性值数组
/// @param key_value_count 属性数量
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_peer_site_set_attributes(
    rados_ioctx_t p, const char *uuid, const char *keys, const char *values,
    size_t key_value_count);

/// @brief 列出镜像的全局镜像状态
/// @param io_ctx I/O 上下文
/// @param start_id 开始镜像 ID（用于分页）
/// @param max 最大返回数量
/// @param image_ids [输出] 镜像 ID 数组
/// @param images [输出] 镜像状态数组
/// @param len [输入/输出] 缓冲区大小 / 实际数量
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_global_status_list(
    rados_ioctx_t io_ctx, const char *start_id, size_t max, char **image_ids,
    rbd_mirror_image_global_status_t *images, size_t *len);

/// @brief 清理镜像全局状态列表（释放内存）
/// @param image_ids 镜像 ID 数组
/// @param images 镜像状态数组
/// @param len 数组大小
CEPH_RBD_API void rbd_mirror_image_global_status_list_cleanup(
    char **image_ids, rbd_mirror_image_global_status_t *images, size_t len);

/* 以下函数已废弃，请使用对应的 rbd_mirror_peer_site_ 函数 */
/// @brief 添加镜像对等体（已废弃）
/// @deprecated 请使用 rbd_mirror_peer_site_add
CEPH_RBD_API int rbd_mirror_peer_add(
    rados_ioctx_t io_ctx, char *uuid, size_t uuid_max_length,
    const char *cluster_name, const char *client_name)
  CEPH_RBD_DEPRECATED;

/// @brief 移除镜像对等体（已废弃）
/// @deprecated 请使用 rbd_mirror_peer_site_remove
CEPH_RBD_API int rbd_mirror_peer_remove(
    rados_ioctx_t io_ctx, const char *uuid)
  CEPH_RBD_DEPRECATED;

/// @brief 列出镜像对等体（已废弃）
/// @deprecated 请使用 rbd_mirror_peer_site_list
CEPH_RBD_API int rbd_mirror_peer_list(
    rados_ioctx_t io_ctx, rbd_mirror_peer_t *peers, int *max_peers)
  CEPH_RBD_DEPRECATED;

/// @brief 清理镜像对等体列表（已废弃）
/// @deprecated 请使用 rbd_mirror_peer_site_list_cleanup
CEPH_RBD_API void rbd_mirror_peer_list_cleanup(
    rbd_mirror_peer_t *peers, int max_peers)
  CEPH_RBD_DEPRECATED;

/// @brief 设置镜像对等体客户端名称（已废弃）
/// @deprecated 请使用 rbd_mirror_peer_site_set_client_name
CEPH_RBD_API int rbd_mirror_peer_set_client(
    rados_ioctx_t io_ctx, const char *uuid, const char *client_name)
  CEPH_RBD_DEPRECATED;

/// @brief 设置镜像对等体集群名称（已废弃）
/// @deprecated 请使用 rbd_mirror_peer_site_set_name
CEPH_RBD_API int rbd_mirror_peer_set_cluster(
    rados_ioctx_t io_ctx, const char *uuid, const char *cluster_name)
  CEPH_RBD_DEPRECATED;

/// @brief 获取镜像对等体属性（已废弃）
/// @deprecated 请使用 rbd_mirror_peer_site_get_attributes
CEPH_RBD_API int rbd_mirror_peer_get_attributes(
    rados_ioctx_t p, const char *uuid, char *keys, size_t *max_key_len,
    char *values, size_t *max_value_len, size_t *key_value_count)
  CEPH_RBD_DEPRECATED;

/// @brief 设置镜像对等体属性（已废弃）
/// @deprecated 请使用 rbd_mirror_peer_site_set_attributes
CEPH_RBD_API int rbd_mirror_peer_set_attributes(
    rados_ioctx_t p, const char *uuid, const char *keys, const char *values,
    size_t key_value_count)
  CEPH_RBD_DEPRECATED;

/* rbd_mirror_image_status_list_ 命令已废弃，请使用
 * rbd_mirror_image_global_status_list_ 命令 */

/// @brief 列出镜像状态（已废弃）
/// @deprecated 请使用 rbd_mirror_image_global_status_list
CEPH_RBD_API int rbd_mirror_image_status_list(
    rados_ioctx_t io_ctx, const char *start_id, size_t max, char **image_ids,
    rbd_mirror_image_status_t *images, size_t *len)
  CEPH_RBD_DEPRECATED;

/// @brief 清理镜像状态列表（已废弃）
/// @deprecated 请使用 rbd_mirror_image_global_status_list_cleanup
CEPH_RBD_API void rbd_mirror_image_status_list_cleanup(
    char **image_ids, rbd_mirror_image_status_t *images, size_t len)
  CEPH_RBD_DEPRECATED;

/// @brief 获取镜像状态摘要
/// @param io_ctx I/O 上下文
/// @param states [输出] 状态数组
/// @param counts [输出] 各状态的计数数组
/// @param maxlen 数组大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_status_summary(
    rados_ioctx_t io_ctx, rbd_mirror_image_status_state_t *states, int *counts,
    size_t *maxlen);

/// @brief 列出镜像实例 ID
/// @param io_ctx I/O 上下文
/// @param start_id 开始镜像 ID（用于分页）
/// @param max 最大返回数量
/// @param image_ids [输出] 镜像 ID 数组
/// @param instance_ids [输出] 实例 ID 数组
/// @param len [输入/输出] 缓冲区大小 / 实际数量
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_instance_id_list(rados_ioctx_t io_ctx,
                                                   const char *start_id,
                                                   size_t max, char **image_ids,
                                                   char **instance_ids,
                                                   size_t *len);

/// @brief 清理镜像实例 ID 列表（释放内存）
/// @param image_ids 镜像 ID 数组
/// @param instance_ids 实例 ID 数组
/// @param len 数组大小
CEPH_RBD_API void rbd_mirror_image_instance_id_list_cleanup(char **image_ids,
                                                           char **instance_ids,
                                                           size_t len);
/// @brief 列出镜像镜像信息
/// @param io_ctx I/O 上下文
/// @param mode_filter 模式过滤器（nullptr 表示不过滤）
/// @param start_id 开始镜像 ID（用于分页）
/// @param max 最大返回数量
/// @param image_ids [输出] 镜像 ID 数组
/// @param mode_entries [输出] 镜像模式数组
/// @param info_entries [输出] 镜像信息数组
/// @param num_entries [输入/输出] 缓冲区大小 / 实际数量
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_info_list(
    rados_ioctx_t io_ctx, rbd_mirror_image_mode_t *mode_filter,
    const char *start_id, size_t max, char **image_ids,
    rbd_mirror_image_mode_t *mode_entries,
    rbd_mirror_image_info_t *info_entries, size_t *num_entries);

/// @brief 清理镜像镜像信息列表（释放内存）
/// @param image_ids 镜像 ID 数组
/// @param mode_entries 镜像模式数组
/// @param info_entries 镜像信息数组
/// @param num_entries 数组大小
CEPH_RBD_API void rbd_mirror_image_info_list_cleanup(
    char **image_ids, rbd_mirror_image_info_t *info_entries,
    size_t num_entries);

/// @brief 池元数据管理函数集合
/* pool metadata */
/// @brief 获取池元数据
/// @param io_ctx I/O 上下文
/// @param key 元数据键
/// @param value 存储元数据的缓冲区
/// @param val_len 缓冲区大小（输入时），实际值长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_pool_metadata_get(rados_ioctx_t io_ctx, const char *key,
                                       char *value, size_t *val_len);

/// @brief 设置池元数据
/// @param io_ctx I/O 上下文
/// @param key 元数据键
/// @param value 元数据值
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_pool_metadata_set(rados_ioctx_t io_ctx, const char *key,
                                       const char *value);

/// @brief 删除池元数据
/// @param io_ctx I/O 上下文
/// @param key 元数据键
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_pool_metadata_remove(rados_ioctx_t io_ctx,
                                          const char *key);

/// @brief 列出池元数据
/// @param io_ctx I/O 上下文
/// @param start 开始键（用于分页）
/// @param max 最大返回数量
/// @param keys 键缓冲区
/// @param key_len 键缓冲区大小（输入时），实际键长度（输出时）
/// @param values 值缓冲区
/// @param vals_len 值缓冲区大小（输入时），实际值长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_pool_metadata_list(rados_ioctx_t io_ctx, const char *start,
                                        uint64_t max, char *keys,
                                        size_t *key_len, char *values,
                                        size_t *vals_len);

/// @brief 列出池配置选项
/// @param io_ctx I/O 上下文
/// @param options 配置选项数组缓冲区
/// @param max_options 缓冲区大小（输入时），实际选项数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_config_pool_list(rados_ioctx_t io_ctx,
                                      rbd_config_option_t *options,
                                      int *max_options);

/// @brief 清理池配置选项列表（释放内存）
/// @param options 配置选项数组
/// @param max_options 数组大小
CEPH_RBD_API void rbd_config_pool_list_cleanup(rbd_config_option_t *options,
                                               int max_options);

/// @brief 打开镜像（同步版本）
/// @param io I/O 上下文
/// @param name 镜像名称
/// @param image [输出] 镜像对象句柄
/// @param snap_name 快照名称（nullptr 表示打开当前版本）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_open(rados_ioctx_t io, const char *name,
                          rbd_image_t *image, const char *snap_name);

/// @brief 通过 ID 打开镜像（同步版本）
/// @param io I/O 上下文
/// @param id 镜像 ID
/// @param image [输出] 镜像对象句柄
/// @param snap_name 快照名称（nullptr 表示打开当前版本）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_open_by_id(rados_ioctx_t io, const char *id,
                                rbd_image_t *image, const char *snap_name);

/// @brief 异步打开镜像
/// @param io I/O 上下文
/// @param name 镜像名称
/// @param image [输出] 镜像对象句柄
/// @param snap_name 快照名称（nullptr 表示打开当前版本）
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_open(rados_ioctx_t io, const char *name,
			      rbd_image_t *image, const char *snap_name,
			      rbd_completion_t c);

/// @brief 异步通过 ID 打开镜像
/// @param io I/O 上下文
/// @param id 镜像 ID
CEPH_RBD_API int rbd_aio_open_by_id(rados_ioctx_t io, const char *id,
                                    rbd_image_t *image, const char *snap_name,
                                    rbd_completion_t c);

/**
 * @brief 以只读模式打开镜像
 *
 * 此函数适用于由于 cephx 权限限制而无法写入块设备的客户端。
 * 不会在头对象上建立监视，因为监视是一种写入操作。这意味着
 * 关于此镜像报告的元数据（父镜像、快照、大小等）可能会变得过时。
 * 不应将其用于长时间运行的操作，除非您能确定这些属性之一
 * 发生变化是安全的。
 *
 * 尝试写入只读镜像将返回 -EROFS。
 *
 * @param io 用于确定镜像所在池的 I/O 上下文
 * @param name 镜像名称
 * @param image 存储新打开的镜像句柄的位置
 * @param snap_name 要打开的快照名称，或 NULL 表示无快照
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_open_read_only(rados_ioctx_t io, const char *name,
                                    rbd_image_t *image, const char *snap_name);

/// @brief 通过 ID 以只读模式打开镜像
/// @param io I/O 上下文
/// @param id 镜像 ID
/// @param image [输出] 镜像句柄
/// @param snap_name 快照名称（nullptr 表示无快照）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_open_by_id_read_only(rados_ioctx_t io, const char *id,
                                          rbd_image_t *image, const char *snap_name);

/// @brief 异步以只读模式打开镜像
/// @param io I/O 上下文
/// @param name 镜像名称
/// @param image [输出] 镜像句柄
/// @param snap_name 快照名称（nullptr 表示无快照）
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_open_read_only(rados_ioctx_t io, const char *name,
					rbd_image_t *image, const char *snap_name,
					rbd_completion_t c);

/// @brief 异步通过 ID 以只读模式打开镜像
/// @param io I/O 上下文
/// @param id 镜像 ID
/// @param image [输出] 镜像句柄
/// @param snap_name 快照名称（nullptr 表示无快照）
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_open_by_id_read_only(rados_ioctx_t io, const char *id,
                                              rbd_image_t *image, const char *snap_name,
                                              rbd_completion_t c);

/// @brief 将功能特性转换为字符串
/// @param features 功能特性位
/// @param str_features 存储字符串的缓冲区
/// @param size 缓冲区大小（输入时），实际字符串长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_features_to_string(uint64_t features, char *str_features,
                                        size_t *size);

/// @brief 从字符串解析功能特性
/// @param str_features 功能特性字符串
/// @param features [输出] 功能特性位
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_features_from_string(const char *str_features, uint64_t *features);
/// @brief 关闭镜像
/// @param image 镜像句柄
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_close(rbd_image_t image);

/// @brief 异步关闭镜像
/// @param image 镜像句柄
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_close(rbd_image_t image, rbd_completion_t c);

/// @brief 调整镜像大小
/// @param image 镜像句柄
/// @param size 新的镜像大小（字节）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_resize(rbd_image_t image, uint64_t size);

/// @brief 调整镜像大小（带收缩控制和进度回调）
/// @param image 镜像句柄
/// @param size 新的镜像大小（字节）
/// @param allow_shrink 是否允许缩小镜像
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_resize2(rbd_image_t image, uint64_t size, bool allow_shrink,
			     librbd_progress_fn_t cb, void *cbdata);

/// @brief 调整镜像大小（带进度回调）
/// @param image 镜像句柄
/// @param size 新的镜像大小（字节）
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_resize_with_progress(rbd_image_t image, uint64_t size,
			     librbd_progress_fn_t cb, void *cbdata);

/// @brief 获取镜像统计信息
/// @param image 镜像句柄
/// @param info [输出] 镜像信息结构
/// @param infosize 信息结构大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_stat(rbd_image_t image, rbd_image_info_t *info,
                          size_t infosize);

/// @brief 检查镜像是否为旧格式
/// @param image 镜像句柄
/// @param old [输出] 是否为旧格式（1 表示旧格式，0 表示新格式）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_old_format(rbd_image_t image, uint8_t *old);

/// @brief 获取镜像大小
/// @param image 镜像句柄
/// @param size [输出] 镜像大小（字节）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_size(rbd_image_t image, uint64_t *size);

/// @brief 获取镜像功能特性
/// @param image 镜像句柄
/// @param features [输出] 功能特性位
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_features(rbd_image_t image, uint64_t *features);

/// @brief 更新镜像功能特性
/// @param image 镜像句柄
/// @param features 要更新的功能特性位
/// @param enabled 是否启用（1）或禁用（0）这些特性
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_update_features(rbd_image_t image, uint64_t features,
                                     uint8_t enabled);

/// @brief 获取镜像操作功能特性
/// @param image 镜像句柄
/// @param op_features [输出] 操作功能特性位
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_op_features(rbd_image_t image, uint64_t *op_features);

/// @brief 获取镜像条带单元大小
/// @param image 镜像句柄
/// @param stripe_unit [输出] 条带单元大小（字节）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_stripe_unit(rbd_image_t image, uint64_t *stripe_unit);

/// @brief 获取镜像条带计数
/// @param image 镜像句柄
/// @param stripe_count [输出] 条带计数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_stripe_count(rbd_image_t image,
                                      uint64_t *stripe_count);

/// @brief 获取镜像创建时间戳
/// @param image 镜像句柄
/// @param timestamp [输出] 创建时间戳
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_create_timestamp(rbd_image_t image,
                                          struct timespec *timestamp);

/// @brief 获取镜像访问时间戳
/// @param image 镜像句柄
/// @param timestamp [输出] 访问时间戳
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_access_timestamp(rbd_image_t image,
                                          struct timespec *timestamp);

/// @brief 获取镜像修改时间戳
/// @param image 镜像句柄
/// @param timestamp [输出] 修改时间戳
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_modify_timestamp(rbd_image_t image,
                                          struct timespec *timestamp);

/// @brief 获取镜像重叠大小
/// @param image 镜像句柄
/// @param overlap [输出] 重叠大小（字节）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_overlap(rbd_image_t image, uint64_t *overlap);

/// @brief 获取镜像名称
/// @param image 镜像句柄
/// @param name 存储镜像名称的缓冲区
/// @param name_len 缓冲区大小（输入时），实际名称长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_name(rbd_image_t image, char *name, size_t *name_len);

/// @brief 获取镜像 ID
/// @param image 镜像句柄
/// @param id 存储镜像 ID 的缓冲区
/// @param id_len 缓冲区大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_id(rbd_image_t image, char *id, size_t id_len);

/// @brief 获取镜像块名称前缀
/// @param image 镜像句柄
/// @param prefix 存储前缀的缓冲区
/// @param prefix_len 缓冲区大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_block_name_prefix(rbd_image_t image,
                                           char *prefix, size_t prefix_len);

/// @brief 获取镜像数据池 ID
/// @param image 镜像句柄
/// @return 数据池 ID，失败返回负值
CEPH_RBD_API int64_t rbd_get_data_pool_id(rbd_image_t image);

/// @brief 获取父镜像信息（已废弃）
/// @deprecated 请使用 rbd_get_parent
CEPH_RBD_API int rbd_get_parent_info(rbd_image_t image,
			             char *parent_poolname, size_t ppoolnamelen,
			             char *parent_name, size_t pnamelen,
			             char *parent_snapname,
                                     size_t psnapnamelen)
  CEPH_RBD_DEPRECATED;

/// @brief 获取父镜像信息（带 ID，已废弃）
/// @deprecated 请使用 rbd_get_parent
CEPH_RBD_API int rbd_get_parent_info2(rbd_image_t image,
                                      char *parent_poolname,
                                      size_t ppoolnamelen,
                                      char *parent_name, size_t pnamelen,
                                      char *parent_id, size_t pidlen,
                                      char *parent_snapname,
                                      size_t psnapnamelen)
  CEPH_RBD_DEPRECATED;

/// @brief 获取父镜像信息
/// @param image 镜像句柄
/// @param parent_image [输出] 父镜像规格信息
/// @param parent_snap [输出] 父快照规格信息
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_parent(rbd_image_t image,
                                rbd_linked_image_spec_t *parent_image,
                                rbd_snap_spec_t *parent_snap);

/// @brief 获取镜像迁移源规格字符串
/// @param image 镜像句柄
/// @param source_spec 存储源规格的缓冲区
/// @param max_len 缓冲区大小（输入时），实际字符串长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_migration_source_spec(rbd_image_t image,
                                               char* source_spec,
                                               size_t* max_len);

/// @brief 获取镜像标志位
/// @param image 镜像句柄
/// @param flags [输出] 标志位
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_flags(rbd_image_t image, uint64_t *flags);

/// @brief 获取镜像所属分组信息
/// @param image 镜像句柄
/// @param group_info [输出] 分组信息结构
/// @param group_info_size 信息结构大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_get_group(rbd_image_t image, rbd_group_info_t *group_info,
                               size_t group_info_size);

/// @brief 设置镜像通知
/// @param image 镜像句柄
/// @param fd 文件描述符
/// @param type 事件类型（EVENT_TYPE_PIPE 或 EVENT_TYPE_EVENTFD）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_set_image_notification(rbd_image_t image, int fd, int type);

/// @brief 独占锁功能集合
/* exclusive lock feature */
/// @brief 检查是否为独占锁所有者
/// @param image 镜像句柄
/// @param is_owner [输出] 是否为锁所有者（1 表示是，0 表示否）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_is_exclusive_lock_owner(rbd_image_t image, int *is_owner);

/// @brief 获取独占锁
/// @param image 镜像句柄
/// @param lock_mode 锁模式
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_lock_acquire(rbd_image_t image, rbd_lock_mode_t lock_mode);

/// @brief 释放独占锁
/// @param image 镜像句柄
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_lock_release(rbd_image_t image);

/// @brief 获取锁所有者列表
/// @param image 镜像句柄
/// @param lock_mode [输出] 锁模式
/// @param lock_owners [输出] 锁所有者数组
/// @param max_lock_owners 数组大小（输入时），实际所有者数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_lock_get_owners(rbd_image_t image,
                                     rbd_lock_mode_t *lock_mode,
                                     char **lock_owners,
                                     size_t *max_lock_owners);

/// @brief 清理锁所有者列表（释放内存）
/// @param lock_owners 锁所有者数组
/// @param lock_owner_count 数组大小
CEPH_RBD_API void rbd_lock_get_owners_cleanup(char **lock_owners,
                                              size_t lock_owner_count);

/// @brief 强制解除锁（打破死锁）
/// @param image 镜像句柄
/// @param lock_mode 锁模式
/// @param lock_owner 要解除的锁所有者
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_lock_break(rbd_image_t image, rbd_lock_mode_t lock_mode,
                                const char *lock_owner);

/// @brief 对象映射功能集合
/* object map feature */
/// @brief 重建对象映射（带进度回调）
/// @param image 镜像句柄
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_rebuild_object_map(rbd_image_t image,
                                        librbd_progress_fn_t cb, void *cbdata);

/// @brief 拷贝镜像到指定池中的新镜像（基本版本）
/// @param image 源镜像句柄
/// @param dest_io_ctx 目标 I/O 上下文
/// @param destname 目标镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_copy(rbd_image_t image, rados_ioctx_t dest_io_ctx,
                          const char *destname);

/// @brief 拷贝镜像到另一个镜像（镜像到镜像拷贝）
/// @param src 源镜像句柄
/// @param dest 目标镜像句柄
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_copy2(rbd_image_t src, rbd_image_t dest);

/// @brief 拷贝镜像到指定池中的新镜像（使用选项对象）
/// @param src 源镜像句柄
/// @param dest_io_ctx 目标 I/O 上下文
/// @param destname 目标镜像名称
/// @param dest_opts 目标镜像选项对象
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_copy3(rbd_image_t src, rados_ioctx_t dest_io_ctx,
			   const char *destname, rbd_image_options_t dest_opts);

/// @brief 拷贝镜像到指定池中的新镜像（使用选项对象和稀疏大小）
/// @param src 源镜像句柄
/// @param dest_io_ctx 目标 I/O 上下文
/// @param destname 目标镜像名称
/// @param dest_opts 目标镜像选项对象
/// @param sparse_size 稀疏大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_copy4(rbd_image_t src, rados_ioctx_t dest_io_ctx,
			   const char *destname, rbd_image_options_t dest_opts,
			   size_t sparse_size);

/// @brief 拷贝镜像到指定池中的新镜像（带进度回调，基本版本）
/// @param image 源镜像句柄
/// @param dest_p 目标 I/O 上下文
/// @param destname 目标镜像名称
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_copy_with_progress(rbd_image_t image, rados_ioctx_t dest_p,
                                        const char *destname,
                                        librbd_progress_fn_t cb, void *cbdata);

/// @brief 拷贝镜像到另一个镜像（带进度回调）
/// @param src 源镜像句柄
/// @param dest 目标镜像句柄
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_copy_with_progress2(rbd_image_t src, rbd_image_t dest,
			                 librbd_progress_fn_t cb, void *cbdata);

/// @brief 拷贝镜像到指定池中的新镜像（带进度回调和选项对象）
/// @param image 源镜像句柄
/// @param dest_p 目标 I/O 上下文
/// @param destname 目标镜像名称
/// @param dest_opts 目标镜像选项对象
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_copy_with_progress3(rbd_image_t image,
					 rados_ioctx_t dest_p,
					 const char *destname,
					 rbd_image_options_t dest_opts,
					 librbd_progress_fn_t cb, void *cbdata);
/// @brief 拷贝镜像到指定池中的新镜像（带进度回调、选项对象和稀疏大小）
/// @param image 源镜像句柄
/// @param dest_p 目标 I/O 上下文
/// @param destname 目标镜像名称
/// @param dest_opts 目标镜像选项对象
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @param sparse_size 稀疏大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_copy_with_progress4(rbd_image_t image,
					 rados_ioctx_t dest_p,
					 const char *destname,
					 rbd_image_options_t dest_opts,
					 librbd_progress_fn_t cb, void *cbdata,
					 size_t sparse_size);

/// @brief 深度拷贝功能集合
/* deep copy */
/// @brief 深度拷贝镜像到指定池中的新镜像
/// @param src 源镜像句柄
/// @param dest_io_ctx 目标 I/O 上下文
/// @param destname 目标镜像名称
/// @param dest_opts 目标镜像选项对象
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_deep_copy(rbd_image_t src, rados_ioctx_t dest_io_ctx,
                               const char *destname,
                               rbd_image_options_t dest_opts);

/// @brief 深度拷贝镜像到指定池中的新镜像（带进度回调）
/// @param image 源镜像句柄
/// @param dest_io_ctx 目标 I/O 上下文
/// @param destname 目标镜像名称
/// @param dest_opts 目标镜像选项对象
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_deep_copy_with_progress(rbd_image_t image,
                                             rados_ioctx_t dest_io_ctx,
                                             const char *destname,
                                             rbd_image_options_t dest_opts,
                                             librbd_progress_fn_t cb,
                                             void *cbdata);

/// @brief 加密功能集合
/* encryption */

/**
 * @brief 使用指定的加密规格格式化镜像
 *
 * 对于平面镜像（非克隆镜像），新加密会被隐式加载，无需后续调用 rbd_encryption_load()。
 * 如果已有加密已加载，会自动被新加密替换。
 *
 * 对于克隆镜像，必须显式加载新加密。现有的加密（如果有）不能被加载。
 *
 * @param image 镜像句柄
 * @param format 加密格式
 * @param opts 加密选项
 * @param opts_size 选项大小
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_encryption_format(rbd_image_t image,
                                       rbd_encryption_format_t format,
                                       rbd_encryption_options_t opts,
                                       size_t opts_size);

/**
 * @brief 加载镜像及其所有祖先镜像的加密规格
 *
 * 如果遇到不匹配 librbd 已知加密格式的祖先镜像，该镜像及其剩余祖先镜像将被解释为明文。
 *
 * @param image 镜像句柄
 * @param format 加密格式
 * @param opts 加密选项
 * @param opts_size 选项大小
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_encryption_load(rbd_image_t image,
                                     rbd_encryption_format_t format,
                                     rbd_encryption_options_t opts,
                                     size_t opts_size);
/**
 * @brief 加载多个加密规格
 *
 * 传递的数组中的第一个规格应用于镜像本身，第二个规格应用于其祖先镜像，
 * 第三个规格应用于该祖先镜像的祖先镜像，以此类推。
 *
 * 如果传递的规格不够，最后一个规格会被重复使用，就像在 rbd_encryption_load() 中一样。
 * 如果正在重复使用最后一个规格的祖先镜像不符合 librbd 已知的任何加密格式，
 * 则该镜像及其剩余祖先镜像将被解释为明文。
 *
 * @param image 镜像句柄
 * @param specs 加密规格数组
 * @param spec_count 规格数量
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_encryption_load2(rbd_image_t image,
                                      const rbd_encryption_spec_t *specs,
                                      size_t spec_count);

/// @brief 快照管理功能集合
/* snapshots */
/// @brief 列出镜像快照
/// @param image 镜像句柄
/// @param snaps 快照信息数组缓冲区
/// @param max_snaps 缓冲区大小（输入时），实际快照数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_list(rbd_image_t image, rbd_snap_info_t *snaps,
                               int *max_snaps);

/// @brief 结束快照列表（释放内存）
/// @param snaps 快照信息数组
CEPH_RBD_API void rbd_snap_list_end(rbd_snap_info_t *snaps);

/// @brief 检查快照是否存在
/// @param image 镜像句柄
/// @param snapname 快照名称
/// @param exists [输出] 是否存在（1 表示存在，0 表示不存在）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_exists(rbd_image_t image, const char *snapname, bool *exists);

/// @brief 创建快照
/// @param image 镜像句柄
/// @param snapname 快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_create(rbd_image_t image, const char *snapname);

/// @brief 创建快照（带标志和进度回调）
/// @param image 镜像句柄
/// @param snap_name 快照名称
/// @param flags 创建标志（RBD_SNAP_CREATE_*）
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_create2(rbd_image_t image, const char *snap_name,
                                  uint32_t flags, librbd_progress_fn_t cb,
                                  void *cbdata);

/// @brief 删除快照
/// @param image 镜像句柄
/// @param snapname 快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_remove(rbd_image_t image, const char *snapname);

/// @brief 删除快照（带标志和进度回调）
/// @param image 镜像句柄
/// @param snap_name 快照名称
/// @param flags 删除标志（RBD_SNAP_REMOVE_*）
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_remove2(rbd_image_t image, const char *snap_name,
                                  uint32_t flags, librbd_progress_fn_t cb,
                                  void *cbdata);

/// @brief 通过 ID 删除快照
/// @param image 镜像句柄
/// @param snap_id 快照 ID
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_remove_by_id(rbd_image_t image, uint64_t snap_id);

/// @brief 回滚到快照
/// @param image 镜像句柄
/// @param snapname 快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_rollback(rbd_image_t image, const char *snapname);

/// @brief 回滚到快照（带进度回调）
/// @param image 镜像句柄
/// @param snapname 快照名称
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_rollback_with_progress(rbd_image_t image,
                                                 const char *snapname,
				                 librbd_progress_fn_t cb,
                                                 void *cbdata);

/// @brief 重命名快照
/// @param image 镜像句柄
/// @param snapname 原快照名称
/// @param dstsnapsname 新快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_rename(rbd_image_t image, const char *snapname,
				 const char* dstsnapsname);
/**
 * @brief 保护快照防止被删除
 *
 * 保护的快照在取消保护之前无法被删除。
 *
 * @param image 镜像句柄
 * @param snap_name 要保护的快照名称
 * @return 成功返回 0，失败返回负的错误码
 * @return -EBUSY 如果快照已被保护
 */
CEPH_RBD_API int rbd_snap_protect(rbd_image_t image, const char *snap_name);

/**
 * @brief 取消保护快照允许删除
 *
 * @param image 镜像句柄
 * @param snap_name 要取消保护的快照名称
 * @return 成功返回 0，失败返回负的错误码
 * @return -EINVAL 如果快照未被保护
 */
CEPH_RBD_API int rbd_snap_unprotect(rbd_image_t image, const char *snap_name);

/**
 * @brief 检查快照是否被保护
 *
 * @param image 镜像句柄
 * @param snap_name 要检查的快照名称
 * @param is_protected [输出] 是否被保护（1 表示被保护，0 表示未被保护）
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_snap_is_protected(rbd_image_t image, const char *snap_name,
			               int *is_protected);
/**
 * @brief 获取镜像的快照数量限制
 *
 * 如果未设置限制，将返回 UINT64_MAX。
 *
 * @param image 镜像句柄
 * @param limit [输出] 快照数量限制
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_snap_get_limit(rbd_image_t image, uint64_t *limit);

/**
 * @brief 设置镜像的快照数量限制
 *
 * @param image 镜像句柄
 * @param limit 允许的最大快照数量
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_snap_set_limit(rbd_image_t image, uint64_t limit);

/**
 * @brief 获取快照的时间戳
 *
 * @param image 镜像句柄
 * @param snap_id 快照 ID
 * @param timestamp [输出] 快照时间戳
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_snap_get_timestamp(rbd_image_t image, uint64_t snap_id, struct timespec *timestamp);

/// @brief 设置当前快照（通过名称）
/// @param image 镜像句柄
/// @param snapname 快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_set(rbd_image_t image, const char *snapname);

/// @brief 设置当前快照（通过 ID）
/// @param image 镜像句柄
/// @param snap_id 快照 ID
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_set_by_id(rbd_image_t image, uint64_t snap_id);

/// @brief 获取快照名称（通过 ID）
/// @param image 镜像句柄
/// @param snap_id 快照 ID
/// @param snapname 存储快照名称的缓冲区
/// @param name_len 缓冲区大小（输入时），实际名称长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_get_name(rbd_image_t image, uint64_t snap_id, char *snapname, size_t *name_len);

/// @brief 获取快照 ID（通过名称）
/// @param image 镜像句柄
/// @param snapname 快照名称
/// @param snap_id [输出] 快照 ID
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_get_id(rbd_image_t image, const char *snapname, uint64_t *snap_id);

/// @brief 获取快照命名空间类型
/// @param image 镜像句柄
/// @param snap_id 快照 ID
/// @param namespace_type [输出] 命名空间类型
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_get_namespace_type(rbd_image_t image,
                                             uint64_t snap_id,
                                             rbd_snap_namespace_type_t *namespace_type);

/// @brief 获取快照分组命名空间信息
/// @param image 镜像句柄
/// @param snap_id 快照 ID
/// @param group_snap [输出] 分组快照命名空间信息
/// @param group_snap_size 信息结构大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_get_group_namespace(rbd_image_t image,
                                              uint64_t snap_id,
                                              rbd_snap_group_namespace_t *group_snap,
                                              size_t group_snap_size);

/// @brief 清理快照分组命名空间信息（释放内存）
/// @param group_snap 分组快照命名空间信息结构
/// @param group_snap_size 结构大小
CEPH_RBD_API int rbd_snap_group_namespace_cleanup(rbd_snap_group_namespace_t *group_snap,
                                                  size_t group_snap_size);

/// @brief 获取快照垃圾回收箱命名空间信息（旧版本）
/// @param image 镜像句柄
/// @param snap_id 快照 ID
/// @param original_name 原始名称缓冲区
/// @param max_length 缓冲区大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_get_trash_namespace(rbd_image_t image,
                                              uint64_t snap_id,
                                              char* original_name,
                                              size_t max_length);

/// @brief 获取快照垃圾回收箱命名空间信息
/// @param image 镜像句柄
/// @param snap_id 快照 ID
/// @param trash_snap [输出] 垃圾回收箱快照命名空间信息
/// @param trash_snap_size 信息结构大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_get_trash_namespace2(
    rbd_image_t image, uint64_t snap_id,
    rbd_snap_trash_namespace_t *trash_snap, size_t trash_snap_size);

/// @brief 清理快照垃圾回收箱命名空间信息（释放内存）
/// @param trash_snap 垃圾回收箱快照命名空间信息结构
/// @param trash_snap_size 结构大小
CEPH_RBD_API int rbd_snap_trash_namespace_cleanup(
    rbd_snap_trash_namespace_t *trash_snap, size_t trash_snap_size);

/// @brief 获取快照镜像命名空间信息
/// @param image 镜像句柄
/// @param snap_id 快照 ID
/// @param mirror_snap [输出] 镜像快照命名空间信息
/// @param mirror_snap_size 信息结构大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_snap_get_mirror_namespace(
    rbd_image_t image, uint64_t snap_id,
    rbd_snap_mirror_namespace_t *mirror_snap, size_t mirror_snap_size);

/// @brief 清理快照镜像命名空间信息（释放内存）
/// @param mirror_snap 镜像快照命名空间信息结构
/// @param mirror_snap_size 结构大小
CEPH_RBD_API int rbd_snap_mirror_namespace_cleanup(
    rbd_snap_mirror_namespace_t *mirror_snap, size_t mirror_snap_size);

/// @brief 扁平化克隆镜像
/// @param image 镜像句柄
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_flatten(rbd_image_t image);

/// @brief 扁平化克隆镜像（带进度回调）
/// @param image 镜像句柄
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_flatten_with_progress(rbd_image_t image,
                                           librbd_progress_fn_t cb,
                                           void *cbdata);

/// @brief 使镜像稀疏化
/// @param image 镜像句柄
/// @param sparse_size 稀疏大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_sparsify(rbd_image_t image, size_t sparse_size);

/// @brief 使镜像稀疏化（带进度回调）
/// @param image 镜像句柄
/// @param sparse_size 稀疏大小
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_sparsify_with_progress(rbd_image_t image,
                                            size_t sparse_size,
                                            librbd_progress_fn_t cb,
                                            void *cbdata);

/**
 * @brief 列出从指定快照克隆的所有镜像（已废弃）
 *
 * 此函数会遍历所有池，因此应该由具有所有池读取权限的用户运行。
 * pools_len 和 images_len 会被填入放入缓冲区的字节数。
 *
 * 如果提供的缓冲区太短，需要的长度仍会被填入，但数据不会被写入，并返回 -ERANGE。
 * 否则，缓冲区会被填入子镜像的池名称和镜像名称，每个名称后跟 '\0'。
 *
 * @param image 要列出克隆的镜像（隐式快照）
/// @param pools 存储池名称的缓冲区
/// @param pools_len 池缓冲区字节数
/// @param images 存储镜像名称的缓冲区
/// @param images_len 镜像缓冲区字节数
/// @return 成功时返回子镜像数量，失败时返回负的错误码
/// @return 如果任一缓冲区太短，返回 -ERANGE
 * @deprecated 请使用 rbd_list_children2
 */
CEPH_RBD_API ssize_t rbd_list_children(rbd_image_t image, char *pools,
                                       size_t *pools_len, char *images,
                                       size_t *images_len)
  CEPH_RBD_DEPRECATED;

/// @brief 列出子镜像信息（已废弃）
/// @deprecated 请使用 rbd_list_children3
CEPH_RBD_API int rbd_list_children2(rbd_image_t image,
                                    rbd_child_info_t *children,
                                    int *max_children)
  CEPH_RBD_DEPRECATED;

/// @brief 清理子镜像信息（释放内存）
/// @param child 子镜像信息结构
/// @brief 清理子镜像信息（已废弃）
/// @deprecated 请使用 rbd_list_children_cleanup
CEPH_RBD_API void rbd_list_child_cleanup(rbd_child_info_t *child)
  CEPH_RBD_DEPRECATED;

/// @brief 清理子镜像列表（已废弃）
/// @deprecated 请使用 rbd_list_children3 和相关清理函数
CEPH_RBD_API void rbd_list_children_cleanup(rbd_child_info_t *children,
                                            size_t num_children)
  CEPH_RBD_DEPRECATED;

/// @brief 列出子镜像（新版本，返回链接镜像规格）
/// @param image 镜像句柄
/// @param images [输出] 子镜像规格数组
/// @param max_images 数组大小（输入时），实际镜像数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_list_children3(rbd_image_t image,
                                    rbd_linked_image_spec_t *images,
                                    size_t *max_images);

/// @brief 列出所有后代镜像（递归子镜像）
/// @param image 镜像句柄
/// @param images [输出] 后代镜像规格数组
/// @param max_images 数组大小（输入时），实际镜像数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_list_descendants(rbd_image_t image,
                                      rbd_linked_image_spec_t *images,
                                      size_t *max_images);

/**
 * @defgroup librbd_h_locking Advisory Locking
 *
 * An rbd image may be locking exclusively, or shared, to facilitate
 * e.g. live migration where the image may be open in two places at once.
 * These locks are intended to guard against more than one client
 * writing to an image without coordination. They don't need to
 * be used for snapshots, since snapshots are read-only.
 *
 * Currently locks only guard against locks being acquired.
 * They do not prevent anything else.
 *
 * A locker is identified by the internal rados client id of the
 * holder and a user-defined cookie. This (client id, cookie) pair
 * must be unique for each locker.
 *
 * A shared lock also has a user-defined tag associated with it. Each
 * additional shared lock must specify the same tag or lock
 * acquisition will fail. This can be used by e.g. groups of hosts
 * using a clustered filesystem on top of an rbd image to make sure
 * they're accessing the correct image.
 *
 * @{
 */
/**
 * @brief 列出持有镜像锁的客户端和锁信息
 *
 * 每个缓冲区所需的字节数会被放入对应的 size 输出参数中。如果任一提供的缓冲区太短，
 * 这些大小会被填入后返回 -ERANGE。
 *
 * @param image 镜像句柄
 * @param exclusive [输出] 锁是否为独占锁（1 表示独占，0 表示共享）
 * @param tag 存储镜像关联标签的缓冲区
 * @param tag_len 标签缓冲区字节数
 * @param clients 存储锁持有者客户端的缓冲区，用 '\0' 分隔
 * @param clients_len 客户端缓冲区字节数
 * @param cookies 存储锁持有者 cookie 的缓冲区，用 '\0' 分隔
 * @param cookies_len cookie 缓冲区字节数
 * @param addrs 存储锁持有者地址的缓冲区，用 '\0' 分隔
 * @param addrs_len 地址缓冲区字节数
 * @return 成功时返回锁持有者数量，失败时返回负的错误码
 * @return 如果任一缓冲区太短，返回 -ERANGE
 */
CEPH_RBD_API ssize_t rbd_list_lockers(rbd_image_t image, int *exclusive,
			              char *tag, size_t *tag_len,
			              char *clients, size_t *clients_len,
			              char *cookies, size_t *cookies_len,
			              char *addrs, size_t *addrs_len);

/**
 * @brief 获取镜像的独占锁
 *
 * @param image 要锁定的镜像
 * @param cookie 此锁实例的用户定义标识符
 * @return 成功返回 0，失败返回负的错误码
 * @return -EBUSY 如果锁已被其他（客户端，cookie）对持有
 * @return -EEXIST 如果锁已被相同的（客户端，cookie）对持有
 */
CEPH_RBD_API int rbd_lock_exclusive(rbd_image_t image, const char *cookie);

/**
 * @brief 获取镜像的共享锁
 *
 * 其他客户端也可以获取共享锁，只要它们使用相同的标签即可。
 *
 * @param image 要锁定的镜像
 * @param cookie 此锁实例的用户定义标识符
 * @param tag 此共享锁使用的用户定义标识符
 * @return 成功返回 0，失败返回负的错误码
 * @return -EBUSY 如果锁已被其他（客户端，cookie）对持有
 * @return -EEXIST 如果锁已被相同的（客户端，cookie）对持有
 */
CEPH_RBD_API int rbd_lock_shared(rbd_image_t image, const char *cookie,
                                 const char *tag);

/**
 * @brief 释放镜像的共享或独占锁
 *
 * @param image 要解锁的镜像
 * @param cookie 锁实例的用户定义标识符
 * @return 成功返回 0，失败返回负的错误码
 * @return -ENOENT 如果指定的（客户端，cookie）对未持有锁
 */
CEPH_RBD_API int rbd_unlock(rbd_image_t image, const char *cookie);

/**
 * @brief 释放指定客户端持有的共享或独占锁（打破死锁）
 *
 * @param image 要解锁的镜像
 * @param client 持有锁的实体（由 rbd_list_lockers() 返回）
 * @param cookie 要打破的锁实例的用户定义标识符
 * @return 成功返回 0，失败返回负的错误码
 * @return -ENOENT 如果指定的（客户端，cookie）对未持有锁
 */
CEPH_RBD_API int rbd_break_lock(rbd_image_t image, const char *client,
                                const char *cookie);

/** @} locking */

/**
 * @brief I/O 操作功能集合
 */
/* I/O */
/// @brief 从镜像读取数据
/// @param image 镜像句柄
/// @param ofs 读取偏移量
/// @param len 读取长度
/// @param buf 存储数据的缓冲区
/// @return 成功时返回读取的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_read(rbd_image_t image, uint64_t ofs, size_t len,
                              char *buf);

/**
 * @brief 从镜像读取数据（带操作标志）
 * @param op_flags 见 librados.h 中以 LIBRADOS_OP_FLAG 开头的常量
 */
/// @brief 从镜像读取数据（带操作标志）
/// @param image 镜像句柄
/// @param ofs 读取偏移量
/// @param len 读取长度
/// @param buf 存储数据的缓冲区
/// @param op_flags 操作标志（见 librados.h 中以 LIBRADOS_OP_FLAG 开头的常量）
/// @return 成功时返回读取的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_read2(rbd_image_t image, uint64_t ofs, size_t len,
                               char *buf, int op_flags);

/// @brief 迭代读取镜像数据（已废弃）
/// @deprecated 请使用 rbd_read_iterate2
CEPH_RBD_API int64_t rbd_read_iterate(rbd_image_t image, uint64_t ofs, size_t len,
			              int (*cb)(uint64_t, size_t, const char *, void *),
                                      void *arg);

/**
 * @brief 迭代读取镜像数据
 *
 * 读取镜像的每个区域并调用回调函数。如果传递给回调函数的缓冲区指针为 NULL，
 * 则给定的范围被定义为零（一个空洞）。通常回调的粒度是镜像条带大小。
 *
 * @param image 要读取的镜像
 * @param ofs 开始偏移量
 * @param len 要覆盖的源镜像字节数
 * @param cb 每个区域的回调函数
 * @param arg 传递给回调函数的参数
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_read_iterate2(rbd_image_t image, uint64_t ofs, uint64_t len,
		                   int (*cb)(uint64_t, size_t, const char *, void *),
                                   void *arg);

/**
 * @brief 获取镜像两个版本之间的差异
 *
 * 通过回调函数返回镜像两个版本之间的差异，回调函数获取偏移量、长度和一个标志，
 * 指示范围是否存在（1），或已知/定义为零（空洞，0）。如果源快照名称为 NULL，
 * 我们将其解释为时间开始，返回镜像的所有已分配区域。结束版本是当前为镜像句柄
 * 选择的版本（快照或可写头）。
 *
 * @param fromsnapname 开始快照名称，或 NULL
 * @param ofs 开始偏移量
 * @param len 要报告的区域字节长度
 * @param include_parent 如果完整历史差异应包括父镜像则为 1
 * @param whole_object 如果差异范围应覆盖整个对象则为 1
 * @param cb 为每个已分配区域调用的回调函数
 * @param arg 传递给回调函数的参数
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_diff_iterate(rbd_image_t image,
		                  const char *fromsnapname,
		                  uint64_t ofs, uint64_t len,
		                  int (*cb)(uint64_t, size_t, int, void *),
                                  void *arg);

/// @brief 获取镜像两个版本之间的差异（增强版本）
/// @param image 镜像句柄
/// @param fromsnapname 开始快照名称，或 NULL
/// @param ofs 开始偏移量
/// @param len 要报告的区域字节长度
/// @param include_parent 如果完整历史差异应包括父镜像则为 1
/// @param whole_object 如果差异范围应覆盖整个对象则为 1
/// @param cb 为每个已分配区域调用的回调函数
/// @param arg 传递给回调函数的参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_diff_iterate2(rbd_image_t image,
		                   const char *fromsnapname,
		                   uint64_t ofs, uint64_t len,
                                   uint8_t include_parent, uint8_t whole_object,
		                   int (*cb)(uint64_t, size_t, int, void *),
                                   void *arg);
/// @brief 向镜像写入数据
/// @param image 镜像句柄
/// @param ofs 写入偏移量
/// @param len 写入长度
/// @param buf 要写入的数据缓冲区
/// @return 成功时返回写入的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_write(rbd_image_t image, uint64_t ofs, size_t len,
                               const char *buf);

/**
 * @brief 向镜像写入数据（带操作标志）
 * @param op_flags 见 librados.h 中以 LIBRADOS_OP_FLAG 开头的常量
 */
/// @brief 向镜像写入数据（带操作标志）
/// @param image 镜像句柄
/// @param ofs 写入偏移量
/// @param len 写入长度
/// @param buf 要写入的数据缓冲区
/// @param op_flags 操作标志（见 librados.h 中以 LIBRADOS_OP_FLAG 开头的常量）
/// @return 成功时返回写入的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_write2(rbd_image_t image, uint64_t ofs, size_t len,
                                const char *buf, int op_flags);

/// @brief 丢弃镜像中的数据（释放空间）
/// @param image 镜像句柄
/// @param ofs 丢弃偏移量
/// @param len 丢弃长度
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_discard(rbd_image_t image, uint64_t ofs, uint64_t len);

/// @brief 写相同数据（高效重复数据写入）
/// @param image 镜像句柄
/// @param ofs 写入偏移量
/// @param len 写入长度
/// @param buf 数据缓冲区
/// @param data_len 缓冲区中有效数据长度
/// @param op_flags 操作标志
/// @return 成功时返回写入的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_writesame(rbd_image_t image, uint64_t ofs, size_t len,
                                   const char *buf, size_t data_len,
                                   int op_flags);

/// @brief 写零数据到镜像
/// @param image 镜像句柄
/// @param ofs 写入偏移量
/// @param len 写入长度
/// @param zero_flags 零写入标志（RBD_WRITE_ZEROES_FLAG_*）
/// @param op_flags 操作标志
/// @return 成功时返回写入的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_write_zeroes(rbd_image_t image, uint64_t ofs,
                                      size_t len, int zero_flags,
                                      int op_flags);

/// @brief 比较并写入数据（原子操作）
/// @param image 镜像句柄
/// @param ofs 操作偏移量
/// @param len 操作长度
/// @param cmp_buf 比较缓冲区
/// @param buf 写入缓冲区
/// @param mismatch_off [输出] 如果比较失败，返回第一个不匹配的偏移量
/// @param op_flags 操作标志
/// @return 成功时返回写入的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_compare_and_write(rbd_image_t image, uint64_t ofs,
                                           size_t len, const char *cmp_buf,
                                           const char *buf,
                                           uint64_t *mismatch_off,
                                           int op_flags);

/// @brief 异步写入数据到镜像
/// @param image 镜像句柄
/// @param off 写入偏移量
/// @param len 写入长度
/// @param buf 要写入的数据缓冲区
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_write(rbd_image_t image, uint64_t off, size_t len,
                               const char *buf, rbd_completion_t c);

/**
 * @brief 异步写入数据到镜像（带操作标志）
 * @param op_flags 见 librados.h 中以 LIBRADOS_OP_FLAG 开头的常量
 */
/// @brief 异步写入数据到镜像（带操作标志）
/// @param image 镜像句柄
/// @param off 写入偏移量
/// @param len 写入长度
/// @param buf 要写入的数据缓冲区
/// @param c 异步完成回调
/// @param op_flags 操作标志（见 librados.h 中以 LIBRADOS_OP_FLAG 开头的常量）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_write2(rbd_image_t image, uint64_t off, size_t len,
                                const char *buf, rbd_completion_t c,
                                int op_flags);

/// @brief 异步写入向量数据到镜像
/// @param image 镜像句柄
/// @param iov I/O向量数组
/// @param iovcnt 向量数量
/// @param off 写入偏移量
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_writev(rbd_image_t image, const struct iovec *iov,
                                int iovcnt, uint64_t off, rbd_completion_t c);

/// @brief 异步从镜像读取数据
/// @param image 镜像句柄
/// @param off 读取偏移量
/// @param len 读取长度
/// @param buf 存储数据的缓冲区
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_read(rbd_image_t image, uint64_t off, size_t len,
                              char *buf, rbd_completion_t c);

/**
 * @brief 异步从镜像读取数据（带操作标志）
 * @param op_flags 见 librados.h 中以 LIBRADOS_OP_FLAG 开头的常量
 */
/// @brief 异步从镜像读取数据（带操作标志）
/// @param image 镜像句柄
/// @param off 读取偏移量
/// @param len 读取长度
/// @param buf 存储数据的缓冲区
/// @param c 异步完成回调
/// @param op_flags 操作标志（见 librados.h 中以 LIBRADOS_OP_FLAG 开头的常量）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_read2(rbd_image_t image, uint64_t off, size_t len,
                               char *buf, rbd_completion_t c, int op_flags);

/// @brief 异步从镜像读取向量数据
/// @param image 镜像句柄
/// @param iov I/O向量数组
/// @param iovcnt 向量数量
/// @param off 读取偏移量
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_readv(rbd_image_t image, const struct iovec *iov,
                               int iovcnt, uint64_t off, rbd_completion_t c);

/// @brief 异步丢弃镜像中的数据
/// @param image 镜像句柄
/// @param off 丢弃偏移量
/// @param len 丢弃长度
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_discard(rbd_image_t image, uint64_t off, uint64_t len,
                                 rbd_completion_t c);

/// @brief 异步写相同数据到镜像
/// @param image 镜像句柄
/// @param off 写入偏移量
/// @param len 写入长度
/// @param buf 数据缓冲区
/// @param data_len 缓冲区中有效数据长度
/// @param c 异步完成回调
/// @param op_flags 操作标志
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_writesame(rbd_image_t image, uint64_t off, size_t len,
                                   const char *buf, size_t data_len,
                                   rbd_completion_t c, int op_flags);

/// @brief 异步写零数据到镜像
/// @param image 镜像句柄
/// @param off 写入偏移量
/// @param len 写入长度
/// @param c 异步完成回调
/// @param zero_flags 零写入标志（RBD_WRITE_ZEROES_FLAG_*）
/// @param op_flags 操作标志
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_write_zeroes(rbd_image_t image, uint64_t off,
                                      size_t len, rbd_completion_t c,
                                      int zero_flags, int op_flags);

/// @brief 异步比较并写入数据
/// @param image 镜像句柄
/// @param off 操作偏移量
/// @param len 操作长度
/// @param cmp_buf 比较缓冲区
/// @param buf 写入缓冲区
/// @param c 异步完成回调
/// @param mismatch_off [输出] 如果比较失败，返回第一个不匹配的偏移量
/// @param op_flags 操作标志
/// @return 成功时返回写入的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_aio_compare_and_write(rbd_image_t image,
                                               uint64_t off, size_t len,
                                               const char *cmp_buf,
                                               const char *buf,
                                               rbd_completion_t c,
                                               uint64_t *mismatch_off,
                                               int op_flags);

/// @brief 异步比较并写入向量数据
/// @param image 镜像句柄
/// @param off 操作偏移量
/// @param cmp_iov 比较 I/O向量数组
/// @param cmp_iovcnt 比较向量数量
/// @param iov 写入 I/O向量数组
/// @param iovcnt 写入向量数量
/// @param c 异步完成回调
/// @param mismatch_off [输出] 如果比较失败，返回第一个不匹配的偏移量
/// @param op_flags 操作标志
/// @return 成功时返回写入的字节数，失败时返回负的错误码
CEPH_RBD_API ssize_t rbd_aio_compare_and_writev(rbd_image_t image,
                                                uint64_t off,
                                                const struct iovec *cmp_iov,
                                                int cmp_iovcnt,
                                                const struct iovec *iov,
                                                int iovcnt,
                                                rbd_completion_t c,
                                                uint64_t *mismatch_off,
                                                int op_flags);

/// @brief 创建异步 I/O 完成回调对象
/// @param cb_arg 回调参数
/// @param complete_cb 完成回调函数
/// @param c [输出] 异步完成回调对象
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_create_completion(void *cb_arg,
                                           rbd_callback_t complete_cb,
                                           rbd_completion_t *c);

/// @brief 检查异步操作是否完成
/// @param c 异步完成回调对象
/// @return 操作完成返回 1，未完成返回 0，错误返回负值
CEPH_RBD_API int rbd_aio_is_complete(rbd_completion_t c);

/// @brief 等待异步操作完成
/// @param c 异步完成回调对象
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_wait_for_complete(rbd_completion_t c);

/// @brief 获取异步操作的返回值
/// @param c 异步完成回调对象
/// @return 操作返回值
CEPH_RBD_API ssize_t rbd_aio_get_return_value(rbd_completion_t c);

/// @brief 获取异步操作的回调参数
/// @param c 异步完成回调对象
/// @return 回调参数指针
CEPH_RBD_API void *rbd_aio_get_arg(rbd_completion_t c);

/// @brief 释放异步完成回调对象
/// @param c 异步完成回调对象
CEPH_RBD_API void rbd_aio_release(rbd_completion_t c);

/// @brief 刷新镜像（同步所有待写入数据到磁盘）
/// @param image 镜像句柄
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_flush(rbd_image_t image);

/**
 * @brief 异步刷新镜像（如果启用了缓存）
 *
 * 当当前待处理的写入操作完成并写入磁盘时，获取回调通知。
 *
 * @param image 要刷新写入的镜像
 * @param c 刷新完成时调用的回调
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_aio_flush(rbd_image_t image, rbd_completion_t c);

/**
 * @brief 丢弃镜像的任何缓存数据
 *
 * @param image 要失效缓存数据的镜像
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_invalidate_cache(rbd_image_t image);

/// @brief 轮询 I/O 事件
/// @param image 镜像句柄
/// @param comps 完成回调数组
/// @param numcomp 数组大小
/// @return 成功返回完成的操作数量，失败返回负的错误码
CEPH_RBD_API int rbd_poll_io_events(rbd_image_t image, rbd_completion_t *comps, int numcomp);

/// @brief 获取镜像元数据
/// @param image 镜像句柄
/// @param key 元数据键
/// @param value 存储元数据的缓冲区
/// @param val_len 缓冲区大小（输入时），实际值长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_metadata_get(rbd_image_t image, const char *key, char *value, size_t *val_len);

/// @brief 设置镜像元数据
/// @param image 镜像句柄
/// @param key 元数据键
/// @param value 元数据值
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_metadata_set(rbd_image_t image, const char *key, const char *value);

/// @brief 删除镜像元数据
/// @param image 镜像句柄
/// @param key 元数据键
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_metadata_remove(rbd_image_t image, const char *key);
/**
 * @brief 列出与此镜像关联的所有元数据
 *
 * 此函数遍历所有元数据，key_len 和 val_len 会被填入放入缓冲区的字节数。
 *
 * 如果提供的缓冲区太短，需要的长度仍会被填入，但数据不会被写入，并返回 -ERANGE。
 * 否则，缓冲区会被填入镜像的键和值，每个键值对后跟 '\0'。
 *
 * @param image 要列出元数据的镜像（隐式快照）
 * @param start_after 要开始列出之后的名称（使用空字符串从头开始）
 * @param max 要列出的最大名称数（0 表示无限制）
 * @param keys 存储键的缓冲区
 * @param keys_len 键缓冲区字节数
 * @param values 存储值的缓冲区
 * @param vals_len 值缓冲区字节数
 * @return 成功时返回元数据数量，失败时返回负的错误码
 * @return 如果任一缓冲区太短，返回 -ERANGE
 */
CEPH_RBD_API int rbd_metadata_list(rbd_image_t image, const char *start, uint64_t max,
    char *keys, size_t *key_len, char *values, size_t *vals_len);

/// @brief RBD 镜像镜像支持函数集合
// RBD image mirroring support functions
/// @brief 启用镜像镜像（已废弃）
/// @deprecated 请使用 rbd_mirror_image_enable2
CEPH_RBD_API int rbd_mirror_image_enable(rbd_image_t image) CEPH_RBD_DEPRECATED;

/// @brief 启用镜像镜像
/// @param image 镜像句柄
/// @param mode 镜像模式
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_enable2(rbd_image_t image,
                                          rbd_mirror_image_mode_t mode);

/// @brief 禁用镜像镜像
/// @param image 镜像句柄
/// @param force 是否强制禁用
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_disable(rbd_image_t image, bool force);

/// @brief 提升镜像为主镜像
/// @param image 镜像句柄
/// @param force 是否强制提升
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_promote(rbd_image_t image, bool force);

/// @brief 降级镜像为非主镜像
/// @param image 镜像句柄
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_demote(rbd_image_t image);

/// @brief 重新同步镜像镜像
/// @param image 镜像句柄
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_resync(rbd_image_t image);

/// @brief 为镜像镜像创建快照
/// @param image 镜像句柄
/// @param snap_id [输出] 快照 ID
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_create_snapshot(rbd_image_t image,
                                                  uint64_t *snap_id);

/// @brief 为镜像镜像创建快照（带标志）
/// @param image 镜像句柄
/// @param flags 创建标志
/// @param snap_id [输出] 快照 ID
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_create_snapshot2(rbd_image_t image,
                                                   uint32_t flags,
                                                   uint64_t *snap_id);

/// @brief 获取镜像镜像信息
/// @param image 镜像句柄
/// @param mirror_image_info [输出] 镜像镜像信息结构
/// @param info_size 信息结构大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_get_info(rbd_image_t image,
                                           rbd_mirror_image_info_t *mirror_image_info,
                                           size_t info_size);

/// @brief 清理镜像镜像信息（释放内存）
/// @param mirror_image_info 镜像镜像信息结构指针
CEPH_RBD_API void rbd_mirror_image_get_info_cleanup(
    rbd_mirror_image_info_t *mirror_image_info);

/// @brief 获取镜像镜像模式
/// @param image 镜像句柄
/// @param mode [输出] 镜像模式
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_get_mode(rbd_image_t image,
                                           rbd_mirror_image_mode_t *mode);

/// @brief 获取镜像镜像全局状态
/// @param image 镜像句柄
/// @param mirror_image_global_status [输出] 镜像镜像全局状态结构
/// @param status_size 状态结构大小
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_get_global_status(
    rbd_image_t image,
    rbd_mirror_image_global_status_t *mirror_image_global_status,
    size_t status_size);

/// @brief 清理镜像镜像全局状态（释放内存）
/// @param mirror_image_global_status 镜像镜像全局状态结构指针
CEPH_RBD_API void rbd_mirror_image_global_status_cleanup(
    rbd_mirror_image_global_status_t *mirror_image_global_status);

/// @brief 获取镜像镜像状态（已废弃）
/// @deprecated 请使用 rbd_mirror_image_get_global_status
CEPH_RBD_API int rbd_mirror_image_get_status(
    rbd_image_t image, rbd_mirror_image_status_t *mirror_image_status,
    size_t status_size)
  CEPH_RBD_DEPRECATED;

/// @brief 获取镜像镜像实例 ID
/// @param image 镜像句柄
/// @param instance_id 存储实例 ID 的缓冲区
/// @param id_max_length 缓冲区大小（输入时），实际 ID 长度（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_mirror_image_get_instance_id(rbd_image_t image,
                                                  char *instance_id,
                                                  size_t *id_max_length);

/// @brief 异步提升镜像为主镜像
/// @param image 镜像句柄
/// @param force 是否强制提升
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_mirror_image_promote(rbd_image_t image, bool force,
                                              rbd_completion_t c);

/// @brief 异步降级镜像为非主镜像
/// @param image 镜像句柄
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_mirror_image_demote(rbd_image_t image,
                                             rbd_completion_t c);

/// @brief 异步获取镜像镜像信息
/// @param image 镜像句柄
/// @param mirror_image_info [输出] 镜像镜像信息结构
/// @param info_size 信息结构大小
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_mirror_image_get_info(rbd_image_t image,
                                               rbd_mirror_image_info_t *mirror_image_info,
                                               size_t info_size,
                                               rbd_completion_t c);

/// @brief 异步获取镜像镜像模式
/// @param image 镜像句柄
/// @param mode [输出] 镜像模式
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_mirror_image_get_mode(rbd_image_t image,
                                               rbd_mirror_image_mode_t *mode,
                                               rbd_completion_t c);

/// @brief 异步获取镜像镜像全局状态
/// @param image 镜像句柄
/// @param mirror_global_image_status [输出] 镜像镜像全局状态结构
/// @param status_size 状态结构大小
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_mirror_image_get_global_status(
    rbd_image_t image,
    rbd_mirror_image_global_status_t *mirror_global_image_status,
    size_t status_size, rbd_completion_t c);

/// @brief 异步获取镜像镜像状态（已废弃）
/// @deprecated 请使用 rbd_aio_mirror_image_get_global_status
CEPH_RBD_API int rbd_aio_mirror_image_get_status(
    rbd_image_t image, rbd_mirror_image_status_t *mirror_image_status,
    size_t status_size, rbd_completion_t c)
  CEPH_RBD_DEPRECATED;

/// @brief 异步为镜像镜像创建快照
/// @param image 镜像句柄
/// @param flags 创建标志
/// @param snap_id [输出] 快照 ID
/// @param c 异步完成回调
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_aio_mirror_image_create_snapshot(rbd_image_t image,
                                                      uint32_t flags,
                                                      uint64_t *snap_id,
                                                      rbd_completion_t c);

/// @brief RBD 分组支持函数集合
// RBD groups support functions
/// @brief 创建 RBD 分组
/// @param p I/O 上下文
/// @param name 分组名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_create(rados_ioctx_t p, const char *name);

/// @brief 删除 RBD 分组
/// @param p I/O 上下文
/// @param name 分组名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_remove(rados_ioctx_t p, const char *name);

/// @brief 列出 RBD 分组
/// @param p I/O 上下文
/// @param names 存储分组名称的缓冲区
/// @param size 缓冲区大小（输入时），实际需要的缓冲区大小（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_list(rados_ioctx_t p, char *names, size_t *size);

/// @brief 重命名 RBD 分组
/// @param p I/O 上下文
/// @param src_name 原分组名称
/// @param dest_name 新分组名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_rename(rados_ioctx_t p, const char *src_name,
                                  const char *dest_name);

/// @brief 清理分组信息（释放内存）
/// @param group_info 分组信息结构指针
/// @param group_info_size 结构大小
CEPH_RBD_API int rbd_group_info_cleanup(rbd_group_info_t *group_info,
                                        size_t group_info_size);

/**
 * @brief 注册镜像元数据变更监视器
 *
 * @param image 要监视的镜像
 * @param handle 存储分配给此监视的内部 ID 的位置
 * @param watch_cb 当在此镜像上收到通知时要执行的操作
 * @param arg 要传递给回调的不透明值
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_update_watch(rbd_image_t image, uint64_t *handle,
				  rbd_update_callback_t watch_cb, void *arg);

/**
 * @brief 注销镜像监视器
 *
 * @param image 要取消监视的镜像
 * @param handle 要注销的监视器
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_update_unwatch(rbd_image_t image, uint64_t handle);

/**
 * @brief 列出镜像的所有监视器
 *
 * 监视器将被分配并存储在传递的监视器数组中。如果监视器数量超过 max_watchers，
 * 将返回 -ERANGE，并将监视器数量存储在 max_watchers 中。
 *
 * 调用者在使用监视器列表完成后应调用 rbd_watchers_list_cleanup。
 *
 * @param image 要列出监视器的镜像
 * @param watchers 存储监视器的数组
 * @param max_watchers 监视器数组的容量
 * @return 成功返回 0，失败返回负的错误码
 * @return 如果监视器太多超出数组容量，返回 -ERANGE
 * @return 监视器数量存储在 max_watchers 中
 */
CEPH_RBD_API int rbd_watchers_list(rbd_image_t image,
				   rbd_image_watcher_t *watchers,
				   size_t *max_watchers);

/// @brief 清理监视器列表（释放内存）
/// @param watchers 监视器数组
/// @param num_watchers 数组大小
CEPH_RBD_API void rbd_watchers_list_cleanup(rbd_image_watcher_t *watchers,
					    size_t num_watchers);

/// @brief 列出镜像配置选项
/// @param image 镜像句柄
/// @param options 配置选项数组缓冲区
/// @param max_options 缓冲区大小（输入时），实际选项数量（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_config_image_list(rbd_image_t image,
                                       rbd_config_option_t *options,
                                       int *max_options);

/// @brief 清理镜像配置选项列表（释放内存）
/// @param options 配置选项数组
/// @param max_options 数组大小
CEPH_RBD_API void rbd_config_image_list_cleanup(rbd_config_option_t *options,
                                                int max_options);

/// @brief 将镜像添加到分组
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param image_p 镜像 I/O 上下文
/// @param image_name 镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_image_add(rados_ioctx_t group_p,
                                     const char *group_name,
                                     rados_ioctx_t image_p,
                                     const char *image_name);

/// @brief 从分组中移除镜像（通过名称）
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param image_p 镜像 I/O 上下文
/// @param image_name 镜像名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_image_remove(rados_ioctx_t group_p,
                                        const char *group_name,
                                        rados_ioctx_t image_p,
                                        const char *image_name);

/// @brief 从分组中移除镜像（通过 ID）
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param image_p 镜像 I/O 上下文
/// @param image_id 镜像 ID
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_image_remove_by_id(rados_ioctx_t group_p,
                                              const char *group_name,
                                              rados_ioctx_t image_p,
                                              const char *image_id);

/// @brief 列出分组中的镜像
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param images [输出] 分组镜像信息数组
/// @param group_image_info_size 信息结构大小
/// @param num_entries [输入/输出] 缓冲区大小 / 实际镜像数量
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_image_list(rados_ioctx_t group_p,
                                      const char *group_name,
                                      rbd_group_image_info_t *images,
                                      size_t group_image_info_size,
                                      size_t *num_entries);

/// @brief 清理分组镜像列表（释放内存）
/// @param images 分组镜像信息数组
/// @param group_image_info_size 信息结构大小
/// @param num_entries 数组大小
CEPH_RBD_API int rbd_group_image_list_cleanup(rbd_group_image_info_t *images,
                                              size_t group_image_info_size,
                                              size_t num_entries);

/// @brief 创建分组快照
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param snap_name 快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_snap_create(rados_ioctx_t group_p,
                                       const char *group_name,
                                       const char *snap_name);

/// @brief 创建分组快照（带标志）
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param snap_name 快照名称
/// @param flags 创建标志
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_snap_create2(rados_ioctx_t group_p,
                                        const char *group_name,
                                        const char *snap_name,
                                        uint32_t flags);

/// @brief 删除分组快照
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param snap_name 快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_snap_remove(rados_ioctx_t group_p,
                                       const char *group_name,
                                       const char *snap_name);

/// @brief 重命名分组快照
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param old_snap_name 原快照名称
/// @param new_snap_name 新快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_snap_rename(rados_ioctx_t group_p,
                                       const char *group_name,
                                       const char *old_snap_name,
                                       const char *new_snap_name);

/// @brief 列出分组快照
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param snaps [输出] 分组快照信息数组
/// @param group_snap_info_size 信息结构大小
/// @param num_entries [输入/输出] 缓冲区大小 / 实际快照数量
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_snap_list(rados_ioctx_t group_p,
                                     const char *group_name,
                                     rbd_group_snap_info_t *snaps,
                                     size_t group_snap_info_size,
                                     size_t *num_entries);

/// @brief 清理分组快照列表（释放内存）
/// @param snaps 分组快照信息数组
/// @param group_snap_info_size 信息结构大小
/// @param num_entries 数组大小
CEPH_RBD_API int rbd_group_snap_list_cleanup(rbd_group_snap_info_t *snaps,
                                             size_t group_snap_info_size,
                                             size_t num_entries);

/// @brief 回滚分组到快照
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param snap_name 快照名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_snap_rollback(rados_ioctx_t group_p,
                                         const char *group_name,
                                         const char *snap_name);

/// @brief 回滚分组到快照（带进度回调）
/// @param group_p 分组 I/O 上下文
/// @param group_name 分组名称
/// @param snap_name 快照名称
/// @param cb 进度回调函数
/// @param cbdata 回调函数参数
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_group_snap_rollback_with_progress(rados_ioctx_t group_p,
                                                       const char *group_name,
                                                       const char *snap_name,
                                                       librbd_progress_fn_t cb,
                                                       void *cbdata);

/// @brief 创建命名空间
/// @param io I/O 上下文
/// @param namespace_name 命名空间名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_namespace_create(rados_ioctx_t io,
                                      const char *namespace_name);

/// @brief 删除命名空间
/// @param io I/O 上下文
/// @param namespace_name 命名空间名称
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_namespace_remove(rados_ioctx_t io,
                                      const char *namespace_name);

/// @brief 列出命名空间
/// @param io I/O 上下文
/// @param namespace_names 存储命名空间名称的缓冲区
/// @param size 缓冲区大小（输入时），实际需要的缓冲区大小（输出时）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_namespace_list(rados_ioctx_t io, char *namespace_names,
                                    size_t *size);

/// @brief 检查命名空间是否存在
/// @param io I/O 上下文
/// @param namespace_name 命名空间名称
/// @param exists [输出] 是否存在（1 表示存在，0 表示不存在）
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_namespace_exists(rados_ioctx_t io,
                                      const char *namespace_name,
                                      bool *exists);

/// @brief 初始化 RBD 池
/// @param io I/O 上下文
/// @param force 是否强制初始化
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_pool_init(rados_ioctx_t io, bool force);

/// @brief 创建池统计对象
/// @param stats 池统计对象指针
CEPH_RBD_API void rbd_pool_stats_create(rbd_pool_stats_t *stats);

/// @brief 销毁池统计对象
/// @param stats 池统计对象
CEPH_RBD_API void rbd_pool_stats_destroy(rbd_pool_stats_t stats);

/// @brief 向池统计对象添加无符号 64 位整数选项
/// @param stats 池统计对象
/// @param stat_option 统计选项
/// @param stat_val 统计值指针
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_pool_stats_option_add_uint64(rbd_pool_stats_t stats,
					          int stat_option,
                                                  uint64_t* stat_val);

/// @brief 获取池统计信息
/// @param io I/O 上下文
/// @param stats 池统计对象
/// @return 成功返回 0，失败返回负的错误码
CEPH_RBD_API int rbd_pool_stats_get(rados_ioctx_t io, rbd_pool_stats_t stats);

/**
 * @brief 注册静默/取消静默监视器
 *
 * @param image 要监视的镜像
 * @param quiesce_cb 当 librbd 想要静默时要执行的操作
 * @param unquiesce_cb 当 librbd 想要取消静默时要执行的操作
 * @param arg 要传递给回调的不透明值
 * @param handle 存储分配给此监视的内部 ID 的位置
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_quiesce_watch(rbd_image_t image,
                                   rbd_update_callback_t quiesce_cb,
                                   rbd_update_callback_t unquiesce_cb,
                                   void *arg, uint64_t *handle);

/**
 * @brief 通知静默完成
 *
 * @param image 要通知的镜像
 * @param handle 哪个监视完成了
 * @param r 返回码
 */
CEPH_RBD_API void rbd_quiesce_complete(rbd_image_t image, uint64_t handle,
                                       int r);

/**
 * @brief 注销静默/取消静默监视器
 *
 * @param image 要取消监视的镜像
 * @param handle 要注销的监视器
 * @return 成功返回 0，失败返回负的错误码
 */
CEPH_RBD_API int rbd_quiesce_unwatch(rbd_image_t image, uint64_t handle);

#if __GNUC__ >= 4
  #pragma GCC diagnostic pop
#endif

#ifdef __cplusplus
}
#endif

#endif /* CEPH_LIBRBD_H */
