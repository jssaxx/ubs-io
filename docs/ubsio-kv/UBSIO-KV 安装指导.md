
# UBSIO-KV 安装指导

## 1. 环境要求

### 1.1 编译工具版本

| 工具 | 版本要求 |
| :--- | :--- |
| 操作系统 | OpenEuler 22.03 LTS+ |
| cmake | 3.20.x |
| gcc | 11.4+ |
| python | 3.11.10 |
| pybind11 | 2.10.3 |
| make | 4.3+ |

### 1.2 安装编译依赖

```bash
yum install -y cmake gcc gcc-c++ python3 python3-pip make \
    autoconf automake libtool maven java-1.8.0-openjdk \
    libboundscheck libaio-devel librados-devel liburing-devel pybind11-devel
```

### 1.3 安装 Zookeeper Server

```bash
yum install -y java-1.8.0-openjdk zookeeper
```

---

## 2. 编译打包

### 2.1 编译 Zookeeper Client

```bash
cd scripts/ubsio-kv/
bash build_zookeeper.sh
```

### 2.2 编译打包 UBSIO-KV

> `ubsio-kv` 编译会同步编译其依赖的 `ubsio-boostio` 模块

```bash
cd scripts/ubsio-kv/
bash build_ubsio.sh
```

编译完成后，产物位于 `dist/` 目录：

```
dist/
└── ubsio-kv-1.0.0.tar.gz    # 部署包
```

部署包结构（解压后）：

```
ubsio-kv-1.0.0/
├── ubsio/
│   ├── lib/      # UBSIO-KV 和 Boostio 动态库
│   ├── bin/      # 可执行文件
│   └── conf/     # 配置文件
├── zookeeper/
│   └── lib/      # Zookeeper 客户端库
├── install.sh    # 安装脚本
└── uninstall.sh  # 卸载脚本
```

---

## 3. 部署安装

### 3.1 解压部署包

```bash
tar -xzvf ubsio-kv-1.0.0.tar.gz
cd ubsio-kv-1.0.0/
```

### 3.2 执行安装

```bash
bash install.sh
```

---

## 4. 卸载

```bash
bash uninstall.sh
```

---

## 5. 相关组件安装

### 5.1 memfabric_hybrid

安装指导：[memfabric_hybrid 安装文档](https://gitcode.com/Ascend/memfabric_hybrid/blob/master/doc/installation.md)

### 5.2 memcache

安装指导：[memcache 安装文档](https://gitcode.com/Ascend/memcache/blob/master/doc/build.md)
