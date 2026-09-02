# UBSIO-MemStore 安装部署指南

本文档介绍 UBSIO-MemStore 的环境要求、安装、启动和卸载流程。产品特性和故障处理请参见[用户指南](memstore_user_guide.md)，配置项及 TLS 认证操作请参见[配置说明](memstore_configuration_guide.md)。

## 软件安装

### 环境要求

安装部署 UBSIO-MemStore 前，请检查物理环境、依赖软件及其版本是否满足要求。满足前置环境要求是成功安装部署和正常运行应用程序的先决条件。

**硬件要求**

UBSIO-MemStore 安装在计算节点上，集群中各计算节点的硬件要求如[表 1](#硬件要求)所示。

<a id="硬件要求"></a>
**表 1 硬件要求**

| 硬件名称 | 配套关系 |
| --- | --- |
| 服务器 | TaiShan 200 服务器 |
| 处理器 | 鲲鹏 920 处理器或鲲鹏 950 处理器 |
| 内存大小 | 512 GB |
| 内存频率 | 2666 MHz 或 2600 MHz |
| 网卡 | CX5 100 GE；TCP 10 GE |

**软件要求**

安装 UBSIO-MemStore 前，需要先安装依赖软件。建议遵循各软件的安全标准进行安装。集群中各节点的软件版本要求如[表 2](#软件要求)所示，以下软件不在交付范围内。

<a id="软件要求"></a>
**表 2 软件要求**

| 软件名称 | 软件版本 |
| --- | --- |
| OS | openEuler 24.03 LTS SP3 |
| ZooKeeper | 3.9.5 |
| Java SDK | 1.8 |

**运行依赖包**

UBSIO-MemStore 的运行依赖包如[表 3](#运行依赖包)所示。使用 `yum` 在线安装 UBSIO-MemStore 时，`yum` 会自动解析并安装这些依赖，无需逐个安装；使用 `rpm` 离线安装时，需要提前准备产品包及其依赖 RPM。

<a id="运行依赖包"></a>
**表 3 运行依赖包**

| RPM 包名 | 用途 |
| --- | --- |
| `ubs-comm-lib` | 提供 UBSIO-MemStore 使用的 `libhcom.so` 通信库 |
| `libzookeeper-mt2` | 提供连接 ZooKeeper 所需的 `libzookeeper_mt.so.2` 客户端库 |
| `openssl-libs` | 提供 TLS 认证所需的 OpenSSL 运行库 |
| `libboundscheck` | 提供安全 C 库函数运行库 |

> 系统基础运行库由操作系统提供，不需要单独准备。

### 软件包说明

UBSIO-MemStore 提供运行包和开发包，具体用途如[表 4](#软件包说明)所示。

<a id="软件包说明"></a>
**表 4 软件包说明**

| RPM 包名 | 用途 |
| --- | --- |
| `ubs-io-memstore` | 运行包，提供可执行程序、共享库和配置文件；用于部署和运行 UBSIO-MemStore |
| `ubs-io-memstore-devel` | 开发包，提供 C API 头文件；仅在编译或集成 UBSIO-MemStore 应用时安装 |

### 安装前提条件

- ZooKeeper 已由用户在指定节点完成安装和配置，并已规划可供 UBSIO-MemStore 使用的服务地址。本文档不提供 ZooKeeper 安装方法。
- UBSIO-MemStore 的运行用户和用户组已在所有目标节点创建，且 UID、GID 保持一致。

### （可选）清理环境

> **须知：**
>
> 重新安装前，请确保环境中不存在旧版本 UBSIO-MemStore，并在卸载前停止相关业务和 UBSIO-MemStore 进程。

在已安装旧版本的节点上执行：

```bash
yum remove ubs-io-memstore-devel ubs-io-memstore
```

### 安装 UBSIO-MemStore

1. 使用 `root` 用户登录服务器。
2. 选择以下任一方式安装 UBSIO-MemStore。

    **在线安装**

    仅部署和运行 UBSIO-MemStore 时，安装运行包：

    ```bash
    yum install ubs-io-memstore -y
    ```

    编译或集成 UBSIO-MemStore 应用时，安装开发包：

    ```bash
    yum install ubs-io-memstore-devel -y
    ```

    `ubs-io-memstore-devel` 依赖同版本的运行包。使用 `yum` 安装开发包时，会自动安装匹配的 `ubs-io-memstore`，无需重复执行运行包安装命令。

    **离线安装**

    `rpm` 不会自动从软件源下载缺失的依赖。离线安装前，根据使用场景将[表 4](#软件包说明)中的产品 RPM 与[表 3](#运行依赖包)中的依赖 RPM 放在同一独立目录中。安装开发包时，还必须准备版本匹配的运行包。然后执行：

    ```bash
    rpm -ivh ./*.rpm
    ```

3. 按照相同流程在其他节点上安装 UBSIO-MemStore。

#### 安装目录

安装完成后的主要目录和文件如[表 5](#软件包目录结构)所示。

<a id="软件包目录结构"></a>
**表 5 软件包目录结构**

| 文件或目录 | 说明 |
| --- | --- |
| `/etc/mms/mms.conf` | UBSIO-MemStore 配置文件 |
| `/usr/lib64/` | UBSIO-MemStore 动态库 |
| `/usr/bin/` | UBSIO-MemStore 可执行文件 |
| `/usr/include/mms/mms_c.h` | `ubs-io-memstore-devel` 提供的 UBSIO-MemStore C API 头文件 |

#### 安装后配置

1. 根据业务场景配置 `/etc/mms/mms.conf`。具体配置项和 TLS 认证操作请参见[配置说明](memstore_configuration_guide.md)。
2. 确保所有节点的集群规模、ZooKeeper 地址、部署模式和 TLS 开关等公共配置保持一致。

## 软件启动

### 启动前提条件

启动 UBSIO-MemStore 前，请确认：

- ZooKeeper 服务已经启动且运行正常，所有 UBSIO-MemStore 节点均可访问该服务。
- `/etc/mms/mms.conf` 中的 `mms.cm.zk_host` 与实际 ZooKeeper 服务地址一致。
- 各节点间的业务网络连通，TLS 开启时所需证书、私钥和口令密文文件可由运行用户读取。

### 配置 RDMA 无损网络

> **说明：**
>
> 不使用 RDMA 协议时，可跳过本节。

如果安装环境配置了 RoCE 网卡，并且 UBSIO-MemStore 使用 RDMA 协议，需要先配置 RDMA 无损网络参数，避免数据通信过程中出现错误。具体配置方法请参见网卡厂商提供的 RDMA 使用说明。

### 启动 UBSIO-MemStore

#### 融合部署模式

融合部署模式下，UBSIO-MemStore 没有独立进程，而是与用户进程共同运行。

1. 将 `mms.conf` 中的 `mms.deployment.mode` 配置为 `converge`。
2. 用户进程链接 `libmms_server.so`，并调用 `MmsInitialize` 启动服务。

#### 分离部署模式

分离部署模式下，Server 端以独立进程运行并读取 `mms.conf`，Client 端集成在用户进程中。

1. 将 `mms.conf` 中的 `mms.deployment.mode` 配置为 `separate`。
2. 在每个 Server 节点启动 `mmsd`：

    ```bash
    mmsd &
    ```

3. 用户进程链接 `libmms_client.so`，并调用 `MmsInitialize` 启动 Client 端服务。

## 软件卸载

> **须知：**
>
> 卸载前，请停止相关业务和 UBSIO-MemStore 进程，并根据安全要求妥善清理证书、私钥和口令密文文件。

在每个安装节点执行：

```bash
yum remove ubs-io-memstore-devel ubs-io-memstore
```
