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
|![](image/zh-cn_image_0000002485192120.png)|表示如不避免则将会导致死亡或严重伤害的具有高等级风险的危害。|
|![](image/zh-cn_image_0000002485032160.png)|表示如不避免则可能导致死亡或严重伤害的具有中等级风险的危害。|
|![](image/zh-cn_image_0000002517312047.png)|表示如不避免则可能导致轻微或中度伤害的具有低等级风险的危害。|
|![](image/zh-cn_image_0000002517312049.png)|用于传递设备或环境安全警示信息。如不避免则可能会导致设备损坏、数据丢失、设备性能降低或其它不可预知的结果。“须知”不涉及人身伤害。|
|![](image/zh-cn_image_0000002517352069.png)|对正文中重点信息的补充说明。“说明”不是安全警示信息，不涉及人身、设备及环境伤害信息。|

**修改记录<a name="section2467512116410"></a>**

<a name="zh-cn_topic_0000002543399273_table1557726816410"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000002543399273_row2942532716410"><th class="cellrowborder" valign="top" width="20.72%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000002543399273_p3778275416410"><a name="zh-cn_topic_0000002543399273_p3778275416410"></a><a name="zh-cn_topic_0000002543399273_p3778275416410"></a><strong id="zh-cn_topic_0000002543399273_b5687322716410"><a name="zh-cn_topic_0000002543399273_b5687322716410"></a><a name="zh-cn_topic_0000002543399273_b5687322716410"></a>文档版本</strong></p>
</th>
<th class="cellrowborder" valign="top" width="26.119999999999997%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000002543399273_p5627845516410"><a name="zh-cn_topic_0000002543399273_p5627845516410"></a><a name="zh-cn_topic_0000002543399273_p5627845516410"></a><strong id="zh-cn_topic_0000002543399273_b5800814916410"><a name="zh-cn_topic_0000002543399273_b5800814916410"></a><a name="zh-cn_topic_0000002543399273_b5800814916410"></a>发布日期</strong></p>
</th>
<th class="cellrowborder" valign="top" width="53.16%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000002543399273_p2382284816410"><a name="zh-cn_topic_0000002543399273_p2382284816410"></a><a name="zh-cn_topic_0000002543399273_p2382284816410"></a><strong id="zh-cn_topic_0000002543399273_b3316380216410"><a name="zh-cn_topic_0000002543399273_b3316380216410"></a><a name="zh-cn_topic_0000002543399273_b3316380216410"></a>修改说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000002543399273_row259511572216"><td class="cellrowborder" valign="top" width="20.72%" headers="mcps1.1.4.1.1 "><p id="zh-cn_topic_0000002543399273_p7595175717219"><a name="zh-cn_topic_0000002543399273_p7595175717219"></a><a name="zh-cn_topic_0000002543399273_p7595175717219"></a>01</p>
</td>
<td class="cellrowborder" valign="top" width="26.119999999999997%" headers="mcps1.1.4.1.2 "><p id="zh-cn_topic_0000002543399273_p1459565782115"><a name="zh-cn_topic_0000002543399273_p1459565782115"></a><a name="zh-cn_topic_0000002543399273_p1459565782115"></a>2026-03-30</p>
</td>
<td class="cellrowborder" valign="top" width="53.16%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000002543399273_p759519575214"><a name="zh-cn_topic_0000002543399273_p759519575214"></a><a name="zh-cn_topic_0000002543399273_p759519575214"></a>第一次正式发布。</p>
</td>
</tr>
</tbody>
</table>

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
![](image/场景说明.png "场景说明")

## 整体架构<a name="ZH-CN_TOPIC_0000002552740609"></a>

**图 1**  UBS IO整体架构设计<a name="fig78661457194015"></a>  
![](image/UBS-IO整体架构设计.png "UBS-IO整体架构设计")

上述架构模型视图中的逻辑元素如[表1](#table567036182612)描述所示。

**表 1**  逻辑元素

<a name="table567036182612"></a>
<table><thead align="left"><tr id="row467011620267"><th class="cellrowborder" valign="top" width="20.22%" id="mcps1.2.4.1.1"><p id="p367019642612"><a name="p367019642612"></a><a name="p367019642612"></a>模块名中文名</p>
</th>
<th class="cellrowborder" valign="top" width="11.24%" id="mcps1.2.4.1.2"><p id="p2531831152316"><a name="p2531831152316"></a><a name="p2531831152316"></a>模块英文名</p>
</th>
<th class="cellrowborder" valign="top" width="68.54%" id="mcps1.2.4.1.3"><p id="p166709619265"><a name="p166709619265"></a><a name="p166709619265"></a>详细描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row10670561266"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p567076112610"><a name="p567076112610"></a><a name="p567076112610"></a>缓存客户端</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p13548314234"><a name="p13548314234"></a><a name="p13548314234"></a>SDK</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p46706619262"><a name="p46706619262"></a><a name="p46706619262"></a>提供C版本的对外API，UBS IO分布式缓存访问入口，负责实例管理、网络资源管理、节点/分区视图管理和流量控制等功能。</p>
</td>
</tr>
<tr id="row1067026132616"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p1067018622620"><a name="p1067018622620"></a><a name="p1067018622620"></a>数据镜像模块</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p3541631152314"><a name="p3541631152314"></a><a name="p3541631152314"></a>Mirror</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p1067176182618"><a name="p1067176182618"></a><a name="p1067176182618"></a>负责数据多副本冗余管理，缓存对象请求分发等功能。</p>
</td>
</tr>
<tr id="row18671265264"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p12671666261"><a name="p12671666261"></a><a name="p12671666261"></a>写缓存模块</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p17541231192320"><a name="p17541231192320"></a><a name="p17541231192320"></a>WriteCache</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p56712692614"><a name="p56712692614"></a><a name="p56712692614"></a>负责写缓存对象数据、索引元数据和淘汰策略的管理功能，提供数据回写和透写模式。</p>
</td>
</tr>
<tr id="row1449019463411"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p34902413348"><a name="p34902413348"></a><a name="p34902413348"></a>读缓存模块</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p1154133122318"><a name="p1154133122318"></a><a name="p1154133122318"></a>ReadCache</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p24904433417"><a name="p24904433417"></a><a name="p24904433417"></a>负责读缓存对象数据、索引元数据和淘汰策略的管理功能，提供对象数据预取功能。</p>
</td>
</tr>
<tr id="row10971611344"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p1997114113342"><a name="p1997114113342"></a><a name="p1997114113342"></a>流式空间模块</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p14542031162315"><a name="p14542031162315"></a><a name="p14542031162315"></a>Flow</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p139711713344"><a name="p139711713344"></a><a name="p139711713344"></a>提供无限长的逻辑线性空间的申请和释放接口，支持数据Append方式写入。</p>
</td>
</tr>
<tr id="row35974598339"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p665415473518"><a name="p665415473518"></a><a name="p665415473518"></a>内存空间管理模块</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p145453119236"><a name="p145453119236"></a><a name="p145453119236"></a>MM</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p259735923311"><a name="p259735923311"></a><a name="p259735923311"></a>负责用于缓存的内存空间按照Block粒度进行管理，支持内存注册到RDMA和Shared Memory。</p>
</td>
</tr>
<tr id="row1335245723313"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p1235265719335"><a name="p1235265719335"></a><a name="p1235265719335"></a>磁盘块设备管理模块</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p15493118235"><a name="p15493118235"></a><a name="p15493118235"></a>BDM</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p035215753319"><a name="p035215753319"></a><a name="p035215753319"></a>负责用于缓存的磁盘块设备空间按照Block粒度进行管理，提供同步/异步数据读写功能。</p>
</td>
</tr>
<tr id="row10512116193612"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p951214663617"><a name="p951214663617"></a><a name="p951214663617"></a>后端存储管理模块</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p10541631172315"><a name="p10541631172315"></a><a name="p10541631172315"></a>UFS</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p4992130479"><a name="p4992130479"></a><a name="p4992130479"></a>管理多种后端存储系统，对上提供统一的数据访问接口，屏蔽后端存储系统差异。</p>
</td>
</tr>
<tr id="row1998710383617"><td class="cellrowborder" valign="top" width="20.22%" headers="mcps1.2.4.1.1 "><p id="p39871034364"><a name="p39871034364"></a><a name="p39871034364"></a>集群管理模块</p>
</td>
<td class="cellrowborder" valign="top" width="11.24%" headers="mcps1.2.4.1.2 "><p id="p1854123114237"><a name="p1854123114237"></a><a name="p1854123114237"></a>CM</p>
</td>
<td class="cellrowborder" valign="top" width="68.54%" headers="mcps1.2.4.1.3 "><p id="p11467199796"><a name="p11467199796"></a><a name="p11467199796"></a>基于开源ZooKeeper提供缓存集群管理功能，负责状态监控、分区视图计算和故障处理等功能。</p>
</td>
</tr>
</tbody>
</table>

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
![](image/缓存策略可配置.png "缓存策略可配置")

### 数据管理技术方案<a name="ZH-CN_TOPIC_0000002521860656"></a>

为了追求应用数据在不同I/O粒度、随机I/O模型和磁盘读写下的高性能，UBS IO创新性地在缓存层的多级数据布局上采用流式/线性数据存储方式，主要解决不同I/O大小带来的缓存空间浪费问题，同时减少随机I/O带来的频繁磁盘寻址导致的写入时延增大等性能瓶颈问题。流式数据管理方案的核心思想是提供一个逻辑地址无限大的线性空间，当数据写入的时候首先从线性空间中向后递增地分配写入偏移，然后以append形式将数据追加写入到线性空间中。当前流式数据管理支持使用内存和NVMe SSD磁盘介质作为缓存空间。

**图 1**  流式数据管理<a name="fig7727049124813"></a>  
![](image/流式数据管理.png "流式数据管理")

## 部署方式<a name="ZH-CN_TOPIC_0000002552860621"></a>

大数据应用和智算AI应用在不同的场景下具有较大的部署和组网差异，并且结合现网中的集群部署方式，对软件在安装部署方面提出了更高的要求。UBS IO分布式缓存层整体设计采用C/S架构，对外提供灵活多样的安装部署方式，其部署方式可以大致分为节点裸机部署和容器/虚拟机部署两类，每类又可细分为融合部署、分离部署和独立部署三种，此处以节点裸机部署为例详细介绍三种部署方式。

**图 1**  融合部署<a name="fig1895841541014"></a>  
![](image/融合部署.png "融合部署")

**图 2**  分离部署<a name="fig738018111175"></a>  
![](image/分离部署.png "分离部署")

**图 3**  独立部署<a name="fig147331725116"></a>  
![](image/独立部署.png "独立部署")

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

<a name="zh-cn_topic_0000001664980070_table564mcpsimp"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000002543359289_row242mcpsimp"><th class="cellrowborder" valign="top" width="39.03%" id="mcps1.2.3.1.1"><p id="zh-cn_topic_0000002543359289_p244mcpsimp"><a name="zh-cn_topic_0000002543359289_p244mcpsimp"></a><a name="zh-cn_topic_0000002543359289_p244mcpsimp"></a>软件名称</p>
</th>
<th class="cellrowborder" valign="top" width="60.97%" id="mcps1.2.3.1.2"><p id="zh-cn_topic_0000002543359289_p246mcpsimp"><a name="zh-cn_topic_0000002543359289_p246mcpsimp"></a><a name="zh-cn_topic_0000002543359289_p246mcpsimp"></a>软件版本</p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000002543359289_row122352569429"><td class="cellrowborder" valign="top" width="39.03%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000002543359289_p122351256174213"><a name="zh-cn_topic_0000002543359289_p122351256174213"></a><a name="zh-cn_topic_0000002543359289_p122351256174213"></a>OS</p>
</td>
<td class="cellrowborder" valign="top" width="60.97%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000002543359289_p162351356204218"><a name="zh-cn_topic_0000002543359289_p162351356204218"></a><a name="zh-cn_topic_0000002543359289_p162351356204218"></a>openEuler 22.03 LTS SP3</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002543359289_row1033219148438"><td class="cellrowborder" valign="top" width="39.03%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p595mcpsimp"><a name="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p595mcpsimp"></a><a name="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p595mcpsimp"></a>JuiceFS</p>
</td>
<td class="cellrowborder" valign="top" width="60.97%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p17359948185812"><a name="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p17359948185812"></a><a name="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p17359948185812"></a>1.0.3</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002543359289_row13260151764319"><td class="cellrowborder" valign="top" width="39.03%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p605mcpsimp"><a name="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p605mcpsimp"></a><a name="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p605mcpsimp"></a>Redis</p>
</td>
<td class="cellrowborder" valign="top" width="60.97%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p1935934855818"><a name="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p1935934855818"></a><a name="zh-cn_topic_0000002543359289_zh-cn_topic_0000001664980070_p1935934855818"></a>4.0.11</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002543359289_row250mcpsimp"><td class="cellrowborder" valign="top" width="39.03%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000002543359289_p11555487209"><a name="zh-cn_topic_0000002543359289_p11555487209"></a><a name="zh-cn_topic_0000002543359289_p11555487209"></a>ZooKeeper</p>
</td>
<td class="cellrowborder" valign="top" width="60.97%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000002543359289_p13552486207"><a name="zh-cn_topic_0000002543359289_p13552486207"></a><a name="zh-cn_topic_0000002543359289_p13552486207"></a>3.9.3</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002543359289_row133137288432"><td class="cellrowborder" valign="top" width="39.03%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000002543359289_p2901112719302"><a name="zh-cn_topic_0000002543359289_p2901112719302"></a><a name="zh-cn_topic_0000002543359289_p2901112719302"></a>Ceph</p>
</td>
<td class="cellrowborder" valign="top" width="60.97%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000002543359289_p9250151812226"><a name="zh-cn_topic_0000002543359289_p9250151812226"></a><a name="zh-cn_topic_0000002543359289_p9250151812226"></a>12.2.8</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002543359289_row28161330134313"><td class="cellrowborder" valign="top" width="39.03%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000002543359289_p1357342081518"><a name="zh-cn_topic_0000002543359289_p1357342081518"></a><a name="zh-cn_topic_0000002543359289_p1357342081518"></a>Python</p>
</td>
<td class="cellrowborder" valign="top" width="60.97%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000002543359289_p11573142017154"><a name="zh-cn_topic_0000002543359289_p11573142017154"></a><a name="zh-cn_topic_0000002543359289_p11573142017154"></a>3.7</p>
</td>
</tr>
</tbody>
</table>

## 安装UBS IO<a name="ZH-CN_TOPIC_0000002552740595"></a>

-   UBS IO分为分离部署、融合部署、独立部署场景，融合部署场景使用上层调用组件用户（例如：juiceadmin:juicegroup）统一安装UBS IO Server和SDK，分离部署和独立部署场景需要创建Server端用户（例如：bioadmin:biogroup）安装UBS IO Server，使用上层调用组件用户安装UBS IO SDK。使用root等特权账号运行程序时，如果程序遭到入侵攻击，攻击者可以利用该程序的高级运行权限来对整个系统造成危害。
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

    <a name="table15512184811916"></a>
    <table><thead align="left"><tr id="row1251214831919"><th class="cellrowborder" valign="top" width="27.74%" id="mcps1.2.5.1.1"><p id="p1651316489194"><a name="p1651316489194"></a><a name="p1651316489194"></a>目录地址</p>
    </th>
    <th class="cellrowborder" valign="top" width="18.5%" id="mcps1.2.5.1.2"><p id="p511645202012"><a name="p511645202012"></a><a name="p511645202012"></a>用户和用户组</p>
    </th>
    <th class="cellrowborder" valign="top" width="15.559999999999999%" id="mcps1.2.5.1.3"><p id="p1051364811919"><a name="p1051364811919"></a><a name="p1051364811919"></a>权限</p>
    </th>
    <th class="cellrowborder" valign="top" width="38.2%" id="mcps1.2.5.1.4"><p id="p3574754113510"><a name="p3574754113510"></a><a name="p3574754113510"></a>备注</p>
    </th>
    </tr>
    </thead>
    <tbody><tr id="row0205173119208"><td class="cellrowborder" valign="top" width="27.74%" headers="mcps1.2.5.1.1 "><p id="p9205163112014"><a name="p9205163112014"></a><a name="p9205163112014"></a>/var/log/boostio</p>
    </td>
    <td class="cellrowborder" valign="top" width="18.5%" headers="mcps1.2.5.1.2 "><p id="p89501849103419"><a name="p89501849103419"></a><a name="p89501849103419"></a>Server安装用户：Server安装用户组</p>
    </td>
    <td class="cellrowborder" valign="top" width="15.559999999999999%" headers="mcps1.2.5.1.3 "><p id="p2020523118202"><a name="p2020523118202"></a><a name="p2020523118202"></a>750</p>
    </td>
    <td class="cellrowborder" valign="top" width="38.2%" headers="mcps1.2.5.1.4 "><p id="p957425410353"><a name="p957425410353"></a><a name="p957425410353"></a>UBS IO Server端日志目录。</p>
    </td>
    </tr>
    <tr id="row209178378340"><td class="cellrowborder" valign="top" width="27.74%" headers="mcps1.2.5.1.1 "><p id="p991803716346"><a name="p991803716346"></a><a name="p991803716346"></a>/var/log/boostio/trace</p>
    </td>
    <td class="cellrowborder" valign="top" width="18.5%" headers="mcps1.2.5.1.2 "><p id="p5650254193415"><a name="p5650254193415"></a><a name="p5650254193415"></a>Server安装用户：Server安装用户组</p>
    </td>
    <td class="cellrowborder" valign="top" width="15.559999999999999%" headers="mcps1.2.5.1.3 "><p id="p29182037153420"><a name="p29182037153420"></a><a name="p29182037153420"></a>750</p>
    </td>
    <td class="cellrowborder" valign="top" width="38.2%" headers="mcps1.2.5.1.4 "><p id="p6574054133516"><a name="p6574054133516"></a><a name="p6574054133516"></a>UBS IO统计日志目录。</p>
    </td>
    </tr>
    <tr id="row195421659132117"><td class="cellrowborder" valign="top" width="27.74%" headers="mcps1.2.5.1.1 "><p id="p155421659172116"><a name="p155421659172116"></a><a name="p155421659172116"></a>sdk初始化函数中的日志路径</p>
    </td>
    <td class="cellrowborder" valign="top" width="18.5%" headers="mcps1.2.5.1.2 "><p id="p12542185914217"><a name="p12542185914217"></a><a name="p12542185914217"></a>SDK安装用户：SDK安装用户组</p>
    </td>
    <td class="cellrowborder" valign="top" width="15.559999999999999%" headers="mcps1.2.5.1.3 "><p id="p93191419112315"><a name="p93191419112315"></a><a name="p93191419112315"></a>750</p>
    </td>
    <td class="cellrowborder" valign="top" width="38.2%" headers="mcps1.2.5.1.4 "><p id="p1757425443518"><a name="p1757425443518"></a><a name="p1757425443518"></a>UBS IO SDK端日志目录。</p>
    </td>
    </tr>
    </tbody>
    </table>

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

    <a name="table6587201602818"></a>
    <table><thead align="left"><tr id="row145881016142812"><th class="cellrowborder" valign="top" width="25%" id="mcps1.2.3.1.1"><p id="p205881916152816"><a name="p205881916152816"></a><a name="p205881916152816"></a>目录</p>
    </th>
    <th class="cellrowborder" valign="top" width="75%" id="mcps1.2.3.1.2"><p id="p115883169283"><a name="p115883169283"></a><a name="p115883169283"></a>说明</p>
    </th>
    </tr>
    </thead>
    <tbody><tr id="row18588191614282"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.3.1.1 "><p id="p19588181620287"><a name="p19588181620287"></a><a name="p19588181620287"></a>/usr/bin</p>
    </td>
    <td class="cellrowborder" valign="top" width="75%" headers="mcps1.2.3.1.2 "><p id="p9588316202810"><a name="p9588316202810"></a><a name="p9588316202810"></a>可执行文件。</p>
    </td>
    </tr>
    <tr id="row358817163289"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.3.1.1 "><p id="p656185901010"><a name="p656185901010"></a><a name="p656185901010"></a>/usr/lib64</p>
    </td>
    <td class="cellrowborder" valign="top" width="75%" headers="mcps1.2.3.1.2 "><p id="p558831613282"><a name="p558831613282"></a><a name="p558831613282"></a>动态库和静态库文件。</p>
    </td>
    </tr>
    <tr id="row185886162284"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.3.1.1 "><p id="p13116113571016"><a name="p13116113571016"></a><a name="p13116113571016"></a>/etc/boostio</p>
    </td>
    <td class="cellrowborder" valign="top" width="75%" headers="mcps1.2.3.1.2 "><p id="p4588316102820"><a name="p4588316102820"></a><a name="p4588316102820"></a>配置文件。</p>
    </td>
    </tr>
    </tbody>
    </table>

    **表 3**  bin目录文件说明

    <a name="table13781183131313"></a>
    <table><thead align="left"><tr id="row978115371310"><th class="cellrowborder" valign="top" width="15%" id="mcps1.2.4.1.1"><p id="p678115361314"><a name="p678115361314"></a><a name="p678115361314"></a>目录</p>
    </th>
    <th class="cellrowborder" valign="top" width="25%" id="mcps1.2.4.1.2"><p id="p1878153111315"><a name="p1878153111315"></a><a name="p1878153111315"></a>文件名称</p>
    </th>
    <th class="cellrowborder" valign="top" width="60%" id="mcps1.2.4.1.3"><p id="p15781143141317"><a name="p15781143141317"></a><a name="p15781143141317"></a>描述</p>
    </th>
    </tr>
    </thead>
    <tbody><tr id="row117816331319"><td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.4.1.1 "><p id="p67811314136"><a name="p67811314136"></a><a name="p67811314136"></a>/usr/bin</p>
    </td>
    <td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.4.1.2 "><p id="p0781730138"><a name="p0781730138"></a><a name="p0781730138"></a>bio_daemon</p>
    </td>
    <td class="cellrowborder" valign="top" width="60%" headers="mcps1.2.4.1.3 "><p id="p1478115321310"><a name="p1478115321310"></a><a name="p1478115321310"></a>UBS IO服务可执行文件。</p>
    </td>
    </tr>
    </tbody>
    </table>

    **表 4**  lib目录文件说明

    <a name="table1578031815166"></a>
    <table><thead align="left"><tr id="row7781111851611"><th class="cellrowborder" valign="top" width="15%" id="mcps1.2.4.1.1"><p id="p14781131811613"><a name="p14781131811613"></a><a name="p14781131811613"></a>目录</p>
    </th>
    <th class="cellrowborder" valign="top" width="25%" id="mcps1.2.4.1.2"><p id="p11781171811167"><a name="p11781171811167"></a><a name="p11781171811167"></a>文件名称</p>
    </th>
    <th class="cellrowborder" valign="top" width="60%" id="mcps1.2.4.1.3"><p id="p07811118141619"><a name="p07811118141619"></a><a name="p07811118141619"></a>描述</p>
    </th>
    </tr>
    </thead>
    <tbody><tr id="row1380284314268"><td class="cellrowborder" rowspan="6" valign="top" width="15%" headers="mcps1.2.4.1.1 "><p id="p2096831122910"><a name="p2096831122910"></a><a name="p2096831122910"></a>/usr/lib</p>
    </td>
    <td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.4.1.2 "><p id="p128021743102613"><a name="p128021743102613"></a><a name="p128021743102613"></a>libbio_interceptor_server.so</p>
    </td>
    <td class="cellrowborder" valign="top" width="60%" headers="mcps1.2.4.1.3 "><p id="p7802174310261"><a name="p7802174310261"></a><a name="p7802174310261"></a>桥接服务<span>共享对象文件</span>。</p>
    </td>
    </tr>
    <tr id="row72691346172619"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p182691465266"><a name="p182691465266"></a><a name="p182691465266"></a>libbio_server.so</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p2269104612269"><a name="p2269104612269"></a><a name="p2269104612269"></a>UBS IO Server端<span>共享对象文件</span>。</p>
    </td>
    </tr>
    <tr id="row5378155017266"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p5379195092617"><a name="p5379195092617"></a><a name="p5379195092617"></a>libbio_sdk.so.1.0.0</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p113791350142611"><a name="p113791350142611"></a><a name="p113791350142611"></a>UBS IO SDK端<span>共享对象文件</span>。</p>
    </td>
    </tr>
    <tr id="row1997531532719"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p109751151276"><a name="p109751151276"></a><a name="p109751151276"></a>libbio_sdk.so.1</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p1397531512274"><a name="p1397531512274"></a><a name="p1397531512274"></a>UBS IO SDK端<span>共享对象文件软连接</span>。</p>
    </td>
    </tr>
    <tr id="row1152874912718"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p11528184942713"><a name="p11528184942713"></a><a name="p11528184942713"></a>libock_interceptor.so</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p75281849162717"><a name="p75281849162717"></a><a name="p75281849162717"></a>桥接服务<span>共享对象文件</span>。</p>
    </td>
    </tr>
    <tr id="row12938175222711"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p19938252102718"><a name="p19938252102718"></a><a name="p19938252102718"></a>libock_iofwd_proxy.so</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p13938145232716"><a name="p13938145232716"></a><a name="p13938145232716"></a>桥接服务<span>共享对象文件</span>。</p>
    </td>
    </tr>
    </tbody>
    </table>

4.  将zookeeper client需要的so文件"libzookeeper\_mt.so"拷贝到bio用户有权限读取的路径，并将路径添加到LD\_LIBRARY\_PATH中。
5.  配置安装信息。

    根据业务使用情况和待安装部署的环境设置“/etc/boostio”目录下bio.conf中的相关配置项，具体配置项说明如[表5](#table8874183794213)所示。

    **表 5**  UBS IO配置项

    <a name="table8874183794213"></a>
    <table><thead align="left"><tr id="row787411377421"><th class="cellrowborder" valign="top" width="9.99%" id="mcps1.2.7.1.1"><p id="p587483704216"><a name="p587483704216"></a><a name="p587483704216"></a>归属模块</p>
    </th>
    <th class="cellrowborder" valign="top" width="20.01%" id="mcps1.2.7.1.2"><p id="p1887416378425"><a name="p1887416378425"></a><a name="p1887416378425"></a>配置项名称</p>
    </th>
    <th class="cellrowborder" valign="top" width="20%" id="mcps1.2.7.1.3"><p id="p587419375424"><a name="p587419375424"></a><a name="p587419375424"></a>简要描述</p>
    </th>
    <th class="cellrowborder" valign="top" width="15%" id="mcps1.2.7.1.4"><p id="p88742037194218"><a name="p88742037194218"></a><a name="p88742037194218"></a>默认值</p>
    </th>
    <th class="cellrowborder" valign="top" width="15%" id="mcps1.2.7.1.5"><p id="p178741537134212"><a name="p178741537134212"></a><a name="p178741537134212"></a>合法值/区间</p>
    </th>
    <th class="cellrowborder" valign="top" width="20%" id="mcps1.2.7.1.6"><p id="p1874203754218"><a name="p1874203754218"></a><a name="p1874203754218"></a>注意事项</p>
    </th>
    </tr>
    </thead>
    <tbody><tr id="row11875193784211"><td class="cellrowborder" valign="top" width="9.99%" headers="mcps1.2.7.1.1 "><p id="p78755372423"><a name="p78755372423"></a><a name="p78755372423"></a>Log</p>
    </td>
    <td class="cellrowborder" valign="top" width="20.01%" headers="mcps1.2.7.1.2 "><p id="p987553774211"><a name="p987553774211"></a><a name="p987553774211"></a>bio.log.level</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.3 "><p id="p587513724213"><a name="p587513724213"></a><a name="p587513724213"></a>日志打印级别。</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.4 "><p id="p148756376425"><a name="p148756376425"></a><a name="p148756376425"></a>info</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.5 "><a name="ul16707191194010"></a><a name="ul16707191194010"></a><ul id="ul16707191194010"><li>debug</li><li>info</li><li>warn</li><li>trace</li><li>error</li></ul>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.6 "><p id="p16875113794216"><a name="p16875113794216"></a><a name="p16875113794216"></a>-</p>
    </td>
    </tr>
    <tr id="row2875937154211"><td class="cellrowborder" rowspan="17" valign="top" width="9.99%" headers="mcps1.2.7.1.1 "><p id="p158751937124214"><a name="p158751937124214"></a><a name="p158751937124214"></a>Net</p>
    </td>
    <td class="cellrowborder" valign="top" width="20.01%" headers="mcps1.2.7.1.2 "><p id="p5875037134216"><a name="p5875037134216"></a><a name="p5875037134216"></a>bio.net.data.ip_mask</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.3 "><p id="p987573710425"><a name="p987573710425"></a><a name="p987573710425"></a>IP地址段。</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.4 "><p id="p2875123764219"><a name="p2875123764219"></a><a name="p2875123764219"></a>127.0.0.1/24</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.5 "><p id="p98754376429"><a name="p98754376429"></a><a name="p98754376429"></a>*.*.*.*/#，其中*为0 ~ 255，#为0 ~ 32</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.6 "><p id="p108754379428"><a name="p108754379428"></a><a name="p108754379428"></a>使用JuiceFS跑大数据业务时，该字段需要和/etc/hosts中的主机名对应的IP保持一致。</p>
    </td>
    </tr>
    <tr id="row128759373422"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1587663784212"><a name="p1587663784212"></a><a name="p1587663784212"></a>bio.net.data.listen_port</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p0876133719421"><a name="p0876133719421"></a><a name="p0876133719421"></a>业务面网络通信端口号。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1887615378426"><a name="p1887615378426"></a><a name="p1887615378426"></a>7201</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p14876837154210"><a name="p14876837154210"></a><a name="p14876837154210"></a>7201 ~ 7800</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p1287617372423"><a name="p1287617372423"></a><a name="p1287617372423"></a>-</p>
    </td>
    </tr>
    <tr id="row88768375421"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p4876137134213"><a name="p4876137134213"></a><a name="p4876137134213"></a>bio.net.data.protocol</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p20876133718426"><a name="p20876133718426"></a><a name="p20876133718426"></a>网络协议。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p20876337144218"><a name="p20876337144218"></a><a name="p20876337144218"></a>tcp</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><a name="ul101210497402"></a><a name="ul101210497402"></a><ul id="ul101210497402"><li>rdma</li><li>tcp</li></ul>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p1387614376421"><a name="p1387614376421"></a><a name="p1387614376421"></a>-</p>
    </td>
    </tr>
    <tr id="row2876153713426"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p9876133714424"><a name="p9876133714424"></a><a name="p9876133714424"></a>bio.net.rpc.data.busy_polling_mode</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p487673712423"><a name="p487673712423"></a><a name="p487673712423"></a>RPC开启busy-polling标记。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p14876163734212"><a name="p14876163734212"></a><a name="p14876163734212"></a>false</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><a name="ul11141245617"></a><a name="ul11141245617"></a><ul id="ul11141245617"><li>true</li><li>false</li></ul>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p6876113717422"><a name="p6876113717422"></a><a name="p6876113717422"></a>仅RDMA协议生效。</p>
    </td>
    </tr>
    <tr id="row128772037194219"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p787718374427"><a name="p787718374427"></a><a name="p787718374427"></a>bio.net.rpc.data.workers_count</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p17877133717421"><a name="p17877133717421"></a><a name="p17877133717421"></a>RPC数据面工作核数。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p19877137174212"><a name="p19877137174212"></a><a name="p19877137174212"></a>4</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p287714379423"><a name="p287714379423"></a><a name="p287714379423"></a>1 ~ 16</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p587714371428"><a name="p587714371428"></a><a name="p587714371428"></a>-</p>
    </td>
    </tr>
    <tr id="row387711371429"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1887715377424"><a name="p1887715377424"></a><a name="p1887715377424"></a>bio.net.request.executor.thread.num</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p6877183717423"><a name="p6877183717423"></a><a name="p6877183717423"></a>接收端请求处理线程数。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p387733713429"><a name="p387733713429"></a><a name="p387733713429"></a>8</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p687723744210"><a name="p687723744210"></a><a name="p687723744210"></a>8 ~ 256</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p48771737174219"><a name="p48771737174219"></a><a name="p48771737174219"></a>-</p>
    </td>
    </tr>
    <tr id="row38771937114216"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p68773379421"><a name="p68773379421"></a><a name="p68773379421"></a>bio.net.request.executor.queue.size</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p16877837204210"><a name="p16877837204210"></a><a name="p16877837204210"></a>接收端请求处理队列深度。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p087783704216"><a name="p087783704216"></a><a name="p087783704216"></a>1024</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p16877183718421"><a name="p16877183718421"></a><a name="p16877183718421"></a>1024 ~ 65535</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p14878113744211"><a name="p14878113744211"></a><a name="p14878113744211"></a>-</p>
    </td>
    </tr>
    <tr id="row158781737134210"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p168781837104211"><a name="p168781837104211"></a><a name="p168781837104211"></a>bio.net.ipc.data.busy_polling_mode</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p188781637134213"><a name="p188781637134213"></a><a name="p188781637134213"></a>IPC开启busy-polling标记。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1287813377427"><a name="p1287813377427"></a><a name="p1287813377427"></a>false</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><a name="ul12417581571"></a><a name="ul12417581571"></a><ul id="ul12417581571"><li>true</li><li>false</li></ul>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p7878143715424"><a name="p7878143715424"></a><a name="p7878143715424"></a>-</p>
    </td>
    </tr>
    <tr id="row75621826194117"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p995073311419"><a name="p995073311419"></a><a name="p995073311419"></a>bio.net.ipc.data.workers_count</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p89501733164112"><a name="p89501733164112"></a><a name="p89501733164112"></a>IPC数据面工作核数。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p69501533184118"><a name="p69501533184118"></a><a name="p69501533184118"></a>4</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p8950633164118"><a name="p8950633164118"></a><a name="p8950633164118"></a>1 ~ 128</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p19562526144111"><a name="p19562526144111"></a><a name="p19562526144111"></a>-</p>
    </td>
    </tr>
    <tr id="row3565135610414"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p2566125644119"><a name="p2566125644119"></a><a name="p2566125644119"></a>bio.net.tls.enable.switch</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p1856685610417"><a name="p1856685610417"></a><a name="p1856685610417"></a>网络安全开关。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1956675618414"><a name="p1956675618414"></a><a name="p1956675618414"></a>true</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><a name="ul176512185912"></a><a name="ul176512185912"></a><ul id="ul176512185912"><li>true</li><li>false</li></ul>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><a name="ul29324246557"></a><a name="ul29324246557"></a><ul id="ul29324246557"><li>关闭后可能会引入信息安全问题、仿冒等风险，请谨慎操作。</li><li>分离部署时调用UBS IO服务初始化接口传入的enableTls参数需要和该配置项保持一致。</li></ul>
    </td>
    </tr>
    <tr id="row148524511414"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p785218511415"><a name="p785218511415"></a><a name="p785218511415"></a>bio.net.tls.ca.cert.path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p16852175124119"><a name="p16852175124119"></a><a name="p16852175124119"></a>CA证书文件路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p133871556538"><a name="p133871556538"></a><a name="p133871556538"></a>/path/CA/cacert.pem</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p98526511410"><a name="p98526511410"></a><a name="p98526511410"></a>默认值仅作为示例。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p4852151184118"><a name="p4852151184118"></a><a name="p4852151184118"></a>安全开关打开则需要为有效路径，安全开关关闭则不解析该配置项。</p>
    </td>
    </tr>
    <tr id="row2905144719418"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p89051747144120"><a name="p89051747144120"></a><a name="p89051747144120"></a>bio.net.tls.ca.crl.path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p99051947174119"><a name="p99051947174119"></a><a name="p99051947174119"></a>吊销列表文件路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p13905447134113"><a name="p13905447134113"></a><a name="p13905447134113"></a>-</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p1390544764119"><a name="p1390544764119"></a><a name="p1390544764119"></a>-</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p760655414573"><a name="p760655414573"></a><a name="p760655414573"></a>可以为空，不为空时，安全开关打开且需要校验证书是否被吊销时为有效路径，安全开关关闭则不解析该配置项。</p>
    </td>
    </tr>
    <tr id="row19146204513415"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p14146104554117"><a name="p14146104554117"></a><a name="p14146104554117"></a>bio.net.tls.server.cert.path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p11461045154119"><a name="p11461045154119"></a><a name="p11461045154119"></a>服务端证书文件路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p11334826165419"><a name="p11334826165419"></a><a name="p11334826165419"></a>/path/server/servercert.pem</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p181461845144115"><a name="p181461845144115"></a><a name="p181461845144115"></a>默认值仅作为示例。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p10221135185818"><a name="p10221135185818"></a><a name="p10221135185818"></a>安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</p>
    </td>
    </tr>
    <tr id="row13267352165513"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p13267155212553"><a name="p13267155212553"></a><a name="p13267155212553"></a>bio.net.tls.server.key.path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p226825225513"><a name="p226825225513"></a><a name="p226825225513"></a>服务端证书私钥文件路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1673719467215"><a name="p1673719467215"></a><a name="p1673719467215"></a>/path/server/serverkey.pem</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p12268165212556"><a name="p12268165212556"></a><a name="p12268165212556"></a>默认值仅作为示例。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p192682529552"><a name="p192682529552"></a><a name="p192682529552"></a>安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</p>
    </td>
    </tr>
    <tr id="row1168053918551"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p16801339115512"><a name="p16801339115512"></a><a name="p16801339115512"></a>bio.net.tls.server.key.pass.path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p12680439185516"><a name="p12680439185516"></a><a name="p12680439185516"></a>工作证书私钥口令的密文的文件路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p20636195910216"><a name="p20636195910216"></a><a name="p20636195910216"></a>/path/server/server.keypass</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p0680339165513"><a name="p0680339165513"></a><a name="p0680339165513"></a>默认值仅作为示例。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p16229103952813"><a name="p16229103952813"></a><a name="p16229103952813"></a>安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</p>
    <p id="p16801939155518"><a name="p16801939155518"></a><a name="p16801939155518"></a>在加密私钥的时候，私钥口令建议满足复杂度要求。同时满足以下要求：</p>
    <a name="ol127192012553"></a><a name="ol127192012553"></a><ol id="ol127192012553"><li>口令长度至少8个字符。</li><li>口令需要包含如下至少两种字符的组合。<a name="ul32568545560"></a><a name="ul32568545560"></a><ul id="ul32568545560"><li>至少一个小写字母</li><li>至少一个大写字母</li><li>至少一个数字</li><li>至少一个特殊字符：`~!@#$%^&amp;*()-_=+\|[{}];:'"",&lt;.&gt;/?  和空格</li></ul>
    </li></ol>
    </td>
    </tr>
    <tr id="row1662264284114"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1020152519198"><a name="p1020152519198"></a><a name="p1020152519198"></a>bio.net.tls.server.decrypter.lib.path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p1562216424418"><a name="p1562216424418"></a><a name="p1562216424418"></a>安全解密函数so文件路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p498214528548"><a name="p498214528548"></a><a name="p498214528548"></a>/path/libdecrypt.so</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p17622194214119"><a name="p17622194214119"></a><a name="p17622194214119"></a>默认值仅作为示例。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p13831343807"><a name="p13831343807"></a><a name="p13831343807"></a>安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</p>
    </td>
    </tr>
    <tr id="row987853764219"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1880363917446"><a name="p1880363917446"></a><a name="p1880363917446"></a>bio.net.tls.server.ssl.lib.dir</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p117271431108"><a name="p117271431108"></a><a name="p117271431108"></a>openssl so文件所在目录路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1785365115412"><a name="p1785365115412"></a><a name="p1785365115412"></a>-</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p69821238134111"><a name="p69821238134111"></a><a name="p69821238134111"></a>-</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p11737552125719"><a name="p11737552125719"></a><a name="p11737552125719"></a>为空时，使用系统路径下的so文件。</p>
    <p id="p14654130219"><a name="p14654130219"></a><a name="p14654130219"></a>不为空时，安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</p>
    </td>
    </tr>
    <tr id="row10878637114215"><td class="cellrowborder" rowspan="13" valign="top" width="9.99%" headers="mcps1.2.7.1.1 "><p id="p13878737114219"><a name="p13878737114219"></a><a name="p13878737114219"></a>Cache</p>
    </td>
    <td class="cellrowborder" valign="top" width="20.01%" headers="mcps1.2.7.1.2 "><p id="p18166712721"><a name="p18166712721"></a><a name="p18166712721"></a>bio.cache.qos.enable</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.3 "><p id="p616510120215"><a name="p616510120215"></a><a name="p616510120215"></a>流量控制开关。</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.4 "><p id="p21641812427"><a name="p21641812427"></a><a name="p21641812427"></a>true</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.5 "><a name="ul6585194119593"></a><a name="ul6585194119593"></a><ul id="ul6585194119593"><li>false</li><li>true</li></ul>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.6 "><p id="p111631712823"><a name="p111631712823"></a><a name="p111631712823"></a>流量控制开关打开会影响到极限性能，建议性能用例场景关闭。</p>
    </td>
    </tr>
    <tr id="row1106474218"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p82181455185711"><a name="p82181455185711"></a><a name="p82181455185711"></a>bio.data.crc.enable</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p62173551573"><a name="p62173551573"></a><a name="p62173551573"></a>数据完整性校验开关。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p721725511573"><a name="p721725511573"></a><a name="p721725511573"></a>false</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><a name="ul13705204713590"></a><a name="ul13705204713590"></a><ul id="ul13705204713590"><li>false</li><li>true</li></ul>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p1719825513576"><a name="p1719825513576"></a><a name="p1719825513576"></a>数据完整性校验开关打开会增加数据读写时延，建议在问题定位场景使用。</p>
    </td>
    </tr>
    <tr id="row1695420593111"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1588016376425"><a name="p1588016376425"></a><a name="p1588016376425"></a>bio.segment.size_in_mb</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p68801737154215"><a name="p68801737154215"></a><a name="p68801737154215"></a>缓存资源粒度。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p18801237184215"><a name="p18801237184215"></a><a name="p18801237184215"></a>4</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p88801737184216"><a name="p88801737184216"></a><a name="p88801737184216"></a>1 ~ 16</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p551005125812"><a name="p551005125812"></a><a name="p551005125812"></a>单位MB。</p>
    </td>
    </tr>
    <tr id="row15341113013121"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1688014373423"><a name="p1688014373423"></a><a name="p1688014373423"></a>bio.mem.size_in_gb</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p20880237134210"><a name="p20880237134210"></a><a name="p20880237134210"></a>缓存资源内存容量。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p5880193734210"><a name="p5880193734210"></a><a name="p5880193734210"></a>50</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p2880637104216"><a name="p2880637104216"></a><a name="p2880637104216"></a>0 ~ 512</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><a name="ul11536195191410"></a><a name="ul11536195191410"></a><ul id="ul11536195191410"><li>禁止配置超过系统内存。</li><li>单位GB。</li><li>配置为0表示该节点不具备缓存功能。</li></ul>
    </td>
    </tr>
    <tr id="row3313232171513"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p2087993724219"><a name="p2087993724219"></a><a name="p2087993724219"></a>bio.disk.path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p98791137194211"><a name="p98791137194211"></a><a name="p98791137194211"></a>缓存资源磁盘列表。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1588043774220"><a name="p1588043774220"></a><a name="p1588043774220"></a>/dev/sdxx:/dev/sdyy</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p1888063713427"><a name="p1888063713427"></a><a name="p1888063713427"></a>-</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p118801937154213"><a name="p118801937154213"></a><a name="p118801937154213"></a>多个磁盘路径用冒号隔开，当前版本支持最多4块磁盘。</p>
    </td>
    </tr>
    <tr id="row189470444571"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p108781937184218"><a name="p108781937184218"></a><a name="p108781937184218"></a>bio.rcache.evict_water_level</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p118782373420"><a name="p118782373420"></a><a name="p118782373420"></a>读缓存淘汰水位。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p168793376423"><a name="p168793376423"></a><a name="p168793376423"></a>90</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p7879537144219"><a name="p7879537144219"></a><a name="p7879537144219"></a>0 ~ 100</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p987983774210"><a name="p987983774210"></a><a name="p987983774210"></a>表示使用读缓存百分比。</p>
    </td>
    </tr>
    <tr id="row14879193754211"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p16879173704210"><a name="p16879173704210"></a><a name="p16879173704210"></a>bio.cache.mem_read_write_ratio</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p1287903714428"><a name="p1287903714428"></a><a name="p1287903714428"></a>内存读写资源配比。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p7879137164210"><a name="p7879137164210"></a><a name="p7879137164210"></a>5:5</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p16879163744219"><a name="p16879163744219"></a><a name="p16879163744219"></a>0 ~ 10:10 ~ 0</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p14879837104216"><a name="p14879837104216"></a><a name="p14879837104216"></a>-</p>
    </td>
    </tr>
    <tr id="row587903764213"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1787993764216"><a name="p1787993764216"></a><a name="p1787993764216"></a>bio.cache.disk_read_write_ratio</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p20879437114216"><a name="p20879437114216"></a><a name="p20879437114216"></a>磁盘读写资源配比。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1387913711420"><a name="p1387913711420"></a><a name="p1387913711420"></a>5:5</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p20879437184211"><a name="p20879437184211"></a><a name="p20879437184211"></a>0 ~ 10:10 ~ 0</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p98791637124210"><a name="p98791637124210"></a><a name="p98791637124210"></a>-</p>
    </td>
    </tr>
    <tr id="row16731524185013"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1180781292113"><a name="p1180781292113"></a><a name="p1180781292113"></a>bio.work.scene</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p57312412501"><a name="p57312412501"></a><a name="p57312412501"></a>应用场景标记。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p177362455010"><a name="p177362455010"></a><a name="p177362455010"></a>none</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><a name="ul94151391008"></a><a name="ul94151391008"></a><ul id="ul94151391008"><li>none</li><li>bigdata</li></ul>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p1872739101811"><a name="p1872739101811"></a><a name="p1872739101811"></a>可选，默认为none</p>
    <a name="ul12187134918155"></a><a name="ul12187134918155"></a><ul id="ul12187134918155"><li>none：不存在使用约束。</li><li>bigdata：大数据场景，其主要区别是IO强制对齐。</li></ul>
    </td>
    </tr>
    <tr id="row775754812319"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p568111495231"><a name="p568111495231"></a><a name="p568111495231"></a>bio.work.io.alignsize</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p1757448192310"><a name="p1757448192310"></a><a name="p1757448192310"></a>IO对齐数据大小。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p475719480234"><a name="p475719480234"></a><a name="p475719480234"></a>1</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p175704815238"><a name="p175704815238"></a><a name="p175704815238"></a>1 ~ 4194304</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p1975734832315"><a name="p1975734832315"></a><a name="p1975734832315"></a>可选，单位B。</p>
    </td>
    </tr>
    <tr id="row96092419197"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p14351149196"><a name="p14351149196"></a><a name="p14351149196"></a>bio.wcache.evict_water_level</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p1043561441916"><a name="p1043561441916"></a><a name="p1043561441916"></a>写缓存淘汰水位。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p143521481917"><a name="p143521481917"></a><a name="p143521481917"></a>0</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p943531420191"><a name="p943531420191"></a><a name="p943531420191"></a>0 ~ 100</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p8435171431911"><a name="p8435171431911"></a><a name="p8435171431911"></a>可选，默认为0，表示使用写缓存资源百分比。</p>
    </td>
    </tr>
    <tr id="row520024811410"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1890314144"><a name="p1890314144"></a><a name="p1890314144"></a>bio.wcache.negotiate.delay</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p142001548343"><a name="p142001548343"></a><a name="p142001548343"></a>淘汰协商延迟。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p172001048948"><a name="p172001048948"></a><a name="p172001048948"></a>100</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p62003485411"><a name="p62003485411"></a><a name="p62003485411"></a>50 ~ 1000</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p1420014814411"><a name="p1420014814411"></a><a name="p1420014814411"></a>可选，默认100，单位ms。前台写性能敏感场景需要将该值调大，淘汰延迟增大；前台写性能不敏感可使用较小值，更快淘汰。</p>
    </td>
    </tr>
    <tr id="row123546291671"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p143545291971"><a name="p143545291971"></a><a name="p143545291971"></a>bio.trace.enable</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p935412295714"><a name="p935412295714"></a><a name="p935412295714"></a>流程统计开关。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p5355229372"><a name="p5355229372"></a><a name="p5355229372"></a>true</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><a name="ul1692149582"></a><a name="ul1692149582"></a><ul id="ul1692149582"><li>false</li><li>true</li></ul>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p498820331487"><a name="p498820331487"></a><a name="p498820331487"></a>流程统计开关打开会影响到极限性能，建议性能用例场景关闭。</p>
    </td>
    </tr>
    <tr id="row17881937184219"><td class="cellrowborder" rowspan="7" valign="top" width="9.99%" headers="mcps1.2.7.1.1 "><p id="p78811237164217"><a name="p78811237164217"></a><a name="p78811237164217"></a>Underfs</p>
    </td>
    <td class="cellrowborder" valign="top" width="20.01%" headers="mcps1.2.7.1.2 "><p id="p1887213198212"><a name="p1887213198212"></a><a name="p1887213198212"></a>bio.underfs.file_system_type</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.3 "><p id="p14872141914214"><a name="p14872141914214"></a><a name="p14872141914214"></a>后端存储系统类型。</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.4 "><p id="p387231913214"><a name="p387231913214"></a><a name="p387231913214"></a>ceph</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.5 "><a name="ul4723104711017"></a><a name="ul4723104711017"></a><ul id="ul4723104711017"><li>ceph</li><li>hdfs</li></ul>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.6 "><p id="p108729191216"><a name="p108729191216"></a><a name="p108729191216"></a>-</p>
    </td>
    </tr>
    <tr id="row101903922018"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p118811837114217"><a name="p118811837114217"></a><a name="p118811837114217"></a>bio.underfs.ceph.cfg.path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p11881837194217"><a name="p11881837194217"></a><a name="p11881837194217"></a>Ceph配置文件路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1488119373425"><a name="p1488119373425"></a><a name="p1488119373425"></a>/etc/ceph/ceph.conf</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p188193712422"><a name="p188193712422"></a><a name="p188193712422"></a>不为空。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p1988173716428"><a name="p1988173716428"></a><a name="p1988173716428"></a>选择ceph后必填选项，需要是真实存在的路径。</p>
    </td>
    </tr>
    <tr id="row78813378425"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p188112371420"><a name="p188112371420"></a><a name="p188112371420"></a>bio.underfs.ceph.cluster</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p78829373422"><a name="p78829373422"></a><a name="p78829373422"></a>Ceph集群名称。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p0882337134219"><a name="p0882337134219"></a><a name="p0882337134219"></a>ceph</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p168821437194211"><a name="p168821437194211"></a><a name="p168821437194211"></a>不为空。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p414512157211"><a name="p414512157211"></a><a name="p414512157211"></a>选择ceph后必填选项。</p>
    </td>
    </tr>
    <tr id="row5882123713426"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p0882237104217"><a name="p0882237104217"></a><a name="p0882237104217"></a>bio.underfs.ceph.user</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p158821337114214"><a name="p158821337114214"></a><a name="p158821337114214"></a>Ceph用户。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1882133710427"><a name="p1882133710427"></a><a name="p1882133710427"></a>client.admin</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p1888218379428"><a name="p1888218379428"></a><a name="p1888218379428"></a>不为空。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p7326132211216"><a name="p7326132211216"></a><a name="p7326132211216"></a>选择ceph后必填选项。</p>
    </td>
    </tr>
    <tr id="row488293715424"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p16882153744212"><a name="p16882153744212"></a><a name="p16882153744212"></a>bio.underfs.ceph.pool</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p1788213764211"><a name="p1788213764211"></a><a name="p1788213764211"></a>Ceph数据池。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p7882037184211"><a name="p7882037184211"></a><a name="p7882037184211"></a>0:jfspool0,1:jfspool1</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p10882437164212"><a name="p10882437164212"></a><a name="p10882437164212"></a>不为空。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p1388219376427"><a name="p1388219376427"></a><a name="p1388219376427"></a>选择ceph后必填选项，多个参数用英文逗号隔开。</p>
    </td>
    </tr>
    <tr id="row4732191613016"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p27321016183018"><a name="p27321016183018"></a><a name="p27321016183018"></a>bio.underfs.hdfs.name_node</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p87322016113013"><a name="p87322016113013"></a><a name="p87322016113013"></a>hadoop的NameNode。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p6732181615305"><a name="p6732181615305"></a><a name="p6732181615305"></a>default:0</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p1673211673011"><a name="p1673211673011"></a><a name="p1673211673011"></a>*.*.*.*/#，*为0 ~ 255，#为0 ~ 65535</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p724213822214"><a name="p724213822214"></a><a name="p724213822214"></a>可选，默认为default:0，格式：IP地址:端口号，表示使用hadoop配置文件中的IP地址和端口号。</p>
    </td>
    </tr>
    <tr id="row119817205303"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p11981820133020"><a name="p11981820133020"></a><a name="p11981820133020"></a>bio.underfs.hdfs.working_path</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p1998620153015"><a name="p1998620153015"></a><a name="p1998620153015"></a>文件在hdfs系统的存放路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p139819202306"><a name="p139819202306"></a><a name="p139819202306"></a>/hdfs</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p398420193013"><a name="p398420193013"></a><a name="p398420193013"></a>路径名长度小于或等于255的合法路径。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p598182093020"><a name="p598182093020"></a><a name="p598182093020"></a>可选，默认为/hdfs。</p>
    </td>
    </tr>
    <tr id="row1688213377428"><td class="cellrowborder" rowspan="6" valign="top" width="9.99%" headers="mcps1.2.7.1.1 "><p id="p18882183712423"><a name="p18882183712423"></a><a name="p18882183712423"></a>CM</p>
    </td>
    <td class="cellrowborder" valign="top" width="20.01%" headers="mcps1.2.7.1.2 "><p id="p53521054125614"><a name="p53521054125614"></a><a name="p53521054125614"></a>bio.cm.initial.nodes_count</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.3 "><p id="p13351185411567"><a name="p13351185411567"></a><a name="p13351185411567"></a>集群初始化期望节点数。</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.4 "><p id="p18351654165613"><a name="p18351654165613"></a><a name="p18351654165613"></a>2</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.5 "><p id="p733215414563"><a name="p733215414563"></a><a name="p733215414563"></a>2 ~ 256</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.6 "><p id="p118831837144219"><a name="p118831837144219"></a><a name="p118831837144219"></a>-</p>
    </td>
    </tr>
    <tr id="row229923317274"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1299123315273"><a name="p1299123315273"></a><a name="p1299123315273"></a>bio.cm.copy_num</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p1409164432717"><a name="p1409164432717"></a><a name="p1409164432717"></a>数据冗余度。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p15300433162719"><a name="p15300433162719"></a><a name="p15300433162719"></a>2</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p83001633112717"><a name="p83001633112717"></a><a name="p83001633112717"></a>2</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p19321017172814"><a name="p19321017172814"></a><a name="p19321017172814"></a>当前版本仅支持双副本。</p>
    </td>
    </tr>
    <tr id="row9841135016568"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p18883203719421"><a name="p18883203719421"></a><a name="p18883203719421"></a>bio.cm.pts_count</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p188314370426"><a name="p188314370426"></a><a name="p188314370426"></a>分区视图数量。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p5883237134212"><a name="p5883237134212"></a><a name="p5883237134212"></a>16</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p1088373720426"><a name="p1088373720426"></a><a name="p1088373720426"></a>2 ~ 8192</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p12841450185612"><a name="p12841450185612"></a><a name="p12841450185612"></a>-</p>
    </td>
    </tr>
    <tr id="row38831737204213"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p488373715429"><a name="p488373715429"></a><a name="p488373715429"></a>bio.cm.register_timeout_sec</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p6883837104212"><a name="p6883837104212"></a><a name="p6883837104212"></a>ZooKeeper心跳检测超时时间。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p16883163710421"><a name="p16883163710421"></a><a name="p16883163710421"></a>20</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p12883163754217"><a name="p12883163754217"></a><a name="p12883163754217"></a>10 ~ 60</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p68839370421"><a name="p68839370421"></a><a name="p68839370421"></a>单位s。</p>
    </td>
    </tr>
    <tr id="row15883173774218"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p488303719428"><a name="p488303719428"></a><a name="p488303719428"></a>bio.cm.register_perm_timeout_sec</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p14883173794216"><a name="p14883173794216"></a><a name="p14883173794216"></a>永久故障超时时间。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p1288303754219"><a name="p1288303754219"></a><a name="p1288303754219"></a>60</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p14883183794215"><a name="p14883183794215"></a><a name="p14883183794215"></a>60 ~ 600</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p13884113754213"><a name="p13884113754213"></a><a name="p13884113754213"></a>单位s。</p>
    </td>
    </tr>
    <tr id="row8884103715422"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p2884113712421"><a name="p2884113712421"></a><a name="p2884113712421"></a>bio.cm.zk_host</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p16210109123616"><a name="p16210109123616"></a><a name="p16210109123616"></a>ZooKeeper服务节点信息。</p>
    <p id="p19392141083711"><a name="p19392141083711"></a><a name="p19392141083711"></a>例如3节点ZK集群：127.0.0.1:2181,127.0.0.2:2181,127.0.0.3:2181。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p3884137184219"><a name="p3884137184219"></a><a name="p3884137184219"></a>-</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p5884163794218"><a name="p5884163794218"></a><a name="p5884163794218"></a>不为空</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p98846379420"><a name="p98846379420"></a><a name="p98846379420"></a>ZooKeeper使用的网段需要和业务IP地址网段保持一致。</p>
    </td>
    </tr>
    <tr id="row04761159183518"><td class="cellrowborder" rowspan="2" valign="top" width="9.99%" headers="mcps1.2.7.1.1 "><p id="p17477105973517"><a name="p17477105973517"></a><a name="p17477105973517"></a>Prometheus</p>
    </td>
    <td class="cellrowborder" valign="top" width="20.01%" headers="mcps1.2.7.1.2 "><p id="p4477115953512"><a name="p4477115953512"></a><a name="p4477115953512"></a>bio.prometheus.exposer</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.3 "><p id="p184771359143514"><a name="p184771359143514"></a><a name="p184771359143514"></a>Prometheus Server的地址和端口号。</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.4 "><p id="p4477205914358"><a name="p4477205914358"></a><a name="p4477205914358"></a>-</p>
    </td>
    <td class="cellrowborder" valign="top" width="15%" headers="mcps1.2.7.1.5 "><p id="p78041818162919"><a name="p78041818162919"></a><a name="p78041818162919"></a>*.*.*.*:#，*为0 ~ 255，#为0 ~ 65535</p>
    </td>
    <td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.7.1.6 "><p id="p2477105913356"><a name="p2477105913356"></a><a name="p2477105913356"></a>可选</p>
    </td>
    </tr>
    <tr id="row188709354364"><td class="cellrowborder" valign="top" headers="mcps1.2.7.1.1 "><p id="p1987023518366"><a name="p1987023518366"></a><a name="p1987023518366"></a>bio.prometheus.scrape_interval_sec</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.2 "><p id="p10870153513617"><a name="p10870153513617"></a><a name="p10870153513617"></a>Prometheus采样频率。</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.3 "><p id="p187010357369"><a name="p187010357369"></a><a name="p187010357369"></a>15</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.4 "><p id="p88703353366"><a name="p88703353366"></a><a name="p88703353366"></a>-</p>
    </td>
    <td class="cellrowborder" valign="top" headers="mcps1.2.7.1.5 "><p id="p587014357364"><a name="p587014357364"></a><a name="p587014357364"></a>可选，单位s。</p>
    </td>
    </tr>
    </tbody>
    </table>

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

<a name="zh-cn_topic_0000001775152198_table2936153410819"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001775152198_row293714342088"><th class="cellrowborder" valign="top" width="30%" id="mcps1.2.3.1.1"><p id="zh-cn_topic_0000001775152198_p893703412816"><a name="zh-cn_topic_0000001775152198_p893703412816"></a><a name="zh-cn_topic_0000001775152198_p893703412816"></a>文件</p>
</th>
<th class="cellrowborder" valign="top" width="70%" id="mcps1.2.3.1.2"><p id="zh-cn_topic_0000001775152198_p1493793418812"><a name="zh-cn_topic_0000001775152198_p1493793418812"></a><a name="zh-cn_topic_0000001775152198_p1493793418812"></a>说明</p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001775152198_row59376341581"><td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000001775152198_p1393723413816"><a name="zh-cn_topic_0000001775152198_p1393723413816"></a><a name="zh-cn_topic_0000001775152198_p1393723413816"></a>CA文件</p>
</td>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000001775152198_p149371345811"><a name="zh-cn_topic_0000001775152198_p149371345811"></a><a name="zh-cn_topic_0000001775152198_p149371345811"></a>一个自签名的证书，可以签发其它证书。格式为：PEM（*.pem）。</p>
</td>
</tr>
<tr id="row827613613717"><td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.3.1.1 "><p id="p82761561375"><a name="p82761561375"></a><a name="p82761561375"></a>吊销证书列表文件</p>
</td>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.2 "><p id="p32764693713"><a name="p32764693713"></a><a name="p32764693713"></a>给出吊销证书列表文件，格式为：PEM（*.crl）。可选，如无吊销证书，可以没有此文件。</p>
</td>
</tr>
<tr id="zh-cn_topic_0000001775152198_row7937183411811"><td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000001775152198_p199379341087"><a name="zh-cn_topic_0000001775152198_p199379341087"></a><a name="zh-cn_topic_0000001775152198_p199379341087"></a>Server端的证书</p>
</td>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000001775152198_p2937203410810"><a name="zh-cn_topic_0000001775152198_p2937203410810"></a><a name="zh-cn_topic_0000001775152198_p2937203410810"></a>由CA签发的证书，保证在有效期内。格式为：PEM chain（*.pem）。</p>
</td>
</tr>
<tr id="zh-cn_topic_0000001775152198_row269218244126"><td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000001775152198_p3692132415121"><a name="zh-cn_topic_0000001775152198_p3692132415121"></a><a name="zh-cn_topic_0000001775152198_p3692132415121"></a>Server端的证书对应的已加密私钥文件</p>
</td>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000001775152198_p66921124121211"><a name="zh-cn_topic_0000001775152198_p66921124121211"></a><a name="zh-cn_topic_0000001775152198_p66921124121211"></a>要与Server端证书对应，Server安装用户要知道这个私钥文件的口令。格式为：PEM encrypted（*.pem）。</p>
</td>
</tr>
<tr id="row9383193616272"><td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.3.1.1 "><p id="p178538351325"><a name="p178538351325"></a><a name="p178538351325"></a>Server端的私钥口令</p>
</td>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.2 "><p id="p17641143412218"><a name="p17641143412218"></a><a name="p17641143412218"></a>加密后的私钥口令存储文件，口令长度不超过10000字节。</p>
</td>
</tr>
<tr id="row171362321522"><td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.3.1.1 "><p id="p913653212212"><a name="p913653212212"></a><a name="p913653212212"></a>Server端的解密函数so</p>
</td>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.2 "><p id="p2136113210211"><a name="p2136113210211"></a><a name="p2136113210211"></a>用户提供的包含解密函数的so。</p>
</td>
</tr>
<tr id="row1853964712715"><td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.3.1.1 "><p id="p35396479720"><a name="p35396479720"></a><a name="p35396479720"></a>openssl，crypto so文件</p>
</td>
<td class="cellrowborder" valign="top" width="70%" headers="mcps1.2.3.1.2 "><p id="p7539347073"><a name="p7539347073"></a><a name="p7539347073"></a>可选，配置则使用用户提供的版本。</p>
</td>
</tr>
</tbody>
</table>

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

<a name="zh-cn_topic_0000001775152198_table2936153410819"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001775152198_row293714342088"><th class="cellrowborder" valign="top" width="24.279999999999998%" id="mcps1.2.3.1.1"><p id="zh-cn_topic_0000001775152198_p893703412816"><a name="zh-cn_topic_0000001775152198_p893703412816"></a><a name="zh-cn_topic_0000001775152198_p893703412816"></a>文件</p>
</th>
<th class="cellrowborder" valign="top" width="75.72%" id="mcps1.2.3.1.2"><p id="zh-cn_topic_0000001775152198_p1493793418812"><a name="zh-cn_topic_0000001775152198_p1493793418812"></a><a name="zh-cn_topic_0000001775152198_p1493793418812"></a>说明</p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001775152198_row59376341581"><td class="cellrowborder" valign="top" width="24.279999999999998%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000001775152198_p1393723413816"><a name="zh-cn_topic_0000001775152198_p1393723413816"></a><a name="zh-cn_topic_0000001775152198_p1393723413816"></a>CA文件</p>
</td>
<td class="cellrowborder" valign="top" width="75.72%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000001775152198_p149371345811"><a name="zh-cn_topic_0000001775152198_p149371345811"></a><a name="zh-cn_topic_0000001775152198_p149371345811"></a>一个自签名的证书，可以签发其它证书。格式为：PEM（*.pem）。</p>
</td>
</tr>
<tr id="row827613613717"><td class="cellrowborder" valign="top" width="24.279999999999998%" headers="mcps1.2.3.1.1 "><p id="p82761561375"><a name="p82761561375"></a><a name="p82761561375"></a>吊销证书列表文件</p>
</td>
<td class="cellrowborder" valign="top" width="75.72%" headers="mcps1.2.3.1.2 "><p id="p32764693713"><a name="p32764693713"></a><a name="p32764693713"></a>给出吊销证书列表文件，格式为：PEM（*.crl）。可选，如无吊销证书，可以没有此文件。</p>
</td>
</tr>
<tr id="zh-cn_topic_0000001775152198_row7937183411811"><td class="cellrowborder" valign="top" width="24.279999999999998%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000001775152198_p199379341087"><a name="zh-cn_topic_0000001775152198_p199379341087"></a><a name="zh-cn_topic_0000001775152198_p199379341087"></a>Client端的证书</p>
</td>
<td class="cellrowborder" valign="top" width="75.72%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000001775152198_p2937203410810"><a name="zh-cn_topic_0000001775152198_p2937203410810"></a><a name="zh-cn_topic_0000001775152198_p2937203410810"></a>由CA签发的证书，保证在有效期内。格式为：PEM chain（*.pem）。</p>
</td>
</tr>
<tr id="zh-cn_topic_0000001775152198_row269218244126"><td class="cellrowborder" valign="top" width="24.279999999999998%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000001775152198_p3692132415121"><a name="zh-cn_topic_0000001775152198_p3692132415121"></a><a name="zh-cn_topic_0000001775152198_p3692132415121"></a>Client端的证书对应的私钥文件</p>
</td>
<td class="cellrowborder" valign="top" width="75.72%" headers="mcps1.2.3.1.2 "><p id="zh-cn_topic_0000001775152198_p66921124121211"><a name="zh-cn_topic_0000001775152198_p66921124121211"></a><a name="zh-cn_topic_0000001775152198_p66921124121211"></a>要与Client端证书对应，安装用户要知道这个私钥文件的口令。格式为：PEM encrypted（*.pem）。</p>
</td>
</tr>
<tr id="row9383193616272"><td class="cellrowborder" valign="top" width="24.279999999999998%" headers="mcps1.2.3.1.1 "><p id="p178538351325"><a name="p178538351325"></a><a name="p178538351325"></a>Client端的私钥口令</p>
</td>
<td class="cellrowborder" valign="top" width="75.72%" headers="mcps1.2.3.1.2 "><p id="p17641143412218"><a name="p17641143412218"></a><a name="p17641143412218"></a>加密后的私钥口令存储文件，口令长度不超过10000字节。</p>
</td>
</tr>
<tr id="row89611825181815"><td class="cellrowborder" valign="top" width="24.279999999999998%" headers="mcps1.2.3.1.1 "><p id="p913653212212"><a name="p913653212212"></a><a name="p913653212212"></a>Client端的解密函数so</p>
</td>
<td class="cellrowborder" valign="top" width="75.72%" headers="mcps1.2.3.1.2 "><p id="p2136113210211"><a name="p2136113210211"></a><a name="p2136113210211"></a>用户提供的包含解密函数的so。</p>
</td>
</tr>
<tr id="row16126823171820"><td class="cellrowborder" valign="top" width="24.279999999999998%" headers="mcps1.2.3.1.1 "><p id="p35396479720"><a name="p35396479720"></a><a name="p35396479720"></a>openssl，crypto so文件</p>
</td>
<td class="cellrowborder" valign="top" width="75.72%" headers="mcps1.2.3.1.2 "><p id="p7539347073"><a name="p7539347073"></a><a name="p7539347073"></a>可选，配置则使用用户提供的版本。</p>
</td>
</tr>
</tbody>
</table>

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

<a name="table817212119175"></a>
<table><thead align="left"><tr id="row1217281131718"><th class="cellrowborder" valign="top" width="11.379999999999999%" id="mcps1.2.5.1.1"><p id="p2172191181718"><a name="p2172191181718"></a><a name="p2172191181718"></a>场景</p>
</th>
<th class="cellrowborder" valign="top" width="28.62%" id="mcps1.2.5.1.2"><p id="p191722119174"><a name="p191722119174"></a><a name="p191722119174"></a>影响</p>
</th>
<th class="cellrowborder" valign="top" width="30%" id="mcps1.2.5.1.3"><p id="p181721511191718"><a name="p181721511191718"></a><a name="p181721511191718"></a>处理方式</p>
</th>
<th class="cellrowborder" valign="top" width="30%" id="mcps1.2.5.1.4"><p id="p1517210111175"><a name="p1517210111175"></a><a name="p1517210111175"></a>限制</p>
</th>
</tr>
</thead>
<tbody><tr id="row1717291141720"><td class="cellrowborder" valign="top" width="11.379999999999999%" headers="mcps1.2.5.1.1 "><p id="p01721811111710"><a name="p01721811111710"></a><a name="p01721811111710"></a>缓存客户端故障</p>
</td>
<td class="cellrowborder" valign="top" width="28.62%" headers="mcps1.2.5.1.2 "><a name="ul194012029151812"></a><a name="ul194012029151812"></a><ul id="ul194012029151812"><li>写资源配额泄漏。</li><li>分发的请求响应失败。</li><li>通信链路断开。</li><li>创建的线性空间失效。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.3 "><a name="ul2353103142515"></a><a name="ul2353103142515"></a><ul id="ul2353103142515"><li>缓存服务端通过链路断开事件感知客户端退出，回收写资源配额，封存线性空间并将数据淘汰到后端存储。</li><li>删除失效的链路信息。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.4 "><p id="p12682124416376"><a name="p12682124416376"></a><a name="p12682124416376"></a>无。</p>
</td>
</tr>
<tr id="row11172611131712"><td class="cellrowborder" valign="top" width="11.379999999999999%" headers="mcps1.2.5.1.1 "><p id="p14172101117176"><a name="p14172101117176"></a><a name="p14172101117176"></a>缓存客户端恢复</p>
</td>
<td class="cellrowborder" valign="top" width="28.62%" headers="mcps1.2.5.1.2 "><a name="ul2251104720244"></a><a name="ul2251104720244"></a><ul id="ul2251104720244"><li>重新建立通信链路。</li><li>缓存客户端重新执行初始化和上电流程。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.3 "><p id="p8747152018254"><a name="p8747152018254"></a><a name="p8747152018254"></a>缓存服务端处理建链请求。</p>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.4 "><p id="p122497419296"><a name="p122497419296"></a><a name="p122497419296"></a>无。</p>
</td>
</tr>
</tbody>
</table>

## UBS IO进程故障/恢复<a name="ZH-CN_TOPIC_0000002521700650"></a>

分离部署模式下UBS IO存在独立缓存进程，融合部署模式下UBS IO和JuiceFS加载到同一个进程，该进程主要负责缓存客户端的请求处理，数据读写缓存和资源管理等业务处理，因此需要处理缓存进程故障的故障模式。

**表 1**  缓存进程故障模式

<a name="table817212119175"></a>
<table><thead align="left"><tr id="row1217281131718"><th class="cellrowborder" valign="top" width="10%" id="mcps1.2.5.1.1"><p id="p2172191181718"><a name="p2172191181718"></a><a name="p2172191181718"></a>场景</p>
</th>
<th class="cellrowborder" valign="top" width="30%" id="mcps1.2.5.1.2"><p id="p191722119174"><a name="p191722119174"></a><a name="p191722119174"></a>影响</p>
</th>
<th class="cellrowborder" valign="top" width="29.970000000000002%" id="mcps1.2.5.1.3"><p id="p181721511191718"><a name="p181721511191718"></a><a name="p181721511191718"></a>处理方式</p>
</th>
<th class="cellrowborder" valign="top" width="30.03%" id="mcps1.2.5.1.4"><p id="p1517210111175"><a name="p1517210111175"></a><a name="p1517210111175"></a>限制</p>
</th>
</tr>
</thead>
<tbody><tr id="row1717291141720"><td class="cellrowborder" valign="top" width="10%" headers="mcps1.2.5.1.1 "><p id="p01721811111710"><a name="p01721811111710"></a><a name="p01721811111710"></a>缓存进程故障</p>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.2 "><a name="ul194012029151812"></a><a name="ul194012029151812"></a><ul id="ul194012029151812"><li>写缓存的副本数据丢失。</li><li>读缓存的对象数据丢失。</li><li>SDK端请求分发失败。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="29.970000000000002%" headers="mcps1.2.5.1.3 "><a name="ul2353103142515"></a><a name="ul2353103142515"></a><ul id="ul2353103142515"><li>通过ZooKeeper心跳感知到缓存进程故障，通知集群管理更新视图，发布更新后的视图。</li><li>受进程故障影响的分区数据强制淘汰到后端存储。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="30.03%" headers="mcps1.2.5.1.4 "><a name="ul1490634010266"></a><a name="ul1490634010266"></a><ul id="ul1490634010266"><li>缓存进程临时故障仅修改分区视图。</li><li>缓存进程永久故障，集群管理会将该节点移除集群，变更节点视图和分区视图。</li><li>临时故障时间窗口期可配置。</li></ul>
</td>
</tr>
<tr id="row11172611131712"><td class="cellrowborder" valign="top" width="10%" headers="mcps1.2.5.1.1 "><p id="p14172101117176"><a name="p14172101117176"></a><a name="p14172101117176"></a>缓存进程恢复</p>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.2 "><p id="p457717129261"><a name="p457717129261"></a><a name="p457717129261"></a>读写缓存功能恢复。</p>
</td>
<td class="cellrowborder" valign="top" width="29.970000000000002%" headers="mcps1.2.5.1.3 "><a name="ul1169710122920"></a><a name="ul1169710122920"></a><ul id="ul1169710122920"><li>临时故障恢复：通过ZooKeeper心跳感知到缓存进程恢复，通知集群管理更新视图，视图更新后再发布新视图。</li><li>永久故障恢复：执行扩容流程，请求重新加入集群。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="30.03%" headers="mcps1.2.5.1.4 "><p id="p122497419296"><a name="p122497419296"></a><a name="p122497419296"></a>无。</p>
</td>
</tr>
</tbody>
</table>

## 缓存节点故障/恢复<a name="ZH-CN_TOPIC_0000002552740621"></a>

UBS IO要求部署在计算节点上，对外提供分布式读写缓存服务，因此需要处理部署UBS IO的计算节点故障的故障模式。

**表 1**  缓存节点故障模式

<a name="table817212119175"></a>
<table><thead align="left"><tr id="row1217281131718"><th class="cellrowborder" valign="top" width="25%" id="mcps1.2.5.1.1"><p id="p2172191181718"><a name="p2172191181718"></a><a name="p2172191181718"></a>场景</p>
</th>
<th class="cellrowborder" valign="top" width="26.41%" id="mcps1.2.5.1.2"><p id="p191722119174"><a name="p191722119174"></a><a name="p191722119174"></a>影响</p>
</th>
<th class="cellrowborder" valign="top" width="23.59%" id="mcps1.2.5.1.3"><p id="p181721511191718"><a name="p181721511191718"></a><a name="p181721511191718"></a>处理方式</p>
</th>
<th class="cellrowborder" valign="top" width="25%" id="mcps1.2.5.1.4"><p id="p1517210111175"><a name="p1517210111175"></a><a name="p1517210111175"></a>限制</p>
</th>
</tr>
</thead>
<tbody><tr id="row1717291141720"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.5.1.1 "><p id="p01721811111710"><a name="p01721811111710"></a><a name="p01721811111710"></a>缓存节点故障</p>
</td>
<td class="cellrowborder" valign="top" width="26.41%" headers="mcps1.2.5.1.2 "><a name="ul194012029151812"></a><a name="ul194012029151812"></a><ul id="ul194012029151812"><li>写缓存的副本数据丢失。</li><li>读缓存的对象数据丢失。</li><li>SDK端请求分发失败。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="23.59%" headers="mcps1.2.5.1.3 "><a name="ul2353103142515"></a><a name="ul2353103142515"></a><ul id="ul2353103142515"><li>通过ZooKeeper心跳感知到缓存进程故障，通知集群管理更新视图，发布更新后的视图。</li><li>受进程故障影响的分区数据强制淘汰到后端存储。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.5.1.4 "><a name="ul1490634010266"></a><a name="ul1490634010266"></a><ul id="ul1490634010266"><li>缓存节点临时故障仅修改节点视图和分区视图的状态。</li><li>缓存节点永久故障，集群管理会将该节点移除集群，变更节点视图和分区视图。</li><li>临时故障时间窗口期可配置。</li></ul>
</td>
</tr>
<tr id="row11172611131712"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.5.1.1 "><p id="p14172101117176"><a name="p14172101117176"></a><a name="p14172101117176"></a>缓存节点恢复</p>
</td>
<td class="cellrowborder" valign="top" width="26.41%" headers="mcps1.2.5.1.2 "><p id="p12526122811269"><a name="p12526122811269"></a><a name="p12526122811269"></a>读写缓存功能恢复。</p>
</td>
<td class="cellrowborder" valign="top" width="23.59%" headers="mcps1.2.5.1.3 "><a name="ul1169710122920"></a><a name="ul1169710122920"></a><ul id="ul1169710122920"><li>临时故障恢复：通过ZooKeeper心跳感知到缓存进程恢复，通知集群管理更新视图，视图更新后再发布新视图。</li><li>永久故障恢复：执行扩容流程，请求重新加入集群。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.5.1.4 "><p id="p122497419296"><a name="p122497419296"></a><a name="p122497419296"></a>无。</p>
</td>
</tr>
</tbody>
</table>

## UBS IO通信故障/恢复<a name="ZH-CN_TOPIC_0000002521860666"></a>

缓存客户端给服务端发送请求，缓存服务端之间消息交互等场景都会使用网卡进行通信，当网卡遭遇故障时会导致通信消息失败，比如请求消息发送失败，无法接收到响应等情况发生，因此需要处理通信网卡故障的故障模式。

**表 1**  通信网卡故障模式

<a name="table817212119175"></a>
<table><thead align="left"><tr id="row1217281131718"><th class="cellrowborder" valign="top" width="25%" id="mcps1.2.5.1.1"><p id="p2172191181718"><a name="p2172191181718"></a><a name="p2172191181718"></a>场景</p>
</th>
<th class="cellrowborder" valign="top" width="26.41%" id="mcps1.2.5.1.2"><p id="p191722119174"><a name="p191722119174"></a><a name="p191722119174"></a>影响</p>
</th>
<th class="cellrowborder" valign="top" width="23.59%" id="mcps1.2.5.1.3"><p id="p181721511191718"><a name="p181721511191718"></a><a name="p181721511191718"></a>处理方式</p>
</th>
<th class="cellrowborder" valign="top" width="25%" id="mcps1.2.5.1.4"><p id="p1517210111175"><a name="p1517210111175"></a><a name="p1517210111175"></a>限制</p>
</th>
</tr>
</thead>
<tbody><tr id="row1717291141720"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.5.1.1 "><p id="p01721811111710"><a name="p01721811111710"></a><a name="p01721811111710"></a>通信网卡故障</p>
</td>
<td class="cellrowborder" valign="top" width="26.41%" headers="mcps1.2.5.1.2 "><a name="ul194012029151812"></a><a name="ul194012029151812"></a><ul id="ul194012029151812"><li>SDK端请求发送失败。</li><li>SDK端请求接收超时。</li><li>分区视图接收失败。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="23.59%" headers="mcps1.2.5.1.3 "><a name="ul2353103142515"></a><a name="ul2353103142515"></a><ul id="ul2353103142515"><li>通过ZooKeeper心跳感知到通信网卡故障，通知集群管理更新视图，视图更新后再发布新视图。</li><li>受网卡故障影响的分区数据强制淘汰到后端存储。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.5.1.4 "><a name="ul383914791514"></a><a name="ul383914791514"></a><ul id="ul383914791514"><li>支持端口被占用、防火墙和网卡DOWN故障场景。</li><li><span>不支持网络丢包、网络错包、网卡单通和链路闪断等网卡异常场景</span>。</li></ul>
</td>
</tr>
<tr id="row11172611131712"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.5.1.1 "><p id="p14172101117176"><a name="p14172101117176"></a><a name="p14172101117176"></a>通信网卡恢复</p>
</td>
<td class="cellrowborder" valign="top" width="26.41%" headers="mcps1.2.5.1.2 "><a name="ul2251104720244"></a><a name="ul2251104720244"></a><ul id="ul2251104720244"><li>请求发送功能恢复。</li><li>读写缓存各个流程支持重入。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="23.59%" headers="mcps1.2.5.1.3 "><p id="p7642164011266"><a name="p7642164011266"></a><a name="p7642164011266"></a>通过ZooKeeper心跳感知到通信网卡恢复，通知集群管理更新视图，视图更新后再发布新视图。</p>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.2.5.1.4 "><p id="p122497419296"><a name="p122497419296"></a><a name="p122497419296"></a>无。</p>
</td>
</tr>
</tbody>
</table>

## 后端存储系统异常<a name="ZH-CN_TOPIC_0000002521700652"></a>

UBS IO分布式缓存层依赖后端存储系统作为大容量数据持久化存储池，因此需要处理后端存储系统异常故障模式。

**表 1**  后端存储系统故障模式

<a name="table817212119175"></a>
<table><thead align="left"><tr id="row1217281131718"><th class="cellrowborder" valign="top" width="10%" id="mcps1.2.5.1.1"><p id="p2172191181718"><a name="p2172191181718"></a><a name="p2172191181718"></a>场景</p>
</th>
<th class="cellrowborder" valign="top" width="30%" id="mcps1.2.5.1.2"><p id="p191722119174"><a name="p191722119174"></a><a name="p191722119174"></a>影响</p>
</th>
<th class="cellrowborder" valign="top" width="30%" id="mcps1.2.5.1.3"><p id="p181721511191718"><a name="p181721511191718"></a><a name="p181721511191718"></a>处理方式</p>
</th>
<th class="cellrowborder" valign="top" width="30%" id="mcps1.2.5.1.4"><p id="p1517210111175"><a name="p1517210111175"></a><a name="p1517210111175"></a>限制</p>
</th>
</tr>
</thead>
<tbody><tr id="row1717291141720"><td class="cellrowborder" valign="top" width="10%" headers="mcps1.2.5.1.1 "><p id="p01721811111710"><a name="p01721811111710"></a><a name="p01721811111710"></a>后端存储系统故障</p>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.2 "><a name="ul194012029151812"></a><a name="ul194012029151812"></a><ul id="ul194012029151812"><li>写缓存的对象数据无法淘汰。</li><li>读缓存预取加载对象数据失败。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.3 "><p id="p1117210117172"><a name="p1117210117172"></a><a name="p1117210117172"></a>感知后端存储系统故障并作出相应告警，当前告警方式为日志打印。</p>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.4 "><p id="p717251110172"><a name="p717251110172"></a><a name="p717251110172"></a>无。</p>
</td>
</tr>
<tr id="row11172611131712"><td class="cellrowborder" valign="top" width="10%" headers="mcps1.2.5.1.1 "><p id="p14172101117176"><a name="p14172101117176"></a><a name="p14172101117176"></a>后端存储系统恢复</p>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.2 "><a name="ul1113239172012"></a><a name="ul1113239172012"></a><ul id="ul1113239172012"><li>写缓存淘汰功能恢复正常。</li><li>读缓存预取加载功能恢复正常。</li><li>前台业务恢复正常。</li></ul>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.3 "><p id="p111731711161710"><a name="p111731711161710"></a><a name="p111731711161710"></a>感知后端存储系统恢复并重连成功。</p>
</td>
<td class="cellrowborder" valign="top" width="30%" headers="mcps1.2.5.1.4 "><a name="ul199412454711"></a><a name="ul199412454711"></a><ul id="ul199412454711"><li>写缓存淘汰功能恢复，由于后端存储性能限制，恢复到正常水位需要一段时间。</li><li>写缓存淘汰性能限制，前台业务性能需要一段时间逐步恢复为正常水平。</li></ul>
</td>
</tr>
</tbody>
</table>

## 缓存磁盘故障<a name="ZH-CN_TOPIC_0000002521700660"></a>

UBS IO分布式缓存层使用缓存磁盘NVMe SSD作为二级缓存介质，用于持久化读写缓存数据，需要有效应对缓存磁盘故障。

**表 1**  缓存磁盘故障模式

<a name="table1358583917718"></a>
<table><thead align="left"><tr id="row75851239871"><th class="cellrowborder" valign="top" width="7.75%" id="mcps1.2.5.1.1"><p id="p05861139773"><a name="p05861139773"></a><a name="p05861139773"></a>场景</p>
</th>
<th class="cellrowborder" valign="top" width="27.82%" id="mcps1.2.5.1.2"><p id="p8586173919717"><a name="p8586173919717"></a><a name="p8586173919717"></a>影响</p>
</th>
<th class="cellrowborder" valign="top" width="35.15%" id="mcps1.2.5.1.3"><p id="p658618397717"><a name="p658618397717"></a><a name="p658618397717"></a>处理方式</p>
</th>
<th class="cellrowborder" valign="top" width="29.28%" id="mcps1.2.5.1.4"><p id="p1658613398710"><a name="p1658613398710"></a><a name="p1658613398710"></a>限制</p>
</th>
</tr>
</thead>
<tbody><tr id="row1158623915716"><td class="cellrowborder" valign="top" width="7.75%" headers="mcps1.2.5.1.1 "><p id="p358610399716"><a name="p358610399716"></a><a name="p358610399716"></a>新磁盘加入</p>
</td>
<td class="cellrowborder" valign="top" width="27.82%" headers="mcps1.2.5.1.2 "><p id="p258633918714"><a name="p258633918714"></a><a name="p258633918714"></a>接入新盘过程中，前台I/O性能下降，业务受影响的时间不超过60s。</p>
</td>
<td class="cellrowborder" valign="top" width="35.15%" headers="mcps1.2.5.1.3 "><p id="p15860395713"><a name="p15860395713"></a><a name="p15860395713"></a>新磁盘加入和识别、配置文件更新、加盘事件上报、触发视图重均衡、淘汰缓存数据和创建新缓存。</p>
</td>
<td class="cellrowborder" valign="top" width="29.28%" headers="mcps1.2.5.1.4 "><a name="ul14711135019810"></a><a name="ul14711135019810"></a><ul id="ul14711135019810"><li>单次支持加1块盘，单节点可用盘最大支持4块盘，否则报错。</li><li>新加入磁盘容量大小规格，与集群磁盘规格保持一致。</li></ul>
</td>
</tr>
<tr id="row115861639571"><td class="cellrowborder" valign="top" width="7.75%" headers="mcps1.2.5.1.1 "><p id="p358615397717"><a name="p358615397717"></a><a name="p358615397717"></a>故障磁盘剔除</p>
</td>
<td class="cellrowborder" valign="top" width="27.82%" headers="mcps1.2.5.1.2 "><p id="p1858617398716"><a name="p1858617398716"></a><a name="p1858617398716"></a>故障检测与剔除期间，前台I/O性能下降，业务影响的时间不超过60s。</p>
</td>
<td class="cellrowborder" valign="top" width="35.15%" headers="mcps1.2.5.1.3 "><p id="p15586239372"><a name="p15586239372"></a><a name="p15586239372"></a>上报磁盘故障到集群管理、完成受影响分区数据淘汰后上报处理、触发分区视图重计算与发布（期间故障分区IO自动重试）。</p>
</td>
<td class="cellrowborder" valign="top" width="29.28%" headers="mcps1.2.5.1.4 "><a name="ul136663571989"></a><a name="ul136663571989"></a><ul id="ul136663571989"><li>仅支持单盘故障，同时双盘故障会导致丢数据。</li></ul>
</td>
</tr>
</tbody>
</table>

## 缓存节点扩容<a name="ZH-CN_TOPIC_0000002552860607"></a>

-   当前版本支持的集群规模最小为2节点，最大为256节点。因此最大扩容至集群256个节点。
-   支持同时批量进行扩容的缓存节点数为32个。
-   不支持故障处理期间进行缓存节点扩容操作。

# 后端存储使用说明<a name="ZH-CN_TOPIC_0000002521860648"></a>

UBS IO会使用后端存储系统作为数据的持久化容量层。当前仅支持Ceph和HDFS分布式存储系统，后端存储系统由用户提供并创建UBS IO使用的存储池或挂载目录。因为后端存储系统属于用户管理和维护范围，需要用户保证提供给UBS IO使用的存储池或挂载目录不会被其它软件应用使用和访问，否则会破坏应用数据和构成目录越权风险。

# 公网地址声明<a name="ZH-CN_TOPIC_0000002552740627"></a>

以下表格中列出了产品中包含的公网地址，没有安全风险。

<a name="table7827161502410"></a>
<table><thead align="left"><tr id="row8827181502414"><th class="cellrowborder" valign="top" width="39.160000000000004%" id="mcps1.1.3.1.1"><p id="p682701532420"><a name="p682701532420"></a><a name="p682701532420"></a>网址</p>
</th>
<th class="cellrowborder" valign="top" width="60.84%" id="mcps1.1.3.1.2"><p id="p1582761532419"><a name="p1582761532419"></a><a name="p1582761532419"></a>说明</p>
</th>
</tr>
</thead>
<tbody><tr id="row128271415162416"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p19796154019219"><a name="p19796154019219"></a><a name="p19796154019219"></a>http://license.coscl.org.cn/MulanPSL2</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p1230614015221"><a name="p1230614015221"></a><a name="p1230614015221"></a>该网址为开源许可证网站，为UBS IO的开源信息声明，无安全风险。</p>
</td>
</tr>
<tr id="row12612112512112"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p1161252514219"><a name="p1161252514219"></a><a name="p1161252514219"></a>http://www.apache.org/licenses/LICENSE-2.0</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p75234482224"><a name="p75234482224"></a><a name="p75234482224"></a>该网址为开源许可证网站，为Hadoop以及Zookeeper的开源信息声明，无安全风险。</p>
</td>
</tr>
<tr id="row4137823142114"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p1113722372115"><a name="p1113722372115"></a><a name="p1113722372115"></a>https://github.com/nginx/nginx/blob/master/LICENSE</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p013711237219"><a name="p013711237219"></a><a name="p013711237219"></a>该网址为开源许可证网站，为使用的红黑树Nginx的开源信息声明，无安全风险。</p>
</td>
</tr>
<tr id="row1412120112115"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p34121020152115"><a name="p34121020152115"></a><a name="p34121020152115"></a>https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/googletest.git</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p241272092111"><a name="p241272092111"></a><a name="p241272092111"></a>该网址为ut使用的googletest代码仓地址，无安全风险。</p>
</td>
</tr>
<tr id="row5321111714214"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p432181792116"><a name="p432181792116"></a><a name="p432181792116"></a>https://issues.apache.org/jira/browse/ZOOKEEPER-1355</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p123211317152118"><a name="p123211317152118"></a><a name="p123211317152118"></a>该网址为zookeeper开源头文件的声明issue网址，无安全风险。</p>
</td>
</tr>
<tr id="row9423914102111"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p5424101416217"><a name="p5424101416217"></a><a name="p5424101416217"></a>https://gitcode.com/GitHub_Trending/sp/spdlog.git</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p164241114152112"><a name="p164241114152112"></a><a name="p164241114152112"></a>该网址为引入的三方库spdlog的地址，无安全风险。</p>
</td>
</tr>
<tr id="row4989103742614"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p18989537152610"><a name="p18989537152610"></a><a name="p18989537152610"></a>https://gitcode.com/gh_mirrors/pr/prometheus-cpp.git</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p1653314392718"><a name="p1653314392718"></a><a name="p1653314392718"></a>该网址为引入的三方库prometheus的地址，无安全风险。</p>
</td>
</tr>
<tr id="row145003355267"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p1050023512610"><a name="p1050023512610"></a><a name="p1050023512610"></a>https://gitcode.com/openeuler/libboundscheck.git</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p19380204414279"><a name="p19380204414279"></a><a name="p19380204414279"></a>该网址为引入的三方库libboundscheck的地址，无安全风险。</p>
</td>
</tr>
<tr id="row1144603319266"><td class="cellrowborder" valign="top" width="39.160000000000004%" headers="mcps1.1.3.1.1 "><p id="p1446123322612"><a name="p1446123322612"></a><a name="p1446123322612"></a>https://gitcode.com/openeuler/ubs-comm.git</p>
</td>
<td class="cellrowborder" valign="top" width="60.84%" headers="mcps1.1.3.1.2 "><p id="p1694744482716"><a name="p1694744482716"></a><a name="p1694744482716"></a>该网址为引入的三方库ubs-comm的地址，无安全风险。</p>
</td>
</tr>
</tbody>
</table>

# 账户一览表<a name="ZH-CN_TOPIC_0000002552740599"></a>

>![](public_sys-resources/icon-notice.gif) **须知：**
>用户创建的安装用户需定期修改密码。

<a name="table1143674515419"></a>
<table><thead align="left"><tr id="row5437204519419"><th class="cellrowborder" valign="top" width="25%" id="mcps1.1.5.1.1"><p id="p957781624414"><a name="p957781624414"></a><a name="p957781624414"></a>用户</p>
</th>
<th class="cellrowborder" valign="top" width="25%" id="mcps1.1.5.1.2"><p id="p17577191654412"><a name="p17577191654412"></a><a name="p17577191654412"></a>描述</p>
</th>
<th class="cellrowborder" valign="top" width="25%" id="mcps1.1.5.1.3"><p id="p1257711166446"><a name="p1257711166446"></a><a name="p1257711166446"></a>初始密码</p>
</th>
<th class="cellrowborder" valign="top" width="25%" id="mcps1.1.5.1.4"><p id="p357713169444"><a name="p357713169444"></a><a name="p357713169444"></a>密码修改方法</p>
</th>
</tr>
</thead>
<tbody><tr id="row1791731171414"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.1.5.1.1 "><p id="p15801231151417"><a name="p15801231151417"></a><a name="p15801231151417"></a>bioadmin</p>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.1.5.1.2 "><p id="p980133191412"><a name="p980133191412"></a><a name="p980133191412"></a>分离部署场景UBS IO Server运行用户。</p>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.1.5.1.3 "><p id="p14796433141"><a name="p14796433141"></a><a name="p14796433141"></a>用户自定义。</p>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.1.5.1.4 "><p id="p174791543171418"><a name="p174791543171418"></a><a name="p174791543171418"></a>使用<strong id="b647984321417"><a name="b647984321417"></a><a name="b647984321417"></a>passwd</strong>命令修改。</p>
</td>
</tr>
<tr id="row1937312376105"><td class="cellrowborder" valign="top" width="25%" headers="mcps1.1.5.1.1 "><p id="p937410374103"><a name="p937410374103"></a><a name="p937410374103"></a>juiceadmin</p>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.1.5.1.2 "><p id="p33740379102"><a name="p33740379102"></a><a name="p33740379102"></a>融合部署场景上层调用组件运行用户。</p>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.1.5.1.3 "><p id="p10374133751017"><a name="p10374133751017"></a><a name="p10374133751017"></a>用户自定义。</p>
</td>
<td class="cellrowborder" valign="top" width="25%" headers="mcps1.1.5.1.4 "><p id="p4374173710104"><a name="p4374173710104"></a><a name="p4374173710104"></a>使用<strong id="b1567618931113"><a name="b1567618931113"></a><a name="b1567618931113"></a>passwd</strong>命令修改。</p>
</td>
</tr>
</tbody>
</table>

# 版权说明<a name="ZH-CN_TOPIC_0000002553545773"></a>

Copyright \(c\) Huawei Technologies Co., Ltd. 2025. All rights reserved.
