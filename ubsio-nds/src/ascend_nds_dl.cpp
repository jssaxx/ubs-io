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
#include <dlfcn.h>
#include "ascend_nds_dl.h"
#include "ascend_nds_log.h"

namespace ascend_nds {

namespace {
// The multi-range libnds.so uses the same SONAME as the single-range ABI.
struct MultiRangeInitParam {
    uint32_t flags;
    uint32_t reserved1;
    uint32_t version;
    uint32_t reserved2;
    uint64_t reserved3[4];
};

struct MemoryRange {
    uint64_t addr;
    uint64_t size;
};

struct MemoryDesc {
    const MemoryRange *ranges;
    uint32_t nr;
    uint32_t flags;
    uint64_t reserved[4];
};

using MultiRangeInitFunc = int (*)(MultiRangeInitParam *);
using MultiRangeMemoryFunc = int (*)(const MemoryDesc *);
using SingleRangeMemoryFunc = decltype(&nds_register_mem);

int SymLoad(void *&handle, const char *name, void *&out)
{
    out = dlsym(handle, name);
    if (out == nullptr) {
        LogErr("dlsym %s failed: %s", name, dlerror());
        dlclose(handle);
        handle = nullptr;
        return -ENOSYS;
    }
    return 0;
}
}  // namespace

NdsLib &NdsLib::Instance() noexcept
{
    static NdsLib inst;
    return inst;
}

NdsLib::~NdsLib() noexcept
{
    Unload();
}

int NdsLib::Load() noexcept
{
    std::lock_guard<std::mutex> guard(mtx_);
    if (loaded_) {
        return 0;
    }

    static const char *libPaths[] = {
        "libnds.so",
        "/usr/lib64/libnds.so",
        "/usr/lib/libnds.so",
    };

    void *h = nullptr;
    for (const char *path : libPaths) {
        h = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
        if (h != nullptr) {
            LogInfo("loaded libnds.so from %s", path);
            break;
        }
    }
    if (h == nullptr) {
        LogErr("dlopen libnds.so failed: %s", dlerror());
        return -ENOENT;
    }
    const bool multiRangeMem = dlsym(h, "p2p_check_uapi") != nullptr;
    int r = 0;
    if ((r = SymLoad(h, "nds_init", pInit)) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_exit", reinterpret_cast<void *&>(pExit))) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_register_fs", reinterpret_cast<void *&>(pRegisterFs))) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_unregister_fs", reinterpret_cast<void *&>(pUnregisterFs))) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_register_mem", pRegisterMem)) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_unregister_mem", pUnregisterMem)) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_io_new_ctx", reinterpret_cast<void *&>(pNewCtx))) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_io_destroy_ctx", reinterpret_cast<void *&>(pDestroyCtx))) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_io_submit", reinterpret_cast<void *&>(pSubmit))) != 0) {
        return r;
    }
    if ((r = SymLoad(h, "nds_io_getevents", reinterpret_cast<void *&>(pGetevents))) != 0) {
        return r;
    }
    handle_ = h;
    multiRangeMem_ = multiRangeMem;
    loaded_ = true;
    return 0;
}

void NdsLib::Unload() noexcept
{
    std::lock_guard<std::mutex> guard(mtx_);
    if (!loaded_) {
        return;
    }
    pInit = nullptr;
    pExit = nullptr;
    pRegisterFs = nullptr;
    pUnregisterFs = nullptr;
    pRegisterMem = nullptr;
    pUnregisterMem = nullptr;
    pNewCtx = nullptr;
    pDestroyCtx = nullptr;
    pSubmit = nullptr;
    pGetevents = nullptr;
    if (handle_ != nullptr) {
        dlclose(handle_);
        handle_ = nullptr;
    }
    loaded_ = false;
    multiRangeMem_ = false;
}

int NdsLib::Init(struct nds_init_param *param) noexcept
{
    if (pInit == nullptr) {
        LogErr("nds_init symbol not loaded");
        return -ENOSYS;
    }
    int r;
    if (multiRangeMem_) {
        MultiRangeInitParam multiRangeParam{};
        multiRangeParam.flags = param->flags;
        r = reinterpret_cast<MultiRangeInitFunc>(pInit)(&multiRangeParam);
        if (r == 0) {
            param->version = multiRangeParam.version;
        }
    } else {
        r = reinterpret_cast<decltype(&nds_init)>(pInit)(param);
    }
    if (r != 0) {
        LogErr("nds_init failed ret=%d: %s", r, strerror(-r));
    }
    return r;
}

int NdsLib::Exit() noexcept
{
    if (pExit == nullptr) {
        LogErr("nds_exit symbol not loaded");
        return -ENOSYS;
    }
    int r = pExit();
    if (r != 0) {
        LogErr("nds_exit failed ret=%d: %s", r, strerror(-r));
    }
    return r;
}

int NdsLib::RegisterFs(const struct nds_fs_desc *desc) noexcept
{
    if (pRegisterFs == nullptr) {
        LogErr("nds_register_fs symbol not loaded");
        return -ENOSYS;
    }
    int r = pRegisterFs(desc);
    if (r != 0) {
        LogErr("nds_register_fs failed ret=%d: %s", r, strerror(-r));
    }
    return r;
}

int NdsLib::UnregisterFs(const struct nds_fs_desc *desc) noexcept
{
    if (pUnregisterFs == nullptr) {
        LogErr("nds_unregister_fs symbol not loaded");
        return -ENOSYS;
    }
    int r = pUnregisterFs(desc);
    if (r != 0) {
        LogErr("nds_unregister_fs failed ret=%d: %s", r, strerror(-r));
    }
    return r;
}

int NdsLib::RegisterMem(void *addr, uint64_t size) noexcept
{
    return UpdateMemoryRegistration(pRegisterMem, "nds_register_mem", addr, size);
}

int NdsLib::UnregisterMem(void *addr, uint64_t size) noexcept
{
    return UpdateMemoryRegistration(pUnregisterMem, "nds_unregister_mem", addr, size);
}

int NdsLib::UpdateMemoryRegistration(void *symbol, const char *name, void *addr, uint64_t size) noexcept
{
    if (symbol == nullptr) {
        LogErr("%s symbol not loaded", name);
        return -ENOSYS;
    }
    int r;
    if (multiRangeMem_) {
        const MemoryRange range{reinterpret_cast<uintptr_t>(addr), size};
        const MemoryDesc desc{&range, 1, 0, {}};
        r = reinterpret_cast<MultiRangeMemoryFunc>(symbol)(&desc);
    } else {
        r = reinterpret_cast<SingleRangeMemoryFunc>(symbol)(addr, size, 0);
    }
    if (r != 0) {
        LogErr("%s failed ret=%d: %s", name, r, strerror(-r));
    }
    return r;
}

int NdsLib::NewCtx(const struct nds_io_ctx_param *param, struct nds_io_ctx **ctx) noexcept
{
    if (pNewCtx == nullptr) {
        LogErr("nds_io_new_ctx symbol not loaded");
        return -ENOSYS;
    }
    int r = pNewCtx(param, ctx);
    if (r != 0) {
        LogErr("nds_io_new_ctx failed ret=%d: %s", r, strerror(-r));
    }
    return r;
}

int NdsLib::DestroyCtx(struct nds_io_ctx *ctx) noexcept
{
    if (pDestroyCtx == nullptr) {
        LogErr("nds_io_destroy_ctx symbol not loaded");
        return -ENOSYS;
    }
    int r = pDestroyCtx(ctx);
    if (r != 0) {
        LogErr("nds_io_destroy_ctx failed ret=%d: %s", r, strerror(-r));
    }
    return r;
}

int NdsLib::Submit(struct nds_io_ctx *ctx, int nr, const struct nds_io_cb *iocb) noexcept
{
    if (pSubmit == nullptr) {
        LogErr("nds_io_submit symbol not loaded");
        return -ENOSYS;
    }
    int r = pSubmit(ctx, nr, iocb);
    if (r < 0) {
        LogErr("nds_io_submit failed ret=%d: %s (nr=%d)", r, strerror(-r), nr);
    }
    return r;
}

int NdsLib::Getevents(struct nds_io_ctx *ctx, int minNr, int nr,
                      struct nds_io_event *events, struct timespec *timeout) noexcept
{
    if (pGetevents == nullptr) {
        LogErr("nds_io_getevents symbol not loaded");
        return -ENOSYS;
    }
    int r = pGetevents(ctx, minNr, nr, events, timeout);
    if (r < 0) {
        LogErr("nds_io_getevents failed ret=%d: %s (minNr=%d nr=%d)", r, strerror(-r), minNr, nr);
    }
    return r;
}

}  // namespace ascend_nds
