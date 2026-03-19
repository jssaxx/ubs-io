# NDS 安装指南

### 安装前准备
NDS 内核模块依赖内核P2P能力，安装前请检查内核是否开启 `CONFIG_ZONE_DEVICE` 编译选项。
```sh
# 执行此命令后若没有任何输出，说明内核未开启 P2P 能力，需重新编译内核或使用Ubuntu内核版本>=4.15的操作系统
grep CONFIG_ZONE_DEVICE /boot/config-$(uname -r)
```

### 安装 NDS 内核模块
```sh
cd ubsio-nds/kernel
insmod ndsfs.ko
```

### 查看 NDS 内核模块是否安装
```sh
lsmod | grep nds
```

### 卸载 NDS 内核模块
```sh
rmmod ndsfs.ko
```
