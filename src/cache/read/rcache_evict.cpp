/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "bio_log.h"
#include "bio_config_instance.h"
#include "rcache_evict.h"

using namespace ock::bio;

RCacheEvict::RCacheEvict():workIndex(0)
{
}

RCacheEvict::~RCacheEvict()
{
}

uint64_t GetReadRatio(std::string readWriteRatios)
{
    std::vector<std::string> ratios;
    StrUtil::Split(readWriteRatios, ":", ratios);
    long readRatio = 0;
    StrUtil::StrToLong(ratios[0], readRatio);
    return readRatio;
}

uint64_t RCacheEvict::GetEvictDataByTier(const RCachePtr rCache, RCacheTierType tier)
{
    uint64_t waterData;
    uint64_t cacheData = rCache->GetCacheData(tier);
    auto config = BioConfig::Instance()->GetDaemonConfig();
    if (tier == READ_CACHE_TIER_MEM) {
        waterData = (GetReadRatio(config.memReadWriteRatio) * config.memCap * config.evictWaterLevel) / NO_1000;
    } else {
        waterData = (GetReadRatio(config.diskReadWriteRatio) * config.diskCaps.at(rCache->GetDiskId()) *
            config.evictWaterLevel) / NO_1000;
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

    evictRCacheLock[index].Lock();
    std::list<RCachePtr> list = evictRCache[index];
    evictRCacheLock[index].UnLock();

    for (auto iter = list.begin(); iter != list.end();iter++) {
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
    workStatus.store(false);
    return BIO_OK;
}

BResult RCacheEvict::Start(RCachePtr rCachePtr)
{
    uint32_t index = rCachePtr->GetWorkIndex() % READ_CACHE_EVICT_SERVICE_MASK;

    evictRCacheLock[index].Lock();
    evictRCache[index].push_back(rCachePtr);
    evictRCacheLock[index].UnLock();

    return BIO_OK;
}

BResult RCacheEvict::Stop(RCachePtr rCachePtr)
{
    uint32_t index = rCachePtr->GetWorkIndex() % READ_CACHE_EVICT_SERVICE_MASK;

    evictRCacheLock[index].Lock();
    auto iter = std::find(evictRCache[index].begin(), evictRCache[index].end(), rCachePtr);
    if (iter != evictRCache[index].end()) {
        evictRCache[index].erase(iter);
        evictRCacheLock[index].UnLock();
        return BIO_OK;
    }

    evictRCacheLock[index].UnLock();
    return BIO_NOT_EXISTS;
}

