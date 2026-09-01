<div align="center">

<img src="docs/images/ubsio-home.svg" alt="UBS IO" width="37%" />

<strong>A distributed caching suite for high-performance I/O and low-latency in-memory KV workloads</strong>

[![Docs](https://img.shields.io/badge/docs-UBS%20IO-blue)](docs/README.md)
[![License](https://img.shields.io/badge/license-Mulan%20PSL%20v2-orange)](LICENSE)

[中文](README.md) | [Documentation](docs/README.md) | [Contributing](CONTRIBUTING.md) | [UBSIO-KV (release/1.2)](https://gitcode.com/openeuler/ubs-io/blob/release/1.2/README.md)

</div>

## Overview

UBS IO is an I/O acceleration suite for inference, big data, AI convergence, and latency-sensitive trading. The project uses compute-side memory, NVMe SSDs, and high-speed networks to build caching and in-memory storage capabilities, shorten application data access paths, and provide unified C APIs, cluster management, and diagnostic tools for upper-layer services.

The project contains two independent data paths that share common infrastructure:

- **UBSIO-BoostIO** is a distributed read/write cache for storage-compute decoupled deployments. It combines memory and NVMe SSD capacity with replication, eviction, backing storage, and observability capabilities.
- **UBSIO-MemStore** is a distributed in-memory KV store for low-latency services. It provides shared-memory access between processes, cross-node replication, data-change notifications, and failure recovery.
- **UBSIO-KV**, available on [`release/1.2`](https://gitcode.com/openeuler/ubs-io/blob/release/1.2/README.md), provides a unified KV Cache access layer, C/Python APIs, and single-item and batch KV operations for upper-layer applications. It organizes requests and forwards them to the UBSIO-BoostIO cache data path.

## Key Capabilities

- **Multi-tier caching**: Organizes read caches, write caches, and streaming data spaces across memory and NVMe SSDs.
- **Low-latency in-memory KV**: Supports single-item and batch Put, Get, Update, Replace, and Delete operations.
- **Cluster and replica management**: Uses node and partition views to maintain data distribution, service state, and replica consistency.
- **Multiple communication paths**: Supports shared-memory IPC within a node and RDMA/UB RPC across nodes.
- **Operations and diagnostics**: UBSIO-Common CLI and Trace components provide diagnostic entry points for UBSIO-BoostIO and UBSIO-MemStore.

## Components

| Component | Responsibility | Entry |
| --- | --- | --- |
| UBSIO-BoostIO | Distributed cache, Cache/Flow management, BDM disk backend, and UnderFS integration | [User guide](docs/ubsio-boostio/zh/boostio_user_guide.md) · [API reference](docs/ubsio-boostio/zh/boostio_api_reference.md) |
| UBSIO-MemStore | Distributed in-memory KV, shared-memory index, replication, notifications, and CRB recovery | [Feature guide](docs/ubsio-memstore/zh/memstore_user_guide.md) · [API reference](docs/ubsio-memstore/zh/memstore_api_reference.md) |
| UBSIO-KV | C/Python KV Cache APIs with validation, key hashing, and batch request organization | [Read the release/1.2 README](https://gitcode.com/openeuler/ubs-io/blob/release/1.2/README.md) · [API reference](https://gitcode.com/openeuler/ubs-io/blob/release/1.2/docs/ubsio-kv/API接口列表.md) |
| UBSIO-Common | Shared CLI and tracing infrastructure for UBSIO-BoostIO and UBSIO-MemStore | [Source](ubsio-common/) |

## Architecture Overview

### UBSIO-BoostIO

UBSIO-BoostIO builds a distributed caching data path on the compute side using memory, NVMe SSDs, and backing storage. See the [UBSIO-BoostIO user guide](docs/ubsio-boostio/zh/boostio_user_guide.md) for detailed component responsibilities and data flows.

<div align="center">
<img src="docs/images/boostio架构图.png" alt="UBSIO-BoostIO architecture overview" width="72%" />
</div>

#### System Optimizations

1. **I/O cache acceleration**: Uses idle compute-side memory and disk resources to build a distributed multi-tier read/write cache that reduces data access latency.
2. **End-to-end optimization**: Optimizes the complete I/O path with JuiceFS and provides data affinity, zero-copy, and user-space FUSE interception.
3. **Flow-control optimization**: Uses cache quotas and token buckets for load control and restructures timeout handling to significantly increase application concurrency.
4. **Multiple backend storage systems**: Uses a generic object-storage interface for data eviction and supports mainstream storage systems.
5. **Multiple cache strategies**: Supports local affinity and global balancing, write-back and write-through modes, and directory-level cache policies.
6. **RDMA/UB acceleration**: Uses one-sided RDMA/UB operations through HCOM for I/O forwarding and high-speed data transfer.

#### Software Architecture

##### Data Engine

The UBSIO-BoostIO data engine consists of a multi-tier read/write cache and its supporting modules:

- **Cache**: A decoupled read/write caching framework that provides two cache tiers using local DRAM and NVMe SSDs, manages cached data, and supports configurable cache policies and resources.
- **FLOW**: Provides linear data spaces and append writes across memory and disks, converting random I/O into sequential I/O to improve disk-access performance.
- **BDM**: Manages pooled disk capacity and disk I/O, provides high-performance asynchronous I/O through `io_uring`, and supports future integration with pooled Lingqu SSU servers.
- **UFS**: Manages high-capacity backend storage, supports Ceph and HDFS, and allows custom backend-storage extensions.
- **HCOM**: Provides unified IPC and RPC message transport over TCP, RDMA, and UB.
- **CM**: Builds cluster-management capabilities on open source ZooKeeper and supports very large clusters through multiple pools.

##### Metadata Engine

Uses the metadata engines supported by JuiceFS; the current configuration uses Redis.

##### JuiceFS

1. **Data I/O path optimization**: Enables the UBSIO-BoostIO cache to replace the native JuiceFS data buffer and single-node cache.
2. **POSIX interface interception**: Uses `LD_PRELOAD` to intercept POSIX calls, bypass user-space FUSE overhead, and provide end-to-end zero-copy capabilities.

### UBSIO-MemStore

UBSIO-MemStore provides intra-node access and cross-node replication through a lightweight client, shared-memory indexes, and memory-management processes. See the [UBSIO-MemStore user guide](docs/ubsio-memstore/zh/memstore_user_guide.md) for detailed component responsibilities and interactions.

<div align="center">
<img src="docs/images/mms架构图.png" alt="UBSIO-MemStore architecture overview" width="75%" />
</div>

#### Technical Results

1. **Multi-replica in-memory KV cache**: On a Kunpeng SuperNode, synchronous writes to eight replicas achieve approximately 10 μs latency for 1 KB values, reads complete in under 1 μs, and per-node throughput exceeds 200,000 operations per second.
2. **Reliable and fault-tolerant UB multicast**: Provides self-healing and fault tolerance for network disconnections and read/write failures, detects faults in under one second, and completes data-migration recovery in under two minutes.

## Quick Start

Build UBSIO-BoostIO:

```bash
cd ubsio-boostio
bash build.sh -t debug
```

Build UBSIO-MemStore:

```bash
cd ubsio-memstore
bash build.sh -t debug
```

See the component documents for prerequisites, deployment, and configuration:

- [UBSIO-BoostIO user guide](docs/ubsio-boostio/zh/boostio_user_guide.md)
- [UBSIO-MemStore deployment guide](docs/ubsio-memstore/zh/memstore_deployment_guide.md)

## Tests

```bash
cd ubsio-boostio/test/llt
bash run_dt.sh
```

```bash
cd ubsio-memstore/test/llt
bash run_dt.sh
```

The scripts rebuild their component and generate coverage results. Install the documented build tools and third-party dependencies before running them.

## Documentation

All component-level product documentation is under the top-level [`docs/`](docs/README.md) directory:

- [`docs/ubsio-boostio/zh/`](docs/ubsio-boostio/zh/) contains UBSIO-BoostIO user, API, security, and release documents.
- [`docs/ubsio-memstore/zh/`](docs/ubsio-memstore/zh/) contains UBSIO-MemStore feature, deployment, API, performance, and security documents.

## Contributing

Issues and pull requests are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md), keep each change focused, add the necessary tests and documentation, and record the validation commands in the pull request.

## License

UBS IO source code is licensed under [Mulan PSL v2](LICENSE). The product documentation license is available at [`docs/LICENSE`](docs/LICENSE).
