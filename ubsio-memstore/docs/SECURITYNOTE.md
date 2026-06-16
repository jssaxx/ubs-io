### 通信矩阵

| 原设备             | 源IP地址     | 源端口             | 目的设备            | 目的IP地址                   | 目的端口（侦听）                                                | 协议            | 端口说明                     | 侦听端口是否可更改 | 认证方式 |
|-----------------|-----------|-----------------|-----------------|--------------------------|---------------------------------------------------------|---------------|:-------------------------|-----------|------|
| 计算服务器         | 计算服务器IP地址 | 随机分配（由操作系统自动分配） | MemStore Server | MemStore Server自配置IP | 7201-7800                                                  | ROCE          | MemStore SDK与MemStore Server之间的通信交互；MemStore Server和MemStore Server之间的通信交互 | 是         | 证书双向认证 |
| 计算服务器         | 计算服务器IP地址 | 随机分配（由操作系统自动分配） | MemStore Server | MemStore Server自配置IP | 7201-7800                                                  | ROCE          | MemStore Server和MemStore Server之间的组播通信交互                        | 是         | 证书双向认证 |
| 计算服务器         | 计算服务器IP地址 | 随机分配（由操作系统自动分配） | Zookeeper运行服务器 | Zookeeper配置           | 客户配置                                                    | TCP           | Zookeeper业务端口，用于MemStore集群管理的消息交互                       | 是         | Zookeeper配置 |

说明：
- Zookeeper为开源组件，由客户自己安装在环境上安装。配置参考Zookeeper的官网安全配置章节 https://zookeeper.apache.org/doc/current/zookeeperProgrammers.html#sc_Security
- 通信矩阵中涉及的安全配置建议用户根据实际业务场景和安全要求进行配置。

### 运行用户建议

- 基于安全性考虑，建议您在执行任何命令时，不建议使用root等管理员类型账户执行，遵循权限最小化原则。

### 文件权限最大值建议

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
| 密钥组件、私钥、证书、密文文件目录 | 700（rwx—----）  |
| 密钥组件、私钥、证书、加密密文   | 600（rw-------） |
| 加解密接口、加解密脚本       | 500（r-x------） |


### 源码内公网地址

| 类型         | 开源代码地址                                      | 文件名                    | 公网IP地址/公网URL地址/域名/邮箱地址                      | 用途说明        |
|------------|---------------------------------------------|------------------------|---------------------------------------------|-------------|
| 依赖三方库      | https://gitcode.com/openeuler/libboundscheck.git | CMakeLists.txt         | https://gitcode.com/openeuler/libboundscheck.git | 安全边界检查库依赖  |
| 依赖三方库      | https://gitcode.com/openeuler/ubs-comm.git  | CMakeLists.txt         | https://gitcode.com/openeuler/ubs-comm.git  | 通信库依赖      |
| 依赖三方库      | https://gitcode.com/GitHub_Trending/sp/spdlog.git | CMakeLists.txt         | https://gitcode.com/GitHub_Trending/sp/spdlog.git | 日志框架依赖      |
| 依赖三方库      | https://gitcode.com/gh_mirrors/li/libart.git | CMakeLists.txt         | https://gitcode.com/gh_mirrors/li/libart.git | ART索引库依赖    |
| 依赖三方库      | https://gitcode.com/gh_mirrors/zo/zookeeper.git | CMakeLists.txt         | https://gitcode.com/gh_mirrors/zo/zookeeper.git | Zookeeper依赖   |
| license 地址 | http://license.coscl.org.cn/MulanPSL2       | 多处源码文件头            | http://license.coscl.org.cn/MulanPSL2       | licensefile   |
| license 地址 | http://www.apache.org/licenses/LICENSE-2.0 | 多处源码文件头            | http://www.apache.org/licenses/LICENSE-2.0  | Apache许可证声明  |
| 依赖三方软件      | http://archive.apache.org/dist/zookeeper/zookeeper-3.7.0/zookeeper-3.7.0.tar.gz | memstore_deployment_guide.md      | http://archive.apache.org/dist/zookeeper/zookeeper-3.7.0/zookeeper-3.7.0.tar.gz | Zookeeper下载参考  |
