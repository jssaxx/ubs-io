# feat/batchget-bdm-pipeline 分支变更说明

本文说明 `feat/batchget-bdm-pipeline` 当前单提交包含的主要改动。说明范围以功能、性能路径、配置、CLI 和风险点为主。

## 总体目标

这组改动的目标是给 BoostIO 增加一版更适合验证 BDM io_uring 和 BatchGet 读盘路径的性能分支：

1. BDM 层支持通过配置在 `sync` 和 `io_uring` 两种 IO 引擎之间切换。
2. BatchGet 命中 WCache 磁盘数据时，支持把一批 key 的 BDM 读请求收敛为批量异步读，并按窗口流水化提交。
3. Standalone 模式下放开原 SDK BatchGet 128/256 拆分限制，支持更大的单 batch，例如 600 key。
4. CLI 增加 BDMPerf、BatchGetPerf、BatchGetMixPerf 等压测入口，用于验证纯 BDM 和业务 BatchGet 的性能。
5. 删除 perfstat 统计和相关 CLI，减少性能验证分支的热路径观测开销。

## BDM io_uring 和 BatchGet Pipeline

### 1. BDM IO 引擎切换

新增配置项：

```ini
ubsio.bdm.io_engine = sync
```

可选值：

- `sync`
- `io_uring`

默认值仍为 `sync`。只有显式配置为 `io_uring` 时才启用 io_uring 路径。

代码路径：

- `ubsio-boostio/src/config/bio_config_instance.h`
- `ubsio-boostio/src/config/bio_config_instance.cpp`
- `ubsio-boostio/configs/ubsio.conf`
- `ubsio-boostio/src/server/bio_server.cpp`
- `ubsio-boostio/src/disk/common/bdm_core.h`
- `ubsio-boostio/src/disk/common/bdm_disk.c`

服务启动时，`BioServer::BioBdmInit()` 会读取 `daemonConfig.bdmIoEngine`，调用 `BdmSetIoEngine()`，再执行 `BdmInit()`。如果配置值非法，初始化直接失败，不做静默降级。

注意点：

- 该分支没有把默认引擎改成 io_uring。
- BDM 只支持 `sync` 和 `io_uring` 两个后端；libaio 不再参与三方构建、链接和打包，UT 保留
  `BdmSetIoEngine("libaio")` 必须返回非法参数的负向断言。
- `src/disk/CMakeLists.txt` 链接了 `uring`，因此构建环境必须有 `liburing-devel`。

### 2. BDM io_uring 实现

BDM disk 层新增 io_uring 执行路径，核心变化在：

- `ubsio-boostio/src/disk/common/bdm_disk.c`
- `ubsio-boostio/src/disk/common/bdm_core.c`
- `ubsio-boostio/src/disk/common/bdm_core.h`
- `ubsio-boostio/src/disk/common/bdm_obj.h`

主要机制：

1. 每个 BDM worker 线程维护一个 `io_uring` ring。
2. ring 初始化使用 `IORING_SETUP_SQPOLL`。
3. 每个 ring 通过 semaphore 控制在途请求数，当前最大在途为 `BDM_URING_MAX_INFLIGHT = 896`。
4. CQE 线程使用 `io_uring_wait_cqe()` 等待首个完成事件，然后通过 `io_uring_for_each_cqe()` 批量收割已完成 CQE。
5. 完成回调中检查 `cqe->res`，成功时必须等于请求长度，否则按错误处理。
6. 对 `-EAGAIN` 做有限重试，避免短暂块层资源不足直接导致业务失败。

新增或调整的关键函数：

- `BdmSetIoEngine()`
- `BdmGetIoEngine()`
- `BdmDiskSubmitWaitUring()`
- `BdmDiskSubmitOneUring()`
- `BdmDiskSubmitUring()`
- `BdmCompleteUringHandler()`
- `BdmDiskEventsThread()`
- `BdmDiskReadBatchAsync()`
- `BdmDiskWriteBatchAsync()`
- `BdmExit()`
- `BdmDiskExit()`

EAGAIN 处理：

- 如果 io_uring CQE 返回 `-EAGAIN`，会按指数退避重提。
- 重试上限为 `BDM_URING_EAGAIN_RETRY_NUM = 64`。
- 初始退避为 100 us，上限 5000 us。
- 重试耗尽后返回 `BDM_CODE_ERR_IO`，并上报磁盘 IO fault。

这部分改动是为了支持 loop/nvme 在高并发大 IO 下偶发 `EAGAIN` 的场景。此前已经用 fio 在裸设备分区上复现过类似 `Resource temporarily unavailable`，说明这不是 BoostIO 业务层独有的问题。

`BDM_URING_MAX_INFLIGHT = 896` 的依据：

- 当前 ring 深度是 `BDM_IOCTX_EVENTS_NUM = 1024`。
- 896 等于 1024 减去 128，保留一部分 SQ/CQ 余量，避免应用侧把 ring 长时间顶满后，重试、唤醒、批量提交和完成回收之间互相放大抖动。
- 这个值不是硬件公式推导出的最优值，也不是 fio/BoostIO 多轮矩阵验证后的最终阈值；它是一个偏保守的保护上限。
- 从工程角度看，后续更合理的方式是把它配置化，或者至少按设备类型、ring 深度和业务窗口大小重新压测校准。

### 3. O_DIRECT 和对齐处理

BDM 读写路径改为使用 direct IO 语义，新增了对长度、offset 和用户 buffer 地址的处理：

- 写路径长度和 offset 必须按 512 字节对齐。
- 读路径如果范围不满足 512 字节对齐，会按对齐边界 over-read 到 bounce buffer，再拷贝用户需要的数据。
- 用户 buffer 如果已经 512 字节对齐，直接用于 IO。
- 用户 buffer 如果未对齐，会分配 512 字节对齐的 bounce buffer。
- 写路径会先把用户 buffer 拷贝到 bounce buffer。
- 读路径在 IO 成功后再把 bounce buffer 拷回用户 buffer。
- 写请求长度或 offset 不对齐时直接返回错误，不自动 fallback。

相关函数：

- `BdmDiskIsRangeAligned()`
- `BdmDiskCheckDirectRangeAligned()`
- `BdmDiskPrepareDirectBuffer()`
- `BdmDiskFinishDirectBuffer()`

注意点：

- 这不是降级策略。写请求参数不满足 direct IO 要求时会失败；读请求通过 over-read 保持旧语义兼容。
- 当前实现保留 bounce buffer，主要解决上层传入 buffer 地址不满足 direct IO 对齐的问题。

### 4. BDM 批量异步接口

BDM core 新增批量异步接口：

```c
int32_t BdmReadBatchAsync(BdmBatchIo *ios, uint32_t ioNum);
int32_t BdmWriteBatchAsync(BdmBatchIo *ios, uint32_t ioNum);
```

新增结构：

```c
typedef struct {
    uint64_t chunkId;
    uint64_t offset;
    void *buf;
    uint64_t len;
    BdmIoCtx *ioCtx;
} BdmBatchIo;
```

处理逻辑：

1. 校验每个 IO 的 `chunkId`、`offset`、`len`、`buf` 和回调上下文。
2. 如果一批 IO 属于同一个 BDM 对象，优先走对象的 `readBatchAsync/writeBatchAsync`。
3. 如果跨 BDM 对象，按单个 BDM 对象拆分提交。
4. 单个请求失败时执行对应请求的 callback，不阻塞整个批次的其他请求。

该接口是 BatchGet 读盘 pipeline 的基础，避免上层逐 key、逐 slice 同步等待。

### 5. BatchGet 的 BDM 读盘 Pipeline

新增 `BdmCopyBatchContext`，用于把 BatchGet 中命中 WCache disk 的读盘请求先收集起来，再按窗口和流水深度提交给 BDM。

代码路径：

- `ubsio-boostio/src/cache/cache_slice_operator.h`
- `ubsio-boostio/src/cache/cache_slice_operator.cpp`
- `ubsio-boostio/src/cache/cache.cpp`
- `ubsio-boostio/src/cache/cache.h`
- `ubsio-boostio/src/cache/write/wcache_manager.cpp`
- `ubsio-boostio/src/cache/write/wcache_manager.h`
- `ubsio-boostio/src/server/mirror_server.cpp`

新增配置项：

```ini
ubsio.bdm.batch_read.window_keys = 128
ubsio.bdm.batch_read.window_bytes_mb = 64
ubsio.bdm.batch_read.pipeline_depth = 4
```

配置含义：

- `window_keys`：单个 BDM 批读窗口最多包含多少个 BatchGet key。
- `window_bytes_mb`：单个 BDM 批读窗口最多包含多少 MB 数据。
- `pipeline_depth`：允许同时在途的 BDM 批读窗口数量。

默认值：

- `window_keys = 128`
- `window_bytes_mb = 64`
- `pipeline_depth = 4`

配置约束：

- `window_keys` 范围：1 到 1024。
- `window_bytes_mb` 范围：1 到 1024。
- `pipeline_depth` 范围：1 到 64。

Pipeline 执行流程：

1. BatchGet 每个 key 先按原逻辑查 WCache。
2. 如果该 key 的数据在 WCache memory，仍走原内存拷贝路径。
3. 如果该 key 的数据在 WCache disk，并且目标是 memory slice，则不立即同步读盘，而是调用 `BdmCopyBatchContext::EnqueueDiskToMemory()` 收集读盘任务。
4. 每个 key 的查询逻辑完成后，BatchGet 统一调用 `bdmBatch.Submit()`。
5. `Submit()` 按 `window_keys/window_bytes_mb` 切窗口。
6. 每个窗口通过 `SubmitBdmBatchAsync()` 异步提交给 BDM。
7. 同时最多保留 `pipeline_depth` 个窗口在途；达到上限时，在提交下一个窗口前等待最早窗口完成。
8. 每个窗口完成后扫描单 IO 结果，把失败结果回填到对应 key 的结果数组。

适用范围：

- 当前只优化 WCache disk 到 memory 的 BatchGet 读盘路径。
- 不优化 rcache。
- 不优化 underfs/backend。
- CRC 开启时不走 Batch WCache read，返回原路径重试。

### 6. WCache GetBatch 和 slice lease 生命周期

WCache 新增 `GetBatch()`，允许 writer 接管 `WCacheSliceRefPtr`，保证异步 BDM 读盘未完成前，WCache slice 不会被提前释放或复用。

新增类型：

```cpp
using WCacheBatchSliceWriter =
    std::function<BResult(const SlicePtr &from, const SlicePtr &to, WCacheSliceRefPtr &sliceRef)>;
```

关键点：

- 普通 `Get()` 是同步 writer，函数内即可释放 `sliceRef`。
- BatchGet pipeline 中，disk slice 读盘变成异步提交，因此必须把 `sliceRef` 延长到异步读完成后。
- `BdmCopyBatchContext::Entry` 保存 `WCacheSliceRefPtr`，析构时释放，避免异步读期间源 slice 被回收。

新增单测：

- `test_batch_get_releases_unclaimed_slice_ref`
- `test_batch_get_transfers_slice_ref_to_writer`

这两个用例分别验证：

1. writer 不接管 lease 时，`GetBatch()` 正常释放。
2. writer 接管 lease 时，外部可以持有到异步流程结束后再释放。

### 7. BatchGet 并发和 Standalone 行为

BatchGet key 数上限按部署形态区分：

```cpp
const uint32_t KEY_MAX_COUNT = 256;
const uint32_t NET_BATCH_GET_MAX_COUNT = KEY_MAX_COUNT;
const uint32_t BATCH_GET_MAX_COUNT = 16 * 1024;
const uint32_t STANDALONE_BATCH_GET_MAX_COUNT = BATCH_GET_MAX_COUNT;
```

影响：

- 网络 BatchGet 保持 256 key 上限，避免分离/网络部署下消息过大。
- Standalone 直接调用路径允许最多 16k key，用于支持大 batch 验证场景。
- 网络 BatchGet 响应新增变长 `BatchGetWireResponse`，只返回实际 count 个结果；standalone 进程内接口继续使用
  固定数组 `BatchGetResponse`。

Standalone 模式调整：

- `MirrorClient::DispathBatchGet()` 在 standalone 模式下不再按 SDK dispatch batch count 拆分，直接走 `BatchGet()`。
- `BatchFree()`、`BatchGetImpl()`、`DispathBatchGetRecycleResource()` 在 standalone 下不释放 data message pool buffer，因为 standalone 测试里常使用用户传入 buffer。
- SDK diagnose 初始化支持 standalone，不再依赖网络模式的 CLI flag；standalone 下直接尝试复用 server 已启动的 CLI agent 注册 `sdk` 命令。
- `bio_console 2` 只负责设置 standalone 设备并调用 `BioService::Initialize()`，SDK 压测命令由 SDK 初始化流程统一注册。

### 8. Cache/QoS 辅助修复

QoS 关闭时，`ReleaseQuota()` 直接返回，不再打印找不到 holder 的告警。

代码路径：

- `ubsio-boostio/src/cache/overloadctrl/cache_overload_ctrl.cpp`
- `ubsio-boostio/src/cache/overloadctrl/cache_overload_ctrl.h`

变化：

- `CacheOverloadCtrl::Initialize()` 记录 `enableQos` 到 `mEnableQos`。
- `ReleaseQuota()` 判断 QoS 未开启时直接返回。
- 析构时判断 `mStatisticExecutor` 非空再 stop，避免空指针风险。

### 9. Daemon 和 BDM 退出流程

`bio_daemon` 收到退出后会调用：

```cpp
bioServer->Exit();
```

`BioServer::BioBdmExit()` 改为调用 `BdmExit()`，BDM 层新增 `BdmDiskExit()` 清理 io_uring ring、事件线程、semaphore、mutex 等资源。

这部分是为了解决切换 engine 或 daemon 退出时资源未释放、线程仍在等待 CQE 的问题。

### 10. Trace 打点补充

新增 C 侧 htracer 桥接文件：

- `ubsio-boostio/src/htracer/htracer_c.h`
- `ubsio-boostio/src/htracer/service/htracer.cpp`

新增 trace 类型：

- `MIRROR_TRACE_BATCH_GET_LOOKUP`
- `MIRROR_TRACE_BATCH_GET_FINAL_FLUSH`
- `BDM_TRACE_READ_BATCH`
- `BDM_TRACE_READ_BATCH_WINDOW_BUILD`
- `BDM_TRACE_READ_BATCH_WAIT`
- `BDM_TRACE_READ_BATCH_RESULT_SCAN`

用途：

- 定位 BatchGet key 查找和最终 BDM 批读 flush 的耗时。
- 定位 BDM 批读窗口构建、等待和结果扫描耗时。

注意：

- 这些是 trace 点，不是已删除的 perfstat 统计体系。
- trace 是否产生运行时数据仍受 `ubsio.trace.enable` 控制。
- `BDM_TRACE_PREPARE_DIRECT`、`BDM_TRACE_URING_LOCK`、`BDM_TRACE_URING_SUBMIT`、`BDM_TRACE_URING_COMPLETE` 这类更细的辅助打点已经从纯性能分支删除，只保留 BatchGet lookup/final flush 和 BDM batch window/wait/result scan 这类能定位端到端瓶颈的关键打点。

### 11. BDMPerf CLI

`bioServer` CLI 新增 BDM 纯盘压测命令：

```text
bioServer BdmPerf [read/write] [bsKb] [ioDepth] [sizeMb] [rounds] [dropCaches] [batch:0/1] [batchSize] [bdmStart] [bdmCount]
```

参数含义：

- `read/write`：读或写。
- `bsKb`：单 IO 大小，单位 KB。
- `ioDepth`：在途 IO 深度。
- `sizeMb`：单轮总数据量，单位 MB。
- `rounds`：测试轮数。
- `dropCaches`：是否尝试清 page cache，1 表示清理。
- `batch`：是否使用 BDM 批量接口，1 表示使用。
- `batchSize`：每次批量提交的 IO 数，要求 `0 < batchSize <= ioDepth`。
- `bdmStart`：起始 BDM disk id。
- `bdmCount`：参与测试的 BDM disk 数。

输出包含：

- elapsed
- throughput
- IOPS
- avg latency
- p50
- p99
- errors

约束：

- `bs <= ubsio.segment.size_in_mb`
- `segmentSize % bs == 0`
- `bs` 需要 4KB 对齐
- `sizeMb` 对应总字节数需要能被 `bs` 整除

### 12. SDK BatchGet 压测 CLI

`sdk` CLI 新增两个 BatchGet 压测命令。

纯 BatchGet：

```text
sdk batchgetperf [bs(Kb)] [batchNUM] [rounds] [fill(Mb)]
```

用途：

- 预置一批目标 key。
- 写入 filler 数据触发淘汰。
- 验证目标 key 可以从盘上读回。
- 输出每轮 get/free/e2e 耗时和带宽。

混合命中率 BatchGet：

```text
sdk batchgetmixperf [bs(Kb)] [batchNUM] [concurrency] [hitPercent] [rounds] [fill(Mb)]
```

用途：

- 构造指定 batch 大小和并发数。
- 按 `hitPercent` 控制 cache key 和 disk key 比例。
- 写入 filler 触发部分数据淘汰到盘。
- warmup 后多线程同时执行 BatchGet。
- 输出总带宽、key IOPS、失败项、短读、准备阶段耗时、观测命中统计等。

当前限制：

- `batchgetperf` 最大 batch 为 256。
- `batchgetmixperf` 最大 batch 为 600。
- `batchgetmixperf` 最大并发为 64。
- disk key 校验通过 `BioBatchGetKeyDiskAddr()` 做尽力验证，如果查询失败会标记 skipped，不作为 standalone 强校验。

## perfstat 删除说明

本分支删除 `perfstat` 统计体系和相关 CLI。删除范围包括：

- `src/common/bio_perf_stats.*`
- SDK/server/cache/BatchGet/BDM 窗口路径中的 `BioPerfRecord*` 打点
- `bioServer perfstat` 和 `sdk perfstat` 相关接口

保留的相关功能：

- `bioServer BdmPerf`
- `sdk batchgetperf`
- `sdk batchgetmixperf`
- `sdk perf`
- `sdk batchget`
- trace 打点
- BDM io_uring
- BatchGet BDM pipeline

### perfstat A/B 参考

测试命令：

```text
bioServer BdmPerf read 4096 16 2048 5 1 1 16 0 1
bioServer BdmPerf write 4096 16 2048 5 1 1 16 0 1
bioServer BdmPerf read 64 32 1024 5 1 1 32 0 1
bioServer BdmPerf write 64 32 1024 5 1 1 32 0 1
```

吞吐对比：

| 场景 | 带 perfstat | 删除 perfstat | 变化 |
| --- | ---: | ---: | ---: |
| 4MiB read | 3130.91 MB/s | 3179.65 MB/s | +1.56% |
| 4MiB write | 2798.22 MB/s | 2802.19 MB/s | +0.14% |
| 64KiB read | 2250.44 MB/s | 2170.84 MB/s | -3.54% |
| 64KiB write | 2734.64 MB/s | 2759.86 MB/s | +0.92% |

平均时延对比：

| 场景 | 带 perfstat | 删除 perfstat | 变化 |
| --- | ---: | ---: | ---: |
| 4MiB read | 15108.74 us | 14452.97 us | -4.34% |
| 4MiB write | 16766.74 us | 16472.18 us | -1.76% |
| 64KiB read | 662.64 us | 653.58 us | -1.37% |
| 64KiB write | 400.56 us | 391.86 us | -2.17% |

结论：

- 删除 perfstat 对 4MiB 大 IO 的吞吐收益约 0% 到 1.6%。
- 64KiB write 有约 0.9% 吞吐收益，64KiB read 反而低 3.5%，说明 read 结果更容易受设备状态、调度和缓存波动影响，不能把差值全部归因到 perfstat。
- perfstat 的理论开销存在，但在此前纯 BDMPerf 场景下不是主瓶颈；当前按性能分支要求删除该统计体系。

## 本次清理补充

在当前性能分支基础上，进一步清理未使用的 libaio 残留和非关键辅助 trace：

- 删除 libaio 三方构建入口、源码准备入口、打包拷贝和 `aio` 链接依赖；构建过程不再探测或构建 libaio。
- 保留 UT 中的 `libaio` 负向配置测试，确保 `ubsio.bdm.io_engine=libaio` 不会被误认为可用。
- 删除 `BDM_TRACE_PREPARE_DIRECT`、`BDM_TRACE_URING_LOCK`、`BDM_TRACE_URING_SUBMIT`、`BDM_TRACE_URING_COMPLETE` 这类细粒度辅助 trace。
- 保留 `sync` 和 `io_uring` 两种 BDM IO 引擎，默认仍为 `sync`。

同步补充 UT：

- `test_bdm_io_engine_config_case_return_ok`：验证 `sync`、`io_uring` 配置可用，`libaio` 和空配置会返回非法参数。
- `test_bdm_batch_async_invalid_param_case_return_ok`：验证 BDM batch async 的空指针、空批次和非法 IO 参数路径会正确返回或回调错误。

## 当前分支最终行为

### 默认行为

- BDM 默认仍是 `sync`。
- 网络 BatchGet 保持 `KEY_MAX_COUNT = 256`，standalone 直接调用路径支持 `STANDALONE_BATCH_GET_MAX_COUNT = 16k`。
- BatchGet 命中 WCache disk 时，可以走 BDM 批量读 pipeline。
- trace 仍保留，是否启用取决于 `ubsio.trace.enable`。

### 启用 io_uring

配置文件中设置：

```ini
ubsio.bdm.io_engine = io_uring
ubsio.bdm.io_uring.sqpoll_mode = auto
```

`sqpoll_mode=auto` 会优先使用 SQPOLL；内核、权限或容器策略不支持时，所有 BDM worker 会统一重建为普通 io_uring，运行期 IO 失败不会回退到 sync。`required` 要求必须使用 SQPOLL，`disabled` 直接使用普通 io_uring。

可配合：

```ini
ubsio.bdm.batch_read.window_keys = 128
ubsio.bdm.batch_read.window_bytes_mb = 64
ubsio.bdm.batch_read.pipeline_depth = 4
```

如果要降低 loop 或分区设备上的瞬时压力，可以减小：

```ini
ubsio.bdm.batch_read.window_keys = 32
ubsio.bdm.batch_read.window_bytes_mb = 32
ubsio.bdm.batch_read.pipeline_depth = 1
```

这不会消除底层 EAGAIN，只是减少一次性提交到块层的压力。

### sync batch 内部并发

sync 引擎的单 IO 行为保持不变，BDM batch 由内部阻塞 IO 线程池并发执行：

```ini
ubsio.bdm.io_engine = sync
ubsio.bdm.sync.worker_num = 16
```

默认 64 MB window、4 级 pipeline 下，4 MB key 每个 window 最多 16 个，14 MB key 每个 window 最多 4 个；sync worker 数可在 1 到 64 之间调整。

## 风险和注意事项

1. 网络 BatchGet 仍限制为 256 key；standalone 允许 16k key，调用方需要确认 buffer 生命周期覆盖整个批处理。
2. io_uring 写路径依赖 direct IO 对齐；读路径对非对齐范围使用 over-read bounce buffer 兼容。
3. 用户 buffer 地址未对齐时使用 bounce buffer，会引入额外内存分配和拷贝。
4. BatchGet pipeline 当前只覆盖 WCache disk 到 memory 的读盘路径，不覆盖 rcache 和 underfs。
5. CRC 开启时 Batch WCache read 不启用，会回到原路径。
6. EAGAIN 使用有限重试，不做无限重试；重试耗尽会返回 IO 错误。
7. trace 仍可能对极限性能有影响，纯性能压测时建议关闭 `ubsio.trace.enable`。

## 建议 review 重点

1. BDM io_uring 退出流程是否覆盖所有 ring/event thread/semaphore/mutex 资源。
2. `BdmCopyBatchContext` 对 `WCacheSliceRefPtr` 生命周期的持有是否覆盖所有异步失败路径。
3. standalone 下跳过 SDK BatchGet 拆分后，600 key、32 并发时消息和 buffer 生命周期是否稳定。
4. 网络 BatchGet 变长响应在分离部署和跨节点场景下是否兼容所有客户端路径。
5. io_uring EAGAIN 的重试次数和退避策略是否需要配置化。
