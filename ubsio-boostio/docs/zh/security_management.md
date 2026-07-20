# 安全指南

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

密钥更新需要重启UBS IO加速组件服务，请合理规划密钥更新周期。密钥管理请参见开启TLS认证。

### 缓冲区溢出安全保护

为阻止缓冲区溢出攻击，建议使用ASLR（Address space layout randomization）技术，通过对堆、栈、共享库映射等线性区布局的随机化，增加攻击者预测目的地址的难度，防止攻击者直接定位攻击代码位置。该技术可作用于堆、栈、内存映射区（mmap基址、shared libraries、vdso页）。

开启方式：

```cmd
echo 2 >/proc/sys/kernel/randomize_va_space
```

>[!TIP]须知
>
>该命令需要root权限才能执行，且该修改方式是临时的，系统重启后会失效。

## 公网地址声明

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

## 账户一览表

>[!TIP]须知
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
