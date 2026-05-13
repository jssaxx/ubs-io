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

#include <cstdint>
#include <cstdio>
#include <string>
#include <dlfcn.h>
#include <iostream>
#include <cstring>
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_def.h"
#include "ubsio_nds_dl_api.h"

namespace ock {
namespace ubsio {

bool NdsApi::gLoaded = false;
std::mutex NdsApi::gMutex;
void *NdsApi::ndsHandle = nullptr;
static std::string g_ndsLibName = "libnds_file.so";

NdsInitFunc NdsApi::pNdsInit = nullptr;
NdsInitAsyncFunc NdsApi::pNdsInitAsync = nullptr;
NdsUninitFunc NdsApi::pNdsUninit = nullptr;
NdsOpenFunc NdsApi::pNdsOpen = nullptr;
NdsRegmemFunc NdsApi::pNdsRegmem = nullptr;
NdsUnregmemFunc NdsApi::pNdsUnregmem = nullptr;
NdsReadFunc NdsApi::pNdsRead = nullptr;
NdsReadvBatchFunc NdsApi::pNdsReadvBatch = nullptr;


int32_t NdsApi::LoadLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gLoaded) {
        return 0;
    }

    const char* libPaths[] = {
        "libnds_file.so",
        "/usr/lib64/libnds_file.so",
        "/usr/lib/libnds_file.so"
    };

    void* handle = nullptr;
    for (const char* path : libPaths) {
        handle = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
        if (handle != nullptr) {
            LOG_INFO("Successfully loaded libnds_file.so from: " << path);
            break;
        }
    }

    if (handle == nullptr) {
        LOG_ERROR("Failed to open libnds_file.so, error: " << dlerror());
        return UBSIO_KVC_ERR;
    }

    DL_LOAD_SYM(pNdsInit, NdsInitFunc, handle, "nds_init");
    DL_LOAD_SYM(pNdsInitAsync, NdsInitAsyncFunc, handle, "nds_init_async");
    DL_LOAD_SYM(pNdsUninit, NdsUninitFunc, handle, "nds_uninit");
    DL_LOAD_SYM(pNdsOpen, NdsOpenFunc, handle, "nds_open");
    DL_LOAD_SYM(pNdsRegmem, NdsRegmemFunc, handle, "nds_regmem");
    DL_LOAD_SYM(pNdsUnregmem, NdsUnregmemFunc, handle, "nds_unregmem");
    DL_LOAD_SYM(pNdsRead, NdsReadFunc, handle, "nds_read");
    DL_LOAD_SYM(pNdsReadvBatch, NdsReadvBatchFunc, handle, "nds_readv_batch");

    ndsHandle = handle;
    gLoaded = true;
    return 0;
}

void NdsApi::CleanupLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (!gLoaded) {
        return;
    }

    pNdsInit = nullptr;
    pNdsInitAsync = nullptr;
    pNdsUninit = nullptr;
    pNdsOpen = nullptr;
    pNdsRegmem = nullptr;
    pNdsUnregmem = nullptr;
    pNdsRead = nullptr;
    pNdsReadvBatch = nullptr;

    if (ndsHandle != nullptr) {
        dlclose(ndsHandle);
        ndsHandle = nullptr;
    }

    gLoaded = false;
}

}  // namespace ubsio
}  // namespace ock
