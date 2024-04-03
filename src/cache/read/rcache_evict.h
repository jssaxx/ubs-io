/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#ifndef BOOSTIO_RCACHE_EVICT_H
#define BOOSTIO_RCACHE_EVICT_H

#include <thread>
#include "bio_err.h"
#include "bio_ref.h"
#include "rcache.h"

namespace ock {
    namespace bio {
        constexpr uint32_t READ_CACHE_EVICT_SERVICE_NUM = 2;
        constexpr uint32_t READ_CACHE_EVICT_SERVICE_MASK = READ_CACHE_EVICT_SERVICE_NUM - 1;
        constexpr uint32_t READ_CACHE_EVICT_INTERVAL_MS = 6 * 1000;

        class RCacheEvict {
        public:
            RCacheEvict();

            ~RCacheEvict();

            BResult Initialize();

            BResult Destroy();

            inline bool GetWorkStatus() noexcept {return workStatus.load();}

            inline void IncCacheData(uint64_t len)
            {
                cacheData += len;
            }

            inline void DecCacheData(uint64_t len)
            {
                if (cacheData < len) {
                    return;
                }

                cacheData -= len;
            }

            BResult Start(RCachePtr rCachePtr);

            BResult Stop(RCachePtr rCachePtr);

            DEFINE_REF_COUNT_FUNCTIONS
        private:
            uint64_t GetEvictDataByTier(const RCachePtr rCache, RCacheTierType tier);

            BResult EvictOneRCacheHandle(RCachePtr rCache, RCacheTierType tier);

            BResult EvictHandle(uint32_t index, RCacheTierType tier);

            static void *Worker(void *context);

        private:
            std::atomic<uint64_t> cacheData;

            std::atomic<bool> workStatus;
            std::atomic<uint32_t> workIndex;
            SpinLock evictRCacheLock[READ_CACHE_EVICT_SERVICE_NUM];
            std::list<RCachePtr> evictRCache[READ_CACHE_EVICT_SERVICE_NUM];
            std::thread *works[READ_CACHE_TIER_BUTT][READ_CACHE_EVICT_SERVICE_NUM];

            DEFINE_REF_COUNT_VARIABLE
        };

        using RCacheEvictPtr = Ref<RCacheEvict>;

        struct RCacheEvictWorkerParam {
            RCacheTierType tier;
            uint32_t index;
            RCacheEvictPtr rCacheEvict;
        };
    }
}

#endif // BOOSTIO_RCACHE_EVICT_H
