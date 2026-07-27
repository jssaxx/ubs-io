# ubsio-kv API 接口列表

本文档说明 ubsio-kv 面向上层应用提供的标准 KV Cache 接口。C API 定义见 [ubsio_kvc.h](../../ubsio-kv/include/ubsio_kvc.h)，Python SDK 封装见 [pykvc.py](../../ubsio-kv/python_whl/pykvc/pykvc/pykvc.py)。Python 最小样例执行说明见 [examples/ubsio-kv/README.md](../../examples/ubsio-kv/README.md)。

## 通用约定

- 返回值为 `int32_t` 的接口，`0` 表示成功，非 `0` 表示失败。
- `key` 为 C 字符串，长度范围为 `1-255` 字节，且不能为 `NULL`。
- `flags` 为保留字段，当前建议传 `0`。
- 批量接口中的 `keys`、`bufs`、`lengths`、`results` 等数组长度需与 `keysCount` 一致，批量数量范围为 `1-16384`。
- 调用读写类接口前，应先调用 `UbsioKvCacheInit` 完成初始化；进程退出前调用 `UbsioKvCacheExit` 释放资源。

## UbsioKvCacheInit

作用：初始化 UBS IO KV Cache 客户端。

```c
int32_t UbsioKvCacheInit(int32_t devId);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `devId` | `int32_t`；`-1` 或有效设备 ID | `-1` 表示不绑定 ACL 设备并使用 standalone device 0；非负值表示绑定指定设备。 |

返回值：`0` 表示初始化成功，非 `0` 表示失败。

## UbsioKvCacheRegisterMetaEventCallback

作用：注册 UBS IO 元数据事件回调，供上层同步 SSD recovery 或淘汰删除事件。

```c
typedef enum {
    UBSIO_META_RECOVER_C = 0,
    UBSIO_META_DELETE_C = 1,
} UbsioMetaEventTypeC;

typedef struct {
    int32_t type;
    const char *key;
    uint32_t keyLen;
} UbsioMetaEventC;

typedef void (*UbsioMetaEventCallbackC)(void *context,
                                        const UbsioMetaEventC *events,
                                        uint32_t count);

int32_t UbsioKvCacheRegisterMetaEventCallback(UbsioMetaEventCallbackC callback, void *context);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `callback` | `UbsioMetaEventCallbackC` 或 `NULL` | 事件回调函数；传入 `NULL` 表示注销回调。 |
| `context` | `void *` | 用户上下文，会在回调时原样传回。 |

事件缓冲区和 `key` 指针只在回调期间有效，调用方如需长期保存应自行拷贝。

返回值：`0` 表示注册成功，非 `0` 表示失败。

## UbsioKvCacheExit

作用：退出 UBS IO KV Cache 客户端并释放相关资源。

```c
void UbsioKvCacheExit(void);
```

参数：无。

返回值：无。

## UbsioKvCachePut

作用：按 key 写入单个 KV Cache 数据。

```c
int32_t UbsioKvCachePut(const char *key, void *buf, size_t length, uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `const char *`；长度 `1-255` | 数据 key，不能为空。 |
| `buf` | `void *`；非 `NULL` | 待写入的数据地址。 |
| `length` | `size_t`；`> 0` | 待写入数据长度，单位为字节。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示写入成功，非 `0` 表示失败。

## UbsioKvCacheBatchPut

作用：批量写入多个 KV Cache 数据。

```c
int32_t UbsioKvCacheBatchPut(const char **keys,
                             uint32_t keysCount,
                             void **bufs,
                             size_t *lengths,
                             int *results,
                             uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `const char **`；数组长度为 `keysCount` | key 数组，每个 key 长度 `1-255`。 |
| `keysCount` | `uint32_t`；`1-16384` | 批量写入的 key 数量。 |
| `bufs` | `void **`；数组长度为 `keysCount` | 待写入数据地址数组，每个元素不能为空。 |
| `lengths` | `size_t *`；数组长度为 `keysCount` | 每个 value 的长度，单位为字节，每项应 `> 0`。 |
| `results` | `int *`；数组长度为 `keysCount` | 出参，逐项返回写入结果，`0` 表示对应 key 写入成功。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示接口调用成功；单个 key 的结果以 `results` 为准。

## UbsioKvCacheGet

作用：按 key 读取单个 KV Cache 数据。

```c
int32_t UbsioKvCacheGet(const char *key, void *buf, size_t length, uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `const char *`；长度 `1-255` | 待读取数据的 key，不能为空。 |
| `buf` | `void *`；非 `NULL` | 出参，存放读取结果的缓冲区。 |
| `length` | `size_t`；`> 0` | `buf` 可承载长度，单位为字节。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示读取成功，非 `0` 表示失败。

## UbsioKvCacheBatchGet

作用：批量读取多个 KV Cache 数据。

```c
int32_t UbsioKvCacheBatchGet(const char **keys,
                             uint32_t keysCount,
                             void **bufs,
                             size_t *lengths,
                             int *results,
                             uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `const char **`；数组长度为 `keysCount` | key 数组，每个 key 长度 `1-255`。 |
| `keysCount` | `uint32_t`；`1-16384` | 批量读取的 key 数量。 |
| `bufs` | `void **`；数组长度为 `keysCount` | 出参，存放每个 key 对应的数据地址。 |
| `lengths` | `size_t *`；数组长度为 `keysCount` | 每个读取缓冲区或返回数据的长度，单位为字节。 |
| `results` | `int *`；数组长度为 `keysCount` | 出参，逐项返回读取结果，`0` 表示对应 key 读取成功。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示接口调用成功；单个 key 的结果以 `results` 为准。

## UbsioKvCacheBatchGetDirect

作用：预留接口，后续支持批量直通读取到显存 buffer 空间。

```c
int32_t UbsioKvCacheBatchGetDirect(const char **keys,
                                   uint32_t keysCount,
                                   void ***bufs,
                                   size_t **lengths,
                                   uint32_t lengthsRows,
                                   uint32_t lengthsCols,
                                   int *results,
                                   uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `const char **`；数组长度为 `keysCount` | key 数组，每个 key 长度 `1-255`。 |
| `keysCount` | `uint32_t`；`1-16384` | 批量读取的 key 数量。 |
| `bufs` | `void ***`；二维 buffer 地址数组 | 出参，按 `keysCount x lengthsCols` 组织每个 key 的多段目标显存 buffer。 |
| `lengths` | `size_t **`；二维长度数组 | 每段目标 buffer 的长度，单位为字节。 |
| `lengthsRows` | `uint32_t`；建议等于 `keysCount` | `lengths` 的行数。 |
| `lengthsCols` | `uint32_t`；`> 0` | 每个 key 对应的 buffer 段数。 |
| `results` | `int *`；数组长度为 `keysCount` | 出参，逐项返回读取结果，`0` 表示对应 key 读取成功。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示接口调用成功；单个 key 的结果以 `results` 为准。

## UbsioKvCacheExist

作用：查询单个 key 是否存在。

```c
bool UbsioKvCacheExist(const char *key, uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `const char *`；长度 `1-255` | 待查询的 key，不能为空。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`true` 表示 key 存在，`false` 表示 key 不存在或查询失败。

## UbsioKvCacheBatchExist

作用：批量查询多个 key 是否存在。

```c
int32_t UbsioKvCacheBatchExist(const char **keys,
                               uint32_t keysCount,
                               bool *results,
                               uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `const char **`；数组长度为 `keysCount` | key 数组，每个 key 长度 `1-255`。 |
| `keysCount` | `uint32_t`；`1-16384` | 批量查询的 key 数量。 |
| `results` | `bool *`；数组长度为 `keysCount` | 出参，逐项返回是否存在，`true` 表示存在。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示查询成功，非 `0` 表示失败。

## UbsioKvCacheDelete

作用：按 key 删除单个 KV Cache 数据。

```c
int32_t UbsioKvCacheDelete(const char *key, uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `const char *`；长度 `1-255` | 待删除的 key，不能为空。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示删除成功，非 `0` 表示失败。

## UbsioKvCacheBatchDelete

作用：批量删除多个 key。

```c
int32_t UbsioKvCacheBatchDelete(const char **keys,
                                uint32_t keysCount,
                                int32_t *results,
                                uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `const char **`；数组长度为 `keysCount` | key 数组，每个 key 长度 `1-255`。 |
| `keysCount` | `uint32_t`；`1-16384` | 批量删除的 key 数量。 |
| `results` | `int32_t *`；数组长度为 `keysCount` | 出参，逐项返回删除结果，`0` 表示对应 key 删除成功。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示接口调用成功；单个 key 的结果以 `results` 为准。

## UbsioKvCacheGetLength

作用：查询单个 key 对应 value 的长度。

```c
int32_t UbsioKvCacheGetLength(const char *key, size_t *length, uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `const char *`；长度 `1-255` | 待查询的 key，不能为空。 |
| `length` | `size_t *`；非 `NULL` | 出参，返回 value 长度，单位为字节。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示查询成功，非 `0` 表示失败。

## UbsioKvCacheBatchGetLength

作用：批量查询多个 key 对应 value 的长度。

```c
int32_t UbsioKvCacheBatchGetLength(const char **keys,
                                   uint32_t keysCount,
                                   size_t *lengths,
                                   int32_t *results,
                                   uint32_t flags);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `const char **`；数组长度为 `keysCount` | key 数组，每个 key 长度 `1-255`。 |
| `keysCount` | `uint32_t`；`1-16384` | 批量查询的 key 数量。 |
| `lengths` | `size_t *`；数组长度为 `keysCount` | 出参，逐项返回 value 长度，单位为字节。 |
| `results` | `int32_t *`；数组长度为 `keysCount` | 出参，逐项返回查询结果，`0` 表示对应 key 查询成功。 |
| `flags` | `uint32_t`；当前建议 `0` | 保留字段。 |

返回值：`0` 表示接口调用成功；单个 key 的结果以 `results` 为准。

## UbsioKvCacheBatchFree

作用：释放批量读取接口返回的共享内存地址。

```c
int32_t UbsioKvCacheBatchFree(void **bufs, uint32_t keysCount);
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `bufs` | `void **`；数组长度为 `keysCount` | 待释放的地址数组。 |
| `keysCount` | `uint32_t`；`1-16384` | 需要释放的地址数量。 |

返回值：`0` 表示释放成功，非 `0` 表示失败。

## Python SDK 接口

Python SDK 主要是 C API 的轻量封装。接口定义见 [pykvc.py](../../ubsio-kv/python_whl/pykvc/pykvc/pykvc.py)。

### initialize

作用：初始化 UBS IO KV Cache 客户端。

```python
initialize(device_id=-1) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `device_id` | `int`；默认 `-1` | `-1` 表示不绑定 ACL 设备并使用 standalone device 0；非负值表示绑定指定设备。 |

返回值：`0` 表示初始化成功，`-1` 表示失败。

### exit

作用：退出 UBS IO KV Cache 客户端。

```python
exit()
```

参数：无。

返回值：无。

### put

作用：按 key 写入单个 KV Cache 数据。

```python
put(key, value) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `str`；长度 `1-255` | 数据 key，不能为空。 |
| `value` | `bytes` 或类 bytes 对象；长度 `> 0` | 待写入的数据内容。 |

返回值：`0` 表示写入成功，`-1` 表示失败。

### get

作用：按 key 读取单个 KV Cache 数据。

```python
get(key, value) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `str`；长度 `1-255` | 待读取数据的 key，不能为空。 |
| `value` | 类 bytes 缓冲区；长度 `> 0` | 出参缓冲区，用于承载读取结果。 |

返回值：`0` 表示读取成功，`-1` 表示失败。

### delete

作用：按 key 删除单个 KV Cache 数据。

```python
delete(key) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `str`；长度 `1-255` | 待删除的 key，不能为空。 |

返回值：`0` 表示删除成功，`-1` 表示失败。

### exist

作用：查询单个 key 是否存在。

```python
exist(key) -> bool
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `str`；长度 `1-255` | 待查询的 key，不能为空。 |

返回值：`True` 表示 key 存在，`False` 表示 key 不存在或查询失败。

### get_length

作用：查询单个 key 对应 value 的长度。

```python
get_length(key) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `str`；长度 `1-255` | 待查询的 key，不能为空。 |

返回值：大于 `0` 表示 value 长度，单位为字节；`0` 表示查询失败或 value 长度为 `0`。

### batch_put

作用：批量写入多个 KV Cache 数据。

```python
batch_put(keys, values) -> list
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `list[str]`；长度 `1-16384` | key 列表，每个 key 长度 `1-255`。 |
| `values` | `list[bytes]` 或类 bytes 对象列表 | value 列表，长度需与 `keys` 一致。 |

返回值：结果列表，列表长度与 `keys` 一致；每项 `0` 表示对应 key 写入成功，`-1` 表示失败。

### batch_get

作用：批量读取多个 KV Cache 数据。

```python
batch_get(keys, values) -> list
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `list[str]`；长度 `1-16384` | key 列表，每个 key 长度 `1-255`。 |
| `values` | 类 bytes 缓冲区列表 | 出参缓冲区列表，长度需与 `keys` 一致。 |

返回值：结果列表，列表长度与 `keys` 一致；每项 `0` 表示对应 key 读取成功，`-1` 表示失败。

### batch_exist

作用：批量查询多个 key 是否存在。

```python
batch_exist(keys) -> list[bool]
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `list[str]`；长度 `1-16384` | key 列表，每个 key 长度 `1-255`。 |

返回值：布尔结果列表，列表长度与 `keys` 一致；`True` 表示对应 key 存在，`False` 表示不存在或查询失败。

### batch_delete

作用：批量删除多个 key。

```python
batch_delete(keys) -> list
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `list[str]`；长度 `1-16384` | key 列表，每个 key 长度 `1-255`。 |

返回值：结果列表，列表长度与 `keys` 一致；每项 `0` 表示对应 key 删除成功，`-1` 表示失败。

### batch_get_length

作用：批量查询多个 key 对应 value 的长度。

```python
batch_get_length(keys) -> list
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `list[str]`；长度 `1-16384` | key 列表，每个 key 长度 `1-255`。 |

返回值：长度列表，列表长度与 `keys` 一致；每项为对应 key 的 value 长度，长度为 `0` 表示查询失败或 value 长度为 `0`。

### nds_init

作用：初始化 NDS（NPU Direct Storage）。

```python
nds_init(device: int) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `device` | `int` | 本地 NPU device index。 |

返回值：`0` 表示初始化成功，`-1` 表示失败。

### nds_uninit

作用：反初始化 NDS。

```python
nds_uninit() -> int
```

返回值：`0` 表示成功，`-1` 表示失败。

### nds_regmem

作用：注册 NDS 可直接读写的内存区域。

```python
nds_regmem(addr, length) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `addr` | `int` | 待注册内存首地址。 |
| `length` | `int`；`> 0` | 待注册内存长度，单位为字节。 |

返回值：`0` 表示成功，`-1` 表示失败。

### nds_unregmem

作用：注销已注册的 NDS 内存区域。

```python
nds_unregmem(addr, length) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `addr` | `int` | 待注销内存首地址。 |
| `length` | `int`；`> 0` | 待注销内存长度，单位为字节。 |

返回值：`0` 表示成功，`-1` 表示失败。

### nds_read

作用：按 key 将数据直接读取到一组目标 buffer。

```python
nds_read(key: str, buffers: list[int], sizes: list[int]) -> int
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `key` | `str`；长度 `1-255` | 待读取数据的 key。 |
| `buffers` | `list[int]` | 目标 buffer 地址列表。 |
| `sizes` | `list[int]` | 每段目标 buffer 的长度，单位为字节。 |

返回值：`0` 表示成功，`-1` 表示失败。

### nds_batch_read

作用：批量按 key 将数据直接读取到多组目标 buffer。

```python
nds_batch_read(keys: list[str], buffers: list[list[int]], sizes: list[list[int]]) -> list[int]
```

| 参数 | 类型/取值范围 | 参数说明 |
| --- | --- | --- |
| `keys` | `list[str]`；长度 `1-16384` | key 列表，每个 key 长度 `1-255`。 |
| `buffers` | `list[list[int]]` | 每个 key 对应的目标 buffer 地址列表。 |
| `sizes` | `list[list[int]]` | 每个 key 对应的目标 buffer 长度列表，单位为字节。 |

返回值：结果列表，列表长度与 `keys` 一致；每项 `0` 表示对应 key 读取成功，`-1` 表示失败。

## Python SDK 对应关系

以下为 KV Python 接口与 `UbsioKvCache*` C API 的对应关系；NDS 辅助接口对应 `c2python_sdk.Nds*` 绑定，不对应 `UbsioKvCache*` C ABI。

| Python 接口 | 对应 C API |
| --- | --- |
| `initialize` | `UbsioKvCacheInit` |
| `exit` | `UbsioKvCacheExit` |
| `put` | `UbsioKvCachePut` |
| `get` | `UbsioKvCacheGet` |
| `delete` | `UbsioKvCacheDelete` |
| `exist` | `UbsioKvCacheExist` |
| `get_length` | `UbsioKvCacheGetLength` |
| `batch_put` | `UbsioKvCacheBatchPut` |
| `batch_get` | `UbsioKvCacheBatchGet` |
| `batch_exist` | `UbsioKvCacheBatchExist` |
| `batch_delete` | `UbsioKvCacheBatchDelete` |
| `batch_get_length` | `UbsioKvCacheBatchGetLength` |
