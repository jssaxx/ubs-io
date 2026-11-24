# UBS IO 单机模式配置参考

本文面向 standalone 推理三级池化场景。UBS IO 可以使用本地 SSD 作为 KV Cache 缓存层，也可以将本地
或共享文件系统目录作为 UnderFS 后端，使 wcache 数据直接淘汰到文件系统。

本文只说明推荐配置方式，不修改仓库中的现有配置文件。默认配置文件为 `ubsio-boostio/configs/ubsio.conf`；如需指定运行时配置，建议通过环境变量加载：

```bash
export UBSIO_CONFIG_PATH=/path/to/ubsio.conf
```

## 适用范围

- standalone 模式推理服务，覆盖本地 SSD、本地文件系统和共享文件系统后端三种 KV Cache 扩容场景。
- UBSIO-KV 通过 BoostIO 后端提供标准 KV 接口，并可与 memcache、Mooncake 组合使用。
- 不要求 UBS IO 自身绑定特定硬件；与 memcache、Mooncake 或上层推理框架组合使用时，以对应项目官方文档为准。

## 最小单机模式示例

```ini
# BoostIO 配置
[ubsio]

# 设置缓存盘或分区路径。不填表示不使用磁盘设备，仅使用ubsio内存。
ubsio.disk.path =

# ubsio单个进程内存大小，单位GB。例如单节点剩余内存200GB，推理服务启动4卡，可以配置200/4=50GB。
ubsio.mem.size_in_gb = 50
# ubsio磁盘淘汰水位，示例为90%
ubsio.wcache.disk_evict_water_level = 90

# 推理服务中开启二级池化，且配置 DRAM 不为 0 的 worker 进程数
ubsio.standalone.device_count = 1

# 设置日志输出级别。可选`error`、`warn`、`info`、`debug`、`trace`。
ubsio.log.level = info
# 设置服务端日志根目录，普通日志写入该目录，统计日志写入该目录下的trace子目录。
ubsio.log.path = /var/log/ubsio

# 以下配置无需修改
# 将全部内存缓存容量用于写缓存。
ubsio.cache.mem_read_write_ratio = 0:10
# 将全部磁盘缓存容量用于写缓存。
ubsio.cache.disk_read_write_ratio = 0:10
# 不使用 UnderFS 时设置为 none；后端类型可选 ceph、hdfs、local、shared 或 none。
ubsio.underfs.file_system_type = none
# 本地单机运行时禁用 TLS 配置路径校验。
ubsio.net.tls.enable.switch = false
ubsio.sdkmem.size_in_mb = 0
ubsio.cli_tools.enable = true
ubsio.cache.qos.enable = true
# QoS 写配额水位默认88%，生效上限90%；单机模式可能按内存预算进一步下调。
ubsio.cache.qos.water_level = 88

# standalone BatchGet 使用 scratch buffer 中转数据。
ubsio.batch_read.standalone.use_scratch_pool = true
# BatchGet 同时在途的批读窗口数量。
ubsio.batch_read.pipeline_depth = 4
# BatchGet 从 scratch buffer 拷贝到请求目标 buffer 的并发 worker 数。
ubsio.batch_read.copy_workers = 4
```

### 本地文件系统后端

在上述最小配置的基础上，只需覆盖以下两项。`/path/to/local/kv` 为同一槽位分配域内各进程共用、已存在且可读写的父目录。
基础示例的 `ubsio.disk.path` 为空，内存写缓存淘汰时直接下刷文件系统。

```ini
ubsio.underfs.file_system_type = local
ubsio.underfs.root_path = /path/to/local/kv
```

### 共享文件系统后端

在上述最小配置的基础上，使用以下配置覆盖对应项。`/path/to/shared/kv` 为所有相关存储 rank 均可访问的
同一共享存储目录，启动前应完成挂载并确保运行用户具有读写权限。

```ini
ubsio.underfs.file_system_type = shared
ubsio.underfs.root_path = /path/to/shared/kv
```

## 配置表

| 配置项 | 值类型 | 是否必填 | 默认值 | 有效范围 | 说明                                                                                                                             |
| --- | --- | --- | --- | --- |--------------------------------------------------------------------------------------------------------------------------------|
| `ubsio.disk.path` | 字符串 | 可选 | 空 | standalone 模式最多 `16` 个有效块设备路径，以英文冒号分隔 | UBS IO 独占的整盘、分区或 loop 块设备。留空表示不使用裸盘；启用 `local` / `shared` UnderFS 时，wcache 数据直接淘汰到配置目录；未启用 UnderFS 时仅使用内存缓存。设备不能存在挂载点或需要保留的数据。 |
| `ubsio.log.level` | 字符串 | 可选 | `info` | `error`、`warn`、`info`、`debug`、`trace` | 配置初始化后设置 BoostIO server 日志级别。                                                                                                  |
| `ubsio.log.path` | 字符串 | 可选 | `/var/log/ubsio` | 非空且可创建或已存在的目录路径 | BoostIO server 普通日志目录；统计日志写入其 `trace` 子目录。                                                                                     |
| `ubsio.standalone.device_count` | 整数 | 可选 | `1` | 使用块设备或 `local` 时为 `1` 到 `16`；纯内存或 `shared` 模式可为 `0` | 使用块设备或 `local` 的配置设备数及 slot 分配上限。LocalFS 按 slot 隔离目录，额度取文件系统总容量的 90% 再除以该配置值；slot 的按需分配和释放不改变分母，同一分配域使用相同配置值。                  |
| `ubsio.standalone.device_id_gather_timeout_sec` | 整数 | 可选 | `180` | `1` 到 `2147483647` | 配置缓存盘时，等待全部 standalone 逻辑 device ID 完成汇聚的超时时间，单位为秒。                                                                            |
| `ubsio.standalone.force_new_disk` | 布尔值 | 可选 | `false` | `true`、`false` | 是否在启动时将目标设备初始化为新缓存盘。当前版本不恢复旧缓存盘，配置 SSD 时建议设置为 `true`；原缓存会失效。未配置 SSD 时不生效。                                                      |
| `ubsio.segment.size_in_mb` | 整数 | 可选 | `4` | `1` 到 `16` | 缓存 segment 大小。单机内存池 block、BDM chunk 和 SDK data-message block 都使用该大小。                                                           |
| `ubsio.mem.size_in_gb` | 整数 | 可选 | `50` | `0` 到 `3072`；不得超过当前系统可用内存 | 单个 UBS IO 进程的内存池容量。部署建议不低于 `5GB`；SSD 场景推荐 `10GB`，分离部署场景推荐 `50GB`。                                                              |
| `ubsio.sdkmem.size_in_mb` | 整数 | 可选 | `0` | `0` 到 `4194304` | SDK data-message 内存池大小，主要用于 BatchGet 数据缓冲。                                                                                     |
| `ubsio.net.tls.enable.switch` | 布尔值 | 可选 | `false` | `true` 或 `false` | 本地单机运行且没有 TLS 文件时，将该项设为 `false`。                                                                                               |
| `ubsio.trace.enable` | 布尔值 | 可选 | `true` | `true` 或 `false` | 启用或禁用 HTrace 采集。单机模式会初始化 tracer 模块。                                                                                            |
| `ubsio.data.crc.enable` | 布尔值 | 可选 | `false` | `true` 或 `false` | 启用缓存数据 CRC 校验。单机 direct-call 路径会把该设置传给 SDK 和 cache 模块。                                                                         |
| `ubsio.cache.qos.enable` | 布尔值 | 可选 | `true` | `true` 或 `false` | 启用 cache QoS 和过载控制行为。                                                                                                          |
| `ubsio.cache.qos.water_level` | 整数 | 可选 | `88` | 输入为 `1` 到 `100`，实际生效上限为 `90` | 写配额上限占 WCache 内存容量的百分比；配置超过 `90` 时按 `90` 处理，单机模式还可能按内存预算进一步下调。启用 QoS 时，实际内存淘汰水位不超过原生效水位和 `max(0, 实际QoS水位−5)`，单机模式下还受配额补充余量约束。  |
| `ubsio.wcache.evict_water_level` | 整数 | 可选 | `0` | `0` 到 `100` | L2.5 写缓存内存淘汰水位。无盘模式下配置为 `0` 时，运行时水位按 `90` 处理；仅使用 L3 时设置为 `0`；同时使用 L2.5 和 L3 时设置为大于 `0`。                                        |
| `ubsio.wcache.disk_evict_water_level` | 整数 | 可选 | `90` | `0` 到 `100` | 块设备写缓存的淘汰触发水位，单位百分比。                                                                                                           |
| `ubsio.rcache.evict_water_level` | 整数 | 可选 | `90` | `0` 到 `100` | 读缓存淘汰水位。内存和磁盘读缓存淘汰阈值使用同一个值。                                                                                                    |
| `ubsio.cache.mem_read_write_ratio` | 字符串 | 可选 | `0:10` | 两个 `0` 到 `10` 的整数；总和必须为 `10` | 将 `ubsio.mem.size_in_gb` 划分给读缓存和写缓存。例如：`0:10` 表示把全部内存缓存容量预留给写缓存。                                                               |
| `ubsio.cache.disk_read_write_ratio` | 字符串 | 可选 | `0:10` | 两个 `0` 到 `10` 的整数；总和必须为 `10` | 将缓存盘容量划分给读缓存和写缓存。例如：`0:10` 表示把全部磁盘缓存容量预留给写缓存。                                                                                  |
| `ubsio.bdm.io_engine` | 字符串 | 可选 | `sync` | `sync`、`io_uring` | BDM I/O 引擎。                                                                                                                    |
| `ubsio.bdm.io_uring.sqpoll_mode` | 字符串 | 可选 | `auto` | `auto`、`required`、`disabled` | io_uring 内核轮询模式；`auto` 在内核轮询不可用时使用普通 io_uring。                                                                                 |
| `ubsio.bdm.sync.worker_num` | 整数 | 可选 | `16` | `1` 到 `64` | sync 引擎执行 BDM 批量阻塞 I/O 的内部线程数。                                                                                                 |
| `ubsio.bdm.batch_read.window_keys` | 整数 | 可选 | `128` | `1` 到 `1024` | BatchGet 经 BDM 读盘时，单个窗口的 key 数上限。                                                                                              |
| `ubsio.bdm.batch_read.window_bytes_mb` | 整数 | 可选 | `64` | `1` 到 `1024` | BatchGet 经 BDM 读盘时，单个窗口的字节数上限，单位 MB。                                                                                           |
| `ubsio.bdm.batch_read.temp_pool_mb` | 整数 | 可选 | `0` | `0` 到 `65535` | BDM 批量读独立临时缓冲池大小，单位 MB；`0` 表示按需申请。                                                                                             |
| `ubsio.work.io.timeout` | 整数 | 可选 | `60` | `60` 到 `300` | 通过单机 runtime config 传给 SDK 的 I/O 超时时间，单位为秒。                                                                                    |
| `ubsio.batchget.thread.num` | 整数 | 可选 | `32` | `8` 到 `512` | 非 standalone 模式下 MirrorServer BatchGet executor 的线程数。standalone 模式使用 `ubsio.batch_read.copy_workers`。                          |
| `ubsio.batch_read.standalone.use_scratch_pool` | 布尔值 | 可选 | `true` | `true` 或 `false` | standalone BatchGet 是否先将 SSD 或 UnderFS 数据读入内部 scratch buffer，再拷贝到上层目标 buffer。上层 buffer 不能直接用于存储 I/O 时必须开启。                     |
| `ubsio.batch_read.pipeline_depth` | 整数 | 可选 | `4` | `1` 到 `64` | BatchGet 同时在途的 BDM 批读窗口数量；同时与 `copy_workers` 共同决定 UnderFS BatchGet 最大在途请求数。                                                    |
| `ubsio.batch_read.copy_workers` | 整数 | 可选 | `4` | `1` 到 `64` | standalone BatchGet 从 SSD 或 UnderFS scratch buffer 拷贝到请求目标 buffer 的并发 worker 数，同时决定 standalone BatchGet executor 的线程数。         |
| `ubsio.cli_tools.enable` | 布尔值 | 可选 | `false` | `true` 或 `false` | 启用 CLI agent 诊断能力。使用 `cli_client --attach` 连接 `bio_console` 时应设为 `true`。                                                       |
| `ubsio.underfs.file_system_type` | 字符串 | 启用 LocalFS 时需设置 | `none` | `ceph`、`hdfs`、`local`、`shared` 或 `none` | UnderFS 后端类型。`local` 使用节点本地目录，`shared` 使用所有相关存储 rank 可访问的共享存储挂载目录；不使用 UnderFS 时设置为 `none`。                                     |
| `ubsio.underfs.root_path` | 字符串 | `local` / `shared` 部署时显式设置 | `/mnt/kv`（内置默认） | 已存在且可读写的目录 | 文件系统 KV 根目录。`shared` 使用所有相关存储 rank 可见的共享目录。                                                          |
| `ubsio.underfs.eviction.policy` | 字符串 | 可选 | `lru` | `lru` | 指定 `underfs` 的淘汰策略，目前仅支持 `LRU`。仅在 `underfs` 为 `local` 时生效。                                                                     |
| `ubsio.underfs.eviction.check_interval_seconds` | 整数 | 可选 | `10` | `1` 到 `3600` | 后台淘汰周期检测时间。单位为秒。仅在 `underfs` 为 `local` 时生效。                                                                                    |
| `ubsio.underfs.batch_read.worker_num` | 整数 | 可选 | `8` | `1` 到 `64` | standalone BatchGet、BatchStat 和 BatchExist 并发访问 UnderFS 的 worker 数。                                                            |
| `ubsio.underfs.ceph.cfg.path` | 字符串 | 可选 | `/etc/ceph/ceph.conf` | 当 `ubsio.underfs.file_system_type = ceph` 时必须是已存在路径 | Ceph 配置文件路径。当 UnderFS 类型为 `ceph` 且该路径无效时，配置初始化失败。                                                                              |
| `ubsio.underfs.ceph.cluster` | 字符串 | 可选 | `ceph` | 非空字符串 | 后端为 Ceph 时，UnderFS 插件使用的 Ceph 集群名。                                                                                             |
| `ubsio.underfs.ceph.user` | 字符串 | 可选 | `client.admin` | 非空字符串 | 后端为 Ceph 时，UnderFS 插件使用的 Ceph 用户。                                                                                              |
| `ubsio.underfs.ceph.pool` | 字符串 | 可选 | `0:jfspool1,1:jfspool2` | 使用逗号分隔的 `poolId:poolName` 条目，poolId 为非负整数 | Ceph pool 映射。当前 UnderFS helper 使用 pool id `0` 初始化插件。                                                                           |

## 详细配置项说明

### `ubsio.disk.path`

`ubsio.disk.path` 用于指定 UBS IO 的本地 SSD 缓存设备。支持整盘块设备、已有分区和 loop 设备；多个路径使用英文冒号（`:`）分隔，standalone 模式最多配置 `16` 条路径。

配置的设备必须由 UBS IO 独占，不能存在挂载点或需要保留的数据。使用 loop 设备时，应先在宿主机创建，再映射到容器中。留空表示无盘模式，此时使用 `ubsio.mem.size_in_gb` 配置的内存池。启用 `local` / `shared` UnderFS 时，wcache 数据可以淘汰到配置目录；未启用 UnderFS 时，进程退出后内存缓存数据不会保留。

```ini
# 同时配置整盘、分区和 loop 设备
ubsio.disk.path = /dev/nvme0n1:/dev/nvme1n1p1:/dev/loop2

# 无盘模式
ubsio.disk.path =
```

### `ubsio.standalone.device_count`

`ubsio.standalone.device_count` 表示参与本地缓存路径分配的 device 数量。纯内存模式配置为 `0`。
配置缓存盘时取值为 `1` 到 `16`，并与 `ock.mmc.local_service.dram.size > 0` 的 `local_service` 进程数一致。
`ubsio.disk.path` 中配置由这些进程共同使用的整盘、分区或 loop 设备。

例如，有 `4` 个启用 DRAM 的 `local_service` 进程并使用两块缓存盘时，可配置为：

```ini
ubsio.disk.path = /dev/nvme0n1:/dev/nvme1n1
ubsio.standalone.device_count = 4
```

只有 `1` 个启用 DRAM 的 `local_service` 进程并使用一块缓存盘时，可配置为：

```ini
ubsio.disk.path = /dev/nvme0n1
ubsio.standalone.device_count = 1
```

### `ubsio.mem.size_in_gb`

`ubsio.mem.size_in_gb` 表示单个 UBS IO 进程可使用的本地内存池容量，单位为 GB。该值应根据节点可供 UBS IO 使用的剩余内存和启用 DRAM 的 `local_service` 进程数计算。

节点剩余内存是指扣除系统预留、推理进程和 DRAM 池等占用后，可分配给 UBS IO 的内存。单进程配置上限按以下公式计算，结果向下取整：

```text
min(3072, floor(节点可供 UBS IO 使用的剩余内存（GB） / 启用 DRAM 的 local_service 进程数))
```

生产部署建议单进程至少预留 `5GB`。SSD 场景通常配置为 `10GB`，分离部署场景通常配置为 `50GB`；实际配置值不能超过上述计算结果。仅在需要使用 L2.5 缓存时考虑增大该值，并同步调整 `ubsio.wcache.evict_water_level`。

例如，节点可供 UBS IO 使用的剩余内存为 `200GB`，共有 `4` 个启用 DRAM 的 `local_service` 进程，则单进程配置上限为 `50GB`。SSD 场景可配置为：

```ini
# floor(200 / 4) = 50，单进程配置上限为 50GB
ubsio.mem.size_in_gb = 10
```

### `ubsio.wcache.evict_water_level`

`ubsio.wcache.evict_water_level` 是 L2.5 写缓存的内存淘汰水位，表示 L2.5 使用量达到写缓存内存容量的多少百分比后开始淘汰，取值范围为 `0` 到 `100`。

启用 QoS 时，这个配置是请求水位，实际生效值还受 QoS 预算限制。默认 QoS 水位为 `88%` 时，内存淘汰水位最高为 `83%`；小容量配置可能进一步降低。应以启动日志中打印的实际生效水位为准。

- L2.5 是 UBS IO 的内存写缓存层，位于上层 DRAM 缓存与本地 SSD 缓存之间。
- L3 是 `ubsio.disk.path` 指定的本地 SSD 缓存层。

仅使用 L2.5、不启用 L3 时，数据达到水位后直接淘汰，建议设置为较高水位，例如：

```ini
ubsio.disk.path =
ubsio.wcache.evict_water_level = 85
```

仅使用 L3 时，将水位设置为 `0`，使写入 L2.5 的数据尽快淘汰到 L3：

```ini
ubsio.disk.path = /dev/nvme0n1:/dev/nvme1n1
ubsio.wcache.evict_water_level = 0
```

同时使用 L2.5 和 L3 时，将水位设置为大于 `0` 的值，使数据优先保留在 L2.5，达到水位后再淘汰到 L3。水位越高，数据越倾向于保留在 L2.5。例如：

```ini
ubsio.disk.path = /dev/nvme0n1:/dev/nvme1n1
ubsio.wcache.evict_water_level = 85
```

### `ubsio.underfs.file_system_type`

<a name="ubsio-underfs-file-system-type"></a>

`ubsio.underfs.file_system_type` 指定 UnderFS 访问后端。

| 配置值 | 后端用途 | Memcache 元数据管理模式 |
| --- | --- | --- |
| `none` | 不使用 UnderFS。 | 聚合模式。 |
| `local` | 各进程独占所属 slot 目录，额度取文件系统总容量的 90% 再除以配置设备数，自动执行 LRU 水位淘汰。 | 聚合模式。 |
| `shared` | 通过挂载目录访问所有相关存储 rank 可见的同一共享存储命名空间，不探测容量、不淘汰。 | 分离模式。 |
| `ceph` | 使用 Ceph 后端。 | 聚合模式。 |
| `hdfs` | 使用 HDFS 后端。 | 聚合模式。 |

`local` 和 `shared` 复用哈希路径规则和 POSIX 读写实现，通过文件系统接口访问 `ubsio.underfs.root_path` 指定的目录。
local 文件保存完整 key 和长度尾部，shared 文件只存原始 value。`local` 独占所属 slot 的文件并自动淘汰；
跨 rank 共用同一批文件时选择 `shared`。
`shared` 需要部署方确保所有参与存储查询和回温的 rank 访问同一共享存储命名空间；类型配置不会自动创建或检查共享挂载。

与 MemCache 集成时，LocalService 在 UBS IO 初始化成功后调用
[`UbsioGetInitInfo`](ubsio-kv/api_reference.md#ubsiogetinitinfo) 获取文件系统类型、slot 和 NDS 状态。
文件系统类型为 `UBSIO_UNDERFS_SHARED` 时使用分离模式，其余类型及查询失败时使用聚合模式。
LocalService 在注册和重连时向 MetaService 上报分离标志。
MetaService 由首次成功的存储注册确定模式，在当前 MetaManager 生命周期内保持固定；未启用存储的客户端
不参与模式确定，模式不一致的存储 rank 返回 `MMC_INVALID_PARAM` 并拒绝注册。MetaService 和 LocalService 使用同版本的注册协议。

### `ubsio.underfs.root_path`

`ubsio.underfs.root_path` 指定 `local` 或 `shared` 文件后端的 KV 根目录。目录必须在 UBS IO 启动前创建，
运行用户需要具备读写权限。

该项内置默认值为 `/mnt/kv`，随附的 `ubsio-boostio/configs/ubsio.conf` 将其设置为空。启用 `local` 或 `shared` 时，
应在运行时配置中显式指定有效目录。使用 `shared` 时，该目录需指向所有相关存储 rank 共同访问的共享存储命名空间。

### `ubsio.batch_read.standalone.use_scratch_pool`

`ubsio.batch_read.standalone.use_scratch_pool` 控制 standalone BatchGet 经 SSD、本地或共享文件系统读取时，是否先将
数据读入 scratch pool，再复制到上层 buffer。

- `true`：先读入 scratch pool，再复制到上层 buffer。A3 场景以及上层 buffer 不能直接用于存储 I/O 的场景
  必须使用该模式。
- `false`：直接将数据读入上层 buffer，可减少一次内存复制，仅适用于上层 buffer 支持直接存储 I/O 的场景。

```ini
ubsio.batch_read.standalone.use_scratch_pool = false
```

## 推荐检查项

- `ubsio.disk.path` 指向的整盘、分区或 loop 设备应由 UBS IO 独占，不能存在挂载点或需要保留的数据；
  standalone 模式最多配置 `16` 条路径，且不支持运行时动态加盘。
- UBS IO 运行用户需要对配置的块设备具有读写权限；全部本地 SSD 的总随机读带宽建议不低于 `7GB/s`。
- 使用块设备或 `local` 时，`ubsio.standalone.device_count` 应为 `1` 到 `16`，并与使用对应存储的
  `local_service` 进程数一致；纯内存或 `shared` 模式可配置为 `0`。
- `ubsio.mem.size_in_gb` 的单进程上限为
  `min(3072, floor(节点可供 UBS IO 使用的剩余内存 / 已启用 DRAM 的 local_service 进程数))`。
- 未配置 SSD 和 UnderFS 时，`ubsio.wcache.evict_water_level` 建议设置为较高水位，例如 `85`；仅使用 SSD
  或文件系统作为后端时设置为 `0`；同时使用 L2.5 和 SSD 时设置为大于 `0`。
- `ubsio.standalone.force_new_disk = true` 会使原缓存失效，只能用于无须保留数据的缓存设备。
- 仅使用 BDM 缓存盘时设置 `ubsio.underfs.file_system_type = none`。
- 内存直接下刷文件系统时将 `ubsio.disk.path` 留空；各进程按 slot 独立管理文件时选择 `local`，跨 rank 共享文件时选择 `shared`。
- 文件系统目录必须在 UBS IO 启动前创建并可读写。共享部署时，所有相关存储 rank 应完成共享目录挂载，
  并使用一致的文件访问权限和 UID 配置。
- A3 场景以及上层 buffer 不能直接用于文件系统或磁盘 I/O 的场景，必须保持
  `ubsio.batch_read.standalone.use_scratch_pool = true`。
- 如需使用 `bio_console` 和 CLI 诊断命令，设置 `ubsio.cli_tools.enable = true`。
