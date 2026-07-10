# UBS IO Roadmap 2026 H1

UBS IO 在 2026 年 H1 阶段的目标是发布首个商用版本，面向 LLM 推理场景提供基于计算侧 SSD 的 L3 分布式 KV Cache 缓存能力，并围绕 vLLM-Ascend 推理生态补齐 memcache 与 Mooncake 接入能力。

---

## 关键特性

### UBS IO 1.0.0 商用版本

- [x] [**2026H1**] 支持标准 KV Cache 访问能力，覆盖初始化、退出、写入、读取、删除、存在性查询、长度查询，以及批量写入、批量读取、批量删除、批量查询等操作。
- [x] [**2026H1**] 支持 standalone 本地缓存运行时，推理进程可通过 SDK 拉起本地缓存 server，减少独立服务进程和额外网络依赖。
- [x] [**2026H1**] 支持本地内存与 NVMe SSD 组成缓存空间，提供 WCache/RCache、Flow 映射、缓存水位、读写比例、日志、Trace、CRC 和 QoS 等基础配置能力。
- [x] [**2026H1**] 支持无盘/有盘两种部署形态：无盘模式仅使用本地内存缓存，有盘模式通过 `bio.disk.path` 配置本地 SSD 作为 KV Cache 扩容层。
- [x] [**2026H1**] 支持与 memcache 组合接入 vLLM-Ascend KV Cache 复用加载场景。
- [x] [**2026H1**] 提供 Mooncake KV Backend patch，用于 Mooncake Store 接入 UBSIO 后端并对接 vLLM-Ascend 场景。

### 后续计划

- [ ] [**后续**] 建立 KV Cache 关键场景性能基线。
- [ ] [**后续**] 将 NDS 直通能力纳入后续演进计划，补齐接口、部署和场景化验证说明。
