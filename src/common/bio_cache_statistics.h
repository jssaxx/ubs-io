/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description:
 * Create: 2024-08-19
 */
#include <atomic>

#ifndef TURBO_IO_BIO_CACHE_STATISTICS_H
#define TURBO_IO_BIO_CACHE_STATISTICS_H

namespace ock {
namespace bio {

typedef struct {
    std::atomic<uint64_t> rCacheHitCount;
    std::atomic<uint64_t> rCacheTotalCount;
    std::atomic<uint64_t> wCacheHitCount;
    std::atomic<uint64_t> wCacheTotalCount;
} CacheHitDesc;

}
}

#endif
