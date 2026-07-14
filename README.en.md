# UBS IO

UBS IO is an I/O acceleration suite for Ascend/NPU inference scenarios. The current commercial focus is single-node inference three-tier pooling, where UBS IO works as the SSD layer in a KV Cache hierarchy to expand local KV Cache capacity, improve hit ratio for long-context and high-reuse Agent workloads, reduce repeated Prefill computation, and improve end-to-end inference experience.

[中文](README.md) | [Contributing](CONTRIBUTING.md) | [Roadmap](ROADMAP.md) | [FAQ](FAQ.md) | [Security](SECURITY.md)

## Latest News

- [2026/06] UBS IO completed G2.5 feature integration, supporting cache read/write and optional SSD cache.
- [2026/06] UBS IO builds an L3 distributed KV Cache layer on compute-side SSDs and supports the Mooncake open-source ecosystem for HBM+DRAM+SSD three-tier cache architecture.

## Overview

UBS IO currently focuses on the vLLM-Ascend inference ecosystem and provides two integration paths: memcache integration and a Mooncake patch. UBSIO-KV exposes standard KV APIs to upper-layer applications and acts as the adapter layer between those applications and BoostIO-backed cache capabilities.

<p align="center">
  <img src="docs/images/ubsio-dram-pool-layout-v5.svg" alt="UBS IO multi-node DRAM Pool deployment view" width="92%" />
</p>

Key capabilities:

- **G2.5 cache read/write**: cache read/write capability for current inference cache scenarios.
- **KV Cache reuse**: standard KV APIs and an adapter layer for memcache and Mooncake integration.
- **vLLM-Ascend ecosystem integration**: memcache-based integration and Mooncake KV Backend patch integration.
- **Local cache runtime**: BoostIO runtime libraries, standalone mode, local memory and SSD cache configuration.

## Components

| Component | Description | Entry |
| --- | --- | --- |
| UBSIO-KV | Standard KV APIs such as `put`, `get`, `batch_put`, and `batch_get`; adapter layer between upper-layer applications and BoostIO. | [KV API reference](docs/ubsio-kv/API接口列表.md) |
| UBSIO-BoostIO | Local cache runtime, daemon, standalone mode, offline build, and packaging capability. | [BoostIO README](ubsio-boostio/README.md), [deployment guide](docs/ubsio-boostio/推理三级池化场景安装部署指南.md) |
| Mooncake patch | Mooncake Store backend integration patch for vLLM-Ascend. | [Mooncake patch guide](patches/mooncake/mooncake.md) |
| ubsio-common CLI | CLI server/client and BoostIO diagnose commands. | [CLI command reference](docs/ubsio-common/CLI命令说明.md) |

## Quick Start

- [Installation and deployment](docs/ubsio-boostio/推理三级池化场景安装部署指南.md#部署库包)
- [Configuration file](docs/ubsio-boostio/推理三级池化场景安装部署指南.md#配置文件)
- [Python example execution](examples/ubsio-kv/README.md)

## Documentation

| Category | Documents |
| --- | --- |
| BoostIO APIs | [API reference](docs/ubsio-boostio/UBS-IO%20API参考.md) |
| UBSIO-KV APIs | [KV API reference](docs/ubsio-kv/API接口列表.md) |
| CLI | [ubsio-common CLI command reference](docs/ubsio-common/CLI命令说明.md) |
| Ecosystem | [Mooncake patch guide](patches/mooncake/mooncake.md) |
| Notices | [Third-party notices](THIRD_PARTY_NOTICES.md) |

## Hardware and Software Notes

UBS IO itself does not declare mandatory hardware requirements. When UBS IO is used together with memcache, Mooncake, vLLM-Ascend, or other external components, follow the corresponding official hardware, software, driver, and version requirements.

## Contributing

Issues, pull requests, and design discussions are welcome. Please read:

- [CONTRIBUTING.md](CONTRIBUTING.md) for contribution types, branch model, PR requirements, RFC criteria, and review SLA.
- [SECURITY.md](SECURITY.md) for vulnerability reporting and disclosure.

## License

UBS IO is licensed under Mulan PSL v2. See [LICENSE](LICENSE).
