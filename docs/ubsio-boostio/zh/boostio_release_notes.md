# UBSIO-BoostIO 版本说明

## 产品版本信息

<table style="undefined;table-layout: fixed; width: 435px"><colgroup>
<col style="width: 201px">
<col style="width: 234px">
</colgroup>
<thead>
  <tr>
    <th>产品</th>
    <th>版本信息</th>
  </tr></thead>
<tbody>
  <tr>
    <td>产品名称</td>
    <td>Kunpeng BoostKit</td>
  </tr>
  <tr>
    <td>产品版本</td>
    <td>26.2.0</td>
  </tr>
  <tr>
    <td>软件名称和版本</td>
    <td>UBSIO 1.1.0</td>
  </tr>
</tbody>
</table>

## 软件版本配套说明

<table style="undefined;table-layout: fixed; width: 435px"><colgroup>
<col style="width: 201px">
<col style="width: 234px">
</colgroup>
<thead>
  <tr>
    <th>项目</th>
    <th>版本配套</th>
  </tr></thead>
<tbody>
  <tr>
    <td>操作系统</td>
    <td>openEuler 22.03 LTS SP4</td>
  </tr>
  <tr>
    <td>JuiceFS</td>
    <td>1.0.3</td>
  </tr>
  <tr>
    <td>Redis</td>
    <td>4.0.11</td>
  </tr>
    <tr>
    <td>ZooKeeper</td>
    <td>3.9.3</td>
  </tr>
  <tr>
    <td>Ceph</td>
    <td>12.2.8</td>
  </tr>
    <tr>
    <td>Python</td>
    <td>3.7</td>
  </tr>
</tbody>
</table>

## 硬件版本配套说明

<table style="undefined;table-layout: fixed; width: 435px"><colgroup>
<col style="width: 201px">
<col style="width: 234px">
</colgroup>
<thead>
  <tr>
    <th>硬件</th>
    <th>版本配套</th>
  </tr></thead>
<tbody>
  <tr>
    <td>服务器名称</td>
    <td>TaiShan 200服务器</td>
  </tr>
    <tr>
    <td>处理器</td>
    <td>鲲鹏920处理器</td>
  </tr>
  <tr>
    <td>内存大小</td>
    <td>512GB</td>
  </tr>
    <tr>
    <td>内存频率</td>
    <td>2666MHz</td>
  </tr>
  <tr>
    <td>网卡</td>
    <td>RoCE 100GE<br>TCP 10GE</td>
  </tr>
    <tr>
    <td>硬盘（NVMe SSD）</td>
    <td>至少一块3.6TB或7.68TB磁盘</td>
  </tr>
</tbody>
</table>

## 更新说明

当前版本对外开源，TLS加密使用方式有所更改，包括初始化函数和配置文件。安装部署通过rpm包安装在系统目录。解决了大数据和AI场景下，远端存储距离增加导致读写性能无法充分发挥。需要在远端存储和本地之间构筑一层分布式缓存来加速IO的读写性能。计算侧分布式缓存UBS IO提供可维护性接口，支撑客户对接自有监控软件开源Prometheus，实现在现网环境中实时监控UBS IO的关键魂村详情。增加新加盘功能，提高软件系统磁盘可靠性。

## 已解决的问题

- 修复了离线升级失败问题。

- 修复了故障恢复后CRB（Cache Rebuilding Process，缓存重建流程）读存储没有被创建，系统会从后端存储读取数据的问题。

- 修复了故障盘不能恢复的问题，同时新增了加盘接口。

## 遗留问题

无

## 版本配套文档

<table style="undefined;table-layout: fixed; width: 855px"><colgroup>
<col style="width: 451px">
<col style="width: 285px">
<col style="width: 119px">
</colgroup>
<thead>
  <tr>
    <th>文档名称</th>
    <th>内容简介</th>
    <th>交付形式</th>
  </tr></thead>
<tbody>
  <tr>
    <td>《<a href="boostio_release_notes.md">UBSIO-BoostIO 版本说明</a>》</td>
    <td>本文档提供 UBSIO-BoostIO 的版本发布信息。</td>
    <td>开源仓</td>
  </tr>
  <tr>
    <td>《<a href="boostio_user_guide.md">UBSIO-BoostIO 用户指南</a>》</td>
    <td>本文档提供 UBSIO-BoostIO 特性介绍、安装部署及使用指导。</td>
    <td>开源仓</td>
  </tr>
  <tr>
    <td>《<a href="boostio_api_reference.md">UBSIO-BoostIO API 参考</a>》</td>
    <td>本文档提供 UBSIO-BoostIO API 接口说明。</td>
    <td>开源仓</td>
  </tr>
  <tr>
    <td>《<a href="boostio_security_management.md">UBSIO-BoostIO 安全指南</a>》</td>
    <td>本文档提供 UBSIO-BoostIO 的安全配置和加固说明。</td>
    <td>开源仓</td>
  </tr>
  <tr>
    <td>《<a href="boostio_security_note.md">UBSIO-BoostIO 安全说明</a>》</td>
    <td>本文档提供 UBSIO-BoostIO 的通信矩阵、运行用户和文件权限建议。</td>
    <td>开源仓</td>
  </tr>
</tbody>
</table>
