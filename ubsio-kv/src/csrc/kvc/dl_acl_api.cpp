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

#include <cstdint>
#include <cstdio>
#include <string>
#include <dlfcn.h>
#include <iostream>
#include <cstring>
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_def.h"
#include "dl_acl_api.h"

namespace ock {
namespace ubsio {

bool ACLApi::gLoaded = false;
std::mutex ACLApi::gMutex;
void *ACLApi::aclHandle = nullptr;
static std::string g_ascendAclLibName = "libascendcl.so";

AclrtGetDeviceFunc ACLApi::pAclrtGetDevice = nullptr;
AclrtSetDeviceFunc ACLApi::pAclrtSetDevice = nullptr;
AclrtCreateStreamFunc ACLApi::pAclrtCreateStream = nullptr;
AclrtCreateStreamWithConfigFunc ACLApi::pAclrtCreateStreamWithConfig = nullptr;
AclrtDestroyStreamFunc ACLApi::pAclrtDestroyStream = nullptr;
AclrtSynchronizeStreamFunc ACLApi::pAclrtSynchronizeStream = nullptr;
AclrtMallocFunc ACLApi::pAclrtMalloc = nullptr;
AclrtFreeFunc ACLApi::pAclrtFree = nullptr;
AclrtMallocHostFunc ACLApi::pAclrtMallocHost = nullptr;
AclrtFreeHostFunc ACLApi::pAclrtFreeHost = nullptr;
AclrtMemcpyFunc ACLApi::pAclrtMemcpy = nullptr;
AclrtMemcpyBatchFunc ACLApi::pAclrtMemcpyBatch = nullptr;
AclrtMemcpyAsyncFunc ACLApi::pAclrtMemcpyAsync = nullptr;
AclrtMemcpy2dFunc ACLApi::pAclrtMemcpy2d = nullptr;
AclrtMemcpy2dAsyncFunc ACLApi::pAclrtMemcpy2dAsync = nullptr;
AclrtMemsetFunc ACLApi::pAclrtMemset = nullptr;
RtGetDeviceInfoFunc ACLApi::pRtGetDeviceInfo = nullptr;


int32_t ACLApi::LoadLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gLoaded) {
        return 0;
    }
    char *path = std::getenv("ASCEND_HOME_PATH");
    if (path == nullptr) {
        LOG_ERROR("ASCEND_HOME_PATH is not set");
        return DFC_ERR;
    }
    std::string libPath = std::string(path) + "/lib64/" + g_ascendAclLibName;
    /* dlopen library */
    aclHandle = dlopen(libPath.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (aclHandle == nullptr) {
        LOG_ERROR("Failed to open library [" << libPath << "], error: " << dlerror());
        return DFC_ERR;
    }

    /* load sym */
    DL_LOAD_SYM(pAclrtGetDevice, AclrtGetDeviceFunc, aclHandle, "aclrtGetDevice");
    DL_LOAD_SYM(pAclrtSetDevice, AclrtSetDeviceFunc, aclHandle, "aclrtSetDevice");
    DL_LOAD_SYM(pAclrtCreateStream, AclrtCreateStreamFunc, aclHandle, "aclrtCreateStream");
    DL_LOAD_SYM(pAclrtCreateStreamWithConfig, AclrtCreateStreamWithConfigFunc,
        aclHandle, "aclrtCreateStreamWithConfig");
    DL_LOAD_SYM(pAclrtDestroyStream, AclrtDestroyStreamFunc, aclHandle, "aclrtDestroyStream");
    DL_LOAD_SYM(pAclrtSynchronizeStream, AclrtSynchronizeStreamFunc, aclHandle, "aclrtSynchronizeStream");
    DL_LOAD_SYM(pAclrtMalloc, AclrtMallocFunc, aclHandle, "aclrtMalloc");
    DL_LOAD_SYM(pAclrtFree, AclrtFreeFunc, aclHandle, "aclrtFree");
    DL_LOAD_SYM(pAclrtMallocHost, AclrtMallocHostFunc, aclHandle, "aclrtMallocHost");
    DL_LOAD_SYM(pAclrtFreeHost, AclrtFreeHostFunc, aclHandle, "aclrtFreeHost");
    DL_LOAD_SYM(pAclrtMemcpy, AclrtMemcpyFunc, aclHandle, "aclrtMemcpy");
    DL_LOAD_SYM(pAclrtMemcpyBatch, AclrtMemcpyBatchFunc, aclHandle, "aclrtMemcpyBatch");
    DL_LOAD_SYM(pAclrtMemcpyAsync, AclrtMemcpyAsyncFunc, aclHandle, "aclrtMemcpyAsync");
    DL_LOAD_SYM(pAclrtMemcpy2d, AclrtMemcpy2dFunc, aclHandle, "aclrtMemcpy2d");
    DL_LOAD_SYM(pAclrtMemcpy2dAsync, AclrtMemcpy2dAsyncFunc, aclHandle, "aclrtMemcpy2dAsync");
    DL_LOAD_SYM(pAclrtMemset, AclrtMemsetFunc, aclHandle, "aclrtMemset");
    DL_LOAD_SYM(pRtGetDeviceInfo, RtGetDeviceInfoFunc, aclHandle, "rtGetDeviceInfo");

    gLoaded = true;
    return 0;
}

void ACLApi::CleanupLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (!gLoaded) {
        return;
    }
    pAclrtGetDevice = nullptr;
    pAclrtSetDevice = nullptr;
    pAclrtCreateStream = nullptr;
    pAclrtCreateStreamWithConfig = nullptr;
    pAclrtDestroyStream = nullptr;
    pAclrtSynchronizeStream = nullptr;
    pAclrtMalloc = nullptr;
    pAclrtFree = nullptr;
    pAclrtMallocHost = nullptr;
    pAclrtFreeHost = nullptr;
    pAclrtMemcpy = nullptr;
    pAclrtMemcpyBatch = nullptr;
    pAclrtMemcpyAsync = nullptr;
    pAclrtMemcpy2d = nullptr;
    pAclrtMemcpy2dAsync = nullptr;
    pAclrtMemset = nullptr;
    pRtGetDeviceInfo = nullptr;

    if (aclHandle != nullptr) {
        dlclose(aclHandle);
        aclHandle = nullptr;
    }
    gLoaded = false;
}

}  // namespace ubsio
}  // namespace ock