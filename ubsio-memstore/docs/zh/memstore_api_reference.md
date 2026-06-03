# API 参考

## 介绍

本文介绍 UBS MemStore 对外提供的 C API。接口定义以 `src/include/mms_c.h` 为准。

UBS MemStore 主体使用 C/C++ 开发，对外提供 C 语言接口。调用方应先调用 `MmsInitialize` 初始化服务，再调用读写、查询、通知注册等接口，退出时调用 `MmsExit`。

## 基本约定

- 批量接口中，每个 item 都有独立的 `result` 字段，用于返回单条 key 的执行结果。
- 批量接口整体返回值规则：全部成功返回 `RET_MMS_OK`；任意 item 失败时返回最后一个失败 item 的错误码。
- key 统一使用 `const char *key + uint16_t keyLen` 表示，`keyLen` 不包含字符串结束符 `'\0'`。
- `PutItems::isNotify` 和 `DeleteItems::isNotify` 用于控制单条 key 是否触发数据变更通知。全局通知开关关闭或 `isNotify` 为 0 时，不触发通知。
- 前缀查询、范围查询和范围删除受 `mms.art.query.switch` 控制。开关关闭时，相关接口返回不可用错误。
- 查询接口返回的资源必须使用 `MmsFreeResources` 释放。

## 错误码

|错误码|取值|说明|
|--|--:|--|
|`RET_MMS_OK`|0|成功。|
|`RET_MMS_PROTECTED`|1|写保护。|
|`RET_MMS_ERROR`|2|未知错误。|
|`RET_MMS_EPERM`|3|入参错误。|
|`RET_MMS_BUSY`|4|服务忙，需要外部重试。|
|`RET_MMS_NEED_RETRY`|5|需要重试。|
|`RET_MMS_NOT_READY`|6|服务未就绪。|
|`RET_MMS_NOT_FOUND`|7|key 不存在。|
|`RET_MMS_CONFLICT`|8|key 冲突。|
|`RET_MMS_MISS`|9|缓存未命中。|
|`RET_MMS_NO_SPACE`|10|容量不足。|
|`RET_MMS_UNAVAILABLE`|11|服务不可用。|
|`RET_MMS_EXCEED_QUOTA`|12|超过配额限制。|
|`RET_MMS_PT_FAULT`|13|分区故障。|
|`RET_MMS_READ_EXCEED`|14|读取范围超过限制。|
|`RET_MMS_EXISTS`|15|资源已存在。|

## 公共类型

### MmsOptions

MMS 初始化配置。

```c
typedef struct {
    uint16_t netConnectCnt;
    uint16_t netGroupNum;
    uint8_t netIsBusyPolling;
    uint8_t tlsEnable;
    char certificationPath[PATH_MAX];
    char caCerPath[PATH_MAX];
    char caCrlPath[PATH_MAX];
    char privateKeyPath[PATH_MAX];
    char privateKeyPasswordPath[PATH_MAX];
    char decrypterLibPath[PATH_MAX];
    char opensslLibDir[PATH_MAX];
} MmsOptions;
```

|字段| 说明                                                                |
|--|-------------------------------------------------------------------|
|`netConnectCnt`| 每个 channel 的网络连接数。                                                |
|`netGroupNum`| IPC worker group 数量，需要与mms.conf中mms.net.ipc.worker.groups配置的组数一致。 |
|`netIsBusyPolling`| 网络 polling 模式。0 表示 event polling；非 0 表示 busy polling。             |
|`tlsEnable`| TLS 开关。0 表示关闭；非 0 表示开启。                                           |
|`certificationPath`| 证书路径。                                                             |
|`caCerPath`| CA 证书路径。                                                          |
|`caCrlPath`| CA CRL 文件路径。                                                      |
|`privateKeyPath`| 私钥路径。                                                             |
|`privateKeyPasswordPath`| 私钥口令密文文件路径。                                                       |
|`decrypterLibPath`| 证书解密函数动态库路径。                                                      |
|`opensslLibDir`| OpenSSL 动态库目录。                                                    |

### PutItems

写入 item 描述。

```c
typedef struct {
    const char *key;
    const char *value;
    uint32_t valueLen;
    uint16_t keyLen;
    uint16_t isNotify;
    char **valueAddr;
    int32_t *result;
} PutItems;
```

|字段|方向|说明|
|--|--|--|
|`key`|输入|key 首地址。|
|`value`|输入|value 首地址。|
|`valueLen`|输入|value 长度。|
|`keyLen`|输入|key 长度，不包含 `'\0'`。|
|`isNotify`|输入|是否触发数据变更通知。0 表示不通知；非 0 表示通知。|
|`valueAddr`|输出|写入成功后返回底层存储数据的内存首地址。|
|`result`|输出|单条 item 的执行结果。|

### GetItems

读取 item 描述。

```c
typedef struct {
    const char *key;
    uint16_t keyLen;
    uint32_t offset;
    uint32_t length;
    char **value;
    uint32_t *realLength;
    int32_t *result;
} GetItems;
```

|字段|方向|说明|
|--|--|--|
|`key`|输入|key 首地址。|
|`keyLen`|输入|key 长度，不包含 `'\0'`。|
|`offset`|输入|读取起始偏移。|
|`length`|输入|期望读取长度。|
|`value`|输入/输出|若 `*value == NULL`，返回底层存储数据的内存首地址；否则将数据拷贝到 `*value` 指向的内存。|
|`realLength`|输出|实际返回的数据长度。|
|`result`|输出|单条 item 的执行结果。|

### UpdateItems / ReplaceItems

更新和替换 item 描述。`ReplaceItems` 与 `UpdateItems` 使用相同结构。

```c
typedef struct {
    const char *key;
    const char *value;
    uint16_t keyLen;
    uint32_t valueLen;
    uint32_t offset;
    int32_t *result;
} UpdateItems, ReplaceItems;
```

|字段|方向|说明|
|--|--|--|
|`key`|输入|key 首地址。|
|`value`|输入|待写入的数据首地址。|
|`keyLen`|输入|key 长度，不包含 `'\0'`。|
|`valueLen`|输入|待写入的数据长度。|
|`offset`|输入|写入起始偏移。|
|`result`|输出|单条 item 的执行结果。|

### DeleteItems

删除 item 描述。

```c
typedef struct {
    const char *key;
    uint16_t keyLen;
    uint16_t isNotify;
    int32_t *result;
} DeleteItems;
```

|字段|方向|说明|
|--|--|--|
|`key`|输入|key 首地址。|
|`keyLen`|输入|key 长度，不包含 `'\0'`。|
|`isNotify`|输入|是否触发数据变更通知。0 表示不通知；非 0 表示通知。|
|`result`|输出|单条 item 的执行结果。|

### ValueInfo

前缀查询和范围查询返回的 key/value 描述。

```c
typedef struct {
    char *key;
    char *value;
    uint64_t length;
} ValueInfo;
```

|字段|说明|
|--|--|
|`key`|匹配到的 key。|
|`value`|匹配到的 value。|
|`length`|value 长度。|

### 数据变更通知类型

```c
typedef enum {
    OP_PUT = 0,
    OP_DELETE = 1,
    OP_BUTT
} OperateType;

typedef void (*NotifyCallback)(const char *key, OperateType opType);
```

当前通知类型包括：

|类型|说明|
|--|--|
|`OP_PUT`|写入通知。|
|`OP_DELETE`|删除通知。|

### 服务状态回调

```c
typedef void (*ServiceCallback)(uint8_t serviceable);
```

`serviceable` 为 0 表示不可服务，非 0 表示可服务。

## MmsInitialize

### 功能

初始化 MMS 服务。

### 函数原型

```c
CResult MmsInitialize(const MmsOptions *options, ServiceCallback service);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`options`|输入|初始化配置。|
|`service`|输入|服务状态回调函数。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|初始化成功。|
|其它|初始化失败。|

## MmsRegisterNotifyCallback

### 功能

注册数据变更通知回调函数。

### 函数原型

```c
CResult MmsRegisterNotifyCallback(NotifyCallback callback);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`callback`|输入|数据变更通知回调函数。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|注册成功。|
|其它|注册失败。|

## MmsExit

### 功能

退出 MMS 服务。

### 函数原型

```c
void MmsExit(void);
```

### 参数

无。

### 返回值

无。

## MmsPut

### 功能

批量写入 key/value。

### 函数原型

```c
CResult MmsPut(PutItems *itemList, uint32_t itemNum);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`itemList`|输入/输出|写入 item 数组。|
|`itemNum`|输入|item 数量。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|全部 item 写入成功。|
|其它|至少一个 item 写入失败，返回最后一个失败 item 的错误码。|

### 注意事项

- 每个 item 的 `result` 返回该 key 的执行结果。
- 每个 item 的 `valueAddr` 返回实际写入后的内存地址。
- `isNotify` 仅表示该 item 是否请求通知；最终是否通知还受全局通知开关控制。

## MmsGet

### 功能

批量读取 key/value。

### 函数原型

```c
CResult MmsGet(GetItems *itemList, uint32_t itemNum);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`itemList`|输入/输出|读取 item 数组。|
|`itemNum`|输入|item 数量。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|全部 item 读取成功。|
|其它|至少一个 item 读取失败，返回最后一个失败 item 的错误码。|

### 注意事项

- 若 `GetItems::value` 指向的指针为 `NULL`，接口返回底层存储数据的内存首地址，不执行 value 拷贝。
- 若 `GetItems::value` 指向的指针非 `NULL`，接口将读取到的数据拷贝到调用方提供的内存中。
- 免拷贝返回的内存由 MMS 管理，调用方不得释放。

## MmsUpdate

### 功能

批量更新已存在 key 的部分 value。

### 函数原型

```c
CResult MmsUpdate(UpdateItems *itemList, uint32_t itemNum);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`itemList`|输入/输出|更新 item 数组。|
|`itemNum`|输入|item 数量。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|全部 item 更新成功。|
|其它|至少一个 item 更新失败，返回最后一个失败 item 的错误码。|

## MmsDelete

### 功能

批量删除 key。

### 函数原型

```c
CResult MmsDelete(DeleteItems *itemList, uint32_t itemNum);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`itemList`|输入/输出|删除 item 数组。|
|`itemNum`|输入|item 数量。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|全部 item 删除成功。|
|其它|至少一个 item 删除失败，返回最后一个失败 item 的错误码。|

### 注意事项

`isNotify` 仅表示该 item 是否请求通知；最终是否通知还受全局通知开关控制。

## MmsReplace

### 功能

批量替换 key/value。key 已存在时更新，key 不存在时写入。

### 函数原型

```c
CResult MmsReplace(ReplaceItems *itemList, uint32_t itemNum);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`itemList`|输入/输出|替换 item 数组。|
|`itemNum`|输入|item 数量。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|全部 item 替换成功。|
|其它|至少一个 item 替换失败，返回最后一个失败 item 的错误码。|

## MmsGetValuesByPrefix

### 功能

按 key 前缀查询匹配到的 key/value。

> [!TIP]
> 前缀查询会一次性返回所有匹配数据。如果匹配数据量过大，可能导致内存压力过高。调用方应确保查询结果规模可控。

### 函数原型

```c
CResult MmsGetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`prefix`|输入|key 前缀。|
|`valueInfoItems`|输出|匹配结果数组。|
|`itemNum`|输出|匹配结果数量。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|查询成功。|
|其它|查询失败。|

### 注意事项

- 该接口受 `mms.art.query.switch` 控制。
- 成功返回且 `*valueInfoItems != NULL` 时，调用方必须调用 `MmsFreeResources` 释放资源。

## MmsGetValuesByRange

### 功能

按 key 范围查询匹配到的 key/value，范围包含 `start` 和 `end`。

> [!TIP]
> 范围查询会一次性返回所有匹配数据。如果匹配数据量过大，可能导致内存压力过高。调用方应确保查询结果规模可控。

### 函数原型

```c
CResult MmsGetValuesByRange(const char *start, const char *end, ValueInfo **valueInfoItems, uint64_t *itemNum);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`start`|输入|范围起始 key，包含该 key。|
|`end`|输入|范围结束 key，包含该 key，要求 `start <= end`。|
|`valueInfoItems`|输出|匹配结果数组。|
|`itemNum`|输出|匹配结果数量。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|查询成功。|
|其它|查询失败。|

### 注意事项

- 该接口受 `mms.art.query.switch` 控制。
- 成功返回且 `*valueInfoItems != NULL` 时，调用方必须调用 `MmsFreeResources` 释放资源。

## MmsBatchDeleteByRange

### 功能

按 key 范围删除匹配到的 key/value，范围包含 `start` 和 `end`。

### 函数原型

```c
CResult MmsBatchDeleteByRange(const char *start, const char *end);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`start`|输入|范围起始 key，包含该 key。|
|`end`|输入|范围结束 key，包含该 key，要求 `start <= end`。|

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|删除成功。|
|其它|删除失败。|

### 注意事项

该接口受 `mms.art.query.switch` 控制。

## MmsFreeResources

### 功能

释放 `MmsGetValuesByPrefix` 或 `MmsGetValuesByRange` 返回的资源。

### 函数原型

```c
void MmsFreeResources(ValueInfo **valueInfoItems, uint64_t itemNum);
```

### 参数

|参数|方向|说明|
|--|--|--|
|`valueInfoItems`|输入/输出|查询接口返回的结果数组。释放后会置空。|
|`itemNum`|输入|结果数组元素数量。|

### 返回值

无。

## MmsStartCatchUpTask

### 功能

启动恢复追平任务。

### 函数原型

```c
CResult MmsStartCatchUpTask(void);
```

### 参数

无。

### 返回值

|返回值|说明|
|--|--|
|`RET_MMS_OK`|启动成功。|
|其它|启动失败。|
