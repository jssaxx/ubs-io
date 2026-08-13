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
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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
constexpr const char *KVC_DEPLOYMENT_MODE_ENV = "UBSIO_KV_DEPLOY_MODE";
constexpr const char *KVC_MODE_STANDALONE = "standalone";
constexpr const char *KVC_MODE_SEPARATES = "separates";

uint32_t GetStandaloneDeviceId(int32_t devId)
{
    return devId == KVC_NO_DEVICE_ID ? DEFAULT_STANDALONE_DEVICE_ID : static_cast<uint32_t>(devId);
}

bool GetKvcWorkerMode(WorkerMode &mode)
{
    const char *envMode = std::getenv(KVC_DEPLOYMENT_MODE_ENV);
    if (envMode == nullptr || envMode[0] == '\0') {
        mode = WorkerMode::STANDALONE;
        return true;
    }

    std::string configuredMode(envMode);
    std::transform(configuredMode.begin(), configuredMode.end(), configuredMode.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (configuredMode == KVC_MODE_STANDALONE) {
        mode = WorkerMode::STANDALONE;
        return true;
    }
    if (configuredMode == KVC_MODE_SEPARATES) {
        mode = WorkerMode::SEPARATES;
        return true;
    }
    return false;
}
}

namespace ock {
namespace ubsio {

bool DlBioSdkApi::gLoaded = false;
WorkerMode DlBioSdkApi::gWorkerMode = WorkerMode::STANDALONE;
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
BioRegisterMetaEventCallbackFunc DlBioSdkApi::pBioRegisterMetaEventCallback = nullptr;

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
    DL_LOAD_SYM(pBioRegisterMetaEventCallback, BioRegisterMetaEventCallbackFunc, bioSdkHandle,
        "BioRegisterMetaEventCallback");

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
    pBioRegisterMetaEventCallback = nullptr;
    gWorkerMode = WorkerMode::STANDALONE;

    if (bioSdkHandle != nullptr) {
        dlclose(bioSdkHandle);
        bioSdkHandle = nullptr;
    }
    gLoaded = false;
}

int32_t DlBioSdkApi::KvBioInit(int32_t devId)
{
    LOG_INFO("Start boostio begin...");
    if (devId < KVC_NO_DEVICE_ID) {
        LOG_ERROR("Invalid device id:" << devId << ".");
        return -1;
    }

    WorkerMode workerMode = WorkerMode::STANDALONE;
    if (!GetKvcWorkerMode(workerMode)) {
        LOG_ERROR("Invalid " << KVC_DEPLOYMENT_MODE_ENV << ", expected standalone or separates.");
        return -1;
    }

    if (workerMode == WorkerMode::STANDALONE) {
        auto standaloneDeviceId = GetStandaloneDeviceId(devId);
        if (devId == KVC_NO_DEVICE_ID) {
            LOG_INFO("Use default standalone device id:" << standaloneDeviceId << " for kv device id:" << devId << ".");
        }
        SetStandaloneDevice(standaloneDeviceId);
    }

    ClientOptionsConfig optConf{};
    optConf.logType = (LogType)(1);
    optConf.enable = false;
    std::string logDir = "/var/log/boostio";
    std::snprintf(optConf.logFilePath, sizeof(optConf.logFilePath), "%s", logDir.c_str());

    auto ret = Initialize(workerMode, &optConf);
    if (ret != 0) {
        LOG_ERROR("boostio initialize failed, ret: " << ret);
        return -1;
    }
    gWorkerMode = workerMode;
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
