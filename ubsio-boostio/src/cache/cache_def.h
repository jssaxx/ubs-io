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

#ifndef BOOSTIO_CACHE_DEF_H
#define BOOSTIO_CACHE_DEF_H

#include <cstdint>
#include <functional>
#include <ctime>
#include "bio.h"
#include "slice.h"

namespace ock {
namespace bio {
using Key = char *;
using SliceReader = std::function<BResult(const SlicePtr &from, const SlicePtr &to)>;
using SliceWriter = std::function<BResult(const SlicePtr &from, const SlicePtr &to)>;
using GetGlobEvictOffset = std::function<BResult(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset)>;
using GetLocDiskId = std::function<BResult(uint16_t ptId, uint16_t &diskId)>;
using GetLocDiskStatus = std::function<void(uint16_t ptId, uint16_t diskId, bool &isNormal)>;
using CheckServiceState = std::function<bool()>;
using CheckDegrade = std::function<BResult(uint16_t ptId, bool &isDegrade)>;
using CheckLocRole = std::function<BResult(uint16_t ptId, bool &isMaster)>;

enum CacheType : uint16_t {
    WRITE_CACHE = 0,
    READ_CACHE = 1,
    CACHE_BUTT
};

enum RealIoStrategy : uint32_t {
    WRITE_DEFAULT = 0,
    WRITE_MEM_BACK = 1,
    WRITE_DISK_BACK = 2,
    WRITE_UNDERFS_BACK = 3,
};

struct CacheResDescription {
    uint64_t memCapacity;
    uint64_t diskCapacity;
    uint64_t memUsedSize;
    uint64_t diskUsedSize;
};

struct CacheAttr {
    RealIoStrategy ioStrategy;
    uint64_t mTenantId;
    AffinityStrategy affinity;
    WriteStrategy strategy;

    CacheAttr(uint64_t id, AffinityStrategy aff, WriteStrategy str)
        : ioStrategy(WRITE_DEFAULT), mTenantId(id), affinity(aff), strategy(str)
    {}
    CacheAttr(RealIoStrategy iostr, uint64_t id, AffinityStrategy aff, WriteStrategy str)
        : ioStrategy(iostr), mTenantId(id), affinity(aff), strategy(str)
    {}
    CacheAttr(CacheAttr &other)
        : ioStrategy(other.ioStrategy), mTenantId(other.mTenantId), affinity(other.affinity),
        strategy(other.strategy)
    {}
    inline CacheAttr &operator = (const CacheAttr &other)
    {
        ioStrategy = other.ioStrategy;
        mTenantId = other.mTenantId;
        affinity = other.affinity;
        strategy = other.strategy;
        return *this;
    }
};

struct CacheObjStat {
    uint64_t size; // value size
    time_t time;   // modify time;
};
}
}

#endif // BOOSTIO_CACHE_DEF_H
