/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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
using GetGlobEvictOffset = std::function<BResult(uint16_t ptId, uint64_t flowId, bool &isMaster, uint64_t &flowOffset)>;
using GetLocDiskId = std::function<BResult(uint16_t ptId, uint16_t &diskId)>;
using GetLocDiskStatus = std::function<void(uint16_t ptId, uint16_t diskId, bool &isNormal)>;
using CheckDegrade = std::function<BResult(uint16_t ptId, bool &isDegrade)>;
using CheckLocRole = std::function<BResult(uint16_t ptId, bool &isMaster)>;

struct CacheAttr {
    uint64_t mTenantId;
    AffinityStrategy affinity;
    WriteStrategy strategy;

    CacheAttr(uint64_t id, AffinityStrategy aff, WriteStrategy str) : mTenantId(id), affinity(aff), strategy(str) {}

    inline CacheAttr &operator = (const CacheAttr &other)
    {
        mTenantId = other.mTenantId;
        affinity = other.affinity;
        strategy = other.strategy;
        return *this;
    }
};

struct CacheObjStat {
    uint64_t size; // value size
    time_t   time; // modify time;
};
}
}

#endif // BOOSTIO_CACHE_DEF_H
