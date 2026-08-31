# ubs-io

## Introduction

UBS IO is an I/O acceleration service suite designed for various scenarios such as inference, training, and post-training. It provides acceleration capabilities like NPU Direct Storage, distributed block-storage-based KV/file cache, and block storage expansion. These capabilities help improve I/O performance in scenarios such as model weight file loading during startup and KV cache reuse and offloading.

## Software Architecture

UBS IO provides the following key features:

NPU Direct Storage (NDS): It provides direct-access interfaces for KV and files. It supports direct data read/write between the NPU HBM and local PCIe SSDs, UB-based block storage, or external storage systems interconnected through RDMA/UB, reducing redundant memory copy overhead of storage devices and CPU. NDS provides plug-in extensions compatible with mainstream open-source multi-level KV cache frameworks and can integrate with popular open-source third-party file/KV cache systems, accelerating KV cache reuse and loading.
I/O cache: It provides FUSE-based standard file I/O interfaces and native KV interfaces. It supports deployment across multiple system architectures, including local block storage based on servers and supernodes, as well as distributed storage servers with storage-compute decoupled architecture. It is natively compatible with NDS services to enable shortest-path transmission in scenarios such as KV cache and weight loading.
Block storage expansion: It provides software-hardware collaboration for block storage, including space allocation and management across multiple types of block devices, multi-stream I/O adaptation for block storage, and coordinated UB connection setup and memory registration between NPUs and block storage. It also supports interconnection with UB-based block storage pooling systems.

## How to Contribute

We welcome community developers to submit issues, PRs, or participate in discussions. Please read the contribution guide and comply with the openEuler community code of conduct.

## License

This project is licensed under Mulan PSL v2.
