/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include <cstdint>
#include "bio_log.h"
#include "rcache_evict.h"

using namespace ock::bio;

RCacheEvict::RCacheEvict():workIndex(0)
{
}

RCacheEvict::~RCacheEvict()
{
}

uint64_t RCacheEvict::GetEvictDataByTier(const RCachePtr rCache, RCacheTierType tier)
{
    uint64_t waterData;
    uint64_t cacheData = rCache->GetCacheData(tier);

    if (tier == READ_CACHE_TIER_MEM) {
        waterData = (READ_CACHE_MEM_RESOURCE_QUANTITY * READ_CACHE_EVICT_WATER_LEVEL) / 100;
    } else {
        waterData = (READ_CACHE_DISK_RESOURCE_QUANTITY * READ_CACHE_EVICT_WATER_LEVEL) / 100;
    }

    return cacheData > waterData ? cacheData - waterData : 0ULL;
}

BResult RCacheEvict::EvictOneRCacheHandle(RCachePtr rCache, RCacheTierType tier)
{
    uint64_t evictData = 0ULL;
    uint64_t evictTotalData = GetEvictDataByTier(rCache, tier);

    if (evictTotalData == 0ULL) {
        return BIO_NEED_WAIT;
    }

    int32_t ret;
    while (evictData < evictTotalData) {
        if (tier == READ_CACHE_TIER_MEM) {
            ret = rCache->EvictMemData(evictTotalData, evictData);
        } else {
            ret = rCache->EvictDiskData(evictTotalData, evictData);
        }

        if (ret == BIO_NEED_WAIT) {
            break;
        }

        if (evictData == 0ULL) {
            break;
        }

        evictTotalData -= evictData;
    }

    return BIO_OK;
}

BResult RCacheEvict::EvictHandle(uint32_t index, RCacheTierType tier)
{
    BResult result;
    RCachePtr rCache = nullptr;

    for (auto iter = evictRCache[index].begin(); iter != evictRCache[index].end();iter++) {
        rCache = *iter;
        result = EvictOneRCacheHandle(rCache, tier);
        if (result != BIO_NEED_WAIT && result != BIO_OK) {
            LOG_ERROR("Evict handle read cache pt " << rCache->GetPtId() << "failed, error code" << result);
        }
    }

    return BIO_NEED_WAIT;
}

void *RCacheEvict::Worker(void *context)
{
    RCacheEvictWorkerParam *para = static_cast<RCacheEvictWorkerParam*>(context);
    RCacheEvictPtr rCacheEvict = para->rCacheEvict;

    BResult result;
    while (rCacheEvict->GetWorkStatus()) {
        result = rCacheEvict->EvictHandle(para->index, para->tier);
        if (result != BIO_NEED_WAIT && result != BIO_OK) {
            LOG_ERROR("Gc handle read cache index " << para->index << "tier " << para->tier << "failed, error code" << result);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(READ_CACHE_EVICT_INTERVAL_MS));
    }

    return nullptr;
}

BResult RCacheEvict::Initialize()
{
    RCacheEvictWorkerParam *para = nullptr;

    workStatus.store(true);

    for (int32_t tier = 0; tier < READ_CACHE_TIER_BUTT; tier++) {
        for (uint32_t i = 0; i < READ_CACHE_EVICT_SERVICE_MASK; i++) {
            para = new (std::nothrow) RCacheEvictWorkerParam();
            if (para == nullptr) {
                LOG_ERROR("Alloc read cache para memory failed");
                return BIO_ALLOC_FAIL;
            }

            para->tier  = static_cast<RCacheTierType>(tier);;
            para->index = i;
            para->rCacheEvict = this;
            auto *th = new std::thread(Worker, static_cast<void*>(para));
            if (th) {
                pthread_setname_np(th->native_handle(), "evictWorker");
                works[i] = th;
            } else {
                LOG_ERROR("Create thread for read cache evict failed");
                return BIO_ALLOC_FAIL;
            }
        }
    }

    return BIO_OK;
}

BResult RCacheEvict::Destroy()
{
    return BIO_OK;
}

BResult RCacheEvict::Start(RCachePtr rCachePtr)
{
    uint32_t index = (workIndex++) % READ_CACHE_EVICT_SERVICE_MASK;

    evictRCacheLock->Lock();
    evictRCache[index].push_back(rCachePtr);
    evictRCacheLock->UnLock();

    return BIO_OK;
}

BResult RCacheEvict::Stop(RCachePtr rCachePtr)
{
    workStatus.store(false);

    for (int32_t tier = 0; tier < READ_CACHE_TIER_BUTT; tier++) {
        for (uint32_t i = 0; i < READ_CACHE_EVICT_SERVICE_MASK; i++) {
            works[i]->join();
        }
    }

    evictRCacheLock->Lock();
    for (uint32_t index = 0; index < READ_CACHE_EVICT_SERVICE_NUM; index++) {
        evictRCache[index].clear();
    }
    evictRCacheLock->UnLock();

    return BIO_OK;
}

