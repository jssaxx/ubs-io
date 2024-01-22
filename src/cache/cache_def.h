/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_CACHE_DEF_H
#define BOOSTIO_CACHE_DEF_H

#include <cstdint>
#include <functional>
#include <ctime>
#include "slice.h"

namespace ock {
namespace bio {
using Key = char *;
using SliceReader = std::function<BResult(const SlicePtr &from, const SlicePtr &to)>;
using SliceWriter = std::function<BResult(const SlicePtr &from, const SlicePtr &to)>;
using FlowEvictSync = std::function<BResult(uint64_t flowId, uint64_t flowOffset)>;

struct CacheObjStat {
    uint32_t size; // value size
    time_t   time; // modify time;
};
}
}

#endif // BOOSTIO_CACHE_DEF_H
