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

#include <stdint.h>
#include <cstdio>
#include <string>
#include <dlfcn.h>
#include <iostream>
#include <cstring>
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_def.h"
#include "dl_biosdk_api.h"

namespace {
constexpr int32_t KVC_NO_DEVICE_ID = -1;
constexpr uint32_t DEFAULT_STANDALONE_DEVICE_ID = 0;

uint32_t GetStandaloneDeviceId(int32_t devId)
{
    return devId == KVC_NO_DEVICE_ID ? DEFAULT_STANDALONE_DEVICE_ID : static_cast<uint32_t>(devId);
}
}

namespace ock {
namespace ubsio {

bool DlBioSdkApi::gLoaded = false;
std::mutex DlBioSdkApi::gMutex;
void *DlBioSdkApi::bioSdkHandle = nullptr;
const std::string DlBioSdkApi::gBioSdkLibName = "libbio_sdk.so";

BioExitFunc DlBioSdkApi::pBioExit = nullptr;
BioInitFunc DlBioSdkApi::pBioInitialize = nullptr;
BioSetStandaloneDeviceFunc DlBioSdkApi::pBioSetStandaloneDevice = nullptr;
BioCreateCacheFunc DlBioSdkApi::pBioCreateCache = nullptr;
BioCalLocationFunc DlBioSdkApi::pBioCalcLocation = nullptr;
BioGetFunc DlBioSdkApi::pBioGet = nullptr;
BioPutFunc DlBioSdkApi::pBioPut = nullptr;
BioStatFunc DlBioSdkApi::pBioStat = nullptr;
BioBatchGetFunc DlBioSdkApi::pBioBatchGet = nullptr;
BioBatchExistFunc DlBioSdkApi::pBioBatchExist = nullptr;
BioBatchFreeFunc DlBioSdkApi::pBioBatchGetFree = nullptr;
BioDeleteFunc DlBioSdkApi::pBioDelete = nullptr;
BioBatchGetKeyDiskAddrFunc DlBioSdkApi::pBioBatchGetKeyDiskAddr = nullptr;

int32_t DlBioSdkApi::LoadLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gLoaded) {
        return 0;
    }

    /* dlopen library */
    bioSdkHandle = dlopen(gBioSdkLibName.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (bioSdkHandle == nullptr) {
        LOG_ERROR("Failed to open library [" << gBioSdkLibName << "], error: " << dlerror());
        return -1;
    }

    /* load sym */
    DL_LOAD_SYM(pBioExit, BioExitFunc, bioSdkHandle, "BioExit");
    DL_LOAD_SYM(pBioInitialize, BioInitFunc, bioSdkHandle, "BioInitialize");
    DL_LOAD_SYM(pBioSetStandaloneDevice, BioSetStandaloneDeviceFunc, bioSdkHandle, "BioSetStandaloneDevice");
    DL_LOAD_SYM(pBioGet, BioGetFunc, bioSdkHandle, "BioGet");
    DL_LOAD_SYM(pBioPut, BioPutFunc, bioSdkHandle, "BioPut");
    DL_LOAD_SYM(pBioStat, BioStatFunc, bioSdkHandle, "BioStat");
    DL_LOAD_SYM(pBioCreateCache, BioCreateCacheFunc, bioSdkHandle, "BioCreateCache");
    DL_LOAD_SYM(pBioCalcLocation, BioCalLocationFunc, bioSdkHandle, "BioCalcLocation");
    DL_LOAD_SYM(pBioBatchGet, BioBatchGetFunc, bioSdkHandle, "BioBatchGet");
    DL_LOAD_SYM(pBioBatchExist, BioBatchExistFunc, bioSdkHandle, "BioBatchExist");
    DL_LOAD_SYM(pBioBatchGetFree, BioBatchFreeFunc, bioSdkHandle, "BioBatchGetFree");
    DL_LOAD_SYM(pBioDelete, BioDeleteFunc, bioSdkHandle, "BioDelete");
    DL_LOAD_SYM(pBioBatchGetKeyDiskAddr, BioBatchGetKeyDiskAddrFunc, bioSdkHandle, "BioBatchGetKeyDiskAddr");

    gLoaded = true;
    return 0;
}

void DlBioSdkApi::CleanupLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (!gLoaded) {
        return;
    }

    pBioExit = nullptr;
    pBioInitialize = nullptr;
    pBioSetStandaloneDevice = nullptr;
    pBioGet = nullptr;
    pBioPut = nullptr;
    pBioStat = nullptr;
    pBioCreateCache = nullptr;
    pBioCalcLocation = nullptr;
    pBioBatchGet = nullptr;
    pBioBatchExist = nullptr;
    pBioBatchGetFree = nullptr;
    pBioDelete = nullptr;
    pBioBatchGetKeyDiskAddr = nullptr;

    if (bioSdkHandle != nullptr) {
        dlclose(bioSdkHandle);
        bioSdkHandle = nullptr;
    }
    gLoaded = false;
}

int32_t DlBioSdkApi::KvBioInit(int32_t devId, uint64_t ssdSize)
{
    (void)ssdSize;
    LOG_INFO("Start boostio begin...");
    if (devId < KVC_NO_DEVICE_ID) {
        LOG_ERROR("Invalid device id:" << devId << ".");
        return -1;
    }

    auto standaloneDeviceId = GetStandaloneDeviceId(devId);
    if (devId == KVC_NO_DEVICE_ID) {
        LOG_INFO("Use default standalone device id:" << standaloneDeviceId << " for kv device id:" << devId << ".");
    }
    SetStandaloneDevice(standaloneDeviceId);

    ClientOptionsConfig optConf{};
    optConf.logType = (LogType)(1);
    optConf.enable = false;
    std::string logDir = "/var/log/boostio";
    std::snprintf(optConf.logFilePath, sizeof(optConf.logFilePath), "%s", logDir.c_str());

    auto ret = Initialize(WorkerMode::STANDALONE, &optConf);
    if (ret != 0) {
        LOG_ERROR("boostio initialize failed, ret: " << ret);
        return -1;
    }
    LOG_INFO("Start boostio success.");

    LOG_INFO("boostio createcache...");
    uint64_t tenantId = 1;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    ret = CreateCache({ tenantId, affinity, strategy });
    if (ret == RET_CACHE_EXISTS) {
        LOG_INFO("boostio cache already exist");
        return 0;
    }
    if (ret != 0) {
        LOG_ERROR("boostio createcache failed, ret: " << ret);
        return -1;
    }
    LOG_INFO("boostio createcache success.");

    return 0;
}

}  // namespace ubsio
}  // namespace ock
