# UBS IO SSD 可选化缓存改造方案

## 背景

当前 UBS IO 默认要求配置 SSD，`bio.disk.path` 会被解析为本地磁盘列表，并驱动 BDM、CM 磁盘注册、WCache disk tier、RCache disk flow 等路径初始化。

在只需要内存缓存能力的场景下，节点不接 SSD，也不接 underFs。此时数据只保留在 WCache memory flow 中，超过内存水位后直接淘汰丢弃。该场景要求与当前 WCache 分层架构保持一致，不新增独立的 memory-only cache 架构。

## 目标

- 支持 `bio.disk.path` 为空时启动 UBS IO。
- 配置 SSD 时保持原有行为：WCache memory 超水位后淘汰到 SSD。
- 未配置 SSD 时复用原有内存淘汰调度：WCache memory 超水位后直接 discard。
- RCache 不新增显式开关，由现有内存读写配比控制；无 SSD 时支持 memory-only RCache。
- 不重构 CM/PT 模型，不重构 WCache/FlowManager 主架构。

## 非目标

- 不支持无 SSD 场景的数据持久化。
- 不支持无 SSD 场景的 `WRITE_THROUGH`。
- 不支持无 SSD 场景的动态加盘。
- 不引入新的 cache manager 或独立 memory-only cache 实现。

## 配置语义

### SSD 是否启用

通过 `bio.disk.path` 推导：

```ini
bio.disk.path = /dev/ssd0:/dev/ssd1
```

表示存在 SSD，启用 disk tier。

```ini
bio.disk.path =
```

表示无 SSD，不启用 disk tier。

内部运行态通过 `DaemonConfig::hasDiskCache` 保存该推导结果。

### RCache 是否启用

通过现有内存读写配比推导：

```ini
bio.cache.mem_read_write_ratio = 5:5
```

表示给 RCache 分配读缓存资源，允许启用 RCache。

```ini
bio.cache.mem_read_write_ratio = 0:10
```

表示不给 RCache 分配读缓存资源，不启用 RCache。

内部运行态：

```cpp
enableRCache = memReadRatio > 0;
```

无 SSD 时 RCache 不创建 disk tier，只使用 memory tier。是否启用仍由 `memReadRatio > 0` 决定。

### 无 SSD 推荐配置

```ini
bio.disk.path =
bio.underfs.file_system_type = none
bio.cache.mem_read_write_ratio = 0:10
bio.wcache.evict_water_level = 90
```

如果希望无 SSD 场景仍启用 RCache，可将读比例设置为非 0，例如：

```ini
bio.cache.mem_read_write_ratio = 2:8
```

如果无 SSD 且 `bio.wcache.evict_water_level = 0`，内部默认将内存淘汰水位调整为 `90`，避免写入后立即全部淘汰。

## 架构设计

本次改造保持当前架构：

```text
Cache
  -> WCacheManager
    -> WCache
      -> WCacheTier
        -> FlowManager
```

唯一架构调整是：`WCACHE_DISK` 从必选 tier 变为可选 tier。

### 配置 SSD 时

```text
Put
  -> WCACHE_MEMORY Write
  -> memory evict queue
  -> memory over water
  -> EvictFromMemToDisk
  -> WCACHE_DISK
```

原有 memory 到 disk 的淘汰路径保持不变。

### 未配置 SSD 时

```text
Put
  -> WCACHE_MEMORY Write
  -> memory evict queue
  -> memory over water
  -> EvictFromMemToDiscard
  -> 删除 WCache 索引
  -> 释放 memory flow 资源
```

无 SSD 模式继续复用 `StartEvictTask(WCACHE_MEMORY)`、memory evict queue 和 WCache 索引回调机制。

## 主要改动点

### 配置层

文件：

```text
ubsio-boostio/src/config/bio_config_instance.h
ubsio-boostio/src/config/bio_config_instance.cpp
```

改动：

- 增加内部运行态 `DaemonConfig::hasDiskCache`。
- 保留内部运行态 `DaemonConfig::enableRCache`，但不新增外部配置。
- `bio.disk.path` 为空时：
  - `hasDiskCache = false`
  - 清空 `diskList` 和 `diskCaps`
  - 不再将 `memCap` 清零
- 根据 `mem_read_write_ratio` 推导 RCache 是否启用。

### 启动层

文件：

```text
ubsio-boostio/src/server/bio_server.cpp
ubsio-boostio/src/server/mirror_server.cpp
```

改动：

- 无 SSD 时跳过 BDM 初始化。
- 无 SSD 时注册保护型 `DiskAllocator`，误入 `FLOW_DISK` 分配时返回错误。
- 无 SSD 时 CM 上报虚拟 `diskId = 0`，用于兼容当前 PT 分配模型。
- 无 SSD 时拒绝动态 `AddDisk`、`BioBdmUpdate`、`BioDiskReset`。

### Cache 层

文件：

```text
ubsio-boostio/src/cache/cache.cpp
ubsio-boostio/src/cache/cache.h
```

改动：

- RCache 可选初始化。
- RCache 关闭时：
  - 不创建 RCache flow
  - 不访问 RCacheManager
  - RCache 资源查询返回 0
  - 读 miss 且无 underFs 时返回 `BIO_NOT_EXISTS`

### WCache 层

文件：

```text
ubsio-boostio/src/cache/write/wcache.cpp
ubsio-boostio/src/cache/write/wcache.h
ubsio-boostio/src/cache/write/wcache_manager.cpp
ubsio-boostio/src/cache/write/wcache_manager.h
```

改动：

- `WCache::Init()` 根据 `hasDiskCache` 决定是否创建 `WCACHE_DISK`。
- 无 SSD 时不启动 disk evict executor。
- 无 SSD 时 `WRITE_THROUGH` 返回 `BIO_INVALID_PARAM`。
- `EvictMemSatisfiedCond()` 在无 SSD 时只判断 memory 水位，不访问 `diskCaps[mDiskId]`。
- 新增 memory discard 淘汰路径：
  - 读取 memory meta 中的 key
  - 释放 memory slice
  - 通过现有 evict callback 删除 WCache 索引
  - 将 sliceRef 置为 invalid

## 行为说明

### 写路径

有 SSD：

```text
WRITE_BACK/WRITE_THROUGH 维持原有策略
memory 超水位后淘汰到 disk
```

无 SSD：

```text
WRITE_BACK 写入 memory
WRITE_THROUGH 返回 BIO_INVALID_PARAM
memory 超水位后 discard
```

### 读路径

无 SSD、无 RCache、无 underFs：

```text
WCache memory 命中 -> 返回数据
WCache miss -> 返回 BIO_NOT_EXISTS
被 discard 的 key 后续读取 -> BIO_NOT_EXISTS
```

### 资源查询

无 SSD：

```text
wcache.memCapacity > 0
wcache.memUsedSize >= 0
wcache.diskCapacity = 0
wcache.diskUsedSize = 0
rcache.memCapacity > 0 if memReadRatio > 0
rcache.diskCapacity = 0
rcache.diskUsedSize = 0
```

## 风险与约束

- 虚拟 `diskId = 0` 仅用于兼容 CM/PT 模型，不代表真实磁盘。
- 无 SSD 模式必须避免进入 BDM、`FLOW_DISK`、`diskCaps[mDiskId]` 路径。
- discard 淘汰必须清理 WCache 索引，否则可能出现索引命中无效 sliceRef。
- 无 SSD 模式不具备持久化能力，服务重启后缓存数据丢失。
- 无 SSD 模式不支持 `WRITE_THROUGH`，避免误导用户认为数据已持久化。

## 验证建议

- `bio.disk.path` 非空时，验证原有 SSD 模式 memory 到 disk 淘汰不变。
- `bio.disk.path` 为空时，验证服务启动不依赖 BDM。
- `bio.disk.path` 为空时，验证 CM 注册包含虚拟 `diskId = 0`。
- `bio.cache.mem_read_write_ratio = 0:10` 时，验证不初始化 RCache。
- `bio.disk.path` 为空且 `bio.cache.mem_read_write_ratio` 读比例非 0 时，验证 RCache 只创建 memory tier。
- 无 SSD 写入超过水位后，验证 memory slice 被 discard，后续读返回 `BIO_NOT_EXISTS`。
- 验证无 SSD 下 `WRITE_THROUGH` 返回 `BIO_INVALID_PARAM`。
- 验证无 SSD 下资源查询 disk 资源为 0。

## 架构影响

本次没有重构原有架构。

保持不变：

- `Cache/WCacheManager/WCache/WCacheTier/FlowManager` 分层。
- WCache 写入先落 memory tier 的主路径。
- SSD 模式下 memory 到 disk 的淘汰链路。
- CM/PT 的 `nodeId + diskId` 分配模型。

新增能力：

- `WCACHE_DISK` 可选。
- 无 SSD 时 memory evict target 从 disk 切换为 discard。
