// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/**
 * @file features.h
 * @brief RBD (RADOS Block Device) 功能特性定义
 *
 * 本文件定义了 RBD 镜像支持的各种功能特性，包括：
 * - 基础功能：分层、条带化、独占锁等
 * - 高级功能：对象映射、快速差异、深度扁平化等
 * - 操作功能：克隆父级、克隆子级、分组、快照垃圾回收等
 *
 * 这些特性通过位标志表示，可以组合使用以启用不同的功能集合。
 */

#ifndef CEPH_RBD_FEATURES_H
#define CEPH_RBD_FEATURES_H

// RBD 基础功能特性定义
/// @brief 分层支持 - 允许镜像克隆和快照
#define RBD_FEATURE_LAYERING            (1ULL<<0)
/// @brief 条带化 v2 - 改进的对象条带化布局
#define RBD_FEATURE_STRIPINGV2          (1ULL<<1)
/// @brief 独占锁 - 确保同一时间只有一个客户端可以写入镜像
#define RBD_FEATURE_EXCLUSIVE_LOCK      (1ULL<<2)
/// @brief 对象映射 - 跟踪镜像中的对象分配
#define RBD_FEATURE_OBJECT_MAP          (1ULL<<3)
/// @brief 快速差异 - 高效计算镜像差异
#define RBD_FEATURE_FAST_DIFF           (1ULL<<4)
/// @brief 深度扁平化 - 支持克隆镜像的深度扁平化操作
#define RBD_FEATURE_DEEP_FLATTEN        (1ULL<<5)
/// @brief 日志记录 - 为镜像操作提供持久化日志
#define RBD_FEATURE_JOURNALING          (1ULL<<6)
/// @brief 数据池分离 - 将镜像数据存储在单独的池中
#define RBD_FEATURE_DATA_POOL           (1ULL<<7)
/// @brief 内部操作 - 镜像内部操作支持
#define RBD_FEATURE_OPERATIONS          (1ULL<<8)
/// @brief 迁移支持 - 支持镜像在线迁移
#define RBD_FEATURE_MIGRATING           (1ULL<<9)
/// @brief 非主站点 - 支持镜像镜像的非主站点操作
#define RBD_FEATURE_NON_PRIMARY         (1ULL<<10)
/// @brief 脏缓存 - 支持缓存脏数据跟踪
#define RBD_FEATURE_DIRTY_CACHE         (1ULL<<11)

/// @brief 默认启用的功能集合 - 提供基本的分层、锁、映射和差异功能
#define RBD_FEATURES_DEFAULT             (RBD_FEATURE_LAYERING | \
                                         RBD_FEATURE_EXCLUSIVE_LOCK | \
                                         RBD_FEATURE_OBJECT_MAP | \
                                         RBD_FEATURE_FAST_DIFF | \
                                         RBD_FEATURE_DEEP_FLATTEN)

// RBD 功能名称字符串定义（用于显示和配置）
#define RBD_FEATURE_NAME_LAYERING        "layering"
#define RBD_FEATURE_NAME_STRIPINGV2      "striping"
#define RBD_FEATURE_NAME_EXCLUSIVE_LOCK  "exclusive-lock"
#define RBD_FEATURE_NAME_OBJECT_MAP      "object-map"
#define RBD_FEATURE_NAME_FAST_DIFF       "fast-diff"
#define RBD_FEATURE_NAME_DEEP_FLATTEN    "deep-flatten"
#define RBD_FEATURE_NAME_JOURNALING      "journaling"
#define RBD_FEATURE_NAME_DATA_POOL       "data-pool"
#define RBD_FEATURE_NAME_OPERATIONS      "operations"
#define RBD_FEATURE_NAME_MIGRATING       "migrating"
#define RBD_FEATURE_NAME_NON_PRIMARY     "non-primary"
#define RBD_FEATURE_NAME_DIRTY_CACHE     "dirty-cache"

/// @brief 不兼容功能集合 - 使镜像无法被不支持这些功能的客户端读写
/// @details 包括分层、条带化、数据池分离和脏缓存等基础功能
#define RBD_FEATURES_INCOMPATIBLE       (RBD_FEATURE_LAYERING       | \
                                         RBD_FEATURE_STRIPINGV2     | \
                                         RBD_FEATURE_DATA_POOL      | \
                                         RBD_FEATURE_DIRTY_CACHE)

/// @brief 读写不兼容功能集合 - 使镜像无法被不支持这些功能的客户端写入
/// @details 包含所有不兼容功能加上独占锁、对象映射、快速差异等高级功能
#define RBD_FEATURES_RW_INCOMPATIBLE    (RBD_FEATURES_INCOMPATIBLE  | \
                                         RBD_FEATURE_EXCLUSIVE_LOCK | \
                                         RBD_FEATURE_OBJECT_MAP     | \
                                         RBD_FEATURE_FAST_DIFF      | \
                                         RBD_FEATURE_DEEP_FLATTEN   | \
                                         RBD_FEATURE_JOURNALING     | \
                                         RBD_FEATURE_OPERATIONS     | \
                                         RBD_FEATURE_MIGRATING      | \
                                         RBD_FEATURE_NON_PRIMARY)

/// @brief 所有可用功能集合 - 包含所有支持的功能特性
#define RBD_FEATURES_ALL                (RBD_FEATURE_LAYERING       | \
                                         RBD_FEATURE_STRIPINGV2     | \
                                         RBD_FEATURE_EXCLUSIVE_LOCK | \
                                         RBD_FEATURE_OBJECT_MAP     | \
                                         RBD_FEATURE_FAST_DIFF      | \
                                         RBD_FEATURE_DEEP_FLATTEN   | \
                                         RBD_FEATURE_JOURNALING     | \
                                         RBD_FEATURE_DATA_POOL      | \
                                         RBD_FEATURE_OPERATIONS     | \
                                         RBD_FEATURE_MIGRATING      | \
                                         RBD_FEATURE_NON_PRIMARY    | \
                                         RBD_FEATURE_DIRTY_CACHE)

/// @brief 可变功能集合 - 可以在运行时动态启用或禁用的功能
/// @details 包括独占锁、对象映射、快速差异、日志、非主站点和脏缓存
#define RBD_FEATURES_MUTABLE            (RBD_FEATURE_EXCLUSIVE_LOCK | \
                                         RBD_FEATURE_OBJECT_MAP     | \
                                         RBD_FEATURE_FAST_DIFF      | \
                                         RBD_FEATURE_JOURNALING     | \
                                         RBD_FEATURE_NON_PRIMARY    | \
                                         RBD_FEATURE_DIRTY_CACHE)

/// @brief 内部可变功能集合 - 仅供内部使用的可变功能
#define RBD_FEATURES_MUTABLE_INTERNAL   (RBD_FEATURE_NON_PRIMARY    | \
                                         RBD_FEATURE_DIRTY_CACHE)

/// @brief 仅禁用功能集合 - 只可以禁用的功能，不能重新启用
#define RBD_FEATURES_DISABLE_ONLY       (RBD_FEATURE_DEEP_FLATTEN)

/// @brief 单客户端功能集合 - 仅在单一客户端写入时正常工作的功能
/// @details 包括独占锁、对象映射、快速差异、日志和脏缓存功能
#define RBD_FEATURES_SINGLE_CLIENT (RBD_FEATURE_EXCLUSIVE_LOCK | \
                                    RBD_FEATURE_OBJECT_MAP     | \
                                    RBD_FEATURE_FAST_DIFF      | \
                                    RBD_FEATURE_JOURNALING     | \
                                    RBD_FEATURE_DIRTY_CACHE)

/// @brief 隐式启用功能集合 - 这些功能会被自动启用，不需要用户显式指定
/// @details 包括条带化、数据池分离、快速差异、操作、迁移、非主站点和脏缓存
#define RBD_FEATURES_IMPLICIT_ENABLE  (RBD_FEATURE_STRIPINGV2  | \
                                       RBD_FEATURE_DATA_POOL   | \
                                       RBD_FEATURE_FAST_DIFF   | \
                                       RBD_FEATURE_OPERATIONS  | \
                                       RBD_FEATURE_MIGRATING   | \
                                       RBD_FEATURE_NON_PRIMARY | \
                                       RBD_FEATURE_DIRTY_CACHE)

/// @brief 内部功能集合 - 用户无法控制的内部功能特性
#define RBD_FEATURES_INTERNAL         (RBD_FEATURE_OPERATIONS | \
                                       RBD_FEATURE_MIGRATING)

// RBD 操作功能特性定义（用于镜像操作支持）
/// @brief 克隆父级操作支持 - 支持作为父镜像进行克隆操作
#define RBD_OPERATION_FEATURE_CLONE_PARENT      (1ULL<<0)
/// @brief 克隆子级操作支持 - 支持作为子镜像进行克隆操作
#define RBD_OPERATION_FEATURE_CLONE_CHILD       (1ULL<<1)
/// @brief 分组操作支持 - 支持镜像分组操作
#define RBD_OPERATION_FEATURE_GROUP             (1ULL<<2)
/// @brief 快照垃圾回收操作支持 - 支持快照垃圾回收操作
#define RBD_OPERATION_FEATURE_SNAP_TRASH        (1ULL<<3)

// RBD 操作功能名称字符串定义
#define RBD_OPERATION_FEATURE_NAME_CLONE_PARENT "clone-parent"
#define RBD_OPERATION_FEATURE_NAME_CLONE_CHILD  "clone-child"
#define RBD_OPERATION_FEATURE_NAME_GROUP        "group"
#define RBD_OPERATION_FEATURE_NAME_SNAP_TRASH   "snap-trash"

/// @brief 所有有效的操作功能集合 - 包含所有支持的操作功能特性
#define RBD_OPERATION_FEATURES_ALL (RBD_OPERATION_FEATURE_CLONE_PARENT | \
                                    RBD_OPERATION_FEATURE_CLONE_CHILD  | \
                                    RBD_OPERATION_FEATURE_GROUP        | \
                                    RBD_OPERATION_FEATURE_SNAP_TRASH)

#endif
