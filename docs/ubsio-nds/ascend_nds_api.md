# Ascend NDS API

> 本文档描述 `libascend_nds.so` 对外公开的 C 接口。该库将厂商 NDS 异步原语（`libnds.so`）封装为
> GPUDirect Storage（cuFile）语义形态的公开接口，命名采用昇腾风味前缀 `ndsFile*` / `NDSfile*_t`，
> 使 GDS 用户迁移成本最小、且与原生 GDS 同进程无符号冲突。
>
> 头文件见 [`ubsio-nds/include/ascend_nds.h`](../../ubsio-nds/include/ascend_nds.h)。独立构建见 [`ubsio-nds/README.md`](../../ubsio-nds/README.md)。底层原语对照见 [NDS API 原语](./nds_api.md)。
> `libascend_nds.so.1` 的公开接口使用 `ASCEND_NDS_1` 符号版本。调用方使用配套头文件编译。

目录

- [1. 设计要点](#1-设计要点)
- [2. 接口列表](#2-接口列表)
- [3. cuFile 对照](#3-cufile-对照)
- [4. 使用示例](#4-使用示例)
- [5. 限制与不支持](#5-限制与不支持)

---

## 1. 设计要点

- **独立交付**：`libascend_nds.so` 仅链接 `pthread`/`dl`/`libstdc++`，运行时 `dlopen` `libnds.so`，不依赖 `libubsio_kvc`。
- 批上下文使用 `NDSfileBatchHandle_t`。批请求和同步读写均使用 `NDSfileHandle_t` 文件句柄。
- 接口以 `-errno` 返回错误。`ndsFileBatchIOSubmit` 返回实际接收的请求数。`ndsFileRead/Write` 和 `NDSfileIOEvents_t.ret` 返回 `0` 表示整项 I/O 完成。
- **拓扑注册用户侧**：`ndsFileDriverOpen` 零参（仅 `nds_init`），拓扑 fd 由用户使用 `ndsFileRegisterFs` 显式注册并保持到驱动关闭。当前 XDS 的 `ndsFileDeregisterFs` 只清理封装库中的登记，内核拓扑在关闭驱动时释放。
- **同步便利**：NDS 仅异步；`ndsFileRead/Write` 在内部用临时上下文做 submit+阻塞收割，提供同步语义。
- `ndsFileBufRegister` 在返回前向 XDS 注册传入的地址和长度。同步 I/O 自动识别已注册内存，批提交通过 `NDS_FILE_IO_REGISTERED_MEM` 选择已注册内存。`ndsFileBufDeregister` 只注销指定 buffer，注销成功后即可释放该 buffer。

## 2. 接口列表

| 用途 | 函数 |
| --- | --- |
| 初始化驱动 | `int ndsFileDriverOpen(void)` |
| 注册文件系统拓扑 | `int ndsFileRegisterFs(const NDSfileFsDesc_t *desc)` |
| 清理封装库中的拓扑登记 | `int ndsFileDeregisterFs(const NDSfileFsDesc_t *desc)` |
| 卸载驱动 | `int ndsFileDriverClose(void)` |
| 注册文件句柄 | `int ndsFileHandleRegister(NDSfileHandle_t *fh, const NDSfileDescr_t *descr)` |
| 注销文件句柄 | `void ndsFileHandleDeregister(NDSfileHandle_t fh)` |
| 注册内存 | `int ndsFileBufRegister(void *buf, size_t length, int flags)` |
| 注销内存 | `int ndsFileBufDeregister(void *buf)` |
| 建批上下文 | `int ndsFileBatchIOSetUp(NDSfileBatchHandle_t *batch, unsigned nr)` |
| 销毁批上下文 | `int ndsFileBatchIODestroy(NDSfileBatchHandle_t batch)` |
| 提交批 I/O | `int ndsFileBatchIOSubmit(NDSfileBatchHandle_t batch, unsigned nr, const NDSfileIOParams_t *params, unsigned flags)` |
| 取完成 | `int ndsFileBatchIOGetStatus(NDSfileBatchHandle_t batch, unsigned min_nr, unsigned *nr, NDSfileIOEvents_t *events, struct timespec *timeout)` |
| 同步读 | `int ndsFileRead(NDSfileHandle_t fh, void *buf, size_t size, off_t file_offset, off_t bufPtr_offset)` |
| 同步写 | `int ndsFileWrite(NDSfileHandle_t fh, const void *buf, size_t size, off_t file_offset, off_t bufPtr_offset)` |
| 版本查询 | `int ndsFileGetVersion(int *version)` |

结构与常量见 `ascend_nds.h`：`NDSfileFsDesc_t`、`NDSfileDescr_t`、`NDSfileIOParams_t`、`NDSfileIOEvents_t`、`NDSfileOpcode_t`、`NDSfileStatus_t`、`NDS_FILE_RDMA_REGISTER` 等。

## 3. cuFile 对照

| cuFile | ascend_nds | 差异 |
| --- | --- | --- |
| `cuFileDriverOpen()` | `ndsFileDriverOpen()` | 一致（均零参）；NDS 拓扑 fd 由用户随后 `ndsFileRegisterFs` 单独注册 |
| — | `ndsFileRegisterFs`/`ndsFileDeregisterFs` | NDS 专有：cuFile 零参隐式拓扑，NDS 显式注册拓扑，按 fd 值清理封装库中的登记 |
| `cuFileDriverClose()` | `ndsFileDriverClose()` | NDS 关闭时释放内核拓扑和已注册内存，并使文件句柄失效。关闭前须结束在途调用并销毁批上下文，否则返回 `-EBUSY` |
| `cuFileHandleRegister/Deregister` | `ndsFileHandleRegister/Deregister` | 为批提交和同步读写创建或释放包装 fd 的文件句柄 |
| `cuFileBufRegister/Deregister` | `ndsFileBufRegister/Deregister` | flags 被掩码到 0（NDS 仅接受 0） |
| `cuFileBatchIOSetUp/Destroy` | `ndsFileBatchIOSetUp/Destroy` | `nr` 作 `max_io_cnt` 提示 |
| `cuFileBatchIOSubmit` | `ndsFileBatchIOSubmit` | 每项包含一组缓冲区，返回实际入队的请求数或 `-errno` |
| `cuFileBatchIOGetStatus` | `ndsFileBatchIOGetStatus` | `*nr` 出参；`ret` 为 `int`：成功 `0`、失败 `-errno`（NDS 无字节数，不报完成字节） |
| `cuFileRead/Write` | `ndsFileRead/Write` | 返回 `int`（`0`/`-errno`），**不返回字节数**（cuFile 返 `ssize_t` 字节，NDS 仅 0/-errno）；迁移须改错误处理 |
| `cuFileGetVersion` | `ndsFileGetVersion` | 返回 NDS 用户态接口版本 |

## 4. 使用示例

调用方完成 Ascend 初始化，并提供至少 4096 字节、按 512 字节对齐的 HBM 缓冲区。

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "ascend_nds.h"

static void check_result(int result, int expected, const char *operation)
{
    if (result != expected) {
        fprintf(stderr, "%s returned %d, expected %d\n", operation, result, expected);
        exit(EXIT_FAILURE);
    }
}

#define CHECK_RESULT(call, expected) check_result((call), (expected), #call)

void read_file_to_hbm(const char *path, void *buf)
{
    int fd = open(path, O_RDONLY | O_DIRECT);
    if (fd < 0) { perror("open"); exit(EXIT_FAILURE); }

    /* 1. 打开驱动 + 注册拓扑 */
    CHECK_RESULT(ndsFileDriverOpen(), 0);
    NDSfileFsDesc_t fs = { .fs_fd = &fd, .fs_fd_cnt = 1 };
    CHECK_RESULT(ndsFileRegisterFs(&fs), 0);

    /* 2. 注册文件句柄 + 注册内存 */
    NDSfileHandle_t fh;
    NDSfileDescr_t descr = { .type = NDS_FILE_HANDLE_TYPE_OPAQUE_FD, .handle.fd = fd };
    CHECK_RESULT(ndsFileHandleRegister(&fh, &descr), 0);

    CHECK_RESULT(ndsFileBufRegister(buf, 4096, 0), 0);

    /* 3. 批量异步读 */
    NDSfileBatchHandle_t batch;
    CHECK_RESULT(ndsFileBatchIOSetUp(&batch, 8), 0);
    NDSfileIOVec_t iovs[] = { { buf, 2048 }, { (char *)buf + 2048, 2048 } };
    NDSfileIOParams_t p = {
        .bufPtr_bases = iovs, .file_offset = 0, .bufPtr_offset = 0, .iov_count = 2,
        .fh = fh, .opcode = NDS_FILE_READ, .cookie = (void *)1,
    };
    CHECK_RESULT(ndsFileBatchIOSubmit(batch, 1, &p, NDS_FILE_IO_REGISTERED_MEM), 1);

    unsigned got = 1;
    NDSfileIOEvents_t ev;
    CHECK_RESULT(ndsFileBatchIOGetStatus(batch, 1, &got, &ev, NULL), 0);
    CHECK_RESULT((int)got, 1);
    CHECK_RESULT(ev.status, NDS_FILE_COMPLETE);

    CHECK_RESULT(ndsFileBatchIODestroy(batch), 0);

    /* 4. 同步读（便利接口，返回 0 表示整段完成） */
    CHECK_RESULT(ndsFileRead(fh, buf, 4096, 0, 0), 0);

    /* 5. 拆除 */
    CHECK_RESULT(ndsFileBufDeregister(buf), 0);
    ndsFileHandleDeregister(fh);
    CHECK_RESULT(ndsFileDriverClose(), 0);
    close(fd);
}
```

## 5. 限制与不支持

- **仅异步核心 + 同步便利**：提供 batch 与同步 read/write；**不提供** `cuFileReadAsync`/`WriteAsync`、`cuFileStreamRegister`/`Deregister`、`cuFileBatchIOCancel`（NDS 无流/取消概念，头中不声明，编译期 `NDS_FILE_NO_ASYNC` 可检测）。
- **不报完成字节数**：NDS 完成事件仅含 `res`（`0` 成功/`-errno`），无字节计数字段。`ndsFileRead/Write` 返回 `int`（`0`/`-errno`），`NDSfileIOEvents_t.ret` 同；均不返回实际传输字节数。cuFile `cuFileRead/Write` 返 `ssize_t` 字节，迁移须据此改错误处理（成功判 `== 0` 而非 `> 0`）。
- **不做调优存根**：`cuFileDriverGetProperties`/`SetPollMode`/`SetMax*`/`cuFileUseCount` 不提供（避免误导性 no-op）。
- **对齐**：offset / 长度 / buffer 地址须 512B 对齐。
- 单次提交最多 `NDS_FILE_MAX_BATCH_IO` 项，每项最多 `NDS_FILE_MAX_IOV` 段，每段长度不超过 `UINT32_MAX` 字节。
- `ndsFileGetVersion` 返回 NDS 用户态接口版本，当前 `0.1` 对应整数 `1`。
