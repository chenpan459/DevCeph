# Ceph 核心组件完整解析

## 概述

Ceph 有 7 个主要可执行程序，分别负责不同的功能：

| 组件 | 文件 | 类型 | 主要作用 |
|------|------|------|---------|
| **ceph-osd** | `src/ceph_osd.cc` | 守护进程 | 对象存储守护进程 |
| **ceph-mon** | `src/ceph_mon.cc` | 守护进程 | 监视器守护进程 |
| **ceph-mgr** | `src/ceph_mgr.cc` | 守护进程 | 管理器守护进程 |
| **ceph-mds** | `src/ceph_mds.cc` | 守护进程 | 元数据服务器守护进程 |
| **ceph-fuse** | `src/ceph_fuse.cc` | 客户端 | CephFS FUSE 挂载客户端 |
| **ceph-syn** | `src/ceph_syn.cc` | 工具 | 合成负载测试工具 |
| **ceph_ver** | `src/ceph_ver.c` | 库符号 | 版本信息符号 |

---

## 一、ceph-osd（对象存储守护进程）

### 📋 基本信息

- **文件**：`src/ceph_osd.cc`
- **核心类**：`OSD` (src/osd/OSD.h)
- **端口**：
  - 6800：公共网络（客户端通信）
  - 6801：集群网络（OSD 间通信）
  - 6802-6803：心跳网络
- **进程名**：`ceph-osd`

### 🎯 核心作用

1. **数据存储**：
   - 存储对象数据到本地磁盘
   - 支持 BlueStore、FileStore 后端
   - 管理 PG（Placement Group）

2. **数据复制与恢复**：
   - Primary-Replica 复制
   - 纠删码（Erasure Code）
   - 自动恢复故障数据

3. **数据一致性**：
   - Peering 过程保证一致性
   - Scrubbing（数据校验）
   - PG Log 记录操作

4. **I/O 处理**：
   - 处理客户端读写请求
   - 处理 OSD 间的复制请求
   - 操作调度（mClock/WPQ）

### 🔄 启动流程

```
main() [ceph_osd.cc:120]
  │
  ├─ 1. global_init()
  │    └─ 初始化 CephContext
  │
  ├─ 2. 创建 ObjectStore
  │    └─ BlueStore/FileStore
  │
  ├─ 3. 创建 7 个 Messenger
  │    ├─ ms_public (客户端)
  │    ├─ ms_cluster (集群)
  │    ├─ ms_hb_front_client (心跳前端客户端)
  │    ├─ ms_hb_back_client (心跳后端客户端)
  │    ├─ ms_hb_front_server (心跳前端服务端)
  │    ├─ ms_hb_back_server (心跳后端服务端)
  │    └─ ms_objecter (Objecter)
  │
  ├─ 4. 创建 MonClient
  │    └─ 与 Monitor 通信
  │
  ├─ 5. new OSD(...)
  │    ├─ OSD 构造函数
  │    └─ 创建 OSDService
  │
  ├─ 6. pre_init()
  │    └─ 检查存储状态
  │
  ├─ 7. 启动所有 Messenger
  │
  ├─ 8. init()
  │    ├─ 加载超级块
  │    ├─ 初始化 ObjectStore
  │    ├─ 加载 OSDMap
  │    ├─ 加载 PG
  │    └─ 启动工作线程
  │
  ├─ 9. final_init()
  │    ├─ 注册到 Monitor
  │    ├─ 启动心跳
  │    └─ 启动 tick 定时器
  │
  └─ 10. 进入消息循环
       └─ 处理 I/O 请求
```

### 📊 关键参数

```bash
# 启动命令
ceph-osd -i 0 \
  --osd-data /var/lib/ceph/osd/ceph-0 \
  --osd-journal /var/lib/ceph/osd/ceph-0/journal
```

---

## 二、ceph-mon（监视器守护进程）

### 📋 基本信息

- **文件**：`src/ceph_mon.cc`
- **核心类**：`Monitor` (src/mon/Monitor.h)
- **端口**：3300（默认）或 6789（旧版）
- **进程名**：`ceph-mon`

### 🎯 核心作用

1. **集群地图管理**：
   - **MonMap**：Monitor 集群地图
   - **OSDMap**：OSD 集群地图（最重要）
   - **PGMap**：PG 状态地图
   - **MDSMap**：MDS 地图
   - **CrushMap**：CRUSH 算法地图

2. **共识协议 Paxos**：
   - 使用 Paxos 算法保证一致性
   - Leader Election（领导选举）
   - 提案提交与确认

3. **集群健康监控**：
   - OSD 状态监控（up/down, in/out）
   - 集群健康状态（HEALTH_OK/WARN/ERR）
   - 告警管理

4. **认证管理**：
   - CephX 认证
   - 密钥管理
   - 权限控制

### 🔄 启动流程

```
main() [ceph_mon.cc:主函数]
  │
  ├─ 1. global_init()
  │    └─ 初始化 CephContext
  │
  ├─ 2. 创建 MonitorDBStore
  │    └─ 持久化存储（RocksDB）
  │
  ├─ 3. 加载 MonMap
  │    ├─ 从存储加载
  │    └─ 或从 mkfs 初始化
  │
  ├─ 4. 创建 Messenger
  │    └─ Monitor 集群通信
  │
  ├─ 5. new Monitor(...)
  │    ├─ Monitor 构造函数
  │    ├─ 创建 Paxos 实例
  │    ├─ 创建各种 PaxosService
  │    │   ├─ OSDMonitor（管理 OSDMap）
  │    │   ├─ AuthMonitor（管理认证）
  │    │   ├─ MDSMonitor（管理 MDS）
  │    │   └─ ...
  │    └─ 创建 MonitorDBStore
  │
  ├─ 6. bootstrap()
  │    ├─ 如果是新集群：创建初始 map
  │    └─ 如果加入现有集群：同步数据
  │
  ├─ 7. init()
  │    ├─ 启动 Paxos
  │    ├─ 启动 Elector（选举）
  │    └─ 启动各种 Service
  │
  └─ 8. 进入消息循环
       ├─ 处理客户端请求
       ├─ 处理 OSD 报告
       └─ 处理 Paxos 消息
```

### 📊 关键参数

```bash
# 启动命令
ceph-mon -i a \
  --mon-data /var/lib/ceph/mon/ceph-a \
  --public-addr 192.168.1.10:6789

# Monitor 集群需要奇数个节点（如 3、5、7）
```

### 🔑 核心组件

```
Monitor
  ├─ Paxos (共识算法)
  │   ├─ Proposer (提案者)
  │   ├─ Acceptor (接受者)
  │   └─ Learner (学习者)
  │
  ├─ Elector (选举器)
  │   └─ Leader Election
  │
  ├─ PaxosService (Paxos 服务)
  │   ├─ OSDMonitor (OSDMap 管理)
  │   ├─ AuthMonitor (认证管理)
  │   ├─ MDSMonitor (MDS 管理)
  │   ├─ PGMonitor (PG 统计)
  │   └─ LogMonitor (日志管理)
  │
  └─ MonitorDBStore (持久化)
      └─ RocksDB 后端
```

---

## 三、ceph-mgr（管理器守护进程）

### 📋 基本信息

- **文件**：`src/ceph_mgr.cc`
- **核心类**：`MgrStandby` (src/mgr/MgrStandby.h)
- **端口**：
  - 6800：默认端口
  - 8443：Dashboard HTTPS
  - 9283：Prometheus exporter
- **进程名**：`ceph-mgr`
- **引入版本**：Ceph Luminous (v12)

### 🎯 核心作用

1. **插件式管理框架**（基于 Python）：
   - **Dashboard**：Web 管理界面
   - **Prometheus**：监控指标导出
   - **Balancer**：自动 PG 平衡
   - **Telemetry**：遥测数据收集
   - **Orchestrator**：编排管理（cephadm）
   - **RGW**：RGW 管理
   - **Alerts**：告警管理

2. **集群监控与统计**：
   - 收集 OSD、PG、Pool 统计信息
   - 性能指标聚合
   - 历史数据存储

3. **主备模式**：
   - Active Manager：处理请求
   - Standby Manager：热备

4. **RESTful API**：
   - 提供 HTTP API
   - Dashboard 后端
   - 第三方集成接口

### 🔄 启动流程

```
main() [ceph_mgr.cc:42]
  │
  ├─ 1. global_init()
  │    └─ 初始化 CephContext
  │
  ├─ 2. MgrStandby mgr(argc, argv)
  │    ├─ 创建 MgrStandby 对象
  │    ├─ 初始化 Python 解释器
  │    └─ 加载管理模块
  │
  ├─ 3. mgr.init()
  │    ├─ 创建 MonClient
  │    ├─ 创建 Messenger
  │    ├─ 连接 Monitor
  │    └─ 初始化所有模块
  │
  └─ 4. mgr.main(args)
       ├─ 进入主循环
       ├─ 如果是 Active：
       │   ├─ 处理客户端请求
       │   ├─ 收集集群数据
       │   └─ 运行各个模块
       └─ 如果是 Standby：
           └─ 等待成为 Active
```

### 📊 核心模块

| 模块 | 功能 | 端口 |
|------|------|------|
| **dashboard** | Web UI 管理界面 | 8443 |
| **prometheus** | Prometheus 指标导出 | 9283 |
| **balancer** | 自动 PG 平衡 | - |
| **orchestrator** | 集群编排（cephadm） | - |
| **telemetry** | 遥测数据收集 | - |
| **devicehealth** | 设备健康监控（SMART） | - |
| **crash** | 崩溃报告管理 | - |
| **alerts** | 告警管理 | - |
| **restful** | RESTful API | 8003 |
| **zabbix** | Zabbix 集成 | - |

### 🔧 关键参数

```bash
# 启动命令
ceph-mgr -i x --keyring /var/lib/ceph/mgr/ceph-x/keyring

# 主备模式（需要至少 2 个 mgr）
ceph-mgr -i a   # Active
ceph-mgr -i b   # Standby
```

---

## 四、ceph-mds（元数据服务器守护进程）

### 📋 基本信息

- **文件**：`src/ceph_mds.cc`
- **核心类**：`MDSDaemon` (src/mds/MDSDaemon.h)
- **端口**：6800+（动态分配）
- **进程名**：`ceph-mds`
- **用途**：仅用于 **CephFS**

### 🎯 核心作用

1. **文件系统元数据管理**：
   - **Inode** 信息（文件属性）
   - **Dentry** 信息（目录项）
   - **目录树结构**
   - **文件权限与所有者**

2. **元数据缓存**：
   - 热数据缓存
   - LRU 策略
   - 客户端能力（Capability）管理

3. **负载均衡**：
   - 动态子树分区（Dynamic Subtree Partitioning）
   - 多 Active MDS 支持
   - 热点迁移

4. **一致性保障**：
   - 分布式锁
   - 日志（Journal）
   - 快照（Snapshot）

### 🔄 启动流程

```
main() [ceph_mds.cc:82]
  │
  ├─ 1. global_init()
  │    └─ 初始化 CephContext (ENTITY_TYPE_MDS)
  │
  ├─ 2. 创建 MonClient
  │    └─ 连接 Monitor 集群
  │
  ├─ 3. 创建 Messenger
  │    └─ MDS 通信
  │
  ├─ 4. new MDSDaemon(...)
  │    ├─ MDSDaemon 构造函数
  │    ├─ 创建 Beacon
  │    ├─ 创建 Server
  │    ├─ 创建 Locker
  │    └─ 创建 MDCache
  │
  ├─ 5. init()
  │    ├─ 启动 Messenger
  │    ├─ 连接 Monitor
  │    ├─ 获取 MDSMap
  │    └─ 确定自己的 rank
  │
  ├─ 6. 状态机启动
  │    ├─ STANDBY → REPLAY → RECONNECT → REJOIN → ACTIVE
  │    └─ 或保持 STANDBY
  │
  └─ 7. 进入消息循环
       ├─ 处理客户端请求（open, read, write, mkdir...）
       ├─ 处理其他 MDS 消息
       └─ 元数据日志持久化
```

### 📊 MDS 状态机

```
┌──────────┐
│  BOOT    │
└────┬─────┘
     │
     ▼
┌──────────┐     ┌──────────┐
│ STANDBY  │────→│ STANDBY- │
│          │     │  REPLAY  │
└──────────┘     └────┬─────┘
                      │
                      ▼
                 ┌──────────┐
                 │  REPLAY  │
                 └────┬─────┘
                      │
                      ▼
                 ┌──────────┐
                 │RECONNECT │
                 └────┬─────┘
                      │
                      ▼
                 ┌──────────┐
                 │ REJOIN   │
                 └────┬─────┘
                      │
                      ▼
                 ┌──────────┐
                 │ ACTIVE   │◄──┐
                 └────┬─────┘   │
                      └─────────┘
```

### 🔧 关键参数

```bash
# 启动命令
ceph-mds -i myfs-a \
  --mds-data /var/lib/ceph/mds/ceph-myfs-a

# 多 Active MDS（例如 2 个 Active）
ceph fs set myfs max_mds 2
```

### 📂 元数据结构

```
MDSDaemon
  ├─ MDCache (元数据缓存)
  │   ├─ CInode (Cached Inode)
  │   ├─ CDentry (Cached Dentry)
  │   └─ CDir (Cached Directory)
  │
  ├─ Server (客户端请求处理)
  │   ├─ handle_client_request()
  │   └─ dispatch_client_request()
  │
  ├─ Locker (分布式锁)
  │   ├─ SimpleLock
  │   ├─ ScatterLock
  │   └─ FileLock
  │
  ├─ MDLog (元数据日志)
  │   └─ LogEvent
  │
  └─ Migrator (负载迁移)
      └─ export_dir() / import_dir()
```

---

## 五、ceph-fuse（CephFS FUSE 客户端）

### 📋 基本信息

- **文件**：`src/ceph_fuse.cc`
- **核心类**：`Client` (src/client/Client.h) + FUSE 接口
- **类型**：用户态文件系统客户端
- **进程名**：`ceph-fuse`
- **技术**：FUSE (Filesystem in Userspace)

### 🎯 核心作用

1. **文件系统挂载**：
   - 在用户态实现文件系统
   - 无需内核模块（与 kernel client 相对）
   - 跨平台支持（Linux, macOS, FreeBSD）

2. **POSIX 兼容**：
   - 标准文件操作（open, read, write, close）
   - 目录操作（mkdir, rmdir, readdir）
   - 权限管理（chmod, chown）
   - 扩展属性（xattr）

3. **性能优化**：
   - 元数据缓存
   - 数据缓存
   - 预读（Readahead）
   - 异步 I/O

### 🔄 启动流程

```
main() [ceph_fuse.cc:92]
  │
  ├─ 1. global_init()
  │    └─ 初始化 CephContext (ENTITY_TYPE_CLIENT)
  │
  ├─ 2. 解析 FUSE 参数
  │    ├─ 挂载点
  │    ├─ FUSE 选项
  │    └─ Ceph 选项
  │
  ├─ 3. 创建 MonClient
  │    └─ 连接 Monitor
  │
  ├─ 4. 创建 Messenger
  │    └─ 客户端网络通信
  │
  ├─ 5. new Client(...)
  │    ├─ Client 构造函数
  │    ├─ 创建 ObjectCacher
  │    └─ 创建 Filer
  │
  ├─ 6. client->init()
  │    ├─ 连接 Monitor
  │    ├─ 获取 MDSMap
  │    └─ 连接 MDS
  │
  ├─ 7. client->mount()
  │    ├─ 挂载文件系统
  │    ├─ 获取根目录 inode
  │    └─ 建立会话
  │
  ├─ 8. 创建 FUSE 会话
  │    └─ fuse_lowlevel_new()
  │
  ├─ 9. 注册 FUSE 回调
  │    ├─ getattr → client_ll_getattr()
  │    ├─ lookup → client_ll_lookup()
  │    ├─ open → client_ll_open()
  │    ├─ read → client_ll_read()
  │    ├─ write → client_ll_write()
  │    └─ ... (所有文件系统操作)
  │
  └─ 10. fuse_session_loop()
        └─ 进入 FUSE 事件循环
```

### 🔧 使用方式

```bash
# 基本挂载
ceph-fuse /mnt/cephfs

# 指定子目录挂载
ceph-fuse -r /subdir /mnt/mycephfs

# 指定配置和用户
ceph-fuse -n client.foo \
  -c /etc/ceph/ceph.conf \
  -k /etc/ceph/ceph.client.foo.keyring \
  /mnt/cephfs

# FUSE 选项
ceph-fuse /mnt/cephfs -o allow_other,default_permissions
```

### 📊 FUSE 架构

```
┌─────────────────────────────────────┐
│        用户应用程序                  │
│   (ls, cat, vim, etc.)              │
└────────────┬────────────────────────┘
             │ syscall (read, write...)
             ▼
┌─────────────────────────────────────┐
│         内核 VFS 层                  │
└────────────┬────────────────────────┘
             │
             ▼
┌─────────────────────────────────────┐
│        FUSE 内核模块                 │
└────────────┬────────────────────────┘
             │ /dev/fuse
             ▼
┌─────────────────────────────────────┐
│       ceph-fuse (用户态)             │
│  ┌───────────────────────────────┐  │
│  │ FUSE Lowlevel API             │  │
│  ├───────────────────────────────┤  │
│  │ Client                        │  │
│  │  ├─ MetadataCache             │  │
│  │  ├─ ObjectCacher              │  │
│  │  └─ Filer                     │  │
│  └───────────────────────────────┘  │
└────────────┬────────────────────────┘
             │ librados
             ▼
┌─────────────────────────────────────┐
│        Ceph 集群                     │
│   ┌─────────┐  ┌─────────┐          │
│   │   MDS   │  │   OSD   │          │
│   └─────────┘  └─────────┘          │
└─────────────────────────────────────┘
```

---

## 六、ceph-syn（合成负载测试工具）

### 📋 基本信息

- **文件**：`src/ceph_syn.cc`
- **核心类**：`SyntheticClient` (src/client/SyntheticClient.h)
- **类型**：测试工具
- **进程名**：`ceph-syn`

### 🎯 核心作用

1. **性能测试**：
   - 模拟多客户端并发访问
   - 各种工作负载模式
   - 压力测试

2. **功能测试**：
   - 文件系统操作测试
   - 元数据操作测试
   - 边界条件测试

3. **工作负载生成**：
   - **makedir**：创建目录树
   - **makedirs**：批量创建目录
   - **makefiles**：批量创建文件
   - **readfile**：顺序读文件
   - **writefile**：顺序写文件
   - **walk**：目录遍历
   - **randomwalk**：随机目录遍历

### 🔄 启动流程

```
main() [ceph_syn.cc:41]
  │
  ├─ 1. global_init()
  │    └─ 初始化 CephContext (ENTITY_TYPE_CLIENT)
  │
  ├─ 2. parse_syn_options()
  │    └─ 解析测试参数
  │
  ├─ 3. 创建 MonClient
  │    └─ 连接 Monitor
  │
  ├─ 4. 创建多个客户端
  │    └─ for (i = 0; i < num_client; i++)
  │        ├─ 创建 Messenger
  │        ├─ new Client()
  │        └─ new SyntheticClient()
  │
  ├─ 5. 启动客户端线程
  │    └─ client->start_thread()
  │
  └─ 6. 等待测试完成
       └─ client->join_thread()
```

### 🔧 使用示例

```bash
# 创建目录树
ceph-syn --syn makedir 0 100 10

# 创建文件
ceph-syn --syn makefiles 0 100 1000

# 多客户端测试
ceph-syn --num_client 10 --syn makedirs 0 10 100

# 读写测试
ceph-syn --syn writefile 0 1048576 1024    # 写 1GB 文件
ceph-syn --syn readfile /testfile          # 读文件

# 混合负载
ceph-syn --syn trace /path/to/trace.txt
```

### 📊 测试场景

| 命令 | 说明 | 用途 |
|------|------|------|
| `makedir` | 创建目录 | 元数据性能测试 |
| `makedirs` | 批量创建目录 | 元数据压力测试 |
| `makefiles` | 批量创建文件 | 文件创建性能 |
| `readfile` | 读文件 | 顺序读性能 |
| `writefile` | 写文件 | 顺序写性能 |
| `walk` | 目录遍历 | 目录查询性能 |
| `opentest` | 打开/关闭测试 | 元数据缓存测试 |

---

## 七、ceph_ver（版本符号）

### 📋 基本信息

- **文件**：`src/ceph_ver.c` + `src/ceph_ver.h.in.cmake`
- **类型**：编译时版本符号
- **用途**：版本信息嵌入

### 🎯 核心作用

1. **版本标识**：
   - 嵌入 Git 版本信息
   - 用于版本检查
   - 防止不同版本混用

2. **符号定义**：
   ```c
   // ceph_ver.c
   #define CONCAT_VER_SYMBOL(x) ceph_ver__##x
   #define DEFINE_VER_SYMBOL(x) int CONCAT_VER_SYMBOL(x)
   
   DEFINE_VER_SYMBOL(CEPH_GIT_VER);
   ```

3. **链接时检查**：
   - 每个二进制引用特定版本符号
   - 不同版本符号不兼容
   - 链接时自动检测版本冲突

### 📊 版本信息示例

```bash
# Ceph 版本
ceph --version
# ceph version 19.2.3 (commit_hash) quincy (stable)

# 版本符号（nm 命令查看）
nm /usr/bin/ceph-osd | grep ceph_ver
# U ceph_ver__19_2_3_commit_hash
```

---

## 八、组件协作流程

### 🔄 完整集群启动顺序

```
1. ceph-mon (Monitor 集群)
   ├─ 启动 Monitor 守护进程
   ├─ Paxos 选举 Leader
   ├─ 创建初始 OSDMap
   └─ 等待 OSD 注册

2. ceph-mgr (Manager)
   ├─ 启动 Manager 守护进程
   ├─ 连接 Monitor
   ├─ 选举 Active Manager
   └─ 启动各个管理模块

3. ceph-osd (OSD)
   ├─ 启动 OSD 守护进程
   ├─ 连接 Monitor
   ├─ 注册到集群
   ├─ 获取 OSDMap
   ├─ 加载 PG
   └─ 开始提供服务

4. ceph-mds (MDS, 可选)
   ├─ 启动 MDS 守护进程
   ├─ 连接 Monitor
   ├─ 获取 rank
   └─ 提供元数据服务

5. ceph-fuse (客户端, 可选)
   ├─ 启动客户端
   ├─ 连接 Monitor
   ├─ 连接 MDS
   ├─ 挂载文件系统
   └─ 提供 POSIX 接口
```

### 📊 组件间通信

```
                    ┌─────────────┐
                    │   ceph-mon  │
                    │  (Monitor)  │
                    └──────┬──────┘
                           │
            ┌──────────────┼──────────────┐
            │              │              │
            ▼              ▼              ▼
      ┌──────────┐   ┌──────────┐   ┌──────────┐
      │ ceph-mgr │   │ ceph-osd │   │ ceph-mds │
      │(Manager) │   │  (OSD)   │   │  (MDS)   │
      └────┬─────┘   └────┬─────┘   └────┬─────┘
           │              │              │
           │              │              │
           └──────────────┼──────────────┘
                          │
                          ▼
                    ┌──────────┐
                    │ceph-fuse │
                    │ (Client) │
                    └──────────┘
```

### 🔑 关键交互

1. **OSD → Monitor**：
   - 心跳（Heartbeat）
   - 状态报告（PG 状态、统计信息）
   - 请求 OSDMap 更新

2. **Manager → Monitor**：
   - 获取集群状态
   - 收集统计信息
   - 执行管理命令

3. **MDS → Monitor**：
   - 心跳
   - MDSMap 更新
   - 状态报告

4. **Client → Monitor**：
   - 获取集群地图
   - 认证

5. **Client → OSD**：
   - I/O 请求（读写）

6. **Client → MDS**：
   - 元数据操作（CephFS）

---

## 九、总结对比表

| 组件 | 角色 | 数量要求 | 主要职责 | 关键技术 |
|------|------|---------|---------|---------|
| **ceph-mon** | 守护进程 | 奇数个（3/5/7） | 集群地图管理、共识 | Paxos, RocksDB |
| **ceph-mgr** | 守护进程 | ≥2（主备） | 监控、管理、插件 | Python, RESTful |
| **ceph-osd** | 守护进程 | ≥3 | 数据存储、复制 | BlueStore, CRUSH |
| **ceph-mds** | 守护进程 | ≥1（仅 CephFS） | 元数据管理 | 动态子树分区 |
| **ceph-fuse** | 客户端 | 按需 | 文件系统挂载 | FUSE, librados |
| **ceph-syn** | 工具 | 测试用 | 负载测试 | SyntheticClient |
| **ceph_ver** | 符号 | 编译时 | 版本标识 | 链接符号 |

---

## 十、进程关系图

```
╔═══════════════════════════════════════════════════════════╗
║                    Ceph 集群架构                          ║
╚═══════════════════════════════════════════════════════════╝

┌─────────────────────────────────────────────────────────┐
│                    Monitor 集群                          │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐           │
│  │ ceph-mon  │  │ ceph-mon  │  │ ceph-mon  │           │
│  │    (a)    │  │    (b)    │  │    (c)    │           │
│  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘           │
│        └────────────┬────────────────┘                  │
│                     │ Paxos 共识                        │
└─────────────────────┼─────────────────────────────────┘
                      │
        ┌─────────────┼─────────────┬─────────────┐
        │             │             │             │
        ▼             ▼             ▼             ▼
┌─────────────┐ ┌─────────────────────────────────────┐
│  ceph-mgr   │ │          OSD 集群                    │
│  ┌────────┐ │ │  ┌─────────┐ ┌─────────┐ ┌────────┐ │
│  │Active  │ │ │  │ceph-osd │ │ceph-osd │ │ceph-osd│ │
│  │Manager │ │ │  │   (0)   │ │   (1)   │ │  (2)   │ │
│  └────────┘ │ │  └─────────┘ └─────────┘ └────────┘ │
│  ┌────────┐ │ │                                      │
│  │Standby │ │ │  ... 更多 OSD ...                    │
│  │Manager │ │ └─────────────────────────────────────┘
│  └────────┘ │
└─────────────┘        ▼
                 ┌─────────────────────────────────────┐
                 │          MDS 集群 (CephFS)          │
                 │  ┌─────────┐ ┌─────────┐            │
                 │  │ceph-mds │ │ceph-mds │            │
                 │  │ (Active)│ │(Standby)│            │
                 │  └─────────┘ └─────────┘            │
                 └──────────────┬──────────────────────┘
                                │
                                ▼
                 ┌─────────────────────────────────────┐
                 │            客户端                    │
                 │  ┌─────────────┐  ┌──────────────┐  │
                 │  │  ceph-fuse  │  │  RBD/RGW     │  │
                 │  │  (CephFS)   │  │  (客户端)    │  │
                 │  └─────────────┘  └──────────────┘  │
                 └─────────────────────────────────────┘
```

---

## 十一、实际部署示例

### 最小集群（3 节点）

```bash
# 节点 1
ceph-mon -i node1 --public-addr 192.168.1.1
ceph-mgr -i node1
ceph-osd -i 0 --osd-data /var/lib/ceph/osd/ceph-0

# 节点 2
ceph-mon -i node2 --public-addr 192.168.1.2
ceph-osd -i 1 --osd-data /var/lib/ceph/osd/ceph-1

# 节点 3
ceph-mon -i node3 --public-addr 192.168.1.3
ceph-osd -i 2 --osd-data /var/lib/ceph/osd/ceph-2
```

### 生产集群（典型配置）

```bash
# 3 个 Monitor 节点
3 x ceph-mon

# 2 个 Manager 节点（主备）
2 x ceph-mgr

# N 个 OSD 节点（每节点多个 OSD）
10 个节点 x 12 个 OSD/节点 = 120 个 ceph-osd

# 2 个 MDS 节点（仅 CephFS）
2 x ceph-mds (1 Active + 1 Standby)

# 客户端
多个 ceph-fuse 挂载点
```

这就是 Ceph 各个核心组件的完整解析！每个组件都有其特定的职责，协同工作构成完整的分布式存储系统。🚀

