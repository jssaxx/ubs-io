# API参考

## 介绍

本文档描述了UBS IO对外提供的API接口信息，包括API接口参数解释和使用样例等。

- 编程语言

    UBS IO主体使用C/C++语言开发，对外提供C API。

- 功能架构

    UBS IO结合JuiceFS实现计算侧分布式缓存，降低IO读写时延，提升端到端的整体性能。

- API参考

    介绍应用开发过程中最常用和基础的API，建议使用UBS IO的开发者对这些API都有所了解。

- 错误码

    介绍UBS IO的错误码名称、取值及部分常见错误码的处理方法。

## BioInitialize

**函数定义**

UBS IO初始化接口，使用者根据实际应用场景选择合适的工作模式来初始化UBS IO读写缓存服务。

**实现方法**

CResult BioInitialize\(WorkerMode mode, ClientOptionsConfig \*optConf\)

**参数说明**

**表 1 参数说明**

<table><thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>mode</td>
    <td>WorkerMode</td>
    <td>入参</td>
    <td>工作模式，有以下两种类型：<br>CONVERGENCE(0)：融合模式，适用于AI场景。<br>SEPARATES(1)：分离模式，适用于大数据场景。</td>
  </tr>
  <tr>
    <td>optConf</td>
    <td>ClientOptionsConfig*</td>
    <td>入参</td>
    <td>UBS IO初始化选项，详细解释参见<a href="#详细参数说明1">表2</a>。</td>
  </tr>
</tbody>
</table>

**表 2  详细参数说明**<a id="详细参数说明1"></a>

<table><thead>
  <tr>
    <th>参数</th>
    <th>结构体字段</th>
    <th>说明</th>
  </tr></thead>
<tbody>
  <tr>
    <td rowspan="10">ClientOptionsConfig</td>
    <td>LogType logType</td>
    <td>日志类型。<br>STDOUT_TYPE(0)标准流输出。<br>FILE_TYPE(1)日志文件输出。<br>STDERR_TYPE(2)标准错误流输出。<br>仅分离模式下生效。</td>
  </tr>
  <tr>
    <td>char logFilePath[PATH_MAX]</td>
    <td>日志文件输出路径，仅分离模式下生效，使用者需要保证传入的日志路径可访问。</td>
  </tr>
  <tr>
    <td>uint8_t enable</td>
    <td>安全开关。<br>0：表示关闭。<br>非0：表示打开。</td>
  </tr>
  <tr>
    <td>char certificationPath[PATH_MAX]</td>
    <td>Client证书路径，安全使能时要求路径有效。</td>
  </tr>
  <tr>
    <td>char caCerPath[PATH_MAX]</td>
    <td>CA证书路径，安全使能时要求路径有效。</td>
  </tr>
  <tr>
    <td>char caCrlPath[PATH_MAX]</td>
    <td>吊销证书列表文件路径，可选。安全使能时要求路径有效。</td>
  </tr>
  <tr>
    <td>char privateKeyPath[PATH_MAX]</td>
    <td>Client证书私钥路径，安全使能时要求路径有效。</td>
  </tr>
  <tr>
    <td>char privateKeyPassword[PATH_MAX]</td>
    <td>Client证书私钥口令密文的文件路径，可以为空，为空时需要提供未加密的私钥路径。不为空时安全使能时要求路径有效。</td>
  </tr>
  <tr>
    <td>char decrypterLibPath[PATH_MAX]</td>
    <td>Client证书解密函数so文件路径，可以为空，为空时需要提供明文口令。不为空时安全使能时要求路径有效。</td>
  </tr>
  <tr>
    <td>char opensslLibDir[PATH_MAX]</td>
    <td>Client端openssl, crypto的so目录路径，可选，为空时使用默认版本，安全使能时要求路径有效。</td>
  </tr>
</tbody></table>

**返回值**

**表 3  返回值说明**

<table style="undefined;table-layout: fixed; width: 358px"><colgroup>
<col style="width: 207px">
<col style="width: 151px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioExit

**函数定义**

UBS IO退出接口，使用者调用该接口退出UBS IO读写缓存服务。

**实现方法**

void BioExit\(\)

**参数说明**

无入参。

**返回值**

无返回值。

## BioCreateCache

**函数定义**

创建Cache实例接口，使用者根据实际应用场景选择合适的缓存策略和数据亲和策略来创建UBS IO Cache实例。

**实现方法**

CResult BioCreateCache\(CacheDescriptor desc\)

**参数说明**

**表 1  参数说明**

<table><thead><tr><th><p>参数名</p>
</th>
<th><p>数据类型</p>
</th>
<th><p>参数类型</p>
</th>
<th><p>描述</p>
</th>
</tr>
</thead>
<tbody><tr><td><p>desc</p>
</td>
<td><p>CacheDescriptor</p>
</td>
<td><p>入参</p>
</td>
<td><p>实例参数描述：</p>
<ul><li>uint64_t tenantId;<p>租户ID。</p>
</li><li>AffinityStrategy affinity;<p>数据亲和策略：</p>
<ul><li>LOCAL_AFFINITY(1)：本地亲和。</li><li>GLOBAL_BALANCE(2)：全局均衡。</li></ul>
</li><li>WriteStrategy strategy;<div>缓存策略：<ul><li>WRITE_BACK(1)：回写模式。</li><li>WRITE_THROUGH(2)：透写模式。</li></ul>
</div>
</li></ul>
</td>
</tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 540px"><colgroup>
<col style="width: 276px">
<col style="width: 264px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EXISTS</td>
    <td>Cache实例已存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NO_SPACE</td>
    <td>缓存空间不足，最大缓存数量为1024个。</td>
  </tr>
</tbody>
</table>

## BioGetCache

**函数定义**

获取Cache实例接口。

**实现方法**

CResult BioGetCache\(uint64\_t tenantId, CacheDescriptor \*desc\)

**参数说明**

**表 1  参数说明**

<table><thead><tr><th><p>参数名</p>
</th>
<th><p>数据类型</p>
</th>
<th><p>参数类型</p>
</th>
<th><p>描述</p>
</th>
</tr>
</thead>
<tbody><tr><td><p>tenantId</p>
</td>
<td><p>uint64_t</p>
</td>
<td><p>入参</p>
</td>
<td><p>租户ID。</p>
</td>
</tr>
<tr><td><p>desc</p>
</td>
<td><p>CacheDescriptor*</p>
</td>
<td><p>出参</p>
</td>
<td><p>实例参数描述：</p>
<ul><li>uint64_t tenantId;<p>租户ID。</p>
</li><li>AffinityStrategy affinity;<p>数据亲和策略：</p>
<ul><li>LOCAL_AFFINITY(1)：本地亲和。</li><li>GLOBAL_BALANCE(2)：全局均衡。</li></ul>
</li><li>WriteStrategy strategy;<div>缓存策略：<ul><li>WRITE_BACK(1)：回写模式。</li><li>WRITE_THROUGH(2)：透写模式。</li></ul>
</div>
</li></ul>
</td>
</tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 485px"><colgroup>
<col style="width: 272px">
<col style="width: 213px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
</tbody>
</table>

## BioDestroyCache

**函数定义**

销毁Cache实例接口。

**实现方法**

CResult BioDestroyCache\(uint64\_t tenantId\)

**参数说明**

**表 1  参数说明**

<table><thead><tr><th><p>参数名</p>
</th>
<th><p>数据类型</p>
</th>
<th><p>参数类型</p>
</th>
<th><p>描述</p>
</th>
</tr>
</thead>
<tbody><tr><td><p>tenantId</p>
</td>
<td><p>uint64_t</p>
</td>
<td><p>入参</p>
</td>
<td><p>租户ID。</p>
</td>
</tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 484px"><colgroup>
<col style="width: 271px">
<col style="width: 213px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
</tbody>
</table>

## BioCalcLocation

**函数定义**

计算对象位置接口。

**实现方法**

CResult BioCalcLocation\(uint64\_t tenantId, uint64\_t objectId, ObjLocation \*location\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 827px"><colgroup>
<col style="width: 196px">
<col style="width: 194px">
<col style="width: 132px">
<col style="width: 305px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
  <tr>
    <td>objectId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>对象ID。</td>
  </tr>
  <tr>
    <td>location</td>
    <td>ObjLocation*</td>
    <td>出参</td>
    <td>对象位置，由两个uint64_t组成，对使用者透明。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 650px"><colgroup>
<col style="width: 364px">
<col style="width: 286px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区错误，对象位置无法写入。</td>
  </tr>
</tbody>
</table>

## BioPut

**函数定义**

对象写入接口。

**实现方法**

CResult BioPut\(uint64\_t tenantId, const char \*key, const char \*value, uint64\_t length, ObjLocation location\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 828px"><colgroup>
<col style="width: 196px">
<col style="width: 194px">
<col style="width: 132px">
<col style="width: 306px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
  <tr>
    <td>key</td>
    <td>const char*</td>
    <td>入参</td>
    <td>对象的key。</td>
  </tr>
  <tr>
    <td>value</td>
    <td>const char*</td>
    <td>入参</td>
    <td>待写入的数据buffer，value空间的长度必须和入参length一致。</td>
  </tr>
  <tr>
    <td>length</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>写入数据长度。</td>
  </tr>
  <tr>
    <td>location</td>
    <td>ObjLocation</td>
    <td>入参</td>
    <td>对象位置。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 579px"><colgroup>
<col style="width: 324px">
<col style="width: 255px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区错误，对象位置无法写入。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioGet

**函数定义**

对象读取接口。

**实现方法**

CResult BioGet\(uint64\_t tenantId, const char \*key, uint64\_t offset, uint64\_t length, ObjLocation location, char \*value, uint64\_t \*realLength\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 658px"><colgroup>
<col style="width: 182px">
<col style="width: 180px">
<col style="width: 122px">
<col style="width: 174px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
  <tr>
    <td>key</td>
    <td>const char*</td>
    <td>入参</td>
    <td>对象的key。</td>
  </tr>
  <tr>
    <td>offset</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待读取数据偏移。</td>
  </tr>
  <tr>
    <td>length</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>读取的数据长度。</td>
  </tr>
  <tr>
    <td>location</td>
    <td>ObjLocation</td>
    <td>入参</td>
    <td>对象位置。</td>
  </tr>
  <tr>
    <td>value</td>
    <td>char*</td>
    <td>入参</td>
    <td>待读取的数据buffer。</td>
  </tr>
  <tr>
    <td>realLength</td>
    <td>uint64_t*</td>
    <td>出参</td>
    <td>实际读取的数据长度。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2 返回值说明**

<table style="undefined;table-layout: fixed; width: 579px"><colgroup>
<col style="width: 324px">
<col style="width: 255px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_READ_EXCEED</td>
    <td>读取数据长度超过写入长度。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioDelete

**函数定义**

对象删除接口。

**实现方法**

CResult BioDelete\(uint64\_t tenantId, const char \*key, ObjLocation location\)

**参数说明**

**表 1 参数说明**

<table style="undefined;table-layout: fixed; width: 773px"><colgroup>
<col style="width: 220px">
<col style="width: 209px">
<col style="width: 142px">
<col style="width: 202px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
  <tr>
    <td>key</td>
    <td>const char*</td>
    <td>入参</td>
    <td>对象的key。</td>
  </tr>
  <tr>
    <td>location</td>
    <td>ObjLocation</td>
    <td>入参</td>
    <td>对象位置。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 579px"><colgroup>
<col style="width: 324px">
<col style="width: 255px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区错误，对象位置无法写入。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioLoad

**函数定义**

对象加载接口，该接口是异步接口。

**实现方法**

CResult BioLoad\(uint64\_t tenantId, const char \*key, uint64\_t offset, uint64\_t length, ObjLocation location, BioLoadCallback callback, void \*context\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 757px"><colgroup>
<col style="width: 215px">
<col style="width: 205px">
<col style="width: 139px">
<col style="width: 198px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
  <tr>
    <td>key</td>
    <td>const char*</td>
    <td>入参</td>
    <td>对象的key。</td>
  </tr>
  <tr>
    <td>offset</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待加载数据的偏移。</td>
  </tr>
  <tr>
    <td>length</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待加载的数据长度。</td>
  </tr>
  <tr>
    <td>location</td>
    <td>ObjLocation</td>
    <td>入参</td>
    <td>对象位置。</td>
  </tr>
  <tr>
    <td>callback</td>
    <td>BioLoadCallback</td>
    <td>入参</td>
    <td>异步回调函数。</td>
  </tr>
  <tr>
    <td>context</td>
    <td>void*</td>
    <td>入参</td>
    <td>回调上下文。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2 返回值说明**

<table style="undefined;table-layout: fixed; width: 579px"><colgroup>
<col style="width: 324px">
<col style="width: 255px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区错误，对象位置无法写入。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioListAll

**函数定义**

对象列举接口，要求使用者调用BioFreeListResources接口释放列举结果的内存资源。

**实现方法**

CResult BioListAll\(uint64\_t tenantId, const char \*prefix, ObjStat \*\*objs, uint64\_t \*objNum\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 700px"><colgroup>
<col style="width: 199px">
<col style="width: 189px">
<col style="width: 129px">
<col style="width: 183px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
  <tr>
    <td>prefix</td>
    <td>const char*</td>
    <td>入参</td>
    <td>待匹配的对象前缀。</td>
  </tr>
  <tr>
    <td>objs</td>
    <td>ObjStat**</td>
    <td>出参</td>
    <td>列举对象结果。</td>
  </tr>
  <tr>
    <td>objNum</td>
    <td>uint64_t*</td>
    <td>出参</td>
    <td>列举对象的数量。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 633px"><colgroup>
<col style="width: 354px">
<col style="width: 279px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NO_SPACE</td>
    <td>内存空间不足。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioFreeListResources

**函数定义**

释放列举对象结果内存资源接口。

**实现方法**

void BioFreeListResources\(ObjStat \*\*objs, uint64\_t objNum\)

**参数说明**

**表 1 参数说明**

<table style="undefined;table-layout: fixed; width: 662px"><colgroup>
<col style="width: 188px">
<col style="width: 179px">
<col style="width: 122px">
<col style="width: 173px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>objs</td>
    <td>ObjStat**</td>
    <td>入参</td>
    <td>列举对象结果。</td>
  </tr>
  <tr>
    <td>objNum</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>列举对象的数量。</td>
  </tr>
</tbody>
</table>

**返回值**

无返回值。

## BioStat

**函数定义**

查询对象信息接口。

**实现方法**

CResult BioStat\(uint64\_t tenantId, const char \*key, ObjLocation location, ObjStat \*stat\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 672px"><colgroup>
<col style="width: 150px">
<col style="width: 212px">
<col style="width: 126px">
<col style="width: 121px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
  <tr>
    <td>key</td>
    <td>const char*</td>
    <td>入参</td>
    <td>对象的key。</td>
  </tr>
  <tr>
    <td>location</td>
    <td>ObjLocation</td>
    <td>入参</td>
    <td>对象位置。</td>
  </tr>
  <tr>
    <td>stat</td>
    <td>ObjStat*</td>
    <td>出参</td>
    <td>对象信息。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2 返回值说明**

<table style="undefined;table-layout: fixed; width: 632px"><colgroup>
<col style="width: 354px">
<col style="width: 278px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区错误，对象位置无法写入。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioNotifyUpgradePrepare

**函数定义**

通知升级准备接口，使用者调用该接口后将停止UBS IO读写缓存服务，缓存数据逐渐淘汰到后端存储，后续前台IO直接写入后端存储中。

**实现方法**

CResult BioNotifyUpgradePrepare\(uint64\_t tenantId\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 681px"><colgroup>
<col style="width: 150px">
<col style="width: 200px">
<col style="width: 119px">
<col style="width: 114px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 631px"><colgroup>
<col style="width: 353px">
<col style="width: 278px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioNotifyUpgradeFinish

**函数定义**

通知升级完成接口，使用者调用该接口后将重启UBS IO读写缓存服务，后续前台IO直接写入后端缓存中。

**实现方法**

CResult BioNotifyUpgradeFinish\(uint64\_t tenantId\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 681px"><colgroup>
<col style="width: 150px">
<col style="width: 150px">
<col style="width: 119px">
<col style="width: 114px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 468px"><colgroup>
<col style="width: 255px">
<col style="width: 213px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioCheckUpgradeReady

**函数定义**

检查升级就绪接口。使用者调用该接口返回成功则表示允许进行离线升级；返回失败则需要延时等待后再次检查。

**实现方法**

CResult BioCheckUpgradeReady\(uint64\_t tenantId\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 613px"><colgroup>
<col style="width: 171px">
<col style="width: 139px">
<col style="width: 132px">
<col style="width: 171px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 436px"><colgroup>
<col style="width: 248px">
<col style="width: 188px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioAllocCacheSpace

**函数定义**

申请缓存空间接口，该接口配合免拷贝写使用。

**实现方法**

CResult BioAllocCacheSpace\(uint64\_t tenantId, uint64\_t objectId, uint64\_t length, CacheSpaceDesc \*space\)

**参数说明**

**表 1  参数说明**

<table><thead><tr><th><p>参数名</p>
</th>
<th><p>数据类型</p>
</th>
<th><p>参数类型</p>
</th>
<th><p>描述</p>
</th>
</tr>
</thead>
<tbody><tr><td><p>tenantId</p>
</td>
<td><p>uint64_t</p>
</td>
<td><p>入参</p>
</td>
<td><p>租户ID。</p>
</td>
</tr>
<tr><td><p>objectId</p>
</td>
<td><p>uint64_t</p>
</td>
<td><p>入参</p>
</td>
<td><p>对象ID。</p>
</td>
</tr>
<tr><td><p>length</p>
</td>
<td><p>uint64_t</p>
</td>
<td><p>入参</p>
</td>
<td><p>待申请缓存空间长度，最大值为4,194,304（4M）。</p>
</td>
</tr>
<tr><td><p>space</p>
</td>
<td><p>CacheSpaceDesc*</p>
</td>
<td><p>出参</p>
</td>
<td><p>缓存空间信息描述：</p>
<ul><li>uint8_t allocLoc;<p>申请标记。</p>
</li><li>uint16_t addressNum;<p>地址数量。</p>
</li><li>uint16_t descriptorSize;<p>缓存空间描述长度。</p>
</li><li>ObjLocation loc;<p>缓存空间位置。</p>
</li><li>CacheAddress address[CACHE_SPACE_ADDRESS_SIZE];<p>缓存空间地址信息：</p>
<ul><li>uint64_t address;<p>缓存地址。</p></li></ul>
<ul><li>uint32_t size;<p>缓存长度。</p></li></ul>
</li><li>char descriptorInfo[CACHE_SPACE_DESC_SIZE];<p>缓存空间描述信息。</p>
</li></ul>
</td>
</tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 538px"><colgroup>
<col style="width: 271px">
<col style="width: 267px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区错误，对象位置无法写入。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioPutWithCopyFree

**函数定义**

对象免拷贝写入接口。

**实现方法**

CResult BioPutWithCopyFree\(uint64\_t tenantId, const char \*key, CacheSpaceDesc \*space\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 800px"><colgroup>
<col style="width: 62px">
<col style="width: 127px">
<col style="width: 67px">
<col style="width: 544px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>tenantId</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>租户ID。</td>
  </tr>
  <tr>
    <td>key</td>
    <td>const char *</td>
    <td>入参</td>
    <td>对象的key。</td>
  </tr>
  <tr>
    <td>space</td>
    <td>CacheSpaceDesc*</td>
    <td>入参</td>
    <td>
    <p>调用BioAllocCacheSpace成功后返回的缓存空间，所有CacheAddress address[CACHE_SPACE_ADDRESS_SIZE]中的size和最大值为4,194,304（4M）。</p>
    <p>缓存空间信息描述：</p>
    <ul><li>uint8_t allocLoc;<p>申请标记。</p>
    </li><li>uint16_t addressNum;<p>地址数量。</p>
    </li><li>uint16_t descriptorSize;<p>缓存空间描述长度。</p>
    </li><li>ObjLocation loc;<p>缓存空间位置。</p>
    </li><li>CacheAddress address[CACHE_SPACE_ADDRESS_SIZE];\
    </li>
    <ul><li>uint64_t address;<p>缓存地址。</p></li></ul>
    <ul><li>uint32_t size;<p>缓存长度。</p></li></ul>
    <li>char descriptorInfo[CACHE_SPACE_DESC_SIZE];<p>缓存空间描述信息。</p>
    </li></ul>
    </td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 596px"><colgroup>
<col style="width: 313px">
<col style="width: 283px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区错误，对象位置无法写入。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioReadHook

**函数定义**

拦截读接口。

**实现方法**

int BioReadHook\(uint64\_t inode, char \*buff, uint64\_t count, uint64\_t offset, int \*readLen\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 782px"><colgroup>
<col style="width: 148px">
<col style="width: 169px">
<col style="width: 159px">
<col style="width: 306px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>inode</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>文件inode。</td>
  </tr>
  <tr>
    <td>buff</td>
    <td>char*</td>
    <td>入参</td>
    <td>待读取数据buffer。</td>
  </tr>
  <tr>
    <td>count</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待读取的数据长度。</td>
  </tr>
  <tr>
    <td>offset</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待读取的数据偏移。</td>
  </tr>
  <tr>
    <td>readLen</td>
    <td>int*</td>
    <td>出参</td>
    <td>实际读取数据长度。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 425px"><colgroup>
<col style="width: 179px">
<col style="width: 246px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>0</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>非0</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioWriteHook

**函数定义**

劫持写接口。

**实现方法**

int BioWriteHook\(uint64\_t inode, char \*buff, uint64\_t count, uint64\_t offset, uint64\_t fh\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 694px"><colgroup>
<col style="width: 132px">
<col style="width: 151px">
<col style="width: 138px">
<col style="width: 273px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>inode</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>文件inode。</td>
  </tr>
  <tr>
    <td>buff</td>
    <td>char*</td>
    <td>入参</td>
    <td>待写入的数据。</td>
  </tr>
  <tr>
    <td>count</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待写入数据的长度。</td>
  </tr>
  <tr>
    <td>offset</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待写入数据的偏移。</td>
  </tr>
  <tr>
    <td>fh</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>文件描述符。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 324px"><colgroup>
<col style="width: 127px">
<col style="width: 197px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>0</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>非0</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioWriteCopyFreeHook

**函数定义**

劫持免拷贝写接口。

**实现方法**

int BioWriteCopyFreeHook\(uint64\_t inode, uint64\_t offset, uint64\_t count, CacheSpaceDesc \*space\)

**参数说明**

表 1  参数说明

<table style="undefined;table-layout: fixed; width: 761px"><colgroup>
<col style="width: 127px">
<col style="width: 197px">
<col style="width: 133px">
<col style="width: 304px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>inode</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>文件inode。</td>
  </tr>
  <tr>
    <td>offset</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待写入数据的偏移。</td>
  </tr>
  <tr>
    <td>count</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>待写入数据的长度。</td>
  </tr>
  <tr>
    <td>space</td>
    <td>CacheSpaceDesc*</td>
    <td>入参</td>
    <td>调用BioAllocCacheSpace成功后返回的缓存空间。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 2  返回值说明**

<table style="undefined;table-layout: fixed; width: 390px"><colgroup>
<col style="width: 145px">
<col style="width: 245px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>0</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>非0</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioRegisterInterceptorRead

**函数定义**

注册拦截读接口。

**实现方法**

void BioRegisterInterceptorRead\(ReadHook rh\)

**参数说明**

**表 1 参数说明**

<table style="undefined;table-layout: fixed; width: 572px"><colgroup>
<col style="width: 95px">
<col style="width: 125px">
<col style="width: 67px">
<col style="width: 285px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>rh</td>
    <td>ReadHook</td>
    <td>入参</td>
    <td>读钩子函数。</td>
  </tr>
</tbody>
</table>

**返回值**

无返回值。

## BioRegisterInterceptorWrite

**函数定义**

注册劫持写接口。

**实现方法**

void BioRegisterInterceptorWrite\(WriteHook wh\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 572px"><colgroup>
<col style="width: 95px">
<col style="width: 125px">
<col style="width: 67px">
<col style="width: 285px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>wh</td>
    <td>WriteHook</td>
    <td>入参</td>
    <td>写钩子函数。</td>
  </tr>
</tbody>
</table>

**返回值**

无返回值。

## BioRegisterInterceptorWriteCopyFree

**函数定义**

注册劫持免拷贝写接口。

**实现方法**

void BioRegisterInterceptorWriteCopyFree\(WriteCopyFreeHook wh\)

**参数说明**

表 1  参数说明

<table style="undefined;table-layout: fixed; width: 572px"><colgroup>
<col style="width: 95px">
<col style="width: 125px">
<col style="width: 67px">
<col style="width: 285px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>wh</td>
    <td>WriteCopyFreeHook</td>
    <td>入参</td>
    <td>免拷贝写钩子函数。</td>
  </tr>
</tbody>
</table>

**返回值**

无返回值。

## BioConvertLocation

**函数定义**

转换对象位置描述接口。

**实现方法**

CResult BioConvertLocation\(ObjLocation location, ObjLocationDetail \*detailLoc\)

**参数说明**

**表 1  参数说明**

<table style="undefined;table-layout: fixed; width: 572px"><colgroup>
<col style="width: 95px">
<col style="width: 125px">
<col style="width: 67px">
<col style="width: 285px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>location</td>
    <td>ObjLocation</td>
    <td>入参</td>
    <td>原始对象位置。</td>
  </tr>
  <tr>
    <td>detailLoc</td>
    <td>ObjLocationDetail*</td>
    <td>出参</td>
    <td>对象位置详细描述的结构参数解释参见<a href="#详细参数说明2">表2</a>。</td>
  </tr>
</tbody>
</table>

**表 2  详细参数说明**<a id="详细参数说明2"></a>

<table style="undefined;table-layout: fixed; width: 663px"><colgroup>
<col style="width: 173px">
<col style="width: 218px">
<col style="width: 272px">
</colgroup>
<thead>
  <tr>
    <th>参数</th>
    <th>结构体字段</th>
    <th>说明</th>
  </tr></thead>
<tbody>
  <tr>
    <td rowspan="4">ObjLocationDetail</td>
    <td>hostMaster</td>
    <td>主副本的主机名数组。</td>
  </tr>
  <tr>
    <td>hostSlave</td>
    <td>从副本的主机名数组。</td>
  </tr>
  <tr>
    <td>portMaster</td>
    <td>主副本主机端口。</td>
  </tr>
  <tr>
    <td>portSlave</td>
    <td>从副本主机端口。</td>
  </tr>
</tbody>
</table>

**返回值**

**表 3  返回值说明**

<table style="undefined;table-layout: fixed; width: 586px"><colgroup>
<col style="width: 341px">
<col style="width: 245px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioShowCacheResource

**函数定义**

查询系统缓存资源使用情况接口后，需要使用者调用BioFreeCacheResourcePtr接口释放查询结果的内存资源。

**实现方法**

CResult BioShowCacheResource\(CacheResourcesDesc \*\*nodeDesc, uint64\_t \*nodeNum\)

**参数说明**

**表 1  参数说明**
<table><thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>nodeDesc</td>
    <td>CacheResourcesDesc**</td>
    <td>出参</td>
    <td>节点缓存资源使用情况，参数解释参见<a href="#详细参数说明3">表2 详细参数说明</a>。</td>
  </tr>
  <tr>
    <td>nodeNum</td>
    <td>uint64_t*</td>
    <td>出参</td>
    <td>节点数量。</td>
  </tr>
</tbody>
</table>

**表 2  详细参数说明**<a id="详细参数说明3"></a>
<table style="undefined;table-layout: fixed; width: 716px"><colgroup>
<col style="width: 180px">
<col style="width: 241px">
<col style="width: 295px">
</colgroup>
<thead>
  <tr>
    <th>参数</th>
    <th>结构体字段</th>
    <th>说明</th>
  </tr></thead>
<tbody>
  <tr>
    <td rowspan="9">CacheResourcesDesc</td>
    <td>nodeId</td>
    <td>节点ID。</td>
  </tr>
  <tr>
    <td>rCacheMemCapacity</td>
    <td>读缓存内存容量。</td>
  </tr>
  <tr>
    <td>rCacheDiskCapacity</td>
    <td>读缓存磁盘容量。</td>
  </tr>
  <tr>
    <td>wCacheMemCapacity</td>
    <td>写缓存内存容量。</td>
  </tr>
  <tr>
    <td>wCacheDiskCapacity</td>
    <td>写缓存磁盘容量。</td>
  </tr>
  <tr>
    <td>rCacheMemUsedSize</td>
    <td>读缓存内存使用情况。</td>
  </tr>
  <tr>
    <td>rCacheDiskUsedSize</td>
    <td>读缓存磁盘使用情况。</td>
  </tr>
  <tr>
    <td>wCacheMemUsedSize</td>
    <td>写缓存内存使用情况。</td>
  </tr>
  <tr>
    <td>wCacheDiskUsedSize</td>
    <td>写缓存磁盘使用情况。</td>
  </tr>
</tbody>
</table>

**返回值**

表 3  返回值说明

<table style="undefined;table-layout: fixed; width: 525px"><colgroup>
<col style="width: 283px">
<col style="width: 242px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>TurboIO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NO_SPACE</td>
    <td>空间不足。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioFreeCacheResourcePtr

**函数定义**

释放缓存资源的内存接口。

**实现方法**

void BioFreeCacheResourcePtr\(CacheResourcesDesc \*\*nodeDesc, uint64\_t nodeNum\)

**参数说明**

表 1 参数说明

<table><thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>nodeDesc</td>
    <td>CacheResourcesDesc**</td>
    <td>入参</td>
    <td>节点缓存资源使用情况。</td>
  </tr>
  <tr>
    <td>nodeNum</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>节点数量。</td>
  </tr>
</tbody>
</table>

**返回值**

无返回值。

## BioShowCacheHitRatio

**函数定义**

查询系统缓存命中率接口，查询结果的内存资源要求使用者调用BioFreeCacheHitPtr接口进行释放。

**实现方法**

CResult BioShowCacheHitRatio\(CacheHitFinalDesc \*desc, CacheHitFinalDesc \*\*nodeDesc, uint64\_t \*nodeNum\)

**参数说明**

**表 1  参数说明**

<table><thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>desc</td>
    <td>CacheHitFinalDesc*</td>
    <td>出参</td>
    <td>所有节点缓存命中率。参数解释参见<a href="#详细参数说明4">表2 详细参数说明</a>。</td>
  </tr>
  <tr>
    <td>nodeDesc</td>
    <td>CacheHitFinalDesc**</td>
    <td>出参</td>
    <td>各节点的缓存命中率。</td>
  </tr>
  <tr>
    <td>nodeNum</td>
    <td>uint64_t*</td>
    <td>出参</td>
    <td>节点数量。</td>
  </tr>
</tbody>
</table>

**表 2  详细参数说明**<a id="详细参数说明4"></a>

<table><thead>
  <tr>
    <th>参数</th>
    <th>结构体字段</th>
    <th>说明</th>
  </tr></thead>
<tbody>
  <tr>
    <td rowspan="10">CacheHitFinalDesc</td>
    <td>nodeId</td>
    <td>节点ID。当所有节点命中率为参数时，该字段为无效信息。</td>
  </tr>
  <tr>
    <td>rCacheHitMemCount</td>
    <td>读缓存内存命中数。</td>
  </tr>
  <tr>
    <td>rCacheHitDiskCount</td>
    <td>读缓存磁盘命中数。</td>
  </tr>
  <tr>
    <td>rCacheHitCount</td>
    <td>读缓存命中总数。</td>
  </tr>
  <tr>
    <td>rCacheTotalCount</td>
    <td>读缓存查询总数。</td>
  </tr>
  <tr>
    <td>wCacheHitMemCount</td>
    <td>写缓存内存命中数。</td>
  </tr>
  <tr>
    <td>wCacheHitDiskCount</td>
    <td>写缓存磁盘命中数。</td>
  </tr>
  <tr>
    <td>wCacheHitCount</td>
    <td>写缓存命中总数。</td>
  </tr>
  <tr>
    <td>wCacheTotalCount</td>
    <td>写缓存查询总数。</td>
  </tr>
  <tr>
    <td>backendHitCount</td>
    <td>后端命中数。</td>
  </tr>
</tbody>
</table>

**返回值**

表 3  返回值说明

<table><thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NO_SPACE</td>
    <td>空间不足。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## BioFreeCacheHitPtr

**函数定义**

释放缓存命中率相关的内存资源。

**实现方法**

void BioFreeCacheHitPtr\(CacheHitFinalDesc \*\*nodeDesc, uint64\_t nodeNum\)

**参数说明**

表 1  参数说明

<table style="undefined;table-layout: fixed; width: 685px"><colgroup>
<col style="width: 107px">
<col style="width: 232px">
<col style="width: 135px">
<col style="width: 211px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>nodeDesc</td>
    <td>CacheHitFinalDesc**</td>
    <td>入参</td>
    <td>各节点缓存命中率。</td>
  </tr>
  <tr>
    <td>nodeNum</td>
    <td>uint64_t</td>
    <td>入参</td>
    <td>节点数量。</td>
  </tr>
</tbody>
</table>

**返回值**

无返回值。

## BioAddDisk

**函数定义**

新增加盘接口。

**实现方法**

CResult BioAddDisk\(const char \*diskPath\)

**参数说明**

表 1  参数说明

<table style="undefined;table-layout: fixed; width: 614px"><colgroup>
<col style="width: 154px">
<col style="width: 113px">
<col style="width: 171px">
<col style="width: 176px">
</colgroup>
<thead>
  <tr>
    <th>参数名</th>
    <th>数据类型</th>
    <th>参数类型</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>diskPath</td>
    <td>const char *</td>
    <td>入参</td>
    <td>新增块设备路径，必须是有效的路径。</td>
  </tr>
</tbody>
</table>

**返回值**

表 2  返回值说明

<table style="undefined;table-layout: fixed; width: 566px"><colgroup>
<col style="width: 295px">
<col style="width: 271px">
</colgroup>
<thead>
  <tr>
    <th>返回值</th>
    <th>描述</th>
  </tr></thead>
<tbody>
  <tr>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NOT_READY</td>
    <td>UBS IO服务未就绪。</td>
  </tr>
  <tr>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区故障。</td>
  </tr>
  <tr>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
</tbody>
</table>

## 错误码

表 1  错误码

<table style="undefined;table-layout: fixed; width: 694px"><colgroup>
<col style="width: 105px">
<col style="width: 275px">
<col style="width: 314px">
</colgroup>
<thead>
  <tr>
    <th>错误码数</th>
    <th>错误码</th>
    <th>含义</th>
  </tr></thead>
<tbody>
  <tr>
    <td>0</td>
    <td>RET_CACHE_OK</td>
    <td>操作成功。</td>
  </tr>
  <tr>
    <td>1</td>
    <td>RET_CACHE_PROTECTED</td>
    <td>写缓存保护。</td>
  </tr>
  <tr>
    <td>2</td>
    <td>RET_CACHE_ERROR</td>
    <td>操作失败。</td>
  </tr>
  <tr>
    <td>3</td>
    <td>RET_CACHE_EPERM</td>
    <td>传入参数错误。</td>
  </tr>
  <tr>
    <td>4</td>
    <td>RET_CACHE_BUSY</td>
    <td>缓存忙碌，需要外部重试。</td>
  </tr>
  <tr>
    <td>5</td>
    <td>RET_CACHE_NEED_RETRY</td>
    <td>需要外部重试。</td>
  </tr>
  <tr>
    <td>6</td>
    <td>RET_CACHE_NOT_READY</td>
    <td>缓存未就绪。</td>
  </tr>
  <tr>
    <td>7</td>
    <td>RET_CACHE_NOT_FOUND</td>
    <td>Cache实例不存在。</td>
  </tr>
  <tr>
    <td>8</td>
    <td>RET_CACHE_CONFLICT</td>
    <td>对象冲突。</td>
  </tr>
  <tr>
    <td>9</td>
    <td>RET_CACHE_MISS</td>
    <td>缓存未命中。</td>
  </tr>
  <tr>
    <td>10</td>
    <td>RET_CACHE_NO_SPACE</td>
    <td>空间不足。</td>
  </tr>
  <tr>
    <td>11</td>
    <td>RET_CACHE_UNAVAILABLE</td>
    <td>缓存服务不可用。</td>
  </tr>
  <tr>
    <td>12</td>
    <td>RET_CACHE_EXCEED_QUOTA</td>
    <td>超出配额限制。</td>
  </tr>
  <tr>
    <td>13</td>
    <td>RET_CACHE_PT_FAULT</td>
    <td>分区故障。</td>
  </tr>
  <tr>
    <td>14</td>
    <td>RET_CACHE_READ_EXCEED</td>
    <td>超出读取限制。</td>
  </tr>
  <tr>
    <td>15</td>
    <td>RET_CACHE_EXISTS</td>
    <td>Cache实例已存在。</td>
  </tr>
  <tr>
    <td>16</td>
    <td>RET_CACHE_DISK_FAULT</td>
    <td>磁盘故障。</td>
  </tr>
  <tr>
    <td>17</td>
    <td>RET_CACHE_UFS_FAULT</td>
    <td>后端存储故障。</td>
  </tr>
</tbody></table>

## 版权说明

Copyright \(c\) Huawei Technologies Co., Ltd. 2026. All rights reserved.
