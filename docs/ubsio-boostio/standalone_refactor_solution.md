# UBSIO 单机改造方案

## 范围

本文说明 BoostIO 从提交 `2de48a7abeb2598bc5c59cde091b2450ee1d7f01` 开始到当前 `HEAD` 的单机模式改造内容。`2de48a7` 是单机模式脚手架提交，后续提交完成运行时接线、IO 路径、构建诊断、流程文档和单测覆盖。

相关提交如下：

| 提交 | 主题 | 说明 |
| --- | --- | --- |
| `2de48a7` | `standalone: add mode scaffolding` | 增加 `STANDALONE` 工作模式、server C 接口声明、runtime config 消息、`StandaloneMemoryPool` 和 `StandaloneView` 脚手架。 |
| `c8dc0f0` | `standalone: wire runtime and IO path` | 将单机模式接入初始化流程、配置解析、SDK/server direct-call 路径、put/get/batchget/batchexist、BDM 元数据和本地 view。 |
| `9b2a41e` | `standalone: add build diagnose and tests` | 增加 ASan/UBSan 构建开关、诊断命令修正、console 单机启动参数、首批 standalone 单测。 |
| `44f45a6` | `standalone: add flow docs and disk scripts` | 增加初始化、BatchGet、磁盘选择 HTML 说明和 `partition_disks.sh` 分区脚本。 |
| `443ebbe` | `test: add standalone mode UT coverage` | 补充 standalone 配置选择、runtime config、memory pool、view、BDM metadata 等 UT 覆盖。 |

## 背景

单机改造的背景是 BoostIO 需要跟 memcache 配合减少数据搬运。该场景不再需要跨节点通信，也不需要依赖 NetEngine、CM/ZooKeeper 发布集群视图，因此将 BoostIO 改造成嵌入当前进程的单机模式，由 SDK 在本进程内启动 server 并通过直接函数调用完成缓存 IO。

## 目标

1. 增加 `STANDALONE` 工作模式，保留原有 `CONVERGENCE`、`SEPARATES` 行为。
2. 单机模式不启动 Net 和 CM 模块，不做 IPC/RPC、fd 传递、ZooKeeper 注册和集群视图订阅。
3. SDK 与 server 位于同一进程，优先通过 direct-call 和本地地址传递减少数据拷贝。
4. 支持 put、get、batchget、batchexist 等核心 IO 路径在单机模式可用。
5. 增加本地 `NodeView`、`PtView`，让 cache、flow、mirror 继续复用既有分区和副本抽象。
6. 支持一个配置文件描述多个本地设备或分区，并按 standalone device id 选择当前进程使用的磁盘。
7. 增加独立内存池，替代单机模式中原本由 NetEngine 提供的 server 内存分配能力。
8. 增加构建、诊断、脚本、文档和 UT 覆盖，便于验证单机改造。

## 使用方式

单机模式需要先设置当前进程的 standalone device id，再初始化 BoostIO：

```c
BioSetStandaloneDevice(deviceId);
BioInitialize(STANDALONE, optConf);
```

`BioSetStandaloneDevice` 必须在 `BioInitialize(STANDALONE, ...)` 之前调用。`test/benchmarks/console.cpp` 中的 `bio_console` 示例从环境变量 `RankNum` 读取该编号，并调用 `BioSetStandaloneDevice(rankNum)`。

## 配置含义

| 配置项 | 单机模式含义 |
| --- | --- |
| `bio.disk.path` | 缓存盘或分区列表，使用 `:` 分隔。单机启动时会先根据 `BioSetStandaloneDevice(deviceId)` 和 `bio.standalone.device_count` 选择当前进程实际使用的子集，再交给 BDM 启动。 |
| `bio.standalone.device_count` | 新增配置。默认 `0` 表示兼容旧逻辑：`deviceId` 直接对应 `bio.disk.path` 的下标，只选择一个盘。大于 `0` 时表示本机单机实例总数，算法会把 `bio.disk.path` 中的盘均衡分配到 `[0, device_count)` 的各个 `deviceId`。 |
| `bio.segment.size_in_mb` | cache segment、BDM chunk、standalone server memory pool block 的大小。server 通过 runtime config 将其作为 `dataMsgBlockSize` 传给 SDK。 |
| `bio.mem.size_in_gb` | 单机 server 内存池总容量。没有 NetEngine 时，FlowManager 的 memory allocator 使用 `StandaloneMemoryPool` 从该容量中分配。 |
| `bio.sdkmem.size_in_mb` | 单机 SDK data message pool 总容量，用于 BatchGet 返回数据缓冲。SDK 侧通过匿名 `mmap` 建池，不再依赖 shm fd 或 MR key。 |
| `bio.cm.pts_count` | 单机模式不启动 CM，但仍使用该值生成本地 `PtView`。PT id 范围为 `[0, pts_count)`，至少生成 1 个 PT。 |
| `bio.cm.copy_num`、`bio.cm.initial.nodes_count`、`bio.cm.zk_host` | 单机模式不依赖这些 CM 注册和 ZooKeeper 配置。保留配置项主要是为了兼容现有配置结构。 |
| `bio.net.data.*` | 单机模式不启动 NetEngine。`dataIp`、`dataPort`、`protocol` 会进入 synthetic `NodeView` 或 runtime config，主要用于兼容视图和诊断字段。 |
| `bio.work.scene`、`bio.work.io.alignsize`、`bio.work.io.timeout`、`bio.work.net.timeout` | 通过 `GetRuntimeConfig` 传给 SDK，复用原有工作场景、对齐和超时校验逻辑。 |
| `bio.batchget.thread.num` | server 侧 BatchGet executor 线程数，单机和非单机都由 MirrorServer 使用。 |
| `bio.data.crc.enable` | 单机 direct-call 路径仍按配置启用 CRC。 |
| `bio.trace.enable`、`bio_cli_tools_enable`、`bio.prometheus.*` | 单机模式继续读取这些诊断、trace、Prometheus 配置，但初始化路径由 direct mode 获取配置，不走 IPC 协商。 |
| `bio.underfs.*` | UnderFs 模块仍会初始化，写穿、加载等后端访问能力继续依赖该配置。 |

典型多实例配置示例：

```ini
bio.disk.path = /dev/nvme0n1p1:/dev/nvme0n1p2:/dev/nvme1n1p1:/dev/nvme1n1p2
bio.standalone.device_count = 2
bio.segment.size_in_mb = 4
bio.mem.size_in_gb = 50
bio.sdkmem.size_in_mb = 5120
```

在上述配置中，`deviceId = 0` 和 `deviceId = 1` 会分别选择一组磁盘或分区。若 `bio.standalone.device_count = 0`，则 `deviceId = 1` 只选择 `bio.disk.path` 中下标为 1 的路径。

## 主要修改部分

### 初始化流程修改

单机模式把原先依赖 Net 和 CM 的初始化拆成一条独立链路：

1. 用户调用 `BioSetStandaloneDevice(deviceId)`，SDK 将 device id 暂存在 `BioClientAgent`。
2. `BioInitialize(STANDALONE, ...)` 进入 `BioClientAgent::Initialize(STANDALONE)`。
3. `BioClientAgent` 加载 `libbio_server.so`，绑定 `BioServerStandaloneInit`、`SetStandaloneDeviceInfo`、`GetRuntimeConfig`、`BatchGet`、`BatchExist` 等 server 导出函数。
4. `BioClientAgent` 先调用 server 侧 `SetStandaloneDeviceInfo(deviceId)`，再调用 `BioServerStandaloneInit()`。
5. `BioServer::StartStandalone()` 初始化配置，执行 `SelectStandaloneDiskByDeviceInfo()`，然后启动 standalone module chain。
6. `BioClientNet::StartPre(STANDALONE)` 不创建 NetEngine，也不做 `ShmInit`；它直接通过 `GetRuntimeConfig` 读取 server 的 runtime 参数，填充 SDK 侧 getter 所需字段。

单机 server module chain 为：

```text
Diagnose -> Tracer -> UnderFs -> Bdm -> StandaloneMem -> Flow -> StandaloneView -> Cache -> MirrorServer
```

相比集群模式，单机链路省略了 `Net`、`CM` 和 `MirrorServerCrb`。Cache 仍然初始化，但它的本地磁盘、PT 状态、降级状态、本地角色等回调改为从 `BioServer` 的本地 view 读取。

### IO 路径修改

#### Put

单机模式下 `MirrorClient` 仍复用 PT view 和副本选择逻辑，但 `StandaloneView` 只生成本地节点的单副本，因此 `PutLocal` 会走 `BioClientAgent::PutLocal` 直接调用 server 导出的 `Put` 函数。

关键变化：

1. `PrepareFromServer` 通过 server 侧 `GetSlice` 获取写资源，server 使用 `StandaloneMemoryPool` 分配内存 slice。
2. `PrepareFromClient` 在 `STANDALONE` 下不再从 SDK data message pool 复制一份数据，而是把用户 buffer 地址写入 `PutRequest`，`mrKey` 为 `0`。
3. `BioClientAgent::PutLocal` 在 direct mode 中同步调用 `putOp(req, &rsp)`，然后立即触发 SDK callback。
4. 单机模式不存在远端副本，也没有 `SendAsyncBuff`、RDMA MR、shm offset 等网络传输步骤。

#### BatchGet

BatchGet 改造的核心是让请求仍按 node plan 聚合，但本地 plan 通过 direct-call 执行：

1. SDK 侧 `MirrorClient::CreateDataMessageMemStandalone()` 通过 `GetRuntimeConfig` 获取 `sdkPoolSize` 和 `dataMsgBlockSize`，用匿名 `mmap` 建立 data message pool。
2. `BatchGetImpl` 为每个 key 从 SDK data message pool 分配结果 buffer。单机模式下 `FillBatchGetBufferInfo` 写入真实地址，`mrKey = 0`、`addressOffset = 0`。
3. `SendBatchGetRequest` 对本地节点调用 `BioClientAgent::BatchGetLocal`。
4. `BioClientAgent::BatchGetLocal` 在 `STANDALONE` 下同步调用 server 导出的 `BatchGet`。
5. server 侧 `MirrorServer::BatchGetConvergence` 使用 BatchGet executor 并发处理每个 key，`BatchSingleGet` 在同节点路径中直接把数据复制到 SDK 传入的地址。
6. SDK callback 汇总 `BatchGetResponse` 中的 `results` 和 `realLengths`，用户通过 `BioBatchGetFree` 释放 SDK data message pool 中的 buffer。

该路径不再需要 IPC shm fd、远端 MR key、地址 offset 翻译或 NetEngine `SyncWrite`。

#### BatchExist

BatchExist 在单机模式中也走本地 direct-call：

1. `Bio::BatchExist` 增加 `count == 0` 和 key 有效性校验。
2. `MirrorClient::BatchExistImpl` 仍按 PT copy 生成 node plan。单机 `PtView` 只有本地副本，所以只生成本地 plan。
3. `SendBatchExistRequest` 对本地节点调用 `BatchExistLocal`。
4. `BioClientAgent::BatchExistLocal` 在 `STANDALONE` 下同步调用 server 导出的 `BatchExist`。
5. `MirrorServer::BatchExistConvergence` 逐个 key 调用 `Cache::Instance().Exist()`，按 index 回填结果。

`CONVERGENCE` 模式仍不支持 BatchGet/BatchExist 的 dispatch 路径；`SEPARATES` 继续走网络或 IPC 路径。

### 磁盘选择算法添加

`bio.standalone.device_count` 用于支持一个配置文件描述多个单机实例的磁盘资源。

选择逻辑：

1. `BioConfig::SetStandaloneDeviceInfo(deviceId)` 记录当前进程 id；调用时序由 SDK 层保证在 `BioInitialize(STANDALONE)` 前完成。
2. `SelectStandaloneDiskByDeviceInfo()` 校验磁盘列表、容量列表和 device id。
3. 当 `bio.standalone.device_count = 0` 时，使用兼容逻辑：`deviceId` 必须小于 `bio.disk.path` 个数，并直接选择该下标路径。
4. 当 `bio.standalone.device_count > 0` 时：
   - `deviceId` 必须在 `[0, device_count)` 内。
   - `bio.disk.path` 数量必须不少于 `device_count`。
   - 按 `FileUtil::GetPhysicalDiskKey()` 将分区归组；获取失败时以路径本身作为分组 key。
   - 在各组内按轮次把磁盘分配到实例，尽量保证每个实例磁盘数量差不超过 1，并尽量把同一物理盘的多个分区打散到不同实例。
5. 选择完成后，`mDaemonConfig.diskList` 和 `diskCaps` 会被收缩为当前进程实际使用的磁盘子集，后续 BDM 只看到该子集。

BDM 也增加了 standalone metadata 防串用能力：

1. `BdmSetDiskStartupInfo(isStandalone, deviceId)` 将 standalone magic 和 device id 写入 disk head 的 `pad` 字段。
2. 恢复已有 BDM metadata 时，如果磁盘头中的 standalone mode 或 device id 与当前启动信息不一致，会返回 metadata mismatch。
3. 旧的 cluster pad 仍保留兼容恢复行为。
4. `BdmStart` 增加磁盘数量上限校验。

`scripts/partition_disks.sh` 用于把整盘或 loop 设备等分成多个分区，并输出可直接写入 `bio.disk.path` 的配置。默认 dry-run，只有加 `--yes` 才会实际写分区表；`--wipe-metadata` 会清理旧文件系统签名和 BoostIO BDM 头。

### 内存池添加

单机模式没有 NetEngine，因此原来依赖 NetEngine 的 server 内存分配能力需要替代实现。

`StandaloneMemoryPool` 的职责：

1. 使用 `mmap(MAP_PRIVATE | MAP_ANONYMOUS)` 按 `bio.mem.size_in_gb` 创建 server 内存池。
2. 按 `bio.segment.size_in_mb` 切成固定 block，并复用 `NetBlockPool` 管理空闲 block。
3. 提供 `Alloc(size, address)` 和 `Free(address)`，校验 size、启动状态和 block 地址合法性。
4. 维护 `mUsedBlock`，供 `BioServer::GetMemUsedSize()` 和写缓存策略使用。
5. 在 `BioStandaloneMemInit()` 中注册为 `FlowManager` 的 memory allocator。

SDK 侧还增加了 standalone data message pool：

1. `MirrorClient::CreateDataMessageMemStandalone()` 使用 server runtime config 中的 `sdkPoolSize` 和 `dataMsgBlockSize`。
2. 用匿名 `mmap` 建池，不注册 MR，也不保存 shm fd。
3. BatchGet 结果 buffer 从该池分配，用户通过 `BioBatchGetFree` 释放。

### StandaloneView 模块

单机模式不启动 CM，但 Cache、MirrorClient、FlowManager 仍需要 `NodeView` 和 `PtView`。`StandaloneView` 负责构造本地单节点视图：

1. `localNid` 固定为当前 group 下的 vnode `0`。
2. `NodeView` 只包含一个本地节点，节点磁盘状态来自 `BdmGetDiskStatus(diskId)`。
3. `PtView` 生成 `[0, bio.cm.pts_count)` 的 PT，版本固定为 `1`。
4. 每个 PT 只有一个本地 copy，master node 为 `0`，master disk 使用 `ptId % diskNum` 轮询分配。
5. 如果目标 disk 正常，PT 为 `CM_PT_NORMAL`、copy 为 `CM_COPY_RUNNING`；如果目标 disk 故障，PT 为 `CM_PT_FAULT`、copy 为 `CM_COPY_DOWN`。

`BioStandaloneViewInit()` 将生成的 view 写入 `BioServer`，并更新时间戳。SDK 通过原有 `GetNodeView`、`GetPtView` direct-call 读取同一份本地 view。

## 其他配套修改

1. `WCache::PutSetIoStrategy` 改为通过 `BioServer::GetMemUsedSize()` 读取内存使用量，兼容 NetEngine 和 standalone memory pool。
2. `BioQos::Initialize` 将 `STANDALONE` 视为 direct mode，quota pid 使用 `0`。
3. `MirrorServer::AddDiskImpl` 禁止单机模式动态加盘，避免绕过启动时的磁盘选择和 BDM metadata 校验。
4. `NotifyUpdate`、`CheckUpdateReady` 等 CM 相关状态在 standalone 下改为 `BioServer` 本地状态。
5. 构建系统增加 `BOOSTIO_ENABLE_ASAN_UBSAN` 和 `build.sh --san=asan`，并把 `bio_common`、`htracer`、`bio_security` 改为 shared library 安装，便于诊断工具和 direct-call 场景加载。
6. SDK/server 诊断命令去掉 `std::regex` 依赖，增加 `sdk exist`，修正若干 printf 参数和 BatchGet 校验逻辑。

## 测试覆盖

新增和补充的 UT 覆盖点包括：

1. `test_standalone_config.cpp`：legacy device id 选择、多实例均衡分配、同物理盘分区打散、非法输入校验。
2. `test_standalone_view.cpp`：空配置拒绝、单节点 view 构造、PT round-robin、故障盘状态映射。
3. `test_standalone_memory_pool.cpp`：分配耗尽、复用、非法参数、零容量、非法 free、并发分配释放。
4. `test_bio_client_agent.cpp`：`BioSetStandaloneDevice` 调用顺序、runtime config direct-call、standalone async get。
5. `test_bio_client_net.cpp`：standalone runtime config 边界校验和 `ApplyStandaloneRuntimeConfig`。
6. `test_disk.cpp`：BDM standalone metadata 的同 device 恢复、不同 device 拒绝、cluster pad 兼容。

建议验证命令：

```bash
cd ubsio-boostio
bash build.sh -t debug --ut
cd test/llt
bash run_dt.sh
```

如需排查内存越界或未定义行为，可使用：

```bash
cd ubsio-boostio
bash build.sh -t debug --ut --san=asan
```

## 约束和注意事项

1. `BioSetStandaloneDevice` 必须先于 `BioInitialize(STANDALONE, ...)` 调用，且初始化后不能再修改 device id。
2. 单机模式不启动 Net 和 CM，因此不支持依赖跨节点连接、CM CRB、ZooKeeper 发布订阅的能力。
3. `BioBatchGetKeyDiskAddr` 当前只支持分离部署路径；direct mode 下会返回不支持。
4. 单机模式不支持运行时动态加盘。需要调整磁盘时，应停进程、修改 `bio.disk.path` 或 `bio.standalone.device_count`，必要时清理 BDM metadata 后重新启动。
5. 复用已有磁盘或分区时要注意 BDM disk head 中的 standalone device id。device id 不匹配会拒绝恢复，避免不同单机实例误用同一份缓存盘。
6. `bio.disk.path` 可以在配置阶段写入多于 BDM 单进程上限的路径，但磁盘选择后交给 BDM 的实际路径数仍必须满足 BDM 限制。
