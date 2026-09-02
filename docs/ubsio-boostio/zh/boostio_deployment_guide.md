# UBSIO-BoostIO 安装部署指南

本文档介绍 UBSIO-BoostIO 的环境要求、安装前清理、软件安装、启动、卸载和升级流程。产品特性和故障处理请参见[用户指南](boostio_user_guide.md)，配置项及 TLS 认证操作请参见[配置说明](boostio_configuration_guide.md)。

## 软件安装

### 环境要求

安装部署 UBSIO-BoostIO 前，请检查物理环境、依赖软件及其版本是否满足要求。满足前置环境要求是成功安装部署和正常运行应用程序的先决条件。

**硬件要求**

UBSIO-BoostIO 安装在计算节点上，集群中各计算节点的硬件要求如[表 1](#硬件配套要求)所示。

<a id="硬件配套要求"></a>
**表 1 硬件配套要求**

| 硬件名称 | 配套关系 |
| --- | --- |
| 服务器 | TaiShan 200 服务器 |
| 处理器 | 鲲鹏 920 处理器或鲲鹏 950 处理器 |
| 内存大小 | 512 GB |
| 内存频率 | 2666 MHz |
| 网卡 | RoCE 100 GE；TCP 10 GE |
| 硬盘（NVMe SSD） | 至少一块 3.6 TB 或 7.68 TB 磁盘 |

**软件要求**

安装 UBSIO-BoostIO 前，需要先安装依赖软件。建议遵循各软件的安全标准进行安装。集群中各节点的软件版本要求如[表 2](#软件要求)所示，以下软件不在交付范围内。

<a id="软件要求"></a>
**表 2 软件要求**

| 软件名称 | 软件版本 |
| --- | --- |
| OS | openEuler 24.03 LTS SP4 |
| JuiceFS | 1.0.2 |
| Redis | 4.0.11 |
| ZooKeeper | 3.9.5 |
| Ceph | 12.2.8 |
| Python | 3.7 |

**运行依赖包**

UBSIO-BoostIO 的运行依赖包如[表 3](#运行依赖包)所示。使用 `yum` 在线安装 UBSIO-BoostIO 时，`yum` 会自动解析并安装这些依赖，无需逐个安装；使用 `rpm` 离线安装时，需要提前准备产品包及其依赖 RPM。

<a id="运行依赖包"></a>
**表 3 运行依赖包**

| RPM 包名 | 用途 |
| --- | --- |
| `ubs-comm-lib` | 提供 UBSIO-BoostIO 使用的 `libhcom.so` 通信库 |
| `libzookeeper-mt2` | 提供连接 ZooKeeper 所需的 `libzookeeper_mt.so.2` 客户端库 |
| `openssl-libs` | 提供 TLS 认证所需的 OpenSSL 运行库 |
| `libboundscheck` | 提供安全 C 库函数运行库 |
| `librados2` | 提供访问 Ceph 集群所需的 RADOS 客户端库 |

> 系统基础运行库由操作系统提供，不需要单独准备。

### 软件包说明

UBSIO-BoostIO 提供运行包和开发包，具体用途如[表 4](#软件包说明)所示。

<a id="软件包说明"></a>
**表 4 软件包说明**

| RPM 包名 | 用途 |
| --- | --- |
| `ubs-io-boostio` | 运行包，提供服务程序、运行库和配置文件；用于部署和运行 UBSIO-BoostIO |
| `ubs-io-boostio-devel` | 开发包，提供头文件、SDK 开发链接库和静态库；仅在编译或集成 UBSIO-BoostIO 应用时安装 |

### （可选）清理环境

> **须知：**
>
> - 重新安装前，请确保环境中不存在旧版本 UBSIO-BoostIO。
> - 建议及时清理 SDK 端不再使用的日志文件，避免磁盘空间耗尽。
> - SDK 端统计文件最大为 10 MB，Server 端统计文件最大为 50 MB。UBSIO-BoostIO 重新部署启动后会生成新的统计文件，建议及时清理旧文件。

1. 收集所有需要安装 UBSIO-BoostIO 的节点 IP 地址。
2. 在 ZooKeeper Server 节点上清理 UBSIO-BoostIO 集群信息。

    运行 ZooKeeper 客户端：

    ```bash
    sh /opt/zookeeper/bin/zkCli.sh
    ```

    连接 ZooKeeper 后执行：

    ```text
    deleteall /cm
    ```

3. 清理 UBSIO-BoostIO 磁盘管理元数据。

    > **危险：**
    >
    > 以下命令会覆盖目标设备起始位置的 8 MiB 数据。执行前必须确认设备路径无误、未挂载，并且未被 LVM、RAID 或 swap 使用。请勿对系统盘或业务数据盘执行该命令。

    将 `/dev/nvmeXnY` 替换为确认无误的目标设备：

    ```bash
    dd bs=8K count=1024 if=/dev/zero of=/dev/nvmeXnY
    ```

### 安装 UBSIO-BoostIO

1. 使用 `root` 用户登录服务器。
2. 选择以下任一方式安装 UBSIO-BoostIO。

    **在线安装**

    仅部署和运行 UBSIO-BoostIO 时，安装运行包：

    ```bash
    yum install ubs-io-boostio -y
    ```

    编译或集成 UBSIO-BoostIO 应用时，安装开发包：

    ```bash
    yum install ubs-io-boostio-devel -y
    ```

    `ubs-io-boostio-devel` 依赖同版本的运行包。使用 `yum` 安装开发包时，会自动安装匹配的 `ubs-io-boostio`，无需重复执行运行包安装命令。

    **离线安装**

    `rpm` 不会自动从软件源下载缺失的依赖。离线安装前，根据使用场景将[表 4](#软件包说明)中的产品 RPM 与[表 3](#运行依赖包)中的依赖 RPM 放在同一独立目录中。安装开发包时，还必须准备版本匹配的运行包。然后执行：

    ```bash
    rpm -ivh ./*.rpm
    ```

#### 安装目录

安装完成后的目录结构如[表 5](#软件包目录结构)所示。

<a id="软件包目录结构"></a>
**表 5 软件包目录结构**

| 目录 | 说明 |
| --- | --- |
| `/usr/bin` | 可执行文件 |
| `/usr/lib64` | 动态库和静态库文件 |
| `/etc/boostio` | 配置文件 |
| `/usr/include/boostio` | `ubs-io-boostio-devel` 提供的开发头文件 |

**表 6 bin 目录文件说明**

| 目录 | 文件名称 | 描述 |
| --- | --- | --- |
| `/usr/bin` | `bio_daemon` | UBSIO-BoostIO 服务可执行文件 |

**表 7 lib 目录文件说明**

| 目录 | 文件名称 | 描述 |
| --- | --- | --- |
| `/usr/lib64` | `libbio_interceptor_server.so` | 桥接服务共享对象文件 |
| `/usr/lib64` | `libbio_server.so` | UBSIO-BoostIO Server 端共享对象文件 |
| `/usr/lib64` | `libbio_sdk.so.1.0.0` | UBSIO-BoostIO SDK 端共享对象文件 |
| `/usr/lib64` | `libbio_sdk.so.1` | `libbio_sdk.so.1.0.0` 的软链接 |
| `/usr/lib64` | `libbio_sdk.so` | `ubs-io-boostio-devel` 提供的 SDK 开发链接库 |
| `/usr/lib64` | `libbio_sdk.a` | `ubs-io-boostio-devel` 提供的 SDK 静态库 |
| `/usr/lib64` | `libock_interceptor.so` | 桥接服务共享对象文件 |
| `/usr/lib64` | `libock_iofwd_proxy.so` | 桥接服务共享对象文件 |

#### 安装后配置

1. 根据业务场景配置 `/etc/boostio/bio.conf`。具体配置项和 TLS 认证操作请参见[配置说明](boostio_configuration_guide.md)。
2. 配置 Ceph Client 密钥环的读取权限。

    > **说明：**
    >
    > UBSIO-BoostIO 启动时需要读取 Ceph Client 端密钥。权限配置方法请参见 [Ceph 官方文档](https://docs.ceph.com/en/latest/rados/configuration/auth-config-ref/#keys)。

3. 按照相同流程在其他节点上安装并配置 UBSIO-BoostIO。

## 软件启动

### 启动前提条件

启动 UBSIO-BoostIO 前，请确认以下外部服务已经启动且运行正常，并确保所有 UBSIO-BoostIO 节点均可访问对应服务：

- JuiceFS 使用的 Redis 服务已经启动。
- UBSIO-BoostIO 集群管理使用的 ZooKeeper 服务已经启动，且服务地址与 `bio.conf` 中的 ZooKeeper 配置一致。
- 使用 Ceph 作为后端存储时，Ceph 集群已经启动且状态正常，目标存储池及客户端密钥可用。

### 配置 RDMA 无损网络

如果安装环境配置了 RoCE 网卡，并且 UBSIO-BoostIO 使用 RDMA 协议，需要先配置 RDMA 无损网络参数，避免数据通信过程中出现错误。具体配置方法请参见网卡厂商提供的 RDMA 使用说明。

### 启动 UBSIO-BoostIO

#### 融合部署模式

融合部署模式下，UBSIO-BoostIO 不存在独立运行进程，而是以动态链接库的方式加载到 JuiceFS 进程中。因此，使用 UBSIO-BoostIO 功能前需要启动 JuiceFS 进程。

#### 分离部署模式

- 每个物理节点有且仅有一个 `bio_daemon` 进程。请勿在同一节点启动多个 `bio_daemon` 实例，否则可能导致数据不一致。
- 分离部署模式下，UBSIO-BoostIO 与 JuiceFS 是独立运行的组件，必须先启动 `bio_daemon`，再启动 JuiceFS。
- JuiceFS 启动时会自动加载 UBSIO-BoostIO SDK 链接库，因此必须确保 `bio_daemon` 已经运行并就绪。
- 支持通过命令行在后台手动启动 `bio_daemon`，适用于应用开发、功能调试和需要自行监控进程状态的场景。

## 软件卸载

> **须知：**
>
> - 卸载 UBSIO-BoostIO 前，建议先安全删除所有密钥存储文件。
> - 建议删除安装 UBSIO-BoostIO 时创建的目录。

**操作步骤**

1. 登录 UBSIO-BoostIO 安装节点并执行：

    ```bash
    yum remove ubs-io-boostio-devel ubs-io-boostio
    ```

2. 在其他节点上依次执行相同操作。

## 软件升级

> **说明：**
>
> 当前版本仅支持离线升级。升级时需要停止应用业务，不支持在线升级。

UBSIO-BoostIO 提供升级准备、升级检查和升级完成三种操作，用于 JuiceFS 开发者集成端到端软件升级流程。

### 升级准备

执行升级准备操作后，UBSIO-BoostIO 会打开写透模式并关闭分布式缓存功能，此时前台业务 I/O 将直接写入后端存储系统。详情请参见 [UBSIO-BoostIO API 参考的 BioNotifyUpgradePrepare 章节](boostio_api_reference.md#bionotifyupgradeprepare)。

### 升级检查

执行升级检查操作后，UBSIO-BoostIO 会检查分布式缓存中的业务数据是否已淘汰完成。淘汰完成则检查通过，否则检查失败。只有检查通过后，才能执行集群下电操作。详情请参见 [UBSIO-BoostIO API 参考的 BioCheckUpgradeReady 章节](boostio_api_reference.md#biocheckupgradeready)。

### 升级完成

软件离线升级完成并重启集群服务后，需要执行升级完成操作。UBSIO-BoostIO 会关闭写透模式并重新启用分布式缓存服务。详情请参见 [UBSIO-BoostIO API 参考的 BioNotifyUpgradeFinish 章节](boostio_api_reference.md#bionotifyupgradefinish)。
