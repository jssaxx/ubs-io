# NDS API

> NDS 原语以 openEuler 内核仓 `openEuler-26.09` 分支的 `tools/lib/nds/file_p2p/nds_api.h` 和 `nds_api.c` 为准。GDS（GPUDirect Storage）接口以 `libcufile-dev_1.9.1.3~12.4.1-8.2` 中的 `cufile.h` 为准。[XDS 构建](../../ubsio-nds/README.md#构建)

目录

- [1. 接口总览](#1-接口总览)
- [2. 接口对比：GDS ↔ NDS](#2-接口对比gds--nds)
- [3. 接口使用实例](#3-接口使用实例)

---

# 1. 接口总览

## 参考

- GDS 侧来源：NVIDIA cuFile C API（`cufile.h` v1.9.1.3）。
- NDS 用户态库通过 `linux/nds_p2p.h` 定义的内核 UAPI 执行 NPU HBM/CMB 与 NVMe 之间的 P2P I/O。[XDS UAPI](https://atomgit.com/openeuler/kernel/blob/openEuler-26.09/tools/include/uapi/linux/nds_p2p.h)

| Purpose（用途）                              | GDS API                                                                                                                                                 | NDS API                                                                                                                   |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| 初始化用户态库 / 驱动                             | `CUfileError_t cuFileDriverOpen(void)`                                                                                                                  | `int nds_init(struct nds_init_param *param)`                                                                              |
| 卸载用户态库 / 驱动                              | `CUfileError_t cuFileDriverClose(void)`                                                                                                                 | `int nds_exit(void)`                                                                                                      |
| 注册已打开 fd 为 cuFile 句柄                     | `CUfileError_t cuFileHandleRegister(CUfileHandle_t *fh, CUfileDescr_t *descr)`                                                                          | NA（先调用 `nds_register_fs` 注册拓扑；I/O 直接使用裸 fd，见 §2.3）                                                                |
| 注销 cuFile 文件句柄                           | `void cuFileHandleDeregister(CUfileHandle_t fh)`                                                                                                        | NA（NDS 不持有文件句柄，fd 由调用方自己关闭）                                                                                               |
| 注册 GPU/Buffer 内存用于高效 I/O | `CUfileError_t cuFileBufRegister(const void *bufPtr_base, size_t length, int flags)` | `int nds_register_mem(void *addr, uint64_t size, int flags)` |
| 注销已注册的 buffer | `CUfileError_t cuFileBufDeregister(const void *bufPtr_base)` | `int nds_unregister_mem(void *addr, uint64_t size, int flags)` |
| 创建批处理（batch/queue）I/O 上下文                | `CUfileError_t cuFileBatchIOSetUp(CUfileBatchHandle_t *batch_idp, unsigned nr)`                                                                         | `int nds_io_new_ctx(const struct nds_io_ctx_param *param, struct nds_io_ctx **ctx)`                                       |
| 销毁批处理 I/O 上下文                            | `void cuFileBatchIODestroy(CUfileBatchHandle_t batch_idp)`                                                                                              | `int nds_io_destroy_ctx(struct nds_io_ctx *ctx)`                                                                          |
| 提交多个异步 I/O 请求                            | `CUfileError_t cuFileBatchIOSubmit(CUfileBatchHandle_t batch_idp, unsigned nr, CUfileIOParams_t *iocbp, unsigned int flags)`                            | `int nds_io_submit(struct nds_io_ctx *ctx, int nr, const struct nds_io_cb *iocb)`                                         |
| 获取已提交批处理的完成状态                            | `CUfileError_t cuFileBatchIOGetStatus(CUfileBatchHandle_t batch_idp, unsigned min_nr, unsigned *nr, CUfileIOEvents_t *iocbp, struct timespec *timeout)` | `int nds_io_getevents(struct nds_io_ctx *ctx, int min_nr, int nr, struct nds_io_event *events, struct timespec *timeout)` |
| 同步读                                      | `ssize_t cuFileRead(CUfileHandle_t fh, void *bufPtr_base, size_t size, off_t file_offset, off_t bufPtr_offset)`                                         | NA（NDS 仅提供异步 I/O，无同步 read）                                                                                                |
| 同步写                                      | `ssize_t cuFileWrite(CUfileHandle_t fh, const void *bufPtr_base, size_t size, off_t file_offset, off_t bufPtr_offset)`                                  | NA（NDS 仅提供异步 I/O，无同步 write）                                                                                               |
| （可选）批处理取消                                | `CUfileError_t cuFileBatchIOCancel(CUfileBatchHandle_t batch_idp)`                                                                                      | NA                                                                                                                        |
| Driver 会话属性查询/设置（PollMode/MaxIOSize/缓存等） | `cuFileDriverGetProperties / SetPollMode / SetMaxDirectIOSize / SetMaxCacheSize / SetMaxPinnedMemSize`                                                  | NA                                                                                                                        |
| 版本查询 | `CUfileError_t cuFileGetVersion(int *version)` | `nds_init_param.version` 输出 `NDS_API_VERSION` |
| 驱动引用计数查询                                 | `long cuFileUseCount(void)`                                                                                                                             | NA                                                                                                                        |

> 说明：NDS 先调用 `nds_init` 初始化，再调用 `nds_register_fs` 注册目标拓扑；NDS **没有**与 `cuFileHandleRegister/Deregister` 对应的文件句柄概念，I/O 使用调用方持有的裸 fd（`nds_io_obj.fd`）。

---

# 2. 接口对比：GDS ↔ NDS

> 逐对说明语义差异、参数差异与返回约定。GDS 返回 `CUfileError_t`（`err` 为 `CUfileOpError`，成功为 `CU_FILE_SUCCESS`）；
> NDS 一律返回 `int`（`0`/正数成功，`-errno` 失败）。

## 2.1 初始化：`cuFileDriverOpen` ↔ `nds_init`

```c
// GDS：无参数，全局驱动初始化
CUfileError_t cuFileDriverOpen(void);
```

**结构体定义**

```c
/* —— NDS 侧（GDS cuFileDriverOpen 无参数、无结构体）—— */
struct nds_fs_desc {
    int32_t  *fs_fd;      /* 指向 fd 数组 */
    uint32_t  fs_fd_cnt;  /* 至少 1，每个元素是文件或块设备 fd */
    uint32_t  reserved;   /* 必须为 0 */
};

struct nds_init_param {
    uint32_t flags;          /* 必须为 0 */
    uint64_t reserved[4];    /* 必须清零 */
    uint32_t version;        /* 输出 NDS_API_VERSION */
};
```

调用约定：

```c
// NDS：初始化后另行注册拓扑；成功后 param->version 被写回 NDS_API_VERSION
int nds_init(struct nds_init_param *param);
int nds_register_fs(const struct nds_fs_desc *desc);
```

| 维度   | GDS                            | NDS                                                                   |
| ---- | ------------------------------ | --------------------------------------------------------------------- |
| 参数   | 无                              | `nds_init_param`：`flags`（恒为 0）、`reserved`（清零）、`version`（输出） |
| 语义   | 打开 nvidia-fs 驱动、初始化 cuFile 会话  | 初始化 NDS 会话；另行调用 `nds_register_fs` 注册目标拓扑 |
| 拓扑   | 显式 `cuFileHandleRegister` 逐个注册 | `nds_register_fs` 可在初始化后注册拓扑 |
| 返回   | `CU_FILE_SUCCESS` 或错误码         | `0` 成功 / `-errno` 失败                                                  |
| 线程安全 | 无                              | **非线程安全、非重入**：单线程调用一次，并发 init 未定义                                     |
| 限制   | ——                             | `flags` 和 `reserved` 必须为 0；注册拓扑时 `fs_fd_cnt>=1` 且 `fs_fd` 非空 |

## 2.2 卸载：`cuFileDriverClose` ↔ `nds_exit`

```c
CUfileError_t cuFileDriverClose(void);   // GDS
int nds_exit(void);                      // NDS，返回 int
```

- 两侧均为无参数、无结构体。
- 两者都释放库/驱动资源。
- NDS 额外：自动注销所有已注册的内存并释放资源；恒返回 `0`。
- GDS 若仍有活动 I/O 返回 `CU_FILE_DRIVER_CLOSING`。

## 2.3 文件句柄注册：`cuFileHandleRegister` ↔ （无）

```c
CUfileError_t cuFileHandleRegister(CUfileHandle_t *fh, CUfileDescr_t *descr);
```

**结构体定义（GDS 侧）**

```c
typedef void* CUfileHandle_t;   /* 不透明文件句柄 */

enum CUfileFileHandleType {
    CU_FILE_HANDLE_TYPE_OPAQUE_FD    = 1, /* Linux fd */
    CU_FILE_HANDLE_TYPE_OPAQUE_WIN32 = 2, /* Windows（不支持） */
    CU_FILE_HANDLE_TYPE_USERSPACE_FS = 3, /* 用户态文件系统 */
};

typedef struct CUfileDescr_t {
    enum CUfileFileHandleType type;  /* 被注册文件的类型 */
    union {
        int  fd;      /* Linux  */
        void *handle; /* Windows */
    } handle;
    const CUfileFSOps_t *fs_ops;     /* 文件系统操作表 */
} CUfileDescr_t;
```

> `CUfileDescr_t.fs_ops` 指向用户态文件系统操作表 `CUfileFSOps_t`（仅 `CU_FILE_HANDLE_TYPE_USERSPACE_FS` 时使用，fd 路径下为 NULL）。NDS 侧无对应结构体。

- GDS：必须先把 fd 注册成 cuFile 句柄，之后的 `cuFileRead/Write` 都持 `fh`。要求文件以 `O_DIRECT` 打开。

- NDS：**没有对应接口**。文件不“注册为句柄”——

  1. 调用 `nds_init` 后通过 `nds_register_fs` 注册底层块设备拓扑；

  2. 每次提交在 `nds_io_cb.obj.fd` 里放调用方自己的裸 fd。
  - 因此上表中的 "NA" 表示 NDS 不创建文件句柄；拓扑注册由 `nds_register_fs` 完成。

## 2.4 文件句柄注销：`cuFileHandleDeregister` ↔ （无）

```c
void cuFileHandleDeregister(CUfileHandle_t fh);   // GDS，返回 void
```

- 参数 `fh` 为 `typedef void* CUfileHandle_t`（见 §2.3）；无其他结构体。NDS 无对应物：没有句柄概念，fd 的生命周期完全由调用方负责（自己 `close()` 即可）。

## 2.5 内存注册：`cuFileBufRegister` ↔ `nds_register_mem`

```c
CUfileError_t cuFileBufRegister(const void *bufPtr_base, size_t length, int flags);
int nds_register_mem(void *addr, uint64_t size, int flags);
```

- 两侧均无结构体，直接传「基地址 + 长度 + 标志位」。可用标志位定义：

```c
/* GDS 侧 */
#define CU_FILE_RDMA_REGISTER 1           /* cuFileBufRegister flags */
#define CU_FILE_RDMA_RELAXED_ORDERING (1 << 1)

/* NDS 侧：仅接受 flags == 0 */
```

| 维度     | GDS                                                             | NDS                                                               |
| ------ | --------------------------------------------------------------- | ----------------------------------------------------------------- |
| 语义 | 把 GPU（或 host）内存注册为 GDS 可 DMA 的 buffer | 按地址和长度逐段注册，同一进程可调用多次 |
| handle | 无显式句柄，后续用同一 bufPtr | 库记录每次内核注册返回的独立 handle。提交时由首个 iov 选择 handle |
| flags  | `CU_FILE_RDMA_REGISTER` 等                                       | 必须为 `0`                                                           |
| 返回     | `CU_FILE_SUCCESS`/错误码（重复注册→`CU_FILE_MEMORY_ALREADY_REGISTERED`） | `0`/`-errno`                                                      |
| 特殊     | ——                                                              | `addr` 允许为 `0`（CMB 窗口自 VA 0 起）；`size==0` 或 `flags!=0` → `-EINVAL` |

设置 `NDS_IO_F_REGISTERED_MEM` 时，一个 IOCB 的所有 iov 都须完整落在首个 iov 选中的注册区间内。

## 2.6 内存注销：`cuFileBufDeregister` ↔ `nds_unregister_mem`

```c
CUfileError_t cuFileBufDeregister(const void *bufPtr_base);
int nds_unregister_mem(void *addr, uint64_t size, int flags);
```

- NDS 按注册时的地址和大小逐段注销，`flags` 传 `0`。地址不存在返回 `-ENOENT`，大小不匹配返回 `-EINVAL`。注销成功后可释放对应内存。

## 2.7 创建 I/O 上下文：`cuFileBatchIOSetUp` ↔ `nds_io_new_ctx`

```c
CUfileError_t cuFileBatchIOSetUp(CUfileBatchHandle_t *batch_idp, unsigned nr);
int nds_io_new_ctx(const struct nds_io_ctx_param *param, struct nds_io_ctx **ctx);
```

**结构体定义**

```c
/* —— GDS 侧 —— */
typedef void* CUfileBatchHandle_t;   /* 不透明批处理句柄 */

/* —— NDS 侧 —— */
struct nds_io_ctx_param {
    uint32_t max_io_cnt;   /* 接受 1..NDS_IO_MAX_IO_CNT，仅作队列容量提示，不作强制预算 */
    uint32_t flags;        /* 必须为 0 */
    uint64_t reserved;     /* 必须为 0 */
};

struct nds_io_ctx;         /* 不透明类型；通过 nds_io_new_ctx / nds_io_destroy_ctx 管理生命周期 */
```

- 都创建一个批处理/队列上下文，句柄经出参返回。
- NDS 的 `ctx` 是二级指针 `struct nds_io_ctx **ctx`，句柄经出参返回。
- 参数：GDS 直接给 `nr`；NDS 给 `nds_io_ctx_param`（`max_io_cnt` 为 1..1024 的容量提示，`flags`/`reserved` 必须 0）。

## 2.8 销毁 I/O 上下文：`cuFileBatchIODestroy` ↔ `nds_io_destroy_ctx`

```c
void cuFileBatchIODestroy(CUfileBatchHandle_t batch_idp);   // GDS，void
int nds_io_destroy_ctx(struct nds_io_ctx *ctx);             // NDS，int
```

- 参数分别为 `typedef void* CUfileBatchHandle_t`（见 §2.7）与不透明 `struct nds_io_ctx`（见 §2.7）。
- NDS 销毁时等待未完成的 I/O 全部完成后释放 `ctx`。
- 规定：禁止在 `nds_io_getevents` 与 `destroy_ctx` 并发调用同一 ctx。
- 返回：`0` 成功，否则 `-errno`。

## 2.9 提交：`cuFileBatchIOSubmit` ↔ `nds_io_submit`

```c
CUfileError_t cuFileBatchIOSubmit(CUfileBatchHandle_t batch_idp, unsigned nr,
                                  CUfileIOParams_t *iocbp, unsigned int flags);
int nds_io_submit(struct nds_io_ctx *ctx, int nr, const struct nds_io_cb *iocb);
```

**结构体定义**

```c
/* —— GDS 侧 —— */
typedef enum CUfileOpcode {
    CUFILE_READ  = 0,
    CUFILE_WRITE = 1,
} CUfileOpcode_t;

typedef enum cufileBatchMode {
    CUFILE_BATCH = 1,
} CUfileBatchMode_t;

/* 单个批处理 I/O 描述：单 buffer + fh + opcode + cookie */
typedef struct CUfileIOParams {
    CUfileBatchMode_t mode;   /* 必须为第一个字段 */
    union {
        struct {
            void *devPtr_base;  /* 设备或主机内存指针 */
            off_t file_offset;
            off_t devPtr_offset;
            size_t size;
        } batch;
    } u;
    CUfileHandle_t fh;        /* typedef void* CUfileHandle_t（见 §2.3） */
    CUfileOpcode_t opcode;
    void *cookie;             /* 完成时回填到 CUfileIOEvents_t */
} CUfileIOParams_t;

/* —— NDS 侧 —— */
#define NDS_IO_OP_PREAD   0u   /* 读一个或多个 buffer（file 或 block dev） */
#define NDS_IO_OP_PWRITE  1u   /* 写一个或多个 buffer（仅 block dev） */
#define NDS_IO_F_REGISTERED_MEM (1u << 0)  /* buffer 已通过 nds_register_mem() 注册 */
#define NDS_IO_F_MASK            NDS_IO_F_REGISTERED_MEM

struct nds_io_obj {          /* I/O 目标 */
    uint64_t reserved;   /* 必须为 0 */
    union {
        uint64_t _data;  /* 不使用 fd 时置 0 */
        int32_t  fd;     /* 已打开的文件 / 块设备 fd（唯一支持形式） */
    };
};

struct nds_io_vec {          /* 一段 scatter-gather 窗口 */
    uint64_t buf_addr;   /* HBM/CMB 虚拟地址 */
    uint32_t buf_len;    /* 本段长度 */
    uint32_t reserved;   /* 必须为 0 */
};

struct nds_io_cb {           /* 一个逻辑 I/O */
    uint32_t                opcode;    /* NDS_IO_OP_* */
    uint32_t                rw_flags;  /* NDS_IO_F_* 位掩码 */
    struct nds_io_obj       obj;       /* 目标 fd */
    uint64_t                offset;    /* 目标对象的字节偏移 */
    uint64_t                user_data; /* 不透明 cookie，完成时原样回填 */
    const struct nds_io_vec *iov;      /* 指向 nds_io_vec[] 数组 */
    uint32_t                iov_cnt;   /* iov 段数（<= NDS_IO_MAX_IOV） */
    int32_t                 host_pid;  /* 一次性 HBM VA 的属主；0=当前进程；REGISTERED_MEM 时必须为 0 */
    uint64_t                reserved[2];  /* 必须为 0 */
};
```

| 维度       | GDS                                                                                       | NDS                                                                                                              |
| -------- | ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| 参数       | `nr`（unsigned）、`iocbp[]`、`flags`                                                          | `nr`（int）、`iocb[]`（const 指针）                                                                                     |
| 单 I/O 描述 | `CUfileIOParams_t`：单 buffer（`devPtr_base/devPtr_offset/size`）+ `fh` + `opcode` + `cookie` | `nds_io_cb`：支**scatter-gather**（`iov[]/iov_cnt`）+ 裸 `fd` + `opcode` + `user_data` + 可选 `REGISTERED_MEM`          |
| 多文件      | 一个 batch 通常同 fh，不同 iocb 可不同 buffer                                                        | 不同 iocb 可指向**不同文件**（每个 iocb 独立提交，不做跨 iocb 合并）                                                                    |
| 返回       | `CU_FILE_SUCCESS`（整体入队结果）                                                                 | **类 Linux `io_submit` 的防错停（fail-stop）**：部分成功后返回已接受个数（正数）；一个都没接受返回 `-errno`；已接受的 I/O 不回滚，须用 `nds_io_getevents` 收割 |
| 常量       | `CUFILE_READ/CUFILE_WRITE`                                                                | `NDS_IO_OP_PREAD/NDS_IO_OP_PWRITE`（写仅 block dev）                                                                 |

## 2.10 取完成：`cuFileBatchIOGetStatus` ↔ `nds_io_getevents`

```c
CUfileError_t cuFileBatchIOGetStatus(CUfileBatchHandle_t batch_idp, unsigned min_nr,
                                     unsigned *nr, CUfileIOEvents_t *iocbp, struct timespec *timeout);
int nds_io_getevents(struct nds_io_ctx *ctx, int min_nr, int nr,
                     struct nds_io_event *events, struct timespec *timeout);
```

**结构体定义**

```c
/* —— GDS 侧：完成事件 —— */
typedef enum CUFILEStatus_enum {
    CUFILE_WAITING  = 0x000001, /* 提交前必需 */
    CUFILE_PENDING  = 0x000002, /* 已入队 */
    CUFILE_INVALID  = 0x000004, /* 请求非法或未能入队 */
    CUFILE_CANCELED = 0x000008, /* 已取消 */
    CUFILE_COMPLETE = 0x000010, /* 已完成 */
    CUFILE_TIMEOUT  = 0x000020, /* 超时 */
    CUFILE_FAILED   = 0x000040  /* 无法完成 */
} CUfileStatus_t;

typedef struct CUfileIOEvents {
    void *cookie;             /* 提交时提供的 cookie */
    CUfileStatus_t status;    /* 操作状态（见上） */
    size_t ret;               /* 负值为错误，或完成的字节数 */
} CUfileIOEvents_t;

/* —— NDS 侧：完成事件（48B） —— */
struct nds_io_event {
    uint64_t user_data;      /* 提交时的 cookie */
    int64_t  res;            /* ==0 成功；否则 -errno */
    uint64_t reserved[4];    /* 必须为 0 */
};
```

| 维度      | GDS                                               | NDS                                                             |
| ------- | ------------------------------------------------- | --------------------------------------------------------------- |
| 请求数量    | `min_nr` + `unsigned *nr`（出参，返回实际数量）              | `min_nr` + `nr`（入参，`events` 数组容量上界），返回值为实际数量                    |
| 完成事件    | `CUfileIOEvents_t`：`cookie` + `status`（枚举）+ `ret` | `nds_io_event`：`user_data` + `res`（==0 或 -errno）+ `reserved[4]` |
| timeout | 支持                                                | `NULL`=永久阻塞；`{0,0}`=非阻塞；否则换算 `timeout_ns`                       |
| 空队列     | ——                                                | **无 outstanding I/O 时立即返回 0**（即使 `min_nr>0` 也不等待）               |
| 上下文     | ——                                                | 禁止与 `nds_io_destroy_ctx` 在同一 ctx 并发                             |
| 签名      | `iocbp` 数组指针                                      | `events`（复数）、参数类型为 `int`                                        |

## 2.11 同步读/写：`cuFileRead` / `cuFileWrite` ↔ （无）

```c
ssize_t cuFileRead (CUfileHandle_t fh, void *bufPtr_base, size_t size,
                    off_t file_offset, off_t bufPtr_offset);
ssize_t cuFileWrite(CUfileHandle_t fh, const void *bufPtr_base, size_t size,
                    off_t file_offset, off_t bufPtr_offset);
```

- GDS 侧为标量参数（fd 句柄 + 指针 + size + 偏移），无专门结构体。
- NDS **只做异步**，不提供任何同步 `pread/pwrite` 辅助函数。需要同步语义的调用方自建：`nds_io_submit` 1 个 iocb + `nds_io_getevents` 阻塞收割。

## 2.12 其他无对应 NDS 接口

| GDS API                                                                                                        | 说明                                                              |
| -------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- |
| `cuFileReadAsync` / `cuFileWriteAsync`                                                                         | CUDA 流异步，NDS 无流概念                                               |
| `cuFileStreamRegister` / `cuFileStreamDeregister`                                                              | 流注册/注销，NDS 无对应                                                  |
| `cuFileBatchIOCancel`                                                                                          | 批处理取消，NDS 无对应                                                   |
| `cuFileDriverGetProperties` / `SetPollMode` / `SetMaxDirectIOSize` / `SetMaxCacheSize` / `SetMaxPinnedMemSize` | 驱动会话调优，NDS 无对应（无缓冲、无 poll 开关配置）                                 |
| `cuFileGetVersion` | `nds_init_param.version` 输出 `NDS_API_VERSION` |
| `cuFileUseCount`                                                                                               | 驱动引用计数，NDS 无对应                                                  |

---

# 3. 接口使用实例

> 以下示例按 `nds_api.h` 的推荐用法编写，编译时不依赖 GDS。
> 约定：初始化后把目标 NVMe/文件所在拓扑的 fd 交给 `nds_register_fs`；
> 对齐要求：offset / 长度 / buffer 地址须 512B 对齐（见头文件说明）。

## 3.1 完整生命周期 + 异步读（含内存注册）

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "nds_api.h"

static void check_result(int result, int expected, const char *operation)
{
    if (result != expected) {
        fprintf(stderr, "%s returned %d, expected %d\n", operation, result, expected);
        exit(EXIT_FAILURE);
    }
}

#define CHECK_RESULT(call, expected) check_result((call), (expected), #call)

int main(void) {
    int file_fd = open("/data/foo.bin", O_RDONLY | O_DIRECT);
    if (file_fd < 0) { perror("open"); return EXIT_FAILURE; }

    /* 1. 初始化并注册 file_fd 的底层块设备拓扑 */
    int topo_fds[] = { file_fd };
    struct nds_init_param ip = { 0 };
    CHECK_RESULT(nds_init(&ip), 0);
    CHECK_RESULT((int)ip.version, NDS_API_VERSION);
    struct nds_fs_desc fs = { .fs_fd = topo_fds, .fs_fd_cnt = 1 };
    CHECK_RESULT(nds_register_fs(&fs), 0);

    /* 2. （可选）注册一段 HBM/CMB 窗口，便于复用，避免每次 pin */
    /*    线程局部 HBM 指针假设为 hbm_buf；地址可为 0（CMB 场景） */
    /*    void *hbm_buf = ...; size_t hbm_size = 1 << 22; */
    /*    CHECK_RESULT(nds_register_mem(hbm_buf, hbm_size, 0), 0); */

    /* 3. 创建 I/O 上下文（队列容量提示，不强制预算） */
    struct nds_io_ctx *ctx = NULL;
    const struct nds_io_ctx_param ictx = { .max_io_cnt = 64 };
    CHECK_RESULT(nds_io_new_ctx(&ictx, &ctx), 0);

    /* 4. 构造 1 个 iocb；这里用一次性 buffer（未注册） */
    char *buf = aligned_alloc(512, 4096);          /* HBM：须 512B 对齐 */
    struct nds_io_vec iov = { .buf_addr = (uint64_t)buf, .buf_len = 4096 };
    struct nds_io_cb cb = {
        .opcode    = NDS_IO_OP_PREAD,
        .rw_flags  = 0,
        .obj       = { .fd = file_fd },
        .offset    = 0,
        .user_data = (uint64_t)12345,              /* 完成时原样回填 */
        .iov       = &iov,
        .iov_cnt   = 1,
        .host_pid  = 0,                            /* 0 = 当前进程 */
    };
    int acc = nds_io_submit(ctx, 1, &cb);
    CHECK_RESULT(acc, 1);

    /* 5. 阻塞收割（timeout=NULL 永久等待；{0,0} 非阻塞） */
    struct nds_io_event ev;
    int n = nds_io_getevents(ctx, 1, 1, &ev, NULL);
    CHECK_RESULT(n, 1);
    CHECK_RESULT(ev.user_data == 12345, 1);
    CHECK_RESULT(ev.res == 0, 1);                           /* 成功；失败为 -errno */

    /* 6. 拆除：先销毁 ctx */
    CHECK_RESULT(nds_io_destroy_ctx(ctx), 0);

    /* （如果注册过内存）注销由基地址 + 长度定位 */
    /* CHECK_RESULT(nds_unregister_mem(hbm_buf, hbm_size, 0), 0); */

    CHECK_RESULT(nds_exit(), 0);
    free(buf);
    close(file_fd);
    return 0;
}
```

## 3.2 使用已注册内存（`NDS_IO_F_REGISTERED_MEM`）+ 多段 scatter-gather

```c
/* 前提：reg_base 和 reg_size 已通过 nds_register_mem 注册。 */
struct nds_io_vec iovs[2] = {
    { .buf_addr = (uint64_t)reg_base + 0,      .buf_len = 8192 },
    { .buf_addr = (uint64_t)reg_base + 8192,   .buf_len = 8192 },
};
struct nds_io_cb cb = {
    .opcode    = NDS_IO_OP_PWRITE,   /* 写仅支持 block dev */
    .rw_flags  = NDS_IO_F_REGISTERED_MEM,
    .obj       = { .fd = blk_fd },
    .offset    = 0,
    .user_data = 1,
    .iov       = iovs,
    .iov_cnt   = 2,
    .host_pid  = 0,                  /* REGISTERED_MEM 时 host_pid 必须为 0 */
};
/* 两段 iov 都须完整落在 reg_base 对应的同一注册区间内。 */
CHECK_RESULT(nds_io_submit(ctx, 1, &cb), 1);
```

## 3.3 一次提交多个 iocb（可跨文件）+ 批量收割

```c
struct nds_io_cb cbs[2] = {
    { .opcode = NDS_IO_OP_PREAD,  .obj = { .fd = fd_a },
      .offset = 0,  .iov = &iov_a, .iov_cnt = 1, .host_pid = 0, .user_data = 10 },
    { .opcode = NDS_IO_OP_PREAD,  .obj = { .fd = fd_b },   /* 不同文件 */
      .offset = 4096, .iov = &iov_b, .iov_cnt = 1, .host_pid = 0, .user_data = 20 },
};
int acc = nds_io_submit(ctx, 2, cbs);
if (acc < 0)
    perror("nds_io_submit");          /* 一个都没接受：-errno */
/* acc 在 [0..2]：若 acc<2，表示 acc 个已接受、第 acc+1 个失败且已停（不回滚） */

#define MAX_NR 2
struct nds_io_event evs[MAX_NR] = { 0 };
/* 阻塞等满 2 个完成，至多写 2 个事件 */
int got = nds_io_getevents(ctx, 2, MAX_NR, evs, NULL);
for (int i = 0; i < got; i++)
    printf("cookie=%llu res=%lld\n",
           (unsigned long long)evs[i].user_data, (long long)evs[i].res);
```

## 3.4 非阻塞轮询（`timeout = {0,0}`）

```c
struct timespec t = { 0, 0 };        /* 非阻塞 */
for (;;) {
    int done = nds_io_getevents(ctx, 0, MAX_NR, evs, &t);
    if (done > 0) { /* 处理 done 个完成事件 */ }
    if (/* 所有 I/O 都已完成 */) break;
    /* 否则稍后重试，或用 NULL 阻塞等待 */
}
```

## 3.5 对应关系速查（编写时以本节为准）

```text
cuFileDriverOpen            -> nds_init + nds_register_fs
cuFileDriverClose           -> nds_exit
cuFileHandleRegister/Dereg  -> (None) 拓扑由 nds_register_fs 注册 / 调用方自理 fd
cuFileBufRegister           -> nds_register_mem
cuFileBufDeregister         -> nds_unregister_mem
cuFileBatchIOSetUp          -> nds_io_new_ctx
cuFileBatchIODestroy        -> nds_io_destroy_ctx
cuFileBatchIOSubmit         -> nds_io_submit
cuFileBatchIOGetStatus      -> nds_io_getevents
cuFileRead/cuFileWrite(同步) -> (None) 仅异步，自行 submit+getevents
其余 GDS 调优/流 API          -> (None)
```
