# API参考

## 介绍

本文主要介绍UBS MemStore对外提供的接口。

- 编程语言

    UBS MemStore主体使用C/C++语言开发，对外提供C API。

- 功能架构

    UBS MemStore通过组播分发技术实现全副本、低时延的端到端读写能力，。

- API参考

    介绍应用开发过程中最常用和基础的API，建议使用UBS MemStore的开发者对这些API都有所了解。

- 错误码

    介绍UBS MemStore的错误码名称、取值及部分常见错误码的处理方法。

## CResult MmsInitialize

**函数定义**

MMS初始化接口

**实现方法**

CResult MmsInitialize\(MmsOptions &options, ServicesCallback services\)

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|options|MmsOptions|入参|SDK端通信配置参数|
|services|ServicesCallbacktypedef void (*ServiceCallback)(bool serviceable)|入参|可服务订阅回调函数|

**表 2**  详细参数说明

|类型|结构体字段|说明|
|--|--|--|
|MmsOptions|netConnectCnt|每个通信channel的ep数|
|netGroupNum|通信worker分组数量，分离部署时为配置项ipc.worker.groups的组数，融合部署自动计算|
|netIsBusyPolling|busy polling 或者 event polling工作模式|
|tlsEnable|安全开关。0：表示关闭。非0：表示打开。|
|certificationPath|Client证书路径，安全使能时要求路径有效。|
|caCerPath|CA证书路径，安全使能时要求路径有效。|
|caCrlPath|吊销证书列表文件路径，可选。|
|privateKeyPath|Client证书私钥路径，安全使能时要求路径有效。|
|privateKeyPasswordPath|Client证书私钥口令密文的文件路径，安全使能时要求路径有效。|
|decrypterLibPath|Client证书解密函数so文件路径，安全使能时要求路径有效。|
|opensslLibDir|Client端openssl, crypto的so目录路径，可选，为空时使用默认版本，安全使能时要求路径有效。|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|初始化成功。|
|其它|初始化失败。|

## CResult MmsRegisterCallback

**函数定义**

MMS注册数据变更通知函数

**实现方法**

CResult MmsRegisterCallback\(NotifyCallback callback\)

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|callback|NotifyCallback|入参|数据变更通知回调函数|

**表 2**  详细参数说明

|类型|类型定义|说明|
|--|--|--|
| NotifyCallback|typedef void (\*NotifyCallback)(const char \*key, OperateType opType) | 数据变更通知回调函数|
|OperateType|typedef enum {OP_PUT = 0,OP_UPDATE = 1,OP_DELETE = 2,OP_REPLACE = 3,OP_BUTT} OperateType|数据变更类型|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|注册成功。|
|其它|注册失败。|

## void MmsExit

**函数定义**

MMS退出函数

**实现方法**

void MmsExit\(void\)

**参数说明**

无参数

**返回值**

无返回值

## CResult MmsPut

**函数定义**

MMS写接口

**实现方法**

CResult MmsPut\(uint64\_t userId, PutItems \*itemList, uint32\_t num\)

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|userId|uint64_t|入参|用户业务ID|
|itemList|PutItems *|入参|KEY/VALUE描述信息|
|num|uint32_t|入参|本次批量写入KEY/VALUE的个数|

**表 2**  详细参数说明

|类型|类型定义|说明|
|--|--|--|
|PutItems|typedef struct {char \*key;char \*value;uint64_t length;} PutItems|KEY/VALUE描述信息结构体限制：单组key长度(包含'\0')+value长度不能超过mms.net.message.max_buff_size - 48B|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|写入成功。|
|其它|写入失败。|

## CResult MmsGet

**函数定义**

MMS读接口

**实现方法**

CResult MmsGet\(uint64\_t userId, GetItems \*itemList, uint32\_t itemNum\);

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|userId|uint64_t|入参|用户业务ID|
|itemList|GetItems *|出参、出参|KEY/VALUE描述信息|
|itemNum|uint32_t|入参|本次批量读取KEY/VALUE的个数|

**表 2**  详细参数说明

|类型|类型定义|说明|
|--|--|--|
|GetItems|typedef struct {char \*key;uint64_t offset; // 偏移量uint64_t length; // 本次需要读取的数据长度char \*value;uint64_t \*realLength; //本次读取到的实际数据长度} GetItems|KEY/VALUE描述信息结构体，用于存储本次读取到的数据信息|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|读取成功。|
|其它|读取失败。|

## CResult MmsUpdate

**函数定义**

MMS更新接口

**实现方法**

CResult MmsUpdate\(uint64\_t userId, UpdateItems \*itemList, uint32\_t itemNum\)

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|userId|uint64_t|入参|用户业务ID|
|itemList|UpdateItems *|入参|KEY/VALUE描述信息|
|itemNum|uint32_t|入参|本次批量更新KEY/VALUE的个数|

**表 2**  详细参数说明

|类型|类型定义|说明|
|--|--|--|
|UpdateItems|typedef struct {char \*key;char \*value;uint64_t offset;uint64_t length;} UpdateItems|KEY/VALUE描述信息结构体限制：单组key长度(包含'\0')+value长度不能超过mms.net.message.max_buff_size - 48B|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|更新成功。|
|其它|更新失败。|

## CResult MmsReplace

**函数定义**

MMS Replace接口，有则更新，无则写入

**实现方法**

CResult MmsReplace\(uint64\_t userId, ReplaceItems \*itemList, uint32\_t itemNum\)

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|userId|uint64_t|入参|用户业务ID|
|itemList|ReplaceItems *|入参|KEY/VALUE描述信息|
|itemNum|uint32_t|入参|本次批量Replace的KEY/VALUE的个数|

**表 2**  详细参数说明

|类型|类型定义|说明|
|--|--|--|
|ReplaceItems|typedef struct {char \*key;char \*value;uint64_t offset;uint64_t length;} ReplaceItems|KEY/VALUE描述信息结构体限制：单组key长度(包含'\0')+value长度不能超过mms.net.message.max_buff_size - 48B|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|Replace成功。|
|其它|Replace失败。|

## CResult MmsDelete

**函数定义**

MMS删除接口

**实现方法**

CResult MmsDelete\(uint64\_t userId, DeleteItems \*itemList, uint32\_t itemNum\)

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|userId|uint64_t|入参|用户业务ID|
|itemList|DeleteItems *|入参|KEY/VALUE描述信息|
|itemNum|uint32_t|入参|本次批量删除KEY/VALUE的个数|

**表 2**  详细参数说明

|类型|类型定义|说明|
|--|--|--|
|DeleteItems|typedef struct {char *key;} DeleteItems|KEY描述信息结构体|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|删除成功。|
|其它|删除失败。|

## CResult MmsStartCatchUpTask

**函数定义**

MMS数据同步恢复接口

**实现方法**

CResult MmsStartCatchUpTask\(void\)

**参数说明**

无参数

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|操作成功。|
|其它|操作失败。|

## CResult MmsGetValuesByPrefix

>[!TIP] 须知
>前缀查询会把所有匹配到的数据全部返回给上层，如果匹配到的数据量过大，可能会把设备上的物理内存耗尽，从而导致oom，上层应用应该保证查询的数据量在可接受的内存范围内。

**函数定义**

MMS前缀查询接口，返回与指定前缀匹配的所有的key及其value。

**实现方法**

CResult MmsGetValuesByPrefix\(const char \*prefix, ValueInfo \*\*valueInfoItems, uint64\_t \*itemNum\);

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|prefix|const char *|入参|要匹配key的前缀。|
|valueInfoItems|ValueInfo **|出参|用于填充匹配到的key、value数据。|
|itemNum|uint64_t *|出参|匹配到的key、value个数。|

**表 2**  详细参数说明

|类型|类型定义|说明|
|--|--|--|
|GetItems|typedef struct {char \*key;char \*value;uint64_t length;} ValueInfo;|KEY/VALUE描述信息结构体，用于存储本次读取到的数据信息。|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|读取成功。|
|其它|读取失败。|

## CResult MmsGetValuesByRange

>[!TIP] 须知
>范围查询会把所有匹配到的数据全部返回给上层，如果匹配到的数据量过大，可能会把设备上的物理内存耗尽，从而导致oom，上层应用应该保证查询的数据量在可接受的内存范围内。

**函数定义**

MMS范围查询接口，返回与指定区间范围匹配的所有的key及其value。

**实现方法**

CResult MmsGetValuesByRange\(const char \*start, const char \*end, ValueInfo \*\*valueInfoItems, uint64\_t \*itemNum\);

**参数说明**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|start|const char *|入参|要匹配key的范围的开始(包含start)|
|end|const char *|入参|要匹配key的范围的结束(包含end)，start <= end|
|valueInfoItems|ValueInfo **|出参|用于填充匹配到的key、value数据|
|itemNum|uint64_t *|出参|匹配到的key、value个数|

**表 2**  详细参数说明

|类型|类型定义|说明|
|--|--|--|
|GetItems|typedef struct {char \*key;char \*value;uint64_t length;} ValueInfo;|KEY/VALUE描述信息结构体，用于存储本次读取到的数据信息|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|读取成功。|
|其它|读取失败。|

## CResult MmsBatchDeleteByRange

**函数定义**

MMS范围删除接口，删除所有与指定范围匹配的所有的key及其value。

**实现方法**

CResult MmsBatchDeleteByRange\(const char \*start, const char \*end\);

**参数说明**

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|start|const char *|入参|要匹配key的范围的开始(包含start)。|
|end|const char *|入参|要匹配key的范围的结束(包含end), start <= end。|

**返回值**

|返回值|描述|
|--|--|
|RET_MMS_OK|删除成功。|
|其它|删除失败。|

## void MmsFreeResources

**函数定义**

MMS内存释放函数，用于释放前缀查询、范围查询的内存。

**实现方法**

void MmsFreeResources\(ValueInfo \*\*valueInfoItems, uint64\_t itemNum\);

**参数说明**

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|valueInfoItems|ValueInfo **|入参|前缀查询、范围查询结果的内存指针|
|itemNum|uint64_t|入参|前缀查询、范围查询结果的个数|

**返回值**

无返回值
