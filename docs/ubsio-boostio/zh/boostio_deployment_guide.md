# UBS IO-BoostIO 安装部署指南

本文档介绍 UBS IO-BoostIO 的使用场景、节点规划、环境要求，以及安装、启动、卸载和升级流程。

## 使用场景

UBS IO-BoostIO 适用于大数据分析、AI 融合应用等存算分离场景。计算节点通过网络访问远端存储时，可利用 UBS IO-BoostIO 将本地内存和 NVMe SSD 组织为分布式读写缓存，降低业务 I/O 时延并减轻后端存储的访问压力。

本文以接入 JuiceFS、使用 Ceph 作为后端存储的部署环境为例。可根据业务进程与缓存服务的管理方式选择部署模式：

- **融合部署**：UBS IO-BoostIO 以动态库的方式加载到 JuiceFS 进程中，适用于随 JuiceFS 进程共同启动和运行缓存服务的场景。
- **分离部署**：UBS IO-BoostIO 服务以独立的 `bio_daemon` 进程运行，适用于需要分别管理 JuiceFS 进程和缓存服务进程的场景。

## 软件安装

### 节点规划

安装前，请完成以下规划，并将节点清单用于后续逐节点安装和配置：

1. **确定安装节点和软件包。** 所有 UBS IO-BoostIO 集群节点都必须安装相同版本的 `ubs-io-boostio` 运行包及其运行依赖。仅在需要编译或集成应用的节点上额外安装匹配版本的 `ubs-io-boostio-devel` 开发包；各依赖包的用途见下文“运行依赖包”。
2. **提前准备集群节点 IP。** 整理每个节点的主机名、业务 IP 地址及网段掩码；管理网络与业务网络分离时，另行记录用于登录和安装的管理 IP。根据本节点的业务网卡地址设置 `bio.net.data.ip_mask`，规划 `bio.net.data.listen_port`，并确认节点间业务网络互通。
3. **规划外部服务地址。** 提前准备 ZooKeeper 服务的 IP 和端口、JuiceFS 使用的 Redis 服务地址，以及 Ceph 集群连接信息。外部服务按各自角色部署，所有 UBS IO-BoostIO 节点均需能够访问对应服务。
4. **规划缓存资源。** 为每个节点预留内存和 NVMe SSD 缓存空间，记录各节点实际使用的设备路径。设备路径应逐节点核对，不能直接照搬其他节点的配置。

### 环境要求

安装部署 UBS IO-BoostIO 前，请检查物理环境、依赖软件及其版本是否满足要求。满足前置环境要求是成功安装部署和正常运行应用程序的先决条件。

**硬件要求**

UBS IO-BoostIO 安装在计算节点上，集群中各计算节点的硬件要求如[表 1](#硬件配套要求)所示。

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

安装 UBS IO-BoostIO 前，需要先按节点角色准备依赖软件。建议遵循各软件的安全标准进行安装。部署环境涉及的软件版本要求如[表 2](#软件要求)所示，以下软件不在交付范围内；ZooKeeper、Redis 和 Ceph 服务部署在规划的服务节点上。

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

UBS IO-BoostIO 的运行依赖包如[表 3](#运行依赖包)所示。使用 `yum` 在线安装 UBS IO-BoostIO 时，`yum` 会自动解析并安装这些依赖，无需逐个安装；使用 `rpm` 离线安装时，需要提前准备产品包及其依赖 RPM。

<a id="运行依赖包"></a>
**表 3 运行依赖包**

| RPM 包名 | 用途 |
| --- | --- |
| `ubs-comm-lib` | 提供 UBS IO-BoostIO 使用的 `libhcom.so` 通信库 |
| `libzookeeper-mt2` | 提供连接 ZooKeeper 所需的 `libzookeeper_mt.so.2` 客户端库 |
| `openssl-libs` | 提供 TLS 认证所需的 OpenSSL 运行库 |
| `libboundscheck` | 提供安全 C 库函数运行库 |
| `librados2` | 提供访问 Ceph 集群所需的 RADOS 客户端库 |

> 系统基础运行库由操作系统提供，不需要单独准备。

### 软件包说明

UBS IO-BoostIO 提供运行包和开发包，具体用途如[表 4](#软件包说明)所示。

<a id="软件包说明"></a>
**表 4 软件包说明**

| RPM 包名 | 用途 |
| --- | --- |
| `ubs-io-boostio` | 运行包，提供服务程序、运行库和配置文件；用于部署和运行 UBS IO-BoostIO |
| `ubs-io-boostio-devel` | 开发包，提供头文件、SDK 开发链接库和静态库；仅在编译或集成 UBS IO-BoostIO 应用时安装 |

### （可选）清理环境

> **须知：**
>
> - 重新安装前，请确保环境中不存在旧版本 UBS IO-BoostIO。
> - 建议及时清理 SDK 端不再使用的日志文件，避免磁盘空间耗尽。
> - SDK 端统计文件最大为 10 MB，Server 端统计文件最大为 50 MB。UBS IO-BoostIO 重新部署启动后会生成新的统计文件，建议及时清理旧文件。

1. 在 ZooKeeper Server 节点上清理 UBS IO-BoostIO 集群信息。

    运行 ZooKeeper 客户端：

    ```bash
    sh /opt/zookeeper/bin/zkCli.sh
    ```

    连接 ZooKeeper 后执行：

    ```text
    deleteall /cm
    ```

2. 清理 UBS IO-BoostIO 磁盘管理元数据。

    > **危险：**
    >
    > 以下命令会覆盖目标设备起始位置的 8 MiB 数据。执行前必须确认设备路径无误、未挂载，并且未被 LVM、RAID 或 swap 使用。请勿对系统盘或业务数据盘执行该命令。

    将 `/dev/nvmeXnY` 替换为确认无误的目标设备：

    ```bash
    dd bs=8K count=1024 if=/dev/zero of=/dev/nvmeXnY
    ```

### 安装 UBS IO-BoostIO

1. 按节点规划清单，使用 `root` 用户登录待安装的 UBS IO-BoostIO 集群节点。所有集群节点均需执行以下安装流程。
2. 选择以下任一方式安装 UBS IO-BoostIO。

    **在线安装**

    仅部署和运行 UBS IO-BoostIO 时，安装运行包：

    ```bash
    yum install ubs-io-boostio -y
    ```

    编译或集成 UBS IO-BoostIO 应用时，安装开发包：

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
| `/usr/bin` | `bio_daemon` | UBS IO-BoostIO 服务可执行文件 |

**表 7 lib 目录文件说明**

| 目录 | 文件名称 | 描述 |
| --- | --- | --- |
| `/usr/lib64` | `libbio_interceptor_server.so` | 桥接服务共享对象文件 |
| `/usr/lib64` | `libbio_server.so` | UBS IO-BoostIO Server 端共享对象文件 |
| `/usr/lib64` | `libbio_sdk.so.1.0.0` | UBS IO-BoostIO SDK 端共享对象文件 |
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
    > UBS IO-BoostIO 启动时需要读取 Ceph Client 端密钥。权限配置方法请参见 [Ceph 官方文档](https://docs.ceph.com/en/latest/rados/configuration/auth-config-ref/#keys)。

3. 按照相同流程在其他节点上安装并配置 UBS IO-BoostIO。

## 容器镜像部署（可选）

容器环境采用“依赖环境与源码分离”的方式部署：镜像只安装 UBS IO-BoostIO 的构建和运行依赖，`master` 分支源码从宿主机挂载到 `/workspace`，编译产物保留在宿主机源码目录中。这样可以复用同一环境镜像构建不同提交，也便于核对实际运行的源码版本。

> **说明：**
>
> - 本节使用 `quay.io/ascend/vllm-ascend:v0.23.0-openeuler` 作为基础镜像。
> - 基础镜像包含 vLLM Ascend 环境，但 UBS IO-BoostIO 本身不要求挂载 NPU 设备。只有同一容器还运行 vLLM 等 NPU 业务时，才需要按对应业务的容器文档挂载 NPU 驱动和设备。
> - 以下命令以宿主机源码目录 `/opt/ubs-io` 为例。该目录应检出 `master` 分支或经过确认的发布提交，并保证所有节点使用相同版本。

### 部署流程概览

```text
quay.io/ascend/vllm-ascend:v0.23.0-openeuler
                         │
                         │ docker build：安装 BoostIO 构建/运行依赖
                         ▼
           ubsio-boostio-build:v0.23.0-openeuler
                         │
                         │ 挂载 master 源码到 /workspace
                         ▼
          bash ubsio-boostio/build.sh -t release
                         │
                         ▼
       ubsio-boostio/dist/boostio/{bin,lib,conf,include}
                         │
                         │ 挂载配置、日志、Ceph 配置和经核验的缓存盘
                         ▼
                    bio_daemon
```

### 步骤 1：检查宿主机环境和源码

1. 确认 CPU 架构、Docker 服务和磁盘空间：

    ```bash
    uname -m
    docker version
    docker info
    df -h /var/lib/docker
    ```

2. 准备 `master` 分支源码。已有 `/opt/ubs-io` 工作树时，只需核对分支和提交：

    ```bash
    git -C /opt/ubs-io status --short --branch
    git -C /opt/ubs-io rev-parse HEAD
    ```

    尚未准备源码时执行：

    ```bash
    git clone --depth 1 --branch master \
        https://gitcode.com/openeuler/ubs-io.git /opt/ubs-io
    ```

3. 确认宿主机能够访问基础镜像仓库和源码构建期间使用的 GitCode 地址：

    ```bash
    docker pull quay.io/ascend/vllm-ascend:v0.23.0-openeuler
    git ls-remote https://gitcode.com/openeuler/ubs-comm.git HEAD
    git ls-remote https://gitcode.com/openeuler/libboundscheck.git HEAD
    ```

### 步骤 2：构建依赖环境镜像

仓库提供的 `ubsio-boostio/docker/Dockerfile` 只打包依赖环境，不复制源码。它同时显式清除了基础镜像的服务入口，容器启动后由使用者选择执行编译命令或 `bio_daemon`。

在源码根目录执行：

```bash
cd /opt/ubs-io
docker build \
    --build-arg BASE_IMAGE=quay.io/ascend/vllm-ascend:v0.23.0-openeuler \
    -t ubsio-boostio-build:v0.23.0-openeuler \
    ubsio-boostio/docker
```

构建完成后记录镜像标识，便于问题定位和多节点一致性检查：

```bash
docker image inspect ubsio-boostio-build:v0.23.0-openeuler \
    --format 'image={{.Id}} created={{.Created}} arch={{.Architecture}}'
docker image inspect quay.io/ascend/vllm-ascend:v0.23.0-openeuler \
    --format '{{index .RepoDigests 0}}'
```

> **说明：**
>
> - 基础镜像已提供 CMake、GCC/G++、Git、Make、RDMA Core、numactl 开发包、证书和常用系统工具，Dockerfile 不再重复安装。Dockerfile 仅补充 `ubsio-boostio/build/ubs-io.spec`、BoostIO CMake 文件和 `build.sh` 要求但基础镜像缺少的依赖；其中 Maven、Autoconf、Automake 和 Libtool 用于首次构建内置 ZooKeeper 客户端。
> - 首次编译时，如果容器中没有系统安装的 HCOM、libboundscheck 或 ZooKeeper 客户端，CMake 会从 GitCode 拉取并构建对应源码，因此必须保证构建容器能访问 GitCode。
> - 生产环境建议记录基础镜像的 digest，并在多节点使用同一个 digest，避免同名 tag 更新造成环境不一致。

### 步骤 3：创建构建容器并编译

创建容器。纯编译场景不需要 `--privileged`、NPU 设备或原始磁盘：

```bash
docker run -dit \
    --name ubsio-boostio-build \
    --network host \
    -v /opt/ubs-io:/workspace \
    ubsio-boostio-build:v0.23.0-openeuler \
    /bin/bash
```

检查源码挂载和实际构建版本：

```bash
docker exec ubsio-boostio-build \
    test -f /workspace/ubsio-boostio/build.sh
docker exec ubsio-boostio-build \
    git -C /workspace status --short --branch
docker exec ubsio-boostio-build \
    git -C /workspace rev-parse HEAD
```

执行 Release 构建：

```bash
docker exec ubsio-boostio-build \
    bash -lc 'cd /workspace/ubsio-boostio && bash build.sh -t release'
```

`build.sh` 会在挂载的源码目录下生成：

```text
/opt/ubs-io/ubsio-boostio/dist/
├── BoostIO_1.0.0_Linux-<arch>_release.tar.gz
├── boostio/
    ├── bin/bio_daemon
    ├── conf/bio.conf
    ├── include/
    └── lib/
└── 3rdparty/
    ├── libboundscheck/
    ├── ubs-comm/
    └── zookeeper/
```

检查关键产物及其动态库依赖：

```bash
docker exec ubsio-boostio-build bash -lc '
    export LD_LIBRARY_PATH=/workspace/ubsio-boostio/dist/boostio/lib:/workspace/ubsio-boostio/dist/3rdparty/ubs-comm/lib:/workspace/ubsio-boostio/dist/3rdparty/libboundscheck/lib:${LD_LIBRARY_PATH:-} &&
    test -x /workspace/ubsio-boostio/dist/boostio/bin/bio_daemon &&
    test -f /workspace/ubsio-boostio/dist/boostio/lib/libbio_server.so &&
    ! ldd /workspace/ubsio-boostio/dist/boostio/bin/bio_daemon | grep -q "not found"
'
```

### 步骤 4：准备运行配置和目录

UBS IO-BoostIO 固定从 `/etc/boostio/bio.conf` 读取服务配置，并将日志写入 `/var/log/boostio/bio.log`。在宿主机准备持久化目录：

```bash
install -d -m 750 /etc/boostio /var/log/boostio
install -m 640 \
    /opt/ubs-io/ubsio-boostio/dist/boostio/conf/bio.conf \
    /etc/boostio/bio.conf
```

根据[配置说明](boostio_configuration_guide.md)修改 `/etc/boostio/bio.conf`，至少核对以下内容：

| 配置项 | 容器部署要求 |
| --- | --- |
| `bio.net.data.ip_mask` | 使用 `--network host` 后，填写本节点实际业务网卡 IP/掩码；每个节点分别核对。 |
| `bio.net.data.listen_port` | 确认端口未被占用，且集群节点之间可访问。 |
| `bio.net.data.protocol` | 无 RDMA 容器设备时使用 `tcp`；使用 `rdma` 前先完成 RoCE 和容器 RDMA 设备配置。 |
| `bio.cm.initial.nodes_count` | 与计划启动的 BoostIO 节点数一致；当前合法值不少于 2。 |
| `bio.cm.zk_host` | 填写容器可访问的 ZooKeeper 地址列表。 |
| `bio.mem.size_in_gb` | 不得超过容器和宿主机可用内存。 |
| `bio.disk.path` | 填写已经核验并映射到容器内的缓存盘路径。 |
| `bio.underfs.*` | 与实际 Ceph 或 HDFS 后端一致；Ceph 配置和密钥路径在容器内必须可读。 |
| `bio.net.tls.*` | 生产环境保持 TLS 开启，并填写容器内证书、私钥和可选解密库路径。 |

> **危险：**
>
> `bio.disk.path` 指向的设备会作为缓存盘使用。映射任何 `/dev/*` 设备前，必须在宿主机逐个确认设备路径、文件系统、挂载状态以及 LVM、RAID、swap 使用情况。不得使用系统盘或承载业务数据的设备，也不得照搬其他节点的设备名。

只读核验示例：

```bash
lsblk -o NAME,PATH,SIZE,TYPE,FSTYPE,MOUNTPOINTS
findmnt --source /dev/nvmeXnY
swapon --show
pvs
cat /proc/mdstat
```

### 步骤 5：启动 UBS IO-BoostIO 容器

以下示例采用分离部署模式。每个物理节点只能启动一个 `bio_daemon`，所有节点应使用相同镜像和同一版本的源码产物，但 `bio.net.data.ip_mask`、缓存盘路径等节点属性必须按实际环境分别配置。

将 `/dev/nvmeXnY` 替换为已经完成只读核验的缓存盘：

```bash
docker run -d \
    --name ubsio-boostio \
    --network host \
    --ipc host \
    --ulimit memlock=-1:-1 \
    --device=/dev/nvmeXnY:/dev/nvmeXnY \
    -v /opt/ubs-io:/workspace:ro \
    -v /etc/boostio:/etc/boostio \
    -v /var/log/boostio:/var/log/boostio \
    -v /etc/ceph:/etc/ceph:ro \
    -e LD_LIBRARY_PATH=/workspace/ubsio-boostio/dist/boostio/lib:/workspace/ubsio-boostio/dist/3rdparty/ubs-comm/lib:/workspace/ubsio-boostio/dist/3rdparty/libboundscheck/lib:/usr/lib64:/usr/lib \
    ubsio-boostio-build:v0.23.0-openeuler \
    /workspace/ubsio-boostio/dist/boostio/bin/bio_daemon
```

参数说明：

| 参数 | 作用 |
| --- | --- |
| `--network host` | 让容器直接使用宿主机业务网卡和监听端口，使 `bio.net.data.ip_mask` 能匹配宿主机地址。 |
| `--ipc host` | 让分离部署的 SDK 和 Server 在跨容器或宿主机场景下共享 IPC/共享内存命名空间。若 SDK 与 Server 都在同一容器，可按实际隔离方案调整。 |
| `--ulimit memlock=-1:-1` | 避免通信注册内存受过小的 memlock 限制。 |
| `--device` | 仅向容器暴露已核验的缓存设备。生产部署不建议为了方便直接使用 `--privileged`。 |
| `/etc/boostio` 挂载 | 配置更新流程可能生成 `bio.conf.bak`，因此该目录需要可写；证书和私钥仍应单独按最小权限只读挂载。 |
| `/var/log/boostio` 挂载 | 将日志持久化到宿主机，容器重建后仍可用于问题定位。 |

当前实现中，`bio.disk.path` 为空时会将本节点内存缓存容量重置为 0，节点不能加入分区，因此完整服务部署至少需要一块有效缓存盘。使用多块缓存盘时，为每个已核验设备分别增加一个 `--device`。RDMA 模式还需按网卡和 HCOM 部署要求映射对应的 `/dev/infiniband/*` 设备；TCP 模式不需要。

### 步骤 6：验证服务

检查容器、进程、端口和日志：

```bash
docker ps --filter name=ubsio-boostio
docker top ubsio-boostio
ss -lntp | grep 7201
docker logs --tail 100 ubsio-boostio
tail -n 100 /var/log/boostio/bio.log
```

服务完全就绪需要 ZooKeeper、后端存储以及不少于 `bio.cm.initial.nodes_count` 个 BoostIO 节点同时可用。单节点仅能验证镜像、动态库、配置加载和启动前置检查，不能作为集群就绪验证。

建议在所有节点记录以下信息，出现问题时一并收集：

```bash
git -C /opt/ubs-io rev-parse HEAD
docker image inspect ubsio-boostio-build:v0.23.0-openeuler --format '{{.Id}}'
docker inspect ubsio-boostio --format '{{.HostConfig.NetworkMode}} {{.HostConfig.IpcMode}}'
docker logs --tail 200 ubsio-boostio
```

### 停止和清理容器

```bash
docker stop --time 30 ubsio-boostio
docker rm ubsio-boostio
docker stop --time 10 ubsio-boostio-build
docker rm ubsio-boostio-build
```

上述命令只删除容器，不删除宿主机上的源码、编译产物、配置、日志或缓存盘数据。不要在未确认用途时执行镜像批量清理或 Docker 数据目录清理。

## 软件启动

### 启动前提条件

启动 UBS IO-BoostIO 前，请确认以下外部服务已经启动且运行正常，并确保所有 UBS IO-BoostIO 节点均可访问对应服务：

- JuiceFS 使用的 Redis 服务已经启动。
- UBS IO-BoostIO 集群管理使用的 ZooKeeper 服务已经启动，且服务地址与 `bio.conf` 中的 ZooKeeper 配置一致。
- 使用 Ceph 作为后端存储时，Ceph 集群已经启动且状态正常，目标存储池及客户端密钥可用。

### 配置 RDMA 无损网络

如果安装环境配置了 RoCE 网卡，并且 UBS IO-BoostIO 使用 RDMA 协议，需要先配置 RDMA 无损网络参数，避免数据通信过程中出现错误。具体配置方法请参见网卡厂商提供的 RDMA 使用说明。

### 启动 UBS IO-BoostIO

#### 融合部署模式

融合部署模式下，UBS IO-BoostIO 不存在独立运行进程，而是以动态链接库的方式加载到 JuiceFS 进程中。因此，使用 UBS IO-BoostIO 功能前需要启动 JuiceFS 进程。

#### 分离部署模式

- 每个物理节点有且仅有一个 `bio_daemon` 进程。请勿在同一节点启动多个 `bio_daemon` 实例，否则可能导致数据不一致。
- 分离部署模式下，UBS IO-BoostIO 与 JuiceFS 是独立运行的组件，必须先启动 `bio_daemon`，再启动 JuiceFS。
- JuiceFS 启动时会自动加载 UBS IO-BoostIO SDK 链接库，因此必须确保 `bio_daemon` 已经运行并就绪。
- 支持通过命令行在后台手动启动 `bio_daemon`，适用于应用开发、功能调试和需要自行监控进程状态的场景。

## 软件卸载

> **须知：**
>
> - 卸载 UBS IO-BoostIO 前，建议先安全删除所有密钥存储文件。
> - 建议删除安装 UBS IO-BoostIO 时创建的目录。

**操作步骤**

1. 登录 UBS IO-BoostIO 安装节点并执行：

    ```bash
    yum remove ubs-io-boostio-devel ubs-io-boostio
    ```

2. 在其他节点上依次执行相同操作。

## 软件升级

> **说明：**
>
> 当前版本仅支持离线升级。升级时需要停止应用业务，不支持在线升级。

UBS IO-BoostIO 提供升级准备、升级检查和升级完成三种操作，用于 JuiceFS 开发者集成端到端软件升级流程。

### 升级准备

执行升级准备操作后，UBS IO-BoostIO 会打开写透模式并关闭分布式缓存功能，此时前台业务 I/O 将直接写入后端存储系统。详情请参见 [UBS IO-BoostIO API 参考的 BioNotifyUpgradePrepare 章节](boostio_api_reference.md#bionotifyupgradeprepare)。

### 升级检查

执行升级检查操作后，UBS IO-BoostIO 会检查分布式缓存中的业务数据是否已淘汰完成。淘汰完成则检查通过，否则检查失败。只有检查通过后，才能执行集群下电操作。详情请参见 [UBS IO-BoostIO API 参考的 BioCheckUpgradeReady 章节](boostio_api_reference.md#biocheckupgradeready)。

### 升级完成

软件离线升级完成并重启集群服务后，需要执行升级完成操作。UBS IO-BoostIO 会关闭写透模式并重新启用分布式缓存服务。详情请参见 [UBS IO-BoostIO API 参考的 BioNotifyUpgradeFinish 章节](boostio_api_reference.md#bionotifyupgradefinish)。
