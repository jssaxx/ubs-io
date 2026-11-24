/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBSIO_NDS_DL_API_H
#define UBSIO_NDS_DL_API_H

#include <cerrno>
#include <cstdint>
#include <mutex>
#include "ascend_nds.h"

namespace ock {
namespace ubsio {

constexpr int NDS_MAX_IO_DEPTH = NDS_FILE_MAX_BATCH_IO;
constexpr int NDS_DRIVER_UAPI_VERSION = 1; // XDS UAPI 0.1

class NdsApi {
public:
    NdsApi() = delete;

    static int32_t LoadLibrary();

    static void CleanupLibrary();

    static int DriverOpen()
    {
        return functions.driverOpen ? functions.driverOpen() : -ENOSYS;
    }

    static int DriverClose()
    {
        return functions.driverClose ? functions.driverClose() : -ENOSYS;
    }

    static int GetVersion(int *version)
    {
        return functions.getVersion ? functions.getVersion(version) : -ENOSYS;
    }

    static int RegisterFs(const NDSfileFsDesc_t *desc) noexcept;

    static int RegisterFileHandle(NDSfileHandle_t *handle, int fd)
    {
        const NDSfileDescr_t descr{NDS_FILE_HANDLE_TYPE_OPAQUE_FD, {fd}};
        return functions.registerFileHandle ? functions.registerFileHandle(handle, &descr) : -ENOSYS;
    }

    static void DeregisterFileHandle(NDSfileHandle_t handle)
    {
        if (functions.deregisterFileHandle != nullptr) {
            functions.deregisterFileHandle(handle);
        }
    }

    static int RegisterMem(void *address, size_t size)
    {
        return functions.registerMem ? functions.registerMem(address, size, 0) : -ENOSYS;
    }

    static int UnregisterMem(void *address)
    {
        return functions.unregisterMem ? functions.unregisterMem(address) : -ENOSYS;
    }

    static int SetUpBatch(NDSfileBatchHandle_t *batch, unsigned nr)
    {
        return functions.setUpBatch ? functions.setUpBatch(batch, nr) : -ENOSYS;
    }

    static int DestroyBatch(NDSfileBatchHandle_t batch)
    {
        return functions.destroyBatch ? functions.destroyBatch(batch) : -ENOSYS;
    }

    static int SubmitBatch(NDSfileBatchHandle_t batch, unsigned nr,
                           const NDSfileIOParams_t *params, unsigned flags)
    {
        return functions.submitBatch ? functions.submitBatch(batch, nr, params, flags) : -ENOSYS;
    }

    static int GetBatchEvents(NDSfileBatchHandle_t batch, unsigned minNr, unsigned nr,
                              NDSfileIOEvents_t *events, struct timespec *timeout) noexcept;

private:
    struct Functions {
        decltype(&ndsFileDriverOpen) driverOpen{};
        decltype(&ndsFileDriverClose) driverClose{};
        decltype(&ndsFileGetVersion) getVersion{};
        decltype(&ndsFileRegisterFs) registerFs{};
        decltype(&ndsFileHandleRegister) registerFileHandle{};
        decltype(&ndsFileHandleDeregister) deregisterFileHandle{};
        decltype(&ndsFileBufRegister) registerMem{};
        decltype(&ndsFileBufDeregister) unregisterMem{};
        decltype(&ndsFileBatchIOSetUp) setUpBatch{};
        decltype(&ndsFileBatchIODestroy) destroyBatch{};
        decltype(&ndsFileBatchIOSubmit) submitBatch{};
        decltype(&ndsFileBatchIOGetStatus) getStatus{};
    };

    static std::mutex gMutex;
    static void *libraryHandle;
    static Functions functions;
};

} // namespace ubsio
} // namespace ock

#endif // UBSIO_NDS_DL_API_H
