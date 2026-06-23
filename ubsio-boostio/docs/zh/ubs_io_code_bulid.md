# 源码构建指南

## 概述

本文档为鲲鹏BoostKit IO加速套件UBS IO 26.0.RC1版本的对外定向开源代码的构建指导书，用于指导开发者能够独立完成定向开源代码的构建准备、源码编译和软件构建等工作。

## 构建环境指导

本章节描述对定向开源代码进行编译构建所需的环境规格信息，包括服务器硬件规格、支持的操作系统和构建工具规格等信息。

**硬件规格**

**表 1**  构建环境信息

|硬件信息|版本|
|--|--|
|服务器名称|TaiShan服务器 2280|
|处理器|Kunpeng处理器|
|内存大小|32GB以上|
|硬盘|480GB以上|

**操作系统>**

**表 2**  操作系统信息

|操作系统名称|版本|
|--|--|
|openEuler|22.03 LTS SP4|

>[!NOTICE] 说明
>openEuler不是华为对外发布的操作系统。其具体安装过程请参考《[openEuler系统安装指南](https://docs.openeuler.org/zh/docs/22.03_LTS_SP1/docs/Installation/installation.html)》。

**构建工具**

**表 3**  构建工具信息

|构建工具名称|版本|获取方式|
|--|--|--|
|GCC|7.3.0|操作系统默认安装。|
|CMake|3.16及以上|获取链接|
|git|2.27.0及以上|获取链接|

## 源码构建指导

本章节描述对外定向开源代码的构建指导内容，其中包括强制开源义务的开源软件本身和传染部分。

- **[安装构建工具](#安装构建工具)**  

- **[安装依赖软件](#安装依赖软件)**  

- **[构建源码](#构建源码)**  
本章节中操作步骤以非root用户登录至构建环境后切换至root用户进行构建源码为例。

### 安装构建工具<a id="安装构建工具"></a>

**安装CMake**

1. 获取CMake软件包：[https://github.com/Kitware/CMake/releases/tag/v3.20.5](https://github.com/Kitware/CMake/releases/tag/v3.20.5)。
2. 将获取到的文件放置于“/opt/buildtools/”目录下。
3. 执行以下命令进行安装，以3.20.5版本为例。

    ```cmd
    tar -xzf cmake-3.20.5-linux-aarch64.tar.gz
    # Environment Variable
    [ -f /usr/local/bin/cmake ] && rm -rf /usr/local/bin/cmake
    ln -s cmake-3.20.5-linux-aarch64/bin/cmake /usr/local/bin/cmake
    ```

4. 设置环境变量.

    ```cmd
    echo 'export PATH=/opt/buildtools/cmake-3.20.5-linux-aarch64/bin:$PATH' >> ~/.bashrc
    source ~/.bashrc
    ```

5. 完成安装后查询CMake版本。

    ```cmd
    cmake -version
    ```

**安装GCC**

此处以安装GCC 7.3.0为例，具体步骤如下。

1. 获取安装GCC所需的软件包：[https://ftp.gnu.org/gnu/gcc/gcc-7.3.0/gcc-7.3.0.tar.gz](https://ftp.gnu.org/gnu/gcc/gcc-7.3.0/gcc-7.3.0.tar.gz)。
    - gcc-7.3.0.tar.gz
    - gmp-6.2.1.tar.xz
    - mpfr-4.1.0.tar.gz
    - mpc-1.2.1.tar.gz

2. 将获取到的文件放置于“/opt/buildtools/”目录下；
3. 执行以下命令进行安装。

    ```cmd
    install_dir=/opt/buildtools/gcc-7.3.0
    # clear 
    [ -d "${install_dir}" ] && rm -rf ${install_dir} 
    mkdir -p ${install_dir}
    tmp_cpus=$(grep -w processor /proc/cpuinfo|wc -l)  
    tar -xzf gcc-7.3.0.tar.gz 
    cp gmp-6.2.1.tar.xz mpfr-4.1.0.tar.gz mpc-1.2.1.tar.gz backport-CVE-2019-15847.patch backport-CVE-2018-12886.patch gcc-7.3.0/ 
    cd gcc-7.3.0  
    # install
    cp contrib/download_prerequisites . 
     
    sed -ie "s/gmp='gmp-6.1.0.tar.bz2'/gmp='gmp-6.2.1.tar.xz'/" download_prerequisites 
    sed -ie "s/mpfr='mpfr-3.1.4.tar.bz2'/mpfr='mpfr-4.1.0.tar.gz'/" download_prerequisites 
    sed -ie "s/mpc='mpc-1.0.3.tar.gz'/mpc='mpc-1.2.1.tar.gz'/" download_prerequisites
     
    ./download_prerequisites --no-isl --no-verify  
    ./configure --prefix=${install_dir} --enable-checking=release 
    make -j ${tmp_cpus} > gcc_make_install.log 
    make install-strip >>  gcc_make_install.log  
    
    ln -sf ${install_dir}/bin/gcc  ${install_dir}/bin/cc  
    ln -sf ${install_dir}/bin/* /usr/local/bin 
    update-alternatives --install /usr/bin/gcc gcc /usr/local/bin/gcc 99 
    update-alternatives --install /usr/bin/g++ g++ /usr/local/bin/g++ 99
      
    #update libstdc++ library 
    cp ${install_dir}/lib64/libstdc++.so.6.0.24 /usr/lib64/ 
    chmod 755 /usr/lib64/libstdc++.so.6.0.24 
    ln -sf /usr/lib64/libstdc++.so.6.0.24 /usr/lib64/libstdc++.so.6
    ```

4. 安装完成后查询GCC版本。

    ```cmd
    gcc -v
    ```

**安装Git**

1. 获取Git工具的安装包：[https://git-scm.com/downloads](https://git-scm.com/downloads)。
2. 将获取到的文件放置于“/opt/buildtools/”目录下。
3. 执行以下命令进行安装。

    ```cmd
    install_dir=/opt/buildtools/git-2.37.4  
    # clear [ -d "${install_dir}" ] && rm -rf ${install_dir} 
    mkdir -p ${install_dir}  
    tmp_cpus=$(grep -w processor /proc/cpuinfo|wc -l) 
    tar -xzvf git-2.37.4.tar.gz 
    cd git-2.37.4 
    make -j ${tmp_cpus} prefix=${install_dir} all 
    make prefix=${install_dir} install
      
    #link  
    ln -sf ${install_dir}/bin/* /usr/local/bin  
    
    #config ca 
    cd - 
    mkdir -p ${install_dir}/certs ${install_dir}/etc 
    chmod 755 ${install_dir}/certs ${install_dir}/etc
     
    #mv ca-bundle.crt ${install_dir}/certs/ 
    ls /etc/pki/tls/certs 
    cp -rf /etc/pki/tls/certs/ca-bundle.crt ${install_dir}/certs/ 
    chmod 666 ${install_dir}/certs/ca-bundle.crt 
    ${install_dir}/bin/git config --system http.sslcainfo ${install_dir}/certs/ca-bundle.crt 
    chmod 666 ${install_dir}/etc/gitconfig
    ```

4. 安装完成后查询Git版本。

    ```cmd
    git --version
    ```

### 安装依赖软件<a id="安装依赖软件"></a>

> [!NOTICE] 说明
>用户需要保证构建环境上依赖的开源软件版本和应用程序运行环境上的开源软件版本一致。

当前UBS IO构建过程依赖多个开源软件，因此在构建前需要首先安装依赖的开源软件包，相应软件的推荐版本以及开源仓库地址如[表1](#开源软件信息)所示。

**表 1**  开源软件信息<a id="开源软件信息"></a>

|软件|推荐版本|
|--|--|
|[Hadoop](https://github.com/apache/hadoop.git)|branch-3.3.1|
|[Ceph](https://github.com/ceph/ceph/releases/tag/v12.2.8)|12.2.8|
|[RDMA](https://github.com/linux-rdma/rdma-core)|v42.0|
|[spdlog](https://github.com/gabime/spdlog.git)|v1.12.0|
|[ZooKeeper](https://github.com/apache/zookeeper.git)|release-3.8.1|
|[googletest](https://github.com/google/googletest.git)|release-1.8.1|

### 构建源码<a id="构建源码"></a>

本章节中操作步骤以非root用户登录至构建环境后切换至root用户进行构建源码为例。

- **[构建准备](#构建准备)**  

- **[构建执行](#构建执行)**  

#### 构建准备<a name="构建准备"></a>

1. 将UBS IO软件的开源代码放置到编译机路径布局指定位置上，此处以“code-download-path/”路径为用户下载的代码解压之后的路径为例。
2. 将UBS IO依赖的开源软件放置到编译机路径布局指定的位置。

    ```cmd
    cp -r code-download-path/spdlog-1.12.0/*  /usr1/workspace/boost-io/3rdparty/spdlog/spdlog-1.12.0
    cp -r code-download-path/zookeeper-release-3.8.1/*  /usr1/workspace/boost-io/3rdparty/zookeeper/zookeeper-3.8.1
    cp -r code-download-path/hadoop-branch-3.3.1/*  /usr1/workspace/boost-io/3rdparty/hadoop/hadoop-3.3.1
    cp -r code-download-path/ceph-12.2.8/*  /usr1/workspace/boost-io/3rdparty/ceph/ceph-12.2.8
    ```

    UBS IO引用的开源软件代码仓库及路径位置如[表1](#开源软件路径布局)所示。

    **表 1**  开源软件路径布局<a id="开源软件路径布局"></a>

|开源软件名称|路径布局|
|--|--|
|ZooKeeper|/usr1/workspace/boost-io/3rdparty/zookeeper/zookeeper-3.8.1|
|spdlog|/usr1/workspace/boost-io/3rdparty/spdlog/spdlog-1.12.0|
|Hadoop|/usr1/workspace/boost-io/3rdparty/hadoop/hadoop-3.3.1|
|Ceph|/usr1/workspace/boost-io/3rdparty/ceph/ceph-12.2.8|

1. 将UBS IO软件的闭源动态链接库复制到编译机路径布局指定的位置。

    ```cmd
    cp -r code-download-path/huawei_secure_c  /usr1/workspace/boost-io/output/3rdparty
    cp -r code-download-path/hseceasy /usr1/workspace/boost-io/output/3rdparty
    cp -r code-download-path/hcom /usr1/workspace/boost-io/output/3rdparty
    cp -r code-download-path/bdm /usr1/workspace/boost-io/output/3rdparty
    ```

    UBS IO引用华为公司平台软件如[表2](#平台软件路径布局)所示。

    **表 2**  平台软件路径布局<a id="平台软件路径布局"></a>

|平台软件名称|路径布局|
|--|--|
|huawei_secure_c|/usr1/workspace/boost-io/output/3rdparty|
|hseceasy|/usr1/workspace/boost-io/output/3rdparty|
|hcom|/usr1/workspace/boost-io/output/3rdparty|
|bdm|/usr1/workspace/boost-io/output/3rdparty|

#### 构建执行<a id="构建执行"></a>

1. 进入工作目录。
2. 执行编译构建安装包脚本，建议将所有命令顺序写入一个构建脚本合并执行。
    1. 此处以在“/usr1/workspace”路径下新建一个脚本文件build.sh为例。

        ```cmd
        vi build.sh
        ```

    2. 写入如下两行脚本命令作为开头，然后将步骤的所有命令按顺序粘贴到该脚本文件。

        ```cmd
        #!/bin/bash
        set -e
        ```

    3. 按“ESC”，输入**:wq!**，按“Enter”保存并退出。

3. 编译UBS IO开源代码，包括编译引用开源软件、华为公司平台软件编译对应所需的CMakeLists.txt文件，执行脚本build.sh将自动编译所有开源软件、华为公司平台软件。
4. 使用者首先编写根目录的CMakeLists.txt和build.sh文档，然后执行构建脚本build.sh。

    ```cmd
    bash build.sh
    ```

5. 执行完成后在“/usr1/workspace/boost-io/output”目录下查询是否有输出二进制包BoostKit-boostsds-boostio-1.1.0.zip。

    ```cmd
    cd /usr1/workspace/boost-io/output
    ```

6. 构建完成。
