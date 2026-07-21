# ubsio-common(cli) 命令说明

本文档面向 CLI 使用者，说明 `cli_server`、`cli_client` 以及当前注册到 CLI 框架中的 BoostIO 诊断命令。CLI 使用方式见 [UBSIO-BoostIO CLI 使用指导](../ubsio-boostio/cli.md)，命令实现见 [cli_server.c](../../ubsio-common/cli/src/server/cli_server.c)、[cli_client.c](../../ubsio-common/cli/src/client/cli_client.c)、[sdk_diagnose.cpp](../../ubsio-boostio/test/tools/diagnose/cli_sdk/sdk_diagnose.cpp) 和 [server_diagnose.cpp](../../ubsio-boostio/test/tools/diagnose/cli_server/server_diagnose.cpp)。

## 启动命令

### cli_server

```bash
cli_server [--server-port=<port>]
cli_server [-p <port>]
```

| 参数 | 类型/取值范围 | 作用 |
| --- | --- | --- |
| `--server-port=<port>` | `uint16_t`；`1-65535` | 指定 CLI server 监听端口，默认 `7002`；非法值会回退默认端口。 |
| `-p <port>` | `uint16_t`；`1-65535` | `--server-port` 的短参数形式。 |
| `--help` / `-h` | 无 | 打印 `cli_server` 帮助信息后退出。 |

### cli_client

```bash
cli_client [options]
```

| 参数 | 类型/取值范围 | 作用 |
| --- | --- | --- |
| `--help` / `-h` | 无 | 打印 `cli_client` 帮助信息后退出。 |
| `--auto` | 无 | 自动运行模式开关。 |
| `--script=<filename>` | 文件路径 | 从脚本文件读取命令并顺序执行。 |
| `--server-port=<port>` | `uint16_t`；`1-65535` | 指定连接的 CLI server 端口，默认 `7002`；非法值会回退默认端口。 |
| `--log-file=<filename>` | 文件路径 | 将接收到的输出追加写入指定日志文件。 |
| `--set-debug` | 无 | 兼容参数，当前不改变执行逻辑。 |
| `--set-cli` | 无 | 兼容参数，当前不改变执行逻辑。 |
| `--no-prompt` | 无 | 不显示交互式命令提示符。 |
| `--attach=<AppId>` | `uint32_t`；`0-4294967295` | 登录后自动 attach 到指定 AppId；`0` 表示不 attach。 |

### bio_console

```bash
bio_console <mode>
```

| 参数 | 类型/取值范围 | 作用 |
| --- | --- | --- |
| `mode` | `0` 或 `1` | `0` 表示融合部署模式，`1` 表示分离部署模式。 |

`bio_console` 用于启动 BoostIO console 进程。开启 CLI 诊断后，`bio_console` 进程会通过 CLI agent 注册诊断入口，随后可用 `cli_client` attach 到对应 agent 执行 `sdk` 或 `bioServer` 命令。

使用流程：

1. 在 BoostIO 配置文件中开启 CLI 诊断能力：

   ```conf
   ubsio.cli_tools.enable = true
   ```

2. 启动 CLI server。`bio_console` 的 agent 当前按默认端口 `7002` 连接 CLI server，建议此场景使用默认端口。

   ```bash
   ./cli_server &
   ```

3. 启动 `bio_console`。

   ```bash
   ./bio_console 0
   # 或
   ./bio_console 1
   ```

4. 启动 `cli_client`，查看并 attach 到 `bio_console` 注册出来的 agent。

   ```bash
   ./cli_client
   ls
   attach <AppId>
   help
   ```

   `ls` 中通常会看到 `bio_sdk`、`bio_server` 等 agent 名称，实际可见项取决于部署模式、配置和诊断库是否加载成功。也可以通过 `cli_client --attach=<AppId>` 启动后直接 attach。

## 客户端内置命令

这些命令在 `cli_client` 交互界面中执行，不依赖具体 agent。

| 命令 | 作用 | 参数说明 |
| --- | --- | --- |
| `ls` | 列出已注册到 CLI server 的 agent。 | 无参数；输出 `AppId`、状态和应用名。 |
| `attach <AppId>` | attach 到指定 agent。 | `AppId` 为 `ls` 输出的无符号整数；`attach 0` 表示 detach。 |
| `help` | 显示帮助信息。 | 未 attach 时显示 server 内置命令；attach 后转发给 agent 显示 agent 命令。 |
| `.script <filename>` | 在交互模式中执行脚本文件。 | `filename` 为本地可读文件路径，文件中每行一条 CLI 命令。 |
| `exit` / `quit` | 退出 `cli_client`。 | 无参数。 |

## sdk 诊断命令

执行前需要先通过 `attach <AppId>` attach 到包含 `sdk` 命令的 agent。`sdk` 命令名区分大小写。

| 命令 | 作用 | 参数说明 |
| --- | --- | --- |
| `sdk list` | 列出当前可用 cache。 | 无参数。 |
| `sdk create <tenantId> <affinity> <strategy>` | 创建 cache，并把当前 CLI 会话切到该 tenant。 | `tenantId`：`uint32_t`；`affinity`：`1` 表示 `LOCAL_AFFINITY`，`2` 表示 `GLOBAL_BALANCE`；`strategy`：`1` 表示 `WRITE_BACK`，`2` 表示 `WRITE_THROUGH`。 |
| `sdk open <tenantId>` | 打开已有 cache，并把当前 CLI 会话切到该 tenant。 | `tenantId`：`uint32_t`。 |
| `sdk destroy <tenantId>` | 销毁指定 tenant 的 cache。 | `tenantId`：`uint32_t`。 |
| `sdk put <key> <filePath> <length> <sliceId>` | 从本地文件读取数据并写入 cache。 | 需要先 `create` 或 `open`；`key`：字符串，长度 `1-255`；`filePath`：可读文件路径；`length`：`uint64_t` 字节数；`sliceId`：`uint32_t`，用于计算对象位置。 |
| `sdk get <key> <offset> <length> <location> <filePath>` | 从 cache 读取数据并写入本地文件。 | 需要先 `create` 或 `open`；`key`：字符串，长度 `1-255`；`offset`/`length`/`location`：`uint64_t`；`filePath`：输出文件路径。 |
| `sdk stat <key> <location>` | 查询对象元信息。 | 需要先 `create` 或 `open`；`key`：字符串，长度 `1-255`；`location`：`uint64_t`。 |
| `sdk exist <key> <location>` | 查询对象是否存在。 | 需要先 `create` 或 `open`；`key`：字符串，长度 `1-255`；`location`：`uint64_t`。 |
| `sdk listall <prefix>` | 按前缀列举对象。 | 需要先 `create` 或 `open`；`prefix`：字符串前缀。 |
| `sdk load <key> <offset> <length> <location>` | 触发对象异步加载。 | 需要先 `create` 或 `open`；`key`：字符串，长度 `1-255`；`offset`/`length`/`location`：`uint64_t`。 |
| `sdk delete <key> <location>` | 删除对象。 | 需要先 `create` 或 `open`；`key`：字符串，长度 `1-255`；`location`：`uint64_t`。 |
| `sdk show pt all` | 显示完整 PT 视图。 | 固定参数 `pt all`。 |
| `sdk show pt affinity` | 显示本地亲和 PT 列表。 | 固定参数 `pt affinity`。 |
| `sdk show node` | 显示节点视图和本地节点信息。 | 固定参数 `node`。 |
| `sdk show flow <ptId>` | 显示指定 PT 对应 flow 信息。 | `ptId`：`uint16_t` 或可转换为 `uint16_t` 的无符号整数。 |
| `sdk trace <trace_action>` | 查看、清空、开启或关闭 SDK trace 统计。 | `trace_action` 只能取 `show`、`clear`、`open`、`close`。 |
| `sdk cachehit` | 显示整体和各节点缓存命中率。 | 需要先 `create` 或 `open`；无其他参数。 |
| `sdk cacheresource` | 显示各节点 cache 资源容量和使用量。 | 需要先 `create` 或 `open`；无其他参数。 |
| `sdk adddisk <diskPath>` | 向 BoostIO 增加缓存盘。 | `diskPath`：缓存盘或分区路径。 |
| `sdk perf <rw> <bs(Kb)> <ioDepth> <size(Mb)>` | 执行读写诊断压测。 | 需要先 `create` 或 `open`；`rw`：`read` 或 `write`；`bs(Kb)`：无符号整数且 `> 0`；`ioDepth`：无符号整数，建议 `> 0`；`size(Mb)`：无符号整数，建议 `> 0`。 |
| `sdk batchget <bs(Kb)> <batchNUM>` | 写入一组对象后执行批量读取校验。 | 需要先 `create` 或 `open`；`bs(Kb)`：无符号整数；`batchNUM`：无符号整数，表示批量 key 数。 |
| `sdk notifyupdate <tenantId>` | 通知指定 tenant 进入升级准备。 | `tenantId`：`uint32_t`。 |
| `sdk checkupdate <tenantId>` | 检查指定 tenant 是否可升级。 | `tenantId`：`uint32_t`。 |
| `sdk finishupdate <tenantId>` | 通知指定 tenant 升级完成。 | `tenantId`：`uint32_t`。 |
| `sdk exit` | 返回当前命令处理流程。 | 无参数；退出 `cli_client` 请使用客户端内置 `exit` 或 `quit`。 |

## bioServer 诊断命令

执行前需要先通过 `attach <AppId>` attach 到包含 `bioServer` 命令的 agent。`bioServer` 以及 `RCachePut`、`RCacheGet`、`RCacheDelete` 区分大小写。

| 命令 | 作用 | 参数说明 |
| --- | --- | --- |
| `bioServer chgwlv <tier> <water_level>` | 修改 WCache 淘汰水位。 | `tier`：`0` 表示内存层，`1` 表示磁盘层；`water_level`：`uint64_t`，范围 `0-100`。 |
| `bioServer chgmr <memory_ratio>` | 修改内存读写比例。 | `memory_ratio`：形如 `x:y`，`x` 和 `y` 均为 `0-10`，且 `x + y = 10`。 |
| `bioServer chgdr <disk_ratio>` | 修改磁盘读写比例。 | `disk_ratio`：形如 `x:y`，`x` 和 `y` 均为 `0-10`，且 `x + y = 10`。 |
| `bioServer show disk` | 显示缓存盘状态和容量。 | 固定参数 `disk`。 |
| `bioServer show net` | 显示 BoostIO RPC 网络信息。 | 固定参数 `net`。 |
| `bioServer show resources` | 显示 WCache/RCache 资源容量和使用量。 | 固定参数 `resources`。 |
| `bioServer show pt` | 显示服务端 PT 视图。 | 固定参数 `pt`。 |
| `bioServer show node` | 显示服务端节点视图和本地节点信息。 | 固定参数 `node`。 |
| `bioServer show olc` | 显示过载控制信息。 | 固定参数 `olc`。 |
| `bioServer show existHit` | 显示 exist 查询次数和命中率。 | 固定参数 `existHit`。 |
| `bioServer trace <trace_action>` | 查看、清空、开启或关闭 server trace 统计。 | `trace_action` 只能取 `show`、`clear`、`open`、`close`。 |
| `bioServer RCachePut <key> <filePath> <ptId> <length>` | 从本地文件读取数据并写入 RCache。 | `key`：字符串，长度 `1-255`；`filePath`：可读文件路径；`ptId`：无符号整数，需小于当前 PT 数；`length`：`uint64_t` 字节数。 |
| `bioServer RCacheGet <key> <ptId> <offset> <length> <filePath>` | 从 RCache 读取数据并写入本地文件。 | `key`：字符串，长度 `1-255`；`ptId`/`offset`/`length`：无符号整数；`filePath`：输出文件路径。 |
| `bioServer RCacheDelete <ptId> <key>` | 删除 RCache 中指定 key。 | `ptId`：无符号整数；`key`：字符串，长度 `1-255`。 |
| `bioServer exit` | 返回当前命令处理流程。 | 无参数；退出 `cli_client` 请使用客户端内置 `exit` 或 `quit`。 |
