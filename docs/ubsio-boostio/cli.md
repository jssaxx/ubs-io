    # UBSIO-Boostio CLI使用指导

## 1. 安装部署
### 1.so安装
```bash
    将libsdk_diagnose.so，libserver_diagnose.so拷贝到/usr/lib64/boostio/test_tools下，
将libcli_agent.so拷贝到/usr/lib64下
```

### 2.rpm包安装
```bash
    rpm -ivh ubs-io-test-tools-1.0.0-4.oe2403sp4.aarch64.rpm
    
    rpm安装之后二进制文件都在/usr/bin/boostio/test_tools下
```

### 3.配置文件新修改
```bash
boostio(/etc/boostio/bio.conf)配置文件增加配置项bio_cli_tools_enable = true
```

## 2. 启动cli
### 1.  后台启动cli_server
```bash
    ./cli_server &
```
### 2.  前台启动cli_client
```bash
    ./cli_client
```

## 3.   在cli_client中使用server与sdk diagnose
### 1.  查看当前进程
```bash
    ls
```
### 2.  与指定进程交互
```bash
    attach ${ls显示的进程号}
```
```bash
    server端进程为指定进程号456，sdk端进程为系统进程号
```
### 3.  命令提示
```bash
    help
```