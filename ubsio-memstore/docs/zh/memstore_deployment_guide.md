# 部署指南

## 概述

MemStore（以下简称MMS），面向证券极速交易系统。传统交易系统使用磁盘数据读写架构，订单和数据库跨节点访问，交易场景时延较大，目前极速交易场景主要通过缩短传输距离及低时延交换机减小交易时延，业内同质化严重，缺乏竞争力，MMS提供基于RDMA的极致内存管理方案，提供证券核心交易系统数据极速内存同步服务，来满足客户诉求。

## 安装前准备

安装前需要确保环境里没有安装MemStore。如果环境上已安装MMS，需要进行环境清理操作，确保后续安装能够正常进行。

**清理环境**

使用rpm -e ubs-io-memstore-\{version\}.oe2403sp3.aarch64.rpm卸载已经安装的memstore。

**安装Zookeeper**

1. 登录需要安装Zookeeper Server的节点。
2. 进入目标目录，下载Zookeeper软件包并解压。

    以下以目录“/user/local”为例。

    ```cmd
    cd /usr/local
    wget http://archive.apache.org/dist/zookeeper/zookeeper-3.7.0/zookeeper-3.7.0.tar.gz
    tar -zxvf zookeeper-3.7.0.tar.gz
    ```

3. 添加Zookeeper到环境变量。
    1. 打开文件。

        ```cmd
        vim /etc/profile
        ```

    2. 按“i”进入编辑模式，添加ZooKeeper到环境变量。

        ```cmd
        export ZOOKEEPER_HOME=/usr/local/zookeeper
        export PATH=$ZOOKEEPER_HOME/bin:$PATH
        ```

    3. 按“Esc”键，输入**:wq**，按“Enter”保存并退出编辑。
    4. 使环境变量生效。

        ```cmd
        source /etc/profile
        ```

4. 修改Zookeeper配置文件。
    1. 进入Zookeeper所在目录。

        ```cmd
        cd /usr/local/zookeeper/conf
        cp zoo_sample.cfg zoo.cfg
        ```

    2. 打开配置文件。

        ```cmd
        vim zoo.cfg
        ```

    3. 按“i”进入编辑模式，修改数据目录。

        ```cmd
        dataDir=/usr/local/zookeeper/tmp
        ```

    4. 按“Esc”键，输入:wq，按“Enter”保存并退出编辑。

**配置节点间免密登录**

1. 在需要执行安装脚本的节点，执行以下命令生成密钥对。

    ```cmd
    ssh-keygen -t rsa -b 4096 -C "your_email@example.com"
    ```

    一直回车，直到看到以下打印信息。

    ```cmd
    ~/.ssh/id_rsa     # 私钥
    ~/.ssh/id_rsa.pub  # 公钥
    ```

2. 复制公钥到其它节点。

    ```cmd
    cat ~/.ssh/id_rsa.pub | ssh xxx@xx.xx.xx.xx "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys"
    ```

3. 免密登录，登录成功表示配置免密登录成功。

    ```cmd
    ssh user@xxx.xxx.xxx.xxx
    ```

**启动Zookeeper**

1. 执行以下命令，启动Zookeeper。

    ```cmd
    sh /usr/local/zookeeper/bin/zkServer.sh start
    ```

2. 执行以下命令，验证Zookeeper启动成功。

    ```cmd
    sh zkServer.sh status
    ```

    查看以下打印信息，表示启动成功。

    ```cmd
    Client port found:2181. Client address:localhost. Client SSL: false.
    Mode:standalone
    ```   

## 环境要求

>[!NOTICE] 说明
>安装部署MMS软件前，请提前检查物理环境是否满足要求，依赖软件是否已经安装成功，且安装的软件版本是否满足特性要求。前置环境要求是确保安装部署操作成功和后续应用程序正常执行的先决条件。

**硬件要求**

MMS软件的安装部署程序均在计算节点上执行，集群中各个计算节点的硬件要求如[表1](#硬件版本)所示。

**表 1**  硬件版本<a id="硬件版本"></a>

<a></a>
<table><tbody><tr><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.1.1"><p><a></a><a></a>服务器名称</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.1 "><p><a></a><a></a>TaiShan 200服务器</p>
</td>
</tr>
<tr><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.2.1"><p><a></a><a></a>处理器</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.2.1 "><p><a></a><a></a>鲲鹏920处理器</p>
</td>
</tr>
<tr><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.3.1"><p><a></a><a></a>内存大小</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.3.1 "><p><a></a><a></a>512GB</p>
</td>
</tr>
<tr><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.4.1"><p><a></a><a></a>内存频率</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.4.1 "><p><a></a><a></a>2666MHz</p>
</td>
</tr>
<tr><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.5.1"><p><a></a><a></a>网卡</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.5.1 "><a></a><a></a><ul><li>CX5 100GE</li><li>TCP 10GE</li></ul>
</td>
</tr>
</tbody>
</table>

**软件要求**

MMS软件安装前需要将前置依赖的软件安装成功，建议参考各软件安全标准规范安装，集群中各节点的操作系统和软件要求如[表2](#软件版本)所示，以下软件不在交付范围。

**表 2**  软件版本<a id="软件版本"></a>

|软件名称|软件版本|
|--|--|
|OS|openEuler 22.03 LTS SP2|
|ZooKeeper|3.7.0|
|Java SDK|1.8|

**获取软件安装包**

**表 3**  MMS软件获取列表

|名称|包名|发布类型|说明|获取地址|
|--|--|--|--|--|
|MMS软件包|ubs-io-memstore-{version}.oe2403sp3.aarch64.rpm|闭源|MMS软件rpm安装包。|联系华为工程师获取。|

### 安装MMS

1. 登录所有节点，上传memstore的rpm包。
2. 在所有节点使用命令rpm -ivh ubs-io-memstore-\{version\}.oe2403sp3.aarch64.rpm安装mms。

    安装后文件对应关系参照下表：

    **表 1**  mms文件安装目录对应表

    |文件|安装目录|
    |--|--|
    |mms.conf|/etc/mms/mms.conf|
    |.so文件|/usr/lib64/|
    |可执行文件|/usr/bin/|
    |头文件mms_c.h|/usr/include/mms/mms_c.h|

3. 参照表2修改所有节点的配置文件/etc/mms/mms.conf。

    **表 2**  MMS配置项

    |归属模块|配置项名称|简要描述|默认值|合法值/区间|
    |--|--|--|--|--|
    |Log|mms.log.level|日志打印级别。|info|debuginfowarntraceerror|
    |Trace|mms.trace.switch|统计日志开关。|false|truefalse|
    |Crc|mms.crc.switch|数据完整性校验开关。|false|truefalse|
    |Sequence|mms.sequence.switch|消息序列化收发开关。|false|truefalse|
    |Multicast|mms.multicast.switch|组播开关。|true|truefalse|
    |Deployment|mms.deployment.mode|部署方式（分离/融合）。|separate|separateconverge|
    |Memory|mms.mem.numa.id|Numa ID。|0,1|机器上numa的id号|
    |mms.mem.numa.size|每个numa上分配内存的大小。单位：GB|8,8|机器对每个numa限制的内存范围内。|
    |mms.mem.min_block_size|存储data的数据块小块内存大小。单位：KB|2|[1,64]|
    |mms.mem.max_block_size|存储data的数据块大块内存大小。单位：KB|10|[1,64]|
    |mms.mem.min_max_block_rate|小块和大块的总内存大小分配比例。|8:2|x:y，其中x、y均大于等于0，且x+y = 10|
    |Network|mms.net.rpc.ip_mask|RPC掩码。|192.168.100.100/24|*.*.*.*/#，其中*为0 ~ 255，#为0 ~ 32|
    |mms.net.rpc.listen_port|RPC端口。|7500|[7201, 7800]|
    |mms.net.multicast.listen_port|组播端口|7501|[7201, 7800]|
    |mms.net.rpc.protocol|RPC协议。|tcp|rdmatcp|
    |mms.net.multicast.protocol|组播通信协议|rdma|rdmatcp注:UB通信需要用tcp|
    |mms.net.rpc.connect.count|RPC每个通信channel里的连接数。|1|[1, 16]|
    |mms.net.rpc.busy_polling_mode|rpc通信的工作方式是否是busy polling。|true|true, false|
    |mms.net.rpc.worker.groups|rpc work组。|1,1|每组之间数字用','分割，表示每个组有多少个worker|
    |mms.net.rpc.worker.groups.cpuset|worker CPU集合|0-0,2-2|每组之间数字用','分割，x-y表示绑定本组的worker到从x到y号cpu上，与配置项mms.net.rpc.worker.groups要对应。|
    |mms.net.ipc.busy_polling_mode|ipc通信的工作方式是否是busy polling。|true|true, false|
    |mms.net.ipc.worker.groups|ipc work组。|1,1|每组之间数字用','分割，表示每个组有多少个worker|
    |mms.net.ipc.worker.groups.cpuset|worker CPU集合|1-1,3-3|每组之间数字用','分割，x-y表示绑定本组的worker到从x到y号cpu上，与配置项mms.net.ipc.worker.groups要对应。|
    |mms.net.request.executor.thread.num|网络请求线程数。|8|[8, 256]|
    |mms.net.request.executor.queue.size|网络请求队列大小。|1024|[1024 , 65535]|
    |mms.net.publisher.worker.cpuset|组播中发送者的cpu组。|10-17|x-y，表示绑定工作线程到从x到y号cpu上，建议cpu数量与集群中节点数量一致。|
    |mms.net.subscriber.worker.cpuset|组播中接收者的cpu组。|18-18|x-y，表示绑定工作线程到从x到y号cpu上，建议只配一个cpu来接收即可。|
    |mms.net.message.max_buff_size|单次发送消息的最大buff的大小。单位: KB|70|1-4096，建议不要超过256，不然高并发下通信的内存块不够用。|
    |TLS|mms.net.tls.enable|tls开关|true|truefalse|
    |mms.net.tls.certification.path|服务端证书文件路径。|-|安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。|
    |mms.net.tls.ca.cert.path|CA证书文件路径。|-|安全开关打开则需要为有效路径，安全开关关闭则不解析该配置项。|
    |mms.net.tls.ca.crl.path|吊销列表文件路径。|-|可以为空，不为空时，安全开关打开且需要校验证书是否被吊销时为有效路径，安全开关关闭则不解析该配置项。|
    |mms.net.tls.private.key.path|服务端证书私钥文件路径。|-|安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。|
    |mms.net.tls.private.key.password.path|工作证书私钥口令的密文的文件路径。|-|安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。在加密私钥的时候，私钥口令建议满足复杂度要求。同时满足以下要求：口令长度至少8个字符。口令需要包含如下至少两种字符的组合。至少一个小写字母至少一个大写字母至少一个数字至少一个特殊字符：`~!@#$%^&*()-_=+\|[{}];:'"",<.>/?  和空格|
    |mms.net.tls.decrypter.lib.path|安全解密函数so文件路径。|-|安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。|
    |mms.net.tls.openssl.lib.path|openssl so文件所在目录路径。|-|为空时，使用系统路径下的so文件。不为空时，安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。|
    |Cluster manager|mms.cm.node.num|集群节点数。|8|[1, 8]|
    |mms.cm.node.id|集群节点ID编号。|0xFFFFUL|[0, 0xFFFFUL]，该配置项默认不用配置，集群自行生成递增的节点号。|
    |mms.cm.register_timeout_sec|集群注册超时时间（秒）。|10|[10, 60]|
    |mms.cm.zk_host|集群Zookeeper地址。|127.0.0.1:2181|*.*.*.*:#，*为0 ~ 255，#为0 ~ 65535|

### 开启TLS认证

#### 开启Server端TLS认证-融合部署

**注意事项**

- 如需开启TLS认证，则MMS集群中的所有计算节点均需开启TLS认证。
- 安装部署完成后，需手动删除安装过程中用于集群节点间通信的公钥。
- 生成加密口令之前建议关闭系统历史记录功能。口令生成后可重新启用该功能。

    用户导入的私钥需要进行加密。私钥口令需要使用提供的工具加密，否则会有安全风险。

    证书安全要求：

    - 需使用业界公认安全可信的非对称加密算法、密钥长度、Hash算法、证书格式等。
    - 应处于有效期内。

**前提条件**

MMS已经安装成功。获取TLS认证需要的文件，如下表所示。

**表 1**  开启Server端TLS认证所需文件列表

|文件|说明|
|--|--|
|CA文件|一个自签名的证书，可以签发其它证书。格式为：PEM（*.pem）。|
|吊销证书列表文件|给出吊销证书列表文件，格式为：PEM（*.crl）。可选，如无吊销证书，可以没有此文件。|
|Server端的证书|由CA签发的证书，保证在有效期内。格式为：PEM chain（*.pem）。|
|Server端的证书对应的已加密私钥文件|要与Server端证书对应，Server安装用户要知道这个私钥文件的口令。格式为：PEM encrypted（*.pem）。|
|Server端的私钥口令|加密后的私钥口令存储文件，口令长度不超过10000字节。|
|Server端的解密函数crypto so文件|可选，配置则使用用户提供的版本。|
|openssl so文件|可选，配置则使用用户提供的版本。|

**表 2**  文件对应配置项

|文件|server对应配置项|
|--|--|
|CA文件|mms.net.tls.ca.cert.path|
|吊销证书列表文件|mms.net.tls.ca.crl.path|
|Server端的证书|mms.net.tls.certification.path|
|Server端的证书对应的已加密私钥文件|mms.net.tls.private.key.path|
|Server端的私钥口令|mms.net.tls.private.key.password.path|
|Server端的解密函数crypto.so文件|mms.net.tls.decrypter.lib.path|
|openssl so文件|mms.net.tls.openssl.lib.path|

#### 开启Client端TLS认证-分离部署

**注意事项**

- 分离部署时才需要此步骤，TLS开关（tlsEnable）由用户传入，建议用户开启TLS认证，MMS所有节点的TLS认证开启和关闭保持统一。
- 集群中所有的Client端和Server端需要同步开启或关闭TLS认证，否则会连接失败。
- 多用户访问UBS IO服务时，每个用户使用的证书可以是不同的，但需要满足都由同一个CA签发。

**前提条件**

MMS已经安装成功。获取TLS认证需要的文件，如下表所示。

**表 1**  开启Client端TLS认证所需文件列表

|文件|说明|对应配置项|
|--|--|--|
|CA文件|一个自签名的证书，可以签发其它证书。格式为：PEM（*.pem）。|caCerPath|
|吊销证书列表文件|给出吊销证书列表文件，格式为：PEM（*.crl）。可选，如无吊销证书，可以没有此文件。|caCrlPath|
|Client端的证书|由CA签发的证书，保证在有效期内。格式为：PEM chain（*.pem）。|certificationPath|
|Client端的证书对应的私钥文件|要与Client端证书对应，安装用户要知道这个私钥文件的口令。格式为：PEM encrypted（*.pem）。|privateKeyPath|
|Client端的私钥口令|加密后的私钥口令存储文件，口令长度不超过10000字节。|privateKeyPasswordPath|
|Client端的解密函数crypto so文件|可选，配置则使用用户提供的版本。|decrypterLibPath|
|openssl so文件|可选，配置则使用用户提供的版本。|opensslLibDir|

## 启动软件 

### 配置RDMA无损

软件安装环境上如配置有RoCE网卡，且预设MMS网络协议使用RDMA，则首先需要在环境上配置RDMA无损参数，防止数据通信过程中报错，具体配置方法请参考各网卡厂商的官方RDMA使用说明书。

### 启动MMS

#### 融合部署模式

融合部署模式MMS没有单独的进程，MMS进程与用户进程一起，需要用户链接libmms\_server.so。

1. 修改mms.conf配置文件，"mms.deployment.mode"配置项改为"converge"。
2. 链接libmms\_server.so，调用MmsInitialize函数启动服务。

#### 分离部署模式

分离部署模式MMS会有一个独立的server进程，server端的配置从mms.conf里读取，client端在用户进程里，需要用户链接libmms\_client.so。

1. 修改mms.conf配置文件，"mms.deployment.mode"配置项改为"separate";
2. 启动server端进程，"mmsd &";
3. 链接libmms\_client.so，调用MmsInitialize函数启动client端服务。

## 卸载软件

使用rpm -e ubs-io-memstore-\{version\}.oe2403sp3.aarch64.rpm卸载。
