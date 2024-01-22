/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_RCACHE_STATISTIC_H
#define BOOSTIO_RCACHE_STATISTIC_H

#include <cstdint>
#include <atomic>

namespace ock {
    namespace bio {
    class RCacheStatistic {
    public:
        static RCacheStatistic &Instance()
        {
            static RCacheStatistic instance;
            return instance;
        }
        void IncHisCount()
        {
            hisCount++;
        }

        void IncMisCount()
        {
            misCount++;
        }

    private:
        std::atomic<uint64_t> misCount;
        std::atomic<uint64_t> hisCount;
    };
    }
}

#endif // BOOSTIO_RCACHE_STATISTIC_H
