# BoostIO 三方依赖声明

本文说明 UBSIO-BoostIO 构建、离线源码准备和运行库交付过程中涉及的主要三方组件。实际许可证、版权归属和再分发要求以对应上游项目随源码发布的 LICENSE、NOTICE 或包元数据为准。

## 当前推理交付路径

| 组件 | 用途 | 来源/版本 | 许可证说明 |
| --- | --- | --- | --- |
| libboundscheck | 安全函数依赖，构建并随 BoostIO 运行库交付。 | `https://gitcode.com/openeuler/libboundscheck.git`，`v1.1.16` | 以 openEuler libboundscheck 上游声明为准，通常随 Mulan PSL v2 发布。 |
| libaio | Linux 异步 I/O 依赖，构建并随 BoostIO 运行库交付。 | `https://github.com/deepin-community/libaio.git`，`0.3.113-8deepin2` | 以上游 libaio 包声明为准。 |
| spdlog | 日志库，作为 BoostIO 三方依赖构建。 | `https://gitcode.com/GitHub_Trending/sp/spdlog.git`，`v1.15.3` | 以上游 spdlog 许可证声明为准。 |
| ubs-comm | UB 通信相关依赖，作为 BoostIO 三方依赖构建。 | `https://gitcode.com/openeuler/ubs-comm.git`，`tag_BeiMing-I26.0.RC1.B013` | 以上游 ubs-comm 许可证声明为准。 |
| umdk | ubs-comm 离线构建所需依赖源码。 | `https://atomgit.com/openeuler/umdk.git`，`br_openEuler_24.03_LTS_SP3` | 以上游 umdk 许可证声明为准。 |

`ubsio-boostio/3rdparty/prepare_sources.sh` 会准备上述三方源码，并生成 `ubsio-boostio/dist/BoostIO_3rdparty_sources.tar.gz`。离线环境中可将该压缩包随工程代码一起带入目标环境。

## 头文件和可选依赖

| 组件 | 用途 | 说明 |
| --- | --- | --- |
| Apache ZooKeeper C client headers | 集群视图、Zookeeper client 相关构建入口。 | 头文件保留 Apache License 2.0 声明；如需构建 Zookeeper client 依赖，使用 `scripts/ubsio-boostio/build_zookeeper.sh`。 |
| Apache Hadoop HDFS header | UnderFS HDFS 后端接口声明。 | 头文件保留 Apache License 2.0 声明；630 商用版本主推单机模式，默认配置不启用 HDFS 后端。 |
| prometheus-cpp | Prometheus 观测能力的可选构建依赖。 | 当前推理交付路径不默认启用，离线源码准备脚本默认不抓取该依赖。 |

## 使用边界

- UBS IO 根目录许可证见 [LICENSE](LICENSE)。
- 三方组件的许可证、NOTICE、导出限制、安全公告和漏洞修复节奏由对应上游项目负责。
- 如果业务部署启用了 Ceph、HDFS、memcache、Mooncake、vLLM-Ascend 等外部组件，相关依赖、驱动、硬件和版本配套以对应项目官方文档为准。
