/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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
#include "rcache_gc.h"

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

            BResult AllocResources(uint64_t ptId, uint64_t len, WCacheSlicePtr &slice); // alloc resources for write cache evict data

            BResult Put(uint64_t ptId, const Key &key, const WCacheSlicePtr &slice);

            BResult Get(uint64_t ptId, const Key &key, uint64_t offset, const RCacheSlicePtr &slice,
                const SliceWriter &sliceWriter, uint64_t &realLen);

            BResult Load(uint64_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen);

            BResult Delete(uint64_t ptId, const Key &key);

            BResult CreateRCache(uint64_t ptId, uint16_t diskId);

            BResult DeleteRCache(uint64_t ptId);

            uint64_t GetGCData();

            const RCachePtr GetRCacheInstanceByPtId(uint64_t ptId);
            DEFINE_REF_COUNT_FUNCTIONS
        private:
            ReadWriteLock cacheLock;
            std::unordered_map<uint64_t, RCachePtr> cache; // read cache object

            RCacheEvictPtr rCacheEvict; // read cache evict service

            RCacheGCPtr rCacheGCPtr; // read cache gc service
            DEFINE_REF_COUNT_VARIABLE
        };
    }
}

#endif // BOOSTIO_RCACHE_MANAGER_H
