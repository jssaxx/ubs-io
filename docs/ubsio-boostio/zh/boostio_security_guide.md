# UBSIO-BoostIO 安全指南

本文档汇总 UBSIO-BoostIO 的通信矩阵、运行用户与文件权限建议、安全加固、账户及公网地址说明。

## 通信矩阵

| 原设备| 源IP地址| 源端口| 目的设备| 目的IP地址| 目的端口（侦听）| 协议| 端口说明| 侦听端口是否可更改 | 认证方式 | 加密方式 | 所属平面 |  备注 |
|---|---|---|---|---|---|---|:---|---|---|---|---|---|
| 计算服务器         | 计算服务器IP地址 | 随机分配（由操作系统自动分配） | BoostIO Server | BoostIO Server自配置IP | 7201-7800| ROCE| BoostIO SDK与BoostIO Server之间的通信交互；BoostIO Server和BoostIO Server之间的通信交互 | 是| 证书双向认证 | TLS1.3 | 业务面 | 无|
| 计算服务器| 计算服务器IP地址 | 随机分配（由操作系统自动分配） | BoostIO Server | BoostIO Server自配置IP |7201-7800| TCP | BoostIO SDK与BoostIO Server之间的通信交互；BoostIO Server和BoostIO Server之间的通信交互 | 是 | 证书双向认证 | TLS1.3 | 业务面 | 无|
| 计算服务器 | 计算服务器IP地址 | 随机分配（由操作系统自动分配） | Zookeeper运行服务器 | Zookeeper配置 | 客户配置| TCP| Zookeeper业务端口，用于BoostIO集群管理的消息交互| 否| Zookeeper配置 | Zookeeper配置 | 业务面 | Zookeeper为开源组件，由客户自己安装在环境上安装。配置参考Zookeeper的官网[安全配置](https://zookeeper.apache.org/doc/current/zookeeperProgrammers.html#sc_Security)。 |
| 计算服务器 | 计算服务器IP地址 | 随机分配（由操作系统自动分配） | Ceph运行服务器 | Ceph配置| 客户配置| TCP| Ceph业务端口，用于BoostIO集群数据交互| 否 | Ceph配置 | Ceph配置 | 业务面 | 用户将后端存储类型配置为Ceph时才会存在此通信链路，配置参考Ceph的官网[安全配置](https://docs.ceph.com/en/latest/security/)。 |
| 计算服务器 | 计算服务器IP地址 | 随机分配（由操作系统自动分配） | HDFS运行服务器 | HDFS配置| 客户配置| TCP| HDFS业务端口，用于BoostIO集群数据交互| 否 | HDFS配置 | HDFS配置 | 业务面 | 用户将后端存储类型配置为HDFS时才会存在此通信链路，配置参考HDFS的官网[安全配置](https://hadoop.apache.org/docs/stable/hadoop-project-dist/hadoop-common/SecureMode.html)。 |

通信矩阵中涉及的安全配置建议用户根据实际业务场景和安全要求进行配置。

## 运行用户建议

基于安全性考虑，建议您在执行任何命令时，不建议使用root等管理员类型账户执行，遵循权限最小化原则。

## 文件权限建议

- 建议用户在主机（包括宿主机）及容器中设置运行系统umask值为0027及以上，保障新增文件夹默认最高权限为750，新增文件默认最高权限为640。
- 建议对使用当前项目已有和产生的文件、数据、目录，设置如下建议权限。

| 类型                | Linux权限参考最大值   |
|-------------------|----------------|
| 用户主目录             | 750（rwxr-x---） |
| 程序文件(含脚本文件、库文件等)  | 550（r-xr-x---） |
| 程序文件目录            | 550（r-xr-x---） |
| 配置文件              | 640（rw-r-----） |
| 配置文件目录            | 750（rwxr-x---） |
| 日志文件(记录完毕或者已经归档)  | 440（r--r-----） |
| 日志文件(正在记录)        | 640（rw-r-----） |
| 日志文件目录            | 750（rwxr-x---） |
| Debug文件           | 640（rw-r-----） |
| Debug文件目录         | 750（rwxr-x---） |
| 临时文件目录            | 750（rwxr-x---） |
| 维护升级文件目录          | 770（rwxrwx---） |
| 业务数据文件            | 640（rw-r-----） |
| 业务数据文件目录          | 750（rwxr-x---） |
| 密钥组件、私钥、证书、密文文件目录 | 700（rwx------） |
| 密钥组件、私钥、证书、加密密文   | 600（rw-------） |
| 加解密接口、加解密脚本       | 500（r-x------） |

## 安全加固

### 设置登录会话超时时间

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

### 设置umask

建议用户服务器的umask设置为027\~077，提高文件权限。

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

### 安全配置基线

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
    <td>开启后，集群中所有的Client端和Server端需要同步开启TLS认证，否则会连接失败。同时UBS IO集群中的所有计算节点均需开启TLS认证。</td>
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

### 密钥更新

密钥更新需要重启UBS IO加速组件服务，请合理规划密钥更新周期。密钥管理请参见[配置说明中的 TLS 认证配置](boostio_configuration_guide.md#tls-认证配置)。

### 缓冲区溢出安全保护

为阻止缓冲区溢出攻击，建议使用ASLR（Address space layout randomization）技术，通过对堆、栈、共享库映射等线性区布局的随机化，增加攻击者预测目的地址的难度，防止攻击者直接定位攻击代码位置。该技术可作用于堆、栈、内存映射区（mmap基址、shared libraries、vdso页）。

开启方式：

```cmd
echo 2 >/proc/sys/kernel/randomize_va_space
```

> **说明：**
>
> 该命令需要root权限才能执行，且该修改方式是临时的，系统重启后会失效。

## 账户一览表

> **说明：**
>
> 用户创建的安装用户需定期修改密码。

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

## 公网地址声明

以下地址来自源码、构建配置或文档引用，均用于开源依赖、许可证或安全配置说明。

| 类型| 开源代码地址|文件名| 公网IP地址/公网URL地址/域名/邮箱地址| 用途说明|
|------------|---------------------------------------------|------------------------|---------------------------------------------|-------------|
| 依赖三方库      | `https://gitcode.com/Ascend/mockcpp.git`      | CMakeLists.txt         | `https://gitcode.com/Ascend/mockcpp.git`      | 单元测试框架依赖    |
| 依赖三方库      | `https://gitcode.com/openeuler/libboundscheck.git` | CMakeLists.txt         | `https://gitcode.com/openeuler/libboundscheck.git` | 安全边界检查库依赖  |
| 依赖三方库      | `https://gitcode.com/openeuler/ubs-comm.git`  | CMakeLists.txt         | `https://gitcode.com/openeuler/ubs-comm.git`  | 通信库依赖      |
| 依赖三方库      | `https://gitcode.com/gh_mirrors/pr/prometheus-cpp.git` | CMakeLists.txt         | `https://gitcode.com/gh_mirrors/pr/prometheus-cpp.git` | 监控指标库依赖    |
| 依赖三方库      | `https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/googletest.git` | install_test_tools.sh  | `https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/googletest.git` | 单元测试框架依赖    |
| license 地址 | 不涉及                                         | LICENSE                | `http://license.coscl.org.cn/MulanPSL2`       | licensefile   |
| license 地址 | 不涉及                                         | 多处源码文件头            | `http://www.apache.org/licenses/LICENSE-2.0`  | Apache许可证声明  |
| license 地址 | `https://github.com/nginx/nginx/blob/master/LICENSE` | nginx相关代码文件        | `https://github.com/nginx/nginx/blob/master/LICENSE` | nginx代码License |
| 依赖三方库      | `https://docs.ceph.com/en/latest/rados/configuration/auth-config-ref/#keys` | boostio_deployment_guide.md      | `https://docs.ceph.com/en/latest/rados/configuration/auth-config-ref/#keys` | Ceph密钥配置参考  |
