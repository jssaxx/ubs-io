/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 */

#ifndef ASCEND_NDS_DL_H
#define ASCEND_NDS_DL_H

#include <cstdint>
#include <cstddef>
#include <mutex>
extern "C" {
#include "nds_api.h"
}

namespace ascend_nds {

inline constexpr uint32_t NDS_DRIVER_UAPI_VERSION = NDS_API_VERSION;

/*
 * Self-contained dlopen shim for libnds.so. Independent of the ubsio_kvc
 * NdsApi shim so that libascend_nds.so links only pthread/dl.
 * Returns 0 on success, -errno on failure (NDS uses 0/-errno natively).
 */
class NdsLib {
public:
    static NdsLib &Instance() noexcept;

    /* dlopen + dlsym; idempotent. 0/-errno. */
    int Load() noexcept;
    /* dlclose + clear pointers; idempotent. */
    void Unload() noexcept;

    bool Loaded() const noexcept { return loaded_; }

    bool SupportsRegisteredIoAcrossRanges() const noexcept { return multiRangeMem_; }

    int Init(struct nds_init_param *param) noexcept;
    int Exit() noexcept;
    int RegisterFs(const struct nds_fs_desc *desc) noexcept;
    int UnregisterFs(const struct nds_fs_desc *desc) noexcept;
    int RegisterMem(void *addr, uint64_t size) noexcept;

    int UnregisterMem(void *addr, uint64_t size) noexcept;

    int NewCtx(const struct nds_io_ctx_param *param, struct nds_io_ctx **ctx) noexcept;
    int DestroyCtx(struct nds_io_ctx *ctx) noexcept;
    int Submit(struct nds_io_ctx *ctx, int nr, const struct nds_io_cb *iocb) noexcept;
    int Getevents(struct nds_io_ctx *ctx, int minNr, int nr,
                  struct nds_io_event *events, struct timespec *timeout) noexcept;

private:
    NdsLib() = default;
    ~NdsLib() noexcept;

    NdsLib(const NdsLib &) = delete;
    NdsLib &operator=(const NdsLib &) = delete;

    int UpdateMemoryRegistration(void *symbol, const char *name, void *addr, uint64_t size) noexcept;

    void *handle_{ nullptr };
    bool loaded_{ false };
    bool multiRangeMem_{ false };
    std::mutex mtx_;

    void *pInit{ nullptr };
    int (*pExit)(void){ nullptr };
    int (*pRegisterFs)(const struct nds_fs_desc *){ nullptr };
    int (*pUnregisterFs)(const struct nds_fs_desc *){ nullptr };
    void *pRegisterMem{ nullptr };
    void *pUnregisterMem{ nullptr };
    int (*pNewCtx)(const struct nds_io_ctx_param *, struct nds_io_ctx **){ nullptr };
    int (*pDestroyCtx)(struct nds_io_ctx *){ nullptr };
    int (*pSubmit)(struct nds_io_ctx *, int, const struct nds_io_cb *){ nullptr };
    int (*pGetevents)(struct nds_io_ctx *, int, int,
                      struct nds_io_event *, struct timespec *){ nullptr };
};

}  // namespace ascend_nds

#endif /* ASCEND_NDS_DL_H */
