# UBSIO-BoostIO 三方依赖声明

本文说明 UBSIO-BoostIO 构建、离线源码准备和运行库交付过程中涉及的主要三方组件。实际许可证、版权归属和再分发要求以对应上游项目随源码发布的 LICENSE、NOTICE 或包元数据为准。

本清单与 `ubsio-boostio/3rdparty` 下的 CMake 配置及 `prepare_sources.sh` 保持一致。

## 默认构建与离线源码

| 组件 | 用途 | 来源/版本 | 许可证说明 |
| --- | --- | --- | --- |
| libboundscheck | 安全函数依赖；优先使用系统安装，缺失时从源码构建。 | `https://gitcode.com/openeuler/libboundscheck.git`，`v1.1.16` | 以 openEuler libboundscheck 上游声明为准，通常随 Mulan PSL v2 发布。 |
| liburing | Linux `io_uring` 异步 I/O 依赖；优先使用系统安装，缺失时从源码构建。 | `https://gitcode.com/gh_mirrors/li/liburing.git`，`liburing-2.6` | 以上游 liburing 的 LICENSE、COPYING 声明为准。 |
| spdlog | 日志库，作为 BoostIO 三方依赖构建。 | `https://gitcode.com/GitHub_Trending/sp/spdlog.git`，`v1.15.3` | 以上游 spdlog 许可证声明为准。 |
| ubs-comm | UB 通信相关依赖；优先使用系统 HCOM，缺失时从源码构建。 | `https://gitcode.com/openeuler/ubs-comm.git`，`tag_BeiMing-I26.0.RC1.B013` | 以上游 ubs-comm 许可证声明为准。 |
| umdk | ubs-comm 离线构建所需依赖源码。 | `https://atomgit.com/openeuler/umdk.git`，`br_openEuler_24.03_LTS_SP3` | 以上游 umdk 许可证声明为准。 |

`ubsio-boostio/3rdparty/prepare_sources.sh` 会准备 libboundscheck、liburing、spdlog 和 ubs-comm，并为 ubs-comm 的离线构建额外准备 libboundscheck 和 umdk。脚本会将四个顶层依赖源码打包为 `ubsio-boostio/dist/BoostIO_3rdparty_sources.tar.gz`，供离线环境随工程代码一起带入目标环境。

## 头文件和可选依赖

| 组件 | 用途 | 说明 |
| --- | --- | --- |
| Apache ZooKeeper C client headers | 集群视图、Zookeeper client 相关构建入口。 | 头文件保留 Apache License 2.0 声明；如需构建 Zookeeper client 依赖，使用 `scripts/ubsio-boostio/build_zookeeper.sh`。 |
| Apache Hadoop HDFS header | UnderFS HDFS 后端接口声明。 | 头文件保留 Apache License 2.0 声明；630 商用版本主推单机模式，默认配置不启用 HDFS 后端。 |
| prometheus-cpp | Prometheus 观测能力的可选构建依赖。 | 使用 `v1.2.4`；仅在 `OPEN_PROMETHEUS=ON` 时构建，离线源码准备脚本默认不抓取该依赖。 |

## 使用边界

- UBS IO 根目录许可证见 [LICENSE](LICENSE)。
- 三方组件的许可证、NOTICE、导出限制、安全公告和漏洞修复节奏由对应上游项目负责。
- 如果业务部署启用了 Ceph、HDFS、memcache、Mooncake、vLLM-Ascend 等外部组件，相关依赖、驱动、硬件和版本配套以对应项目官方文档为准。
