/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#ifndef BOOSTIO_RCACHE_GC_H
#define BOOSTIO_RCACHE_GC_H

#include <cstdint>
#include <thread>
#include "bio_ref.h"
#include "rcache.h"

namespace ock {
    namespace bio {
    constexpr uint32_t READ_CACHE_GC_SERVICE_NUM = 2;
    constexpr uint32_t READ_CACHE_GC_SERVICE_MASK = READ_CACHE_GC_SERVICE_NUM - 1;
    constexpr uint32_t READ_CACHE_GC_INTERVAL_MS = 6 * 1000;
    constexpr uint32_t READ_CACHE_GC_WATER_LEVEL = 90;

    class RCacheGC {
    public:
        RCacheGC();

        ~RCacheGC();

        inline bool GetWorkStatus() noexcept {return workStatus.load();}

        inline void IncGarbageData(uint64_t len)
        {
            garbageData += len;
        }

        inline void DecGarbageData(uint64_t len)
        {
            if (garbageData < len) {
                return;
            }

            garbageData -= len;
        }

        BResult Initialize();

        BResult Destroy();

        BResult Start(RCachePtr rCachePtr);

        BResult Stop(RCachePtr rCachePtr);

        DEFINE_REF_COUNT_FUNCTIONS
    private:
        BResult GCOneRCacheHandle(RCachePtr rCache, RCacheTierType tier);

        BResult GcHandle(uint32_t index, RCacheTierType tier);

        static void *Worker(void *context);

        uint64_t GetGcWaterLevelByTier(RCacheTierType tier);
    private:
        std::atomic<uint64_t> garbageData;
        std::atomic<bool> workStatus;
        std::atomic<uint32_t> workIndex;
        SpinLock GCRCacheLock[READ_CACHE_GC_SERVICE_NUM];
        std::list<RCachePtr> GCRCache[READ_CACHE_GC_SERVICE_NUM];
        std::thread *works[READ_CACHE_GC_SERVICE_NUM];
        DEFINE_REF_COUNT_VARIABLE
    };

    using RCacheGCPtr = Ref<RCacheGC>;

    struct RCacheGCWorkerParam {
        RCacheTierType tier;
        uint32_t index;
        RCacheGCPtr rCacheEvict;
    };
    }
}

#endif // BOOSTIO_RCACHE_GC_H
