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

    inline void IncExistHitCount()
    {
        existHitCount.fetch_add(1ULL);
    }

    inline void IncExistTotalCount()
    {
        existCount.fetch_add(1ULL);
    }

    uint64_t GetExistTotalCount()
    {
        return existCount.load();
    }

    uint64_t GetExistHitCount()
    {
        return existHitCount.load();
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

    uint64_t GetHitCount()
    {
        return hitCount.load();
    }

    uint64_t GetHitMemCount()
    {
        return hitMemCount.load();
    }

    uint64_t GetHitDiskCount()
    {
        return hitDiskCount.load();
    }

    inline void StatisticalByType(FlowType type)
    {
        switch (type) {
            case FLOW_MEMORY:
                IncHitMemCount();
                break;
            case FLOW_DISK:
                IncHitDiskCount();
                break;
            default:
                return;
        }
    }

private:
    WCacheStatistic() : totalCount(0), hitCount(0), hitMemCount(0), hitDiskCount(0) {}
private:
    std::atomic<uint64_t> totalCount;
    std::atomic<uint64_t> hitCount;
    std::atomic<uint64_t> hitMemCount;
    std::atomic<uint64_t> hitDiskCount;
    std::atomic<uint64_t> existCount;
    std::atomic<uint64_t> existHitCount;
};
}
}

#endif // BOOSTIO_WCACHE_STATISTIC_H
