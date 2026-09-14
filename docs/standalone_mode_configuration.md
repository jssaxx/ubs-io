# UBS IO 单机模式配置参考

本文面向 630 商用版本的主推场景：单机推理三级池化。该场景下 UBS IO 作为 KV Cache 分层缓存体系中的 SSD 层，配合 memcache 或 Mooncake 接入 vLLM-Ascend，扩大本地 KV Cache 可承载容量并提升高复用请求命中率。

本文只说明推荐配置方式，不修改仓库中的现有配置文件。默认配置文件为 `ubsio-boostio/configs/ubsio.conf`；如需指定运行时配置，建议通过环境变量加载：

```bash
export UBSIO_CONFIG_PATH=/path/to/ubsio.conf
```

## 适用范围

- 单机模式推理服务，当前重点覆盖本地 SSD 作为 KV Cache 扩容层的场景。
- UBS IO KV 通过 BoostIO 后端提供标准 KV 接口，并可与 memcache、Mooncake 组合使用。
- 不要求 UBS IO 自身绑定特定硬件；与 memcache、Mooncake 或上层推理框架组合使用时，以对应项目官方文档为准。

## 最小本地单机模式示例

```ini
# BoostIO 配置
[ubsio]

# 设置缓存盘或分区路径。不填表示不使用磁盘设备，仅使用ubsio内存。
ubsio.disk.path =

# ubsio单个进程内存大小，单位GB。例如单节点剩余内存200GB，推理服务启动4卡，可以配置200/4=50GB。
ubsio.mem.size_in_gb = 50
# ubsio内存淘汰水位，示例为60%
ubsio.wcache.evict_water_level = 60
# ubsio磁盘淘汰水位，示例为90%
ubsio.wcache.disk_evict_water_level = 90

# 推理服务中开启二级池化，且配置 DRAM 不为 0 的 worker 进程数
ubsio.standalone.device_count = 0

# 设置日志输出级别。可选`error`、`warn`、`info`、`debug`、`trace`。
ubsio.log.level = info

# 以下配置无需修改
# 将全部内存缓存容量用于写缓存。
ubsio.cache.mem_read_write_ratio = 0:10
# 将全部磁盘缓存容量用于写缓存。
ubsio.cache.disk_read_write_ratio = 0:10
# 设置 UnderFS 后端：本地单机使用 none。需要后端访问时使用 ceph 或 hdfs。
ubsio.underfs.file_system_type = none
# 本地单机运行时禁用 TLS 配置路径校验。
ubsio.net.tls.enable.switch = false
ubsio.sdkmem.size_in_mb = 0
ubsio.cli_tools.enable = true
ubsio.cache.qos.enable = false
```

## 配置表

| 配置项 | 值类型 | 是否必填 | 默认值 | 有效范围 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `ubsio.disk.path` | 字符串 | 可选 | 空 | `device_count = 0` 时最多 `64` 个路径；`device_count > 0` 时最多 `16` 个块设备路径，以英文冒号分隔 | UBS IO 独占的整盘、分区或 loop 块设备；留空表示仅使用内存缓存。设备不能存在挂载点或需要保留的数据。 |
| `ubsio.log.level` | 字符串 | 可选 | `info` | `error`、`warn`、`info`、`debug`、`trace` | 配置初始化后设置 BoostIO server 日志级别。 |
| `ubsio.standalone.device_count` | 整数 | 可选 | `0` | `0` 到 `16` | 当前 vLLM 服务中的 memcache `local_server` 进程数量。配置为 `0` 时，每个 `deviceId` 按路径下标选择一个设备；配置为 `1` 到 `16` 时，全部进程共享所配置块设备的虚拟区域。 |
| `ubsio.standalone.device_id_gather_timeout_sec` | 整数 | 可选 | `180` | `1` 到 `2147483647` | `device_count > 0` 时，等待全部 standalone 逻辑 device ID 完成汇聚的超时时间，单位为秒。 |
| `ubsio.standalone.force_new_disk` | 布尔值 | 可选 | `false` | `true`、`false` | 是否在启动时将目标设备初始化为新缓存盘。设置为 `true` 后，设备中的原缓存数据失效。 |
| `ubsio.segment.size_in_mb` | 整数 | 可选 | `4` | `1` 到 `16` | 缓存 segment 大小。单机内存池 block、BDM chunk 和 SDK data-message block 都使用该大小。 |
| `ubsio.mem.size_in_gb` | 整数 | 可选 | `50` | `0` 到 `3072`；不得超过当前系统可用内存 | 单机 server 内存池容量。 |
| `ubsio.sdkmem.size_in_mb` | 整数 | 可选 | `0` | `0` 到 `4194304` | SDK data-message 内存池大小，主要用于 BatchGet 数据缓冲。 |
| `ubsio.net.tls.enable.switch` | 布尔值 | 可选 | `false` | `true` 或 `false` | 本地单机运行且没有 TLS 文件时，将该项设为 `false`。 |
| `ubsio.trace.enable` | 布尔值 | 可选 | `true` | `true` 或 `false` | 启用或禁用 HTrace 采集。单机模式会初始化 tracer 模块。 |
| `ubsio.data.crc.enable` | 布尔值 | 可选 | `false` | `true` 或 `false` | 启用缓存数据 CRC 校验。单机 direct-call 路径会把该设置传给 SDK 和 cache 模块。 |
| `ubsio.cache.qos.enable` | 布尔值 | 可选 | `false` | `true` 或 `false` | 启用 cache QoS 和过载控制行为。 |
| `ubsio.wcache.evict_water_level` | 整数 | 可选 | `0` | `0` 到 `100` | 写缓存内存淘汰水位。无盘模式下配置为 `0` 时，运行时水位按 `90` 处理。 |
| `ubsio.wcache.disk_evict_water_level` | 整数 | 可选 | `90` | `0` 到 `100` | 写缓存磁盘淘汰水位，占配置的写缓存磁盘容量的百分比。 |
| `ubsio.rcache.evict_water_level` | 整数 | 可选 | `90` | `0` 到 `100` | 读缓存淘汰水位。内存和磁盘读缓存淘汰阈值使用同一个值。 |
| `ubsio.cache.mem_read_write_ratio` | 字符串 | 可选 | `0:10` | 两个 `0` 到 `10` 的整数；总和必须为 `10` | 将 `ubsio.mem.size_in_gb` 划分给读缓存和写缓存。例如：`0:10` 表示把全部内存缓存容量预留给写缓存。 |
| `ubsio.cache.disk_read_write_ratio` | 字符串 | 可选 | `0:10` | 两个 `0` 到 `10` 的整数；总和必须为 `10` | 将缓存盘容量划分给读缓存和写缓存。例如：`0:10` 表示把全部磁盘缓存容量预留给写缓存。 |
| `ubsio.bdm.io_engine` | 字符串 | 可选 | `sync` | `sync`、`io_uring` | BDM I/O 引擎。 |
| `ubsio.bdm.batch_read.window_keys` | 整数 | 可选 | `128` | `1` 到 `1024` | BatchGet 经 BDM 读盘时，单个窗口的 key 数上限。 |
| `ubsio.bdm.batch_read.window_bytes_mb` | 整数 | 可选 | `64` | `1` 到 `1024` | BatchGet 经 BDM 读盘时，单个窗口的字节数上限，单位 MB。 |
| `ubsio.bdm.batch_read.pipeline_depth` | 整数 | 可选 | `4` | `1` 到 `64` | BatchGet 经 BDM 读盘时允许同时在途的窗口数。 |
| `ubsio.bdm.batch_read.temp_pool_mb` | 整数 | 可选 | `0` | `0` 到 `65535` | BDM 批量读独立临时缓冲池大小，单位 MB；`0` 表示按需申请。 |
| `ubsio.bdm.batch_read.standalone.use_scratch_pool` | 布尔值 | 可选 | `true` | `true`、`false` | standalone BatchGet 是否先读入 scratch pool；A3 场景必须开启，其他场景建议关闭以减少一次内存复制。 |
| `ubsio.work.io.timeout` | 整数 | 可选 | `60` | `60` 到 `300` | 通过单机 runtime config 传给 SDK 的 I/O 超时时间，单位为秒。 |
| `ubsio.batchget.thread.num` | 整数 | 可选 | `32` | `8` 到 `512` | MirrorServer BatchGet executor 线程数。 |
| `ubsio.cli_tools.enable` | 布尔值 | 可选 | `false` | `true` 或 `false` | 启用 CLI agent 诊断能力。使用 `cli_client --attach` 连接 `bio_console` 时应设为 `true`。 |
| `ubsio.underfs.file_system_type` | 字符串 | 可选 | `none` | `ceph`、`hdfs` 或 `none` | UnderFS 后端类型。单机模式使用 `none`。需要 load 或 write-through 后端访问时使用 `ceph` 或 `hdfs`。 |
| `ubsio.underfs.ceph.cfg.path` | 字符串 | 可选 | `/etc/ceph/ceph.conf` | 当 `ubsio.underfs.file_system_type = ceph` 时必须是已存在路径 | Ceph 配置文件路径。当 UnderFS 类型为 `ceph` 且该路径无效时，配置初始化失败。 |
| `ubsio.underfs.ceph.cluster` | 字符串 | 可选 | `ceph` | 非空字符串 | 后端为 Ceph 时，UnderFS 插件使用的 Ceph 集群名。 |
| `ubsio.underfs.ceph.user` | 字符串 | 可选 | `client.admin` | 非空字符串 | 后端为 Ceph 时，UnderFS 插件使用的 Ceph 用户。 |
| `ubsio.underfs.ceph.pool` | 字符串 | 可选 | `0:jfspool1,1:jfspool2` | 使用逗号分隔的 `poolId:poolName` 条目，poolId 为非负整数 | Ceph pool 映射。当前 UnderFS helper 使用 pool id `0` 初始化插件。 |

## 推荐检查项

- `ubsio.disk.path` 指向的磁盘或分区应由 UBS IO 独占使用，避免与文件系统或其他服务混用。
- 未配置缓存盘时，`ubsio.wcache.evict_water_level` 建议设置为 `100`；配置缓存盘后，可根据容量和淘汰策略调整内存与磁盘水位。
- 单机模式建议设置 `ubsio.underfs.file_system_type = none`，除非明确需要接入 Ceph 或 HDFS 后端。
- 如需使用 `bio_console` 和 CLI 诊断命令，设置 `ubsio.cli_tools.enable = true`。
