/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include "bio_c.h"
#include <cstring>
#include "securec.h"
#include "bio.h"

using namespace ock::bio;

int32_t BioCalculateLocation(void *bioHandle, uint32_t objectId, CobjLocation *objLocation)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location = {0, 0};

    auto ret = bio->CalculateLocation(objectId, location);
    if (ret != 0) {
        return ret;
    }
    objLocation->location[0] = location.location[0];
    objLocation->location[1] = location.location[1];
    return ret;
}

void *BioCreateCache(uint64_t tenantId, CAffinityStrategy affinityStrategy, CWriteStrategy writeStrategy)
{
    BioService::Descriptor descriptor;
    descriptor.tenantId = tenantId;
    descriptor.affinity = static_cast<AffinityStrategy>(affinityStrategy);
    descriptor.strategy = static_cast<WriteStrategy>(writeStrategy);
    return BioService::CreateCache(descriptor).get();
}

void *BioNewService()
{
    auto bs = new BioService();
    if (bs->Initialize() != RET_CACHE_OK) {
        return nullptr;
    }
    return bs;
}

int32_t BioPut(void *bioHandle, const char *key, const char *value, uint64_t length, CobjLocation objLocation)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location = {objLocation.location[0], objLocation.location[1]};
    auto ret = bio->Put(key, value, length, location);
    return ret;
}

int32_t BioGet(void *bioHandle, const char *key, uint64_t offset, uint64_t length, char *value,
    CobjLocation objLocation, uint64_t *realLength)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location = {objLocation.location[0], objLocation.location[1]};
    auto ret = bio->Get(key, offset, length, location, value, *realLength);
    return ret;
}

int32_t BioDelete(void *bioHandle, const char *key, CobjLocation objLocation)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location = {objLocation.location[0], objLocation.location[1]};
    return bio->Delete(key, location);
}

int32_t BioLoad(void *bioHandle, const char *key, uint64_t offset, uint64_t length,
    CobjLocation objLocation, LoadCallback callback, void *context)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location = {objLocation.location[0], objLocation.location[1]};
    auto ret = bio->Load(key, offset, length, location, callback, context);
    return ret;
}

int32_t BioListAll(void *bioHandle, const char *prefix, PairStat **allObj, uint64_t *objNum)
{
    std::vector<std::pair<char *, Bio::ObjStat>> objs;
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    auto ret = bio->ListAll(prefix, objs);
    if (ret != RET_CACHE_OK) {
        return ret;
    }
    *objNum = objs.size();
    *allObj = reinterpret_cast<PairStat*>(malloc(*objNum * sizeof(PairStat)));
    int i = 0;
    for (auto ele : objs) {
        error_t result = strcpy_s((*allObj)[i].key, MAX_KEY_SIZE, ele.first);
        if (result != 0) {
            free(*allObj);
            *allObj= nullptr;
            return result;
        }
        (*allObj)[i].stat.size = ele.second.size;
        (*allObj)[i].stat.time = ele.second.time;
        i++;
    }
    return ret;
}

CobjStat BioStat(void *bioHandle, const char *key, CobjLocation objLocation)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location = {objLocation.location[0], objLocation.location[1]};
    auto stat = bio->Stat(key, location);
    return {stat.size, stat.time};
}

uint64_t BioGetTenantId(void *bioHandle)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    return bio->GetTenantId();
}

CAffinityStrategy BioGetAffinityPolicy(void *bioHandle)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    return static_cast<CAffinityStrategy>(bio->GetAffinityPolicy());
}

CWriteStrategy BioGetWriteStrategy(void *bioHandle)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    return static_cast<CWriteStrategy>(bio->GetWriteStrategy());
}

void BioFreeService()
{
    BioService::Exit();
}

void *BioGetCache(uint64_t tenantId)
{
    return BioService::GetCache(tenantId).get();
}

PairCache *BioListCache(uint64_t *cacheNum)
{
    auto cacheMap = BioService::ListCache();
    *cacheNum = cacheMap.size();
    auto *allCache = reinterpret_cast<PairCache*>(malloc(*cacheNum * sizeof(PairCache)));
    if (allCache == nullptr) {
        *cacheNum = 0;
        return nullptr;
    }
    int i = 0;
    for (auto &it : cacheMap) {
        allCache[i].tenantId = it.first;
        allCache[i].bio = it.second.get();
        i++;
    }
    return allCache;
}

void BioDestroyCache(uint64_t tenantId)
{
    BioService::DestroyCache(tenantId);
}