# UBSIO-MemStore 安全指南

本文档说明 UBSIO-MemStore 的通信端口、运行用户、文件权限及源码公网地址。

## 通信矩阵

| 源设备 | 源 IP 地址 | 源端口 | 目的设备 | 目的 IP 地址 | 目的端口（监听） | 协议 | 端口说明 | 监听端口是否可更改 | 认证方式 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 计算服务器 | 计算服务器 IP 地址 | 操作系统随机分配 | UBSIO-MemStore Server | Server 配置的 IP 地址 | 7201～7800 | RoCE | SDK 与 Server、Server 与 Server 之间的通信 | 是 | 证书双向认证 |
| 计算服务器 | 计算服务器 IP 地址 | 操作系统随机分配 | UBSIO-MemStore Server | Server 配置的 IP 地址 | 7201～7800 | RoCE | Server 节点间的组播通信 | 是 | 证书双向认证 |
| 计算服务器 | 计算服务器 IP 地址 | 操作系统随机分配 | ZooKeeper 节点 | ZooKeeper 配置的 IP 地址 | 用户配置 | TCP | UBSIO-MemStore 集群管理通信 | 是 | 由 ZooKeeper 配置决定 |

> **说明：**
>
> - ZooKeeper 为用户自行安装和维护的开源组件，安全配置请参见 [ZooKeeper 官方文档](https://zookeeper.apache.org/doc/current/zookeeperProgrammers.html#sc_Security)。
> - 请根据实际业务场景和安全要求配置通信链路。

## 运行用户建议

- 遵循最小权限原则，不建议使用 `root` 等管理员账户运行 UBSIO-MemStore。
- 集群各节点的运行用户、用户组及对应 UID、GID 应保持一致。

## 文件权限建议

建议在宿主机和容器中将 `umask` 设置为 `0027` 或更严格的值，使新增目录默认权限不超过 750、新增文件默认权限不超过 640。相关文件和目录的权限建议如下。

| 类型 | Linux 权限参考最大值 |
| --- | --- |
| 用户主目录 | 750（`rwxr-x---`） |
| 程序文件（含脚本和动态库） | 550（`r-xr-x---`） |
| 程序文件目录 | 550（`r-xr-x---`） |
| 配置文件 | 640（`rw-r-----`） |
| 配置文件目录 | 750（`rwxr-x---`） |
| 已归档日志文件 | 440（`r--r-----`） |
| 正在记录的日志文件 | 640（`rw-r-----`） |
| 日志文件目录 | 750（`rwxr-x---`） |
| Debug 文件 | 640（`rw-r-----`） |
| Debug 文件目录 | 750（`rwxr-x---`） |
| 临时文件目录 | 750（`rwxr-x---`） |
| 维护升级文件目录 | 770（`rwxrwx---`） |
| 业务数据文件 | 640（`rw-r-----`） |
| 业务数据文件目录 | 750（`rwxr-x---`） |
| 密钥组件、私钥、证书和密文文件目录 | 700（`rwx------`） |
| 密钥组件、私钥、证书和密文文件 | 600（`rw-------`） |
| 加解密接口和脚本 | 500（`r-x------`） |

## 公网地址声明

以下地址来自源码、构建配置或文档引用，仅用于获取开源依赖、许可证或安全配置说明。

| 类型 | 公网地址 | 文件 | 用途 |
| --- | --- | --- | --- |
| 依赖三方库 | [libboundscheck](https://gitcode.com/openeuler/libboundscheck.git) | `CMakeLists.txt` | 安全边界检查库 |
| 依赖三方库 | [ubs-comm](https://gitcode.com/openeuler/ubs-comm.git) | `CMakeLists.txt` | 通信库 |
| 依赖三方库 | [spdlog](https://gitcode.com/GitHub_Trending/sp/spdlog.git) | `CMakeLists.txt` | 日志框架 |
| 依赖三方库 | [ZooKeeper](https://gitcode.com/gh_mirrors/zo/zookeeper.git) | `CMakeLists.txt` | ZooKeeper 客户端依赖 |
| 许可证 | [木兰宽松许可证第 2 版](http://license.coscl.org.cn/MulanPSL2) | `LICENSE` 及源码文件头 | 木兰宽松许可证第 2 版 |
| 许可证 | [Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0) | 三方源码文件头 | Apache License 2.0 |
| 安全配置 | [ZooKeeper 安全配置](https://zookeeper.apache.org/doc/current/zookeeperProgrammers.html#sc_Security) | `memstore_security_guide.md` | ZooKeeper 安全配置参考 |
