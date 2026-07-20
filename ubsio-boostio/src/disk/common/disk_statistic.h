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

#ifndef BOOSTIO_DISK_STATISTIC_H
#define BOOSTIO_DISK_STATISTIC_H
#include <atomic>
#include <cstdint>

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
} // namespace bio
} // namespace ock
#endif // BOOSTIO_DISK_STATISTIC_H
