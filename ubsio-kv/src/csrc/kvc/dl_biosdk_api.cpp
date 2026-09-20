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

namespace ock {
namespace ubsio {

namespace {
constexpr LogLevel BIO_TO_KVC_LOG_LEVEL[BIO_LOG_LEVEL_BUTT] = {
    DEBUG_LEVEL, DEBUG_LEVEL, INFO_LEVEL, WARN_LEVEL, ERROR_LEVEL
};

inline bool IsBioLogLevelValid(int32_t level)
{
    return level >= static_cast<int32_t>(BIO_LOG_LEVEL_TRACE) &&
        level < static_cast<int32_t>(BIO_LOG_LEVEL_BUTT);
}

void SetKvcLogLevel(LogLevel level)
{
    int32_t ret = UbsioLog::Instance().SetLogLevel(level);
    if (UNLIKELY(ret != 0)) {
        LOG_WARN("Set KVC log level failed, ret:" << ret << ", level:" << static_cast<int32_t>(level) << ".");
    }
}

void SyncKvcLogLevel()
{
    BioLogLevel bioLogLevel = BIO_LOG_LEVEL_INFO;
    CResult ret = DlBioSdkApi::GetLogLevel(&bioLogLevel);
    if (UNLIKELY(ret != RET_CACHE_OK)) {
        LOG_WARN("Get effective boostio log level failed, ret:" << ret << ". Use default KVC log level.");
        SetKvcLogLevel(INFO_LEVEL);
        return;
    }
    int32_t levelIndex = static_cast<int32_t>(bioLogLevel);
    if (UNLIKELY(!IsBioLogLevelValid(levelIndex))) {
        LOG_WARN("Get invalid boostio log level, level:" << levelIndex << ". Use default KVC log level.");
        SetKvcLogLevel(INFO_LEVEL);
        return;
    }
    SetKvcLogLevel(BIO_TO_KVC_LOG_LEVEL[static_cast<size_t>(levelIndex)]);
}
}

bool DlBioSdkApi::gLoaded = false;
std::mutex DlBioSdkApi::gMutex;
void *DlBioSdkApi::bioSdkHandle = nullptr;
const std::string DlBioSdkApi::gBioSdkLibName = "libbio_sdk.so";

BioExitFunc DlBioSdkApi::pBioExit = nullptr;
BioInitFunc DlBioSdkApi::pBioInitialize = nullptr;
BioGetLogLevelFunc DlBioSdkApi::pBioGetLogLevel = nullptr;
BioCreateCacheFunc DlBioSdkApi::pBioCreateCache = nullptr;
BioCalLocationFunc DlBioSdkApi::pBioCalcLocation = nullptr;
BioGetFunc DlBioSdkApi::pBioGet = nullptr;
BioPutFunc DlBioSdkApi::pBioPut = nullptr;
BioStatFunc DlBioSdkApi::pBioStat = nullptr;
BioBatchStatFunc DlBioSdkApi::pBioBatchStat = nullptr;
BioBatchGetFunc DlBioSdkApi::pBioBatchGet = nullptr;
BioBatchExistFunc DlBioSdkApi::pBioBatchExist = nullptr;
BioBatchFreeFunc DlBioSdkApi::pBioBatchGetFree = nullptr;
BioDeleteFunc DlBioSdkApi::pBioDelete = nullptr;
BioBatchGetKeyDiskAddrFunc DlBioSdkApi::pBioBatchGetKeyDiskAddr = nullptr;
BioRegisterMetaEventCallbackFunc DlBioSdkApi::pBioRegisterMetaEventCallback = nullptr;
BioShowLocalCacheResourceFunc DlBioSdkApi::pBioShowLocalCacheResource = nullptr;
BioScanKeyFunc DlBioSdkApi::pBioScanKey = nullptr;
BioFreeScanKeyResultFunc DlBioSdkApi::pBioFreeScanKeyResult = nullptr;

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
    pBioGetLogLevel = reinterpret_cast<BioGetLogLevelFunc>(dlsym(bioSdkHandle, "BioGetLogLevel"));
    DL_LOAD_SYM(pBioGet, BioGetFunc, bioSdkHandle, "BioGet");
    DL_LOAD_SYM(pBioPut, BioPutFunc, bioSdkHandle, "BioPut");
    DL_LOAD_SYM(pBioStat, BioStatFunc, bioSdkHandle, "BioStat");
    DL_LOAD_SYM(pBioBatchStat, BioBatchStatFunc, bioSdkHandle, "BioBatchStat");
    DL_LOAD_SYM(pBioCreateCache, BioCreateCacheFunc, bioSdkHandle, "BioCreateCache");
    DL_LOAD_SYM(pBioCalcLocation, BioCalLocationFunc, bioSdkHandle, "BioCalcLocation");
    DL_LOAD_SYM(pBioBatchGet, BioBatchGetFunc, bioSdkHandle, "BioBatchGet");
    DL_LOAD_SYM(pBioBatchExist, BioBatchExistFunc, bioSdkHandle, "BioBatchExist");
    DL_LOAD_SYM(pBioBatchGetFree, BioBatchFreeFunc, bioSdkHandle, "BioBatchGetFree");
    DL_LOAD_SYM(pBioDelete, BioDeleteFunc, bioSdkHandle, "BioDelete");
    DL_LOAD_SYM(pBioBatchGetKeyDiskAddr, BioBatchGetKeyDiskAddrFunc, bioSdkHandle, "BioBatchGetKeyDiskAddr");
    DL_LOAD_SYM(pBioRegisterMetaEventCallback, BioRegisterMetaEventCallbackFunc, bioSdkHandle,
        "BioRegisterMetaEventCallback");
    DL_LOAD_SYM(pBioShowLocalCacheResource, BioShowLocalCacheResourceFunc, bioSdkHandle,
        "BioShowLocalCacheResource");
    DL_LOAD_SYM(pBioScanKey, BioScanKeyFunc, bioSdkHandle, "BioScanKey");
    DL_LOAD_SYM(pBioFreeScanKeyResult, BioFreeScanKeyResultFunc, bioSdkHandle, "BioFreeScanKeyResult");

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
    pBioGetLogLevel = nullptr;
    pBioGet = nullptr;
    pBioPut = nullptr;
    pBioStat = nullptr;
    pBioBatchStat = nullptr;
    pBioCreateCache = nullptr;
    pBioCalcLocation = nullptr;
    pBioBatchGet = nullptr;
    pBioBatchExist = nullptr;
    pBioBatchGetFree = nullptr;
    pBioDelete = nullptr;
    pBioBatchGetKeyDiskAddr = nullptr;
    pBioRegisterMetaEventCallback = nullptr;
    pBioShowLocalCacheResource = nullptr;
    pBioScanKey = nullptr;
    pBioFreeScanKeyResult = nullptr;

    if (bioSdkHandle != nullptr) {
        dlclose(bioSdkHandle);
        bioSdkHandle = nullptr;
    }
    gLoaded = false;
}

int32_t DlBioSdkApi::KvBioInit()
{
    LOG_INFO("Start boostio begin...");
    ClientOptionsConfig optConf{};
    optConf.logType = (LogType)(1);
    optConf.enable = false;
    std::string logDir = "/var/log/ubsio";
    std::snprintf(optConf.logFilePath, sizeof(optConf.logFilePath), "%s", logDir.c_str());

    auto ret = Initialize(WorkerMode::STANDALONE, &optConf);
    if (ret != 0) {
        LOG_ERROR("boostio initialize failed, ret: " << ret);
        return -1;
    }
    SyncKvcLogLevel();
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
