# OSDService 创建流程详解

## 概述

`OSDService` 是 **由 `OSD` 类创建和拥有的**，作为 `OSD` 类的成员变量，在 `OSD` 对象构造时自动初始化。

---

## 一、创建关系

```
程序启动 (main)
    │
    ├─ 创建各种 Messenger
    ├─ 创建 MonClient
    │
    ▼
创建 OSD 对象 (new OSD(...))
    │
    ├─ OSD 构造函数初始化
    │
    └─ OSDService 作为成员变量初始化
        ├─ service(this, poolctx)
        └─ OSDService(OSD *osd, io_context_pool& poolctx)
```

**关键点**：
- ✅ `OSDService` 不是独立创建的
- ✅ `OSDService` 是 `OSD` 类的成员变量
- ✅ `OSDService` 在 `OSD` 构造函数的初始化列表中创建
- ✅ `OSDService` 的生命周期与 `OSD` 对象绑定

---

## 二、详细创建流程

### 步骤 1：程序入口 (src/ceph_osd.cc)

```cpp
// src/ceph_osd.cc 第 120 行
int main(int argc, const char **argv)
{
  // 1. 初始化 CephContext
  auto cct = global_init(...);
  
  // 2. 解析命令行参数
  // 获取 whoami (OSD ID)
  int whoami = strtol(id, &end, 10);
  
  // 3. 创建 ObjectStore
  std::unique_ptr<ObjectStore> store = 
    ObjectStore::create(cct, store_type, ...);
  
  // 4. 创建各种 Messenger
  Messenger *ms_public = ...;        // 客户端网络
  Messenger *ms_cluster = ...;       // 集群网络
  Messenger *ms_hb_front_client = ...; // 心跳前端客户端
  Messenger *ms_hb_back_client = ...;  // 心跳后端客户端
  Messenger *ms_hb_front_server = ...; // 心跳前端服务端
  Messenger *ms_hb_back_server = ...;  // 心跳后端服务端
  Messenger *ms_objecter = ...;      // Objecter 网络
  
  // 5. 创建 MonClient
  MonClient mc(g_ceph_context, poolctx);
  mc.build_initial_monmap();
  
  // 6. 创建 OSD 对象 ★★★ 关键步骤 ★★★
  osdptr = new OSD(
    g_ceph_context,     // CephContext
    std::move(store),   // ObjectStore
    whoami,             // OSD ID
    ms_cluster,         // 集群 Messenger
    ms_public,          // 公共 Messenger
    ms_hb_front_client, // 心跳前端客户端
    ms_hb_back_client,  // 心跳后端客户端
    ms_hb_front_server, // 心跳前端服务端
    ms_hb_back_server,  // 心跳后端服务端
    ms_objecter,        // Objecter Messenger
    &mc,                // MonClient
    data_path,          // 数据路径
    journal_path,       // 日志路径
    poolctx             // I/O 上下文池
  );
  
  // 7. 预初始化
  osdptr->pre_init();
  
  // 8. 启动各种 Messenger
  ms_public->start();
  ms_cluster->start();
  ...
  
  // 9. 初始化 OSD
  osdptr->init();
  
  // 10. 最终初始化
  osdptr->final_init();
  
  // 11. 等待消息处理
  ms_public->wait();
  ...
  
  // 12. 清理
  delete osdptr;
}
```

---

### 步骤 2：OSD 构造函数 (src/osd/OSD.cc)

```cpp
// src/osd/OSD.cc 第 2327 行
OSD::OSD(
  CephContext *cct_,
  std::unique_ptr<ObjectStore> store_,
  int id,
  Messenger *internal_messenger,
  Messenger *external_messenger,
  Messenger *hb_client_front,
  Messenger *hb_client_back,
  Messenger *hb_front_serverm,
  Messenger *hb_back_serverm,
  Messenger *osdc_messenger,
  MonClient *mc,
  const std::string &dev,
  const std::string &jdev,
  ceph::async::io_context_pool& poolctx
) :
  Dispatcher(cct_),
  tick_timer(cct, osd_lock),
  tick_timer_without_osd_lock(cct, tick_timer_lock),
  // ... 省略大量初始化 ...
  
  // ★★★ 关键：OSDService 成员变量初始化 ★★★
  service(this, poolctx)  // 第 2386 行
{
  // OSD 构造函数体
  
  // 1. 设置 MonClient
  monc->set_messenger(client_messenger);
  
  // 2. 配置 OpTracker
  op_tracker.set_complaint_and_threshold(...);
  
  // 3. 确定调度器类型
  op_queue_type_t op_queue = get_op_queue_type();
  
  // 4. 创建 OSD Shards
  num_shards = get_num_op_shards();
  for (uint32_t i = 0; i < num_shards; i++) {
    OSDShard *one_shard = new OSDShard(i, cct, this, ...);
    shards.push_back(one_shard);
  }
}
```

**关键点**：
- `service(this, poolctx)` 在初始化列表中
- 传递 `this` 指针（OSD 对象自己）
- 传递 `poolctx`（I/O 上下文池）

---

### 步骤 3：OSDService 构造函数 (src/osd/OSD.cc)

```cpp
// src/osd/OSD.cc 第 234 行
OSDService::OSDService(OSD *osd, ceph::async::io_context_pool& poolctx) :
  // ══════════════════════════════════════════════════════
  // 从 OSD 对象获取引用
  // ══════════════════════════════════════════════════════
  osd(osd),                                 // OSD 指针
  cct(osd->cct),                           // CephContext
  whoami(osd->whoami),                     // OSD ID
  store(osd->store.get()),                 // ObjectStore
  log_client(osd->log_client),             // LogClient
  clog(osd->clog),                         // LogChannel
  pg_recovery_stats(osd->pg_recovery_stats), // 恢复统计
  cluster_messenger(osd->cluster_messenger), // 集群 Messenger
  client_messenger(osd->client_messenger),   // 客户端 Messenger
  logger(osd->logger),                     // 性能计数器
  recoverystate_perf(osd->recoverystate_perf), // 恢复状态计数器
  monc(osd->monc),                         // MonClient
  
  // ══════════════════════════════════════════════════════
  // 初始化配置缓存
  // ══════════════════════════════════════════════════════
  osd_max_object_size(cct->_conf, "osd_max_object_size"),
  osd_skip_data_digest(cct->_conf, "osd_skip_data_digest"),
  
  // ══════════════════════════════════════════════════════
  // 初始化锁
  // ══════════════════════════════════════════════════════
  publish_lock{ceph::make_mutex("OSDService::publish_lock")},
  pre_publish_lock{ceph::make_mutex("OSDService::pre_publish_lock")},
  
  // ══════════════════════════════════════════════════════
  // 初始化 Scrub 服务
  // ══════════════════════════════════════════════════════
  m_osd_scrub{cct, *this, cct->_conf},
  
  // ══════════════════════════════════════════════════════
  // 初始化 Agent 相关
  // ══════════════════════════════════════════════════════
  agent_valid_iterator(false),
  agent_ops(0),
  flush_mode_high_count(0),
  agent_active(true),
  agent_thread(this),                     // Agent 线程
  agent_stop_flag(false),
  agent_timer(osd->client_messenger->cct, agent_timer_lock),
  
  // ══════════════════════════════════════════════════════
  // 其他初始化
  // ══════════════════════════════════════════════════════
  last_recalibrate(ceph_clock_now()),
  promote_max_objects(0),
  promote_max_bytes(0),
  poolctx(poolctx),
  
  // ══════════════════════════════════════════════════════
  // 创建 Objecter（用于 Cache Tier）
  // ══════════════════════════════════════════════════════
  objecter(make_unique<Objecter>(
    osd->client_messenger->cct,
    osd->objecter_messenger,
    osd->monc,
    poolctx
  )),
  
  // ══════════════════════════════════════════════════════
  // Objecter Finishers
  // ══════════════════════════════════════════════════════
  m_objecter_finishers(cct->_conf->osd_objecter_finishers),
  
  // ══════════════════════════════════════════════════════
  // Watch 定时器
  // ══════════════════════════════════════════════════════
  watch_timer(osd->client_messenger->cct, watch_lock),
  next_notif_id(0),
  
  // ══════════════════════════════════════════════════════
  // 恢复请求定时器
  // ══════════════════════════════════════════════════════
  recovery_request_timer(cct, recovery_request_lock, false),
  
  // ══════════════════════════════════════════════════════
  // Sleep 定时器
  // ══════════════════════════════════════════════════════
  sleep_timer(cct, sleep_lock, false),
  
  // ══════════════════════════════════════════════════════
  // Reserver 和 Finisher
  // ══════════════════════════════════════════════════════
  reserver_finisher(cct),
  local_reserver(cct, &reserver_finisher,
                 cct->_conf->osd_max_backfills,
                 cct->_conf->osd_min_recovery_priority),
  remote_reserver(cct, &reserver_finisher,
                  cct->_conf->osd_max_backfills,
                  cct->_conf->osd_min_recovery_priority),
  snap_reserver(cct, &reserver_finisher,
                cct->_conf->osd_max_trimming_pgs),
  scrub_reserver(cct, &reserver_finisher,
                 cct->_conf->osd_max_scrubs),
  
  // ══════════════════════════════════════════════════════
  // 恢复统计
  // ══════════════════════════════════════════════════════
  recovery_ops_active(0),
  recovery_ops_reserved(0),
  recovery_paused(false),
  
  // ══════════════════════════════════════════════════════
  // Map 缓存
  // ══════════════════════════════════════════════════════
  map_cache(cct, cct->_conf->osd_map_cache_size),
  map_bl_cache(cct->_conf->osd_map_cache_size),
  map_bl_inc_cache(cct->_conf->osd_map_cache_size),
  
  // ══════════════════════════════════════════════════════
  // Full 状态
  // ══════════════════════════════════════════════════════
  cur_state(NONE),
  cur_ratio(0),
  physical_ratio(0),
  
  // ══════════════════════════════════════════════════════
  // Epoch 信息
  // ══════════════════════════════════════════════════════
  boot_epoch(0),
  up_epoch(0),
  bind_epoch(0)
{
  // ══════════════════════════════════════════════════════
  // 构造函数体
  // ══════════════════════════════════════════════════════
  
  // 1. 初始化 Objecter
  objecter->init();
  
  // 2. 创建 Objecter Finishers
  for (int i = 0; i < m_objecter_finishers; i++) {
    ostringstream str;
    str << "objecter-finisher-" << i;
    auto fin = make_unique<Finisher>(
      osd->client_messenger->cct,
      str.str(),
      "finisher"
    );
    objecter_finishers.push_back(std::move(fin));
  }
}
```

---

## 三、类定义关系

### OSD 类定义 (src/osd/OSD.h)

```cpp
// src/osd/OSD.h 第 1062 行
class OSD : public Dispatcher, public md_config_obs_t {
  
  // ... 大量成员变量 ...
  
public:
  // ★★★ OSDService 成员变量 ★★★
  OSDService service;  // 第 2051 行
  
  friend class OSDService;
  
  // 构造函数
  OSD(CephContext *cct_,
      std::unique_ptr<ObjectStore> store_,
      int id,
      Messenger *internal_messenger,
      Messenger *external_messenger,
      Messenger *hb_client_front,
      Messenger *hb_client_back,
      Messenger *hb_front_serverm,
      Messenger *hb_back_serverm,
      Messenger *osdc_messenger,
      MonClient *mc,
      const std::string &dev,
      const std::string &jdev,
      ceph::async::io_context_pool& poolctx);
  
  ~OSD();
  
  // ... 方法定义 ...
};
```

### OSDService 类定义 (src/osd/OSD.h)

```cpp
// src/osd/OSD.h 第 98 行
class OSDService : public Scrub::ScrubSchedListener {
public:
  // ══════════════════════════════════════════════════════
  // OSD 相关引用（从 OSD 对象获取）
  // ══════════════════════════════════════════════════════
  OSD *osd;                           // OSD 指针
  CephContext *cct;                   // CephContext
  ObjectStore::CollectionHandle meta_ch;
  const int whoami;                   // OSD ID
  ObjectStore * const store;          // ObjectStore
  LogClient &log_client;              // LogClient
  LogChannelRef clog;                 // LogChannel
  PGRecoveryStats &pg_recovery_stats; // 恢复统计
  
private:
  Messenger *&cluster_messenger;      // 集群 Messenger
  Messenger *&client_messenger;       // 客户端 Messenger
  
public:
  PerfCounters *&logger;              // 性能计数器
  PerfCounters *&recoverystate_perf;  // 恢复状态计数器
  MonClient *&monc;                   // MonClient
  
  // ... 配置、锁、定时器、缓存等 ...
  
  // ══════════════════════════════════════════════════════
  // 构造函数
  // ══════════════════════════════════════════════════════
  explicit OSDService(OSD *osd, ceph::async::io_context_pool& poolctx);
  ~OSDService() = default;
};
```

---

## 四、初始化顺序时序图

```
时间  |  操作                                      |  代码位置
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
T0    |  main() 函数启动                          | ceph_osd.cc:120
      |                                            |
T1    |  global_init()                            |
      |  - 初始化 CephContext                     |
      |  - 加载配置                               |
      |                                            |
T2    |  创建 ObjectStore                         | ceph_osd.cc:~280
      |  store = ObjectStore::create(...)         |
      |                                            |
T3    |  创建 Messenger 对象                      | ceph_osd.cc:~450
      |  ms_public = Messenger::create(...)       |
      |  ms_cluster = Messenger::create(...)      |
      |  ... (共 7 个 Messenger)                  |
      |                                            |
T4    |  创建 MonClient                           | ceph_osd.cc:690
      |  MonClient mc(g_ceph_context, poolctx)    |
      |  mc.build_initial_monmap()                |
      |                                            |
T5    |  ★ 创建 OSD 对象 ★                        | ceph_osd.cc:699
      |  osdptr = new OSD(...)                    |
      |    │                                       |
      |    ├─ 调用 OSD 构造函数                   | OSD.cc:2327
      |    │                                       |
      |    └─ 在初始化列表中初始化 service        | OSD.cc:2386
      |        service(this, poolctx)             |
      |          │                                 |
      |          └─ 调用 OSDService 构造函数      | OSD.cc:234
      |              OSDService(osd, poolctx)     |
      |                │                          |
      |                ├─ 保存 OSD 指针           |
      |                ├─ 从 OSD 获取引用         |
      |                ├─ 初始化 Objecter         |
      |                ├─ 创建 Finishers          |
      |                └─ 初始化各种组件          |
      |                                            |
T6    |  OSD::pre_init()                          | ceph_osd.cc:714
      |  - 检查存储挂载状态                       |
      |  - 注册配置观察者                         |
      |                                            |
T7    |  启动 Messenger                            | ceph_osd.cc:721
      |  ms_public->start()                       |
      |  ms_cluster->start()                      |
      |  ... (所有 Messenger)                     |
      |                                            |
T8    |  OSD::init()                              | ceph_osd.cc:730
      |  - 加载 OSD 超级块                        |
      |  - 初始化 ObjectStore                     |
      |  - 加载 OSDMap                            |
      |  - 创建 PG                                |
      |  - 启动后台线程                           |
      |                                            |
T9    |  OSD::final_init()                        | ceph_osd.cc:748
      |  - 注册到 Monitor                         |
      |  - 开始心跳                               |
      |  - 启动 tick 定时器                       |
      |                                            |
T10   |  OSD 开始处理请求                         |
      |  - Messenger 接收消息                     |
      |  - Dispatch 到 OSD                        |
      |  - OSD 处理操作                           |
```

---

## 五、OSDService 的作用

`OSDService` 是 OSD 的**服务层**，为 PG 提供各种服务：

### 1. OSDMap 管理
```cpp
// 发布当前 OSDMap
void publish_map(OSDMapRef map);

// 获取当前 OSDMap
OSDMapRef get_osdmap();

// 预发布下一个 OSDMap
void pre_publish_map(OSDMapRef map);

// 激活 Map
void activate_map();
```

### 2. 消息发送
```cpp
// 发送消息到其他 OSD（集群网络）
void send_message_osd_cluster(int peer, Message *m, epoch_t from_epoch);

// 发送消息到客户端
void send_message_osd_client(Message *m, const ConnectionRef& con);

// 获取 OSD 连接
ConnectionRef get_con_osd_cluster(int peer, epoch_t from_epoch);
```

### 3. PG 调度与队列
```cpp
// 将操作加入队列
void enqueue_back(OpSchedulerItem&& qi);
void enqueue_front(OpSchedulerItem&& qi);

// 操作成本计算
double get_cost_per_io() const;
```

### 4. 恢复管理
```cpp
// 本地恢复预留
AsyncReserver<spg_t, Finisher> local_reserver;

// 远程恢复预留
AsyncReserver<spg_t, Finisher> remote_reserver;

// 快照修剪预留
AsyncReserver<spg_t, Finisher> snap_reserver;

// 开始恢复
void queue_recovery_after_sleep(...);
```

### 5. Scrub 管理
```cpp
// Scrub 服务
OsdScrub& get_scrub_services();

// Scrub 预留
AsyncReserver<spg_t, Finisher>& get_scrub_reserver();
```

### 6. Cache Tier（缓存层）
```cpp
// Objecter（用于访问基础池）
std::unique_ptr<Objecter> objecter;

// Agent 线程（缓存管理）
AgentThread agent_thread;
void agent_entry();
```

### 7. 统计与监控
```cpp
// OSD 统计
osd_stat_t osd_stat;

// 性能计数器
PerfCounters *logger;
PerfCounters *recoverystate_perf;
```

---

## 六、关键设计特点

### 1. **组合模式**
```
OSD 包含 OSDService 作为成员
├─ OSD 负责：
│  ├─ 消息分发
│  ├─ 整体生命周期
│  └─ 配置管理
│
└─ OSDService 负责：
   ├─ PG 服务支持
   ├─ Map 管理
   ├─ 恢复调度
   └─ Scrub 管理
```

### 2. **引用传递**
```cpp
// OSDService 不拷贝，而是引用 OSD 的组件
OSD *osd;                          // 指向 OSD
CephContext *cct;                  // 引用 OSD->cct
Messenger *&cluster_messenger;     // 引用 OSD->cluster_messenger
MonClient *&monc;                  // 引用 OSD->monc
```

**优点**：
- ✅ 避免重复
- ✅ 保持一致性
- ✅ 减少内存开销

### 3. **生命周期绑定**
```cpp
// OSD 析构时，OSDService 自动析构
OSD::~OSD() {
  // ... 清理 ...
  // service 成员会自动调用析构函数
}

OSDService::~OSDService() = default;  // 使用默认析构
```

---

## 七、总结

### 创建者
**`OSDService` 由 `OSD` 类创建**，作为成员变量存在。

### 创建时机
在 **`OSD` 对象构造时**，通过初始化列表自动创建。

### 创建位置
| 文件 | 行号 | 内容 |
|------|------|------|
| `src/ceph_osd.cc` | 699 | `osdptr = new OSD(...)` |
| `src/osd/OSD.cc` | 2327 | `OSD::OSD(...)` 构造函数定义 |
| `src/osd/OSD.cc` | 2386 | `service(this, poolctx)` 初始化 |
| `src/osd/OSD.cc` | 234 | `OSDService::OSDService(...)` 构造函数定义 |
| `src/osd/OSD.h` | 2051 | `OSDService service;` 成员变量声明 |
| `src/osd/OSD.h` | 98 | `class OSDService` 类定义 |

### 关键调用栈
```
main()
  └─ new OSD(...)
      └─ OSD::OSD(...) 构造函数
          └─ service(this, poolctx)  // 初始化列表
              └─ OSDService::OSDService(osd, poolctx)  // 构造函数
                  ├─ 保存 OSD 指针
                  ├─ 从 OSD 获取各种引用
                  ├─ 创建 Objecter
                  ├─ 初始化 Finishers
                  ├─ 初始化 Scrub 服务
                  └─ 初始化各种组件
```

### 设计意图
`OSDService` 是 OSD 的**服务提供层**，将各种公共服务封装起来，供 PG 和其他组件使用，实现了关注点分离和代码复用。

✅ **OSD**：负责整体管理和消息分发  
✅ **OSDService**：提供 PG 所需的各种服务  
✅ **PG**：使用 OSDService 提供的服务处理具体的 PG 操作

