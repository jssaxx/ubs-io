# UBSIO NDS facade

`ubsio-nds` 构建 `libascend_nds.so`，提供 `ndsFile*` C 接口，并在运行时加载 XDS 的 `libnds.so`。

接口定义见 [`include/ascend_nds.h`](include/ascend_nds.h)，使用说明见 [Ascend NDS API](../docs/ubsio-nds/ascend_nds_api.md)，底层原语见 [NDS API 原语](../docs/ubsio-nds/nds_api.md)。

## 构建

在 Linux 上准备 Git、GNU Make、GCC/G++ 和 CMake，从仓库根目录执行。

```bash
bash ubsio-nds/build.sh -t release
```

CMake 通过 FetchContent 浅拉取 openEuler 内核仓的 `openEuler-26.09` 分支，仅检出 XDS 用户态源码及所需的 UAPI 头文件，默认缓存于 `ubsio-nds/Build/_deps/xds-openeuler-src/`。构建核对用户态 ABI 声明后，通过 `tools/lib/nds/file_p2p/Makefile` 编译 XDS。[openEuler XDS 源码](https://atomgit.com/openeuler/kernel/tree/openEuler-26.09/tools/lib/nds)

离线构建可将 `openEuler-26.09` 分支源码预置到本地，并将 `FETCHCONTENT_SOURCE_DIR_XDS` 指向内核源码根目录。构建使用 `tools/lib/nds/file_p2p` 和 `tools/include/uapi`。

```bash
FETCHCONTENT_SOURCE_DIR_XDS=/path/to/openeuler-kernel bash ubsio-nds/build.sh -t release
```

`ubsio-nds/dist/lib/` 包含 `libascend_nds.so.1.0.0` 及其 `libascend_nds.so.1`、`libascend_nds.so` 符号链接，也包含 XDS 库及其 `libnds.so` 符号链接。公开头文件位于 `ubsio-nds/dist/include/`。通过 `bash scripts/build.sh --components nds` 构建时，两套库会同时收集到 `output/lib/`。

UBSIO-KV 启用 NDS 时动态加载 `libascend_nds.so`，该库再加载 XDS 的 `libnds.so`。部署时将同一版本构建的两套库加入动态库搜索路径。

部署时使用对应 openEuler 内核构建中的 `p2p_dev` 和兼容的 Ascend 驱动。用户态库与内核模块须使用配套的 ABI。

## 使用示例

### 加载 NDS 内核模块

加载已安装的 XDS 模块。库打开的设备节点为 `/dev/p2p_device`。

```bash
sudo modprobe p2p_dev
```

### 运行读性能测试

从仓库根目录构建 [benchmark](benchmarks/test_nds.cpp)。`ASCEND_TOOLKIT_HOME` 指向包含 ACL 头文件与 `libascendcl.so` 的工具包目录。示例使用至少 1 MiB 的测试文件，所在存储拓扑须可由 NDS 注册，I/O 偏移、长度和缓冲区地址均按 512 字节对齐。

```bash
bash ubsio-nds/build.sh -t release --bench ON
export LD_LIBRARY_PATH="$PWD/ubsio-nds/dist/lib:$ASCEND_TOOLKIT_HOME/lib64:${LD_LIBRARY_PATH:-}"
ubsio-nds/dist/bin/test_nds /path/to/nds-supported-test-file 1024 64 1 0 1
```

参数依次为测试文件、读取长度（KiB）、单次读取粒度（KiB）、线程数、NPU 编号、重复次数。该命令执行 NDS 异步/同步读取及普通 `pread` 对照，结果写入当前目录的 `statistic_out.csv`；若日志出现读取或 NDS 错误，本次结果无效。

### 卸载 NDS 内核模块

测试程序退出后卸载本次加载的模块。

```bash
sudo modprobe -r p2p_dev
```
