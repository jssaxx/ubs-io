# 前言<a name="ZH-CN_TOPIC_0000002521860672"></a>

**概述<a name="section4537382116410"></a>**

本文档描述了UBS IO对外提供的API接口信息，包括API接口参数解释和使用样例等。

**读者对象<a name="section4378592816410"></a>**

本文档主要适用于以下工程师：

-   技术支持工程师
-   二次开发工程师
-   维护工程师

**符号约定<a name="section133020216410"></a>**

在本文中可能出现下列标志，它们所代表的含义如下。

|**符号**|**说明**|
|--|--|
|![](figures/zh-cn_image_0000002521860688.png)|表示如不避免则将会导致死亡或严重伤害的具有高等级风险的危害。|
|![](figures/zh-cn_image_0000002521860686.png)|表示如不避免则可能导致死亡或严重伤害的具有中等级风险的危害。|
|![](figures/zh-cn_image_0000002552860651.png)|表示如不避免则可能导致轻微或中度伤害的具有低等级风险的危害。|
|![](figures/zh-cn_image_0000002521700692.png)|用于传递设备或环境安全警示信息。如不避免则可能会导致设备损坏、数据丢失、设备性能降低或其它不可预知的结果。“须知”不涉及人身伤害。|
|![](figures/zh-cn_image_0000002552740643.png)|对正文中重点信息的补充说明。“说明”不是安全警示信息，不涉及人身、设备及环境伤害信息。|


**修改记录<a name="section2467512116410"></a>**

|**文档版本**|**发布日期**|**修改说明**|
|--|--|--|
|01|2026-03-30|第一次正式发布。|


# 介绍<a name="ZH-CN_TOPIC_0000002521700648"></a>

本文主要介绍UBS IO对外提供的API接口。

-   编程语言

    UBS IO主体使用C/C++语言开发，对外提供C API。

-   功能架构

    UBS IO结合JuiceFS实现计算侧分布式缓存，降低IO读写时延，提升端到端的整体性能。

-   API参考

    介绍应用开发过程中最常用和基础的API，建议使用UBS IO的开发者对这些API都有所了解。

-   错误码

    介绍UBS IO的错误码名称、取值及部分常见错误码的处理方法。

# API参考<a name="ZH-CN_TOPIC_0000002552740629"></a>

## BioInitialize<a name="ZH-CN_TOPIC_0000002521860640"></a>

**函数定义<a name="section4292195611128"></a>**

UBS IO初始化接口，使用者根据实际应用场景选择合适的工作模式来初始化UBS IO读写缓存服务。

**实现方法<a name="section144356817161"></a>**

CResult BioInitialize\(WorkerMode mode, ClientOptionsConfig \*optConf\)

**参数说明<a name="section8984192751117"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|mode|WorkerMode|入参|工作模式，有以下两种类型：CONVERGENCE(0)：融合模式，适用于AI场景。SEPARATES(1)：分离模式，适用于大数据场景。|
|optConf|ClientOptionsConfig*|入参|UBS IO初始化选项，详细解释参见表2。|


**表 2**  详细参数说明

|参数|结构体字段|说明|
|--|--|--|
|ClientOptionsConfig|LogType logType|日志类型。STDOUT_TYPE(0)标准流输出。FILE_TYPE(1)日志文件输出。STDERR_TYPE(2)标准错误流输出。仅分离模式下生效。|
|char logFilePath[PATH_MAX]|日志文件输出路径，仅分离模式下生效，使用者需要保证传入的日志路径可访问。|
|uint8_t enable|安全开关。0：表示关闭。非0：表示打开。|
|char certificationPath[PATH_MAX]|Client证书路径，安全使能时要求路径有效。|
|char caCerPath[PATH_MAX]|CA证书路径，安全使能时要求路径有效。|
|char caCrlPath[PATH_MAX]|吊销证书列表文件路径，可选。|
|char privateKeyPath[PATH_MAX]|Client证书私钥路径，安全使能时要求路径有效。|
|char privateKeyPassword[PATH_MAX]|Client证书私钥口令密文的文件路径，安全使能时要求路径有效。|
|char decrypterLibPath[PATH_MAX]|Client证书解密函数so文件路径，安全使能时要求路径有效。|
|char opensslLibDir[PATH_MAX]|Client端openssl, crypto的so目录路径，可选，为空时使用默认版本，安全使能时要求路径有效。|


**返回值<a name="section1891215448215"></a>**

**表 3**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_ERROR|操作失败。|


## BioExit<a name="ZH-CN_TOPIC_0000002552860615"></a>

**函数定义<a name="section4349141610276"></a>**

UBS IO退出接口，使用者调用该接口退出UBS IO读写缓存服务。

**实现方法<a name="section11350171616279"></a>**

void BioExit\(\)

**参数说明<a name="section13350116182717"></a>**

无入参。

**返回值<a name="section1891215448215"></a>**

无返回值。

## BioCreateCache<a name="ZH-CN_TOPIC_0000002521860668"></a>

**函数定义<a name="section4349141610276"></a>**

创建Cache实例接口，使用者根据实际应用场景选择合适的缓存策略和数据亲和策略来创建UBS IO Cache实例。

**实现方法<a name="section11350171616279"></a>**

CResult BioCreateCache\(CacheDescriptor desc\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|desc|CacheDescriptor|入参|实例参数描述：uint64_t tenantId;租户ID。AffinityStrategy affinity;数据亲和策略：LOCAL_AFFINITY(1)：本地亲和。GLOBAL_BALANCE(2)：全局均衡。WriteStrategy strategy;缓存策略：WRITE_BACK(1)：回写模式。WRITE_THROUGH(2)：透写模式。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_EXISTS|Cache实例已存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NO_SPACE|缓存空间不足，最大缓存数量为1024个。|


## BioGetCache<a name="ZH-CN_TOPIC_0000002521860662"></a>

**函数定义<a name="section4349141610276"></a>**

获取Cache实例接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioGetCache\(uint64\_t tenantId, CacheDescriptor \*desc\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|desc|CacheDescriptor*|出参|实例参数描述：uint64_t tenantId;租户ID。AffinityStrategy affinity;数据亲和策略：LOCAL_AFFINITY(1)：本地亲和。GLOBAL_BALANCE(2)：全局均衡。WriteStrategy strategy;缓存策略：WRITE_BACK(1)：回写模式。WRITE_THROUGH(2)：透写模式。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|


## BioDestroyCache<a name="ZH-CN_TOPIC_0000002521700654"></a>

**函数定义<a name="section4349141610276"></a>**

销毁Cache实例接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioDestroyCache\(uint64\_t tenantId\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|


## BioCalcLocation<a name="ZH-CN_TOPIC_0000002552860623"></a>

**函数定义<a name="section4349141610276"></a>**

计算对象位置接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioCalcLocation\(uint64\_t tenantId, uint64\_t objectId, ObjLocation \*location\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|objectId|uint64_t|入参|对象ID。|
|location|ObjLocation*|出参|对象位置，由两个uint64_t组成，对使用者透明。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_PT_FAULT|分区错误，对象位置无法写入。|


## BioPut<a name="ZH-CN_TOPIC_0000002521860654"></a>

**函数定义<a name="section4349141610276"></a>**

对象写入接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioPut\(uint64\_t tenantId, const char \*key, const char \*value, uint64\_t length, ObjLocation location\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|key|const char*|入参|对象的key。|
|value|const char*|入参|待写入的数据buffer，value空间的长度必须和入参length一致。|
|length|uint64_t|入参|写入数据长度。|
|location|ObjLocation|入参|对象位置。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_PT_FAULT|分区错误，对象位置无法写入。|
|RET_CACHE_ERROR|操作失败。|


## BioGet<a name="ZH-CN_TOPIC_0000002552860633"></a>

**函数定义<a name="section4349141610276"></a>**

对象读取接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioGet\(uint64\_t tenantId, const char \*key, uint64\_t offset, uint64\_t length, ObjLocation location, char \*value, uint64\_t \*realLength\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|key|const char*|入参|对象的key。|
|offset|uint64_t|入参|待读取数据偏移。|
|length|uint64_t|入参|读取的数据长度。|
|location|ObjLocation|入参|对象位置。|
|value|char*|入参|待读取的数据buffer。|
|realLength|uint64_t*|出参|实际读取的数据长度。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_PT_FAULT|分区错误，对象位置无法写入。|
|RET_CACHE_READ_EXCEED|读取数据长度超过写入长度。|
|RET_CACHE_ERROR|操作失败。|


## BioDelete<a name="ZH-CN_TOPIC_0000002552740619"></a>

**函数定义<a name="section4349141610276"></a>**

对象删除接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioDelete\(uint64\_t tenantId, const char \*key, ObjLocation location\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|key|const char*|入参|对象的key。|
|location|ObjLocation|入参|对象位置。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_PT_FAULT|分区错误，对象位置无法写入。|
|RET_CACHE_ERROR|操作失败。|


## BioLoad<a name="ZH-CN_TOPIC_0000002521860644"></a>

**函数定义<a name="section4349141610276"></a>**

对象加载接口，该接口是异步接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioLoad\(uint64\_t tenantId, const char \*key, uint64\_t offset, uint64\_t length, ObjLocation location, BioLoadCallback callback, void \*context\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|key|const char*|入参|对象的key。|
|offset|uint64_t|入参|待加载数据的偏移。|
|length|uint64_t|入参|待加载的数据长度。|
|location|ObjLocation|入参|对象位置。|
|callback|BioLoadCallback|入参|异步回调函数。|
|context|void*|入参|回调上下文。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_PT_FAULT|分区错误，对象位置无法写入。|
|RET_CACHE_ERROR|操作失败。|


## BioListAll<a name="ZH-CN_TOPIC_0000002521860650"></a>

**函数定义<a name="section4349141610276"></a>**

对象列举接口，要求使用者调用BioFreeListResources接口释放列举结果的内存资源。

**实现方法<a name="section11350171616279"></a>**

CResult BioListAll\(uint64\_t tenantId, const char \*prefix, ObjStat \*\*objs, uint64\_t \*objNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|prefix|const char*|入参|待匹配的对象前缀。|
|objs|ObjStat**|出参|列举对象结果。|
|objNum|uint64_t*|出参|列举对象的数量。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_NO_SPACE|内存空间不足。|
|RET_CACHE_ERROR|操作失败。|


## BioFreeListResources<a name="ZH-CN_TOPIC_0000002552740623"></a>

**函数定义<a name="section4349141610276"></a>**

释放列举对象结果内存资源接口。

**实现方法<a name="section11350171616279"></a>**

void BioFreeListResources\(ObjStat \*\*objs, uint64\_t objNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|objs|ObjStat**|入参|列举对象结果。|
|objNum|uint64_t|入参|列举对象的数量。|


**返回值<a name="section1891215448215"></a>**

无返回值。

## BioStat<a name="ZH-CN_TOPIC_0000002552740607"></a>

**函数定义<a name="section4349141610276"></a>**

查询对象信息接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioStat\(uint64\_t tenantId, const char \*key, ObjLocation location, ObjStat \*stat\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|key|const char*|入参|对象的key。|
|location|ObjLocation|入参|对象位置。|
|stat|ObjStat*|出参|对象信息。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_PT_FAULT|分区错误，对象位置无法写入。|
|RET_CACHE_ERROR|操作失败。|


## BioNotifyUpgradePrepare<a name="ZH-CN_TOPIC_0000002552860619"></a>

**函数定义<a name="section4349141610276"></a>**

通知升级准备接口，使用者调用该接口后将停止UBS IO读写缓存服务，缓存数据逐渐淘汰到后端存储，后续前台IO直接写入后端存储中。

**实现方法<a name="section11350171616279"></a>**

CResult BioNotifyUpgradePrepare\(uint64\_t tenantId\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_ERROR|操作失败。|


## BioNotifyUpgradeFinish<a name="ZH-CN_TOPIC_0000002552740597"></a>

**函数定义<a name="section4349141610276"></a>**

通知升级完成接口，使用者调用该接口后将重启UBS IO读写缓存服务，后续前台IO直接写入后端缓存中。

**实现方法<a name="section11350171616279"></a>**

CResult BioNotifyUpgradeFinish\(uint64\_t tenantId\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_ERROR|操作失败。|


## BioCheckUpgradeReady<a name="ZH-CN_TOPIC_0000002552860627"></a>

**函数定义<a name="section4349141610276"></a>**

检查升级就绪接口。使用者调用该接口返回成功则表示允许进行离线升级；返回失败则需要延时等待后再次检查。

**实现方法<a name="section11350171616279"></a>**

CResult BioCheckUpgradeReady\(uint64\_t tenantId\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_ERROR|操作失败。|


## BioAllocCacheSpace<a name="ZH-CN_TOPIC_0000002521700666"></a>

**函数定义<a name="section4349141610276"></a>**

申请缓存空间接口，该接口配合免拷贝写使用。

**实现方法<a name="section11350171616279"></a>**

CResult BioAllocCacheSpace\(uint64\_t tenantId, uint64\_t objectId, uint64\_t length, CacheSpaceDesc \*space\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|objectId|uint64_t|入参|对象ID。|
|length|uint64_t|入参|待申请缓存空间长度，最大值为4,194,304（4M）。|
|space|CacheSpaceDesc*|出参|缓存空间信息描述：uint8_t allocLoc;申请标记。uint16_t addressNum;地址数量。uint16_t descriptorSize;缓存空间描述长度。ObjLocation loc;缓存空间位置。CacheAddress address[CACHE_SPACE_ADDRESS_SIZE];缓存空间地址信息：uint64_t address;缓存地址。uint32_t size;缓存长度。char descriptorInfo[CACHE_SPACE_DESC_SIZE];缓存空间描述信息。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_PT_FAULT|分区错误，对象位置无法写入。|
|RET_CACHE_ERROR|操作失败。|


## BioPutWithCopyFree<a name="ZH-CN_TOPIC_0000002552740611"></a>

**函数定义<a name="section4349141610276"></a>**

对象免拷贝写入接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioPutWithCopyFree\(uint64\_t tenantId, const char \*key, CacheSpaceDesc \*space\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tenantId|uint64_t|入参|租户ID。|
|key|const char *|入参|对象的key。|
|space|CacheSpaceDesc*|入参|调用BioAllocCacheSpace成功后返回的缓存空间，所有CacheAddress address[CACHE_SPACE_ADDRESS_SIZE]中的size和最大值为4,194,304（4M）。缓存空间信息描述：uint8_t allocLoc;申请标记。uint16_t addressNum;地址数量。uint16_t descriptorSize;缓存空间描述长度。ObjLocation loc;缓存空间位置。CacheAddress address[CACHE_SPACE_ADDRESS_SIZE];缓存空间地址信息：uint64_t address;缓存地址。uint32_t size;缓存长度。char descriptorInfo[CACHE_SPACE_DEC_SIZE];缓存空间描述信息。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_PT_FAULT|分区错误，对象位置无法写入。|
|RET_CACHE_ERROR|操作失败。|


## BioReadHook<a name="ZH-CN_TOPIC_0000002521700662"></a>

**函数定义<a name="section4349141610276"></a>**

拦截读接口。

**实现方法<a name="section11350171616279"></a>**

int BioReadHook\(uint64\_t inode, char \*buff, uint64\_t count, uint64\_t offset, int \*readLen\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|inode|uint64_t|入参|文件inode。|
|buff|char*|入参|待读取数据buffer。|
|count|uint64_t|入参|待读取的数据长度。|
|offset|uint64_t|入参|待读取的数据偏移。|
|readLen|int*|出参|实际读取数据长度。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。|


## BioWriteHook<a name="ZH-CN_TOPIC_0000002552740601"></a>

**函数定义<a name="section4349141610276"></a>**

劫持写接口。

**实现方法<a name="section11350171616279"></a>**

int BioWriteHook\(uint64\_t inode, char \*buff, uint64\_t count, uint64\_t offset, uint64\_t fh\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|inode|uint64_t|入参|文件inode。|
|buff|char*|入参|待写入的数据。|
|count|uint64_t|入参|待写入数据的长度。|
|offset|uint64_t|入参|待写入数据的偏移。|
|fh|uint64_t|入参|文件描述符。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。|


## BioWriteCopyFreeHook<a name="ZH-CN_TOPIC_0000002521700676"></a>

**函数定义<a name="section4349141610276"></a>**

劫持免拷贝写接口。

**实现方法<a name="section11350171616279"></a>**

int BioWriteCopyFreeHook\(uint64\_t inode, uint64\_t offset, uint64\_t count, CacheSpaceDesc \*space\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|inode|uint64_t|入参|文件inode。|
|offset|uint64_t|入参|待写入数据的偏移。|
|count|uint64_t|入参|待写入数据的长度。|
|space|CacheSpaceDesc*|入参|调用BioAllocCacheSpace成功后返回的缓存空间。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|0|操作成功。|
|非0|操作失败。|


## BioRegisterInterceptorRead<a name="ZH-CN_TOPIC_0000002521700658"></a>

**函数定义<a name="section4349141610276"></a>**

注册拦截读接口。

**实现方法<a name="section11350171616279"></a>**

void BioRegisterInterceptorRead\(ReadHook rh\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|rh|ReadHook|入参|读钩子函数。|


**返回值<a name="section1891215448215"></a>**

无返回值。

## BioRegisterInterceptorWrite<a name="ZH-CN_TOPIC_0000002552860605"></a>

**函数定义<a name="section4349141610276"></a>**

注册劫持写接口。

**实现方法<a name="section11350171616279"></a>**

void BioRegisterInterceptorWrite\(WriteHook wh\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|wh|WriteHook|入参|写钩子函数。|


**返回值<a name="section1891215448215"></a>**

无返回值。

## BioRegisterInterceptorWriteCopyFree<a name="ZH-CN_TOPIC_0000002521700644"></a>

**函数定义<a name="section4349141610276"></a>**

注册劫持免拷贝写接口。

**实现方法<a name="section11350171616279"></a>**

void BioRegisterInterceptorWriteCopyFree\(WriteCopyFreeHook wh\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|wh|WriteCopyFreeHook|入参|免拷贝写钩子函数。|


**返回值<a name="section1891215448215"></a>**

无返回值。

## BioConvertLocation<a name="ZH-CN_TOPIC_0000002552860601"></a>

**函数定义<a name="section4349141610276"></a>**

转换对象位置描述接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioConvertLocation\(ObjLocation location, ObjLocationDetail \*detailLoc\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|location|ObjLocation|入参|原始对象位置。|
|detailLoc|ObjLocationDetail*|出参|对象位置详细描述的结构参数解释参见表2。|


**表 2**  详细参数说明

|参数|结构体字段|说明|
|--|--|--|
|ObjLocationDetail|hostMaster|主副本的主机名数组。|
|hostSlave|从副本的主机名数组。|
|portMaster|主副本主机端口。|
|portSlave|从副本主机端口。|


**返回值<a name="section1891215448215"></a>**

**表 3**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_ERROR|操作失败。|


## BioShowCacheResource<a name="ZH-CN_TOPIC_0000002521700680"></a>

**函数定义<a name="section4349141610276"></a>**

查询系统缓存资源使用情况接口后，需要使用者调用BioFreeCacheResourcePtr接口释放查询结果的内存资源。

**实现方法<a name="section11350171616279"></a>**

CResult BioShowCacheResource\(CacheResourcesDesc \*\*nodeDesc, uint64\_t \*nodeNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|nodeDesc|CacheResourcesDesc**|出参|节点缓存资源使用情况，参数解释参见表2 详细参数说明。|
|nodeNum|uint64_t*|出参|节点数量。|


**表 2**  详细参数说明

|参数|结构体字段|说明|
|--|--|--|
|CacheResourcesDesc|nodeId|节点ID。|
|rCacheMemCapacity|读缓存内存容量。|
|rCacheDiskCapacity|读缓存磁盘容量。|
|wCacheMemCapacity|写缓存内存容量。|
|wCacheDiskCapacity|写缓存磁盘容量。|
|rCacheMemUsedSize|读缓存内存使用情况。|
|rCacheDiskUsedSize|读缓存磁盘使用情况。|
|wCacheMemUsedSize|写缓存内存使用情况。|
|wCacheDiskUsedSize|写缓存磁盘使用情况。|


**返回值<a name="section1891215448215"></a>**

**表 3**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_READY|TurboIO服务未就绪。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_NO_SPACE|空间不足。|
|RET_CACHE_ERROR|操作失败。|


## BioFreeCacheResourcePtr<a name="ZH-CN_TOPIC_0000002521860658"></a>

**函数定义<a name="section4349141610276"></a>**

释放缓存资源的内存接口。

**实现方法<a name="section11350171616279"></a>**

void BioFreeCacheResourcePtr\(CacheResourcesDesc \*\*nodeDesc, uint64\_t nodeNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|nodeDesc|CacheResourcesDesc**|入参|节点缓存资源使用情况。|
|nodeNum|uint64_t|入参|节点数量。|


**返回值<a name="section1891215448215"></a>**

无返回值。

## BioShowCacheHitRatio<a name="ZH-CN_TOPIC_0000002521700670"></a>

**函数定义<a name="section4349141610276"></a>**

查询系统缓存命中率接口，查询结果的内存资源要求使用者调用BioFreeCacheHitPtr接口进行释放。

**实现方法<a name="section11350171616279"></a>**

CResult BioShowCacheHitRatio\(CacheHitFinalDesc \*desc, CacheHitFinalDesc \*\*nodeDesc, uint64\_t \*nodeNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|desc|CacheHitFinalDesc*|出参|所有节点缓存命中率。参数解释参见表2 详细参数说明。|
|nodeDesc|CacheHitFinalDesc**|出参|各节点的缓存命中率。|
|nodeNum|uint64_t*|出参|节点数量。|


**表 2**  详细参数说明

|参数|结构体字段|说明|
|--|--|--|
|CacheHitFinalDesc|nodeId|节点ID。当所有节点命中率为参数时，该字段为无效信息。|
|rCacheHitMemCount|读缓存内存命中数。|
|rCacheHitDiskCount|读缓存磁盘命中数。|
|rCacheHitCount|读缓存命中总数。|
|rCacheTotalCount|读缓存查询总数。|
|wCacheHitMemCount|写缓存内存命中数。|
|wCacheHitDiskCount|写缓存磁盘命中数。|
|wCacheHitCount|写缓存命中总数。|
|wCacheTotalCount|写缓存查询总数。|
|backendHitCount|后端命中数。|


**返回值<a name="section1891215448215"></a>**

**表 3**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|RET_CACHE_NO_SPACE|空间不足。|
|RET_CACHE_ERROR|操作失败。|


## BioFreeCacheHitPtr<a name="ZH-CN_TOPIC_0000002552740615"></a>

**函数定义<a name="section4349141610276"></a>**

释放缓存命中率相关的内存资源。

**实现方法<a name="section11350171616279"></a>**

void BioFreeCacheHitPtr\(CacheHitFinalDesc \*\*nodeDesc, uint64\_t nodeNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|nodeDesc|CacheHitFinalDesc**|入参|各节点缓存命中率。|
|nodeNum|uint64_t|入参|节点数量。|


**返回值<a name="section1891215448215"></a>**

无返回值。

## BioAddDisk<a name="ZH-CN_TOPIC_0000002552860637"></a>

**函数定义<a name="section4349141610276"></a>**

新增加盘接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioAddDisk\(const char \*diskPath\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|diskPath|const char *|入参|新增块设备路径，必须是有效的路径。|


**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

|返回值|描述|
|--|--|
|RET_CACHE_OK|操作成功。|
|RET_CACHE_EPERM|传入参数错误。|
|RET_CACHE_NOT_READY|UBS IO服务未就绪。|
|RET_CACHE_PT_FAULT|分区故障。|
|RET_CACHE_NEED_RETRY|需要外部重试。|
|RET_CACHE_ERROR|操作失败。|


# 错误码<a name="ZH-CN_TOPIC_0000002552860609"></a>

**表 1**  错误码

|错误码数|错误码|含义|
|--|--|--|
|0|RET_CACHE_OK|操作成功。|
|1|RET_CACHE_PROTECTED|写缓存保护。|
|2|RET_CACHE_ERROR|操作失败。|
|3|RET_CACHE_EPERM|传入参数错误。|
|4|RET_CACHE_BUSY|缓存忙碌，需要外部重试。|
|5|RET_CACHE_NEED_RETRY|需要外部重试。|
|6|RET_CACHE_NOT_READY|缓存未就绪。|
|7|RET_CACHE_NOT_FOUND|Cache实例不存在。|
|8|RET_CACHE_CONFLICT|对象冲突。|
|9|RET_CACHE_MISS|缓存未命中。|
|10|RET_CACHE_NO_SPACE|空间不足。|
|11|RET_CACHE_UNAVAILABLE|缓存服务不可用。|
|12|RET_CACHE_EXCEED_QUOTA|超出配额限制。|
|13|RET_CACHE_PT_FAULT|分区故障。|
|14|RET_CACHE_READ_EXCEED|超出读取限制。|
|15|RET_CACHE_EXISTS|Cache实例已存在。|
|16|RET_CACHE_DISK_FAULT|磁盘故障。|
|17|RET_CACHE_UFS_FAULT|后端存储故障。|


# 版权说明<a name="ZH-CN_TOPIC_0000002553669131"></a>

Copyright \(c\) Huawei Technologies Co., Ltd. 2025. All rights reserved.
