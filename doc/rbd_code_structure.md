# RBD (RADOS Block Device) 代码详解

## 📋 目录
1. [RBD 整体架构](#一rbd-整体架构)
2. [代码目录结构](#二代码目录结构)
3. [核心数据结构](#三核心数据结构)
4. [I/O 数据路径](#四io-数据路径)
5. [快照与克隆](#五快照与克隆)
6. [缓存机制](#六缓存机制)
7. [镜像与复制](#七镜像与复制)
8. [性能优化](#八性能优化)

---

## 一、RBD 整体架构

### 1.1 RBD 在 Ceph 中的位置

```
应用层：虚拟机/容器
        │
        ▼
    /dev/rbd0 (块设备)
        │
        ├─ 内核模块 (krbd)
        │  └─ drivers/block/rbd.c
        │
        └─ 用户态库 (librbd)
           └─ src/librbd/
        │
        ▼
    librados (RADOS 客户端)
        │
        ▼
    RADOS (OSD 集群)
```

### 1.2 RBD 核心概念

#### 对象映射

```
RBD Image (块设备)
┌─────────────────────────────────────────────────────┐
│  虚拟块设备：10 GB                                   │
│  条带大小：4 MB (默认)                              │
└─────────────────────────────────────────────────────┘
         │
         │ 映射到
         ▼
┌─────────────────────────────────────────────────────┐
│  RADOS 对象                                          │
│                                                      │
│  rbd_data.<image_id>.0000000000000000  (4MB)        │
│  rbd_data.<image_id>.0000000000000001  (4MB)        │
│  rbd_data.<image_id>.0000000000000002  (4MB)        │
│  ...                                                 │
│  rbd_data.<image_id>.0000000000000a3f  (4MB)        │
│                                                      │
│  共 2560 个对象 (10GB / 4MB)                        │
└─────────────────────────────────────────────────────┘
```

#### 元数据对象

```
Pool: rbd
  │
  ├─ rbd_header.<image_id>          # Image 元数据
  │  • size, features, flags
  │  • object_prefix
  │  • parent 信息
  │  • 快照列表
  │
  ├─ rbd_id.<image_name>            # Name → ID 映射
  │
  ├─ rbd_object_map.<image_id>      # Object Map (可选)
  │  • 每个对象的状态位图
  │  • EXISTS, PENDING, ...
  │
  ├─ rbd_data.<image_id>.<object_no>  # 实际数据
  │
  └─ rbd_directory                   # Image 列表目录
```

### 1.3 特性标志 (Features)

```cpp
// src/librbd/Features.h
enum {
  RBD_FEATURE_LAYERING            = (1<<0),  // 分层/克隆
  RBD_FEATURE_STRIPINGV2          = (1<<1),  // 高级条带化
  RBD_FEATURE_EXCLUSIVE_LOCK      = (1<<2),  // 排他锁
  RBD_FEATURE_OBJECT_MAP          = (1<<3),  // 对象映射
  RBD_FEATURE_FAST_DIFF           = (1<<4),  // 快速差异计算
  RBD_FEATURE_DEEP_FLATTEN        = (1<<5),  // 深度扁平化
  RBD_FEATURE_JOURNALING          = (1<<6),  // 日志记录
  RBD_FEATURE_DATA_POOL           = (1<<7),  // 数据池分离
  RBD_FEATURE_OPERATIONS          = (1<<8),  // 操作记录
  RBD_FEATURE_MIGRATING           = (1<<9),  // 迁移标记
  RBD_FEATURE_NON_PRIMARY         = (1<<10), // 非主镜像
  RBD_FEATURE_DIRTY_CACHE         = (1<<11), // 脏缓存
  RBD_FEATURE_ENCRYPTION          = (1<<12), // 加密
};
```

---

## 二、代码目录结构

### 2.1 src/librbd/ 核心目录

```
src/librbd/
├── librbd.cc                    # C API 入口 (7600+ 行)
├── ImageCtx.h/cc                # Image 上下文 (核心数据结构)
├── ImageState.h/cc              # Image 状态机
├── Operations.h/cc              # Image 操作 (resize, flatten, ...)
│
├── api/                         # 高层 API
│   ├── Image.h/cc              # Image 管理
│   ├── Io.h/cc                 # I/O 操作
│   ├── Snapshot.h/cc           # 快照管理
│   ├── Pool.h/cc               # Pool 管理
│   ├── Mirror.h/cc             # 镜像复制
│   ├── Migration.h/cc          # 迁移功能
│   └── ...
│
├── io/                          # I/O 路径 (49 文件)
│   ├── ImageRequest.h/cc       # Image 层请求
│   ├── ObjectRequest.h/cc      # Object 层请求
│   ├── ImageDispatcher.h/cc    # Image I/O 调度
│   ├── ObjectDispatcher.h/cc   # Object I/O 调度
│   ├── AioCompletion.h/cc      # 异步 I/O 完成
│   ├── CopyupRequest.h/cc      # Copyup 请求 (分层)
│   ├── QosImageDispatch.h/cc   # QoS 调度
│   ├── Types.h                 # I/O 类型定义
│   └── ...
│
├── cache/                       # 缓存层
│   ├── pwl/                    # Persistent Write Log (47 文件)
│   │   ├── WriteLog.h/cc       # 持久化写日志
│   │   ├── SyncPoint.h/cc      # 同步点
│   │   └── ...
│   ├── ImageWriteback.h/cc     # Image 回写
│   ├── ObjectCacherObjectDispatch.h/cc
│   ├── WriteLogImageDispatch.h/cc
│   └── ...
│
├── exclusive_lock/              # 排他锁 (8 文件)
│   ├── AutomaticPolicy.h/cc    # 自动锁策略
│   ├── StandardPolicy.h/cc     # 标准锁策略
│   ├── PreAcquireRequest.h/cc  # 获取锁前处理
│   └── PostAcquireRequest.h/cc # 获取锁后处理
│
├── image/                       # Image 操作 (36 文件)
│   ├── OpenRequest.h/cc        # 打开 Image
│   ├── CloseRequest.h/cc       # 关闭 Image
│   ├── CreateRequest.h/cc      # 创建 Image
│   ├── RemoveRequest.h/cc      # 删除 Image
│   ├── ResizeRequest.h/cc      # 调整大小
│   ├── RefreshRequest.h/cc     # 刷新元数据
│   └── ...
│
├── operation/                   # 操作请求 (40 文件)
│   ├── SnapshotCreateRequest.h/cc
│   ├── SnapshotRemoveRequest.h/cc
│   ├── SnapshotRollbackRequest.h/cc
│   ├── FlattenRequest.h/cc
│   ├── RebuildObjectMapRequest.h/cc
│   └── ...
│
├── journal/                     # 日志 (25 文件)
│   ├── Replay.h/cc             # 日志重放
│   ├── Types.h                 # 日志类型
│   └── ...
│
├── mirror/                      # 镜像复制 (43 文件)
│   ├── ImageSync.h/cc          # Image 同步
│   ├── SnapshotCopyRequest.h/cc
│   └── ...
│
├── object_map/                  # Object Map (27 文件)
│   ├── CreateRequest.h/cc
│   ├── UpdateRequest.h/cc
│   └── ...
│
├── managed_lock/                # 分布式锁 (13 文件)
│   ├── AcquireRequest.h/cc     # 获取锁
│   ├── ReleaseRequest.h/cc     # 释放锁
│   ├── BreakRequest.h/cc       # 打破锁
│   └── ...
│
├── crypto/                      # 加密 (24 文件)
│   ├── luks/                   # LUKS 加密
│   ├── CryptoObjectDispatch.h/cc
│   └── ...
│
├── deep_copy/                   # 深度复制 (17 文件)
│   ├── ImageCopyRequest.h/cc
│   ├── ObjectCopyRequest.h/cc
│   └── ...
│
├── migration/                   # 迁移 (32 文件)
│   └── ...
│
└── trash/                       # 回收站 (4 文件)
    └── ...

总计：~265 个头文件，~250 个源文件
```

### 2.2 内核模块

```
drivers/block/rbd.c              # Linux 内核 RBD 驱动
  • 约 7000+ 行 C 代码
  • 实现 /dev/rbd* 块设备
  • 使用 libceph 通信
```

### 2.3 相关工具

```
src/tools/rbd/
├── action/
│   ├── Bench.h/cc              # 性能测试
│   ├── Clone.h/cc              # 克隆
│   ├── Create.h/cc             # 创建
│   ├── Export.h/cc             # 导出
│   ├── Import.h/cc             # 导入
│   ├── Snap.h/cc               # 快照
│   └── ...
└── Shell.cc                     # rbd 命令行工具
```

---

## 三、核心数据结构

### 3.1 ImageCtx (Image 上下文)

```cpp
// src/librbd/ImageCtx.h
struct ImageCtx {
  // ══════════════════════════════════════════════════════
  // 基本信息
  // ══════════════════════════════════════════════════════
  
  CephContext *cct;                     // Ceph 上下文
  
  std::string name;                     // Image 名称
  std::string id;                       // Image ID (UUID)
  std::string header_oid;               // 头对象名
  std::string object_prefix;            // 对象前缀
  
  uint64_t size;                        // Image 大小
  uint64_t features;                    // 特性标志
  uint8_t order;                        // 对象大小 order (2^order)
  bool old_format;                      // 是否旧格式
  
  // ══════════════════════════════════════════════════════
  // RADOS 接口
  // ══════════════════════════════════════════════════════
  
  librados::IoCtx data_ctx;             // 数据 Pool
  librados::IoCtx md_ctx;               // 元数据 Pool
  neorados::RADOS& rados_api;           // 新 RADOS API
  
  // ══════════════════════════════════════════════════════
  // 快照相关
  // ══════════════════════════════════════════════════════
  
  ::SnapContext snapc;                  // 快照上下文
  std::vector<librados::snap_t> snaps;  // 快照列表
  std::map<librados::snap_t, SnapInfo> snap_info;  // 快照信息
  std::map<SnapKey, librados::snap_t> snap_ids;    // 快照 ID 映射
  uint64_t snap_id;                     // 当前快照 ID
  bool snap_exists;                     // 快照是否存在
  
  // ══════════════════════════════════════════════════════
  // 分层 (Layering)
  // ══════════════════════════════════════════════════════
  
  ParentImageInfo parent_md;            // 父镜像信息
  ImageCtx *parent;                     // 父 ImageCtx
  ImageCtx *child;                      // 子 ImageCtx
  
  // ══════════════════════════════════════════════════════
  // 锁与同步
  // ══════════════════════════════════════════════════════
  
  ceph::shared_mutex owner_lock;        // 所有权锁
  ceph::shared_mutex image_lock;        // Image 锁
  ceph::shared_mutex timestamp_lock;    // 时间戳锁
  ceph::mutex async_ops_lock;           // 异步操作锁
  ceph::mutex copyup_list_lock;         // Copyup 列表锁
  
  bool exclusive_locked;                // 是否持有排他锁
  std::string lock_tag;                 // 锁标签
  
  // ══════════════════════════════════════════════════════
  // I/O 相关
  // ══════════════════════════════════════════════════════
  
  io::ImageDispatcherInterface *io_image_dispatcher;
  io::ObjectDispatcherInterface *io_object_dispatcher;
  
  xlist<io::AsyncOperation*> async_ops;              // 异步操作
  std::map<uint64_t, io::CopyupRequest*> copyup_list;  // Copyup 请求
  
  Readahead readahead;                  // 预读
  std::atomic<uint64_t> total_bytes_read;
  
  // ══════════════════════════════════════════════════════
  // 组件
  // ══════════════════════════════════════════════════════
  
  ImageState *state;                    // 状态机
  Operations *operations;               // 操作
  ExclusiveLock *exclusive_lock;        // 排他锁
  ObjectMap *object_map;                // 对象映射
  Journal *journal;                     // 日志
  ImageWatcher *image_watcher;          // 观察者
  
  // ══════════════════════════════════════════════════════
  // 性能计数器
  // ══════════════════════════════════════════════════════
  
  PerfCounters *perfcounter;            // 性能统计
  
  // ══════════════════════════════════════════════════════
  // 高级特性
  // ══════════════════════════════════════════════════════
  
  uint64_t stripe_unit, stripe_count;   // 条带化参数
  uint64_t flags;                       // 标志
  uint64_t op_features;                 // 操作特性
  
  file_layout_t layout;                 // 文件布局
  
  MigrationInfo migration_info;         // 迁移信息
  cls::rbd::GroupSpec group_spec;       // 组信息
  
  crypto::EncryptionFormat* encryption_format;  // 加密格式
};
```

### 3.2 I/O 请求结构

```cpp
// src/librbd/io/ImageDispatchSpec.h
class ImageDispatchSpec {
  // I/O 操作类型
  struct Read {
    ReadResult read_result;
    int read_flags;
  };
  
  struct Write {
    bufferlist bl;
  };
  
  struct Discard {
    uint32_t discard_granularity_bytes;
  };
  
  struct WriteSame {
    bufferlist bl;
  };
  
  struct CompareAndWrite {
    bufferlist cmp_bl;
    bufferlist bl;
    uint64_t *mismatch_offset;
  };
  
  struct Flush {
    FlushSource flush_source;
  };
  
  struct ListSnaps {
    SnapIds snap_ids;
    int list_snaps_flags;
    SnapshotDelta* snapshot_delta;
  };
  
  // 请求参数
  uint64_t offset;                      // 偏移量
  uint64_t length;                      // 长度
  AioCompletion *aio_comp;              // 异步完成回调
  ZTracer::Trace trace;                 // Tracing
  
  // Variant 保存具体操作
  boost::variant<Read, Write, Discard, ...> request;
};
```

### 3.3 ObjectRequest (对象请求)

```cpp
// src/librbd/io/ObjectRequest.h
template <typename ImageCtxT>
class ObjectRequest {
protected:
  ImageCtxT *m_ictx;                    // Image 上下文
  uint64_t m_object_no;                 // 对象编号
  IOContext m_io_context;               // I/O 上下文
  Context *m_completion;                // 完成回调
  ZTracer::Trace m_trace;               // Tracing
  
  bool m_has_parent;                    // 是否有父镜像
  
public:
  virtual void send() = 0;              // 发送请求
  virtual const char *get_op_type() const = 0;
  
  // 子类：
  // • ObjectReadRequest
  // • ObjectWriteRequest
  // • ObjectDiscardRequest
  // • ObjectWriteSameRequest
  // • ObjectCompareAndWriteRequest
};
```

### 3.4 AioCompletion (异步完成)

```cpp
// src/librbd/io/AioCompletion.h
class AioCompletion {
  // 引用计数
  std::atomic<uint32_t> ref;
  std::atomic<uint32_t> rbd_comp_ref;
  
  // 回调
  callback_t complete_cb;
  void *complete_arg;
  
  // 状态
  std::atomic<ssize_t> rval;            // 返回值
  std::atomic<int> error_rval;          // 错误码
  std::atomic<uint32_t> state;          // 状态
  std::atomic<uint32_t> pending_count;  // 待处理计数
  
  // 读取结果
  ReadResult read_result;
  
  // 锁
  ceph::mutex lock;
  ceph::condition_variable cond;
  
public:
  void complete();                      // 完成
  void wait_for_complete();             // 等待完成
  ssize_t get_return_value();           // 获取返回值
  
  void add_request();                   // 添加请求
  void complete_request(ssize_t r);     // 完成请求
};
```

---

## 四、I/O 数据路径

### 4.1 I/O 路径概览

```
┌─────────────────────────────────────────────────────────────┐
│             I/O 请求处理流程                                  │
└─────────────────────────────────────────────────────────────┘

应用层 API 调用
   │
   │ rbd_write(image, offset, length, data)
   ▼
┌─────────────────────────────────────────────────────────────┐
│ librbd API 层 (librbd.cc)                                    │
│ • 参数验证                                                   │
│ • 创建 AioCompletion                                         │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ api::Io 层 (api/Io.cc)                                       │
│ • 构造 ImageDispatchSpec                                     │
│ • 调用 io_image_dispatcher->send()                           │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ ImageDispatcher 层 (io/ImageDispatcher.cc)                  │
│                                                              │
│ 调度链：                                                      │
│   ImageDispatch (链式处理器)                                 │
│   ├─> QosImageDispatch          (QoS 限流)                  │
│   ├─> WriteBlockImageDispatch   (写阻塞)                    │
│   ├─> RefreshImageDispatch      (元数据刷新)                │
│   ├─> ExclusiveLockImageDispatch (排他锁检查)               │
│   ├─> ObjectCacherObjectDispatch (对象缓存)                 │
│   ├─> WriteLogImageDispatch     (持久化写日志)              │
│   └─> CryptoObjectDispatch      (加密)                      │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ ImageRequest 层 (io/ImageRequest.cc)                        │
│ • 地址映射：Image offset → Object list                      │
│ • 切分请求为多个 ObjectRequest                               │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ ObjectDispatcher 层 (io/ObjectDispatcher.cc)                │
│                                                              │
│ 调度链：                                                      │
│   ObjectDispatch (链式处理器)                                │
│   ├─> CryptoObjectDispatch       (加密)                     │
│   ├─> WriteAroundObjectDispatch  (写绕过)                   │
│   └─> SimpleSchedulerObjectDispatch (调度)                  │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ ObjectRequest 层 (io/ObjectRequest.cc)                      │
│ • 处理分层 (Layering)                                        │
│ • 处理 Copyup                                                │
│ • 更新 Object Map                                            │
│ • 构造 librados::ObjectWriteOperation                        │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ librados (客户端库)                                          │
│ • CRUSH 计算                                                 │
│ • 连接 OSD                                                   │
│ • 发送 RADOS 操作                                            │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
                     OSD 集群
```

### 4.2 写入流程详解

```cpp
// 第一步：API 调用
// ═══════════════════════════════════════════════════════════

// librbd.cc
int rbd_write(rbd_image_t image, uint64_t ofs, size_t len,
              const char *buf)
{
  librbd::ImageCtx *ictx = (librbd::ImageCtx *)image;
  
  // 创建异步完成对象
  librbd::RBD::AioCompletion *c = new librbd::RBD::AioCompletion();
  
  // 异步写入
  int r = librbd::api::Io<>::aio_write(
    *ictx, c->pc, ofs, len, bufferlist::static_from_mem(buf, len),
    0, true);
  
  // 等待完成
  if (r == 0) {
    c->wait_for_complete();
    r = c->get_return_value();
  }
  
  c->release();
  return r;
}

// 第二步：构造 ImageDispatchSpec
// ═══════════════════════════════════════════════════════════

// api/Io.cc
void aio_write(ImageCtx& ictx, AioCompletion* aio_comp,
               uint64_t off, uint64_t len, bufferlist&& bl, int op_flags)
{
  // 创建 ImageDispatchSpec
  auto req = io::ImageDispatchSpec::create_write(
    ictx, io::IMAGE_DISPATCH_LAYER_API_START, aio_comp,
    {{off, len}}, std::move(bl), op_flags, {});
  
  // 发送到 ImageDispatcher
  req->send();
}

// 第三步：ImageDispatcher 调度
// ═══════════════════════════════════════════════════════════

// io/ImageDispatcher.cc
void ImageDispatcher::send(ImageDispatchSpec* spec) {
  // 遍历 Dispatch 链
  for (auto dispatch : m_dispatches) {
    if (dispatch->write(...)) {
      // 某个 Dispatch 处理了请求
      return;
    }
  }
  
  // 最终到达 ImageDispatch
  // 转发到 ImageRequest
  auto req = ImageRequest::create_write_request(
    ictx, spec->aio_comp, spec->image_extents, ...);
  req->send();
}

// 第四步：Image → Object 映射
// ═══════════════════════════════════════════════════════════

// io/ImageRequest.cc
void ImageWriteRequest::send_request() {
  // 计算涉及的对象
  // offset = 8MB, length = 8MB
  // object_size = 4MB (order=22, 2^22=4MB)
  
  // 对象 2: [8MB, 12MB)  -> [0, 4MB)
  // 对象 3: [12MB, 16MB) -> [0, 4MB)
  
  for (auto& extent : m_object_extents) {
    // 创建 ObjectRequest
    auto req = ObjectRequest::create_write(
      m_ictx, extent.object_no, extent.object_off,
      std::move(extent.bl), m_io_context, op_flags, ...);
    
    // 发送 ObjectRequest
    req->send();
  }
}

// 第五步：ObjectRequest 处理
// ═══════════════════════════════════════════════════════════

// io/ObjectRequest.cc
void ObjectWriteRequest::send() {
  // 检查是否需要 Copyup (分层)
  if (has_parent() && !object_exists) {
    // 需要先从父镜像读取数据
    pre_write_object_map_update();
    copyup();
    return;
  }
  
  // 更新 Object Map (如果启用)
  if (object_map_enabled) {
    pre_write_object_map_update();
  }
  
  // 构造 librados WriteOp
  neorados::WriteOp write_op;
  add_write_ops(&write_op);
  
  // 发送到 RADOS
  auto comp = create_rados_callback();
  m_ictx->rados_api.execute(object_name, m_io_context,
                             std::move(write_op), comp);
}

// 第六步：librados 发送
// ═══════════════════════════════════════════════════════════

// librados 负责：
// 1. CRUSH 计算：object → PG → OSDs
// 2. 构造 MOSDOp 消息
// 3. 发送到 Primary OSD
// 4. 等待副本确认
// 5. 回调 completion
```

### 4.3 读取流程

```cpp
// 读取流程相对简单 (无需 Copyup)

// io/ObjectReadRequest.cc
void ObjectReadRequest::read_object() {
  // 构造 librados ReadOp
  neorados::ReadOp read_op;
  read_op.read(m_object_off, m_object_len, &m_read_data, nullptr);
  
  // 发送到 RADOS (只需访问 Primary OSD)
  auto comp = create_rados_callback();
  m_ictx->rados_api.execute(object_name, m_io_context,
                             std::move(read_op), comp);
}

// 如果对象不存在且有父镜像
void ObjectReadRequest::read_parent() {
  // 从父镜像读取
  if (m_ictx->parent) {
    // 递归读取父镜像
    auto req = ObjectReadRequest::create(
      m_ictx->parent, m_object_no, &extents, ...);
    req->send();
  }
}

// 可选：触发 Copyup (将父镜像数据复制到子镜像)
void ObjectReadRequest::copyup() {
  auto req = CopyupRequest::create(m_ictx, m_object_no, ...);
  req->send();
}
```

### 4.4 地址映射算法

```cpp
// 将 Image 偏移转换为 Object 编号和偏移

uint64_t image_offset = 10 * 1024 * 1024;  // 10 MB
uint64_t length = 8 * 1024 * 1024;         // 8 MB
uint8_t order = 22;                        // 4 MB objects (2^22)
uint64_t object_size = 1ULL << order;      // 4 MB

// 计算起始对象
uint64_t start_object_no = image_offset >> order;
// = 10MB >> 22 = 2

// 计算对象内偏移
uint64_t start_object_off = image_offset & (object_size - 1);
// = 10MB & (4MB - 1) = 2 MB

// 计算结束对象
uint64_t end_object_no = (image_offset + length - 1) >> order;
// = (10MB + 8MB - 1) >> 22 = 4

// 结果：涉及对象 2, 3, 4
// 对象 2: [2MB, 4MB)  - 2 MB 数据
// 对象 3: [0, 4MB)    - 4 MB 数据
// 对象 4: [0, 2MB)    - 2 MB 数据
```

---

## 五、快照与克隆

### 5.1 快照机制

```
┌─────────────────────────────────────────────────────────────┐
│ RBD 快照原理                                                 │
└─────────────────────────────────────────────────────────────┘

Image 状态：
  size = 10 GB
  snapshots = []
  
  rbd_data.<id>.0000000000  (RADOS 对象)
  rbd_data.<id>.0000000001
  ...

创建快照 snap1：
  $ rbd snap create pool/image@snap1

步骤：
1. 冻结 I/O (可选)
2. 创建 RADOS 快照
   • data_ctx.snap_create("snap1")
   • 对所有对象创建快照
3. 更新元数据
   • 在 rbd_header 中添加快照元数据
   • snap_id = 10
   • snap_name = "snap1"
   • snap_namespace = user
   • size = 10 GB
   • timestamp
4. 更新 SnapContext
   • snapc.snaps = [10]
   • snapc.seq = 10
5. 恢复 I/O
```

#### 快照后写入

```
客户端写入到 HEAD (活动镜像)：

1. librados::ObjectWriteOperation op;
   
2. 设置快照上下文
   op.set_snap_context(snapc);
   // snapc = {seq: 10, snaps: [10]}
   
3. 写入操作
   op.write(offset, data);
   
4. OSD 处理：
   • 检查对象是否有快照
   • 如果有，先执行 COW (Copy-on-Write)
     - 克隆对象到快照 10
     - 然后写入新数据到 HEAD
   • 如果没有，直接写入 HEAD
```

### 5.2 克隆机制

```
┌─────────────────────────────────────────────────────────────┐
│ RBD 克隆 (分层)                                              │
└─────────────────────────────────────────────────────────────┘

步骤 1：保护快照
  $ rbd snap protect pool/parent@snap1
  
  • 标记快照为 protected
  • 防止被删除

步骤 2：创建克隆
  $ rbd clone pool/parent@snap1 pool/child
  
  • 创建新 Image (child)
  • 设置 parent 元数据：
    - parent_pool = pool
    - parent_image_id = parent_id
    - parent_snap_id = snap1_id
    - parent_overlap = 10 GB
  • child 没有实际数据对象

步骤 3：读取克隆
  应用读取 pool/child@HEAD[offset=5MB, len=4MB]
  
  1. ObjectReadRequest::read_object()
     • 尝试从 rbd_data.<child_id>.0000001 读取
     • 对象不存在 → -ENOENT
  
  2. ObjectReadRequest::read_parent()
     • 检查 has_parent() → true
     • 检查 offset < parent_overlap → true
     • 从 parent@snap1 读取：
       m_ictx->parent->aio_read(..., snap_id=snap1_id)
  
  3. 返回父镜像数据

步骤 4：写入克隆 (Copyup)
  应用写入 pool/child@HEAD[offset=5MB, len=1MB]
  
  1. ObjectWriteRequest::send()
     • 检查 has_parent() && !object_exists → true
     • 需要 Copyup
  
  2. CopyupRequest::send()
     a) 从父镜像读取整个对象 (4MB)
        read_from_parent(rbd_data.<parent_id>.0000001@snap1)
     
     b) 合并新数据
        data[0:5MB] = parent_data[0:5MB]  (保持不变)
        data[5MB:6MB] = new_data          (新写入)
        data[6MB:4MB] = parent_data[6MB:4MB]  (保持不变)
     
     c) 写入到子镜像
        write(rbd_data.<child_id>.0000001, data)
     
     d) 更新 Object Map
        object_map[1] = OBJECT_EXISTS
  
  3. 完成
     • 子镜像现在拥有该对象的完整副本
     • 后续读写直接访问子镜像，无需访问父镜像
```

#### Flatten (扁平化)

```
将克隆转换为独立镜像：

$ rbd flatten pool/child

过程：
1. 遍历所有对象
   for object_no in range(0, total_objects):
     if not object_exists(object_no):
       if object_no < parent_overlap:
         # 需要从父镜像复制
         copyup_from_parent(object_no)

2. 移除父镜像引用
   • parent_md = NULL
   • parent_overlap = 0

3. 结果
   • child 现在是完全独立的镜像
   • 可以删除父镜像的快照保护
```

---

## 六、缓存机制

### 6.1 Object Cacher

```
┌─────────────────────────────────────────────────────────────┐
│ Object Cacher 架构                                           │
└─────────────────────────────────────────────────────────────┘

librbd I/O
   │
   ▼
ObjectCacherObjectDispatch
   │
   ├─ 命中缓存 → 直接返回
   │
   └─ 未命中 → 从 OSD 读取 → 填充缓存
   
缓存结构：
┌──────────────────────────────────────┐
│ ObjectCacher                          │
│                                      │
│ ┌─────────────┐  ┌─────────────┐   │
│ │ Object 1    │  │ Object 2    │   │
│ │             │  │             │   │
│ │ ┌─────────┐ │  │ ┌─────────┐ │   │
│ │ │ Buffer  │ │  │ │ Buffer  │ │   │
│ │ │ [0, 64K]│ │  │ │ [0, 64K]│ │   │
│ │ └─────────┘ │  │ └─────────┘ │   │
│ │ ┌─────────┐ │  │ ┌─────────┐ │   │
│ │ │ Buffer  │ │  │ │ Buffer  │ │   │
│ │ │[64K,128K]│ │ │[128K,192K]│   │
│ │ └─────────┘ │  │ └─────────┘ │   │
│ └─────────────┘  └─────────────┘   │
└──────────────────────────────────────┘

特点：
• 缓存粒度：64KB
• LRU 淘汰策略
• 写回 (writeback) 或写通 (writethrough)
• 脏页跟踪
```

### 6.2 Persistent Write Log (PWL)

```
┌─────────────────────────────────────────────────────────────┐
│ PWL (持久化写日志) - RBD 19.x 新特性                        │
└─────────────────────────────────────────────────────────────┘

目的：
• 将写入先持久化到本地 SSD/PMEM
• 异步刷新到 RADOS
• 降低写延迟，提高性能

架构：
┌──────────────────────────────────────────────────────────┐
│ 应用写入                                                  │
└────────────┬─────────────────────────────────────────────┘
             │
             ▼
┌──────────────────────────────────────────────────────────┐
│ WriteLogImageDispatch                                     │
│                                                           │
│ 1. 分配日志条目                                           │
│ 2. 写入本地 SSD/PMEM (WriteLog)                          │
│ 3. 返回给应用 ✓                                          │
│                                                           │
│ 后台线程：                                                │
│ 4. 批量刷新到 RADOS                                       │
│ 5. 释放日志空间                                           │
└──────────────────────────────────────────────────────────┘

WriteLog 结构：
┌──────────────────────────────────────────────────────────┐
│ WriteLog (环形缓冲区)                                     │
│                                                           │
│ ┌───────────┐ ┌───────────┐ ┌───────────┐              │
│ │ Entry 1   │ │ Entry 2   │ │ Entry 3   │ ...          │
│ │ offset    │ │ offset    │ │ offset    │              │
│ │ length    │ │ length    │ │ length    │              │
│ │ data      │ │ data      │ │ data      │              │
│ │ checksum  │ │ checksum  │ │ checksum  │              │
│ └───────────┘ └───────────┘ └───────────┘              │
│                                                           │
│ 同步点 (Sync Point)：                                     │
│ • 定期创建                                                │
│ • 标记一致性边界                                          │
│ • 用于崩溃恢复                                            │
└──────────────────────────────────────────────────────────┘

性能提升：
• 写延迟：~100us (vs ~2ms)
• 写 IOPS：~50K (vs ~5K)
• 适用场景：写密集型工作负载
```

---

## 七、镜像与复制

### 7.1 RBD Mirroring

```
┌─────────────────────────────────────────────────────────────┐
│ RBD Mirroring 架构                                           │
└─────────────────────────────────────────────────────────────┘

两种模式：

1. Journal-based Mirroring (基于日志)
   ══════════════════════════════════════════════════════════
   
   主集群                              备集群
   ┌──────────────┐                  ┌──────────────┐
   │ Image (RW)   │                  │ Image (RO)   │
   │   │          │                  │              │
   │   │ write    │                  │              │
   │   ▼          │                  │              │
   │ Journal      │  ─────复制─────> │ 重放日志     │
   │ (记录操作)   │                  │              │
   └──────────────┘                  └──────────────┘
   
   特点：
   • 近实时复制 (秒级延迟)
   • 保证顺序
   • 需要 JOURNALING 特性
   • 更多开销

2. Snapshot-based Mirroring (基于快照)
   ══════════════════════════════════════════════════════════
   
   主集群                              备集群
   ┌──────────────┐                  ┌──────────────┐
   │ Image        │                  │ Image        │
   │   │          │                  │              │
   │   │ 周期快照  │                  │              │
   │   ▼          │                  │              │
   │ snap_1       │  ─────复制─────> │ 接收快照     │
   │ snap_2       │                  │              │
   └──────────────┘                  └──────────────┘
   
   特点：
   • 定期复制 (分钟级延迟)
   • 增量传输 (仅差异)
   • 无需 JOURNALING
   • 开销较小
```

### 7.2 Journal 实现

```cpp
// src/librbd/journal/

Journal 写入流程：
1. 应用写入 → Image I/O
2. ExclusiveLockImageDispatch 检查锁
3. Journal::append_write_event()
   • 序列化 WriteEvent
   • 写入 Journal 对象
4. 等待 Journal 持久化
5. 执行实际 I/O
6. 完成

Journal 对象存储：
Pool: rbd
  └─ journal.<image_id>
     ├─ journal_data.<image_id>.0000000000000000
     ├─ journal_data.<image_id>.0000000000000001
     ├─ journal_data.<image_id>.0000000000000002
     └─ ...

事件类型：
enum EventType {
  EVENT_TYPE_AIO_DISCARD,        // Discard 操作
  EVENT_TYPE_AIO_WRITE,          // Write 操作
  EVENT_TYPE_AIO_FLUSH,          // Flush 操作
  EVENT_TYPE_OP_FINISH,          // 操作完成
  EVENT_TYPE_SNAP_CREATE,        // 快照创建
  EVENT_TYPE_SNAP_REMOVE,        // 快照删除
  EVENT_TYPE_SNAP_RENAME,        // 快照重命名
  EVENT_TYPE_SNAP_PROTECT,       // 快照保护
  EVENT_TYPE_SNAP_UNPROTECT,     // 快照解除保护
  EVENT_TYPE_SNAP_ROLLBACK,      // 快照回滚
  EVENT_TYPE_RENAME,             // 重命名
  EVENT_TYPE_RESIZE,             // 调整大小
  EVENT_TYPE_FLATTEN,            // 扁平化
};
```

### 7.3 镜像同步

```cpp
// src/librbd/mirror/ImageSync.cc

同步流程：
1. 创建本地 Image (如果不存在)
2. 复制快照
   for each snapshot in source:
     • 创建本地快照
     • 计算差异 (diff)
     • 复制对象
3. 复制 HEAD (当前状态)
4. 完成

增量复制：
• 使用 diff_iterate 计算差异
• 只复制变化的数据块
• 显著减少网络传输

示例：
  源镜像：10 GB，变化 100 MB
  全量复制：10 GB 网络传输
  增量复制：100 MB 网络传输 (节省 99%)
```

---

## 八、性能优化

### 8.1 Object Map

```
┌─────────────────────────────────────────────────────────────┐
│ Object Map - 对象状态位图                                    │
└─────────────────────────────────────────────────────────────┘

目的：
• 跟踪每个对象的状态
• 优化操作性能

状态：
enum ObjectState {
  OBJECT_NONEXISTENT = 0,      // 对象不存在
  OBJECT_EXISTS = 1,           // 对象存在
  OBJECT_PENDING = 2,          // 对象操作中
  OBJECT_EXISTS_CLEAN = 3,     // 对象存在且干净
};

存储：
Pool: rbd
  └─ rbd_object_map.<image_id>
     • BitVector<2>              // 每个对象 2 bit
     • 10GB Image (4MB 对象) → 2560 对象 → 640 字节

优化场景：

1. Resize (缩小)
   ───────────────────────────────────────────────
   Without Object Map:
   • 需要删除所有超出范围的对象
   • 即使对象不存在，也要尝试删除
   • 时间：O(n) RADOS 操作
   
   With Object Map:
   • 查询 Object Map
   • 只删除 EXISTS 状态的对象
   • 时间：O(m)，m = 实际存在的对象数

2. Delete (删除 Image)
   ───────────────────────────────────────────────
   Without Object Map:
   • 尝试删除所有可能的对象 (0 到 max_object)
   • 稀疏镜像会有大量失败操作
   
   With Object Map:
   • 只删除 EXISTS 状态的对象
   • 显著加速

3. Diff (差异计算)
   ───────────────────────────────────────────────
   Without Object Map:
   • 需要查询每个对象的 stat
   • 大量 RADOS 操作
   
   With Object Map:
   • 直接读取 Object Map
   • 快速确定变化对象
```

### 8.2 Exclusive Lock

```
┌─────────────────────────────────────────────────────────────┐
│ Exclusive Lock - 排他锁                                      │
└─────────────────────────────────────────────────────────────┘

目的：
• 保证只有一个客户端可以写入
• 支持高级特性（Object Map, Journal, Mirroring）

实现：
• 基于 cls_lock (RADOS 对象锁)
• 存储在 rbd_header.<image_id>

状态机：
┌────────────┐
│  UNLOCKED  │
└──────┬─────┘
       │ acquire_lock()
       ▼
┌────────────┐
│ ACQUIRING  │
└──────┬─────┘
       │ success
       ▼
┌────────────┐  <───────┐
│   LOCKED   │          │ reacquire_lock()
└──────┬─────┘          │
       │ release_lock() │
       ▼                │
┌────────────┐          │
│ RELEASING  │──────────┘
└────────────┘

策略：

1. AutomaticPolicy (自动)
   ───────────────────────────────────────────────
   • 客户端打开 Image 时自动获取锁
   • I/O 操作前检查锁
   • 失去锁时自动重新获取

2. StandardPolicy (标准)
   ───────────────────────────────────────────────
   • 需要显式获取锁
   • 适用于手动控制场景

锁劫持 (Lock Breaking)：
• 如果锁持有者崩溃
• 其他客户端可以打破锁
• 通过黑名单机制防止冲突
```

### 8.3 QoS (服务质量)

```cpp
// src/librbd/io/QosImageDispatch.cc

QoS 限制：

1. IOPS 限制
   ───────────────────────────────────────────────
   • rbd_qos_iops_limit = 1000
   • 每秒最多 1000 个 I/O 操作

2. 带宽限制
   ───────────────────────────────────────────────
   • rbd_qos_bps_limit = 100 MB/s
   • 每秒最多 100 MB 数据传输

3. 读写分离限制
   ───────────────────────────────────────────────
   • rbd_qos_read_iops_limit
   • rbd_qos_write_iops_limit
   • rbd_qos_read_bps_limit
   • rbd_qos_write_bps_limit

实现：
• Token Bucket 算法
• 在 ImageDispatch 层拦截
• 超限请求排队等待
```

### 8.4 预读 (Readahead)

```cpp
// ImageCtx::readahead

Readahead readahead;

配置：
• rbd_readahead_trigger_requests = 10
  - 连续 10 次顺序读取后触发预读

• rbd_readahead_max_bytes = 512 KB
  - 最大预读字节数

• rbd_readahead_disable_after_bytes = 50 MB
  - 读取超过 50 MB 后禁用预读

策略：
1. 检测顺序读取模式
2. 预读后续数据块
3. 填充 Object Cacher
4. 提高顺序读取性能
```

### 8.5 性能计数器

```cpp
// src/librbd/Types.h

enum {
  l_librbd_rd,               // 读操作数
  l_librbd_rd_bytes,         // 读字节数
  l_librbd_rd_latency,       // 读延迟
  
  l_librbd_wr,               // 写操作数
  l_librbd_wr_bytes,         // 写字节数
  l_librbd_wr_latency,       // 写延迟
  
  l_librbd_discard,          // Discard 操作数
  l_librbd_discard_bytes,    // Discard 字节数
  l_librbd_discard_latency,  // Discard 延迟
  
  l_librbd_flush,            // Flush 操作数
  l_librbd_flush_latency,    // Flush 延迟
  
  l_librbd_ws,               // WriteSame 操作数
  l_librbd_ws_bytes,         // WriteSame 字节数
  l_librbd_ws_latency,       // WriteSame 延迟
  
  l_librbd_cmp,              // CompareAndWrite 操作数
  l_librbd_cmp_bytes,        // CompareAndWrite 字节数
  l_librbd_cmp_latency,      // CompareAndWrite 延迟
  
  l_librbd_snap_create,      // 快照创建
  l_librbd_snap_remove,      // 快照删除
  l_librbd_snap_rollback,    // 快照回滚
  
  l_librbd_readahead,        // 预读操作数
  l_librbd_readahead_bytes,  // 预读字节数
};

查看：
$ ceph daemon /var/run/ceph/ceph-client.admin.*.asok \
  perf dump librbd
```

---

## 九、关键文件列表

### 9.1 核心文件 (Top 20)

| 文件 | 行数 | 功能 |
|------|------|------|
| `librbd.cc` | ~7600 | C API 入口 |
| `io/ObjectRequest.cc` | ~2500 | 对象 I/O 请求 |
| `io/ImageRequest.cc` | ~2000 | Image I/O 请求 |
| `ImageCtx.cc` | ~1800 | Image 上下文实现 |
| `Operations.cc` | ~1500 | Image 操作 |
| `ImageState.cc` | ~1200 | Image 状态机 |
| `ExclusiveLock.cc` | ~1100 | 排他锁 |
| `Journal.cc` | ~1000 | 日志 |
| `io/ImageDispatcher.cc` | ~900 | I/O 调度 |
| `io/AioCompletion.cc` | ~800 | 异步完成 |
| `ImageWatcher.cc` | ~800 | Image 观察者 |
| `object_map/UpdateRequest.cc` | ~700 | Object Map 更新 |
| `cache/pwl/WriteLog.cc` | ~2000 | 持久化写日志 |
| `mirror/ImageSync.cc` | ~600 | 镜像同步 |
| `operation/SnapshotCreateRequest.cc` | ~500 | 快照创建 |
| `operation/ResizeRequest.cc` | ~600 | 调整大小 |
| `operation/FlattenRequest.cc` | ~500 | 扁平化 |
| `io/CopyupRequest.cc` | ~400 | Copyup 请求 |
| `api/Image.cc` | ~1000 | Image API |
| `api/Io.cc` | ~800 | I/O API |

### 9.2 代码统计

```
目录                    文件数    代码行数
───────────────────────────────────────────
librbd/                 ~50       ~25,000
librbd/io/              ~49       ~15,000
librbd/cache/           ~50       ~12,000
librbd/image/           ~36       ~8,000
librbd/operation/       ~40       ~10,000
librbd/journal/         ~25       ~6,000
librbd/mirror/          ~43       ~8,000
librbd/object_map/      ~27       ~5,000
librbd/exclusive_lock/  ~8        ~2,000
librbd/managed_lock/    ~13       ~3,000
librbd/api/             ~24       ~6,000
librbd/crypto/          ~24       ~4,000
librbd/deep_copy/       ~17       ~3,000
librbd/migration/       ~32       ~5,000
librbd/watcher/         ~7        ~2,000
librbd/trash/           ~4        ~1,000
───────────────────────────────────────────
总计                    ~500      ~120,000+
```

---

## 十、调试与监控

### 10.1 调试选项

```bash
# 启用 RBD 调试日志
ceph config set client debug_rbd 20

# 启用 librbd 调试
export CEPH_ARGS="--debug-rbd=20 --debug-librbd=20"

# 查看 Image 信息
rbd info pool/image
rbd diff pool/image
rbd du pool/image

# 查看 Image 特性
rbd info pool/image | grep features

# 查看锁状态
rbd lock list pool/image

# 查看快照
rbd snap ls pool/image
```

### 10.2 性能测试

```bash
# Bench 测试
rbd bench --io-type write --io-size 4K --io-total 1G pool/image
rbd bench --io-type read --io-size 4K --io-total 1G pool/image

# fio 测试
fio --name=randwrite --ioengine=rbd \
    --pool=rbd --rbdname=image \
    --rw=randwrite --bs=4k --size=1G \
    --numjobs=4 --time_based --runtime=60

# 查看性能统计
rbd perf image iostat
rbd perf image iotop
```

### 10.3 常见问题

```
1. 性能问题
   ───────────────────────────────────────────────
   • 检查 Object Map 是否启用
   • 检查是否有 Copyup 开销
   • 检查 QoS 限制
   • 检查网络延迟
   • 检查 OSD 性能

2. 空间问题
   ───────────────────────────────────────────────
   • 使用 du 查看实际占用
   • 检查快照占用空间
   • 考虑 Flatten 克隆
   • 使用 Discard/TRIM

3. 锁问题
   ───────────────────────────────────────────────
   • 检查谁持有锁
   • 考虑打破锁 (break lock)
   • 检查客户端是否崩溃

4. 克隆性能
   ───────────────────────────────────────────────
   • Copyup 会导致首次写入延迟高
   • 考虑预热 (提前 Copyup)
   • 考虑 Flatten 转为独立镜像
```

---

## 十一、最佳实践

### 11.1 特性选择

```
推荐特性组合：

生产环境 (性能优先)：
├─ layering              ✓ (支持快照/克隆)
├─ exclusive-lock        ✓ (多客户端协调)
├─ object-map            ✓ (加速操作)
├─ fast-diff             ✓ (快速差异)
└─ deep-flatten          ✓ (彻底扁平化)

镜像复制环境：
├─ 上述所有特性
└─ journaling            ✓ (近实时复制)

最小化特性 (兼容性优先)：
└─ layering              ✓ (仅支持快照)

创建示例：
$ rbd create --size 10G --image-feature \
  layering,exclusive-lock,object-map,fast-diff \
  pool/image
```

### 11.2 对象大小选择

```
默认：4 MB (order=22)

小对象 (1 MB, order=20)：
• 优点：更细粒度的条带化，更好的随机 I/O
• 缺点：更多对象，更多元数据开销
• 适用：随机 I/O 密集型

大对象 (16 MB, order=24)：
• 优点：更少对象，更少元数据开销
• 缺点：条带粒度粗，随机 I/O 差
• 适用：顺序 I/O 密集型

推荐：
• 通用场景：4 MB (默认)
• SSD/NVMe：2-4 MB
• HDD：4-8 MB
```

### 11.3 快照管理

```
快照策略：
1. 定期创建快照 (如每天)
2. 保留有限数量 (如 7 天)
3. 删除过期快照
4. 监控快照空间占用

示例脚本：
#!/bin/bash
IMAGE=pool/myimage
KEEP_DAYS=7

# 创建快照
rbd snap create ${IMAGE}@$(date +%Y%m%d)

# 删除旧快照
rbd snap ls ${IMAGE} | while read snap; do
  age=$(( ($(date +%s) - $(stat -c %Y ${snap})) / 86400 ))
  if [ $age -gt $KEEP_DAYS ]; then
    rbd snap rm ${IMAGE}@${snap}
  fi
done
```

---

## 十二、总结

RBD 是 Ceph 提供的**块存储接口**，核心特点：

✅ **分层架构**：API → Dispatch Chain → ObjectRequest → librados
✅ **对象映射**：虚拟块设备 → 4MB RADOS 对象
✅ **快照克隆**：COW + 分层，高效共享数据
✅ **缓存优化**：Object Cacher + PWL
✅ **高级特性**：Object Map, Exclusive Lock, Journaling
✅ **镜像复制**：主备集群同步，灾难恢复
✅ **性能调优**：QoS, Readahead, 预分配

代码组织清晰，模块化程度高，是学习分布式块存储的优秀范例！🚀

