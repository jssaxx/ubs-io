# 前言<a name="ZH-CN_TOPIC_0000002521860660"></a>

**概述<a name="section4537382116410"></a>**

本文档描述了UBS IO的特性指南，包括安装、启动、安全加固、卸载、升级和故障处理等内容。

**读者对象<a name="section4378592816410"></a>**

本文档主要适用于以下工程师：

-   技术支持工程师
-   二次开发工程师
-   维护工程师

**符号约定<a name="section133020216410"></a>**

在本文中可能出现下列标志，它们所代表的含义如下。

|**符号**|**说明**|
|--|--|
|![](figures/zh-cn_image_0000002552860643.png)|表示如不避免则将会导致死亡或严重伤害的具有高等级风险的危害。|
|![](figures/zh-cn_image_0000002552740639.png)|表示如不避免则可能导致死亡或严重伤害的具有中等级风险的危害。|
|![](figures/zh-cn_image_0000002521700690.png)|表示如不避免则可能导致轻微或中度伤害的具有低等级风险的危害。|
|![](figures/zh-cn_image_0000002521860682.png)|用于传递设备或环境安全警示信息。如不避免则可能会导致设备损坏、数据丢失、设备性能降低或其它不可预知的结果。“须知”不涉及人身伤害。|
|![](figures/zh-cn_image_0000002552860645.png)|对正文中重点信息的补充说明。“说明”不是安全警示信息，不涉及人身、设备及环境伤害信息。|


**修改记录<a name="section2467512116410"></a>**

|**文档版本**|**发布日期**|**修改说明**|
|--|--|--|
|01|2026-03-30|第一次正式发布。|


# 特性描述<a name="ZH-CN_TOPIC_0000002552740605"></a>

本文档主要介绍BeiMing IO加速套件UBS IO的关键特性，旨在帮助特性使用者快速了解UBS IO的整体软件架构和应用场景，熟练掌握其软件的安装部署方法，清楚软件可靠性规格。





## 概述<a name="ZH-CN_TOPIC_0000002552740625"></a>

本文档主要介绍BeiMing IO加速套件UBS IO的关键特性，旨在帮助特性使用者快速了解UBS IO的整体软件架构和应用场景，熟练掌握其软件的安装部署方法，清楚软件可靠性规格。

UBS IO特性指南主要内容归纳总结如下：

-   软件整体架构
-   使能应用场景
-   安装部署指导
-   可靠性规格

## 应用场景<a name="ZH-CN_TOPIC_0000002552740603"></a>

随着互联网大数据应用、云原生业务和AI融合应用的快速发展与落地，数据呈爆发式增长。传统的存算一体架构面临横向扩展困难，同时在云化趋势下数据资源共享度变低，因此存算分离架构成为未来发展趋势。但存算分离架构会导致计算侧的应用跨网络访问存储侧的数据，使得业务IO性能降低，并且大规模的计算节点部署也会导致计算侧的资源利用率降低。

BeiMing UBS IO基于华为鲲鹏计算平台构建了计算侧高性能分布式读写缓存，再结合开源JuiceFS的广泛应用生态和优秀的北向兼容性，着力解决大数据领域和AI融合领域在存算分离架构下的性能损失问题。

当前大数据领域广泛应用的Spark计算引擎在存算分离架构下存在数据集加载慢的性能瓶颈，AI融合领域的大模型应用在存算分离架构下存在数据集加载慢和Checkpoint写入慢的性能瓶颈。UBS IO通过以下方式突破性能瓶颈：

-   UBS IO基于计算侧的内存介质和高速磁盘构建多级分布式写缓存，结合RDMA高速网络和多副本冗余机制保证数据高可靠，让应用IO集中在计算侧缓存，从而降低数据写时延。
-   UBS IO创新性地采用读写缓存独立架构设计，可以带来缓存资源独立配置、淘汰策略灵活配置、资源使用互不影响等优势。
-   UBS IO采用分布式读缓存叠加数据智能预取和冷热识别，可以保证热、温数据尽可能缓存在计算侧的内存和高速磁盘介质上，冷数据存储在后端大容量存储集群中，其目的是提高缓存命中率，降低应用数据读取时延。

综上所述，UBS IO可以很好的解决大数据领域和AI融合领域的性能瓶颈问题，提升端到端应用性能表现。

**图 1**  场景说明<a name="fig1920319571095"></a>  
![](figures/场景说明.png "场景说明")

## 整体架构<a name="ZH-CN_TOPIC_0000002552740609"></a>

**图 1**  UBS IO整体架构设计<a name="fig78661457194015"></a>  
![](figures/UBS-IO整体架构设计.png "UBS-IO整体架构设计")

上述架构模型视图中的逻辑元素如[表1](#table567036182612)描述所示。

**表 1**  逻辑元素

|模块名中文名|模块英文名|详细描述|
|--|--|--|
|缓存客户端|SDK|提供C版本的对外API，UBS IO分布式缓存访问入口，负责实例管理、网络资源管理、节点/分区视图管理和流量控制等功能。|
|数据镜像模块|Mirror|负责数据多副本冗余管理，缓存对象请求分发等功能。|
|写缓存模块|WriteCache|负责写缓存对象数据、索引元数据和淘汰策略的管理功能，提供数据回写和透写模式。|
|读缓存模块|ReadCache|负责读缓存对象数据、索引元数据和淘汰策略的管理功能，提供对象数据预取功能。|
|流式空间模块|Flow|提供无限长的逻辑线性空间的申请和释放接口，支持数据Append方式写入。|
|内存空间管理模块|MM|负责用于缓存的内存空间按照Block粒度进行管理，支持内存注册到RDMA和Shared Memory。|
|磁盘块设备管理模块|BDM|负责用于缓存的磁盘块设备空间按照Block粒度进行管理，提供同步/异步数据读写功能。|
|后端存储管理模块|UFS|管理多种后端存储系统，对上提供统一的数据访问接口，屏蔽后端存储系统差异。|
|集群管理模块|CM|基于开源ZooKeeper提供缓存集群管理功能，负责状态监控、分区视图计算和故障处理等功能。|


## 关键技术<a name="ZH-CN_TOPIC_0000002521860646"></a>

UBS IO作为计算侧分布式缓存层，架构方案设计上既要考虑业务性能目标，也要考虑缓存数据可靠性、集群可扩展性等高可用指标，因此UBS IO采用分区视图方案发挥其集群分布式系统能力。


### 分区视图技术方案<a name="ZH-CN_TOPIC_0000002521700664"></a>

UBS IO作为计算侧分布式缓存层，架构方案设计上既要考虑业务性能目标，也要考虑缓存数据可靠性、集群可扩展性等高可用指标，因此UBS IO采用分区视图方案发挥其集群分布式系统能力。

-   分区视图主要作用：
    -   副本管理：分区会关联到副本信息，当前版本支持双副本冗余，每个副本又会关联两级缓存介质，分别为内存和磁盘。
    -   数据均衡：缓存客户端根据分区视图和负载均衡算法，将业务下发的请求分发到集群中各个节点上去执行，可以实现每个节点的业务负载、缓存资源利用率是均衡的，避免出现单点瓶颈问题。
    -   线性扩展：通过分区视图重计算/变更可以做到节点扩容后能够接近线性地扩展性能。
    -   故障处理：分区视图能够标记各个节点上的缓存状态。根据缓存状态做出相应的故障容错处理，保证业务连续性。

-   分区视图设计原理：
    -   分区数量在集群初始化时读取配置项后就固定不变，解决扩容、节点/进程永久故障场景下导致的全局均衡策略失效。
    -   分区副本信息在集群启动后根据集群节点视图、汇总的每个节点的磁盘信息，再叠加负载均衡算法就生成了初始的分区视图，确保每个节点或每块磁盘都承载均匀的分区数量。
    -   永久故障和扩容场景都会导致分区视图重计算，重计算方法需要遵循数据可靠性原则、数据最小迁移原则和负载均衡原则。

### 缓存策略技术方案<a name="ZH-CN_TOPIC_0000002521700656"></a>

不同的应用模型、同一应用不同的业务流程对于数据读写的性能要求都不尽相同，传统的分布式文件系统虽然支持通过参数配置来提供多种策略满足不同的业务需求，但是随着虚拟化、云原生应用的不断发展，对于分布式文件系统支持定制化的需求也不断高涨。

UBS IO采用多实例和软隔离技术支持文件/目录粒度的缓存策略个性化配置，对外提供数据写入策略、数据冗余度、缓存资源、数据持久化策略等参数配置来满足多样化的业务场景，有效提升了端到端整体应用性能。

**图 1**  缓存策略可配置<a name="fig111410156175"></a>  
![](figures/缓存策略可配置.png "缓存策略可配置")

### 数据管理技术方案<a name="ZH-CN_TOPIC_0000002521860656"></a>

为了追求应用数据在不同I/O粒度、随机I/O模型和磁盘读写下的高性能，UBS IO创新性地在缓存层的多级数据布局上采用流式/线性数据存储方式，主要解决不同I/O大小带来的缓存空间浪费问题，同时减少随机I/O带来的频繁磁盘寻址导致的写入时延增大等性能瓶颈问题。流式数据管理方案的核心思想是提供一个逻辑地址无限大的线性空间，当数据写入的时候首先从线性空间中向后递增地分配写入偏移，然后以append形式将数据追加写入到线性空间中。当前流式数据管理支持使用内存和NVMe SSD磁盘介质作为缓存空间。

**图 1**  流式数据管理<a name="fig7727049124813"></a>  
![](figures/流式数据管理.png "流式数据管理")

## 部署方式<a name="ZH-CN_TOPIC_0000002552860621"></a>

大数据应用和智算AI应用在不同的场景下具有较大的部署和组网差异，并且结合现网中的集群部署方式，对软件在安装部署方面提出了更高的要求。UBS IO分布式缓存层整体设计采用C/S架构，对外提供灵活多样的安装部署方式，其部署方式可以大致分为节点裸机部署和容器/虚拟机部署两类，每类又可细分为融合部署、分离部署和独立部署三种，此处以节点裸机部署为例详细介绍三种部署方式。

**图 1**  融合部署<a name="fig1895841541014"></a>  
![](figures/融合部署.png "融合部署")

**图 2**  分离部署<a name="fig738018111175"></a>  
![](figures/分离部署.png "分离部署")

**图 3**  独立部署<a name="fig147331725116"></a>  
![](figures/独立部署.png "独立部署")

## 约束限制<a name="ZH-CN_TOPIC_0000002521700646"></a>

使用UBS IO作为计算侧缓存加速存在以下约束限制，请在使用过程中注意：

-   仅支持在华为鲲鹏计算平台上运行。
-   仅支持软件离线升级，不支持在线升级。
-   UBS IO集群规模的规格为\[2,256\]，即支持最小配置两个计算节点，最大配置256个计算节点。
-   UBS IO的缓存介质规格为内存加NVMe SSD磁盘，即单个计算节点需要配置内存和NVMe SSD磁盘给UBS IO作为数据缓存介质空间。
-   UBS IO的缓存资源大小规格为内存资源大小满足大于或等于50GB，磁盘资源大小满足至少配置一块NVMe SSD，且磁盘容量为大于或等于3.6TB。
-   UBS IO的后端存储系统规格为支持Ceph和HDFS这两种后端存储系统。
-   UBS IO在安装部署过程中需要用户在执行节点上配置安装账户，由用户保证该账户的安全性。
-   UBS IO POSIX桥接加速库限制在研发内部调试应用极限性能使用，由于桥接的接口不完备和安全条件不满足而不具备商用条件。

# 软件安装<a name="ZH-CN_TOPIC_0000002521860664"></a>




## 环境要求<a name="ZH-CN_TOPIC_0000002552860603"></a>

安装部署UBS IO软件前，请预先检查物理环境是否满足要求、依赖软件是否已经安装成功、安装的软件版本是否满足特性要求。前置环境要求是确保安装部署操作成功和后续应用程序正常执行的先决条件。

**硬件要求<a name="section10915213184318"></a>**

UBS IO软件的安装部署程序均在计算节点上执行，集群中各个计算节点的硬件要求如[表1](#zh-cn_topic_0000001713099729___d0e1171)所示。

**表 1**  硬件配套要求

<a name="zh-cn_topic_0000001713099729___d0e1171"></a>
<table><tbody><tr id="zh-cn_topic_0000002511959332_row415mcpsimp"><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.1.1"><p id="zh-cn_topic_0000002511959332_p417mcpsimp"><a name="zh-cn_topic_0000002511959332_p417mcpsimp"></a><a name="zh-cn_topic_0000002511959332_p417mcpsimp"></a>服务器名称</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000002511959332_p419mcpsimp"><a name="zh-cn_topic_0000002511959332_p419mcpsimp"></a><a name="zh-cn_topic_0000002511959332_p419mcpsimp"></a>TaiShan 200服务器</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002511959332_row420mcpsimp"><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.2.1"><p id="zh-cn_topic_0000002511959332_p422mcpsimp"><a name="zh-cn_topic_0000002511959332_p422mcpsimp"></a><a name="zh-cn_topic_0000002511959332_p422mcpsimp"></a>处理器</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.2.1 "><p id="zh-cn_topic_0000002511959332_p424mcpsimp"><a name="zh-cn_topic_0000002511959332_p424mcpsimp"></a><a name="zh-cn_topic_0000002511959332_p424mcpsimp"></a>鲲鹏920处理器</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002511959332_row425mcpsimp"><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.3.1"><p id="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p751mcpsimp"><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p751mcpsimp"></a><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p751mcpsimp"></a>内存大小</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.3.1 "><p id="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p753mcpsimp"><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p753mcpsimp"></a><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p753mcpsimp"></a>512GB</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002511959332_row430mcpsimp"><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.4.1"><p id="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p760mcpsimp"><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p760mcpsimp"></a><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p760mcpsimp"></a>内存频率</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.4.1 "><p id="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p762mcpsimp"><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p762mcpsimp"></a><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p762mcpsimp"></a>2666MHz</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002511959332_row21191339193716"><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.5.1"><p id="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p769mcpsimp"><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p769mcpsimp"></a><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p769mcpsimp"></a>网卡</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.5.1 "><a name="zh-cn_topic_0000002511959332_ul106297346414"></a><a name="zh-cn_topic_0000002511959332_ul106297346414"></a><ul id="zh-cn_topic_0000002511959332_ul106297346414"><li>RoCE 100GE</li><li>TCP 10GE</li></ul>
</td>
</tr>
<tr id="zh-cn_topic_0000002511959332_row15874510370"><th class="firstcol" valign="top" width="30%" id="mcps1.2.3.6.1"><p id="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p778mcpsimp"><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p778mcpsimp"></a><a name="zh-cn_topic_0000002511959332_zh-cn_topic_0000001713099729_p778mcpsimp"></a>硬盘（NVMe SSD）</p>
</th>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.6.1 "><p id="zh-cn_topic_0000002511959332_p1150713361232"><a name="zh-cn_topic_0000002511959332_p1150713361232"></a><a name="zh-cn_topic_0000002511959332_p1150713361232"></a>至少一块3.6TB或7.68TB磁盘</p>
</td>
</tr>
</tbody>
</table>

**软件要求<a name="section5999819104313"></a>**

UBS IO软件安装前需要将前置依赖的软件安装成功，建议参考各软件安全标准规范安装，集群中各节点的操作系统和软件要求如[表2](#zh-cn_topic_0000001664980070_table564mcpsimp)所示，以下软件不在交付范围。

**表 2**  软件要求

|软件名称|软件版本|
|--|--|
|OS|openEuler 22.03 LTS SP3|
|JuiceFS|1.0.3|
|Redis|4.0.11|
|ZooKeeper|3.9.3|
|Ceph|12.2.8|
|Python|3.7|


## 安装UBS IO<a name="ZH-CN_TOPIC_0000002552740595"></a>

-   UBS IO分为分离部署、融合部署、独立部署场景，融合部署场景使用上层调用组件用户（例如：juiceadmin:juicegroup）统一安装UBS IO Server和SDK，分离部署和独立部署场景需要创建Server端用户（例如：bioadmin:biogroup）安装UBS IO Server，使用上层调用组件用户安装UBS IO SDK。应禁止使用root用户安装，使用root等特权账号运行程序时，如果程序遭到入侵攻击，攻击者可以利用该程序的高级运行权限来对整个系统造成危害。
-   Ceph和HDFS通信由用户配置，建议用户使用安全通信链路保证通信安全。

**创建UBS IO Server运行用户<a name="section14564105184514"></a>**

请确保所有物理机（存储节点、管理节点、计算节点）和容器内，组biogroup的GID、用户bioadmin的UID没有被占用，如果被占用可能会导致服务不可用。

-   biogroup的GID为1000。
-   bioadmin的UID为9000。
-   如果设置bioadmin用户的密码，复杂度要求如下：
    -   口令长度至少8个字符。
    -   口令需要包含如下至少三种字符的组合：
        -   小写字母。
        -   大写字母。
        -   数字。
        -   特殊字符：\`\~!@\#$%^&\*\(\)-\_=+\\|\[\{\}\];:'",<.\>/?和空格。

    -   口令和账号不能相同。

在UBS IO组件安装的节点上执行以下命令创建用户。

1.  创建biogroup用户组。

    ```
    groupadd -g 1000 biogroup
    ```

2.  在biogroup用户组内创建bioadmin用户。

    ```
    useradd -g 1000 -d /home/bioadmin -u 9000 -m -s /bin/bash bioadmin
    ```

**（可选）清理环境<a name="section24361820142511"></a>**

>![](public_sys-resources/icon-notice.gif) **须知：**
>-   安装前需确保该环境没有安装UBS IO。若环境上已安装UBS IO。则需要进行环境清理操作，确保后续安装能够正常进行。
>-   建议用户及时清理SDK端未使用的日志文件，避免磁盘耗尽。
>-   统计文件SDK端最大10MB，Server端最大50MB，循环进行计数统计，UBS IO重新部署启动后会生成一个新的统计文件，老的统计文件建议清理。

1.  收集需要安装UBS IO节点的IP地址。
2.  为集群中每个节点提供至少一块NVMe SSD存储盘，并将磁盘归属设置为对应安装用户、用户组。

    ```
    chown [Server安装用户:Server安装用户组] /dev/nvmexnx
    ```

3.  首次安装时需创建下列目录并配置权限。

    **表 1**  目录权限配置信息

|目录地址|用户和用户组|权限|备注|
|--|--|--|--|
|/var/log/boostio|Server安装用户：Server安装用户组|750|UBS IO Server端日志目录。|
|/var/log/boostio/trace|Server安装用户：Server安装用户组|750|UBS IO统计日志目录。|
|sdk初始化函数中的日志路径|SDK安装用户：SDK安装用户组|750|UBS IO SDK端日志目录。|


4.  配置Ceph密钥环权限。

    >![](public_sys-resources/icon-note.gif) **说明：**
    >UBS IO启动需要读取Ceph Client端密钥，因此需要配置对应的权限，详情请参见[Ceph官网](https://docs.ceph.com/en/latest/rados/configuration/auth-config-ref/#keys)。

5.  在ZooKeeper Server节点上执行命令清理UBS IO集群信息（以ZooKeeper 3.9.3版本为例）

    ```
    sh /install_path/apache-zookeeper-3.8.1-bin/bin/zkCli.sh
    >>deleteall /cm
    ```

6.  清理UBS IO磁盘管理元数据信息。

    ```
    dd bs=8k count=1024 if=/dev/zero of=/dev/nvmexnx
    ```

**安装UBS IO<a name="section069831315397"></a>**

1.  登录至任意节点，并上传软件包ubs\_io-boostio-1.0.0-1.oe2203sp3.aarch64.rpm至任意可用目录下。
2.  安装配套的ubs-comm，libboundscheck。如果使用yum安装ubs-io，则该步骤可以跳过，yum自动安装（暂未支持）。
3.  安装软件包

    ```
    rpm -ivh ubs_io-boostio-1.0.0-1.oe2203sp3.aarch64.rpm
    ```

    安装后的目录结构参照[表2](#table6587201602818)。

    **表 2**  软件包目录结构

|目录|说明|
|--|--|
|/usr/bin|可执行文件。|
|/usr/lib64|动态库和静态库文件。|
|/etc/boostio|配置文件。|


    **表 3**  bin目录文件说明

|目录|文件名称|描述|
|--|--|--|
|/usr/bin|bio_daemon|UBS IO服务可执行文件。|


    **表 4**  lib目录文件说明

|目录|文件名称|描述|
|--|--|--|
|/usr/lib|libbio_interceptor_server.so|桥接服务共享对象文件。|
|libbio_server.so|UBS IO Server端共享对象文件。|
|libbio_sdk.so.1.0.0|UBS IO SDK端共享对象文件。|
|libbio_sdk.so.1|UBS IO SDK端共享对象文件软连接。|
|libock_interceptor.so|桥接服务共享对象文件。|
|libock_iofwd_proxy.so|桥接服务共享对象文件。|


4.  将zookeeper client需要的so文件"libzookeeper\_mt.so"拷贝到bio用户有权限读取的路径，并将路径添加到LD\_LIBRARY\_PATH中。
5.  配置安装信息。

    根据业务使用情况和待安装部署的环境设置“/etc/boostio”目录下bio.conf中的相关配置项，具体配置项说明如[表5](#table8874183794213)所示。

    **表 5**  UBS IO配置项

|归属模块|配置项名称|简要描述|默认值|合法值/区间|注意事项|
|--|--|--|--|--|--|
|Log|bio.log.level|日志打印级别。|info|debuginfowarntraceerror|-|
|Net|bio.net.data.ip_mask|IP地址段。|127.0.0.1/24|*.*.*.*/#，其中*为0 ~ 255，#为0 ~ 32|使用JuiceFS跑大数据业务时，该字段需要和/etc/hosts中的主机名对应的IP保持一致。|
|bio.net.data.listen_port|业务面网络通信端口号。|7201|7201 ~ 7800|-|
|bio.net.data.protocol|网络协议。|tcp|rdmatcp|-|
|bio.net.rpc.data.busy_polling_mode|RPC开启busy-polling标记。|false|truefalse|仅RDMA协议生效。|
|bio.net.rpc.data.workers_count|RPC数据面工作核数。|4|1 ~ 16|-|
|bio.net.request.executor.thread.num|接收端请求处理线程数。|8|8 ~ 256|-|
|bio.net.request.executor.queue.size|接收端请求处理队列深度。|1024|1024 ~ 65535|-|
|bio.net.ipc.data.busy_polling_mode|IPC开启busy-polling标记。|false|truefalse|-|
|bio.net.ipc.data.workers_count|IPC数据面工作核数。|4|1 ~ 128|-|
|bio.net.tls.enable.switch|网络安全开关。|true|truefalse|关闭后可能会引入信息安全问题、仿冒等风险，请谨慎操作。分离部署时调用UBS IO服务初始化接口传入的enableTls参数需要和该配置项保持一致。|
|bio.net.tls.ca.cert.path|CA证书文件路径。|/path/CA/cacert.pem|默认值仅作为示例。|安全开关打开则需要为有效路径，安全开关关闭则不解析该配置项。|
|bio.net.tls.ca.crl.path|吊销列表文件路径。|-|-|可以为空，不为空时，安全开关打开且需要校验证书是否被吊销时为有效路径，安全开关关闭则不解析该配置项。|
|bio.net.tls.server.cert.path|服务端证书文件路径。|/path/server/servercert.pem|默认值仅作为示例。|安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。|
|bio.net.tls.server.key.path|服务端证书私钥文件路径。|/path/server/serverkey.pem|默认值仅作为示例。|安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。|
|bio.net.tls.server.key.pass.path|工作证书私钥口令的密文的文件路径。|/path/server/server.keypass|默认值仅作为示例。|安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。在加密私钥的时候，私钥口令建议满足复杂度要求。同时满足以下要求：口令长度至少8个字符。口令需要包含如下至少两种字符的组合。至少一个小写字母至少一个大写字母至少一个数字至少一个特殊字符：`~!@#$%^&*()-_=+\|[{}];:'"",<.>/?  和空格|
|bio.net.tls.server.decrypter.lib.path|安全解密函数so文件路径。|/path/libdecrypt.so|默认值仅作为示例。|安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。|
|bio.net.tls.server.ssl.lib.dir|openssl so文件所在目录路径。|-|-|为空时，使用系统路径下的so文件。不为空时，安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。|
|Cache|bio.cache.qos.enable|流量控制开关。|true|falsetrue|流量控制开关打开会影响到极限性能，建议性能用例场景关闭。|
|bio.data.crc.enable|数据完整性校验开关。|false|falsetrue|数据完整性校验开关打开会增加数据读写时延，建议在问题定位场景使用。|
|bio.segment.size_in_mb|缓存资源粒度。|4|1 ~ 16|单位MB。|
|bio.mem.size_in_gb|缓存资源内存容量。|50|0 ~ 512|禁止配置超过系统内存。单位GB。配置为0表示该节点不具备缓存功能。|
|bio.disk.path|缓存资源磁盘列表。|/dev/sdxx:/dev/sdyy|-|多个磁盘路径用冒号隔开，当前版本支持最多4块磁盘。|
|bio.rcache.evict_water_level|读缓存淘汰水位。|90|0 ~ 100|表示使用读缓存百分比。|
|bio.cache.mem_read_write_ratio|内存读写资源配比。|5:5|0 ~ 10:10 ~ 0|-|
|bio.cache.disk_read_write_ratio|磁盘读写资源配比。|5:5|0 ~ 10:10 ~ 0|-|
|bio.work.scene|应用场景标记。|none|nonebigdata|可选，默认为nonenone：不存在使用约束。bigdata：大数据场景，其主要区别是IO强制对齐。|
|bio.work.io.alignsize|IO对齐数据大小。|1|1 ~ 4194304|可选，单位B。|
|bio.wcache.evict_water_level|写缓存淘汰水位。|0|0 ~ 100|可选，默认为0，表示使用写缓存资源百分比。|
|bio.wcache.negotiate.delay|淘汰协商延迟。|100|50 ~ 1000|可选，默认100，单位ms。前台写性能敏感场景需要将该值调大，淘汰延迟增大；前台写性能不敏感可使用较小值，更快淘汰。|
|bio.trace.enable|流程统计开关。|true|falsetrue|流程统计开关打开会影响到极限性能，建议性能用例场景关闭。|
|Underfs|bio.underfs.file_system_type|后端存储系统类型。|ceph|cephhdfs|-|
|bio.underfs.ceph.cfg.path|Ceph配置文件路径。|/etc/ceph/ceph.conf|不为空。|选择ceph后必填选项，需要是真实存在的路径。|
|bio.underfs.ceph.cluster|Ceph集群名称。|ceph|不为空。|选择ceph后必填选项。|
|bio.underfs.ceph.user|Ceph用户。|client.admin|不为空。|选择ceph后必填选项。|
|bio.underfs.ceph.pool|Ceph数据池。|0:jfspool0,1:jfspool1|不为空。|选择ceph后必填选项，多个参数用英文逗号隔开。|
|bio.underfs.hdfs.name_node|hadoop的NameNode。|default:0|*.*.*.*/#，*为0 ~ 255，#为0 ~ 65535|可选，默认为default:0，格式：IP地址:端口号，表示使用hadoop配置文件中的IP地址和端口号。|
|bio.underfs.hdfs.working_path|文件在hdfs系统的存放路径。|/hdfs|路径名长度小于或等于255的合法路径。|可选，默认为/hdfs。|
|CM|bio.cm.initial.nodes_count|集群初始化期望节点数。|2|2 ~ 256|-|
|bio.cm.copy_num|数据冗余度。|2|2|当前版本仅支持双副本。|
|bio.cm.pts_count|分区视图数量。|16|2 ~ 8192|-|
|bio.cm.register_timeout_sec|ZooKeeper心跳检测超时时间。|20|10 ~ 60|单位s。|
|bio.cm.register_perm_timeout_sec|永久故障超时时间。|60|60 ~ 600|单位s。|
|bio.cm.zk_host|ZooKeeper服务节点信息。例如3节点ZK集群：127.0.0.1:2181,127.0.0.2:2181,127.0.0.3:2181。|-|不为空|ZooKeeper使用的网段需要和业务IP地址网段保持一致。|
|Prometheus|bio.prometheus.exposer|Prometheus Server的地址和端口号。|-|*.*.*.*:#，*为0 ~ 255，#为0 ~ 65535|可选|
|bio.prometheus.scrape_interval_sec|Prometheus采样频率。|15|-|可选，单位s。|


6.  设置磁盘归属用户和用户组。

    ```
    chown [Server安装用户:Server安装用户组] 盘地址1
    chown [Server安装用户:Server安装用户组] 盘地址2
    ```

7.  其他安装节点按照此流程进行。

## 开启TLS认证<a name="ZH-CN_TOPIC_0000002521860652"></a>



### 开启Server端TLS认证<a name="ZH-CN_TOPIC_0000002521700672"></a>

**注意事项<a name="section3715144012423"></a>**

-   如需开启TLS认证，则UBS IO集群中的所有计算节点均需开启TLS认证。
-   安装部署完成后，需手动删除安装过程中用于集群节点间通信的公钥。
-   生成加密口令之前建议关闭系统历史记录功能。口令生成后可重新启用该功能。

    用户导入的私钥需要进行加密。私钥口令需要使用提供的工具加密，否则会有安全风险。

    证书安全要求：

    -   需使用业界公认安全可信的非对称加密算法、密钥长度、Hash算法、证书格式等。
    -   应处于有效期内。

**前提条件<a name="zh-cn_topic_0000001775152198_section15298193810379"></a>**

UBS IO已经安装成功，本章节以安装目录“/opt“为例进行描述。获取TLS认证需要的文件，如[表1](#zh-cn_topic_0000001775152198_table2936153410819)所示。

**表 1**  开启Server端TLS认证所需文件列表

|文件|说明|
|--|--|
|CA文件|一个自签名的证书，可以签发其它证书。格式为：PEM（*.pem）。|
|吊销证书列表文件|给出吊销证书列表文件，格式为：PEM（*.crl）。可选，如无吊销证书，可以没有此文件。|
|Server端的证书|由CA签发的证书，保证在有效期内。格式为：PEM chain（*.pem）。|
|Server端的证书对应的已加密私钥文件|要与Server端证书对应，Server安装用户要知道这个私钥文件的口令。格式为：PEM encrypted（*.pem）。|
|Server端的私钥口令|加密后的私钥口令存储文件，口令长度不超过10000字节。|
|Server端的解密函数so|用户提供的包含解密函数的so。|
|openssl，crypto so文件|可选，配置则使用用户提供的版本。|


>![](public_sys-resources/icon-note.gif) **说明：**
>```
>系统会尝试加载名为配置文件中命名的动态库，并在其中查找名为`DecryptPassword`的函数
>解密函数的签名如下：
>int DecryptPassword(const char* cipherText, const size_t cipherTextLen, char *plainText, size_t *plainTextLen);
>参数说明：
>- `cipherText`: 加密的文本（输入）
>- `cipherTextLen`: 加密文本的长度（输入） 注: 长度不超过10000
>- `plainText`: 解密文本缓冲区（输出）
>- `plainTextLen`: 解密缓冲区大小（输出）
>返回值：
>- 0: 成功
>- 非0: 失败
>```

**操作步骤<a name="section680517113144"></a>**

1.  修改bio.conf配置文件，打开安全开关，相关配置文件写入到配置文件中的相应选项。

    >![](public_sys-resources/icon-note.gif) **说明：**
    >如果有吊销证书列表文件，也需要将其放到Server目录下，并将其路径写入配置文件中的bio.net.tls.ca.crl.path项。

### 开启Client端TLS认证<a name="ZH-CN_TOPIC_0000002521860670"></a>

**注意事项<a name="section047617173434"></a>**

-   分离部署时才需要此步骤，TLS开关（enableTls）由用户传入，建议用户开启TLS认证，UBS IO所有节点的TLS认证开启和关闭保持统一。
-   集群中所有的Client端和Server端需要同步开启或关闭TLS认证，否则会连接失败。
-   多用户访问UBS IO服务时，每个用户使用的证书可以是不同的，但需要满足都由同一个CA签发。

**前提条件<a name="zh-cn_topic_0000001775152198_section15298193810379"></a>**

UBS IO已经安装成功，本章节以安装目录/opt为例进行描述。首先获取TLS认证需要的文件，如[表1](#zh-cn_topic_0000001775152198_table2936153410819)所示。

**表 1**  开启Client端TLS认证所需文件列表

|文件|说明|
|--|--|
|CA文件|一个自签名的证书，可以签发其它证书。格式为：PEM（*.pem）。|
|吊销证书列表文件|给出吊销证书列表文件，格式为：PEM（*.crl）。可选，如无吊销证书，可以没有此文件。|
|Client端的证书|由CA签发的证书，保证在有效期内。格式为：PEM chain（*.pem）。|
|Client端的证书对应的私钥文件|要与Client端证书对应，安装用户要知道这个私钥文件的口令。格式为：PEM encrypted（*.pem）。|
|Client端的私钥口令|加密后的私钥口令存储文件，口令长度不超过10000字节。|
|Client端的解密函数so|用户提供的包含解密函数的so。|
|openssl，crypto so文件|可选，配置则使用用户提供的版本。|


**操作步骤<a name="section166012331140"></a>**

1.  调用初始化接口。请参见《BeiMing 26.0.RC1 UBS IO API参考》中“BioInitialize”章节。

    ```
    TLS涉及到的参数的成员如下：
    uint8_t enable;                    // switch
    char certificationPath[PATH_MAX];  // certification path
    char caCerPath[PATH_MAX];          // caCer path
    char caCrlPath[PATH_MAX];          // caCer path
    char privateKeyPath[PATH_MAX];     // private key path
    char privateKeyPassword[PATH_MAX]; // private key password
    char decrypterLibPath[PATH_MAX];   // decrypter lib path
    char opensslLibDir[PATH_MAX];      // openssl lib dir path
    ```

# 软件启动<a name="ZH-CN_TOPIC_0000002552740613"></a>



## 配置RDMA无损<a name="ZH-CN_TOPIC_0000002552860613"></a>

软件安装环境上如配置有RoCE网卡，且预设UBS IO网络协议使用RDMA，则首先需要在环境上配置RDMA无损参数，防止数据通信过程中报错，具体配置方法请参考各网卡厂商的官方RDMA使用说明书。

## 启动UBS IO<a name="ZH-CN_TOPIC_0000002552860625"></a>




### 融合部署模式<a name="ZH-CN_TOPIC_0000002521700678"></a>

>![](public_sys-resources/icon-notice.gif) **须知：**
>分离部署和独立部署模式均可使用systemd命令启动UBS IO，如希望bio\_daemon进程被操作系统接管，在发生故障或异常情况下能够支持自动重启进行恢复，以保证业务连续性，可以使用此种方式。具体配置可查询官方资料。

融合部署模式下UBS IO不存在独立运行进程，以动态链接库的方式加载到JuiceFS进程。因此如需使用UBS IO功能，需要启动JuiceFS进程，并在进程启动过程中自动加载相应UBS IO动态链接库。

添加UBS IO的二进制链接路径，执行用户和安装用户保持一致。

```
export LD_LIBRARY_PATH=/opt/boostio/lib
```

### 分离部署模式<a name="ZH-CN_TOPIC_0000002552860617"></a>

>![](public_sys-resources/icon-notice.gif) **须知：**
>-   分离部署和独立部署模式均可使用systemd命令启动UBS IO，如希望bio\_daemon进程接受操作系统接管，在发生故障或异常情况下能够支持自动重启进行恢复，以保证业务连续性，可以使用此种方式。具体配置可查询官方资料。
>-   分离部署和独立部署模式下UBS IO都存在独立运行进程bio\_daemon，明确规定在单个物理节点上有且仅有一个bio\_daemon进程，禁止存在多个bio\_daemon进程，此误操作会导致数据不一致风险。

分离部署模式下UBS IO有独立的运行进程，要求首先启动UBS IO独立进程bio\_daemon，然后再启动JuiceFS进程。在JuiceFS进程启动过程中将会自动加载UBS IO SDK链接库。bio\_daemon进程启动方式支持手动后台启动。

用户希望自主监控bio\_daemon进程，在发生故障和异常情况下能够立即进行故障排查和定位，防止故障或异常隐藏和扩散，推荐使用此种方式，主要适用于应用开发和功能调试场景。

### 独立部署模式<a name="ZH-CN_TOPIC_0000002552740617"></a>

>![](public_sys-resources/icon-notice.gif) **须知：**
>-   分离部署和独立部署模式下UBS IO都存在独立运行进程bio\_daemon，明确规定在单个物理节点上有且仅有一个bio\_daemon进程，不允许存在多个bio\_daemon进程，此误操作会导致数据不一致风险。
>-   在不支持数据缓存功能的计算节点启动bio\_daemon独立运行进程前，需要设置bio配置文件中的缓存资源为零，配置项如下所示。
     >    ```
>    bio.mem.size_in_gb = 0
>    bio.disk.path = /path/to/disk
>    ```

独立部署模式下UBS IO有独立的运行进程，要求首先启动UBS IO独立进程bio\_daemon，然后再启动JuiceFS进程。在JuiceFS进程启动过程中将自动加载UBS IO SDK链接库。bio\_daemon进程启动方式支持手动后台启动。

用户希望自主监控bio\_daemon进程，在发生故障和异常情况下能够立即进行故障排查和定位，防止故障或异常隐藏和扩散，推荐使用独立部署方式，主要适用于应用开发和功能调试场景。

# 安全加固<a name="ZH-CN_TOPIC_0000002552860629"></a>

**设置登录会话超时时间<a name="section9551195410106"></a>**

登录会话30分钟（或更短）的时间内没有活动的情况下属于超时。

1.  登录安装UBS IO组件的节点。
2.  执行以下命令，打开“/etc/profile“文件。

    ```
    vim /etc/profile
    ```

3.  按“i”进入编辑模式，在文件尾部增加以下内容。

    ```
    export TMOUT=1800
    readonly TMOUT
    ```

4.  按“ESC”键，输入**:wq!**，按“Enter”保存并退出编辑。

**设置umask<a name="section14408201018277"></a>**

建议用户服务器的umask设置为027\~777，提高文件权限。

此处以设置umask为027为例。

1.  以root用户登录服务器，编辑“/etc/profile“文件。

    ```
    vim /etc/profile
    ```

2.  在“/etc/profile“文件末尾加上**umask 027**，保存并退出。
3.  执行如下命令使配置生效。

    ```
    source /etc/profile
    ```

**安全配置基线<a name="section1790415317257"></a>**

<a name="table18670145525219"></a>
<table><tbody><tr id="row15714115595213"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.1.1"><p id="p10714135512520"><a name="p10714135512520"></a><a name="p10714135512520"></a>所属功能域/功能</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.1.1 "><p id="p1471445555213"><a name="p1471445555213"></a><a name="p1471445555213"></a>TLS证书开关</p>
</td>
</tr>
<tr id="row37141455165217"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.2.1"><p id="p47144556526"><a name="p47144556526"></a><a name="p47144556526"></a>OM对象（可选）</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.2.1 "><p id="p471415555218"><a name="p471415555218"></a><a name="p471415555218"></a>NA</p>
</td>
</tr>
<tr id="row5714195545211"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.3.1"><p id="p271419551528"><a name="p271419551528"></a><a name="p271419551528"></a>配置参数（可选）</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.3.1 "><p id="p19714125519523"><a name="p19714125519523"></a><a name="p19714125519523"></a>NA</p>
</td>
</tr>
<tr id="row10714145555211"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.4.1"><p id="p1371415505218"><a name="p1371415505218"></a><a name="p1371415505218"></a>规则分类（支持定制）</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.4.1 "><p id="p16714185519528"><a name="p16714185519528"></a><a name="p16714185519528"></a>证书管理</p>
</td>
</tr>
<tr id="row1714155105211"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.5.1"><p id="p16714175595218"><a name="p16714175595218"></a><a name="p16714175595218"></a>规则分类ID</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.5.1 "><p id="p87145553526"><a name="p87145553526"></a><a name="p87145553526"></a>NA</p>
</td>
</tr>
<tr id="row14714105585214"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.6.1"><p id="p127141655105214"><a name="p127141655105214"></a><a name="p127141655105214"></a>规则子类（支持定制）</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.6.1 "><p id="p137141355135218"><a name="p137141355135218"></a><a name="p137141355135218"></a>TLS证书认证</p>
</td>
</tr>
<tr id="row47141555185220"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.7.1"><p id="p177153552529"><a name="p177153552529"></a><a name="p177153552529"></a>规则子类ID</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.7.1 "><p id="p187158554529"><a name="p187158554529"></a><a name="p187158554529"></a>NA</p>
</td>
</tr>
<tr id="row177151355145211"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.8.1"><p id="p9715105515528"><a name="p9715105515528"></a><a name="p9715105515528"></a>规则名称</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.8.1 "><p id="p97159557526"><a name="p97159557526"></a><a name="p97159557526"></a>启用TLS认证</p>
</td>
</tr>
<tr id="row37152551524"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.9.1"><p id="p15715145595215"><a name="p15715145595215"></a><a name="p15715145595215"></a>规则ID</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.9.1 "><p id="p27151555185211"><a name="p27151555185211"></a><a name="p27151555185211"></a>NA</p>
</td>
</tr>
<tr id="row1671516555528"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.10.1"><p id="p3715155514522"><a name="p3715155514522"></a><a name="p3715155514522"></a>风险等级</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.10.1 "><p id="p13715135513528"><a name="p13715135513528"></a><a name="p13715135513528"></a>中</p>
</td>
</tr>
<tr id="row167151552526"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.11.1"><p id="p5715955205217"><a name="p5715955205217"></a><a name="p5715955205217"></a>规则描述</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.11.1 "><p id="p37150554525"><a name="p37150554525"></a><a name="p37150554525"></a>开启后，集群中所有的Client端和Server端需要同步开启或关闭TLS认证，否则会连接失败。同时UBS IO集群中的所有计算节点均需开启TLS认证。</p>
</td>
</tr>
<tr id="row18715175513529"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.12.1"><p id="p77152055175218"><a name="p77152055175218"></a><a name="p77152055175218"></a>风险描述</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.12.1 "><p id="p57151155175214"><a name="p57151155175214"></a><a name="p57151155175214"></a>不开启TLS，网络通信数据未加密容易泄露。</p>
</td>
</tr>
<tr id="row571575515211"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.13.1"><p id="p12715155185213"><a name="p12715155185213"></a><a name="p12715155185213"></a>修复影响</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.13.1 "><p id="p11715115510529"><a name="p11715115510529"></a><a name="p11715115510529"></a>开启之后通信通道数据加密传输。</p>
</td>
</tr>
<tr id="row271525513527"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.14.1"><p id="p871511553523"><a name="p871511553523"></a><a name="p871511553523"></a>取值范围</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.14.1 "><p id="p371515575215"><a name="p371515575215"></a><a name="p371515575215"></a>[true,false]</p>
</td>
</tr>
<tr id="row57151155155218"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.15.1"><p id="p2715195545213"><a name="p2715195545213"></a><a name="p2715195545213"></a>安全推荐值</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.15.1 "><p id="p1371535514521"><a name="p1371535514521"></a><a name="p1371535514521"></a>TRUE</p>
</td>
</tr>
<tr id="row1171519554526"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.16.1"><p id="p571515516524"><a name="p571515516524"></a><a name="p571515516524"></a>缺省值</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.16.1 "><p id="p207152559523"><a name="p207152559523"></a><a name="p207152559523"></a>TRUE</p>
</td>
</tr>
<tr id="row18715135575216"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.17.1"><p id="p671665513528"><a name="p671665513528"></a><a name="p671665513528"></a>修复建议</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.17.1 "><p id="p16716855145210"><a name="p16716855145210"></a><a name="p16716855145210"></a>无</p>
</td>
</tr>
<tr id="row167161355125215"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.18.1"><p id="p5716155545210"><a name="p5716155545210"></a><a name="p5716155545210"></a>是否必选项</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.18.1 "><p id="p1571675516528"><a name="p1571675516528"></a><a name="p1571675516528"></a>是</p>
</td>
</tr>
<tr id="row6716155175218"><th class="firstcol" valign="top" width="19.29%" id="mcps1.1.3.19.1"><p id="p16716105520525"><a name="p16716105520525"></a><a name="p16716105520525"></a>是否默认安全</p>
</th>
<td class="cellrowborder" valign="top" width="80.71000000000001%" headers="mcps1.1.3.19.1 "><p id="p8716155155212"><a name="p8716155155212"></a><a name="p8716155155212"></a>是</p>
</td>
</tr>
</tbody>
</table>

**密钥更新<a name="section1198816429424"></a>**

>![](public_sys-resources/icon-note.gif) **说明：**
>密钥更新需要重启UBS IO加速组件服务，请合理规划密钥更新周期。密钥管理请参见[开启TLS认证](开启TLS认证.md)。

**缓冲区溢出安全保护<a name="section99093415506"></a>**

为阻止缓冲区溢出攻击，建议使用ASLR（Address space layout randomization）技术，通过对堆、栈、共享库映射等线性区布局的随机化，增加攻击者预测目的地址的难度，防止攻击者直接定位攻击代码位置。该技术可作用于堆、栈、内存映射区（mmap基址、shared libraries、vdso页）。

开启方式：

```
echo 2 >/proc/sys/kernel/randomize_va_space
```

# 软件卸载<a name="ZH-CN_TOPIC_0000002552860631"></a>

>![](public_sys-resources/icon-notice.gif) **须知：**
>-   建议卸载UBS IO前先安全删除所有密钥存储文件。
>-   建议删除安装UBS IO创建的目录。请参见[表1](安装UBS-IO.md#table15512184811916)。

**前提条件<a name="section537871472515"></a>**

-   若已经删除UBS IO解压后的软件包则需要重新上传软件包并解压。
-   卸载用户需与安装用户为相同用户且具备执行systemctl的命令的权限。
-   需开启安装用户的SSH权限。

**操作步骤<a name="section151296487264"></a>**

1.  登录UBS IO安装节点，执行。

    ```
    rpm -e ubs_io-boostio-1.0.0-1.oe2203sp3.aarch64
    ```

    同时需要卸载ubs-comm，libboundscheck，后续同样可以通过yum卸载。

2.  其他节点以此执行

# 软件升级<a name="ZH-CN_TOPIC_0000002552860635"></a>

>![](public_sys-resources/icon-note.gif) **说明：**
>当前版本仅支持离线升级，升级时需要停止应用业务，不支持在线升级。

UBS IO提供了升级准备、升级检查和升级完成三种操作，用于JuiceFS开发者端到端集成软件升级流程中，以下将描述升级操作的实现逻辑。

**升级准备<a name="section17305915289"></a>**

执行升级准备操作后，UBS IO会打开透写模式，关闭分布式缓存功能，此时前台业务IO将直接写入到后端存储系统中。详情请参见《BeiMing 26.0.RC1 UBS IO API参考》的“BioNotifyUpgradePrepare”章节。

**升级检查<a name="section3821719280"></a>**

执行升级检查操作后，UBS IO会检查分布式缓存的业务数据是否已经淘汰完成，如果淘汰完成则返回检查通过，否则返回检查失败，只有在升级检查通过后才能执行集群下电操作。详情请参见《BeiMing 26.0.RC1 UBS IO API参考》的“BioCheckUpgradeReady”章节。

**升级完成<a name="section1869815221984"></a>**

软件离线升级完成，重启集群服务后需要执行升级完成操作，UBS IO会关闭透写模式，重新使能分布式缓存服务。详情请参见《BeiMing 26.0.RC1 UBS IO API参考》的“BioNotifyUpgradeFinish”章节。

# 故障处理<a name="ZH-CN_TOPIC_0000002552860611"></a>

使用者需要遵循当前UBS IO版本的可靠性规格，超过规格范围外的故障场景存在业务中断或数据丢失风险。
分离部署模式下缓存客户端以二进制库的形式加载到应用进程中，应用进程退出会导致缓存客户端跟随退出，因此需要处理缓存客户端退出的故障模式。
分离部署模式下UBS IO存在独立缓存进程，融合部署模式下UBS IO和JuiceFS加载到同一个进程，该进程主要负责缓存客户端的请求处理，数据读写缓存和资源管理等业务处理，因此需要处理缓存进程故障的故障模式。
UBS IO要求部署在计算节点上，对外提供分布式读写缓存服务，因此需要处理部署UBS IO的计算节点故障的故障模式。
缓存客户端给服务端发送请求，缓存服务端之间消息交互等场景都会使用网卡进行通信，当网卡遭遇故障时会导致通信消息失败，比如请求消息发送失败，无法接收到响应等情况发生，因此需要处理通信网卡故障的故障模式。
UBS IO分布式缓存层依赖后端存储系统作为大容量数据持久化存储池，因此需要处理后端存储系统异常故障模式。


## 可靠性总体规格<a name="ZH-CN_TOPIC_0000002521860642"></a>

使用者需要遵循当前UBS IO版本的可靠性规格，超过规格范围外的故障场景存在业务中断或数据丢失风险。

UBS IO支持的可靠性总体原则：

-   依赖的开源软件ZooKeeper、JuiceFS和Ceph不属于UBS IO可靠性范围内，不支持依赖软件故障或异常的容错处理。
-   当前版本仅支持单重故障，不支持多重故障叠加。
-   当前版本仅支持双副本冗余，同时故障两个副本会导致用户数据丢失。
-   故障节点的数据可靠性不保证。

## 缓存客户端故障/恢复<a name="ZH-CN_TOPIC_0000002521860674"></a>

分离部署模式下缓存客户端以二进制库的形式加载到应用进程中，应用进程退出会导致缓存客户端跟随退出，因此需要处理缓存客户端退出的故障模式。

**表 1**  缓存客户端故障模式

|场景|影响|处理方式|限制|
|--|--|--|--|
|缓存客户端故障|写资源配额泄漏。分发的请求响应失败。通信链路断开。创建的线性空间失效。|缓存服务端通过链路断开事件感知客户端退出，回收写资源配额，封存线性空间并将数据淘汰到后端存储。删除失效的链路信息。|无。|
|缓存客户端恢复|重新建立通信链路。缓存客户端重新执行初始化和上电流程。|缓存服务端处理建链请求。|无。|


## UBS IO进程故障/恢复<a name="ZH-CN_TOPIC_0000002521700650"></a>

分离部署模式下UBS IO存在独立缓存进程，融合部署模式下UBS IO和JuiceFS加载到同一个进程，该进程主要负责缓存客户端的请求处理，数据读写缓存和资源管理等业务处理，因此需要处理缓存进程故障的故障模式。

**表 1**  缓存进程故障模式

|场景|影响|处理方式|限制|
|--|--|--|--|
|缓存进程故障|写缓存的副本数据丢失。读缓存的对象数据丢失。SDK端请求分发失败。|通过ZooKeeper心跳感知到缓存进程故障，通知集群管理更新视图，发布更新后的视图。受进程故障影响的分区数据强制淘汰到后端存储。|缓存进程临时故障仅修改分区视图。缓存进程永久故障，集群管理会将该节点移除集群，变更节点视图和分区视图。临时故障时间窗口期可配置。|
|缓存进程恢复|读写缓存功能恢复。|临时故障恢复：通过ZooKeeper心跳感知到缓存进程恢复，通知集群管理更新视图，视图更新后再发布新视图。永久故障恢复：执行扩容流程，请求重新加入集群。|无。|


## 缓存节点故障/恢复<a name="ZH-CN_TOPIC_0000002552740621"></a>

UBS IO要求部署在计算节点上，对外提供分布式读写缓存服务，因此需要处理部署UBS IO的计算节点故障的故障模式。

**表 1**  缓存节点故障模式

|场景|影响|处理方式|限制|
|--|--|--|--|
|缓存节点故障|写缓存的副本数据丢失。读缓存的对象数据丢失。SDK端请求分发失败。|通过ZooKeeper心跳感知到缓存进程故障，通知集群管理更新视图，发布更新后的视图。受进程故障影响的分区数据强制淘汰到后端存储。|缓存节点临时故障仅修改节点视图和分区视图的状态。缓存节点永久故障，集群管理会将该节点移除集群，变更节点视图和分区视图。临时故障时间窗口期可配置。|
|缓存节点恢复|读写缓存功能恢复。|临时故障恢复：通过ZooKeeper心跳感知到缓存进程恢复，通知集群管理更新视图，视图更新后再发布新视图。永久故障恢复：执行扩容流程，请求重新加入集群。|无。|


## UBS IO通信故障/恢复<a name="ZH-CN_TOPIC_0000002521860666"></a>

缓存客户端给服务端发送请求，缓存服务端之间消息交互等场景都会使用网卡进行通信，当网卡遭遇故障时会导致通信消息失败，比如请求消息发送失败，无法接收到响应等情况发生，因此需要处理通信网卡故障的故障模式。

**表 1**  通信网卡故障模式

|场景|影响|处理方式|限制|
|--|--|--|--|
|通信网卡故障|SDK端请求发送失败。SDK端请求接收超时。分区视图接收失败。|通过ZooKeeper心跳感知到通信网卡故障，通知集群管理更新视图，视图更新后再发布新视图。受网卡故障影响的分区数据强制淘汰到后端存储。|支持端口被占用、防火墙和网卡DOWN故障场景。不支持网络丢包、网络错包、网卡单通和链路闪断等网卡异常场景。|
|通信网卡恢复|请求发送功能恢复。读写缓存各个流程支持重入。|通过ZooKeeper心跳感知到通信网卡恢复，通知集群管理更新视图，视图更新后再发布新视图。|无。|


## 后端存储系统异常<a name="ZH-CN_TOPIC_0000002521700652"></a>

UBS IO分布式缓存层依赖后端存储系统作为大容量数据持久化存储池，因此需要处理后端存储系统异常故障模式。

**表 1**  后端存储系统故障模式

|场景|影响|处理方式|限制|
|--|--|--|--|
|后端存储系统故障|写缓存的对象数据无法淘汰。读缓存预取加载对象数据失败。|感知后端存储系统故障并作出相应告警，当前告警方式为日志打印。|无。|
|后端存储系统恢复|写缓存淘汰功能恢复正常。读缓存预取加载功能恢复正常。前台业务恢复正常。|感知后端存储系统恢复并重连成功。|写缓存淘汰功能恢复，由于后端存储性能限制，恢复到正常水位需要一段时间。写缓存淘汰性能限制，前台业务性能需要一段时间逐步恢复为正常水平。|


## 缓存磁盘故障<a name="ZH-CN_TOPIC_0000002521700660"></a>

UBS IO分布式缓存层使用缓存磁盘NVMe SSD作为二级缓存介质，用于持久化读写缓存数据，需要有效应对缓存磁盘故障。

**表 1**  缓存磁盘故障模式

|场景|影响|处理方式|限制|
|--|--|--|--|
|新磁盘加入|接入新盘过程中，前台I/O性能下降，业务受影响的时间不超过60s。|新磁盘加入和识别、配置文件更新、加盘事件上报、触发视图重均衡、淘汰缓存数据和创建新缓存。|单次支持加1块盘，单节点可用盘最大支持4块盘，否则报错。新加入磁盘容量大小规格，与集群磁盘规格保持一致。|
|故障磁盘剔除|故障检测与剔除期间，前台I/O性能下降，业务影响的时间不超过60s。|上报磁盘故障到集群管理、完成受影响分区数据淘汰后上报处理、触发分区视图重计算与发布（期间故障分区IO自动重试）。|仅支持单盘故障，同时双盘故障会导致丢数据。|


## 缓存节点扩容<a name="ZH-CN_TOPIC_0000002552860607"></a>

-   当前版本支持的集群规模最小为2节点，最大为256节点。因此最大扩容至集群256个节点。
-   支持同时批量进行扩容的缓存节点数为32个。
-   不支持故障处理期间进行缓存节点扩容操作。

# 后端存储使用说明<a name="ZH-CN_TOPIC_0000002521860648"></a>

UBS IO会使用后端存储系统作为数据的持久化容量层。当前仅支持Ceph和HDFS分布式存储系统，后端存储系统由用户提供并创建UBS IO使用的存储池或挂载目录。因为后端存储系统属于用户管理和维护范围，需要用户保证提供给UBS IO使用的存储池或挂载目录不会被其它软件应用使用和访问，否则会破坏应用数据和构成目录越权风险。

# 公网地址声明<a name="ZH-CN_TOPIC_0000002552740627"></a>

以下表格中列出了产品中包含的公网地址，没有安全风险。

|网址|说明|
|--|--|
|appro@openssl.org|该网址为开源软件OpenSSL引入，为OpenSSL的版权信息声明，无安全风险。|


# 账户一览表<a name="ZH-CN_TOPIC_0000002552740599"></a>

>![](public_sys-resources/icon-notice.gif) **须知：**
>用户创建的安装用户需定期修改密码。

|用户|描述|初始密码|密码修改方法|
|--|--|--|--|
|bioadmin|分离部署场景UBS IO Server运行用户。|用户自定义。|使用**passwd**命令修改。|
|juiceadmin|融合部署场景上层调用组件运行用户。|用户自定义。|使用**passwd**命令修改。|


# 版权说明<a name="ZH-CN_TOPIC_0000002553545773"></a>

Copyright \(c\) Huawei Technologies Co., Ltd. 2025. All rights reserved.
