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

#ifndef TURBO_IO_BIO_CACHE_STATISTICS_H
#define TURBO_IO_BIO_CACHE_STATISTICS_H

#include <atomic>
#include "message.h"

namespace ock {
namespace bio {

typedef struct {
    std::atomic<uint64_t> rCacheHitMemCount;
    std::atomic<uint64_t> rCacheHitDiskCount;
    std::atomic<uint64_t> rCacheHitCount;
    std::atomic<uint64_t> rCacheTotalCount;
    std::atomic<uint64_t> wCacheHitMemCount;
    std::atomic<uint64_t> wCacheHitDiskCount;
    std::atomic<uint64_t> wCacheHitCount;
    std::atomic<uint64_t> wCacheTotalCount;
    std::atomic<uint64_t> backendHitCount;
} CacheHitDesc;

}
}

#endif
