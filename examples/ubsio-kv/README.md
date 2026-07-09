# UBSIO-KV Python 样例执行

本文说明如何从端到端环境准备开始，安装 UBSIO 运行库和 `pykvc` 包，配置运行环境变量，并执行 [python_minimal.py](python_minimal.py) 验证最小 KV 调用链路。

该样例用于验证 Python SDK 的 `initialize`、`put`、`get`、`exist`、`get_length`、`delete` 基础接口是否可用，不作为性能 benchmark 或完整推理业务样例。

## 1. 准备构建环境

在 Linux 构建节点准备基础工具和 Python 环境：

```bash
python3 --version
python3 -m pip --version
cmake --version
gcc --version
```

构建工具要求参考 [推理三级池化场景安装部署指南](../../docs/ubsio-boostio/推理三级池化场景安装部署指南.md#构建环境准备)。源码目录保持如下结构：

```text
ubs-io/
├── ubsio-boostio/
├── ubsio-kv/
├── ubsio-common/
└── examples/
```

## 2. 构建运行库和 Python 包

在仓库根目录执行 BoostIO 发布构建，默认会同时构建 UBSIO-KV 组件并生成 `pykvc` wheel：

```bash
cd ubsio-boostio
bash build.sh -t release --pkg
cd ..
```

构建完成后，重点关注以下产物：

```text
ubsio-boostio/dist/lib/
ubsio-kv/dist/pkg/pykvc-*.whl
```

如果 BoostIO 已按 [安装使用](../../docs/ubsio-boostio/推理三级池化场景安装部署指南.md#部署库包) 完成构建，也可以只构建 UBSIO-KV Python 包：

```bash
cd ubsio-kv
bash build.sh -t release
cd ..
```

## 3. 部署库包

准备运行目录并拷贝动态库。以下路径仅为示例，可替换为业务实际部署目录：

```bash
mkdir -p /opt/ubsio/lib
cp -a ubsio-boostio/dist/lib/* /opt/ubsio/lib/
```

如果采用单独构建 UBSIO-KV 的方式，还需要将 `ubsio-kv/dist/lib/` 下的动态库一并放入运行库目录。

安装 `pykvc` wheel：

```bash
python3 -m pip install --force-reinstall ubsio-kv/dist/pkg/pykvc-*.whl
```

## 4. 准备配置文件

默认情况下，运行进程会读取：

```text
/etc/boostio/bio.conf
```

如果需要使用自定义配置文件，可以参考 [配置文件](../../docs/ubsio-boostio/推理三级池化场景安装部署指南.md#配置文件) 准备 `/opt/ubsio/conf/bio.conf`。单机三级池化场景的配置项可参考 [UBS IO 单机模式配置参考](../../docs/单机模式配置说明.md)。

## 5. 设置运行环境变量

执行 Python 样例前，需要保证运行库可被加载：

```bash
export LD_LIBRARY_PATH=/opt/ubsio/lib:${LD_LIBRARY_PATH}
```

如果使用自定义配置文件，再指定配置文件路径：

```bash
export UBSIO_CONFIG_PATH=/opt/ubsio/conf/bio.conf
```

如果使用默认配置文件 `/etc/boostio/bio.conf`，可以不设置 `UBSIO_CONFIG_PATH`。

## 6. 执行 Python 样例

回到仓库根目录执行：

```bash
python3 examples/ubsio-kv/python_minimal.py
```

执行成功时，样例会完成一次写入、读取、存在性检查、长度查询和删除流程，并输出类似结果：

```text
exist(ubsio-python-minimal) = True
get_length(ubsio-python-minimal) = 14
```

## 7. 常见检查项

- `import pykvc` 失败：确认已安装 `ubsio-kv/dist/pkg/pykvc-*.whl`，并且当前执行命令使用的是同一个 Python 环境。
- 动态库加载失败：确认 `LD_LIBRARY_PATH` 包含部署后的 UBSIO 动态库目录。
- 初始化失败：确认配置文件存在且运行用户可读；使用自定义配置时，确认 `UBSIO_CONFIG_PATH` 指向绝对路径。
- 有盘模式启动失败：确认 `bio.disk.path` 指向的设备或分区可访问，并由 UBS IO 独占使用；无盘模式可将 `bio.disk.path` 留空。
