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
#include "hcom_api_dl.h"
#include "net_log.h"

namespace ock {
namespace bio {
HcomApi &HcomApi::Instance()
{
    // Keep both the table and DSO alive through static-object destruction.
    static HcomApi *api = new HcomApi;
    return *api;
}

bool HcomApi::Load()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (handle_ != nullptr) {
        return true;
    }
    void *handle = dlopen("libhcom.so.0", RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        NET_LOG_ERROR("Cannot load HCOM runtime: " << dlerror());
        return false;
    }
    bool complete = true;
#define BIO_HCOM_RESOLVE(name) \
    dlerror(); \
    name = reinterpret_cast<decltype(name)>(dlsym(handle, #name)); \
    if (name == nullptr) { \
        NET_LOG_ERROR("Missing HCOM C symbol " #name ": " << dlerror()); \
        complete = false; \
    }
    BIO_HCOM_API_FUNCTIONS(BIO_HCOM_RESOLVE)
#undef BIO_HCOM_RESOLVE
    if (!complete) {
#define BIO_HCOM_CLEAR(name) name = nullptr;
        BIO_HCOM_API_FUNCTIONS(BIO_HCOM_CLEAR)
#undef BIO_HCOM_CLEAR
        dlclose(handle);
        return false;
    }
    handle_ = handle;
    return true;
}
}
}
