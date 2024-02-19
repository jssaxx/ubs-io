/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include "bio_c.h"
#include "bio.h"

using namespace ock::bio;

int32_t BioCalculateLocation(void *bioHandle, uint32_t objectId, uint64_t *location1, uint64_t *location2)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location;

    auto ret = bio->CalculateLocation(objectId, location);
    if (ret != 0) {
        return ret;
    }
    *location1 = location.location[0];
    *location2 = location.location[1];
    return ret;
}

void *BioCreateCache(void *bioHandle)
{
    auto bio = reinterpret_cast<BioService *>(bioHandle);
    BioService::Descriptor descriptor;
    descriptor.tenantId = 1;
    descriptor.affinity = AffinityStrategy::GLOBAL_BALANCE;
    descriptor.strategy = WriteStrategy::WRITE_BACK;
    return bio->CreateCache(descriptor).get();
}

void *BioNewService()
{
    auto bs = new BioService();
    if (bs->Initialize() != RET_CACHE_OK) {
        return nullptr;
    }
    return bs;
}

int32_t BioPut(void *bioHandle, const char *key, const char *value, uint64_t length, uint64_t location1,
    uint64_t location2)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location;
    location.location[0] = location1;
    location.location[1] = location2;
    auto ret = bio->Put(key, value, length, location);
    return ret;
}

int32_t BioGet(void *bioHandle, const char *key, uint64_t offset, uint64_t length, char *value, uint64_t location1,
    uint64_t location2, uint64_t *realLength)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location;
    location.location[0] = location1;
    location.location[1] = location2;
    auto ret = bio->Get(key, offset, length, location, value, *realLength);
    return ret;
}

int32_t BioDelete(void *bioHandle, const char *key, uint64_t location1, uint64_t location2)
{
    auto bio = reinterpret_cast<Bio *>(bioHandle);
    Bio::ObjLocation location;
    location.location[0] = location1;
    location.location[1] = location2;
    return bio->Delete(key, location);
}