# UBSIO-MemStore 配置说明

本文档介绍 UBSIO-MemStore 的配置项及 TLS 认证配置。安装、启动和卸载流程请参见[安装部署指南](memstore_deployment_guide.md)。

## `mms.conf` 配置项

UBSIO-MemStore 默认配置文件为 `/etc/mms/mms.conf`。修改配置前请备份原文件，并根据实际 CPU、NUMA、网络和集群规划调整示例值。需要在集群各节点保持一致的配置项，应在启动服务前完成同步。

| 归属模块 | 配置项 | 说明 | 默认值/示例值 | 合法值或要求 |
| --- | --- | --- | --- | --- |
| Log | `mms.log.level` | 日志级别 | `info` | `error`、`warn`、`info`、`debug`、`trace` |
| Trace | `mms.trace.switch` | 统计日志开关 | `false` | `true`、`false` |
| CRC | `mms.crc.switch` | 数据完整性校验开关 | `false` | `true`、`false` |
| Sequence | `mms.sequence.switch` | 消息序列化收发开关 | `false` | `true`、`false` |
| Multicast | `mms.multicast.switch` | 组播开关 | `true` | `true`、`false` |
| Data change callback | `mms.data.change.callback.switch` | 数据变更通知开关；关闭后不建立通知链路，也不触发回调 | `false` | `true`、`false` |
| Notify SHM | `mms.notify.shm.queue.depth` | 共享内存通知队列深度 | `65536` | 1024～1048576 范围内的 2 的幂，且除以 worker 数后不小于 1024 |
| Notify SHM | `mms.notify.shm.worker.num` | 共享内存通知 worker 数 | `1` | 1～16 |
| Notify SHM | `mms.notify.shm.worker.cpuset` | 共享内存通知 worker 绑核范围 | `54-54` | 有效的 `x-y` CPU 范围，包含的 CPU 数不少于 worker 数 |
| Notify SHM | `mms.notify.shm.busy_polling` | 共享内存通知 worker 是否使用 busy polling | `true` | `true`、`false` |
| CRB | `mms.crb.send.cpuset` | CRB 故障恢复发送线程绑核范围 | `54-55` | 有效的 `x-y` CPU 范围；多节点场景生效 |
| Deployment | `mms.deployment.mode` | 部署模式 | `separate` | `separate`、`converge` |
| Memory | `mms.mem.numa.id` | 分配内存的 NUMA ID 列表 | `0,1` | 机器上有效的 NUMA ID，多个值用英文逗号分隔 |
| Memory | `mms.mem.numa.size` | 每个 NUMA 节点分配的内存大小，单位 GB | `8,8` | 与 `mms.mem.numa.id` 一一对应，不超过对应 NUMA 的可用内存 |
| Memory | `mms.mem.value.unit.size` | Value 内存分配单元大小，单位 KB | `1` | 1～64 |
| Network | `mms.net.rpc.ip_mask` | RPC 网卡 IPv4 地址和掩码 | `192.168.100.100/24` | IPv4 CIDR 格式 |
| Network | `mms.net.rpc.listen_port` | RPC 监听端口 | `7500` | 7201～7800 |
| Network | `mms.net.multicast.listen_port` | 组播监听端口 | `7501` | 7201～7800 |
| Network | `mms.net.rpc.protocol` | RPC 通信协议 | `rdma` | `tcp`、`rdma` |
| Network | `mms.net.multicast.protocol` | 组播通信协议 | `rdma` | `tcp`、`rdma`；UB 通信场景配置为 `tcp` |
| Network | `mms.net.rpc.connect.count` | 每个 RPC channel 的连接数 | `1` | 1～16 |
| Network | `mms.net.rpc.busy_polling_mode` | RPC worker 是否使用 busy polling | `true` | `true`、`false` |
| Network | `mms.net.rpc.worker.groups` | RPC worker group 配置 | `1,1` | 多个 group 用英文逗号分隔，每个值为对应 group 的 worker 数 |
| Network | `mms.net.rpc.worker.groups.cpuset` | RPC worker 绑核配置 | `10-10,50-50` | 与 RPC worker group 一一对应，每段使用 `x-y` 格式 |
| Network | `mms.net.ipc.busy_polling_mode` | IPC worker 是否使用 busy polling | `true` | `true`、`false` |
| Network | `mms.net.ipc.worker.groups` | IPC worker group 配置 | `1,1` | 多个 group 用英文逗号分隔，每个值为对应 group 的 worker 数 |
| Network | `mms.net.ipc.worker.groups.cpuset` | IPC worker 绑核配置 | `12-12,52-52` | 与 IPC worker group 一一对应，每段使用 `x-y` 格式 |
| Network | `mms.net.request.executor.thread.num` | 网络请求处理线程数 | `8` | 8～256 |
| Network | `mms.net.request.executor.queue.size` | 网络请求处理队列大小 | `1024` | 1024～65535 |
| Network | `mms.net.publisher.worker.cpuset` | 组播 publisher worker 绑核配置 | `10-17` | 有效的 CPU 范围；可按 group 使用英文逗号分段 |
| Network | `mms.net.subscriber.worker.cpuset` | 组播 subscriber worker 绑核配置 | `18-18` | 有效的 `x-y` CPU 范围 |
| Network | `mms.net.subscriber.connect.count` | 每个 subscriber 的连接数 | `1` | 1～16 |
| Network | `mms.net.message.max_buff_size` | 单次消息的最大 buffer 大小，单位 KB | `70` | 1～4096，建议不超过 256 |
| TLS | `mms.net.tls.enable` | Server 端 TLS 开关 | `true` | `true`、`false` |
| TLS | `mms.net.tls.certification.path` | Server 端证书路径 | 空 | TLS 开启时配置真实有效的文件路径 |
| TLS | `mms.net.tls.ca.cert.path` | CA 证书路径 | 空 | TLS 开启时配置真实有效的文件路径 |
| TLS | `mms.net.tls.ca.crl.path` | 证书吊销列表路径 | 空 | 可为空；非空时配置真实有效的文件路径 |
| TLS | `mms.net.tls.private.key.path` | Server 端私钥路径 | 空 | TLS 开启时配置真实有效的文件路径 |
| TLS | `mms.net.tls.private.key.password.path` | Server 端私钥口令密文文件路径 | 空 | TLS 开启时配置真实有效的文件路径 |
| TLS | `mms.net.tls.decrypter.lib.path` | 安全解密函数动态库路径 | 空 | TLS 开启时配置真实有效的文件路径 |
| TLS | `mms.net.tls.openssl.lib.path` | OpenSSL 动态库目录 | 空 | 为空时使用系统路径；非空时目录必须有效 |
| Cluster manager | `mms.cm.node.num` | 集群节点数 | `3` | 1～8 |
| Cluster manager | `mms.cm.node.id` | 集群节点 ID | 未配置 | 0～65535；未配置时由集群自动分配 |
| Cluster manager | `mms.cm.register_timeout_sec` | 集群注册超时时间，单位秒 | `10` | 10～60 |
| Cluster manager | `mms.cm.zk_host` | ZooKeeper 服务地址 | `192.168.100.100:2181` | `IPv4:端口` 格式，多个地址按 ZooKeeper 连接串格式配置 |

## TLS 认证配置

### 配置 Server 端 TLS 认证

#### 注意事项

- 开启 TLS 时，UBSIO-MemStore 集群中的所有计算节点必须同时开启 TLS。
- 生成加密口令前，建议暂时关闭命令历史记录；生成完成后再恢复。
- 私钥必须加密，私钥口令应使用安全工具生成密文文件，不得在配置文件或命令行中填写明文口令。
- 证书应使用业界认可的算法、密钥长度、Hash 算法和证书格式，并处于有效期内。

#### 前提条件

UBSIO-MemStore 已安装完成，并已准备[表 1](#server端tls文件)所列文件。

<a id="server端tls文件"></a>
**表 1 Server 端 TLS 认证所需文件**

| 文件 | 说明 | 对应配置项 |
| --- | --- | --- |
| CA 证书 | 用于签发其他证书的 CA 证书，PEM 格式 | `mms.net.tls.ca.cert.path` |
| 证书吊销列表 | PEM CRL 格式；无吊销证书时可不提供 | `mms.net.tls.ca.crl.path` |
| Server 端证书 | 由 CA 签发且处于有效期内的 PEM chain 文件 | `mms.net.tls.certification.path` |
| Server 端加密私钥 | 与 Server 端证书匹配的 PEM encrypted 文件 | `mms.net.tls.private.key.path` |
| 私钥口令密文文件 | 加密后的私钥口令存储文件，长度不超过 10000 字节 | `mms.net.tls.private.key.password.path` |
| 安全解密函数动态库 | 可选；配置后使用用户提供的实现 | `mms.net.tls.decrypter.lib.path` |
| OpenSSL 动态库 | 可选；配置后使用用户指定的版本 | `mms.net.tls.openssl.lib.path` |

将 `mms.net.tls.enable` 配置为 `true`，并填写对应文件路径。所有节点的 TLS 开关必须保持一致。

### 配置 Client 端 TLS 认证

#### 注意事项

- 本节仅适用于分离部署模式。
- Client 端与所有 Server 端必须同时开启或关闭 TLS，否则连接失败。
- 多个用户访问 UBSIO-MemStore 时可以使用不同证书，但证书必须由同一个 CA 签发。

#### 前提条件

UBSIO-MemStore 已安装完成，并已准备[表 2](#client端tls文件)所列文件。Client 端 TLS 参数通过 [`MmsOptions`](memstore_api_reference.md#mmsoptions) 传入。

<a id="client端tls文件"></a>
**表 2 Client 端 TLS 认证所需文件**

| 文件 | 说明 | `MmsOptions` 对应字段 |
| --- | --- | --- |
| CA 证书 | 用于签发其他证书的 CA 证书，PEM 格式 | `caCerPath` |
| 证书吊销列表 | PEM CRL 格式；无吊销证书时可不提供 | `caCrlPath` |
| Client 端证书 | 由 CA 签发且处于有效期内的 PEM chain 文件 | `certificationPath` |
| Client 端加密私钥 | 与 Client 端证书匹配的 PEM encrypted 文件 | `privateKeyPath` |
| 私钥口令密文文件 | 加密后的私钥口令存储文件，长度不超过 10000 字节 | `privateKeyPasswordPath` |
| 安全解密函数动态库 | 可选；配置后使用用户提供的实现 | `decrypterLibPath` |
| OpenSSL 动态库目录 | 可选；配置后使用用户指定的版本 | `opensslLibDir` |
