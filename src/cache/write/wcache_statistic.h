/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description:
 * Create: 2024-08-17
 */

#ifndef BOOSTIO_WCACHE_STATISTIC_H
#define BOOSTIO_WCACHE_STATISTIC_H

#include <cstdint>
#include <atomic>

namespace ock {
namespace bio {
class WCacheStatistic {
public:
    static WCacheStatistic &Instance()
    {
        static WCacheStatistic instance;
        return instance;
    }

    inline void IncHitCount()
    {
        hitCount.fetch_add(1ULL);
    }

    inline void IncTotalCount()
    {
        totalCount.fetch_add(1ULL);
    }

    uint64_t GetTotalCount()
    {
        return totalCount.load();
    }

    uint64_t GetHitCount()
    {
        return hitCount.load();
    }

private:
    WCacheStatistic() : totalCount(0), hitCount(0) {}
private:
    std::atomic<uint64_t> totalCount;
    std::atomic<uint64_t> hitCount;
};
}
}

#endif // BOOSTIO_WCACHE_STATISTIC_H
