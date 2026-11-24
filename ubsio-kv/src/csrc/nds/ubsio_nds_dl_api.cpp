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

#include <dlfcn.h>
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_def.h"
#include "ubsio_nds_dl_api.h"

namespace ock {
namespace ubsio {

std::mutex NdsApi::gMutex;
void *NdsApi::libraryHandle = nullptr;
NdsApi::Functions NdsApi::functions{};

int32_t NdsApi::LoadLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (libraryHandle != nullptr) {
        return UBSIO_KVC_OK;
    }

    const char *libPaths[] = {
        "libascend_nds.so",
        "/usr/lib64/libascend_nds.so",
        "/usr/lib/libascend_nds.so",
    };

    void *handle = nullptr;
    for (const char *path : libPaths) {
        handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
        if (handle != nullptr) {
            LOG_INFO("Successfully loaded libascend_nds.so from: " << path);
            break;
        }
    }
    if (handle == nullptr) {
        LOG_ERROR("Failed to open libascend_nds.so, error: " << dlerror());
        return UBSIO_KVC_ERR;
    }

    Functions api{};
    DL_LOAD_SYM(api.driverOpen, decltype(api.driverOpen), handle, "ndsFileDriverOpen");
    DL_LOAD_SYM(api.driverClose, decltype(api.driverClose), handle, "ndsFileDriverClose");
    DL_LOAD_SYM(api.getVersion, decltype(api.getVersion), handle, "ndsFileGetVersion");
    DL_LOAD_SYM(api.registerFs, decltype(api.registerFs), handle, "ndsFileRegisterFs");
    DL_LOAD_SYM(api.registerFileHandle, decltype(api.registerFileHandle), handle, "ndsFileHandleRegister");
    DL_LOAD_SYM(api.deregisterFileHandle, decltype(api.deregisterFileHandle), handle, "ndsFileHandleDeregister");
    DL_LOAD_SYM(api.registerMem, decltype(api.registerMem), handle, "ndsFileBufRegister");
    DL_LOAD_SYM(api.unregisterMem, decltype(api.unregisterMem), handle, "ndsFileBufDeregister");
    DL_LOAD_SYM(api.setUpBatch, decltype(api.setUpBatch), handle, "ndsFileBatchIOSetUp");
    DL_LOAD_SYM(api.destroyBatch, decltype(api.destroyBatch), handle, "ndsFileBatchIODestroy");
    api.submitBatch = reinterpret_cast<decltype(api.submitBatch)>(
        dlvsym(handle, "ndsFileBatchIOSubmit", "ASCEND_NDS_1"));
    if (api.submitBatch == nullptr) {
        LOG_ERROR("libascend_nds.so is missing ndsFileBatchIOSubmit with ABI ASCEND_NDS_1: " << dlerror());
        dlclose(handle);
        return UBSIO_KVC_ERR;
    }
    DL_LOAD_SYM(api.getStatus, decltype(api.getStatus), handle, "ndsFileBatchIOGetStatus");

    functions = api;
    libraryHandle = handle;
    return UBSIO_KVC_OK;
}

void NdsApi::CleanupLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (libraryHandle != nullptr) {
        functions = {};
        dlclose(libraryHandle);
        libraryHandle = nullptr;
    }
}

int NdsApi::RegisterFs(const NDSfileFsDesc_t *desc) noexcept
{
    return functions.registerFs ? functions.registerFs(desc) : -ENOSYS;
}

int NdsApi::GetBatchEvents(NDSfileBatchHandle_t batch, unsigned minNr, unsigned nr,
                           NDSfileIOEvents_t *events, struct timespec *timeout) noexcept
{
    if (functions.getStatus == nullptr) {
        return -ENOSYS;
    }
    const int ret = functions.getStatus(batch, minNr, &nr, events, timeout);
    return ret == 0 ? static_cast<int>(nr) : ret;
}

} // namespace ubsio
} // namespace ock
