# UBS IO WCache 淘汰与 MemCache Master 元数据上报设计

## 背景

MemCache 与 UBS IO 运行在同一进程内。MemCache 自己管理 L2 DRAM Cache，并通过集中式 Master 维护全局 key 元数据。UBS IO 位于 MemCache 下方，作为 SSD extension 使用，但 UBS IO 内部又包含两层：

| 层级 | 组件 | Master 是否可见 |
| --- | --- | --- |
| L2 | MemCache DRAM | 是 |
| L2.5 | UBS IO WCache DRAM FLOW | 否 |
| L3 | UBS IO WCache DISK FLOW | 否 |
| Master 视角 | UBS IO SSD extension | 是 |

Master 不区分 UBS IO 内部的 DRAM FLOW 和 DISK FLOW。对 Master 来说，只需要知道某个 key 是否还存在于 UBS IO extension 中。

因此，`DRAM FLOW -> DISK FLOW` 只是 UBS IO 内部迁移，不需要上报 Master。只有 key 从 UBS IO extension 的最后一层被移除时，才需要上报。

## 目标

- UBS IO 自主管理 DRAM/DISK FLOW 水位线和空间回收。
- 对 MemCache Master 屏蔽 UBS IO 内部 tier 细节。
- 对 UBS IO extension 最后一层淘汰进行批量上报。
- 复用同一条上报通道，在 UBS IO SSD recovery 后重建 Master 侧 SSD-extension 元数据。
- 避免 runtime eviction、truncate、free 热路径被 MemCache/Master RPC 阻塞。
- 第一版保持简单：上层 key 是唯一标识，不向上暴露 version、flow id、offset。

## 拓扑

```text
vLLM / SGLang
    -> MemCache L2 DRAM
        -> same-process UBS IO
            -> WCache DRAM FLOW
            -> WCache DISK FLOW

UBS IO
    -> same-process metadata event callback
        -> MemCache
            -> cross-process RPC
                -> Master
```

UBS IO 不直接调用 Master。UBS IO 只通过同进程 callback 把 metadata event 交给 MemCache。Master RPC 协议、批量聚合、失败重试和异常处理都由 MemCache 负责。

## 事件语义

使用一条统一 metadata event 通道，事件包含两种类型：

```cpp
enum UbsIoMetaEventType {
    UBSIO_META_RECOVER,
    UBSIO_META_DELETE,
};

struct UbsIoMetaEvent {
    UbsIoMetaEventType type;
    std::string key;
};
```

事件含义：

| Event | 来源 | Master 动作 |
| --- | --- | --- |
| `UBSIO_META_RECOVER` | SSD recovery 扫描到一个 live key | 在 UBS IO extension 元数据中恢复或重建该 key |
| `UBSIO_META_DELETE` | runtime final eviction 从 UBS IO extension 移除了一个 key | 从 UBS IO extension 元数据中删除该 key |

`DRAM FLOW -> DISK FLOW` 不产生 event，因为 key 仍然存在于 UBS IO extension 中。

## Callback API

UBS IO 内部使用 C++ callback 连接 Cache/WCacheManager：

```cpp
using UbsIoMetaEventCallback = std::function<void(const std::vector<UbsIoMetaEvent> &events)>;

void RegUbsIoMetaEventCallback(UbsIoMetaEventCallback callback);
```

但 MemCache 当前通过 `dlopen` 加载 `libubsio_kvc.so`。跨 so 边界不能暴露 `std::function` 作为稳定 ABI，因此 KVC 对外导出 C ABI 注册函数，并经 `bio_sdk` 转发到嵌入式 `bio_server`：

```cpp
enum UbsioMetaEventTypeC {
    UBSIO_META_RECOVER_C = 0,
    UBSIO_META_DELETE_C = 1,
};

typedef struct {
    int32_t type;
    const char *key;
    uint32_t keyLen;
} UbsioMetaEventC;

typedef void (*UbsioMetaEventCallbackC)(void *context, const UbsioMetaEventC *events, uint32_t count);

extern "C" int32_t UbsioKvCacheRegisterMetaEventCallback(UbsioMetaEventCallbackC callback, void *context);
```

MemCache 初始化时通过 `dlsym` 从 `libubsio_kvc.so` 获取并注册。注册可以发生在 `UbsioKvCacheInit()` 之前；UBS IO 会在 cache manager 初始化后、recovery 前应用 callback，避免漏报 recovery event：

```cpp
using RegisterFunc = int32_t (*)(UbsioMetaEventCallbackC callback, void *context);

auto registerFunc = reinterpret_cast<RegisterFunc>(dlsym(handle, "UbsioKvCacheRegisterMetaEventCallback"));
registerFunc(&MemCacheUbsioMetaEventCallback, this);
```

C callback 中的 `events` 数组和 `key` 指针只在 callback 调用期间有效。MemCache 必须在 callback 内复制 key：

```cpp
void MemCacheUbsioMetaEventCallback(void *context, const UbsioMetaEventC *events, uint32_t count)
{
    auto *memCache = static_cast<MemCache *>(context);
    memCache->EnqueueUbsIoMetaEvents(events, count); // 内部复制 std::string(events[i].key, events[i].keyLen)
}
```

之后 MemCache 再将 events 批量发送给 Master。

## Runtime 淘汰路径

Runtime eviction 必须保持高性能，不能等待 MemCache/Master RPC。

### DRAM FLOW 到 DISK FLOW

现有路径：

```text
WCache::EvictAllMemSliceToDisk()
    -> WCache::EvictFromMemToDisk()
    -> WCache::EvictFromMemToDiskImpl()
    -> memCache->Evict(oldSlice)
    -> diskCache->AddEvictQueue(sliceRef)
    -> StartEvictTask(WCACHE_DISK)
```

处理语义：

```text
不产生 metadata event
保留 WCacheIndex
sliceRef 从 memory slice 切换到 disk slice
truncate/free DRAM FLOW 空间
```

### DISK FLOW 最终淘汰

当 key 从 DISK FLOW 移除，并且不再存在于 UBS IO extension 中时：

```text
设置 meta.hasEvict = 1
删除本地 WCacheIndex entry
设置 sliceRef invalid/null
truncate/free DISK FLOW 空间
向 runtime 上报路径追加 UBSIO_META_DELETE(key)
```

### 无 SSD 模式

当 `hasDiskCache=false` 时，DRAM FLOW 就是 UBS IO extension 的最后一层。因此：

```text
DRAM FLOW -> discard 产生 UBSIO_META_DELETE(key)
```

## Runtime 上报流水线

WCache runtime eviction 使用多个 worker。当前默认值：

```cpp
MEM_EVICT_THREAD_NUM = 4
DISK_EVICT_THREAD_NUM = 8
```

多 worker 之间没有天然的“本轮全局淘汰”边界。采用三层聚合：

```text
worker-local shared batch
    -> WCacheManager pending vector
        -> reporter thread flush vector
```

### Worker 本地收集

每个 eviction worker 持有一个 shared batch：

```cpp
struct UbsIoMetaEventBatch {
    std::mutex lock;
    bool closed{ false };
    std::vector<UbsIoMetaEvent> events;
};
```

这里必须使用 shared batch，而不是简单的栈上 `std::vector`。原因是最终淘汰事件是在 `SetSlice()` callback 中追加的，该 callback 可能延迟到最后一个 reader 调用 `Release()` 时才执行，执行线程也不一定是 eviction worker 线程。

最终淘汰成功后：

```cpp
AppendMetaEvent(UBSIO_META_DELETE, key, batch);
```

本地 events 提交时机：

```text
worker eviction loop 结束
front eviction 结束
flush 结束
如果 callback 在 batch 已关闭后才执行，则 event 直接追加到 WCacheManager pending
```

### WCacheManager Pending Queue

WCacheManager 使用一维 pending vector，不保留 worker batch 边界，因为 MemCache 和 Master 不关心 worker 边界。

```cpp
std::mutex mMetaReportLock;
std::vector<UbsIoMetaEvent> mPendingMetaEvents;
```

追加操作：

```cpp
void WCacheManager::AppendMetaEvents(std::vector<UbsIoMetaEvent> &&events)
{
    if (events.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mMetaReportLock);
        mPendingMetaEvents.insert(
            mPendingMetaEvents.end(),
            std::make_move_iterator(events.begin()),
            std::make_move_iterator(events.end()));
    }

    ScheduleFlushMetaEvents();
}
```

### Reporter Thread

reporter thread 通过 swap 拿走 pending vector，然后在不持有 WCacheManager 锁的情况下调用 MemCache callback：

```cpp
void WCacheManager::FlushMetaEvents()
{
    std::vector<UbsIoMetaEvent> events;
    {
        std::lock_guard<std::mutex> lock(mMetaReportLock);
        events.swap(mPendingMetaEvents);
    }

    if (!events.empty() && mMetaEventCallback != nullptr) {
        mMetaEventCallback(events);
    }
}
```

runtime flush 触发条件：

```text
worker/front/flush 路径提交 batch 并调度 flush
recovery 提交 RECOVER batch 后同步 flush
delayed callback 在原 batch 已关闭后追加 pending 并调度 flush
service exit flush 剩余事件
```

当前实现刻意不引入单独 timer 或可配置阈值。每次提交 batch 都会立即调度 reporter。后续如果 MemCache 侧需要更细的批量策略，可以再增加这些可选参数：

```text
wcacheMetaReportBatchCount = 1024
wcacheMetaReportLocalBatchCount = 256
wcacheMetaReportMaxDelayMs = 10
```

## 并发读与预取处理

Runtime eviction 可能与上层读或预取并发。当前 WCache 中，数据访问通过 `SliceRef` 引用计数保护：

```text
reader/prefetch:
    WCacheIndex::Aquire(key)
        -> sliceRef->Aquire()
        -> 当 slice valid 时 mRef++
    读取数据
    sliceRef->Release()
        -> mRef--
```

eviction 不会同步等待正在执行的 reader，而是调用 `sliceRef->SetSlice(newSlice, callback)`：

```text
if mRef == 0:
    立即切换 slice
    立即执行 callback

if mRef > 0:
    保存 new slice 和 callback
    立即返回
    等最后一个 reader 调用 Release() 后再执行 callback
```

对于 `DRAM FLOW -> DISK FLOW`，含义是：

```text
reader 继续使用旧 DRAM slice
eviction 将 data/meta copy 到 DISK FLOW
旧 reader 结束后，sliceRef 切换到 DISK slice
旧 DRAM FLOW 资源在 delayed callback 中释放
```

对于最终淘汰，含义是：

```text
reader 继续使用旧 memory/disk slice
eviction 标记 meta evicted，并调用 SetSlice(nullptr, callback)
实际 WCacheIndex 删除、data Flow truncate/free、metadata DELETE 上报都在 callback 中执行
如果有活跃 reader，callback 会延迟到最后一个 reader Release() 后执行
```

因此，并发读或预取不会中断 eviction，也不应该让 eviction worker 同步等待读完成。旧 Flow 空间释放会延迟到 reader 全部结束。

### 既有 UBS IO 行为

metadata event 上报没有引入新的读/淘汰同步模型，而是沿用 UBS IO 既有行为：

```text
DRAM FLOW -> DISK FLOW:
    现有代码已经使用 SetSlice(diskSlice, callback)
    旧 DRAM slice 在 callback 中释放

DISK FLOW -> UnderFS/final discard:
    现有代码已经使用 SetSlice(nullptr, callback)
    旧 DISK slice 在 callback 中释放，WCacheIndex 在 callback 中删除
```

新增 metadata report 只是挂在同一个 callback 点：

```text
旧行为:
    callback free/truncate 旧 Flow 资源，并删除 WCacheIndex

新行为:
    callback 仍然 free/truncate 旧 Flow 资源，并删除 WCacheIndex
    callback 额外追加 UBSIO_META_DELETE
```

因此，如果读或预取已经持有 slice，上报会和原本的资源释放一起延迟。这保持了原有生命周期保证。

### Final Eviction 期间的新读

一个需要注意的问题是：当某个 slice 已经被选中做最终淘汰时，新读可能持续 acquire 该 slice，从而延迟真正释放和上报。

当前代码保持既有行为：final WCacheIndex 删除发生在 delayed callback 中。也就是说，在 callback 真正执行并删除 index 前，新读仍可能 acquire 到这个 slice。这样不会改变历史读语义，但如果读持续进入，最终资源回收可能被延迟。

如果后续发现 reclaim 延迟成为问题，可以做独立优化：

```text
1. Eviction worker 获取 per-slice OpLock。
2. SetSlice(nullptr) 前，先将 sliceRef 标记为不可 acquire。
3. 已有 reader 正常继续并结束。
4. 新 read/prefetch 无法 acquire sliceRef，返回 NOT_EXISTS 或 miss。
5. 已有 reader Release() 后执行 SetSlice callback。
6. callback 删除 WCacheIndex entry，free/truncate Flow 资源，并追加 UBSIO_META_DELETE。
```

这种优化可以保证正确性，同时避免阻塞：

```text
existing readers: 允许正常完成
new readers: final eviction 开始后被拒绝
eviction worker: 不等待 existing readers
physical free/report: 延迟到 old readers release 后执行
```

对于内部 `DRAM FLOW -> DISK FLOW` 迁移，slice 仍然有效且 key 仍在 UBS IO extension 中，因此新读可以继续使用当前 slice，直到切换到 DISK slice 完成。

### Delayed Callback 下的 Event Batch

最终淘汰事件在 `SetSlice()` callback 中产生。由于 callback 可能被活跃 reader 延迟，runtime batch 不能捕获 worker 栈上的 vector 引用。

使用 shared batch context：

```cpp
struct UbsIoMetaEventBatch {
    std::mutex lock;
    bool closed{ false };
    std::vector<UbsIoMetaEvent> events;
};
```

处理规则：

```text
worker 创建 shared batch
delayed callback 在 batch open 时将 DELETE 追加到 batch 中
worker eviction loop 结束时 flush 并关闭 batch
如果 delayed callback 在 batch closed 后才执行，则直接追加到 WCacheManager pending queue
```

这样可以避免悬空引用，并确保即使 callback 在 worker 返回后才执行，也不会丢失 metadata DELETE event。

## SSD Recovery 路径

Recovery 发生在 UBS IO 服务正式拉起前。此时 runtime eviction 尚未运行，因此 recovery event 不会和 runtime DELETE event 并发冲突。

现有 recovery 已经会扫描 disk meta flow：

```text
WCache::Recover()
    -> read WFlowSliceMeta
    -> if hasEvict == 0
        -> 通过 recoverCallback 重建本地 WCacheIndex
```

在本地 WCacheIndex 重建成功后，扩展 recovery 流程，收集 `UBSIO_META_RECOVER(key)`。

Recovery 流程：

```text
1. UBS IO 启动 recovery。
2. 扫描 SSD meta flow。
3. 对每条 live record，重建本地 WCacheIndex。
4. 向 recovery batch 添加 UBSIO_META_RECOVER(key)。
5. 通过同一条 metadata callback flush batch。
6. 继续扫描。
7. 扫描结束后 flush 剩余 events。
8. 启动 UBS IO 服务。
```

Recovery 没有严格延迟要求，因此可以采用简单的批量同步上报。

建议的 recovery batch size：

```text
wcacheRecoverMetaReportBatchCount = 4096 or 8192
```

如果 MemCache 选择同步重建 Master 元数据，recovery 上报可以阻塞启动流程，这是可以接受的，因为此时 UBS IO 还没有处理 runtime eviction。如果后续希望更快启动，MemCache 可以先 enqueue recovery events 并在后台上报，但代价是 Master 对 SSD-extension 命中可见性会短暂不完整。

## Key 来源

当前 `WCacheSliceRef` 不包含 key：

```cpp
using WCacheSliceRef = SliceRef<WCacheSlicePtr>;
```

key 存储在 `WFlowSliceMeta` 中：

```cpp
struct WFlowSliceMeta {
    char key[NO_512 - NO_32];
    uint64_t offset;
    uint64_t length;
    uint64_t magic;
    uint64_t hasEvict;
};
```

第一版保持这个模型：

```text
Runtime final eviction 本来就需要读取 meta 并设置 hasEvict。
Recovery 本来就需要读取 meta 并重建 WCacheIndex。
使用 sliceMeta.key 生成 RECOVER/DELETE events。
```

后续可以选择把 `WCacheSliceRef` 做成 WCache 专用类，并在运行态持有 key。但第一版不需要做这个优化。

## 一致性模型

Runtime eviction 采用异步 report-after-free 语义：

```text
UBS IO 先释放本地空间。
UBS IO 异步向 MemCache 上报 DELETE。
MemCache 异步向 Master 上报 DELETE。
```

这样可以让 eviction 热路径不受 Master RPC 延迟影响。

Master 中可能短暂存在 stale metadata：

```text
Master 仍然认为 key 位于 UBS IO extension。
UBS IO 已经淘汰该 key。
```

读路径应当能够容忍 UBS IO 返回 `NOT_EXISTS`，并由 MemCache/Master 清理元数据或回退处理。

Recovery event 更简单：

```text
Recovery 在服务启动前执行。
RECOVER event 和 runtime DELETE event 不并发。
第一版不需要 version/generation。
```

该设计假设上层 key 是唯一标识，并且不会被复用为不同数据。

## 失败处理

UBS IO 到 MemCache 是同进程调用。C ABI callback 是 `void`：

```cpp
typedef void (*UbsioMetaEventCallbackC)(void *context, const UbsioMetaEventC *events, uint32_t count);
```

失败处理属于 MemCache 职责：

```text
MemCache enqueue UBS IO events。
MemCache 发送 Master RPC。
MemCache 负责 Master RPC 失败重试。
UBS IO 不回滚本地淘汰。
```

如果后续 MemCache 需要反压或投递确认，可以将 callback 升级为返回 `BResult`，但第一版不建议增加复杂度。

## 性能原则

- 不在 eviction worker 中直接调用 MemCache callback。
- 调用 MemCache callback 时不持有 WCacheManager 锁。
- 使用 worker-local shared batch，减少逐 key 竞争 WCacheManager 锁，同时兼容 delayed callback。
- WCacheManager 中使用一维 pending vector。
- reporter thread 通过 swap 缩短锁持有时间。
- 淘汰风暴下避免逐 key 打 info 日志。

## 实施步骤

1. 增加 `UbsIoMetaEvent` 和 `UbsIoMetaEventCallback` 内部定义。
2. 在 Cache/WCacheManager 层增加 `RegUbsIoMetaEventCallback()` 透传。
3. 在 `bio_server` C ABI 中导出 `UbsioRegisterMetaEventCallback()`，并支持 cache 初始化前暂存 callback。
4. 在 `bio_sdk` 中导出 `BioRegisterMetaEventCallback()`，转发到 `bio_server`。
5. 在 `libubsio_kvc.so` 中导出 `UbsioKvCacheRegisterMetaEventCallback()`，供 MemCache `dlsym` 注册。
6. 增加 WCacheManager pending event vector 和异步 reporter executor。
7. runtime final eviction 追加 `UBSIO_META_DELETE`。
8. 确保 `DRAM FLOW -> DISK FLOW` 不上报 event。
9. SSD recovery 在本地 index 重建成功后，批量追加 `UBSIO_META_RECOVER`。
10. MemCache 侧注册 callback，并在 UBS IO 外部完成 Master RPC 对接。
11. 补充测试：内部迁移不上报、最终淘汰 DELETE、recovery RECOVER、多 worker 并发 event 聚合。

## 最终决策

使用一条 UBS IO metadata event 通道：

```text
UBSIO_META_RECOVER -> recovery 后重建 Master SSD-extension metadata
UBSIO_META_DELETE -> runtime final eviction 后删除 Master SSD-extension metadata
```

Runtime eviction 为了性能异步上报。Recovery 在服务启动前批量上报，可以采用更简单、更保守的同步处理。
