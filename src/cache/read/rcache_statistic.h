/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_RCACHE_STATISTIC_H
#define BOOSTIO_RCACHE_STATISTIC_H

#include <cstdint>
#include <atomic>
#include "rcache_chunk.h"

namespace ock {
namespace bio {
class RCacheStatistic {
public:
    static RCacheStatistic &Instance()
    {
        static RCacheStatistic instance;
        return instance;
    }

    inline void IncHitCount()
    {
        hitCount.fetch_add(1ULL);
    }

    inline void IncHitMemCount()
    {
        hitMemCount.fetch_add(1ULL);
    }

    inline void IncHitDiskCount()
    {
        hitDiskCount.fetch_add(1ULL);
    }

    inline void IncTotalCount()
    {
        totalCount.fetch_add(1ULL);
    }

    uint64_t GetTotalCount()
    {
        return totalCount.load();
    }

    uint64_t GetHitMemCount()
    {
        return hitMemCount.load();
    }

    uint64_t GetHitDiskCount()
    {
        return hitDiskCount.load();
    }

    uint64_t GetHitCount()
    {
        return hitCount.load();
    }

    inline void StatisticalByType(RCacheTierType type)
    {
        switch (type) {
            case READ_CACHE_TIER_MEM:
                IncHitMemCount();
                break;
            case READ_CACHE_TIER_DISK:
                IncHitDiskCount();
                break;
            default:
                return;
        }
    }

private:
    RCacheStatistic() : totalCount(0), hitCount(0), hitMemCount(0), hitDiskCount(0) {}
private:
    std::atomic<uint64_t> totalCount;
    std::atomic<uint64_t> hitCount;
    std::atomic<uint64_t> hitMemCount;
    std::atomic<uint64_t> hitDiskCount;
};
}
}
#endif // BOOSTIO_RCACHE_STATISTIC_H
