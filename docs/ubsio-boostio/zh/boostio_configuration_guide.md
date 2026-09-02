# UBSIO-BoostIO 配置说明

本文档说明 UBSIO-BoostIO 的 `bio.conf` 配置项和 TLS 认证配置。软件安装与启动流程请参见[安装部署指南](boostio_deployment_guide.md)。

## `bio.conf` 配置项

根据业务使用情况和待安装部署的环境，设置 `/etc/boostio/bio.conf` 中的相关配置项。

**UBSIO-BoostIO 配置项**<a id="boostio-config"></a>

<table style="undefined;table-layout: fixed; width: 1704px"><colgroup>
<col style="width: 100px">
<col style="width: 288px">
<col style="width: 235px">
<col style="width: 237px">
<col style="width: 283px">
<col style="width: 457px">
</colgroup>
<thead>
  <tr>
    <th>归属模块</th>
    <th>配置项名称</th>
    <th>简要描述</th>
    <th>默认值</th>
    <th>合法值/区间</th>
    <th>注意事项</th>
  </tr></thead>
<tbody>
  <tr>
    <td>Log</td>
    <td>bio.log.level</td>
    <td>日志打印级别。</td>
    <td>info</td>
    <td><br>debug<br>info<br>warn<br>trace<br>error</td>
    <td>-</td>
  </tr>
  <tr>
    <td rowspan="17">Net</td>
    <td>bio.net.data.ip_mask</td>
    <td>IP地址段。</td>
    <td>127.0.0.1/24</td>
    <td>*.*.*.*/#，其中*为0 ~ 255，#为0 ~ 32</td>
    <td>使用JuiceFS跑大数据业务时，该字段需要和/etc/hosts中的主机名对应的IP保持一致。</td>
  </tr>
  <tr>
    <td>bio.net.data.listen_port</td>
    <td>业务面网络通信端口号。</td>
    <td>7201</td>
    <td>7201 ~ 7800</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.net.data.protocol</td>
    <td>网络协议。</td>
    <td>tcp</td>
    <td><br>rdma<br>tcp</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.net.rpc.data.busy_polling_mode</td>
    <td>RPC开启busy-polling标记。</td>
    <td>false</td>
    <td><br>true<br>false</td>
    <td>仅RDMA协议生效。</td>
  </tr>
  <tr>
    <td>bio.net.rpc.data.workers_count</td>
    <td>RPC数据面工作核数。</td>
    <td>4</td>
    <td>1 ~ 16</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.net.request.executor.thread.num</td>
    <td>接收端请求处理线程数。</td>
    <td>8</td>
    <td>8 ~ 256</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.net.request.executor.queue.size</td>
    <td>接收端请求处理队列深度。</td>
    <td>1024</td>
    <td>1024 ~ 65535</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.net.ipc.data.busy_polling_mode</td>
    <td>IPC开启busy-polling标记。</td>
    <td>false</td>
    <td><br>true<br>false</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.net.ipc.data.workers_count</td>
    <td>IPC数据面工作核数。</td>
    <td>4</td>
    <td>1 ~ 128</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.net.tls.enable.switch</td>
    <td>网络安全开关。</td>
    <td>true</td>
    <td><br>true<br>false</td>
    <td><br>关闭后可能会引入信息安全问题、仿冒等风险，请谨慎操作。<br>分离部署时调用UBS IO服务初始化接口传入的enableTls参数需要和该配置项保持一致。</td>
  </tr>
  <tr>
    <td>bio.net.tls.ca.cert.path</td>
    <td>CA证书文件路径。</td>
    <td>/path/CA/cacert.pem</td>
    <td>默认值仅作为示例。</td>
    <td>安全开关打开则需要为有效路径，安全开关关闭则不解析该配置项。</td>
  </tr>
  <tr>
    <td>bio.net.tls.ca.crl.path</td>
    <td>吊销列表文件路径。</td>
    <td>-</td>
    <td>-</td>
    <td>可以为空，不为空时，安全开关打开且需要校验证书是否被吊销时为有效路径，安全开关关闭则不解析该配置项。</td>
  </tr>
  <tr>
    <td>bio.net.tls.server.cert.path</td>
    <td>服务端证书文件路径。</td>
    <td>/path/server/servercert.pem</td>
    <td>默认值仅作为示例。</td>
    <td>安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</td>
  </tr>
  <tr>
    <td>bio.net.tls.server.key.path</td>
    <td>服务端证书私钥文件路径。</td>
    <td>/path/server/serverkey.pem</td>
    <td>默认值仅作为示例。</td>
    <td>安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</td>
  </tr>
  <tr>
    <td>bio.net.tls.server.key.pass.path</td>
    <td>工作证书私钥口令的密文的文件路径。</td>
    <td>-</td>
    <td>-</td>
    <td>可以为空，为空时需要提供未加密的私钥文件。不为空时安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。<br>在加密私钥的时候，私钥口令建议满足复杂度要求。同时满足以下要求：<br>口令长度至少8个字符。<br>口令需要包含如下至少两种字符的组合。<br>至少一个小写字母<br>至少一个大写字母<br>至少一个数字<br>至少一个特殊字符：`~!@#$%^&amp;*()-_=+\|[{}];:'"",&lt;.&gt;/? 和空格</td>
  </tr>
  <tr>
    <td>bio.net.tls.server.decrypter.lib.path</td>
    <td>安全解密函数so文件路径。</td>
    <td>-</td>
    <td>-</td>
    <td>可以为空，为空时需要提供明文口令。不为空时安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</td>
  </tr>
  <tr>
    <td>bio.net.tls.server.ssl.lib.dir</td>
    <td>openssl so文件所在目录路径。</td>
    <td>-</td>
    <td>-</td>
    <td>为空时，使用系统路径下的so文件。<br>不为空时，安全开关打开时则需要为有效路径，安全开关关闭则不解析该配置项。</td>
  </tr>
  <tr>
    <td rowspan="13">Cache</td>
    <td>bio.cache.qos.enable</td>
    <td>流量控制开关。</td>
    <td>true</td>
    <td><br>false<br>true</td>
    <td>流量控制开关打开会影响到极限性能，建议性能用例场景关闭。</td>
  </tr>
  <tr>
    <td>bio.data.crc.enable</td>
    <td>数据完整性校验开关。</td>
    <td>false</td>
    <td><br>false<br>true</td>
    <td>数据完整性校验开关打开会增加数据读写时延，建议在问题定位场景使用。</td>
  </tr>
  <tr>
    <td>bio.segment.size_in_mb</td>
    <td>缓存资源粒度。</td>
    <td>4</td>
    <td>1 ~ 16</td>
    <td>单位MB。</td>
  </tr>
  <tr>
    <td>bio.mem.size_in_gb</td>
    <td>缓存资源内存容量。</td>
    <td>50</td>
    <td>0 ~ 512</td>
    <td><br>禁止配置超过系统内存。<br>单位GB。<br>配置为0表示该节点不具备缓存功能。</td>
  </tr>
  <tr>
    <td>bio.disk.path</td>
    <td>缓存资源磁盘列表。</td>
    <td>/dev/sdxx:/dev/sdyy</td>
    <td>-</td>
    <td>多个磁盘路径用冒号隔开，当前版本支持最多4块磁盘。</td>
  </tr>
  <tr>
    <td>bio.rcache.evict_water_level</td>
    <td>读缓存淘汰水位。</td>
    <td>90</td>
    <td>0 ~ 100</td>
    <td>表示使用读缓存百分比。</td>
  </tr>
  <tr>
    <td>bio.cache.mem_read_write_ratio</td>
    <td>内存读写资源配比。</td>
    <td>5:5</td>
    <td>0 ~ 10:10 ~ 0</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.cache.disk_read_write_ratio</td>
    <td>磁盘读写资源配比。</td>
    <td>5:5</td>
    <td>0 ~ 10:10 ~ 0</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.work.scene</td>
    <td>应用场景标记。</td>
    <td>none</td>
    <td><br>none<br>bigdata</td>
    <td>可选，默认为none<br>none：不存在使用约束。<br>bigdata：大数据场景，其主要区别是IO强制对齐。</td>
  </tr>
  <tr>
    <td>bio.work.io.alignsize</td>
    <td>IO对齐数据大小。</td>
    <td>1</td>
    <td>1 ~ 4194304</td>
    <td>可选，单位B。</td>
  </tr>
  <tr>
    <td>bio.wcache.evict_water_level</td>
    <td>写缓存淘汰水位。</td>
    <td>0</td>
    <td>0 ~ 100</td>
    <td>可选，默认为0，表示使用写缓存资源百分比。</td>
  </tr>
  <tr>
    <td>bio.wcache.negotiate.delay</td>
    <td>淘汰协商延迟。</td>
    <td>100</td>
    <td>50 ~ 1000</td>
    <td>可选，默认100，单位ms。前台写性能敏感场景需要将该值调大，淘汰延迟增大；前台写性能不敏感可使用较小值，更快淘汰。</td>
  </tr>
  <tr>
    <td>bio.trace.enable</td>
    <td>流程统计开关。</td>
    <td>true</td>
    <td><br>false<br>true</td>
    <td>流程统计开关打开会影响到极限性能，建议性能用例场景关闭。</td>
  </tr>
  <tr>
    <td rowspan="7">Underfs</td>
    <td>bio.underfs.file_system_type</td>
    <td>后端存储系统类型。</td>
    <td>ceph</td>
    <td><br>ceph<br>hdfs</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.underfs.ceph.cfg.path</td>
    <td>Ceph配置文件路径。</td>
    <td>/etc/ceph/ceph.conf</td>
    <td>不为空。</td>
    <td>选择ceph后必填选项，需要是真实存在的路径。</td>
  </tr>
  <tr>
    <td>bio.underfs.ceph.cluster</td>
    <td>Ceph集群名称。</td>
    <td>ceph</td>
    <td>不为空。</td>
    <td>选择ceph后必填选项。</td>
  </tr>
  <tr>
    <td>bio.underfs.ceph.user</td>
    <td>Ceph用户。</td>
    <td>client.admin</td>
    <td>不为空。</td>
    <td>选择ceph后必填选项。</td>
  </tr>
  <tr>
    <td>bio.underfs.ceph.pool</td>
    <td>Ceph数据池。</td>
    <td>0:jfspool0,1:jfspool1</td>
    <td>不为空。</td>
    <td>选择ceph后必填选项，多个参数用英文逗号隔开。</td>
  </tr>
  <tr>
    <td>bio.underfs.hdfs.name_node</td>
    <td>hadoop的NameNode。</td>
    <td>default:0</td>
    <td>*.*.*.*/#，*为0 ~ 255，#为0 ~ 65535</td>
    <td>可选，默认为default:0，格式：IP地址:端口号，表示使用hadoop配置文件中的IP地址和端口号。</td>
  </tr>
  <tr>
    <td>bio.underfs.hdfs.working_path</td>
    <td>文件在hdfs系统的存放路径。</td>
    <td>/hdfs</td>
    <td>路径名长度小于或等于255的合法路径。</td>
    <td>可选，默认为/hdfs。</td>
  </tr>
  <tr>
    <td rowspan="6">CM</td>
    <td>bio.cm.initial.nodes_count</td>
    <td>集群初始化期望节点数。</td>
    <td>2</td>
    <td>2 ~ 256</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.cm.copy_num</td>
    <td>数据冗余度。</td>
    <td>2</td>
    <td>2</td>
    <td>当前版本仅支持双副本。</td>
  </tr>
  <tr>
    <td>bio.cm.pts_count</td>
    <td>分区视图数量。</td>
    <td>16</td>
    <td>2 ~ 8192</td>
    <td>-</td>
  </tr>
  <tr>
    <td>bio.cm.register_timeout_sec</td>
    <td>ZooKeeper心跳检测超时时间。</td>
    <td>20</td>
    <td>10 ~ 60</td>
    <td>单位s。</td>
  </tr>
  <tr>
    <td>bio.cm.register_perm_timeout_sec</td>
    <td>永久故障超时时间。</td>
    <td>60</td>
    <td>60 ~ 600</td>
    <td>单位s。</td>
  </tr>
  <tr>
    <td>bio.cm.zk_host</td>
    <td>ZooKeeper服务节点信息。<br>例如3节点ZK集群：127.0.0.1:2181,127.0.0.2:2181,127.0.0.3:2181。</td>
    <td>-</td>
    <td>不为空</td>
    <td>ZooKeeper使用的网段需要和业务IP地址网段保持一致。</td>
  </tr>
  <tr>
    <td rowspan="2">Prometheus</td>
    <td>bio.prometheus.exposer</td>
    <td>Prometheus Server的地址和端口号。</td>
    <td>-</td>
    <td>*.*.*.*:#，*为0 ~ 255，#为0 ~ 65535</td>
    <td>可选</td>
  </tr>
  <tr>
    <td>bio.prometheus.scrape_interval_sec</td>
    <td>Prometheus采样频率。</td>
    <td>15</td>
    <td>-</td>
    <td>可选，单位s。</td>
  </tr>
</tbody></table>

## TLS 认证配置

### 开启Server端TLS认证

**注意事项**

- 如需开启TLS认证，则UBS IO集群中的所有计算节点均需开启TLS认证。
- 安装部署完成后，需手动删除安装过程中用于集群节点间通信的公钥。
- 生成加密口令之前建议关闭系统历史记录功能。口令生成后可重新启用该功能。

    用户导入的私钥最好进行加密，否则会有安全风险。

    证书安全要求：

    - 需使用业界公认安全可信的非对称加密算法、密钥长度、Hash算法、证书格式等。
    - 应处于有效期内。

- TLS加密配置有如下三种使用方式。
    - （推荐）提供包含指定签名的解密函数so，提供加密后的口令和加密私钥，根据解密函数对口令进行解密。
    - 不提供解密函数so，提供明文口令和加密私钥。
    - 不提供解密函数so和口令，仅提供未加密的私钥。

**前提条件**

UBS IO已经安装成功，需给予Server用户相应的权限读取下面文件。首先获取TLS认证需要的文件，如[表1](#开启Server端TLS认证所需文件列表)所示。

**表 1  开启Server端TLS认证所需文件列表**<a id="开启Server端TLS认证所需文件列表"></a>

<table style="undefined;table-layout: fixed; width: 898px"><colgroup>
<col style="width: 209px">
<col style="width: 689px">
</colgroup>
<thead>
  <tr>
    <th>文件</th>
    <th>说明</th>
  </tr></thead>
<tbody>
  <tr>
    <td>CA文件</td>
    <td>一个自签名的证书，可以签发其它证书。格式为：PEM（*.pem）。</td>
  </tr>
  <tr>
    <td>吊销证书列表文件</td>
    <td>给出吊销证书列表文件，格式为：PEM（*.crl）。可选，如无吊销证书，可以没有此文件。</td>
  </tr>
  <tr>
    <td>Server端的证书</td>
    <td>由CA签发的证书，保证在有效期内。格式为：PEM chain（*.pem）。</td>
  </tr>
  <tr>
    <td>Server端的证书对应的已加密私钥文件</td>
    <td>要与Server端证书对应，Server安装用户要知道这个私钥文件的口令。格式为：PEM encrypted（*.pem）。如果配置未加密私钥，则口令和解密函数文件应配置为空。</td>
  </tr>
  <tr>
    <td>Server端的私钥口令文件</td>
    <td>可选，加密后的私钥口令存储文件，口令长度不超过10000字节。不进行配置则需要私钥文件未加密。</td>
  </tr>
  <tr>
    <td>Server端的解密库</td>
    <td>可选，配置则使用用户提供的包含解密函数的so。不进行配置需提供明文口令。</td>
  </tr>
  <tr>
    <td>对应存放libssl.so libcrtpto.so文件</td>
    <td>可选，配置则使用用户提供的版本。配置为空则默认使用/usr/lib64路径。</td>
  </tr>
</tbody>
</table>

**操作步骤**

1. 修改bio.conf配置文件。
2. 打开网络安全开关。请参见[UBSIO-BoostIO 配置项](#boostio-config)，将bio.net.tls.enable.switch配置为true。
3. 请参见[表1](#开启Server端TLS认证所需文件列表)，将准备好的相关证书的路径写入到配置文件中的相应选项。

### 开启Client端TLS认证

**注意事项**

- 分离部署时才需要此步骤，TLS开关（enableTls）由用户传入，建议用户开启TLS认证，UBS IO所有节点的TLS认证开启和关闭保持统一。
- 集群中所有的Client端和Server端需要同步开启或关闭TLS认证，否则会连接失败。
- 多用户访问UBS IO服务时，每个用户使用的证书可以是不同的，但需要满足都由同一个CA签发。
- Client和Server的所有节点应使用相同的加密配置方式。

**前提条件**

UBS IO已经安装成功，需给予Client用户相应的权限读取下面文件。首先获取TLS认证需要的文件，如[表1](#开启Client端TLS认证所需文件列表)所示。

**表 1  开启Client端TLS认证所需文件列表**<a id="开启Client端TLS认证所需文件列表"></a>

<table style="undefined;table-layout: fixed; width: 898px"><colgroup>
<col style="width: 209px">
<col style="width: 689px">
</colgroup>
<thead>
  <tr>
    <th>文件</th>
    <th>说明</th>
  </tr></thead>
<tbody>
  <tr>
    <td>CA文件</td>
    <td>一个自签名的证书，可以签发其它证书。格式为：PEM（*.pem）。</td>
  </tr>
  <tr>
    <td>吊销证书列表文件</td>
    <td>给出吊销证书列表文件，格式为：PEM（*.crl）。可选，如无吊销证书，可以没有此文件。</td>
  </tr>
  <tr>
    <td>Client端的证书</td>
    <td>由CA签发的证书，保证在有效期内。格式为：PEM chain（*.pem）。</td>
  </tr>
  <tr>
    <td>Client端的证书对应的私钥文件</td>
    <td>要与Client端证书对应，安装用户要知道这个私钥文件的口令。格式为：PEM encrypted（*.pem）。如果配置未加密私钥，则口令和解密函数文件应配置为空</td>
  </tr>
  <tr>
    <td>Client端的私钥口令文件</td>
    <td>可选，加密或未加密后的私钥口令存储文件，口令长度不超过10000字节。不进行配置则需要私钥文件未加密。</td>
  </tr>
  <tr>
    <td>Client端的解密库</td>
    <td>可选，配置则使用用户提供的包含解密函数的so。不进行配置需提供明文口令。</td>
  </tr>
  <tr>
    <td>对应存放libssl.so libcrtpto.so文件</td>
    <td>可选，配置则使用用户提供的版本。配置为空则默认使用/usr/lib64路径。</td>
  </tr>
</tbody>
</table>

**操作步骤**

1. 获取相应的软件包。
2. 调用初始化接口即可获取[表1](#开启Client端TLS认证所需文件列表)的文件。详情请参见 [UBSIO-BoostIO API 参考的 BioInitialize 章节](boostio_api_reference.md#bioinitialize)。

    ```config
    TLS涉及到的参数的成员如下：
    uint8_t enable;                    // switch
    char certificationPath[PATH_MAX];  // certification path
    char caCerPath[PATH_MAX];          // caCer path
    char caCrlPath[PATH_MAX];          // caCrl path
    char privateKeyPath[PATH_MAX];     // private key path
    char privateKeyPassword[PATH_MAX]; // private key password
    char decrypterLibPath[PATH_MAX];   // decrypter lib path
    char opensslLibDir[PATH_MAX];      // openssl lib dir path
    ```
