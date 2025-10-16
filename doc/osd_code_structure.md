# Ceph OSD 模块代码组织详解

## 📁 一、目录结构总览

```
src/osd/
├── OSD.{h,cc}                      # 核心 OSD 守护进程类
├── OSDService.{h,cc}               # OSD 服务封装
├── PG.{h,cc}                       # Placement Group 基类
├── PrimaryLogPG.{h,cc}             # 主 PG 实现（副本池）
├── PeeringState.{h,cc}             # Peering 状态机
├── PGLog.{h,cc}                    # PG 日志（恢复关键）
├── PGBackend.{h,cc}                # 存储后端抽象
├── ReplicatedBackend.{h,cc}        # 副本后端实现
├── ECBackend.{h,cc}                # 纠删码后端实现
│
├── 数据结构与类型
├── osd_types.{h,cc}                # OSD 核心数据类型
├── osd_internal_types.h            # 内部数据类型
├── object_state.h                  # 对象状态
├── recovery_types.{h,cc}           # 恢复相关类型
│
├── 操作处理
├── OpRequest.{h,cc}                # 操作请求封装
├── OpContext.h                     # 操作上下文
├── PGTransaction.h                 # PG 事务
├── ECTransaction.{h,cc}            # EC 事务
├── osd_op_util.{h,cc}              # 操作工具函数
│
├── 映射与分布
├── OSDMap.{h,cc}                   # OSD 集群拓扑图
├── OSDMapMapping.{h,cc}            # OSD 映射计算
├── MissingLoc.{h,cc}               # 缺失对象定位
│
├── 调度器
├── scheduler/
│   ├── OpScheduler.{h,cc}          # 操作调度器抽象
│   ├── OpSchedulerItem.{h,cc}      # 调度项
│   └── mClockScheduler.{h,cc}      # mClock QoS 调度器
│
├── 清洗（Scrub）
├── scrubber/
│   ├── osd_scrub.{h,cc}            # OSD 清洗服务
│   ├── pg_scrubber.{h,cc}          # PG 清洗器
│   ├── scrub_machine.{h,cc}        # 清洗状态机
│   ├── scrub_backend.{h,cc}        # 清洗后端
│   ├── scrub_job.{h,cc}            # 清洗任务
│   └── ScrubStore.{h,cc}           # 清洗存储
│
├── 快照与克隆
├── SnapMapper.{h,cc}               # 快照映射
├── Session.{h,cc}                  # 会话管理
├── Watch.{h,cc}                    # Watch/Notify 机制
│
├── 性能与监控
├── osd_perf_counters.{h,cc}        # 性能计数器
├── osd_tracer.{h,cc}               # 追踪工具
├── DynamicPerfStats.h              # 动态性能统计
│
├── 其他组件
├── ClassHandler.{h,cc}             # 对象类处理
├── ExtentCache.{h,cc}              # 范围缓存
├── HitSet.{h,cc}                   # 热度统计
├── TierAgentState.h                # 分层代理
└── error_code.{h,cc}               # 错误码定义

src/os/                              # 存储后端
├── ObjectStore.{h,cc}              # 存储抽象层
├── Transaction.{h,cc}              # 事务抽象
├── bluestore/                      # BlueStore 实现
│   ├── BlueStore.{h,cc}            # 主实现
│   ├── BlueFS.{h,cc}               # BlueFS 文件系统
│   ├── Allocator.{h,cc}            # 空间分配器
│   ├── bluestore_types.{h,cc}      # BlueStore 类型
│   └── FreelistManager.{h,cc}      # 空闲列表管理
├── filestore/                      # FileStore（已弃用）
├── memstore/                       # 内存存储（测试用）
└── kstore/                         # KStore（实验性）
```

---

## 🏗️ 二、核心架构设计

### 1. OSD 类（OSD.h/cc）- 守护进程核心

```cpp
class OSD : public Dispatcher,           // 消息分发
            public md_config_obs_t        // 配置观察者
{
  // ===== 核心组件 =====
  
  // 身份标识
  int whoami;                             // OSD ID
  
  // 消息传递
  Messenger *cluster_messenger;           // 集群内部通信
  Messenger *client_messenger;            // 客户端通信
  Messenger *hb_front_messenger;          // 心跳前端
  Messenger *hb_back_messenger;           // 心跳后端
  
  // Monitor 交互
  MonClient *monc;                        // Monitor 客户端
  
  // 存储后端
  ObjectStore *store;                     // 底层存储
  
  // PG 管理
  ceph::mutex pg_map_lock;
  std::map<spg_t, PGRef> pg_map;         // PG ID -> PG 映射
  
  // OSD Map
  OSDMapRef osdmap;                       // 当前 OSD 地图
  std::map<epoch_t, OSDMapRef> superblock.past_intervals;
  
  // 工作队列
  ShardedThreadPool osd_op_tp;           // 操作线程池
  ThreadPool recovery_tp;                 // 恢复线程池
  
  // 调度器
  std::unique_ptr<OpScheduler> op_scheduler;
  
  // 服务
  OSDService service;                     // OSD 服务封装
  
  // 心跳
  HeartbeatThread hb_thread;
  
  // ===== 核心方法 =====
  
  // 生命周期
  int init();
  int shutdown();
  
  // 消息处理
  bool ms_dispatch(Message *m) override;
  void handle_osd_map(class MOSDMap *m);
  void handle_osd_op(OpRequestRef op);
  
  // PG 管理
  void handle_pg_create(OpRequestRef op);
  PGRef _lookup_pg(spg_t pgid);
  void _wake_pg_slot(spg_t pgid);
  
  // 心跳
  void heartbeat();
  void handle_osd_ping(MOSDPing *m);
  
  // Peering
  void advance_pg(epoch_t osd_epoch, PG *pg);
  void queue_for_peering(PG *pg);
};
```

**OSD 职责**：
1. ✅ 管理所有本地 PG
2. ✅ 处理客户端 I/O 请求
3. ✅ 执行数据恢复
4. ✅ 与其他 OSD 通信（Peering、复制、恢复）
5. ✅ 向 Monitor 报告状态
6. ✅ 执行 Scrub 清洗

---

### 2. PG 类（PG.h/cc）- Placement Group

```cpp
class PG : public DoutPrefixProvider,
           public PeeringState::PeeringListener
{
  // ===== 核心成员 =====
  
  // 标识
  const pg_shard_t pg_whoami;            // 我在这个 PG 中的角色
  const spg_t pg_id;                     // PG ID
  
  // 集合（Collection）
  coll_t coll;                           // 存储集合
  ObjectStore::CollectionHandle ch;      // 存储句柄
  
  // Peering 状态
  std::unique_ptr<PeeringState> peering_state;
  
  // 日志
  PGLog pg_log;                          // PG 操作日志
  
  // 后端
  std::unique_ptr<PGBackend> pgbackend;  // 存储后端
  
  // 对象上下文
  std::map<hobject_t, ObjectContextRef> object_contexts;
  
  // 恢复
  PGRecovery recovery_state;
  std::set<hobject_t> missing_loc;       // 缺失对象位置
  
  // 清洗
  std::unique_ptr<PgScrubber> m_scrubber;
  
  // ===== 核心方法 =====
  
  // 操作处理
  void do_request(OpRequestRef& op);
  void do_op(OpRequestRef& op);
  void execute_ctx(OpContext *ctx);
  
  // Peering
  void handle_peering_event(PGPeeringEventRef evt);
  void start_peering_interval();
  void activate();
  
  // 恢复
  void start_recovery_op(hobject_t soid);
  void on_local_recover(hobject_t oid, ...);
  
  // 清洗
  void scrub(epoch_t epoch);
  void replica_scrub(OpRequestRef op);
};
```

**PG 状态机**（精简版）：
```
Initial → Reset → Started → Primary → Active
                          ↓
                       Replica → ReplicaActive
                       
其他重要状态：
- Peering：正在同步状态
- Recovering：正在恢复数据
- Backfilling：正在回填数据
- Down：不可用
- Incomplete：无法完成 Peering
```

---

### 3. PrimaryLogPG 类（PrimaryLogPG.h/cc）- 副本池实现

```cpp
class PrimaryLogPG : public PG,
                     public PGBackend::Listener
{
  // ===== 操作上下文 =====
  struct OpContext {
    OpRequestRef op;                     // 原始请求
    PGTransaction *txn;                  // 本地事务
    
    // 对象信息
    ObjectContextRef obc;                // 对象上下文
    
    // 日志
    std::vector<pg_log_entry_t> log_entries;
    
    // 复制
    bool sent_reply;
    std::set<pg_shard_t> waiting_for_commit;
    
    // 回调
    std::list<std::function<void()>> on_applied;
    std::list<std::function<void()>> on_committed;
  };
  
  // ===== 核心方法 =====
  
  // I/O 路径
  void do_op(OpRequestRef& op) override;
  int do_osd_ops(OpContext *ctx, std::vector<OSDOp>& ops);
  
  // 写入流程
  void execute_ctx(OpContext *ctx);
  void submit_transaction(OpContext *ctx);
  void issue_repop(OpContext *ctx);      // 向副本发送
  void eval_repop(RepGather *repop);     // 评估复制完成
  
  // 读取流程
  int do_read_op(OpContext *ctx);
  int find_object_context(hobject_t oid, ObjectContextRef *obc);
  
  // 对象类操作
  int do_class_read(OpContext *ctx);
  int do_class_write(OpContext *ctx);
};
```

**写入流程（3 副本为例）**：
```
1. 客户端请求到达 Primary OSD
   do_op(op)
     ↓
2. Primary 准备操作
   prepare_transaction(ctx)
   - 检查权限
   - 分配版本号
   - 生成 PG Log Entry
     ↓
3. Primary 执行本地写入
   execute_ctx(ctx)
   - 写入 BlueStore
   - 添加到 PG Log
     ↓
4. Primary 向副本发送
   issue_repop(ctx)
   - 发送 MOSDRepOp 到 Replica 1
   - 发送 MOSDRepOp 到 Replica 2
     ↓
5. Replica 执行并回复
   Replica::do_repop()
   - 写入本地存储
   - 返回 MOSDRepOpReply
     ↓
6. Primary 收集响应
   eval_repop()
   - 等待多数派（2/3）
   - on_applied 回调
   - 等待全部（3/3）
   - on_committed 回调
     ↓
7. 返回客户端
   reply_ctx(ctx)
   - 发送 MOSDOpReply
```

---

### 4. PeeringState 类（PeeringState.h/cc）- Peering 状态机

```cpp
class PeeringState {
  // ===== 状态机核心 =====
  
  // 当前状态
  std::unique_ptr<PeeringState::PeeringMachine> state_machine;
  
  // PG 信息
  pg_info_t info;                        // 本地 PG 信息
  std::map<pg_shard_t, pg_info_t> peer_info; // 其他 OSD 的信息
  
  // Acting Set & Up Set
  std::vector<int> acting;               // 实际负责的 OSD
  std::vector<int> up;                   // 应该负责的 OSD
  int primary;                           // Primary OSD
  
  // Missing & Log
  pg_missing_t missing;                  // 缺失的对象
  pg_missing_t peer_missing[...];        // 副本缺失的对象
  
  // ===== Peering 流程 =====
  
  // 1. GetInfo：收集信息
  void GetInfo::react(const MNotifyRec &notify) {
    // 记录每个 OSD 的 PG 信息
    peer_info[from] = notify.info;
  }
  
  // 2. GetLog：获取权威日志
  void GetLog::react(const MLogRec &msg) {
    // 合并日志，确定权威版本
    merge_log(msg.log);
  }
  
  // 3. GetMissing：确定缺失对象
  void GetMissing::enter() {
    for (auto& peer : peer_info) {
      request_missing(peer);
    }
  }
  
  // 4. WaitUpThru：等待 OSDMap 更新
  void WaitUpThru::react(const ActMap &evt) {
    if (evt.osdmap->get_up_thru(primary) >= need_up_thru)
      post_event(Activate());
  }
  
  // 5. Active：激活状态
  void Active::enter() {
    pg->activate();
    start_recovery();
  }
};
```

**Peering 完整流程**：
```
OSD Map 变化触发
    ↓
Reset 状态
    ↓
Started/Primary
    ↓
Peering/GetInfo
    - 向所有 OSD 发送 pg_query_t
    - 收集 pg_info_t
    ↓
Peering/GetLog
    - 选择权威日志（last_update 最新的）
    - 请求并合并日志
    ↓
Peering/GetMissing
    - 对比日志，确定每个 OSD 缺失的对象
    ↓
Peering/WaitUpThru
    - 等待 Monitor 确认 up_thru
    ↓
Active
    - 开始服务 I/O
    - 启动恢复进程
```

---

### 5. PGLog 类（PGLog.h/cc）- 操作日志

```cpp
struct pg_log_entry_t {
  // 基本信息
  eversion_t version;                    // 版本号（epoch.version）
  eversion_t prior_version;              // 之前的版本
  
  // 操作类型
  int op;                                // MODIFY, DELETE, CLONE, etc.
  
  // 对象
  hobject_t soid;                        // 对象 ID
  
  // 回滚信息（用于 EC）
  ObjectModDesc mod_desc;                // 修改描述
  
  // 重复检测
  osd_reqid_t reqid;                     // 请求 ID
  version_t user_version;                // 用户版本
};

class PGLog {
  // ===== 日志结构 =====
  
  // 主日志
  std::list<pg_log_entry_t> log;        // 最近的操作日志
  
  // 边界
  eversion_t head;                       // 最新版本
  eversion_t tail;                       // 最旧版本
  
  // 重复检测
  std::map<osd_reqid_t, pg_log_dup_t> dups;
  
  // ===== 核心功能 =====
  
  // 1. 恢复加速
  //    通过日志对比，快速确定缺失对象
  void calc_missing(
    const pg_info_t &info,
    const pg_log_t &remote_log,
    pg_missing_t &missing
  );
  
  // 2. 重复检测
  bool logged_req(const osd_reqid_t &reqid) {
    return dups.count(reqid) > 0;
  }
  
  // 3. EC 回滚支持
  void add_entry(pg_log_entry_t &entry);
};
```

**PG Log 的三大作用**：
```
1. 恢复加速
   - 记录最近的对象修改
   - Peering 时对比日志，无需扫描所有对象
   - 只恢复不一致的对象

2. 重复检测
   - 记录 reqid 和 user_version
   - 幂等性保证
   - 避免重复执行操作

3. EC 回滚
   - 记录修改描述（ObjectModDesc）
   - EC 写入失败时可以回滚
   - 保证 EC 的原子性
```

---

### 6. PGBackend 类层次结构

```
       PGBackend（抽象基类）
              ↓
       ┌──────┴──────┐
       ↓             ↓
ReplicatedBackend  ECBackend
  （副本后端）    （纠删码后端）
```

#### ReplicatedBackend（副本模式）
```cpp
class ReplicatedBackend : public PGBackend {
  // ===== 写入流程 =====
  
  // 1. Primary 发起
  void submit_transaction(
    const hobject_t &soid,
    const object_stat_sum_t &delta_stats,
    const eversion_t &at_version,
    PGTransaction *t,
    ...
  ) {
    // 创建 RepModify 操作
    issue_op(soid, at_version, tid, reqid, 
             t->get_data_bl(), ...);
    
    // 发送到副本
    for (auto replica : get_acting_shards()) {
      send_message_osd_cluster(
        replica.osd,
        new MOSDRepOp(...),
        ...
      );
    }
  }
  
  // 2. Replica 接收
  void handle_message(OpRequestRef op) {
    auto m = static_cast<MOSDRepOp*>(op->get_req());
    
    // 应用事务
    int r = store->queue_transaction(
      ch, m->get_transaction(), ...);
    
    // 回复 Primary
    reply_message(m->get_orig_source(), 
                  new MOSDRepOpReply(...));
  }
};
```

#### ECBackend（纠删码模式 - 4+2 为例）
```cpp
class ECBackend : public PGBackend {
  // ===== EC 写入流程 =====
  
  // 1. 分片编码
  void submit_transaction(...) {
    // 原始数据: ABCDEFGH (假设 8 字节)
    // K=4, M=2
    
    // 分片：
    // shard 0: AB
    // shard 1: CD  
    // shard 2: EF
    // shard 3: GH
    // shard 4: P1 (parity 1)
    // shard 5: P2 (parity 2)
    
    std::map<pg_shard_t, bufferlist> shards;
    ECUtil::encode(
      stripe_width, data, &shards);
    
    // 发送到各个 OSD
    for (auto& [shard, data] : shards) {
      send_shard(shard, data, ...);
    }
  }
  
  // 2. 读取恢复（假设 OSD 2, 3 故障）
  void handle_read_op(OpRequestRef op) {
    // 只需要 K 个分片即可恢复
    // 例如读取 shard 0,1,4,5
    
    std::vector<bufferlist> available_shards;
    // 收集可用分片...
    
    // 解码恢复原始数据
    ECUtil::decode(
      available_shards, 
      &original_data);
    
    reply_op(op, original_data);
  }
};
```

---

### 7. ObjectStore 层次结构（BlueStore）

```cpp
// 抽象层
class ObjectStore {
  virtual int queue_transaction(
    CollectionHandle& ch,
    Transaction& t,
    Context *oncommit
  ) = 0;
};

// BlueStore 实现
class BlueStore : public ObjectStore {
  // ===== 核心组件 =====
  
  BlueFS *bluefs;                        // 元数据文件系统
  RocksDB *db;                           // 元数据数据库
  Allocator *alloc;                      // 空间分配器
  
  // ===== 存储结构 =====
  
  /*
   * Collection (PG)
   *   └── Objects
   *       ├── Object Data (Blocks)
   *       ├── Object Omap (RocksDB)
   *       └── Object Xattrs (RocksDB)
   */
  
  // ===== 写入流程 =====
  
  int queue_transaction(...) {
    // 1. 分配空间
    allocate_blocks(...);
    
    // 2. 写入数据到设备
    bdev->aio_write(...);
    
    // 3. 更新元数据到 RocksDB
    db->submit_transaction(...);
    
    // 4. WAL（预写日志）
    bluefs->append_log(...);
    
    // 5. 回调通知完成
    oncommit->complete(0);
  }
  
  // ===== 压缩与校验 =====
  
  // 压缩（可选）
  compress_data(data, &compressed);
  
  // 校验和
  uint32_t csum = crc32c(data);
};
```

**BlueStore 数据布局**：
```
┌─────────────────────────────────────────┐
│          Raw Block Device               │
│  (SSD/HDD/NVMe)                         │
├─────────────────────────────────────────┤
│                                          │
│  ┌────────────────────────────────┐    │
│  │   BlueFS Partition             │    │
│  │   - RocksDB WAL                │    │
│  │   - RocksDB SST Files          │    │
│  │   - BlueStore WAL              │    │
│  └────────────────────────────────┘    │
│                                          │
│  ┌────────────────────────────────┐    │
│  │   Object Data Blocks           │    │
│  │   - 直接管理，无文件系统       │    │
│  │   - 4KB ~ 1MB 块               │    │
│  │   - 支持压缩、校验和           │    │
│  └────────────────────────────────┘    │
│                                          │
└─────────────────────────────────────────┘

RocksDB (元数据):
  - Object 属性（大小、mtime等）
  - Omap（对象键值对）
  - Xattrs（扩展属性）
  - 分配位图
  - Collection 信息
```

---

## 🔄 三、关键数据流程

### 1. 客户端写入流程（完整路径）

```
┌─────────────┐
│   Client    │
│  (librados) │
└──────┬──────┘
       │ 1. CEPH_OSD_OP_WRITE
       │    object="foo", data=...
       ↓
┌──────────────────────────────────────────┐
│          OSD Daemon                      │
├──────────────────────────────────────────┤
│                                          │
│  2. OSD::ms_dispatch()                  │
│     - 接收 MOSDOp 消息                   │
│     - 包装成 OpRequest                   │
│       ↓                                  │
│  3. OSD::enqueue_op()                   │
│     - 放入 OpScheduler                   │
│     - mClock QoS 调度                    │
│       ↓                                  │
│  4. OSD::dequeue_op()                   │
│     - 从队列取出                         │
│     - 查找目标 PG                        │
│       ↓                                  │
│  5. PG::do_request()                    │
│     - 状态检查（is_active?）            │
│     - 权限检查                           │
│       ↓                                  │
│  6. PrimaryLogPG::do_op()               │
│     - 创建 OpContext                     │
│     - 分配版本号：(epoch, version++)    │
│       ↓                                  │
│  7. PrimaryLogPG::prepare_transaction() │
│     - 检查对象是否存在                   │
│     - 准备 PGTransaction                │
│     - 生成 pg_log_entry_t               │
│       ↓                                  │
│  8. PrimaryLogPG::execute_ctx()         │
│     - 应用到本地存储                     │
│     - pgbackend->submit_transaction()   │
│       ↓                                  │
│  ┌──────────────────────────────────┐  │
│  │  ReplicatedBackend               │  │
│  ├──────────────────────────────────┤  │
│  │  9. issue_repop()                │  │
│  │     - 向 Replica 发送 MOSDRepOp  │  │
│  └──────────────────────────────────┘  │
│       ↓                                  │
│  10. BlueStore::queue_transaction()     │
│      - 写入数据块                        │
│      - 更新 RocksDB 元数据              │
│      - fsync()                          │
│       ↓                                  │
│  11. OpContext::on_applied              │
│      - 数据已持久化到 Primary           │
│      - (此时可以返回客户端 - 快速路径)  │
│       ↓                                  │
└──────────────────────────────────────────┘
       │
       │ MOSDRepOp
       ↓
┌──────────────────────────────────────────┐
│      Replica OSD 1                       │
├──────────────────────────────────────────┤
│  12. ReplicatedBackend::handle_message() │
│      - 接收复制请求                       │
│      - 解码 Transaction                   │
│        ↓                                  │
│  13. BlueStore::queue_transaction()      │
│      - 写入本地存储                       │
│        ↓                                  │
│  14. 返回 MOSDRepOpReply                 │
└──────────────────────────────────────────┘
       │
       │ MOSDRepOpReply
       ↓
┌──────────────────────────────────────────┐
│      Primary OSD (继续)                  │
├──────────────────────────────────────────┤
│  15. ReplicatedBackend::handle_reply()   │
│      - 收集副本响应                       │
│      - waiting_for_commit--              │
│        ↓                                  │
│  16. eval_repop()                        │
│      - 检查是否所有副本都已提交          │
│      - 触发 on_committed 回调            │
│        ↓                                  │
│  17. PrimaryLogPG::reply_ctx()           │
│      - 返回 MOSDOpReply 给客户端         │
└──────────────────────────────────────────┘
       │
       │ MOSDOpReply
       ↓
┌─────────────┐
│   Client    │
│  写入完成   │
└─────────────┘
```

**写入延迟构成**：
```
总延迟 = 网络延迟 + Primary 处理 + 磁盘写入 + 副本复制

快速响应模式（applied）：
  - Primary 写入完成即返回
  - 延迟 ~= 1-2ms（SSD）

完全持久化模式（committed）：
  - 等待所有副本确认
  - 延迟 ~= 3-5ms（SSD，3 副本）
```

---

### 2. 客户端读取流程

```
Client
  ↓ CEPH_OSD_OP_READ
OSD::ms_dispatch()
  ↓
OSD::enqueue_op()
  ↓
PG::do_request()
  ↓
PrimaryLogPG::do_op()
  ↓
PrimaryLogPG::do_read_op()
  ├─ find_object_context()        # 查找对象上下文
  │  └─ object_contexts[oid]      # 内存缓存
  │     或
  │  └─ BlueStore::stat()          # 查询存储
  │
  ├─ 检查权限和快照
  │
  └─ BlueStore::read()             # 读取数据
     ├─ 查询 RocksDB 获取 extent 位置
     ├─ 从设备读取数据块
     ├─ 解压缩（如果需要）
     └─ 校验和验证
  ↓
reply_op()                         # 返回数据给客户端
```

**读取优化**：
```
1. 对象上下文缓存
   - 避免重复查询元数据

2. 数据缓存
   - BlueStore 缓存热点数据

3. 并行读取
   - 多个 extent 并行读取

4. 预读
   - 顺序读取时预取后续数据
```

---

### 3. 恢复（Recovery）流程

```
触发条件：
  - OSD 故障恢复
  - OSD 加入集群
  - OSD Map 变化

┌─────────────────────────────────────────┐
│  1. Peering 阶段                         │
│     - 确定 Acting Set                    │
│     - 对比 PG Log                        │
│     - 生成 missing 列表                  │
└────────────┬────────────────────────────┘
             ↓
┌─────────────────────────────────────────┐
│  2. Active 状态                          │
│     PrimaryLogPG::start_recovery()       │
│       ↓                                  │
│     - 选择恢复对象（优先级排序）         │
│     - 每次恢复几个对象（控制并发）       │
└────────────┬────────────────────────────┘
             ↓
┌─────────────────────────────────────────┐
│  3. Primary 发起 Pull                    │
│     recover_object()                     │
│       ↓                                  │
│     - 发送 MOSDPGPull 到源 OSD          │
│       （谁有完整版本）                   │
└────────────┬────────────────────────────┘
             ↓
┌─────────────────────────────────────────┐
│  4. 源 OSD 响应                          │
│     handle_pull()                        │
│       ↓                                  │
│     - 读取对象数据                       │
│     - 发送 MOSDPGPush                    │
└────────────┬────────────────────────────┘
             ↓
┌─────────────────────────────────────────┐
│  5. 目标 OSD 接收                        │
│     handle_push()                        │
│       ↓                                  │
│     - 写入对象到本地存储                 │
│     - 更新 missing 列表                  │
│     - 发送 MOSDPGPushReply              │
└────────────┬────────────────────────────┘
             ↓
┌─────────────────────────────────────────┐
│  6. Primary 确认                         │
│     handle_push_reply()                  │
│       ↓                                  │
│     - 从 missing 移除对象                │
│     - 如果 missing 为空 → Recovered     │
│       否则 → 继续恢复下一个对象         │
└─────────────────────────────────────────┘
```

**恢复优化**：
```
1. 优先级
   - 客户端正在访问的对象优先
   - degraded（降级）对象优先于 unfound

2. 并发控制
   - osd_recovery_max_active：最大并发恢复数
   - 避免影响客户端 I/O

3. 限流
   - osd_recovery_max_single_start
   - osd_recovery_sleep：恢复间隔休眠

4. 智能调度
   - 低流量时加速恢复
   - 高流量时降低恢复优先级
```

---

### 4. Scrub（清洗）流程

```
定期触发或手动触发
  ↓
OSDScrub::scrub()
  ↓
PG::scrub()
  ↓
PgScrubber::scrub()
  ├─ 1. Primary 发起
  │    - 预约副本（reserve scrub slot）
  │    - 生成 ScrubMap
  │      ├─ 遍历对象
  │      ├─ 读取元数据（size, mtime）
  │      ├─ 计算校验和（deep scrub）
  │      └─ 记录到 ScrubMap
  │
  ├─ 2. 请求副本 ScrubMap
  │    - 发送 MOSDRepScrub
  │
  ├─ 3. 对比 ScrubMap
  │    - Primary vs Replica 1
  │    - Primary vs Replica 2
  │    - 发现不一致：
  │      ├─ 元数据不一致
  │      ├─ 校验和不一致
  │      └─ 对象缺失
  │
  └─ 4. 修复（如果启用 auto-repair）
       - 从多数派读取正确版本
       - 覆盖错误副本
```

**Scrub 类型**：
```
1. Light Scrub（轻量清洗）
   - 只对比元数据（size, mtime, omap 大小）
   - 频率：每天一次
   - 开销小，快速发现问题

2. Deep Scrub（深度清洗）
   - 逐字节校验数据
   - 计算并对比 CRC32
   - 频率：每周一次
   - 开销大，彻底验证数据完整性
```

---

## 📊 四、性能优化技术

### 1. OpScheduler（操作调度器）

```cpp
// mClock 调度器：QoS 保证
class mClockScheduler : public OpScheduler {
  // 三个队列，不同优先级
  PriorityQueue client_queue;      // 客户端请求
  PriorityQueue recovery_queue;    // 恢复操作
  PriorityQueue scrub_queue;       // 清洗操作
  
  // mClock 参数
  // - Reservation（预留）：最小保证
  // - Limit（限制）：最大允许
  // - Weight（权重）：相对优先级
  
  OpRequestRef dequeue() {
    // 按 mClock 算法调度
    // 保证客户端 I/O 不被恢复阻塞
  }
};
```

### 2. 对象上下文缓存

```cpp
struct ObjectContext {
  hobject_t soid;                  // 对象 ID
  
  // 元数据（避免重复查询存储）
  struct stat st;                  // 对象状态
  SnapSet snapset;                 // 快照集
  
  // 读写锁
  RWLock rwlock;
  
  // 版本信息
  eversion_t last_update;
  
  // 引用计数
  atomic<int> ref;
};

// 缓存在 PG 中
std::map<hobject_t, ObjectContextRef> object_contexts;
```

### 3. PG 分片（Sharding）

```cpp
// OSD 将 PG 分散到多个 Shard
struct OSDShard {
  int shard_id;
  
  // 每个 Shard 有独立的：
  ThreadPool::TPHandle tp_handle;     // 线程池句柄
  std::list<PGRef> pg_list;          // PG 列表
  ceph::mutex lock;                   // 锁
  
  // 减少锁竞争
  // 提高并发性
};

// OSD 启动时创建多个 Shard
std::vector<std::unique_ptr<OSDShard>> shards;
```

### 4. BlueStore 优化

```cpp
// 1. 延迟分配（Deferred Allocation）
//    - 写入时不立即分配块
//    - 累积多个写入后批量分配
//    - 减少碎片

// 2. 覆盖写优化
//    - 对于已分配的块，直接覆盖
//    - 无需 RMW（Read-Modify-Write）

// 3. 压缩
//    - 透明数据压缩（snappy, zlib, lz4）
//    - 可配置压缩阈值

// 4. Cache
//    - Data Cache：数据块缓存
//    - Meta Cache：元数据缓存
//    - 基于 LRU 淘汰
```

---

## 🎯 五、重要数据结构

### 1. hobject_t（Hashed Object ID）

```cpp
struct hobject_t {
  object_t oid;           // 对象名
  snapid_t snap;          // 快照 ID
  uint32_t hash;          // CRUSH 哈希值
  int64_t pool;           // Pool ID
  string nspace;          // 命名空间
  string key;             // 排序键
  
  // 对象在 PG 内的唯一标识
  // 用于：
  //   - CRUSH 计算 PG
  //   - PG 内对象排序
  //   - 恢复时对象选择
};
```

### 2. eversion_t（版本号）

```cpp
struct eversion_t {
  epoch_t epoch;          // OSD Map epoch
  version_t version;      // PG 内部版本

  // 唯一标识 PG 内的每次操作
  // 用于：
  //   - PG Log 排序
  //   - 冲突解决
  //   - Peering 对比
};
```

### 3. OSDMap（集群拓扑）

```cpp
class OSDMap {
  epoch_t epoch;                      // 当前 epoch
  
  // OSD 状态
  std::vector<uint8_t> osd_state;     // up, down, in, out
  std::vector<uint32_t> osd_weight;   // CRUSH 权重
  std::map<int, entity_addr_t> osd_addrs;
  
  // Pool 信息
  std::map<int64_t, pg_pool_t> pools;
  
  // CRUSH Map
  CrushWrapper crush;
  
  // PG 映射
  void pg_to_up_acting_osds(
    pg_t pgid,
    vector<int> *up,         // 应该在这些 OSD
    vector<int> *acting      // 实际在这些 OSD
  );
};
```

---

## 🔧 六、调试与监控

### 1. 性能计数器

```bash
# 查看 OSD 性能统计
ceph daemon osd.0 perf dump

# 关键指标：
# - op_latency：操作延迟
# - op_r/op_w：读写操作数
# - subop_latency：副本操作延迟
# - commit_latency：提交延迟
# - apply_latency：应用延迟
```

### 2. OSD 状态查询

```bash
# OSD 状态
ceph osd stat
ceph osd tree
ceph osd df

# PG 状态
ceph pg stat
ceph pg dump
ceph pg <pgid> query

# 恢复状态
ceph osd pool stats
ceph -s
```

### 3. 日志级别

```bash
# 提高 OSD 日志级别
ceph tell osd.0 config set debug_osd 20
ceph tell osd.0 config set debug_ms 5

# PG 特定日志
ceph tell osd.0 config set debug_pg 20

# BlueStore 日志
ceph tell osd.0 config set debug_bluestore 20
```

### 4. Admin Socket

```bash
# 连接 OSD admin socket
ceph daemon osd.0 help

# 查看 PG 详情
ceph daemon osd.0 dump_pg_state
ceph daemon osd.0 dump_ops_in_flight

# 查看 BlueStore 统计
ceph daemon osd.0 bluestore allocator dump
```

---

## 📝 七、代码阅读路线

### 阶段 1：基础理解（2-3天）
1. `osd/osd_types.h` - 核心数据类型
2. `osd/OSDMap.h` - 集群拓扑
3. `osd/PG.h` - PG 基本概念（只看类定义）

### 阶段 2：I/O 路径（5-7天）
1. `osd/OSD.cc::ms_dispatch()` - 消息入口
2. `osd/PrimaryLogPG.cc::do_op()` - 操作处理
3. `osd/ReplicatedBackend.cc` - 副本逻辑
4. `os/bluestore/BlueStore.cc::queue_transaction()` - 存储写入

### 阶段 3：Peering 与恢复（7-10天）
1. `osd/PeeringState.h` - 状态机定义
2. `osd/PeeringState.cc` - Peering 流程
3. `osd/PGLog.h` - 日志结构
4. `osd/PrimaryLogPG.cc::recover_object()` - 恢复逻辑

### 阶段 4：高级特性（持续）
1. EC 实现：`osd/ECBackend.cc`
2. Scrub：`osd/scrubber/`
3. 调度器：`osd/scheduler/`

---

## 🎓 八、总结

Ceph OSD 是一个**高度复杂的分布式存储引擎**：

### 核心特点：
1. ✅ **智能数据放置**：CRUSH + PG 双层映射
2. ✅ **强一致性**：PG Log + Peering 保证
3. ✅ **自我修复**：自动恢复、Scrub 验证
4. ✅ **高性能**：并行 I/O、智能调度、缓存优化
5. ✅ **多副本支持**：副本 + 纠删码

### 代码量：
```
src/osd/       ~150,000 行 C++
src/os/        ~100,000 行 C++
总计：         ~250,000 行核心代码
```

### 关键洞察：
- **PG 是核心抽象**：所有操作都是针对 PG 的
- **Peering 是一致性基石**：确保所有副本状态一致
- **PG Log 加速恢复**：避免全量扫描
- **BlueStore 绕过文件系统**：直接管理裸设备，性能更优

这是一个值得深入研究的**工业级存储系统**！🚀

