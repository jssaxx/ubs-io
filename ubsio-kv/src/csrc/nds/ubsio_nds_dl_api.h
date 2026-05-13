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

#include <dlfcn.h>
#include <string>
#include "ubsio_kvc_log.h"

namespace ock {
namespace ubsio {
namespace nds {

// Function pointer types for nds_file.h API
typedef int (*nds_init_t)(int);
typedef int (*nds_init_async_t)(int);
typedef int (*nds_uninit_t)(void);
typedef int (*nds_open_t)(const char*, int);
typedef int (*nds_regmem_t)(nds_fileid_t, const void*, size_t);
typedef int (*nds_unregmem_t)(nds_fileid_t, const void*, size_t);
typedef ssize_t (*nds_read_t)(nds_fileid_t, void*, off_t, size_t, off_t);
typedef ssize_t (*nds_readv_batch_t)(nds_fileid_t, const struct iovec*, int, off_t);

struct NdsDlApi {
    void* handle{ nullptr };
    
    nds_init_t nds_init{ nullptr };
    nds_init_async_t nds_init_async{ nullptr };
    nds_uninit_t nds_uninit{ nullptr };
    nds_open_t nds_open{ nullptr };
    nds_regmem_t nds_regmem{ nullptr };
    nds_unregmem_t nds_unregmem{ nullptr };
    nds_read_t nds_read{ nullptr };
    nds_readv_batch_t nds_readv_batch{ nullptr };

    bool IsLoaded() const {
        return handle != nullptr;
    }

    bool Load() {
        if (handle != nullptr) {
            return true;
        }

        const char* libPaths[] = {
            "libnds_file.so",
            "/usr/lib64/libnds_file.so",
            "/usr/lib/libnds_file.so"
        };

        for (const char* path : libPaths) {
            handle = dlopen(path, RTLD_LAZY);
            if (handle != nullptr) {
                LOG_INFO("Successfully loaded libnds_file.so from: " << path);
                break;
            }
        }

        if (handle == nullptr) {
            LOG_WARN("Failed to load libnds_file.so: " << dlerror());
            return false;
        }

        // Load function pointers
#define LOAD_FUNC(name) \
        name = reinterpret_cast<decltype(name)>(dlsym(handle, #name)); \
        if (name == nullptr) { \
            LOG_WARN("Failed to load symbol " #name ": " << dlerror()); \
        }

        LOAD_FUNC(nds_init);
        LOAD_FUNC(nds_init_async);
        LOAD_FUNC(nds_uninit);
        LOAD_FUNC(nds_open);
        LOAD_FUNC(nds_regmem);
        LOAD_FUNC(nds_unregmem);
        LOAD_FUNC(nds_read);
        LOAD_FUNC(nds_readv_batch);

#undef LOAD_FUNC

        return true;
    }

    void Unload() {
        if (handle != nullptr) {
            dlclose(handle);
            handle = nullptr;
            nds_init = nullptr;
            nds_init_async = nullptr;
            nds_uninit = nullptr;
            nds_open = nullptr;
            nds_regmem = nullptr;
            nds_unregmem = nullptr;
            nds_read = nullptr;
            nds_readv_batch = nullptr;
        }
    }

    ~NdsDlApi() {
        Unload();
    }
};

// Global instance
extern NdsDlApi g_ndsDlApi;

}
}
}

#endif // UBSIO_NDS_DL_API_H
