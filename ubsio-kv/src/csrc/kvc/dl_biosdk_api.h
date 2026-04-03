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

#ifndef DL_BIOSDK_API_H
#define DL_BIOSDK_API_H

#include <dlfcn.h>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include "bio_c.h"
#include "dfc_log.h"
#include "dfc_err.h"

namespace ock {
namespace ubsio {

using BioExitFunc = void (*)(void);
using BioInitFunc = CResult (*)(WorkerMode mode, ClientOptionsConfig *optConf);
using BioCreateCacheFunc = CResult (*)(CacheDescriptor desc);
using BioCalLocationFunc = CResult (*)(uint64_t tenantId, uint64_t objectId, ObjLocation *location);
using BioGetFunc = CResult (*)(uint64_t tenantId, const char *key, uint64_t offset, uint64_t length, ObjLocation location,
                           char *value, uint64_t *realLength);
using BioPutFunc = CResult (*)(uint64_t tenantId, const char *key, const char *value, uint64_t length, ObjLocation location);
using BioStatFunc = CResult (*)(uint64_t tenantId, const char *key, ObjLocation location, ObjStat *stat);
using BioBatchGetFunc = CResult (*)(uint64_t tenantId, const char **keys, const uint32_t count, uint64_t *offsets,
                                uint64_t *lengths, ObjLocation *locations, uintptr_t *valueAddrs, uint64_t *realLengths, int32_t *results);
using BioBatchExistFunc = CResult (*)(uint64_t tenantId, const char *key[], ObjLocation location[], uint32_t count, bool result[]);
using BioBatchFreeFunc = CResult (*)(uint64_t tenantId, uintptr_t *valueAddrs, const uint32_t count);
using BioDeleteFunc = CResult (*)(uint64_t tenantId, const char *key, ObjLocation location);
using BioBatchGetKeyDiskAddrFunc = CResult (*)(uint64_t tenantId, const char **keys, ObjLocation *locations,
                                               const uint32_t count, KeyAddrInfo *infos);

class DlBioSdkApi {
public:
    DlBioSdkApi() = delete;
    static int32_t LoadLibrary();
    static void CleanupLibrary();

    static CResult Initialize(WorkerMode mode, ClientOptionsConfig *optConf)
    {
        int ret = 0;
        if (pBioInitialize != nullptr) {
            ret = pBioInitialize(mode, optConf);
        } else {
            LOG_ERROR("dfcInit failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult CreateCache(CacheDescriptor desc)
    {
        int ret = 0;
        if (pBioCreateCache != nullptr) {
            ret = pBioCreateCache(desc);
        } else {
            LOG_ERROR("create cache failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult CalcLocation(uint64_t tenantId, uint64_t objectId, ObjLocation *location)
    {
        int ret = 0;
        if (pBioCalcLocation != nullptr) {
            ret = pBioCalcLocation(tenantId, objectId, location);
        } else {
            LOG_ERROR("cal location failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult Get(uint64_t tenantId, const char *key, uint64_t offset, uint64_t length, ObjLocation location, char *value,
                uint64_t *realLength)
    {
        int ret = 0;
        if (pBioGet != nullptr) {
            ret = pBioGet(tenantId, key, offset, length, location, value, realLength);
        } else {
            LOG_ERROR("dfcGet failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult Put(uint64_t tenantId, const char *key, const char *value, uint64_t length, ObjLocation location)
    {
        int ret = 0;
        if (pBioPut != nullptr) {
            ret = pBioPut(tenantId, key, value, length, location);
        } else {
            LOG_ERROR("dfcPut failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult Stat(uint64_t tenantId, const char *key, ObjLocation location, ObjStat *stat)
    {
        int ret = 0;
        if (pBioStat != nullptr) {
            ret = pBioStat(tenantId, key, location, stat);
        } else {
            LOG_ERROR("dfcStat failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult BatchExist(uint64_t tenantId, const char *key[], ObjLocation location[], uint32_t count, bool result[])
    {
        int ret = 0;
        if (pBioBatchExist != nullptr) {
            ret = pBioBatchExist(tenantId, key, location, count, result);
        } else {
            LOG_ERROR("BioBatchExist failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult BatchGet(uint64_t tenantId, const char **keys, const uint32_t count, uint64_t *offsets, uint64_t *lengths,
                     ObjLocation *locations, uintptr_t *valueAddrs, uint64_t *realLengths, int32_t *results)
    {
        int ret = 0;
        if (pBioBatchGet != nullptr) {
            ret = pBioBatchGet(tenantId, keys, count, offsets, lengths, locations, valueAddrs, realLengths, results);
        } else {
            LOG_ERROR("BioBatchGet failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult BatchGetFree(uint64_t tenantId, uintptr_t *valueAddrs, const uint32_t count)
    {
        int ret = 0;
        if (pBioBatchGetFree != nullptr) {
            ret = pBioBatchGetFree(tenantId, valueAddrs, count);
        } else {
            LOG_ERROR("BioBatchGetFree failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static CResult Delete(uint64_t tenantId, const char *key, ObjLocation location)
    {
        int ret = 0;
        if (pBioDelete != nullptr) {
            ret = pBioDelete(tenantId, key, location);
        } else {
            LOG_ERROR("BioDelete failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

    static void Exit(void)
    {
        if (pBioExit != nullptr) {
            pBioExit();
        } else {
            LOG_ERROR("dfcExit failed");
        }
    }

    static int32_t DfcKvBioInit(void);

    static CResult BatchGetKeyDiskAddr(uint64_t tenantId, const char **keys, ObjLocation *locations,
                                       const uint32_t count, KeyAddrInfo *infos)
    {
        int ret = 0;
        if (pBioBatchGetKeyDiskAddr != nullptr) {
            ret = pBioBatchGetKeyDiskAddr(tenantId, keys, locations, count, infos);
        } else {
            LOG_ERROR("BioBatchGetKeyDiskAddr failed");
            ret = RET_CACHE_ERROR;
        }
        return static_cast<CResult>(ret);
    }

private:
    static std::mutex gMutex;
    static bool gLoaded;
    static void *bioSdkHandle;
    static const std::string gBioSdkLibName;

    static BioExitFunc pBioExit;
    static BioInitFunc pBioInitialize;
    static BioGetFunc pBioGet;
    static BioPutFunc pBioPut;
    static BioStatFunc pBioStat;
    static BioCreateCacheFunc pBioCreateCache;
    static BioCalLocationFunc pBioCalcLocation;
    static BioBatchGetFunc pBioBatchGet;
    static BioBatchExistFunc pBioBatchExist;
    static BioBatchFreeFunc pBioBatchGetFree;
    static BioDeleteFunc pBioDelete;
    static BioBatchGetKeyDiskAddrFunc pBioBatchGetKeyDiskAddr;
};

}  // namespace ubsio
}  // namespace ock
#endif  // DL_BIOSDK_API_H