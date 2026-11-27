/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 */

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <unordered_set>
#include <vector>
extern "C" {
#include "nds_api.h"
}
#include "ascend_nds.h"
#include "ascend_nds_log.h"

namespace {

using ascend_nds::LogErr;
using ascend_nds::LogInfo;
using ascend_nds::LogWarn;

struct NdsHandle {
    int fd;
};

struct MemoryRange {
    uintptr_t addr;
    uint64_t size;
};

constexpr int NDS_SYNC_TIMEOUT_SEC = 30;
static_assert(NDS_FILE_MAX_BATCH_IO == NDS_IO_MAX_IO_CNT);
static_assert(NDS_FILE_MAX_IOV == NDS_IO_MAX_IOV);

std::shared_mutex gMemMtx;
std::vector<MemoryRange> gMemRegs;

std::shared_mutex gHandleMtx;
std::unordered_set<NdsHandle *> gHandles;

std::shared_mutex gDrvMtx;
std::atomic<uint32_t> gVersion{ 0 };
std::atomic<unsigned> gBatchCount{ 0 };
bool gDriverOpen = false;
std::mutex gFsMtx;
std::unordered_set<int> gFsRegs; /* registered topology fd values */

int LogNdsError(int ret, const char *operation)
{
    if (ret < 0) {
        LogErr("%s failed ret=%d: %s", operation, ret, strerror(-ret));
    }
    return ret;
}

bool Contains(const MemoryRange &range, const nds_io_vec &iov)
{
    return iov.buf_addr >= range.addr && iov.buf_len <= range.size &&
        iov.buf_addr - range.addr <= range.size - iov.buf_len;
}

int RegisteredIoFlags(const nds_io_vec *iov, uint32_t count)
{
    const auto first = std::find_if(gMemRegs.begin(), gMemRegs.end(),
        [&](const MemoryRange &range) { return Contains(range, iov[0]); });
    if (first == gMemRegs.end()) {
        return -ERANGE;
    }
    bool crossesRanges = false;
    for (uint32_t i = 1; i < count; ++i) {
        if (Contains(*first, iov[i])) {
            continue;
        }
        if (std::none_of(gMemRegs.begin(), gMemRegs.end(),
                [&](const MemoryRange &range) { return Contains(range, iov[i]); })) {
            return -ERANGE;
        }
        crossesRanges = true;
    }
    return crossesRanges ? 0 : NDS_IO_F_REGISTERED_MEM;
}

int SubmitIo(nds_io_ctx *ctx, unsigned nr, nds_io_cb *cbs)
{
    std::shared_lock<std::shared_mutex> lock(gMemMtx);
    for (unsigned i = 0; i < nr; ++i) {
        const uint64_t addr = cbs[i].iov[0].buf_addr;
        const auto range = std::find_if(gMemRegs.begin(), gMemRegs.end(), [addr](const MemoryRange &entry) {
            return addr >= entry.addr && addr - entry.addr < entry.size;
        });
        if (range != gMemRegs.end()) {
            cbs[i].rw_flags = NDS_IO_F_REGISTERED_MEM;
        }
    }
    return LogNdsError(nds_io_submit(ctx, static_cast<int>(nr), cbs), "nds_io_submit");
}

int SyncXfer(NDSfileHandle_t fh, void *buf, size_t size, off_t file_offset,
             off_t bufPtr_offset, bool write)
{
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    if (!gDriverOpen) {
        LogWarn("SyncXfer: driver not open");
        return -ENODEV;
    }
    if (size == 0 || size > UINT32_MAX) {
        LogErr("SyncXfer: invalid size=%zu", size);
        return -EINVAL;
    }
    int fd;
    {
        std::shared_lock<std::shared_mutex> handleLk(gHandleMtx);
        const auto it = gHandles.find(static_cast<NdsHandle *>(fh));
        if (it == gHandles.end()) {
            LogErr("SyncXfer: invalid file handle");
            return -EINVAL;
        }
        fd = (*it)->fd;
    }

    struct nds_io_vec iov{};
    struct nds_io_cb cb{};
    cb.opcode = write ? NDS_IO_OP_PWRITE : NDS_IO_OP_PREAD;
    cb.obj.reserved = 0;
    cb.obj.fd = fd;
    cb.offset = static_cast<uint64_t>(file_offset);
    cb.iov = &iov;
    cb.iov_cnt = 1;
    cb.host_pid = 0;
    cb.reserved[0] = 0;
    cb.reserved[1] = 0;
    iov.buf_addr = reinterpret_cast<uint64_t>(static_cast<char *>(buf) + bufPtr_offset);
    iov.buf_len = static_cast<uint32_t>(size);
    iov.reserved = 0;

    struct nds_io_ctx *ctx = nullptr;
    struct nds_io_ctx_param ctxParam{};
    ctxParam.max_io_cnt = NDS_IO_MAX_IO_CNT;
    int r = LogNdsError(nds_io_new_ctx(&ctxParam, &ctx), "nds_io_new_ctx");
    if (r != 0 || ctx == nullptr) {
        if (r == 0) {
            LogErr("SyncXfer: new_ctx returned null ctx");
            r = -ENOSYS;
        }
        return r;
    }

    int result = 0;
    int n = SubmitIo(ctx, 1, &cb);
    if (n != 1) {
        result = (n < 0) ? n : -EIO;
        if (n >= 0) {
            LogWarn("SyncXfer: submit short n=%d (expected 1)", n);
        }
    } else {
        struct nds_io_event ev{};
        struct timespec ts{ NDS_SYNC_TIMEOUT_SEC, 0 };
        int m = LogNdsError(nds_io_getevents(ctx, 1, 1, &ev, &ts), "nds_io_getevents");
        if (m != 1) {
            result = (m < 0) ? m : -ETIMEDOUT;
            if (m >= 0) {
                LogWarn("SyncXfer: getevents short/timeout m=%d (expected 1)", m);
            }
        } else if (ev.res < 0) {
            result = static_cast<int>(ev.res);
            LogErr("SyncXfer: completion failed res=%lld: %s",
                   static_cast<long long>(ev.res), strerror(-result));
        }
    }
    LogNdsError(nds_io_destroy_ctx(ctx), "nds_io_destroy_ctx");
    return result;
}

}  // namespace

extern "C" {

int ndsFileDriverOpen(void)
{
    std::unique_lock<std::shared_mutex> drvLk(gDrvMtx);
    if (gDriverOpen) {
        LogWarn("ndsFileDriverOpen: driver already open");
        return -EBUSY;
    }
    struct nds_init_param ip{};
    int r = LogNdsError(nds_init(&ip), "nds_init");
    if (r != 0) {
        return r;
    }
    if (ip.version != NDS_API_VERSION) {
        LogErr("ndsFileDriverOpen: XDS API version=%u, expected=%u", ip.version, NDS_API_VERSION);
        LogNdsError(nds_exit(), "nds_exit");
        return -EPROTONOSUPPORT;
    }
    gVersion.store(ip.version, std::memory_order_relaxed);
    gDriverOpen = true;
    LogInfo("ndsFileDriverOpen: ok (version=%u)", ip.version);
    return 0;
}

int ndsFileDriverClose(void)
{
    std::unique_lock<std::shared_mutex> drvLk(gDrvMtx, std::try_to_lock);
    if (!drvLk.owns_lock() || gBatchCount.load(std::memory_order_relaxed) != 0) {
        return -EBUSY;
    }
    if (!gDriverOpen) {
        LogWarn("ndsFileDriverClose: driver not open");
        return -ENODEV;
    }
    gDriverOpen = false;
    LogNdsError(nds_exit(), "nds_exit");
    gMemRegs.clear();
    gFsRegs.clear();
    for (auto *h : gHandles) {
        delete h;
    }
    gHandles.clear();
    return 0;
}

int ndsFileRegisterFs(const NDSfileFsDesc_t *desc)
{
    if (desc == nullptr || desc->fs_fd == nullptr || desc->fs_fd_cnt < 1) {
        LogErr("ndsFileRegisterFs: invalid desc=%p fs_fd=%p cnt=%u",
               (const void *)desc, desc ? (const void *)desc->fs_fd : nullptr,
               desc ? desc->fs_fd_cnt : 0);
        return -EINVAL;
    }
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    if (!gDriverOpen) {
        LogWarn("ndsFileRegisterFs: driver not open");
        return -ENODEV;
    }
    struct nds_fs_desc fs{};
    fs.fs_fd = const_cast<int32_t *>(desc->fs_fd);
    fs.fs_fd_cnt = desc->fs_fd_cnt;
    int r = LogNdsError(nds_register_fs(&fs), "nds_register_fs");
    if (r != 0) {
        return r;
    }
    {
        std::lock_guard<std::mutex> lk(gFsMtx);
        for (unsigned int i = 0; i < desc->fs_fd_cnt; ++i) {
            gFsRegs.insert(desc->fs_fd[i]);
        }
    }
    LogInfo("ndsFileRegisterFs: ok (fds=%u)", desc->fs_fd_cnt);
    return 0;
}

int ndsFileDeregisterFs(const NDSfileFsDesc_t *desc)
{
    if (desc == nullptr || desc->fs_fd == nullptr) {
        LogErr("ndsFileDeregisterFs: null desc=%p", (const void *)desc);
        return -EINVAL;
    }
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    int r = -ENODEV;
    if (gDriverOpen) {
        struct nds_fs_desc fs{};
        fs.fs_fd = const_cast<int32_t *>(desc->fs_fd);
        fs.fs_fd_cnt = desc->fs_fd_cnt;
        r = LogNdsError(nds_unregister_fs(&fs), "nds_unregister_fs");
    } else {
        LogWarn("ndsFileDeregisterFs: driver not open, skip native unregister");
    }
    /* Only drop our tracking on native success: a failed unregister keeps the
     * registration tracked in NDS, so the caller may retry. */
    if (r == 0) {
        std::lock_guard<std::mutex> lk(gFsMtx);
        for (unsigned int i = 0; i < desc->fs_fd_cnt; ++i) {
            gFsRegs.erase(desc->fs_fd[i]);
        }
    }
    return r;
}

int ndsFileHandleRegister(NDSfileHandle_t *fh, const NDSfileDescr_t *descr)
{
    if (fh == nullptr || descr == nullptr) {
        LogErr("ndsFileHandleRegister: null fh=%p or descr=%p", (void *)fh, (const void *)descr);
        return -EINVAL;
    }
    if (descr->type != NDS_FILE_HANDLE_TYPE_OPAQUE_FD) {
        LogErr("ndsFileHandleRegister: unsupported type=%d", descr->type);
        return -EINVAL;
    }
    std::unique_ptr<NdsHandle> h(new (std::nothrow) NdsHandle{ descr->handle.fd });
    if (h == nullptr) {
        LogErr("ndsFileHandleRegister: alloc failed");
        return -ENOMEM;
    }
    {
        std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
        std::unique_lock<std::shared_mutex> lk(gHandleMtx);
        gHandles.insert(h.get());
        *fh = h.release();
    }
    return 0;
}

void ndsFileHandleDeregister(NDSfileHandle_t fh)
{
    if (fh == nullptr) {
        LogErr("ndsFileHandleDeregister: null fh");
        return;
    }
    auto *h = static_cast<NdsHandle *>(fh);
    {
        std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
        std::unique_lock<std::shared_mutex> lk(gHandleMtx);
        if (gHandles.erase(h) == 0) {
            LogWarn("ndsFileHandleDeregister: handle is not registered");
            return;
        }
    }
    delete h;
}

int ndsFileBufRegister(void *buf, size_t length, int flags)
{
    (void)flags; /* NDS accepts only 0 */
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    std::unique_lock<std::shared_mutex> lk(gMemMtx);
    if (!gDriverOpen) {
        LogWarn("ndsFileBufRegister: driver not open");
        return -ENODEV;
    }
    const auto addr = reinterpret_cast<uintptr_t>(buf);
    if (addr == 0 || length == 0 || length > UINTPTR_MAX - addr) {
        LogErr("ndsFileBufRegister: invalid range length=%zu", length);
        return -EINVAL;
    }
    for (const auto &entry : gMemRegs) {
        if (addr < entry.addr + entry.size && entry.addr < addr + length) {
            return -EEXIST;
        }
    }
    if (gMemRegs.size() >= NDS_FILE_MAX_MEM_RANGES) {
        return -ENOSPC;
    }
    gMemRegs.push_back({ addr, length });
    const int ret = LogNdsError(nds_register_mem(buf, length, 0), "nds_register_mem");
    if (ret != 0) {
        gMemRegs.pop_back();
    }
    return ret;
}

int ndsFileBufDeregister(void *buf)
{
    if (buf == nullptr) {
        LogErr("ndsFileBufDeregister: null buf");
        return -EINVAL;
    }
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    std::unique_lock<std::shared_mutex> lk(gMemMtx);
    if (!gDriverOpen) {
        return -ENODEV;
    }
    const auto addr = reinterpret_cast<uintptr_t>(buf);
    auto it = std::find_if(gMemRegs.begin(), gMemRegs.end(),
                           [addr](const MemoryRange &entry) { return entry.addr == addr; });
    if (it == gMemRegs.end()) {
        return -ENOENT;
    }
    int r = LogNdsError(nds_unregister_mem(buf, it->size, 0), "nds_unregister_mem");
    if (r == 0) {
        gMemRegs.erase(it);
    }
    return r;
}

int ndsFileBatchIOSetUp(NDSfileBatchHandle_t *batch, unsigned nr)
{
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    if (!gDriverOpen) {
        LogWarn("ndsFileBatchIOSetUp: driver not open");
        return -ENODEV;
    }
    if (batch == nullptr || nr == 0 || nr > NDS_IO_MAX_IO_CNT) {
        LogErr("ndsFileBatchIOSetUp: invalid batch=%p nr=%u", (void *)batch, nr);
        return -EINVAL;
    }
    struct nds_io_ctx_param p{};
    p.max_io_cnt = static_cast<uint32_t>(nr);
    struct nds_io_ctx *ctx = nullptr;
    int r = LogNdsError(nds_io_new_ctx(&p, &ctx), "nds_io_new_ctx");
    if (r != 0 || ctx == nullptr) {
        if (r == 0) {
            LogErr("ndsFileBatchIOSetUp: new_ctx returned null ctx");
            r = -ENOSYS;
        }
        return r;
    }
    gBatchCount.fetch_add(1, std::memory_order_relaxed);
    *batch = ctx;
    return 0;
}

int ndsFileBatchIODestroy(NDSfileBatchHandle_t batch)
{
    if (batch == nullptr) {
        LogErr("ndsFileBatchIODestroy: null batch");
        return -EINVAL;
    }
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    if (!gDriverOpen) {
        return -ENODEV;
    }
    const int ret = LogNdsError(nds_io_destroy_ctx(static_cast<struct nds_io_ctx *>(batch)), "nds_io_destroy_ctx");
    // XDS releases a valid context even when draining or closing reports an error.
    gBatchCount.fetch_sub(1, std::memory_order_relaxed);
    return ret;
}

int ndsFileBatchIOSubmit(NDSfileBatchHandle_t batch, unsigned nr,
                         const NDSfileIOParams_t *params, unsigned flags)
{
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    std::shared_lock<std::shared_mutex> lock(gMemMtx);
    if (!gDriverOpen) {
        return -ENODEV;
    }
    if (batch == nullptr || params == nullptr || nr == 0 || nr > NDS_FILE_MAX_BATCH_IO ||
        (flags & ~NDS_FILE_IO_REGISTERED_MEM) != 0) {
        return -EINVAL;
    }
    size_t iovCount = 0;
    for (unsigned i = 0; i < nr; ++i) {
        const auto &p = params[i];
        if (p.fh == nullptr || (p.opcode != NDS_FILE_READ && p.opcode != NDS_FILE_WRITE) ||
            p.bufPtr_bases == nullptr || p.file_offset < 0 || p.bufPtr_offset < 0 ||
            p.iov_count == 0 || p.iov_count > NDS_FILE_MAX_IOV) {
            return -EINVAL;
        }
        iovCount += p.iov_count;
    }

    std::unique_ptr<nds_io_vec[]> iovs(new (std::nothrow) nds_io_vec[iovCount]);
    std::unique_ptr<nds_io_cb[]> cbs(new (std::nothrow) nds_io_cb[nr]{});
    if (!iovs || !cbs) {
        return -ENOMEM;
    }
    std::shared_lock<std::shared_mutex> handleLk(gHandleMtx);
    size_t nextIov = 0;
    for (unsigned i = 0; i < nr; ++i) {
        const auto &p = params[i];
        const auto handle = gHandles.find(static_cast<NdsHandle *>(p.fh));
        if (handle == gHandles.end() || (*handle)->fd < 0) {
            return -EINVAL;
        }
        const auto *segments = static_cast<const NDSfileIOVec_t *>(p.bufPtr_bases);
        const auto bufferOffset = static_cast<uintptr_t>(p.bufPtr_offset);
        auto &cb = cbs[i];
        cb.iov = iovs.get() + nextIov;
        cb.iov_cnt = static_cast<uint32_t>(p.iov_count);
        for (size_t j = 0; j < p.iov_count; ++j) {
            const auto base = reinterpret_cast<uintptr_t>(segments[j].buf);
            const auto len = segments[j].length;
            if (base == 0 || bufferOffset > UINTPTR_MAX - base || len == 0 || len > UINT32_MAX ||
                len > UINTPTR_MAX - (base + bufferOffset)) {
                return -EINVAL;
            }
            iovs[nextIov++] = {base + bufferOffset, static_cast<uint32_t>(len), 0};
        }
        const int rwFlags = (flags & NDS_FILE_IO_REGISTERED_MEM) ? RegisteredIoFlags(cb.iov, cb.iov_cnt) : 0;
        if (rwFlags < 0) {
            if (i == 0) {
                return rwFlags;
            }
            nr = i;
            break;
        }
        cb.opcode = p.opcode == NDS_FILE_WRITE ? NDS_IO_OP_PWRITE : NDS_IO_OP_PREAD;
        cb.rw_flags = static_cast<uint32_t>(rwFlags);
        cb.obj.fd = (*handle)->fd;
        cb.offset = static_cast<uint64_t>(p.file_offset);
        cb.user_data = reinterpret_cast<uintptr_t>(p.cookie);
    }
    return LogNdsError(nds_io_submit(static_cast<nds_io_ctx *>(batch), static_cast<int>(nr), cbs.get()), "nds_io_submit");
}

int ndsFileBatchIOGetStatus(NDSfileBatchHandle_t batch, unsigned min_nr,
                            unsigned *nr, NDSfileIOEvents_t *events,
                            struct timespec *timeout)
{
    std::shared_lock<std::shared_mutex> drvLk(gDrvMtx);
    if (!gDriverOpen) {
        LogWarn("ndsFileBatchIOGetStatus: driver not open");
        return -ENODEV;
    }
    auto *ctx = static_cast<struct nds_io_ctx *>(batch);
    if (ctx == nullptr || nr == nullptr || events == nullptr) {
        LogErr("ndsFileBatchIOGetStatus: invalid ctx=%p nr=%p events=%p",
               (void *)ctx, (void *)nr, (void *)events);
        return -EINVAL;
    }
    unsigned cap = *nr;
    if (cap == 0) {
        LogErr("ndsFileBatchIOGetStatus: zero capacity (*nr=0)");
        return -EINVAL;
    }
    std::unique_ptr<nds_io_event[]> ev(new (std::nothrow) nds_io_event[cap]);
    if (!ev) {
        *nr = 0;
        return -ENOMEM;
    }
    int n = LogNdsError(nds_io_getevents(ctx, static_cast<int>(min_nr), static_cast<int>(cap), ev.get(), timeout),
                        "nds_io_getevents");
    if (n < 0) {
        *nr = 0;
        return n;
    }
    for (int i = 0; i < n; ++i) {
        events[i].cookie = reinterpret_cast<void *>(ev[i].user_data);
        events[i].status = (ev[i].res == 0) ? NDS_FILE_COMPLETE : NDS_FILE_FAILED;
        events[i].ret = (ev[i].res == 0) ? 0 : static_cast<int>(ev[i].res);
    }
    *nr = static_cast<unsigned>(n);
    return 0;
}

int ndsFileRead(NDSfileHandle_t fh, void *buf, size_t size,
                 off_t file_offset, off_t bufPtr_offset)
{
    return SyncXfer(fh, buf, size, file_offset, bufPtr_offset, false);
}

int ndsFileWrite(NDSfileHandle_t fh, const void *buf, size_t size,
                 off_t file_offset, off_t bufPtr_offset)
{
    return SyncXfer(fh, const_cast<void *>(buf), size, file_offset, bufPtr_offset, true);
}

int ndsFileGetVersion(int *version)
{
    if (version == nullptr) {
        LogErr("ndsFileGetVersion: null version");
        return -EINVAL;
    }
    uint32_t v = gVersion.load(std::memory_order_relaxed);
    if (v == 0) {
        v = NDS_API_VERSION;
    }
    *version = static_cast<int>(v);
    return 0;
}

}  // extern "C"
