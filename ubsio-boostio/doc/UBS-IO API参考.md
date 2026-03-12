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
|![](image/zh-cn_image_0000002485192120.png)|表示如不避免则将会导致死亡或严重伤害的具有高等级风险的危害。|
|![](image/zh-cn_image_0000002485032160.png)|表示如不避免则可能导致死亡或严重伤害的具有中等级风险的危害。|
|![](image/zh-cn_image_0000002517312047.png)|表示如不避免则可能导致轻微或中度伤害的具有低等级风险的危害。|
|![](image/zh-cn_image_0000002517312049.png)|用于传递设备或环境安全警示信息。如不避免则可能会导致设备损坏、数据丢失、设备性能降低或其它不可预知的结果。“须知”不涉及人身伤害。|
|![](image/zh-cn_image_0000002517352069.png)|对正文中重点信息的补充说明。“说明”不是安全警示信息，不涉及人身、设备及环境伤害信息。|

**修改记录<a name="section2467512116410"></a>**

<a name="zh-cn_topic_0000002543399273_table1557726816410"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000002543399273_row2942532716410"><th class="cellrowborder" valign="top" width="20.72%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000002543399273_p3778275416410"><a name="zh-cn_topic_0000002543399273_p3778275416410"></a><a name="zh-cn_topic_0000002543399273_p3778275416410"></a><strong id="zh-cn_topic_0000002543399273_b5687322716410"><a name="zh-cn_topic_0000002543399273_b5687322716410"></a><a name="zh-cn_topic_0000002543399273_b5687322716410"></a>文档版本</strong></p>
</th>
<th class="cellrowborder" valign="top" width="26.119999999999997%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000002543399273_p5627845516410"><a name="zh-cn_topic_0000002543399273_p5627845516410"></a><a name="zh-cn_topic_0000002543399273_p5627845516410"></a><strong id="zh-cn_topic_0000002543399273_b5800814916410"><a name="zh-cn_topic_0000002543399273_b5800814916410"></a><a name="zh-cn_topic_0000002543399273_b5800814916410"></a>发布日期</strong></p>
</th>
<th class="cellrowborder" valign="top" width="53.16%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000002543399273_p2382284816410"><a name="zh-cn_topic_0000002543399273_p2382284816410"></a><a name="zh-cn_topic_0000002543399273_p2382284816410"></a><strong id="zh-cn_topic_0000002543399273_b3316380216410"><a name="zh-cn_topic_0000002543399273_b3316380216410"></a><a name="zh-cn_topic_0000002543399273_b3316380216410"></a>修改说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000002543399273_row259511572216"><td class="cellrowborder" valign="top" width="20.72%" headers="mcps1.1.4.1.1 "><p id="zh-cn_topic_0000002543399273_p7595175717219"><a name="zh-cn_topic_0000002543399273_p7595175717219"></a><a name="zh-cn_topic_0000002543399273_p7595175717219"></a>01</p>
</td>
<td class="cellrowborder" valign="top" width="26.119999999999997%" headers="mcps1.1.4.1.2 "><p id="zh-cn_topic_0000002543399273_p1459565782115"><a name="zh-cn_topic_0000002543399273_p1459565782115"></a><a name="zh-cn_topic_0000002543399273_p1459565782115"></a>2026-03-30</p>
</td>
<td class="cellrowborder" valign="top" width="53.16%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000002543399273_p759519575214"><a name="zh-cn_topic_0000002543399273_p759519575214"></a><a name="zh-cn_topic_0000002543399273_p759519575214"></a>第一次正式发布。</p>
</td>
</tr>
</tbody>
</table>

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

<a name="table144421254161114"></a>
<table><thead align="left"><tr id="row1444245461114"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p194431054151117"><a name="p194431054151117"></a><a name="p194431054151117"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p1744365417115"><a name="p1744365417115"></a><a name="p1744365417115"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p19754161217121"><a name="p19754161217121"></a><a name="p19754161217121"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p1744311545113"><a name="p1744311545113"></a><a name="p1744311545113"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row14431554101112"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p115001720171213"><a name="p115001720171213"></a><a name="p115001720171213"></a>mode</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p714762615127"><a name="p714762615127"></a><a name="p714762615127"></a>WorkerMode</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p18147626121217"><a name="p18147626121217"></a><a name="p18147626121217"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p814762616123"><a name="p814762616123"></a><a name="p814762616123"></a>工作模式，有以下两种类型：</p>
<a name="ul167425280497"></a><a name="ul167425280497"></a><ul id="ul167425280497"><li>CONVERGENCE(0)：融合模式，适用于AI场景。</li><li>SEPARATES(1)：分离模式，适用于大数据场景。</li></ul>
</td>
</tr>
<tr id="row183413415558"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p5928276570"><a name="p5928276570"></a><a name="p5928276570"></a>optConf</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1084774235713"><a name="p1084774235713"></a><a name="p1084774235713"></a>ClientOptionsConfig*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p4834941135516"><a name="p4834941135516"></a><a name="p4834941135516"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1236917392541"><a name="p1236917392541"></a><a name="p1236917392541"></a>UBS IO初始化选项，详细解释参见<a href="#table0292135934712">表2</a>。</p>
</td>
</tr>
</tbody>
</table>

**表 2**  详细参数说明

<a name="table0292135934712"></a>
<table><thead align="left"><tr id="row129210597470"><th class="cellrowborder" valign="top" width="21.542154215421544%" id="mcps1.2.4.1.1"><p id="p3292359174711"><a name="p3292359174711"></a><a name="p3292359174711"></a>参数</p>
</th>
<th class="cellrowborder" valign="top" width="27.792779277927792%" id="mcps1.2.4.1.2"><p id="p5292145918479"><a name="p5292145918479"></a><a name="p5292145918479"></a>结构体字段</p>
</th>
<th class="cellrowborder" valign="top" width="50.66506650665067%" id="mcps1.2.4.1.3"><p id="p1584336164816"><a name="p1584336164816"></a><a name="p1584336164816"></a>说明</p>
</th>
</tr>
</thead>
<tbody><tr id="row14292359194714"><td class="cellrowborder" rowspan="10" valign="top" width="21.542154215421544%" headers="mcps1.2.4.1.1 "><p id="p129255934713"><a name="p129255934713"></a><a name="p129255934713"></a>ClientOptionsConfig</p>
</td>
<td class="cellrowborder" valign="top" width="27.792779277927792%" headers="mcps1.2.4.1.2 "><p id="p19329210105117"><a name="p19329210105117"></a><a name="p19329210105117"></a>LogType logType</p>
</td>
<td class="cellrowborder" valign="top" width="50.66506650665067%" headers="mcps1.2.4.1.3 "><p id="p179831633186"><a name="p179831633186"></a><a name="p179831633186"></a>日志类型。</p>
<a name="ul26315651811"></a><a name="ul26315651811"></a><ul id="ul26315651811"><li>STDOUT_TYPE(0)标准流输出。</li><li>FILE_TYPE(1)日志文件输出。</li><li>STDERR_TYPE(2)标准错误流输出。</li></ul>
<p id="p156291443101814"><a name="p156291443101814"></a><a name="p156291443101814"></a>仅分离模式下生效。</p>
</td>
</tr>
<tr id="row829265924718"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p4292659154713"><a name="p4292659154713"></a><a name="p4292659154713"></a>char logFilePath[PATH_MAX]</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p1841436144819"><a name="p1841436144819"></a><a name="p1841436144819"></a>日志文件输出路径，仅分离模式下生效，使用者需要保证传入的日志路径可访问。</p>
</td>
</tr>
<tr id="row10747143312526"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p674733317526"><a name="p674733317526"></a><a name="p674733317526"></a>uint8_t enable</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p541871452110"><a name="p541871452110"></a><a name="p541871452110"></a>安全开关。</p>
<a name="ul20720230218"></a><a name="ul20720230218"></a><ul id="ul20720230218"><li>0：表示关闭。</li><li>非0：表示打开。</li></ul>
</td>
</tr>
<tr id="row16366193618524"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p23665366522"><a name="p23665366522"></a><a name="p23665366522"></a>char certificationPath[PATH_MAX]</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p133671336195219"><a name="p133671336195219"></a><a name="p133671336195219"></a>Client证书路径，安全使能时要求路径有效。</p>
</td>
</tr>
<tr id="row12111239115215"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p151121439155210"><a name="p151121439155210"></a><a name="p151121439155210"></a>char caCerPath[PATH_MAX]</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p81121139195210"><a name="p81121139195210"></a><a name="p81121139195210"></a>CA证书路径，安全使能时要求路径有效。</p>
</td>
</tr>
<tr id="row1674005210522"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p197408529528"><a name="p197408529528"></a><a name="p197408529528"></a>char caCrlPath[PATH_MAX]</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p19740105295217"><a name="p19740105295217"></a><a name="p19740105295217"></a>吊销证书列表文件路径，可选。安全使能时要求路径有效。</p>
</td>
</tr>
<tr id="row17294459145218"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p12294859175213"><a name="p12294859175213"></a><a name="p12294859175213"></a>char privateKeyPath[PATH_MAX]</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p529475917526"><a name="p529475917526"></a><a name="p529475917526"></a>Client证书私钥路径，安全使能时要求路径有效。</p>
</td>
</tr>
<tr id="row1617875711528"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p161784573520"><a name="p161784573520"></a><a name="p161784573520"></a>char privateKeyPassword[PATH_MAX]</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p2017817572526"><a name="p2017817572526"></a><a name="p2017817572526"></a>Client证书私钥口令密文的文件路径，可以为空，为空时需要提供未加密的私钥路径。不为空时安全使能时要求路径有效。</p>
</td>
</tr>
<tr id="row864321625316"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p13643716205316"><a name="p13643716205316"></a><a name="p13643716205316"></a>char decrypterLibPath[PATH_MAX]</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p1964311615314"><a name="p1964311615314"></a><a name="p1964311615314"></a>Client证书解密函数so文件路径，可以为空，为空时需要提供明文口令。不为空时安全使能时要求路径有效。</p>
</td>
</tr>
<tr id="row10952318185311"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p4952131810531"><a name="p4952131810531"></a><a name="p4952131810531"></a>char opensslLibDir[PATH_MAX]</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p195214189538"><a name="p195214189538"></a><a name="p195214189538"></a>Client端openssl, crypto的so目录路径，可选，为空时使用默认版本，安全使能时要求路径有效。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 3**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p4504142512409"><a name="p4504142512409"></a><a name="p4504142512409"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p19340163234111"><a name="p19340163234111"></a><a name="p19340163234111"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p13340432194119"><a name="p13340432194119"></a><a name="p13340432194119"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

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

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row133501616132717"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p12350111617277"><a name="p12350111617277"></a><a name="p12350111617277"></a>desc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1035016163271"><a name="p1035016163271"></a><a name="p1035016163271"></a>CacheDescriptor</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p12350161610272"><a name="p12350161610272"></a><a name="p12350161610272"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p169487242339"><a name="p169487242339"></a><a name="p169487242339"></a>实例参数描述：</p>
<a name="ul18184171010311"></a><a name="ul18184171010311"></a><ul id="ul18184171010311"><li>uint64_t tenantId;<p id="p1715416341313"><a name="p1715416341313"></a><a name="p1715416341313"></a>租户ID。</p>
</li><li>AffinityStrategy affinity;<p id="p1801156143117"><a name="p1801156143117"></a><a name="p1801156143117"></a>数据亲和策略：</p>
<a name="ul1679282814323"></a><a name="ul1679282814323"></a><ul id="ul1679282814323"><li>LOCAL_AFFINITY(1)：本地亲和。</li><li>GLOBAL_BALANCE(2)：全局均衡。</li></ul>
</li><li>WriteStrategy strategy;<div class="p" id="p199115512344"><a name="p199115512344"></a><a name="p199115512344"></a>缓存策略：<a name="ul117461983354"></a><a name="ul117461983354"></a><ul id="ul117461983354"><li>WRITE_BACK(1)：回写模式。</li><li>WRITE_THROUGH(2)：透写模式。</li></ul>
</div>
</li></ul>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p4244144834418"><a name="p4244144834418"></a><a name="p4244144834418"></a>RET_CACHE_EXISTS</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例已存在。</p>
</td>
</tr>
<tr id="row11515111911453"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p11515111918456"><a name="p11515111918456"></a><a name="p11515111918456"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p125151919184510"><a name="p125151919184510"></a><a name="p125151919184510"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row1102401854"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p910214016510"><a name="p910214016510"></a><a name="p910214016510"></a>RET_CACHE_NO_SPACE</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p4656336512"><a name="p4656336512"></a><a name="p4656336512"></a>缓存空间不足，最大缓存数量为1024个。</p>
</td>
</tr>
</tbody>
</table>

## BioGetCache<a name="ZH-CN_TOPIC_0000002521860662"></a>

**函数定义<a name="section4349141610276"></a>**

获取Cache实例接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioGetCache\(uint64\_t tenantId, CacheDescriptor \*desc\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row133501616132717"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p12350111617277"><a name="p12350111617277"></a><a name="p12350111617277"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1035016163271"><a name="p1035016163271"></a><a name="p1035016163271"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p12350161610272"><a name="p12350161610272"></a><a name="p12350161610272"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p159112497417"><a name="p159112497417"></a><a name="p159112497417"></a>租户ID。</p>
</td>
</tr>
<tr id="row91135216258"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1011322113259"><a name="p1011322113259"></a><a name="p1011322113259"></a>desc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p141131321172516"><a name="p141131321172516"></a><a name="p141131321172516"></a>CacheDescriptor*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p1113122118256"><a name="p1113122118256"></a><a name="p1113122118256"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p169487242339"><a name="p169487242339"></a><a name="p169487242339"></a>实例参数描述：</p>
<a name="ul18184171010311"></a><a name="ul18184171010311"></a><ul id="ul18184171010311"><li>uint64_t tenantId;<p id="p1715416341313"><a name="p1715416341313"></a><a name="p1715416341313"></a>租户ID。</p>
</li><li>AffinityStrategy affinity;<p id="p1801156143117"><a name="p1801156143117"></a><a name="p1801156143117"></a>数据亲和策略：</p>
<a name="ul1679282814323"></a><a name="ul1679282814323"></a><ul id="ul1679282814323"><li>LOCAL_AFFINITY(1)：本地亲和。</li><li>GLOBAL_BALANCE(2)：全局均衡。</li></ul>
</li><li>WriteStrategy strategy;<div class="p" id="p199115512344"><a name="p199115512344"></a><a name="p199115512344"></a>缓存策略：<a name="ul117461983354"></a><a name="ul117461983354"></a><ul id="ul117461983354"><li>WRITE_BACK(1)：回写模式。</li><li>WRITE_THROUGH(2)：透写模式。</li></ul>
</div>
</li></ul>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row11515111911453"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p11515111918456"><a name="p11515111918456"></a><a name="p11515111918456"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p125151919184510"><a name="p125151919184510"></a><a name="p125151919184510"></a>传入参数错误。</p>
</td>
</tr>
</tbody>
</table>

## BioDestroyCache<a name="ZH-CN_TOPIC_0000002521700654"></a>

**函数定义<a name="section4349141610276"></a>**

销毁Cache实例接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioDestroyCache\(uint64\_t tenantId\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row133501616132717"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p12350111617277"><a name="p12350111617277"></a><a name="p12350111617277"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1035016163271"><a name="p1035016163271"></a><a name="p1035016163271"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p12350161610272"><a name="p12350161610272"></a><a name="p12350161610272"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p159112497417"><a name="p159112497417"></a><a name="p159112497417"></a>租户ID。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
</tbody>
</table>

## BioCalcLocation<a name="ZH-CN_TOPIC_0000002552860623"></a>

**函数定义<a name="section4349141610276"></a>**

计算对象位置接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioCalcLocation\(uint64\_t tenantId, uint64\_t objectId, ObjLocation \*location\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row133501616132717"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p12350111617277"><a name="p12350111617277"></a><a name="p12350111617277"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1035016163271"><a name="p1035016163271"></a><a name="p1035016163271"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p12350161610272"><a name="p12350161610272"></a><a name="p12350161610272"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p159112497417"><a name="p159112497417"></a><a name="p159112497417"></a>租户ID。</p>
</td>
</tr>
<tr id="row5213807409"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p132142074014"><a name="p132142074014"></a><a name="p132142074014"></a>objectId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p172141003401"><a name="p172141003401"></a><a name="p172141003401"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p521418015409"><a name="p521418015409"></a><a name="p521418015409"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p192148044020"><a name="p192148044020"></a><a name="p192148044020"></a>对象ID。</p>
</td>
</tr>
<tr id="row415921694017"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p6159201614400"><a name="p6159201614400"></a><a name="p6159201614400"></a>location</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p13159121616407"><a name="p13159121616407"></a><a name="p13159121616407"></a>ObjLocation*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p16159131610409"><a name="p16159131610409"></a><a name="p16159131610409"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p86345541119"><a name="p86345541119"></a><a name="p86345541119"></a>对象位置，由两个uint64_t组成，对使用者透明。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row1434916283447"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p4350152812440"><a name="p4350152812440"></a><a name="p4350152812440"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16230428115612"><a name="p16230428115612"></a><a name="p16230428115612"></a>分区错误，对象位置无法写入。</p>
</td>
</tr>
</tbody>
</table>

## BioPut<a name="ZH-CN_TOPIC_0000002521860654"></a>

**函数定义<a name="section4349141610276"></a>**

对象写入接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioPut\(uint64\_t tenantId, const char \*key, const char \*value, uint64\_t length, ObjLocation location\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row07821652155213"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
<tr id="row6659144111499"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1065911415498"><a name="p1065911415498"></a><a name="p1065911415498"></a>key</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1365919419490"><a name="p1365919419490"></a><a name="p1365919419490"></a>const char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p86591141104910"><a name="p86591141104910"></a><a name="p86591141104910"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p86591541124915"><a name="p86591541124915"></a><a name="p86591541124915"></a>对象的key。</p>
</td>
</tr>
<tr id="row7699174412490"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p97001144104914"><a name="p97001144104914"></a><a name="p97001144104914"></a>value</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p11700164454912"><a name="p11700164454912"></a><a name="p11700164454912"></a>const char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p17001445498"><a name="p17001445498"></a><a name="p17001445498"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p9153114614534"><a name="p9153114614534"></a><a name="p9153114614534"></a>待写入的数据buffer，value空间的长度必须和入参length一致。</p>
</td>
</tr>
<tr id="row1286118473492"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p28611947134911"><a name="p28611947134911"></a><a name="p28611947134911"></a>length</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p4861347134914"><a name="p4861347134914"></a><a name="p4861347134914"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p148611547154910"><a name="p148611547154910"></a><a name="p148611547154910"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p12862184724919"><a name="p12862184724919"></a><a name="p12862184724919"></a>写入数据长度。</p>
</td>
</tr>
<tr id="row166535164911"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p265195110490"><a name="p265195110490"></a><a name="p265195110490"></a>location</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p165105124910"><a name="p165105124910"></a><a name="p165105124910"></a>ObjLocation</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p10659512492"><a name="p10659512492"></a><a name="p10659512492"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1165165111497"><a name="p1165165111497"></a><a name="p1165165111497"></a>对象位置。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row5229828125615"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p423012865618"><a name="p423012865618"></a><a name="p423012865618"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16230428115612"><a name="p16230428115612"></a><a name="p16230428115612"></a>分区错误，对象位置无法写入。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioGet<a name="ZH-CN_TOPIC_0000002552860633"></a>

**函数定义<a name="section4349141610276"></a>**

对象读取接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioGet\(uint64\_t tenantId, const char \*key, uint64\_t offset, uint64\_t length, ObjLocation location, char \*value, uint64\_t \*realLength\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row15416295110"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
<tr id="row596110227564"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1065911415498"><a name="p1065911415498"></a><a name="p1065911415498"></a>key</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1365919419490"><a name="p1365919419490"></a><a name="p1365919419490"></a>const char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p86591141104910"><a name="p86591141104910"></a><a name="p86591141104910"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p86591541124915"><a name="p86591541124915"></a><a name="p86591541124915"></a>对象的key。</p>
</td>
</tr>
<tr id="row117032142579"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p97037141573"><a name="p97037141573"></a><a name="p97037141573"></a>offset</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p4703181455717"><a name="p4703181455717"></a><a name="p4703181455717"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p1770314149579"><a name="p1770314149579"></a><a name="p1770314149579"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1703141415715"><a name="p1703141415715"></a><a name="p1703141415715"></a>待读取数据偏移。</p>
</td>
</tr>
<tr id="row18261191219576"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p177291423135716"><a name="p177291423135716"></a><a name="p177291423135716"></a>length</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p37291023125711"><a name="p37291023125711"></a><a name="p37291023125711"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p1872952311575"><a name="p1872952311575"></a><a name="p1872952311575"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p5729102319570"><a name="p5729102319570"></a><a name="p5729102319570"></a>读取的数据长度。</p>
</td>
</tr>
<tr id="row1763391195711"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p265195110490"><a name="p265195110490"></a><a name="p265195110490"></a>location</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p165105124910"><a name="p165105124910"></a><a name="p165105124910"></a>ObjLocation</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p10659512492"><a name="p10659512492"></a><a name="p10659512492"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1165165111497"><a name="p1165165111497"></a><a name="p1165165111497"></a>对象位置。</p>
</td>
</tr>
<tr id="row1095204567"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p97001144104914"><a name="p97001144104914"></a><a name="p97001144104914"></a>value</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p11700164454912"><a name="p11700164454912"></a><a name="p11700164454912"></a>char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p17001445498"><a name="p17001445498"></a><a name="p17001445498"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p470015449495"><a name="p470015449495"></a><a name="p470015449495"></a>待读取的数据buffer。</p>
</td>
</tr>
<tr id="row133501616132717"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p28611947134911"><a name="p28611947134911"></a><a name="p28611947134911"></a>realLength</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p4861347134914"><a name="p4861347134914"></a><a name="p4861347134914"></a>uint64_t*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p148611547154910"><a name="p148611547154910"></a><a name="p148611547154910"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p12862184724919"><a name="p12862184724919"></a><a name="p12862184724919"></a>实际读取的数据长度。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row5229828125615"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p423012865618"><a name="p423012865618"></a><a name="p423012865618"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16230428115612"><a name="p16230428115612"></a><a name="p16230428115612"></a>分区错误，对象位置无法写入。</p>
</td>
</tr>
<tr id="row163833711417"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p113831571442"><a name="p113831571442"></a><a name="p113831571442"></a>RET_CACHE_READ_EXCEED</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p8383187744"><a name="p8383187744"></a><a name="p8383187744"></a>读取数据长度超过写入长度。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioDelete<a name="ZH-CN_TOPIC_0000002552740619"></a>

**函数定义<a name="section4349141610276"></a>**

对象删除接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioDelete\(uint64\_t tenantId, const char \*key, ObjLocation location\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row101261915477"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
<tr id="row1045485115553"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1065911415498"><a name="p1065911415498"></a><a name="p1065911415498"></a>key</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1365919419490"><a name="p1365919419490"></a><a name="p1365919419490"></a>const char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p86591141104910"><a name="p86591141104910"></a><a name="p86591141104910"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p86591541124915"><a name="p86591541124915"></a><a name="p86591541124915"></a>对象的key。</p>
</td>
</tr>
<tr id="row67949325615"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p265195110490"><a name="p265195110490"></a><a name="p265195110490"></a>location</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p165105124910"><a name="p165105124910"></a><a name="p165105124910"></a>ObjLocation</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p10659512492"><a name="p10659512492"></a><a name="p10659512492"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1165165111497"><a name="p1165165111497"></a><a name="p1165165111497"></a>对象位置。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row229519132450"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p16230428115612"><a name="p16230428115612"></a><a name="p16230428115612"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1892214158457"><a name="p1892214158457"></a><a name="p1892214158457"></a>分区错误，对象位置无法写入。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioLoad<a name="ZH-CN_TOPIC_0000002521860644"></a>

**函数定义<a name="section4349141610276"></a>**

对象加载接口，该接口是异步接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioLoad\(uint64\_t tenantId, const char \*key, uint64\_t offset, uint64\_t length, ObjLocation location, BioLoadCallback callback, void \*context\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row7721043191517"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
<tr id="row279591127"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1065911415498"><a name="p1065911415498"></a><a name="p1065911415498"></a>key</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1365919419490"><a name="p1365919419490"></a><a name="p1365919419490"></a>const char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p86591141104910"><a name="p86591141104910"></a><a name="p86591141104910"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p86591541124915"><a name="p86591541124915"></a><a name="p86591541124915"></a>对象的key。</p>
</td>
</tr>
<tr id="row15596197620"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p97037141573"><a name="p97037141573"></a><a name="p97037141573"></a>offset</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p4703181455717"><a name="p4703181455717"></a><a name="p4703181455717"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p1770314149579"><a name="p1770314149579"></a><a name="p1770314149579"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1703141415715"><a name="p1703141415715"></a><a name="p1703141415715"></a>待加载数据的偏移。</p>
</td>
</tr>
<tr id="row7413251425"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p177291423135716"><a name="p177291423135716"></a><a name="p177291423135716"></a>length</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p37291023125711"><a name="p37291023125711"></a><a name="p37291023125711"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p1872952311575"><a name="p1872952311575"></a><a name="p1872952311575"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p5729102319570"><a name="p5729102319570"></a><a name="p5729102319570"></a>待加载的数据长度。</p>
</td>
</tr>
<tr id="row133501616132717"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p265195110490"><a name="p265195110490"></a><a name="p265195110490"></a>location</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p165105124910"><a name="p165105124910"></a><a name="p165105124910"></a>ObjLocation</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p10659512492"><a name="p10659512492"></a><a name="p10659512492"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1165165111497"><a name="p1165165111497"></a><a name="p1165165111497"></a>对象位置。</p>
</td>
</tr>
<tr id="row19716371025"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p20711037227"><a name="p20711037227"></a><a name="p20711037227"></a>callback</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1771133719218"><a name="p1771133719218"></a><a name="p1771133719218"></a>BioLoadCallback</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p97113717214"><a name="p97113717214"></a><a name="p97113717214"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p137153715218"><a name="p137153715218"></a><a name="p137153715218"></a>异步回调函数。</p>
</td>
</tr>
<tr id="row83364019217"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p8341540425"><a name="p8341540425"></a><a name="p8341540425"></a>context</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p18341740124"><a name="p18341740124"></a><a name="p18341740124"></a>void*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p53414015214"><a name="p53414015214"></a><a name="p53414015214"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p133410402215"><a name="p133410402215"></a><a name="p133410402215"></a>回调上下文。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row17193191294615"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1019371274616"><a name="p1019371274616"></a><a name="p1019371274616"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1892214158457"><a name="p1892214158457"></a><a name="p1892214158457"></a>分区错误，对象位置无法写入。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioListAll<a name="ZH-CN_TOPIC_0000002521860650"></a>

**函数定义<a name="section4349141610276"></a>**

对象列举接口，要求使用者调用BioFreeListResources接口释放列举结果的内存资源。

**实现方法<a name="section11350171616279"></a>**

CResult BioListAll\(uint64\_t tenantId, const char \*prefix, ObjStat \*\*objs, uint64\_t \*objNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row959815224207"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
<tr id="row10797192512613"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p87972258618"><a name="p87972258618"></a><a name="p87972258618"></a>prefix</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p77971025869"><a name="p77971025869"></a><a name="p77971025869"></a>const char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p279772517615"><a name="p279772517615"></a><a name="p279772517615"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p479772514611"><a name="p479772514611"></a><a name="p479772514611"></a>待匹配的对象前缀。</p>
</td>
</tr>
<tr id="row26819221462"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p166827222610"><a name="p166827222610"></a><a name="p166827222610"></a>objs</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p0682422165"><a name="p0682422165"></a><a name="p0682422165"></a>ObjStat**</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p36821322167"><a name="p36821322167"></a><a name="p36821322167"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1468213221267"><a name="p1468213221267"></a><a name="p1468213221267"></a>列举对象结果。</p>
</td>
</tr>
<tr id="row14301911152118"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1130141162118"><a name="p1130141162118"></a><a name="p1130141162118"></a>objNum</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1430281192116"><a name="p1430281192116"></a><a name="p1430281192116"></a>uint64_t*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p5377623192113"><a name="p5377623192113"></a><a name="p5377623192113"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p12302171119215"><a name="p12302171119215"></a><a name="p12302171119215"></a>列举对象的数量。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row4152183114220"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p0152131122216"><a name="p0152131122216"></a><a name="p0152131122216"></a>RET_CACHE_NO_SPACE</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p815273118221"><a name="p815273118221"></a><a name="p815273118221"></a>内存空间不足。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioFreeListResources<a name="ZH-CN_TOPIC_0000002552740623"></a>

**函数定义<a name="section4349141610276"></a>**

释放列举对象结果内存资源接口。

**实现方法<a name="section11350171616279"></a>**

void BioFreeListResources\(ObjStat \*\*objs, uint64\_t objNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row10797192512613"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p15310141215306"><a name="p15310141215306"></a><a name="p15310141215306"></a>objs</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p77971025869"><a name="p77971025869"></a><a name="p77971025869"></a>ObjStat**</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p279772517615"><a name="p279772517615"></a><a name="p279772517615"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p479772514611"><a name="p479772514611"></a><a name="p479772514611"></a>列举对象结果。</p>
</td>
</tr>
<tr id="row26819221462"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p166827222610"><a name="p166827222610"></a><a name="p166827222610"></a>objNum</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p0682422165"><a name="p0682422165"></a><a name="p0682422165"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p36821322167"><a name="p36821322167"></a><a name="p36821322167"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p12302171119215"><a name="p12302171119215"></a><a name="p12302171119215"></a>列举对象的数量。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

无返回值。

## BioStat<a name="ZH-CN_TOPIC_0000002552740607"></a>

**函数定义<a name="section4349141610276"></a>**

查询对象信息接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioStat\(uint64\_t tenantId, const char \*key, ObjLocation location, ObjStat \*stat\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
<tr id="row1838368131019"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p87972258618"><a name="p87972258618"></a><a name="p87972258618"></a>key</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p77971025869"><a name="p77971025869"></a><a name="p77971025869"></a>const char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p279772517615"><a name="p279772517615"></a><a name="p279772517615"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p16819135393613"><a name="p16819135393613"></a><a name="p16819135393613"></a>对象的key。</p>
</td>
</tr>
<tr id="row12445166171012"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p166827222610"><a name="p166827222610"></a><a name="p166827222610"></a>location</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p0682422165"><a name="p0682422165"></a><a name="p0682422165"></a>ObjLocation</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p36821322167"><a name="p36821322167"></a><a name="p36821322167"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1468213221267"><a name="p1468213221267"></a><a name="p1468213221267"></a>对象位置。</p>
</td>
</tr>
<tr id="row133501616132717"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1358493311109"><a name="p1358493311109"></a><a name="p1358493311109"></a>stat</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1558412338106"><a name="p1558412338106"></a><a name="p1558412338106"></a>ObjStat*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p195841933141012"><a name="p195841933141012"></a><a name="p195841933141012"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p2585173315105"><a name="p2585173315105"></a><a name="p2585173315105"></a>对象信息。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row20926052174613"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1019371274616"><a name="p1019371274616"></a><a name="p1019371274616"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1192619528465"><a name="p1192619528465"></a><a name="p1192619528465"></a>分区错误，对象位置无法写入。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioNotifyUpgradePrepare<a name="ZH-CN_TOPIC_0000002552860619"></a>

**函数定义<a name="section4349141610276"></a>**

通知升级准备接口，使用者调用该接口后将停止UBS IO读写缓存服务，缓存数据逐渐淘汰到后端存储，后续前台IO直接写入后端存储中。

**实现方法<a name="section11350171616279"></a>**

CResult BioNotifyUpgradePrepare\(uint64\_t tenantId\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioNotifyUpgradeFinish<a name="ZH-CN_TOPIC_0000002552740597"></a>

**函数定义<a name="section4349141610276"></a>**

通知升级完成接口，使用者调用该接口后将重启UBS IO读写缓存服务，后续前台IO直接写入后端缓存中。

**实现方法<a name="section11350171616279"></a>**

CResult BioNotifyUpgradeFinish\(uint64\_t tenantId\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioCheckUpgradeReady<a name="ZH-CN_TOPIC_0000002552860627"></a>

**函数定义<a name="section4349141610276"></a>**

检查升级就绪接口。使用者调用该接口返回成功则表示允许进行离线升级；返回失败则需要延时等待后再次检查。

**实现方法<a name="section11350171616279"></a>**

CResult BioCheckUpgradeReady\(uint64\_t tenantId\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioAllocCacheSpace<a name="ZH-CN_TOPIC_0000002521700666"></a>

**函数定义<a name="section4349141610276"></a>**

申请缓存空间接口，该接口配合免拷贝写使用。

**实现方法<a name="section11350171616279"></a>**

CResult BioAllocCacheSpace\(uint64\_t tenantId, uint64\_t objectId, uint64\_t length, CacheSpaceDesc \*space\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
<tr id="row3245172411562"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p132142074014"><a name="p132142074014"></a><a name="p132142074014"></a>objectId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p172141003401"><a name="p172141003401"></a><a name="p172141003401"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p521418015409"><a name="p521418015409"></a><a name="p521418015409"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p192148044020"><a name="p192148044020"></a><a name="p192148044020"></a>对象ID。</p>
</td>
</tr>
<tr id="row1586910185714"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p88612107574"><a name="p88612107574"></a><a name="p88612107574"></a>length</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p586161005718"><a name="p586161005718"></a><a name="p586161005718"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p20866109573"><a name="p20866109573"></a><a name="p20866109573"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p5865101576"><a name="p5865101576"></a><a name="p5865101576"></a>待申请缓存空间长度，最大值为4,194,304（4M）。</p>
</td>
</tr>
<tr id="row12716182625620"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1071672605617"><a name="p1071672605617"></a><a name="p1071672605617"></a>space</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p16716142618563"><a name="p16716142618563"></a><a name="p16716142618563"></a>CacheSpaceDesc*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p0716132635618"><a name="p0716132635618"></a><a name="p0716132635618"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p17161526175612"><a name="p17161526175612"></a><a name="p17161526175612"></a>缓存空间信息描述：</p>
<a name="ul590216505558"></a><a name="ul590216505558"></a><ul id="ul590216505558"><li>uint8_t allocLoc;<p id="p98561224569"><a name="p98561224569"></a><a name="p98561224569"></a>申请标记。</p>
</li><li>uint16_t addressNum;<p id="p127992298566"><a name="p127992298566"></a><a name="p127992298566"></a>地址数量。</p>
</li><li>uint16_t descriptorSize;<p id="p102181653135610"><a name="p102181653135610"></a><a name="p102181653135610"></a>缓存空间描述长度。</p>
</li><li>ObjLocation loc;<p id="p2095919284577"><a name="p2095919284577"></a><a name="p2095919284577"></a>缓存空间位置。</p>
</li><li>CacheAddress address[CACHE_SPACE_ADDRESS_SIZE];<p id="p154641880589"><a name="p154641880589"></a><a name="p154641880589"></a>缓存空间地址信息：</p>
<a name="ul1526425562113"></a><a name="ul1526425562113"></a><ul id="ul1526425562113"><li>uint64_t address;</li></ul>
<p id="p616384371819"><a name="p616384371819"></a><a name="p616384371819"></a>缓存地址。</p>
<a name="ul134201430224"></a><a name="ul134201430224"></a><ul id="ul134201430224"><li>uint32_t size;</li></ul>
<p id="p121922141919"><a name="p121922141919"></a><a name="p121922141919"></a>缓存长度。</p>
</li><li>char descriptorInfo[CACHE_SPACE_DESC_SIZE];<p id="p1088861395810"><a name="p1088861395810"></a><a name="p1088861395810"></a>缓存空间描述信息。</p>
</li></ul>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20.61%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="79.39%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20.61%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="79.39%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20.61%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="79.39%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20.61%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="79.39%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20.61%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="79.39%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row74626237551"><td class="cellrowborder" valign="top" width="20.61%" headers="mcps1.2.3.1.1 "><p id="p3463162395510"><a name="p3463162395510"></a><a name="p3463162395510"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="79.39%" headers="mcps1.2.3.1.2 "><p id="p174631823145512"><a name="p174631823145512"></a><a name="p174631823145512"></a>分区错误，对象位置无法写入。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20.61%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="79.39%" headers="mcps1.2.3.1.2 "><p id="p1294945912344"><a name="p1294945912344"></a><a name="p1294945912344"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioPutWithCopyFree<a name="ZH-CN_TOPIC_0000002552740611"></a>

**函数定义<a name="section4349141610276"></a>**

对象免拷贝写入接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioPutWithCopyFree\(uint64\_t tenantId, const char \*key, CacheSpaceDesc \*space\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>tenantId</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>租户ID。</p>
</td>
</tr>
<tr id="row3245172411562"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p132142074014"><a name="p132142074014"></a><a name="p132142074014"></a>key</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p172141003401"><a name="p172141003401"></a><a name="p172141003401"></a>const char *</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p521418015409"><a name="p521418015409"></a><a name="p521418015409"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p192148044020"><a name="p192148044020"></a><a name="p192148044020"></a>对象的key。</p>
</td>
</tr>
<tr id="row12716182625620"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1071672605617"><a name="p1071672605617"></a><a name="p1071672605617"></a>space</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p16716142618563"><a name="p16716142618563"></a><a name="p16716142618563"></a>CacheSpaceDesc*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p0716132635618"><a name="p0716132635618"></a><a name="p0716132635618"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p144328391361"><a name="p144328391361"></a><a name="p144328391361"></a>调用BioAllocCacheSpace成功后返回的缓存空间，所有CacheAddress address[CACHE_SPACE_ADDRESS_SIZE]中的size和最大值为4,194,304（4M）。</p>
<p id="p17161526175612"><a name="p17161526175612"></a><a name="p17161526175612"></a>缓存空间信息描述：</p>
<a name="ul590216505558"></a><a name="ul590216505558"></a><ul id="ul590216505558"><li>uint8_t allocLoc;<p id="p98561224569"><a name="p98561224569"></a><a name="p98561224569"></a>申请标记。</p>
</li><li>uint16_t addressNum;<p id="p127992298566"><a name="p127992298566"></a><a name="p127992298566"></a>地址数量。</p>
</li><li>uint16_t descriptorSize;<p id="p102181653135610"><a name="p102181653135610"></a><a name="p102181653135610"></a>缓存空间描述长度。</p>
</li><li>ObjLocation loc;<p id="p2095919284577"><a name="p2095919284577"></a><a name="p2095919284577"></a>缓存空间位置。</p>
</li><li>CacheAddress address[CACHE_SPACE_ADDRESS_SIZE];<p id="p154641880589"><a name="p154641880589"></a><a name="p154641880589"></a>缓存空间地址信息：</p>
<a name="ul1526425562113"></a><a name="ul1526425562113"></a><ul id="ul1526425562113"><li>uint64_t address;</li></ul>
<p id="p616384371819"><a name="p616384371819"></a><a name="p616384371819"></a>缓存地址。</p>
<a name="ul134201430224"></a><a name="ul134201430224"></a><ul id="ul134201430224"><li>uint32_t size;</li></ul>
<p id="p121922141919"><a name="p121922141919"></a><a name="p121922141919"></a>缓存长度。</p>
</li><li>char descriptorInfo[CACHE_SPACE_DEC_SIZE];<p id="p1088861395810"><a name="p1088861395810"></a><a name="p1088861395810"></a>缓存空间描述信息。</p>
</li></ul>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row1472972734914"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p13729327124914"><a name="p13729327124914"></a><a name="p13729327124914"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16729427154910"><a name="p16729427154910"></a><a name="p16729427154910"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row5229828125615"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p423012865618"><a name="p423012865618"></a><a name="p423012865618"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p16230428115612"><a name="p16230428115612"></a><a name="p16230428115612"></a>分区错误，对象位置无法写入。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioReadHook<a name="ZH-CN_TOPIC_0000002521700662"></a>

**函数定义<a name="section4349141610276"></a>**

拦截读接口。

**实现方法<a name="section11350171616279"></a>**

int BioReadHook\(uint64\_t inode, char \*buff, uint64\_t count, uint64\_t offset, int \*readLen\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>inode</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p132151481359"><a name="p132151481359"></a><a name="p132151481359"></a>文件inode。</p>
</td>
</tr>
<tr id="row12709103323918"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1871014333394"><a name="p1871014333394"></a><a name="p1871014333394"></a>buff</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1971063383915"><a name="p1971063383915"></a><a name="p1971063383915"></a>char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p7710153323919"><a name="p7710153323919"></a><a name="p7710153323919"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p6710233153916"><a name="p6710233153916"></a><a name="p6710233153916"></a>待读取数据buffer。</p>
</td>
</tr>
<tr id="row1662115714390"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p188256383344"><a name="p188256383344"></a><a name="p188256383344"></a>count</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p38258384344"><a name="p38258384344"></a><a name="p38258384344"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p3825143812346"><a name="p3825143812346"></a><a name="p3825143812346"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p14825143893410"><a name="p14825143893410"></a><a name="p14825143893410"></a>待读取的数据长度。</p>
</td>
</tr>
<tr id="row3245172411562"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p132142074014"><a name="p132142074014"></a><a name="p132142074014"></a>offset</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p172141003401"><a name="p172141003401"></a><a name="p172141003401"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p521418015409"><a name="p521418015409"></a><a name="p521418015409"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p17428141613355"><a name="p17428141613355"></a><a name="p17428141613355"></a>待读取的数据偏移。</p>
</td>
</tr>
<tr id="row1982517383342"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p13776206407"><a name="p13776206407"></a><a name="p13776206407"></a>readLen</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p177530104012"><a name="p177530104012"></a><a name="p177530104012"></a>int*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p117751403401"><a name="p117751403401"></a><a name="p117751403401"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1241604574112"><a name="p1241604574112"></a><a name="p1241604574112"></a>实际读取数据长度。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>0</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>非0</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioWriteHook<a name="ZH-CN_TOPIC_0000002552740601"></a>

**函数定义<a name="section4349141610276"></a>**

劫持写接口。

**实现方法<a name="section11350171616279"></a>**

int BioWriteHook\(uint64\_t inode, char \*buff, uint64\_t count, uint64\_t offset, uint64\_t fh\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>inode</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p132151481359"><a name="p132151481359"></a><a name="p132151481359"></a>文件inode。</p>
</td>
</tr>
<tr id="row12709103323918"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1871014333394"><a name="p1871014333394"></a><a name="p1871014333394"></a>buff</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p1971063383915"><a name="p1971063383915"></a><a name="p1971063383915"></a>char*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p7710153323919"><a name="p7710153323919"></a><a name="p7710153323919"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p6710233153916"><a name="p6710233153916"></a><a name="p6710233153916"></a>待写入的数据。</p>
</td>
</tr>
<tr id="row1662115714390"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p188256383344"><a name="p188256383344"></a><a name="p188256383344"></a>count</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p38258384344"><a name="p38258384344"></a><a name="p38258384344"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p3825143812346"><a name="p3825143812346"></a><a name="p3825143812346"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p14825143893410"><a name="p14825143893410"></a><a name="p14825143893410"></a>待写入数据的长度。</p>
</td>
</tr>
<tr id="row3245172411562"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p132142074014"><a name="p132142074014"></a><a name="p132142074014"></a>offset</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p172141003401"><a name="p172141003401"></a><a name="p172141003401"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p521418015409"><a name="p521418015409"></a><a name="p521418015409"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p17428141613355"><a name="p17428141613355"></a><a name="p17428141613355"></a>待写入数据的偏移。</p>
</td>
</tr>
<tr id="row1982517383342"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p13776206407"><a name="p13776206407"></a><a name="p13776206407"></a>fh</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p177530104012"><a name="p177530104012"></a><a name="p177530104012"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p117751403401"><a name="p117751403401"></a><a name="p117751403401"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p6773209403"><a name="p6773209403"></a><a name="p6773209403"></a>文件描述符。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>0</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>非0</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioWriteCopyFreeHook<a name="ZH-CN_TOPIC_0000002521700676"></a>

**函数定义<a name="section4349141610276"></a>**

劫持免拷贝写接口。

**实现方法<a name="section11350171616279"></a>**

int BioWriteCopyFreeHook\(uint64\_t inode, uint64\_t offset, uint64\_t count, CacheSpaceDesc \*space\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>inode</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p132151481359"><a name="p132151481359"></a><a name="p132151481359"></a>文件inode。</p>
</td>
</tr>
<tr id="row3245172411562"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p132142074014"><a name="p132142074014"></a><a name="p132142074014"></a>offset</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p172141003401"><a name="p172141003401"></a><a name="p172141003401"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p521418015409"><a name="p521418015409"></a><a name="p521418015409"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p17428141613355"><a name="p17428141613355"></a><a name="p17428141613355"></a>待写入数据的偏移。</p>
</td>
</tr>
<tr id="row1982517383342"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p188256383344"><a name="p188256383344"></a><a name="p188256383344"></a>count</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p38258384344"><a name="p38258384344"></a><a name="p38258384344"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p3825143812346"><a name="p3825143812346"></a><a name="p3825143812346"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p14825143893410"><a name="p14825143893410"></a><a name="p14825143893410"></a>待写入数据的长度。</p>
</td>
</tr>
<tr id="row12716182625620"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1071672605617"><a name="p1071672605617"></a><a name="p1071672605617"></a>space</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p16716142618563"><a name="p16716142618563"></a><a name="p16716142618563"></a>CacheSpaceDesc*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p0716132635618"><a name="p0716132635618"></a><a name="p0716132635618"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p144328391361"><a name="p144328391361"></a><a name="p144328391361"></a>调用BioAllocCacheSpace成功后返回的缓存空间。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="80%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>0</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row17340203224115"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.3.1.1 "><p id="p08481734132713"><a name="p08481734132713"></a><a name="p08481734132713"></a>非0</p>
</td>
<td class="cellrowborder" valign="top" width="80%" headers="mcps1.2.3.1.2 "><p id="p819316557445"><a name="p819316557445"></a><a name="p819316557445"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioRegisterInterceptorRead<a name="ZH-CN_TOPIC_0000002521700658"></a>

**函数定义<a name="section4349141610276"></a>**

注册拦截读接口。

**实现方法<a name="section11350171616279"></a>**

void BioRegisterInterceptorRead\(ReadHook rh\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>rh</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>ReadHook</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>读钩子函数。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

无返回值。

## BioRegisterInterceptorWrite<a name="ZH-CN_TOPIC_0000002552860605"></a>

**函数定义<a name="section4349141610276"></a>**

注册劫持写接口。

**实现方法<a name="section11350171616279"></a>**

void BioRegisterInterceptorWrite\(WriteHook wh\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>wh</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>WriteHook</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>写钩子函数。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

无返回值。

## BioRegisterInterceptorWriteCopyFree<a name="ZH-CN_TOPIC_0000002521700644"></a>

**函数定义<a name="section4349141610276"></a>**

注册劫持免拷贝写接口。

**实现方法<a name="section11350171616279"></a>**

void BioRegisterInterceptorWriteCopyFree\(WriteCopyFreeHook wh\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>wh</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>WriteCopyFreeHook</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>免拷贝写钩子函数。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

无返回值。

## BioConvertLocation<a name="ZH-CN_TOPIC_0000002552860601"></a>

**函数定义<a name="section4349141610276"></a>**

转换对象位置描述接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioConvertLocation\(ObjLocation location, ObjLocationDetail \*detailLoc\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>location</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>ObjLocation</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>原始对象位置。</p>
</td>
</tr>
<tr id="row115011432131213"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1750115320129"><a name="p1750115320129"></a><a name="p1750115320129"></a>detailLoc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p65015320124"><a name="p65015320124"></a><a name="p65015320124"></a>ObjLocationDetail*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p750173241219"><a name="p750173241219"></a><a name="p750173241219"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p564215520137"><a name="p564215520137"></a><a name="p564215520137"></a>对象位置详细描述的结构参数解释参见<a href="BioInitialize.md#table0292135934712">表2</a>。</p>
</td>
</tr>
</tbody>
</table>

**表 2**  详细参数说明

<a name="table0292135934712"></a>
<table><thead align="left"><tr id="row129210597470"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.4.1.1"><p id="p3292359174711"><a name="p3292359174711"></a><a name="p3292359174711"></a>参数</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.4.1.2"><p id="p5292145918479"><a name="p5292145918479"></a><a name="p5292145918479"></a>结构体字段</p>
</th>
<th class="cellrowborder" valign="top" width="60%" id="mcps1.2.4.1.3"><p id="p1584336164816"><a name="p1584336164816"></a><a name="p1584336164816"></a>说明</p>
</th>
</tr>
</thead>
<tbody><tr id="row14292359194714"><td class="cellrowborder" rowspan="4" valign="top" width="20%" headers="mcps1.2.4.1.1 "><p id="p129255934713"><a name="p129255934713"></a><a name="p129255934713"></a>ObjLocationDetail</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.4.1.2 "><p id="p19329210105117"><a name="p19329210105117"></a><a name="p19329210105117"></a>hostMaster</p>
</td>
<td class="cellrowborder" valign="top" width="60%" headers="mcps1.2.4.1.3 "><p id="p179831633186"><a name="p179831633186"></a><a name="p179831633186"></a>主副本的主机名数组。</p>
</td>
</tr>
<tr id="row829265924718"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p4292659154713"><a name="p4292659154713"></a><a name="p4292659154713"></a>hostSlave</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p1841436144819"><a name="p1841436144819"></a><a name="p1841436144819"></a>从副本的主机名数组。</p>
</td>
</tr>
<tr id="row10747143312526"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p674733317526"><a name="p674733317526"></a><a name="p674733317526"></a>portMaster</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p129362020163"><a name="p129362020163"></a><a name="p129362020163"></a>主副本主机端口。</p>
</td>
</tr>
<tr id="row16366193618524"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p23665366522"><a name="p23665366522"></a><a name="p23665366522"></a>portSlave</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p85614110161"><a name="p85614110161"></a><a name="p85614110161"></a>从副本主机端口。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 3**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="23.580000000000002%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="76.42%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioShowCacheResource<a name="ZH-CN_TOPIC_0000002521700680"></a>

**函数定义<a name="section4349141610276"></a>**

查询系统缓存资源使用情况接口后，需要使用者调用BioFreeCacheResourcePtr接口释放查询结果的内存资源。

**实现方法<a name="section11350171616279"></a>**

CResult BioShowCacheResource\(CacheResourcesDesc \*\*nodeDesc, uint64\_t \*nodeNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>nodeDesc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>CacheResourcesDesc**</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>节点缓存资源使用情况，参数解释参见<a href="#table0292135934712">表2 详细参数说明</a>。</p>
</td>
</tr>
<tr id="row115011432131213"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1750115320129"><a name="p1750115320129"></a><a name="p1750115320129"></a>nodeNum</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p65015320124"><a name="p65015320124"></a><a name="p65015320124"></a>uint64_t*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p750173241219"><a name="p750173241219"></a><a name="p750173241219"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p564215520137"><a name="p564215520137"></a><a name="p564215520137"></a>节点数量。</p>
</td>
</tr>
</tbody>
</table>

**表 2**  详细参数说明

<a name="table0292135934712"></a>
<table><thead align="left"><tr id="row129210597470"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.4.1.1"><p id="p3292359174711"><a name="p3292359174711"></a><a name="p3292359174711"></a>参数</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.4.1.2"><p id="p5292145918479"><a name="p5292145918479"></a><a name="p5292145918479"></a>结构体字段</p>
</th>
<th class="cellrowborder" valign="top" width="60%" id="mcps1.2.4.1.3"><p id="p1584336164816"><a name="p1584336164816"></a><a name="p1584336164816"></a>说明</p>
</th>
</tr>
</thead>
<tbody><tr id="row14292359194714"><td class="cellrowborder" rowspan="9" valign="top" width="20%" headers="mcps1.2.4.1.1 "><p id="p129255934713"><a name="p129255934713"></a><a name="p129255934713"></a>CacheResourcesDesc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.4.1.2 "><p id="p19329210105117"><a name="p19329210105117"></a><a name="p19329210105117"></a>nodeId</p>
</td>
<td class="cellrowborder" valign="top" width="60%" headers="mcps1.2.4.1.3 "><p id="p179831633186"><a name="p179831633186"></a><a name="p179831633186"></a>节点ID。</p>
</td>
</tr>
<tr id="row829265924718"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p5814105418244"><a name="p5814105418244"></a><a name="p5814105418244"></a>rCacheMemCapacity</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p1841436144819"><a name="p1841436144819"></a><a name="p1841436144819"></a>读缓存内存容量。</p>
</td>
</tr>
<tr id="row10747143312526"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p789385711241"><a name="p789385711241"></a><a name="p789385711241"></a>rCacheDiskCapacity</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p129362020163"><a name="p129362020163"></a><a name="p129362020163"></a>读缓存磁盘容量。</p>
</td>
</tr>
<tr id="row16366193618524"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p371760102513"><a name="p371760102513"></a><a name="p371760102513"></a>wCacheMemCapacity</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p85614110161"><a name="p85614110161"></a><a name="p85614110161"></a>写缓存内存容量。</p>
</td>
</tr>
<tr id="row551653752019"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p51037482511"><a name="p51037482511"></a><a name="p51037482511"></a>wCacheDiskCapacity</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p151611375203"><a name="p151611375203"></a><a name="p151611375203"></a>写缓存磁盘容量。</p>
</td>
</tr>
<tr id="row16941340132017"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p184291072515"><a name="p184291072515"></a><a name="p184291072515"></a>rCacheMemUsedSize</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p149494042011"><a name="p149494042011"></a><a name="p149494042011"></a>读缓存内存使用情况。</p>
</td>
</tr>
<tr id="row9477164232010"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p67848341256"><a name="p67848341256"></a><a name="p67848341256"></a>rCacheDiskUsedSize</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p9477174212207"><a name="p9477174212207"></a><a name="p9477174212207"></a>读缓存磁盘使用情况。</p>
</td>
</tr>
<tr id="row116507458205"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p0867113742514"><a name="p0867113742514"></a><a name="p0867113742514"></a>wCacheMemUsedSize</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p156501645162014"><a name="p156501645162014"></a><a name="p156501645162014"></a>写缓存内存使用情况。</p>
</td>
</tr>
<tr id="row10630204715207"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p1328754192519"><a name="p1328754192519"></a><a name="p1328754192519"></a>wCacheDiskUsedSize</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p1963017471202"><a name="p1963017471202"></a><a name="p1963017471202"></a>写缓存磁盘使用情况。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 3**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="23.580000000000002%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="76.42%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>TurboIO服务未就绪。</p>
</td>
</tr>
<tr id="row172037508579"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p192032050205712"><a name="p192032050205712"></a><a name="p192032050205712"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row10652149303"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p15652894015"><a name="p15652894015"></a><a name="p15652894015"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p7799mcpsimp"><a name="p7799mcpsimp"></a><a name="p7799mcpsimp"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row348611251714"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p18486132515115"><a name="p18486132515115"></a><a name="p18486132515115"></a>RET_CACHE_NO_SPACE</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1348652516116"><a name="p1348652516116"></a><a name="p1348652516116"></a>空间不足。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioFreeCacheResourcePtr<a name="ZH-CN_TOPIC_0000002521860658"></a>

**函数定义<a name="section4349141610276"></a>**

释放缓存资源的内存接口。

**实现方法<a name="section11350171616279"></a>**

void BioFreeCacheResourcePtr\(CacheResourcesDesc \*\*nodeDesc, uint64\_t nodeNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>nodeDesc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>CacheResourcesDesc**</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>节点缓存资源使用情况。</p>
</td>
</tr>
<tr id="row115011432131213"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1750115320129"><a name="p1750115320129"></a><a name="p1750115320129"></a>nodeNum</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p65015320124"><a name="p65015320124"></a><a name="p65015320124"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p750173241219"><a name="p750173241219"></a><a name="p750173241219"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p564215520137"><a name="p564215520137"></a><a name="p564215520137"></a>节点数量。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

无返回值。

## BioShowCacheHitRatio<a name="ZH-CN_TOPIC_0000002521700670"></a>

**函数定义<a name="section4349141610276"></a>**

查询系统缓存命中率接口，查询结果的内存资源要求使用者调用BioFreeCacheHitPtr接口进行释放。

**实现方法<a name="section11350171616279"></a>**

CResult BioShowCacheHitRatio\(CacheHitFinalDesc \*desc, CacheHitFinalDesc \*\*nodeDesc, uint64\_t \*nodeNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p123387213400"><a name="p123387213400"></a><a name="p123387213400"></a>desc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p107531810174018"><a name="p107531810174018"></a><a name="p107531810174018"></a>CacheHitFinalDesc*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>所有节点缓存命中率。参数解释参见<a href="#table0292135934712">表2 详细参数说明</a>。</p>
</td>
</tr>
<tr id="row115011432131213"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p9556146406"><a name="p9556146406"></a><a name="p9556146406"></a>nodeDesc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p13746191314403"><a name="p13746191314403"></a><a name="p13746191314403"></a>CacheHitFinalDesc**</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p750173241219"><a name="p750173241219"></a><a name="p750173241219"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p564215520137"><a name="p564215520137"></a><a name="p564215520137"></a>各节点的缓存命中率。</p>
</td>
</tr>
<tr id="row79711511399"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p208061879404"><a name="p208061879404"></a><a name="p208061879404"></a>nodeNum</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p16886161614015"><a name="p16886161614015"></a><a name="p16886161614015"></a>uint64_t*</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p12972543916"><a name="p12972543916"></a><a name="p12972543916"></a>出参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p109818573914"><a name="p109818573914"></a><a name="p109818573914"></a>节点数量。</p>
</td>
</tr>
</tbody>
</table>

**表 2**  详细参数说明

<a name="table0292135934712"></a>
<table><thead align="left"><tr id="row129210597470"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.4.1.1"><p id="p3292359174711"><a name="p3292359174711"></a><a name="p3292359174711"></a>参数</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.4.1.2"><p id="p5292145918479"><a name="p5292145918479"></a><a name="p5292145918479"></a>结构体字段</p>
</th>
<th class="cellrowborder" valign="top" width="60%" id="mcps1.2.4.1.3"><p id="p1584336164816"><a name="p1584336164816"></a><a name="p1584336164816"></a>说明</p>
</th>
</tr>
</thead>
<tbody><tr id="row14292359194714"><td class="cellrowborder" rowspan="10" valign="top" width="20%" headers="mcps1.2.4.1.1 "><p id="p1145761024217"><a name="p1145761024217"></a><a name="p1145761024217"></a>CacheHitFinalDesc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.4.1.2 "><p id="p19329210105117"><a name="p19329210105117"></a><a name="p19329210105117"></a>nodeId</p>
</td>
<td class="cellrowborder" valign="top" width="60%" headers="mcps1.2.4.1.3 "><p id="p179831633186"><a name="p179831633186"></a><a name="p179831633186"></a>节点ID。当所有节点命中率为参数时，该字段为无效信息。</p>
</td>
</tr>
<tr id="row829265924718"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p108671529194313"><a name="p108671529194313"></a><a name="p108671529194313"></a>rCacheHitMemCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p1841436144819"><a name="p1841436144819"></a><a name="p1841436144819"></a>读缓存内存命中数。</p>
</td>
</tr>
<tr id="row10747143312526"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p149172791319"><a name="p149172791319"></a><a name="p149172791319"></a>rCacheHitDiskCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p129362020163"><a name="p129362020163"></a><a name="p129362020163"></a>读缓存磁盘命中数。</p>
</td>
</tr>
<tr id="row16366193618524"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p1343172816133"><a name="p1343172816133"></a><a name="p1343172816133"></a>rCacheHitCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p85614110161"><a name="p85614110161"></a><a name="p85614110161"></a>读缓存命中总数。</p>
</td>
</tr>
<tr id="row551653752019"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p16143081315"><a name="p16143081315"></a><a name="p16143081315"></a>rCacheTotalCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p151611375203"><a name="p151611375203"></a><a name="p151611375203"></a>读缓存查询总数。</p>
</td>
</tr>
<tr id="row129439323130"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p13943183220136"><a name="p13943183220136"></a><a name="p13943183220136"></a>wCacheHitMemCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p19943193211315"><a name="p19943193211315"></a><a name="p19943193211315"></a>写缓存内存命中数。</p>
</td>
</tr>
<tr id="row19988133561316"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p7988335111320"><a name="p7988335111320"></a><a name="p7988335111320"></a>wCacheHitDiskCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p14988935131316"><a name="p14988935131316"></a><a name="p14988935131316"></a>写缓存磁盘命中数。</p>
</td>
</tr>
<tr id="row6990133720134"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p109901237171318"><a name="p109901237171318"></a><a name="p109901237171318"></a>wCacheHitCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p15990437111318"><a name="p15990437111318"></a><a name="p15990437111318"></a>写缓存命中总数。</p>
</td>
</tr>
<tr id="row14663133991318"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p666333914130"><a name="p666333914130"></a><a name="p666333914130"></a>wCacheTotalCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p17663173915137"><a name="p17663173915137"></a><a name="p17663173915137"></a>写缓存查询总数。</p>
</td>
</tr>
<tr id="row382319417132"><td class="cellrowborder" valign="top" headers="mcps1.2.4.1.1 "><p id="p17823184191310"><a name="p17823184191310"></a><a name="p17823184191310"></a>backendHitCount</p>
</td>
<td class="cellrowborder" valign="top" headers="mcps1.2.4.1.2 "><p id="p2823441111311"><a name="p2823441111311"></a><a name="p2823441111311"></a>后端命中数。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 3**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="23.580000000000002%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="76.42%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row58862104369"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p48862010103616"><a name="p48862010103616"></a><a name="p48862010103616"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row383661333611"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p683681333615"><a name="p683681333615"></a><a name="p683681333615"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p7799mcpsimp"><a name="p7799mcpsimp"></a><a name="p7799mcpsimp"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row13371097393"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p16338594399"><a name="p16338594399"></a><a name="p16338594399"></a>RET_CACHE_NO_SPACE</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p103381396391"><a name="p103381396391"></a><a name="p103381396391"></a>空间不足。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

## BioFreeCacheHitPtr<a name="ZH-CN_TOPIC_0000002552740615"></a>

**函数定义<a name="section4349141610276"></a>**

释放缓存命中率相关的内存资源。

**实现方法<a name="section11350171616279"></a>**

void BioFreeCacheHitPtr\(CacheHitFinalDesc \*\*nodeDesc, uint64\_t nodeNum\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>nodeDesc</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>CacheHitFinalDesc**</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>各节点缓存命中率。</p>
</td>
</tr>
<tr id="row115011432131213"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p1750115320129"><a name="p1750115320129"></a><a name="p1750115320129"></a>nodeNum</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p65015320124"><a name="p65015320124"></a><a name="p65015320124"></a>uint64_t</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p750173241219"><a name="p750173241219"></a><a name="p750173241219"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p564215520137"><a name="p564215520137"></a><a name="p564215520137"></a>节点数量。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

无返回值。

## BioAddDisk<a name="ZH-CN_TOPIC_0000002552860637"></a>

**函数定义<a name="section4349141610276"></a>**

新增加盘接口。

**实现方法<a name="section11350171616279"></a>**

CResult BioAddDisk\(const char \*diskPath\)

**参数说明<a name="section13350116182717"></a>**

**表 1**  参数说明

<a name="table8350816152713"></a>
<table><thead align="left"><tr id="row1635061611274"><th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.1"><p id="p0350121618277"><a name="p0350121618277"></a><a name="p0350121618277"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.2"><p id="p93501016152714"><a name="p93501016152714"></a><a name="p93501016152714"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="20%" id="mcps1.2.5.1.3"><p id="p9350121662711"><a name="p9350121662711"></a><a name="p9350121662711"></a>参数类型</p>
</th>
<th class="cellrowborder" valign="top" width="40%" id="mcps1.2.5.1.4"><p id="p73502162275"><a name="p73502162275"></a><a name="p73502162275"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row75645203514"><td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.1 "><p id="p37829526522"><a name="p37829526522"></a><a name="p37829526522"></a>diskPath</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.2 "><p id="p12782852105215"><a name="p12782852105215"></a><a name="p12782852105215"></a>const char *</p>
</td>
<td class="cellrowborder" valign="top" width="20%" headers="mcps1.2.5.1.3 "><p id="p167824524524"><a name="p167824524524"></a><a name="p167824524524"></a>入参</p>
</td>
<td class="cellrowborder" valign="top" width="40%" headers="mcps1.2.5.1.4 "><p id="p1078215285210"><a name="p1078215285210"></a><a name="p1078215285210"></a>新增块设备路径，必须是有效的路径。</p>
</td>
</tr>
</tbody>
</table>

**返回值<a name="section1891215448215"></a>**

**表 2**  返回值说明

<a name="table175041525174011"></a>
<table><thead align="left"><tr id="row16504142574012"><th class="cellrowborder" valign="top" width="23.580000000000002%" id="mcps1.2.3.1.1"><p id="p14504625184012"><a name="p14504625184012"></a><a name="p14504625184012"></a>返回值</p>
</th>
<th class="cellrowborder" valign="top" width="76.42%" id="mcps1.2.3.1.2"><p id="p10504925164015"><a name="p10504925164015"></a><a name="p10504925164015"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1050413258405"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p550432518405"><a name="p550432518405"></a><a name="p550432518405"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1185632804415"><a name="p1185632804415"></a><a name="p1185632804415"></a>操作成功。</p>
</td>
</tr>
<tr id="row2383147204312"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p938319719436"><a name="p938319719436"></a><a name="p938319719436"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p6383167104315"><a name="p6383167104315"></a><a name="p6383167104315"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row827344774310"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p1627314764317"><a name="p1627314764317"></a><a name="p1627314764317"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1927413479432"><a name="p1927413479432"></a><a name="p1927413479432"></a>UBS IO服务未就绪。</p>
</td>
</tr>
<tr id="row44691326121718"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p246902601715"><a name="p246902601715"></a><a name="p246902601715"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p7841mcpsimp"><a name="p7841mcpsimp"></a><a name="p7841mcpsimp"></a>分区故障。</p>
</td>
</tr>
<tr id="row19970175424115"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p797015540412"><a name="p797015540412"></a><a name="p797015540412"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p15970155434117"><a name="p15970155434117"></a><a name="p15970155434117"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row168815399575"><td class="cellrowborder" valign="top" width="23.580000000000002%" headers="mcps1.2.3.1.1 "><p id="p15688939165711"><a name="p15688939165711"></a><a name="p15688939165711"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="76.42%" headers="mcps1.2.3.1.2 "><p id="p1568813945718"><a name="p1568813945718"></a><a name="p1568813945718"></a>操作失败。</p>
</td>
</tr>
</tbody>
</table>

# 错误码<a name="ZH-CN_TOPIC_0000002552860609"></a>

**表 1**  错误码

<a name="table7738mcpsimp"></a>
<table><thead align="left"><tr id="row27781349145017"><th class="cellrowborder" valign="top" width="15.879999999999999%" id="mcps1.2.4.1.1"><p id="p6778949185012"><a name="p6778949185012"></a><a name="p6778949185012"></a>错误码数</p>
</th>
<th class="cellrowborder" valign="top" width="34.01%" id="mcps1.2.4.1.2"><p id="p1677894912503"><a name="p1677894912503"></a><a name="p1677894912503"></a>错误码</p>
</th>
<th class="cellrowborder" valign="top" width="50.11%" id="mcps1.2.4.1.3"><p id="p1477854913501"><a name="p1477854913501"></a><a name="p1477854913501"></a>含义</p>
</th>
</tr>
</thead>
<tbody><tr id="row7744mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7746mcpsimp"><a name="p7746mcpsimp"></a><a name="p7746mcpsimp"></a>0</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7748mcpsimp"><a name="p7748mcpsimp"></a><a name="p7748mcpsimp"></a>RET_CACHE_OK</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7750mcpsimp"><a name="p7750mcpsimp"></a><a name="p7750mcpsimp"></a>操作成功。</p>
</td>
</tr>
<tr id="row7751mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7753mcpsimp"><a name="p7753mcpsimp"></a><a name="p7753mcpsimp"></a>1</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7755mcpsimp"><a name="p7755mcpsimp"></a><a name="p7755mcpsimp"></a>RET_CACHE_PROTECTED</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7757mcpsimp"><a name="p7757mcpsimp"></a><a name="p7757mcpsimp"></a>写缓存保护。</p>
</td>
</tr>
<tr id="row7758mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7760mcpsimp"><a name="p7760mcpsimp"></a><a name="p7760mcpsimp"></a>2</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p579172312164"><a name="p579172312164"></a><a name="p579172312164"></a>RET_CACHE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7764mcpsimp"><a name="p7764mcpsimp"></a><a name="p7764mcpsimp"></a>操作失败。</p>
</td>
</tr>
<tr id="row7765mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7767mcpsimp"><a name="p7767mcpsimp"></a><a name="p7767mcpsimp"></a>3</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p1447392519169"><a name="p1447392519169"></a><a name="p1447392519169"></a>RET_CACHE_EPERM</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7771mcpsimp"><a name="p7771mcpsimp"></a><a name="p7771mcpsimp"></a>传入参数错误。</p>
</td>
</tr>
<tr id="row7772mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7774mcpsimp"><a name="p7774mcpsimp"></a><a name="p7774mcpsimp"></a>4</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7776mcpsimp"><a name="p7776mcpsimp"></a><a name="p7776mcpsimp"></a>RET_CACHE_BUSY</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7778mcpsimp"><a name="p7778mcpsimp"></a><a name="p7778mcpsimp"></a>缓存忙碌，需要外部重试。</p>
</td>
</tr>
<tr id="row7779mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7781mcpsimp"><a name="p7781mcpsimp"></a><a name="p7781mcpsimp"></a>5</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p0144122915165"><a name="p0144122915165"></a><a name="p0144122915165"></a>RET_CACHE_NEED_RETRY</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7785mcpsimp"><a name="p7785mcpsimp"></a><a name="p7785mcpsimp"></a>需要外部重试。</p>
</td>
</tr>
<tr id="row7786mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7788mcpsimp"><a name="p7788mcpsimp"></a><a name="p7788mcpsimp"></a>6</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7790mcpsimp"><a name="p7790mcpsimp"></a><a name="p7790mcpsimp"></a>RET_CACHE_NOT_READY</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7792mcpsimp"><a name="p7792mcpsimp"></a><a name="p7792mcpsimp"></a>缓存未就绪。</p>
</td>
</tr>
<tr id="row7793mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7795mcpsimp"><a name="p7795mcpsimp"></a><a name="p7795mcpsimp"></a>7</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7797mcpsimp"><a name="p7797mcpsimp"></a><a name="p7797mcpsimp"></a>RET_CACHE_NOT_FOUND</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7799mcpsimp"><a name="p7799mcpsimp"></a><a name="p7799mcpsimp"></a>Cache实例不存在。</p>
</td>
</tr>
<tr id="row7800mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7802mcpsimp"><a name="p7802mcpsimp"></a><a name="p7802mcpsimp"></a>8</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p127218352167"><a name="p127218352167"></a><a name="p127218352167"></a>RET_CACHE_CONFLICT</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7806mcpsimp"><a name="p7806mcpsimp"></a><a name="p7806mcpsimp"></a>对象冲突。</p>
</td>
</tr>
<tr id="row7807mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7809mcpsimp"><a name="p7809mcpsimp"></a><a name="p7809mcpsimp"></a>9</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7811mcpsimp"><a name="p7811mcpsimp"></a><a name="p7811mcpsimp"></a>RET_CACHE_MISS</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7813mcpsimp"><a name="p7813mcpsimp"></a><a name="p7813mcpsimp"></a>缓存未命中。</p>
</td>
</tr>
<tr id="row7814mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7816mcpsimp"><a name="p7816mcpsimp"></a><a name="p7816mcpsimp"></a>10</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7818mcpsimp"><a name="p7818mcpsimp"></a><a name="p7818mcpsimp"></a>RET_CACHE_NO_SPACE</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7820mcpsimp"><a name="p7820mcpsimp"></a><a name="p7820mcpsimp"></a>空间不足。</p>
</td>
</tr>
<tr id="row7821mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7823mcpsimp"><a name="p7823mcpsimp"></a><a name="p7823mcpsimp"></a>11</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7825mcpsimp"><a name="p7825mcpsimp"></a><a name="p7825mcpsimp"></a>RET_CACHE_UNAVAILABLE</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7827mcpsimp"><a name="p7827mcpsimp"></a><a name="p7827mcpsimp"></a>缓存服务不可用。</p>
</td>
</tr>
<tr id="row7828mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7830mcpsimp"><a name="p7830mcpsimp"></a><a name="p7830mcpsimp"></a>12</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7832mcpsimp"><a name="p7832mcpsimp"></a><a name="p7832mcpsimp"></a>RET_CACHE_EXCEED_QUOTA</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7834mcpsimp"><a name="p7834mcpsimp"></a><a name="p7834mcpsimp"></a>超出配额限制。</p>
</td>
</tr>
<tr id="row7835mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7837mcpsimp"><a name="p7837mcpsimp"></a><a name="p7837mcpsimp"></a>13</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p02515453166"><a name="p02515453166"></a><a name="p02515453166"></a>RET_CACHE_PT_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7841mcpsimp"><a name="p7841mcpsimp"></a><a name="p7841mcpsimp"></a>分区故障。</p>
</td>
</tr>
<tr id="row7842mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7844mcpsimp"><a name="p7844mcpsimp"></a><a name="p7844mcpsimp"></a>14</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7846mcpsimp"><a name="p7846mcpsimp"></a><a name="p7846mcpsimp"></a>RET_CACHE_READ_EXCEED</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7848mcpsimp"><a name="p7848mcpsimp"></a><a name="p7848mcpsimp"></a>超出读取限制。</p>
</td>
</tr>
<tr id="row7849mcpsimp"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p7851mcpsimp"><a name="p7851mcpsimp"></a><a name="p7851mcpsimp"></a>15</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p7853mcpsimp"><a name="p7853mcpsimp"></a><a name="p7853mcpsimp"></a>RET_CACHE_EXISTS</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p7855mcpsimp"><a name="p7855mcpsimp"></a><a name="p7855mcpsimp"></a>Cache实例已存在。</p>
</td>
</tr>
<tr id="row1357244851817"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p16572548141815"><a name="p16572548141815"></a><a name="p16572548141815"></a>16</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p15572104871818"><a name="p15572104871818"></a><a name="p15572104871818"></a>RET_CACHE_DISK_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p45721848131814"><a name="p45721848131814"></a><a name="p45721848131814"></a>磁盘故障。</p>
</td>
</tr>
<tr id="row1254175114180"><td class="cellrowborder" valign="top" width="15.879999999999999%" headers="mcps1.2.4.1.1 "><p id="p3541751201817"><a name="p3541751201817"></a><a name="p3541751201817"></a>17</p>
</td>
<td class="cellrowborder" valign="top" width="34.01%" headers="mcps1.2.4.1.2 "><p id="p65465161817"><a name="p65465161817"></a><a name="p65465161817"></a>RET_CACHE_UFS_FAULT</p>
</td>
<td class="cellrowborder" valign="top" width="50.11%" headers="mcps1.2.4.1.3 "><p id="p1954185117182"><a name="p1954185117182"></a><a name="p1954185117182"></a>后端存储故障。</p>
</td>
</tr>
</tbody>
</table>

# 版权说明<a name="ZH-CN_TOPIC_0000002553669131"></a>

Copyright \(c\) Huawei Technologies Co., Ltd. 2026. All rights reserved.
