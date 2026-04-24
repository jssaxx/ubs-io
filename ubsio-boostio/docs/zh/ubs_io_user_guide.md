# 软件启动

## 配置RDMA无损

软件安装环境上如配置有RoCE网卡，且预设UBS IO网络协议使用RDMA，则首先需要在环境上配置RDMA无损参数，防止数据通信过程中报错，具体配置方法请参考各网卡厂商的官方RDMA使用说明书。

## 启动UBS IO

### 融合部署模式

>![](figures/icon-notice.gif) **须知：**
>
> 分离部署和独立部署模式均可使用systemd命令启动UBS IO，如希望bio\_daemon进程被操作系统接管，在发生故障或异常情况下能够支持自动重启进行恢复，以保证业务连续性，可以使用此种方式。具体配置可查询官方资料。

融合部署模式下UBS IO不存在独立运行进程，以动态链接库的方式加载到JuiceFS进程。因此如需使用UBS IO功能，需要启动JuiceFS进程。  

### 分离部署模式

>![](figures/icon-notice.gif) **须知：**
>
>- 分离部署和独立部署模式均可使用systemd命令启动UBS IO，如希望bio\_daemon进程接受操作系统接管，在发生故障或异常情况下能够支持自动重启进行恢复，以保证业务连续性，可以使用此种方式。具体配置可查询官方资料。
>- 分离部署和独立部署模式下UBS IO都存在独立运行进程bio\_daemon，明确规定在单个物理节点上有且仅有一个bio\_daemon进程，禁止存在多个bio\_daemon进程，此误操作会导致数据不一致风险。

分离部署模式下UBS IO有独立的运行进程，要求首先启动UBS IO独立进程bio\_daemon，然后再启动JuiceFS进程。在JuiceFS进程启动过程中将会自动加载UBS IO SDK链接库。bio\_daemon进程启动方式支持手动后台启动。

用户希望自主监控bio\_daemon进程，在发生故障和异常情况下能够立即进行故障排查和定位，防止故障或异常隐藏和扩散，推荐使用此种方式，主要适用于应用开发和功能调试场景。

### 独立部署模式

**注意事项**

- 分离部署和独立部署模式下UBS IO都存在独立运行进程bio\_daemon，明确规定在单个物理节点上有且仅有一个bio\_daemon进程，不允许存在多个bio\_daemon进程，此误操作会导致数据不一致风险。
- 在不支持数据缓存功能的计算节点启动bio\_daemon独立运行进程前，需要设置bio配置文件中的缓存资源为零，配置项如下所示。

    > ```cmd
    > bio.mem.size_in_gb = 0
    > bio.disk.path = /path/to/disk
    > ```

**详细说明**    
独立部署模式下UBS IO有独立的运行进程，要求首先启动UBS IO独立进程bio\_daemon，然后再启动JuiceFS进程。在JuiceFS进程启动过程中将自动加载UBS IO SDK链接库。bio\_daemon进程启动方式支持手动后台启动。

用户希望自主监控bio\_daemon进程，在发生故障和异常情况下能够立即进行故障排查和定位，防止故障或异常隐藏和扩散，推荐使用独立部署方式，主要适用于应用开发和功能调试场景。

# 安全加固

## 设置登录会话超时时间

登录会话30分钟（或更短）的时间内没有活动的情况下属于超时。

1. 登录安装UBS IO组件的节点。
2. 执行以下命令，打开“/etc/profile“文件。

    ```cmd
    vim /etc/profile
    ```

3. 按“i”进入编辑模式，在文件尾部增加以下内容。

    ```cmd
    export TMOUT=1800
    readonly TMOUT
    ```

4. 按“ESC”键，输入:wq!，按“Enter”保存并退出编辑。

## 设置umask

建议用户服务器的umask设置为027\~777，提高文件权限。

此处以设置umask为027为例。

1. 以root用户登录服务器，编辑“/etc/profile“文件。

    ```cmd
    vim /etc/profile
    ```

2. 在“/etc/profile“文件末尾加上**umask 027**，保存并退出。
3. 执行如下命令使配置生效。

    ```cmd
    source /etc/profile
    ```

## 安全配置基线

<table style="undefined;table-layout: fixed; width: 729px"><colgroup>
<col style="width: 178px">
<col style="width: 551px">
</colgroup>
<thead>
  <tr>
    <th>所属功能域/功能</th>
    <th>TLS证书开关</th>
  </tr></thead>
<tbody>
  <tr>
    <td>OM对象（可选）</td>
    <td>NA</td>
  </tr>
  <tr>
    <td>配置参数（可选）</td>
    <td>NA</td>
  </tr>
  <tr>
    <td>规则分类（支持定制）</td>
    <td>证书管理</td>
  </tr>
  <tr>
    <td>规则分类ID</td>
    <td>NA</td>
  </tr>
  <tr>
    <td>规则子类（支持定制）</td>
    <td>TLS证书认证</td>
  </tr>
  <tr>
    <td>规则子类ID</td>
    <td>NA</td>
  </tr>
  <tr>
    <td>规则名称</td>
    <td>启用TLS认证</td>
  </tr>
  <tr>
    <td>规则ID</td>
    <td>NA</td>
  </tr>
  <tr>
    <td>风险等级</td>
    <td>中</td>
  </tr>
  <tr>
    <td>规则描述</td>
    <td>开启后，集群中所有的Client端和Server端需要同步开启或关闭TLS认证，否则会连接失败。同时UBS IO集群中的所有计算节点均需开启TLS认证。</td>
  </tr>
  <tr>
    <td>风险描述</td>
    <td>不开启TLS，网络通信数据未加密容易泄露。</td>
  </tr>
  <tr>
    <td>修复影响</td>
    <td>开启之后通信通道数据加密传输。</td>
  </tr>
  <tr>
    <td>取值范围</td>
    <td>[true,false]</td>
  </tr>
  <tr>
    <td>安全推荐值</td>
    <td>TRUE</td>
  </tr>
  <tr>
    <td>缺省值</td>
    <td>TRUE</td>
  </tr>
  <tr>
    <td>修复建议</td>
    <td>无</td>
  </tr>
  <tr>
    <td>是否必选项</td>
    <td>是</td>
  </tr>
  <tr>
    <td>是否默认安全</td>
    <td>是</td>
  </tr>
</tbody></table>

## 密钥更新

>![](figures/icon-note.gif) **说明：**
>
>密钥更新需要重启UBS IO加速组件服务，请合理规划密钥更新周期。密钥管理请参见开启TLS认证。

## 缓冲区溢出安全保护

为阻止缓冲区溢出攻击，建议使用ASLR（Address space layout randomization）技术，通过对堆、栈、共享库映射等线性区布局的随机化，增加攻击者预测目的地址的难度，防止攻击者直接定位攻击代码位置。该技术可作用于堆、栈、内存映射区（mmap基址、shared libraries、vdso页）。

开启方式：

```cmd
echo 2 >/proc/sys/kernel/randomize_va_space
```

# 软件卸载

>![](figures/icon-notice.gif) **须知：**
>
>- 建议卸载UBS IO前先安全删除所有密钥存储文件。
>- 建议删除安装UBS IO创建的目录。请参见目录权限配置信息。

## 前提条件

- 若已经删除UBS IO解压后的软件包则需要重新上传软件包并解压。
- 卸载用户需与安装用户为相同用户且具备执行systemctl的命令的权限。
- 需开启安装用户的SSH权限。

## 操作步骤

1. 登录UBS IO安装节点，执行。
    
    ```cmd
    rpm -e ubs-io
    ```

    同时需要卸载ubs-comm，libboundscheck。
2. 其他节点依次执行。

# 软件升级

>![](figures/icon-note.gif) **说明：**
>
>当前版本仅支持离线升级，升级时需要停止应用业务，不支持在线升级。

UBS IO提供了升级准备、升级检查和升级完成三种操作，用于JuiceFS开发者端到端集成软件升级流程中，以下将描述升级操作的实现逻辑。

## 升级准备

执行升级准备操作后，UBS IO会打开透写模式，关闭分布式缓存功能，此时前台业务IO将直接写入到后端存储系统中。详情请参见《UBS-IO API参考》的“BioNotifyUpgradePrepare”章节。

## 升级检查

执行升级检查操作后，UBS IO会检查分布式缓存的业务数据是否已经淘汰完成，如果淘汰完成则返回检查通过，否则返回检查失败，只有在升级检查通过后才能执行集群下电操作。详情请参见《UBS-IO API参考》的“BioCheckUpgradeReady”章节。

## 升级完成

软件离线升级完成，重启集群服务后需要执行升级完成操作，UBS IO会关闭透写模式，重新使能分布式缓存服务。详情请参见《UBS-IO API参考》的“BioNotifyUpgradeFinish”章节。

# 故障处理

使用者需要遵循当前UBS IO版本的可靠性规格，超过规格范围外的故障场景存在业务中断或数据丢失风险。
分离部署模式下缓存客户端以二进制库的形式加载到应用进程中，应用进程退出会导致缓存客户端跟随退出，因此需要处理缓存客户端退出的故障模式。
分离部署模式下UBS IO存在独立缓存进程，融合部署模式下UBS IO和JuiceFS加载到同一个进程，该进程主要负责缓存客户端的请求处理，数据读写缓存和资源管理等业务处理，因此需要处理缓存进程故障的故障模式。
UBS IO要求部署在计算节点上，对外提供分布式读写缓存服务，因此需要处理部署UBS IO的计算节点故障的故障模式。
缓存客户端给服务端发送请求，缓存服务端之间消息交互等场景都会使用网卡进行通信，当网卡遭遇故障时会导致通信消息失败，比如请求消息发送失败，无法接收到响应等情况发生，因此需要处理通信网卡故障的故障模式。
UBS IO分布式缓存层依赖后端存储系统作为大容量数据持久化存储池，因此需要处理后端存储系统异常故障模式。

## 可靠性总体规格

使用者需要遵循当前UBS IO版本的可靠性规格，超过规格范围外的故障场景存在业务中断或数据丢失风险。

UBS IO支持的可靠性总体原则：

- 依赖的开源软件ZooKeeper、JuiceFS和Ceph不属于UBS IO可靠性范围内，不支持依赖软件故障或异常的容错处理。
- 当前版本仅支持单重故障，不支持多重故障叠加。
- 当前版本仅支持双副本冗余，同时故障两个副本会导致用户数据丢失。
- 故障节点的数据可靠性不保证。

## 缓存客户端故障/恢复

分离部署模式下缓存客户端以二进制库的形式加载到应用进程中，应用进程退出会导致缓存客户端跟随退出，因此需要处理缓存客户端退出的故障模式。

**表 1 缓存客户端故障模式** 

<table style="undefined;table-layout: fixed; width: 1021px"><colgroup>
<col style="width: 120px">
<col style="width: 236px">
<col style="width: 483px">
<col style="width: 121px">
</colgroup>
<thead>
  <tr>
    <th>场景</th>
    <th>影响</th>
    <th>处理方式</th>
    <th>限制</th>
  </tr></thead>
<tbody>
  <tr>
    <td>缓存客户端故障</td>
    <td><br>写资源配额泄漏。<br>分发的请求响应失败。<br>通信链路断开。<br>创建的线性空间失效。</td>
    <td><br>缓存服务端通过链路断开事件感知客户端退出，回收写资源配额，封存线性空间并将数据淘汰到后端存储。<br>删除失效的链路信息。</td>
    <td>无。</td>
  </tr>
  <tr>
    <td>缓存客户端恢复</td>
    <td><br>重新建立通信链路。<br>缓存客户端重新执行初始化和上电流程。</td>
    <td>缓存服务端处理建链请求。</td>
    <td>无。</td>
  </tr>
</tbody>
</table>

## UBS IO进程故障/恢复

分离部署模式下UBS IO存在独立缓存进程，融合部署模式下UBS IO和JuiceFS加载到同一个进程，该进程主要负责缓存客户端的请求处理，数据读写缓存和资源管理等业务处理，因此需要处理缓存进程故障的故障模式。

**表 1  缓存进程故障模式** 

<table style="undefined;table-layout: fixed; width: 1110px"><colgroup>
<col style="width: 121px">
<col style="width: 236px">
<col style="width: 483px">
<col style="width: 270px">
</colgroup>
<thead>
  <tr>
    <th>场景</th>
    <th>影响</th>
    <th>处理方式</th>
    <th>限制</th>
  </tr></thead>
<tbody>
  <tr>
    <td>缓存进程故障</td>
    <td><br>写缓存的副本数据丢失。<br>读缓存的对象数据丢失。<br>SDK端请求分发失败。</td>
    <td><br>通过ZooKeeper心跳感知到缓存进程故障，通知集群管理更新视图，发布更新后的视图。<br>受进程故障影响的分区数据强制淘汰到后端存储。</td>
    <td><br>缓存进程临时故障仅修改分区视图。<br>缓存进程永久故障，集群管理会将该节点移除集群，变更节点视图和分区视图。<br>临时故障时间窗口期可配置。</td>
  </tr>
  <tr>
    <td>缓存进程恢复</td>
    <td>读写缓存功能恢复。</td>
    <td><br>临时故障恢复：通过ZooKeeper心跳感知到缓存进程恢复，通知集群管理更新视图，视图更新后再发布新视图。<br>永久故障恢复：执行扩容流程，请求重新加入集群。</td>
    <td>无。</td>
  </tr>
</tbody>
</table>

## 缓存节点故障/恢复

UBS IO要求部署在计算节点上，对外提供分布式读写缓存服务，因此需要处理部署UBS IO的计算节点故障的故障模式。

**表 1  缓存节点故障模式** 

<table style="undefined;table-layout: fixed; width: 1110px"><colgroup>
<col style="width: 121px">
<col style="width: 236px">
<col style="width: 483px">
<col style="width: 270px">
</colgroup>
<thead>
  <tr>
    <th>场景</th>
    <th>影响</th>
    <th>处理方式</th>
    <th>限制</th>
  </tr></thead>
<tbody>
  <tr>
    <td>缓存节点故障</td>
    <td><br>写缓存的副本数据丢失。<br>读缓存的对象数据丢失。<br>SDK端请求分发失败。</td>
    <td><br>通过ZooKeeper心跳感知到缓存进程故障，通知集群管理更新视图，发布更新后的视图。<br>受进程故障影响的分区数据强制淘汰到后端存储。</td>
    <td><br>缓存节点临时故障仅修改节点视图和分区视图的状态。<br>缓存节点永久故障，集群管理会将该节点移除集群，变更节点视图和分区视图。<br>临时故障时间窗口期可配置。</td>
  </tr>
  <tr>
    <td>缓存节点恢复</td>
    <td>读写缓存功能恢复。</td>
    <td><br>临时故障恢复：通过ZooKeeper心跳感知到缓存进程恢复，通知集群管理更新视图，视图更新后再发布新视图。<br>永久故障恢复：执行扩容流程，请求重新加入集群。</td>
    <td>无。</td>
  </tr>
</tbody>
</table>

## UBS IO通信故障/恢复

缓存客户端给服务端发送请求，缓存服务端之间消息交互等场景都会使用网卡进行通信，当网卡遭遇故障时会导致通信消息失败，比如请求消息发送失败，无法接收到响应等情况发生，因此需要处理通信网卡故障的故障模式。

**表 1  通信网卡故障模式** 

<table style="undefined;table-layout: fixed; width: 1044px"><colgroup>
<col style="width: 151px">
<col style="width: 242px">
<col style="width: 227px">
<col style="width: 424px">
</colgroup>
<thead>
  <tr>
    <th>场景</th>
    <th>影响</th>
    <th>处理方式</th>
    <th>限制</th>
  </tr></thead>
<tbody>
  <tr>
    <td>通信网卡故障</td>
    <td><br>SDK端请求发送失败。<br>SDK端请求接收超时。<br>分区视图接收失败。</td>
    <td><br>通过ZooKeeper心跳感知到通信网卡故障，通知集群管理更新视图，视图更新后再发布新视图。<br>受网卡故障影响的分区数据强制淘汰到后端存储。</td>
    <td><br>支持端口被占用、防火墙和网卡DOWN故障场景。<br>不支持网络丢包、网络错包、网卡单通和链路闪断等网卡异常场景。</td>
  </tr>
  <tr>
    <td>通信网卡恢复</td>
    <td><br>请求发送功能恢复。<br>读写缓存各个流程支持重入。</td>
    <td>通过ZooKeeper心跳感知到通信网卡恢复，通知集群管理更新视图，视图更新后再发布新视图。</td>
    <td>无。</td>
  </tr>
</tbody>
</table>

## 后端存储系统异常

UBS IO分布式缓存层依赖后端存储系统作为大容量数据持久化存储池，因此需要处理后端存储系统异常故障模式。

**表 1  后端存储系统故障模式** 

<table style="undefined;table-layout: fixed; width: 1044px"><colgroup>
<col style="width: 151px">
<col style="width: 242px">
<col style="width: 227px">
<col style="width: 424px">
</colgroup>
<thead>
  <tr>
    <th>场景</th>
    <th>影响</th>
    <th>处理方式</th>
    <th>限制</th>
  </tr></thead>
<tbody>
  <tr>
    <td>后端存储系统故障</td>
    <td><br>写缓存的对象数据无法淘汰。<br>读缓存预取加载对象数据失败。</td>
    <td>感知后端存储系统故障并作出相应告警，当前告警方式为日志打印。</td>
    <td>无。</td>
  </tr>
  <tr>
    <td>后端存储系统恢复</td>
    <td><br>写缓存淘汰功能恢复正常。<br>读缓存预取加载功能恢复正常。<br>前台业务恢复正常。</td>
    <td>感知后端存储系统恢复并重连成功。</td>
    <td><br>写缓存淘汰功能恢复，由于后端存储性能限制，恢复到正常水位需要一段时间。<br>写缓存淘汰性能限制，前台业务性能需要一段时间逐步恢复为正常水平。</td>
  </tr>
</tbody>
</table>

## 缓存磁盘故障

UBS IO分布式缓存层使用缓存磁盘NVMe SSD作为二级缓存介质，用于持久化读写缓存数据，需要有效应对缓存磁盘故障。

**表 1  缓存磁盘故障模式** 

<table style="undefined;table-layout: fixed; width: 1044px"><colgroup>
<col style="width: 151px">
<col style="width: 242px">
<col style="width: 227px">
<col style="width: 424px">
</colgroup>
<thead>
  <tr>
    <th>场景</th>
    <th>影响</th>
    <th>处理方式</th>
    <th>限制</th>
  </tr></thead>
<tbody>
  <tr>
    <td>新磁盘加入</td>
    <td>接入新盘过程中，前台I/O性能下降，业务受影响的时间不超过60s。</td>
    <td>新磁盘加入和识别、配置文件更新、加盘事件上报、触发视图重均衡、淘汰缓存数据和创建新缓存。</td>
    <td><br>单次支持加1块盘，单节点可用盘最大支持4块盘，否则报错。<br>新加入磁盘容量大小规格，与集群磁盘规格保持一致。</td>
  </tr>
  <tr>
    <td>故障磁盘剔除</td>
    <td>故障检测与剔除期间，前台I/O性能下降，业务影响的时间不超过60s。</td>
    <td>上报磁盘故障到集群管理、完成受影响分区数据淘汰后上报处理、触发分区视图重计算与发布（期间故障分区IO自动重试）。</td>
    <td><br>仅支持单盘故障，同时双盘故障会导致丢数据。</td>
  </tr>
</tbody>
</table>

## 缓存节点扩容

- 当前版本支持的集群规模最小为2节点，最大为256节点。因此最大扩容至集群256个节点。
- 支持同时批量进行扩容的缓存节点数为32个。
- 不支持故障处理期间进行缓存节点扩容操作。

# 后端存储使用说明

UBS IO会使用后端存储系统作为数据的持久化容量层。当前仅支持Ceph和HDFS分布式存储系统，后端存储系统由用户提供并创建UBS IO使用的存储池或挂载目录。因为后端存储系统属于用户管理和维护范围，需要用户保证提供给UBS IO使用的存储池或挂载目录不会被其它软件应用使用和访问，否则会破坏应用数据和构成目录越权风险。

# 公网地址声明

以下表格中列出了产品中包含的公网地址，没有安全风险。

<table style="undefined;table-layout: fixed; width: 998px"><colgroup>
<col style="width: 489px">
<col style="width: 509px">
</colgroup>
<thead>
  <tr>
    <th>网址</th>
    <th>说明</th>
  </tr></thead>
<tbody>
  <tr>
    <td>http://license.coscl.org.cn/MulanPSL2</td>
    <td>该网址为开源许可证网站，为UBS IO的开源信息声明，无安全风险。</td>
  </tr>
  <tr>
    <td>http://www.apache.org/licenses/LICENSE-2.0</td>
    <td>该网址为开源许可证网站，为Hadoop以及Zookeeper的开源信息声明，无安全风险。</td>
  </tr>
  <tr>
    <td>https://github.com/nginx/nginx/blob/master/LICENSE</td>
    <td>该网址为开源许可证网站，为使用的红黑树Nginx的开源信息声明，无安全风险。</td>
  </tr>
  <tr>
    <td>https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/googletest.git</td>
    <td>该网址为ut使用的googletest代码仓地址，无安全风险。</td>
  </tr>
  <tr>
    <td>https://issues.apache.org/jira/browse/ZOOKEEPER-1355</td>
    <td>该网址为zookeeper开源头文件的声明issue网址，无安全风险。</td>
  </tr>
  <tr>
    <td>https://gitcode.com/GitHub_Trending/sp/spdlog.git</td>
    <td>该网址为引入的三方库spdlog的地址，无安全风险。</td>
  </tr>
  <tr>
    <td>https://gitcode.com/gh_mirrors/pr/prometheus-cpp.git</td>
    <td>该网址为引入的三方库prometheus的地址，无安全风险。</td>
  </tr>
  <tr>
    <td>https://gitcode.com/openeuler/libboundscheck.git</td>
    <td>该网址为引入的三方库libboundscheck的地址，无安全风险。</td>
  </tr>
  <tr>
    <td>https://gitcode.com/openeuler/ubs-comm.git</td>
    <td>该网址为引入的三方库ubs-comm的地址，无安全风险。</td>
  </tr>
</tbody></table>

# 账户一览表

>![](figures/icon-notice.gif) **须知：**
>用户创建的安装用户需定期修改密码。

<table style="undefined;table-layout: fixed; width: 901px"><colgroup>
<col style="width: 121px">
<col style="width: 323px">
<col style="width: 187px">
<col style="width: 270px">
</colgroup>
<thead>
  <tr>
    <th>用户</th>
    <th>描述</th>
    <th>初始密码</th>
    <th>密码修改方法</th>
  </tr></thead>
<tbody>
  <tr>
    <td>bioadmin</td>
    <td>分离部署场景UBS IO Server运行用户。</td>
    <td>用户自定义。</td>
    <td>使用passwd命令修改。</td>
  </tr>
  <tr>
    <td>juiceadmin</td>
    <td>融合部署场景上层调用组件运行用户。</td>
    <td>用户自定义。</td>
    <td>使用passwd命令修改。</td>
  </tr>
</tbody>
</table>

# 版权说明

Copyright \(c\) Huawei Technologies Co., Ltd. 2026. All rights reserved.
