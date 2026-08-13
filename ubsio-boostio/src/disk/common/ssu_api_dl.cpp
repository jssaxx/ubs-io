/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <mutex>

#include "bio_log.h"
#include "bio_file_util.h"
#include "ssu_api_dl.h"

namespace ock {
namespace bio {
SsuApiOps *DlSsuApi::apiOps = nullptr;

std::mutex DlSsuApi::sMutex;

bool DlSsuApi::gStarted = false;
void *DlSsuApi::gSsuSdk = nullptr;
const char *DlSsuApi::gSsuLibName = "libubse-ssu-client.so";

int DlSsuApi::LoadSsuApiDl()
{
    std::lock_guard<std::mutex> lock(sMutex);
    LOG_INFO("Starting to load ssu api");
    if (gStarted) {
        return 0;
    }

    void *sdk = dlopen(gSsuLibName, RTLD_NOW | RTLD_LOCAL);
    if (sdk == nullptr) {
        LOG_ERROR("dlopen failed: " << dlerror());
        return -1;
    }

    SsuApiOps *ops = new (std::nothrow) SsuApiOps {};
    if (ops == nullptr) {
        LOG_ERROR("alloc SsuApiOps failed");
        dlclose(sdk);
        return -1;
    }

#define SSU_DLSYM(field)                                                                  \
    do {                                                                                  \
        dlerror();                                                                        \
        ops->field = reinterpret_cast<decltype(ops->field)>(dlsym(sdk, "ubs_ssu_" #field)); \
        const char *ssuDlErr = dlerror();                                                 \
        if (ssuDlErr != nullptr || ops->field == nullptr) {                              \
            LOG_ERROR("dlsym failed: ubs_ssu_" #field << ", err="                        \
                << (ssuDlErr != nullptr ? ssuDlErr : "null"));                           \
            delete ops;                                                                  \
            dlclose(sdk);                                                                \
            return -1;                                                                   \
        }                                                                                 \
    } while (0)

    SSU_DLSYM(alloc_info_list);
    SSU_DLSYM(alloc_info_list_free);
    SSU_DLSYM(ns_dev_paths_free);
    SSU_DLSYM(ns_stats_get);
    SSU_DLSYM(ns_stats_free);
    SSU_DLSYM(connect_info_get);
    SSU_DLSYM(connect_info_free);
    SSU_DLSYM(space_alloc);
    SSU_DLSYM(alloc_info_free);
    SSU_DLSYM(space_free);
    SSU_DLSYM(access_permission_add);
    SSU_DLSYM(access_permission_remove);
    SSU_DLSYM(space_attach);
    SSU_DLSYM(space_detach);
    SSU_DLSYM(linear_space_attach);
    SSU_DLSYM(linear_space_detach);
    SSU_DLSYM(striped_space_attach);
    SSU_DLSYM(striped_space_detach);
    SSU_DLSYM(fe_device_list);
    SSU_DLSYM(fe_device_list_free);
    SSU_DLSYM(fe_device_alloc);
    SSU_DLSYM(fe_device_free);

#undef SSU_DLSYM

    gSsuSdk = sdk;
    __atomic_store_n(&apiOps, ops, __ATOMIC_RELEASE);
    gStarted = true;
    return 0;
}

int DlSsuApi::UnloadSsuApiDl()
{
    std::lock_guard<std::mutex> lock(sMutex);
    LOG_INFO("Starting to unload ssu api");
    if (!gStarted) {
        return 0;
    }

    gStarted = false;
    SsuApiOps *ops = __atomic_exchange_n(&apiOps, static_cast<SsuApiOps *>(nullptr), __ATOMIC_ACQ_REL);
    if (gSsuSdk != nullptr) {
        dlclose(gSsuSdk);
        gSsuSdk = nullptr;
    }
    delete ops;
    return 0;
}
} // namespace bio
} // namespace ock
