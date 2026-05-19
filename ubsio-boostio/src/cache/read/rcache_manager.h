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

#ifndef BOOSTIO_RCACHE_MANAGER_H
#define BOOSTIO_RCACHE_MANAGER_H

#include <unordered_map>
#include <cstdint>
#include "bio_log.h"
#include "bio_err.h"
#include "bio_ref.h"
#include "rcache.h"
#include "cache_def.h"
#include "rcache_evict.h"

namespace ock {
namespace bio {
class RCacheManager;
using RCacheManagerPtr = Ref<RCacheManager>;
class RCacheManager {
public:
    RCacheManager();

    ~RCacheManager();

    inline static RCacheManagerPtr &Instance()
    {
        static auto instance = MakeRef<RCacheManager>();
        return instance;
    }

    BResult Init();

    void Exit();

    // alloc resources for write cache evict data
    BResult AllocResources(uint16_t ptId, uint64_t len, WCacheSlicePtr &slice);

    BResult CheckEnoughResource(uint16_t ptId, bool &havaResource);

    BResult Put(uint16_t ptId, const Key &key, const WCacheSlicePtr &slice);

    BResult Get(uint16_t ptId, const Key &key, uint64_t offset, const RCacheSlicePtr &slice,
        const SliceWriter &sliceWriter, uint64_t &realLen);

    BResult Load(uint16_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen);

    BResult Delete(uint16_t ptId, const Key &key);

    BResult CreateRCache(uint16_t ptId, uint64_t ptv, uint16_t diskId);

    BResult DeleteRCache(uint16_t ptId);

    BResult RecoverCache(FlowPtr dataFlow);

    BResult ExpiredClear(uint16_t ptId, uint64_t ptv);

    BResult ExpiredClearImpl(RCachePtr rCache);

    uint64_t GetGCData();

    const RCachePtr GetRCacheInstanceByPtId(uint16_t ptId);

    const RCachePtr FindRCacheInstanceByPtId(uint16_t ptId);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    ReadWriteLock cacheLock;
    std::unordered_map<uint64_t, RCachePtr> cache; // read cache object

    std::atomic<uint32_t> mWorkIndex{ 0 };

    RCacheEvictPtr rCacheEvict; // read cache evict service

    DEFINE_REF_COUNT_VARIABLE;
};
}
}

#endif // BOOSTIO_RCACHE_MANAGER_H
