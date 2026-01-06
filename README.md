# ubs-io

#### 介绍
UBS IO是面向推理、训练、后训练等多种场景的IO加速服务套件，提供NPU直通存储、基于块存储的分布式KV/文件缓存和块存储扩展特性等加速能力，支持模型权重文件启动加载、KV Cache复用卸载等场景IO性能提升。

#### 软件架构
UBS IO目前主要包括以下关键特性：

NDS(NPU Direct Storage)：提供KV和文件直通接口，支持本地PCIE SSD、基于UB的块存储和基于RDMA/UB互连的外置存储系统数据直通读写NPU HBM，减少存储设备与CPU侧冗余的内存拷贝开销。NDS提供适配主流开源多层级KV Cache缓存框架的插件扩展，可适配业界主流的开源三方文件/KV缓存系统，加速KV Cache数据的复用加载。
IO缓存：提供基于FUSE的标准文件IO接口和原生KV接口，支持基于服务器、超节点的本地块存储和存算分离的分布式存储服务器等多种系统架构部署，原生支持适配NDS服务，实现KV Cache、权重加载等场景的最短路径传输。
块存储扩展特性：提供对块存储的软硬协同功能，包括多类型块设备的空间分配和管理、块存储多流IO适配、NPU/块存储 UB建链和内存注册协同等，支持与块存储UB池化系统对接。


#### 安装教程

1.  xxxx
2.  xxxx
3.  xxxx

#### 使用说明

1.  xxxx
2.  xxxx
3.  xxxx

#### 参与贡献

1.  Fork 本仓库
2.  新建 Feat_xxx 分支
3.  提交代码
4.  新建 Pull Request


#### 特技

1.  使用 Readme\_XXX.md 来支持不同的语言，例如 Readme\_en.md, Readme\_zh.md
2.  Gitee 官方博客 [blog.gitee.com](https://blog.gitee.com)
3.  你可以 [https://gitee.com/explore](https://gitee.com/explore) 这个地址来了解 Gitee 上的优秀开源项目
4.  [GVP](https://gitee.com/gvp) 全称是 Gitee 最有价值开源项目，是综合评定出的优秀开源项目
5.  Gitee 官方提供的使用手册 [https://gitee.com/help](https://gitee.com/help)
6.  Gitee 封面人物是一档用来展示 Gitee 会员风采的栏目 [https://gitee.com/gitee-stars/](https://gitee.com/gitee-stars/)
