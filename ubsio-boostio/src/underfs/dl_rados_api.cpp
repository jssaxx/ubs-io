/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

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
#include "bio_log.h"
#include "dl_rados_api.h"

#define DLSYM_RETURN(handle, sym, type, ptr)              \
    do {                                                  \
        auto ptr1 = dlsym((handle), (sym));               \
        if (ptr1 == nullptr) {                            \
            LOG_ERROR("Failed to load " << (sym));        \
            return -1;                                    \
        }                                                 \
        (ptr) = (type)ptr1;                               \
    } while (0)

namespace ock {
namespace bio {
MethodRadosCreate2 DlRadosApi::radosCreate2 = nullptr;
MethodRadosConfReadFile DlRadosApi::radosConfReadFile = nullptr;
MethodRadosConnect DlRadosApi::radosConnect = nullptr;
MethodRadosShutdown DlRadosApi::radosShutdown = nullptr;
MethodRadosPoolLookup DlRadosApi::radosPoolLookup = nullptr;
MethodRadosPoolCreate DlRadosApi::radosPoolCreate = nullptr;
MethodRadosIoctxCreate DlRadosApi::radosIoctxCreate = nullptr;
MethodRadosIoctxDestroy DlRadosApi::radosIoctxDestroy = nullptr;
MethodRadosWrite DlRadosApi::radosWrite = nullptr;
MethodRadosRead DlRadosApi::radosRead = nullptr;
MethodRadosRemove DlRadosApi::radosRemove = nullptr;
MethodRadosStat DlRadosApi::radosStat = nullptr;
MethodRadosNobjectsListOpen DlRadosApi::radosNobjectsListOpen = nullptr;
MethodRadosNobjectsListNext DlRadosApi::radosNobjectsListNext = nullptr;
MethodRadosNobjectsListClose DlRadosApi::radosNobjectsListClose = nullptr;

bool DlRadosApi::gStarted = false;
const char *DlRadosApi::gRadosLibNames[] = { "librados.so.2", "librados.so" };

bool DlRadosApi::IsRadosMethodLoaded()
{
    return radosCreate2 != nullptr && radosConfReadFile != nullptr && radosConnect != nullptr &&
        radosShutdown != nullptr && radosPoolLookup != nullptr && radosPoolCreate != nullptr &&
        radosIoctxCreate != nullptr && radosIoctxDestroy != nullptr && radosWrite != nullptr &&
        radosRead != nullptr && radosRemove != nullptr && radosStat != nullptr &&
        radosNobjectsListOpen != nullptr && radosNobjectsListNext != nullptr &&
        radosNobjectsListClose != nullptr;
}

int DlRadosApi::LoadRadosMethod(void *handle)
{
    DLSYM_RETURN(handle, "rados_create2", MethodRadosCreate2, radosCreate2);
    DLSYM_RETURN(handle, "rados_conf_read_file", MethodRadosConfReadFile, radosConfReadFile);
    DLSYM_RETURN(handle, "rados_connect", MethodRadosConnect, radosConnect);
    DLSYM_RETURN(handle, "rados_shutdown", MethodRadosShutdown, radosShutdown);
    DLSYM_RETURN(handle, "rados_pool_lookup", MethodRadosPoolLookup, radosPoolLookup);
    DLSYM_RETURN(handle, "rados_pool_create", MethodRadosPoolCreate, radosPoolCreate);
    DLSYM_RETURN(handle, "rados_ioctx_create", MethodRadosIoctxCreate, radosIoctxCreate);
    DLSYM_RETURN(handle, "rados_ioctx_destroy", MethodRadosIoctxDestroy, radosIoctxDestroy);
    DLSYM_RETURN(handle, "rados_write", MethodRadosWrite, radosWrite);
    DLSYM_RETURN(handle, "rados_read", MethodRadosRead, radosRead);
    DLSYM_RETURN(handle, "rados_remove", MethodRadosRemove, radosRemove);
    DLSYM_RETURN(handle, "rados_stat", MethodRadosStat, radosStat);
    DLSYM_RETURN(handle, "rados_nobjects_list_open", MethodRadosNobjectsListOpen, radosNobjectsListOpen);
    DLSYM_RETURN(handle, "rados_nobjects_list_next", MethodRadosNobjectsListNext, radosNobjectsListNext);
    DLSYM_RETURN(handle, "rados_nobjects_list_close", MethodRadosNobjectsListClose, radosNobjectsListClose);
    return 0;
}

int DlRadosApi::GetLibPath(std::string libDir, std::string &radosPath)
{
    if (libDir.back() != '/') {
        libDir.push_back('/');
    }

    for (size_t i = 0; i < gRadosLibNameCount; ++i) {
        radosPath = libDir + gRadosLibNames[i];
        if (::access(radosPath.c_str(), F_OK) == 0) {
            return 0;
        }
    }

    LOG_ERROR("librados path is invalid");
    return -1;
}

int DlRadosApi::LoadRadosByPath(const std::string &radosPath)
{
    auto radosHandle = dlopen(radosPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (radosHandle == nullptr) {
        LOG_ERROR("Failed to dlopen " << radosPath << " err: " << dlerror());
        return -1;
    }

    if (LoadRadosMethod(radosHandle) == -1) {
        LOG_ERROR("Failed to load " << radosPath << " method err: " << dlerror());
        dlclose(radosHandle);
        return -1;
    }

    gStarted = true;
    return 0;
}

int DlRadosApi::LoadRadosApiDl(const std::string &libPath)
{
    LOG_INFO("Starting to load rados api");
    if (gStarted || IsRadosMethodLoaded()) {
        return 0;
    }

    if (libPath.empty()) {
        for (size_t i = 0; i < gRadosLibNameCount; ++i) {
            if (LoadRadosByPath(gRadosLibNames[i]) == 0) {
                return 0;
            }
        }
        return -1;
    }

    std::string radosPath;
    if (GetLibPath(libPath, radosPath) != 0) {
        return -1;
    }
    return LoadRadosByPath(radosPath);
}
}
}
