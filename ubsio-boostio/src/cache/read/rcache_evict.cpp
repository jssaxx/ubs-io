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

#include "bio_log.h"
#include "bio_tracepoint_helper.h"
#include "bio_config_instance.h"
#include "rcache_evict.h"

using namespace ock::bio;

RCacheEvict::RCacheEvict() : workStatus(false), workIndex(0)
{
    for (uint32_t i = 0; i < READ_CACHE_TIER_BUTT; i++) {
        for (uint32_t j = 0; j < READ_CACHE_EVICT_SERVICE_NUM; j++) {
            works[i][j] = nullptr;
        }
    }
}

RCacheEvict::~RCacheEvict() {}

uint64_t RCacheEvict::GetEvictDataByTier(const RCachePtr rCache, RCacheTierType tier)
{
    uint64_t memCapacity;
    uint64_t memUsedSize;
    uint64_t diskCapacity;
    uint64_t diskUsedSize;
    RCache::GetCacheResource(memCapacity, memUsedSize, diskCapacity, diskUsedSize);

    auto config = BioConfig::Instance()->GetDaemonConfig();

    uint64_t cacheData;
    uint64_t waterData;
    if (tier == READ_CACHE_TIER_MEM) {
        cacheData = memUsedSize;
        waterData = memCapacity * config.rcacheMemEvictLevel / NO_100;
    } else {
        cacheData = diskUsedSize;
        waterData = diskCapacity * config.rcacheDiskEvictLevel / NO_100;
    }

    return cacheData > waterData ? cacheData - waterData : 0ULL;
}

BResult RCacheEvict::EvictOneRCacheHandle(RCachePtr rCache, RCacheTierType tier)
{
    uint64_t evictTotalData = GetEvictDataByTier(rCache, tier);
    if (evictTotalData == 0ULL) {
        return BIO_NEED_WAIT;
    }

    BResult ret;
    uint64_t evictData = 0ULL;
    if (tier == READ_CACHE_TIER_MEM) {
        ret = rCache->EvictMemData(evictTotalData, evictData);
    } else {
        ret = rCache->EvictDiskData(evictTotalData, evictData);
    }

    return ret;
}

BResult RCacheEvict::EvictHandle(uint32_t index, RCacheTierType tier)
{
    evictRCacheLock[index].Lock();
    std::list<RCachePtr> list = evictRCache[index];
    evictRCacheLock[index].UnLock();

    for (auto iter = list.begin(); iter != list.end(); iter++) {
        RCachePtr rCache = *iter;
        BResult result = EvictOneRCacheHandle(rCache, tier);
        if (result != BIO_NEED_WAIT && result != BIO_OK) {
            LOG_ERROR("Evict handle read cache pt " << rCache->GetPtId() << " failed, error code " << result);
        }
    }

    return BIO_NEED_WAIT;
}

void *RCacheEvict::Worker(void *context)
{
    RCacheEvictWorkerParam *para = static_cast<RCacheEvictWorkerParam *>(context);
    RCacheEvictPtr rCacheEvict = para->rCacheEvict;

    BResult result;
    while (rCacheEvict->GetWorkStatus()) {
        result = rCacheEvict->EvictHandle(para->index, para->tier);
        if (result != BIO_NEED_WAIT && result != BIO_OK) {
            LOG_ERROR("Gc handle read cache index " << para->index << " tier " << para->tier <<
                " failed, error code " << result);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(READ_CACHE_EVICT_INTERVAL_MS));
    }

    delete para;
    return nullptr;
}

BResult RCacheEvict::Initialize()
{
    RCacheEvictWorkerParam *para = nullptr;
    auto hasDiskCache = BioConfig::Instance()->GetDaemonConfig().hasDiskCache;

    workStatus.store(true);
    int32_t tierCount = hasDiskCache ? READ_CACHE_TIER_BUTT : READ_CACHE_TIER_MEM + 1;
    for (int32_t tier = 0; tier < tierCount; tier++) {
        for (uint32_t i = 0; i < READ_CACHE_EVICT_SERVICE_NUM; i++) {
            BIO_TP_START(RCACHE_EVICT_PARAM_FAIL, 0);
            para = new (std::nothrow) RCacheEvictWorkerParam();
            BIO_TP_END;
            if (para == nullptr) {
                LOG_ERROR("Alloc read cache para memory failed");
                workStatus.store(false);
                RecycleThreadResources();
                return BIO_ALLOC_FAIL;
            }

            para->tier = static_cast<RCacheTierType>(tier);
            para->index = i;
            para->rCacheEvict = this;
            std::thread *th = nullptr;
            BIO_TP_START(RCACHE_EVICT_THREAD_FAIL, &th, nullptr);
            th = new (std::nothrow) std::thread(Worker, static_cast<void *>(para));
            BIO_TP_END;
            if (th != nullptr) {
                pthread_setname_np(th->native_handle(), "evictWorker");
                works[tier][i] = th;
            } else {
                LOG_ERROR("Create thread for read cache evict failed");
                delete para;
                workStatus.store(false);
                RecycleThreadResources();
                return BIO_ALLOC_FAIL;
            }
        }
    }

    return BIO_OK;
}

void RCacheEvict::Destroy()
{
    workStatus.store(false);
    for (auto &work : works) {
        for (auto &th : work) {
            if (th) {
                th->join();
                delete th;
                th = nullptr;
            }
        }
    }
}

BResult RCacheEvict::Start(RCachePtr rCachePtr)
{
    uint32_t index = rCachePtr->GetWorkIndex() % READ_CACHE_EVICT_SERVICE_NUM;
    evictRCacheLock[index].Lock();
    evictRCache[index].push_back(rCachePtr);
    evictRCacheLock[index].UnLock();
    return BIO_OK;
}

BResult RCacheEvict::Stop(RCachePtr rCachePtr)
{
    uint32_t index = rCachePtr->GetWorkIndex() % READ_CACHE_EVICT_SERVICE_NUM;
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
