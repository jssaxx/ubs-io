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
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_err.h"

namespace ock {
namespace ubsio {

using BioExitFunc = void (*)(void);
using BioInitFunc = CResult (*)(WorkerMode mode, ClientOptionsConfig *optConf, int32_t devId);
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
using BioRegisterMemFunc = CResult (*)(uint64_t *addrs, uint64_t *size, uint32_t count);
using BioBatchGetPositionsFunc = CResult (*)(uint64_t tenantId, const char **keys, uint32_t count, ObjLocation *locations, uint8_t *position);
using BioBatchGetLocalFunc = CResult (*)(uint64_t tenantId, const char **keys, const uint32_t count, uint64_t *lengths,
                                         ObjLocation *locations, uintptr_t *valueAddrs, int32_t *results);
using BioBatchGetRemoteFunc = CResult (*)(uint64_t tenantId, const char **keys, const uint32_t count,
                                          ObjLocation *locations, uintptr_t **memAddr, size_t **memSize,
                                          uint32_t row, uint32_t col, uintptr_t *valueAddrs, int32_t *results);


class DlBioSdkApi {
public:
    DlBioSdkApi() = delete;
    static int32_t LoadLibrary();
    static void CleanupLibrary();

    static CResult Initialize(WorkerMode mode, ClientOptionsConfig *optConf, int32_t devId)
    {
        return static_cast<CResult>(pBioInitialize(mode, optConf, devId));
    }

    static CResult CreateCache(CacheDescriptor desc)
    {
        return static_cast<CResult>(pBioCreateCache(desc));
    }

    static CResult CalcLocation(uint64_t tenantId, uint64_t objectId, ObjLocation *location)
    {
        return static_cast<CResult>(pBioCalcLocation(tenantId, objectId, location));
    }

    static CResult Get(uint64_t tenantId, const char *key, uint64_t offset, uint64_t length, ObjLocation location, char *value,
                uint64_t *realLength)
    {
        return static_cast<CResult>(pBioGet(tenantId, key, offset, length, location, value, realLength));
    }

    static CResult Put(uint64_t tenantId, const char *key, const char *value, uint64_t length, ObjLocation location)
    {
        return static_cast<CResult>(pBioPut(tenantId, key, value, length, location));
    }

    static CResult Stat(uint64_t tenantId, const char *key, ObjLocation location, ObjStat *stat)
    {
        return static_cast<CResult>(pBioStat(tenantId, key, location, stat));
    }

    static CResult BatchExist(uint64_t tenantId, const char *key[], ObjLocation location[], uint32_t count, bool result[])
    {
        return static_cast<CResult>(pBioBatchExist(tenantId, key, location, count, result));
    }

    static CResult BatchGet(uint64_t tenantId, const char **keys, const uint32_t count, uint64_t *offsets, uint64_t *lengths,
                     ObjLocation *locations, uintptr_t *valueAddrs, uint64_t *realLengths, int32_t *results)
    {
        return static_cast<CResult>(pBioBatchGet(tenantId, keys, count, offsets, lengths,
            locations, valueAddrs, realLengths, results));
    }

    static CResult BatchGetFree(uint64_t tenantId, uintptr_t *valueAddrs, const uint32_t count)
    {
        return static_cast<CResult>(pBioBatchGetFree(tenantId, valueAddrs, count));
    }

    static CResult Delete(uint64_t tenantId, const char *key, ObjLocation location)
    {
        return static_cast<CResult>(pBioDelete(tenantId, key, location));
    }

    static void Exit(void)
    {
        pBioExit();
    }

    static int32_t KvBioInit(int32_t devId);

    static CResult BatchGetKeyDiskAddr(uint64_t tenantId, const char **keys, ObjLocation *locations,
                                       const uint32_t count, KeyAddrInfo *infos)
    {
        return static_cast<CResult>(pBioBatchGetKeyDiskAddr(tenantId, keys, locations, count, infos));
    }

    static CResult RegisterMem(uint64_t *addrs, uint64_t *size, uint32_t count)
    {
        return static_cast<CResult>(pBioRegisterMem(addrs, size, count));
    }

    static CResult BatchGetPositions(uint64_t tenantId, const char **keys, uint32_t count,
                                     ObjLocation *locations, uint8_t *position)
    {
        return static_cast<CResult>(pBioBatchGetPositions(tenantId, keys, count, locations, position));
    }

    static CResult BatchGetLocal(uint64_t tenantId, const char **keys, const uint32_t count, uint64_t *lengths,
                                 ObjLocation *locations, uintptr_t *valueAddrs, int32_t *results)
    {
        return static_cast<CResult>(pBioBatchGetLocal(tenantId, keys, count, lengths, locations, valueAddrs, results));
    }

    static CResult BatchGetRemote(uint64_t tenantId, const char **keys, const uint32_t count,
                                 ObjLocation *locations, uintptr_t **memAddr, size_t **memSize,
                                 uint32_t row, uint32_t col, uintptr_t *valueAddrs, int32_t *results)
    {
        return static_cast<CResult>(pBioBatchGetRemote(tenantId, keys, count, locations, memAddr, memSize,
                                    row, col, valueAddrs, results));
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
    static BioRegisterMemFunc pBioRegisterMem;
    static BioBatchGetPositionsFunc pBioBatchGetPositions;
    static BioBatchGetLocalFunc pBioBatchGetLocal;
    static BioBatchGetRemoteFunc pBioBatchGetRemote;
};

}  // namespace ubsio
}  // namespace ock
#endif  // DL_BIOSDK_API_H