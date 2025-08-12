/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description:
 * Create: 2025-02-06
 */

#ifndef BOOSTIO_DISK_STATISTIC_H
#define BOOSTIO_DISK_STATISTIC_H
#include <cstdint>
#include <atomic>

namespace ock {
namespace bio {
    class DiskStatistic {
    public:
        static DiskStatistic &Instance()
        {
            static DiskStatistic instance;
            return instance;
        }

        inline void IncHitCount()
        {
            hitCount.fetch_add(1ULL);
        }

        uint64_t GetHitCount()
        {
            return hitCount.load();
        }

    private:
        DiskStatistic() : hitCount(0) {}
    private:
        std::atomic<uint64_t> hitCount;
    };
}
}
#endif // BOOSTIO_DISK_STATISTIC_H
