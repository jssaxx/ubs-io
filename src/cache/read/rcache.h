/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_RCACHE_H
#define BOOSTIO_RCACHE_H

#include <unordered_map>
#include <list>
#include <cstdint>
#include "bio_log.h"
#include "bio_err.h"
#include "bio_lock.h"
#include "bio_ref.h"
#include "flow_id_allocator.h"
#include "flow.h"
#include "rcache_chunk.h"
#include "rcache_flow.h"
#include "cache_slice_operator.h"
#include "cache_slice.h"
#include "cache_def.h"

namespace ock {
    namespace bio {
        constexpr uint32_t READ_CACHE_META_HASH_BUCKET_NUM  = 10000;
        constexpr uint32_t READ_CACHE_META_HASH_BUCKET_MASK = READ_CACHE_META_HASH_BUCKET_NUM - 1;
        constexpr uint32_t READ_CACHE_FLOW_MEM_META_PREFIX  = 0;
        constexpr uint32_t READ_CACHE_FLOW_MEM_DATA_PREFIX  = 1;
        constexpr uint32_t READ_CACHE_FLOW_DISK_META_PREFIX = 2;
        constexpr uint32_t READ_CACHE_FLOW_DISK_DATA_PREFIX = 3;
        constexpr uint64_t READ_CACHE_MEM_RESOURCE_QUANTITY  = (10ULL * 1024ULL * 1024ULL * 1024ULL);
        constexpr uint64_t READ_CACHE_DISK_RESOURCE_QUANTITY = (10ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL);

        class RCache {
        public:
            RCache(uint64_t ptId);

            ~RCache();

            BResult Initialize();

            BResult Destroy();

            BResult AllocResources(uint64_t length, WCacheSlicePtr &slice);

            BResult Put(const Key &key, const WCacheSlicePtr &slice);

            BResult Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
                uint64_t &realLen);

            BResult Load(const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen);

            BResult Delete(const Key &key);

            inline uint64_t GetPtId()
            {
                return mPtId;
            }

            inline uint64_t GetCacheData(RCacheTierType tierType)
            {
                return cacheData[tierType];
            }

            inline void IncCacheData(RCacheTierType tierType, uint64_t len)
            {
                cacheData[tierType] += len;
            }

            inline void DecCacheData(RCacheTierType tierType, uint64_t len)
            {
                if (cacheData[tierType] > len) {
                    return;
                }

                cacheData[tierType] -= len;
            }

            inline uint64_t GetGCData(RCacheTierType tierType)
            {
                return gcData[tierType];
            }

            inline void IncGCData(RCacheTierType tierType, uint64_t len)
            {
                gcData[tierType] += len;
            }

            inline void DecGCData(RCacheTierType tierType, uint64_t len)
            {
                if (gcData[tierType] > len) {
                    return;
                }

                gcData[tierType] -= len;
            }

            BResult EvictMemData(const uint64_t needEvictData, uint64_t &haveEvictData);

            BResult EvictDiskData(const uint64_t needEvictData, uint64_t &haveEvictData);

            DEFINE_REF_COUNT_FUNCTIONS
        private:
            BResult InsertToIndex(const Key &key, RCacheChunkPtr &chunk);

            BResult DeleteFromIndex(const Key &key);

            void AddToEvictList(RCacheTierType tierType, MqType mType, RCacheChunkPtr &chunk);

            void DelFromEvictList(RCacheTierType tierType, MqType mType, RCacheChunkPtr &chunk);

            void AddToTruncateList(RCacheTierType tierType, RCacheChunkPtr &chunk);

            void DelFromTruncateList(RCacheTierType tierType, RCacheChunkPtr &chunk);

            uint32_t GetHashBucketByKey(const Key &key);

            FlowType GetFlowTypeByTierType(RCacheTierType tierType);

            BResult AllocChunk(const Key key, const RCacheValue value, RCacheChunkPtr &chunk);
            
            BResult GetSliceFromChunkIO(RCacheTierType tier, const RCacheChunkPtr &chunk, WCacheSlicePtr &slicePtr,
                uint64_t offset, uint64_t len, uint64_t &realLen);

            BResult GetSliceFromChunk(RCacheTierType tier, const RCacheChunkPtr &chunk, WCacheSlicePtr &slicePtr);

            BResult CreateRCacheFlow(RCacheTierType tier, std::vector<uint64_t> flowIds);
        private:
            std::atomic<uint64_t> cacheData[READ_CACHE_TIER_BUTT];
            std::atomic<uint64_t> gcData[READ_CACHE_TIER_BUTT];

            uint64_t mPtId;
            SpinLock indexLock[READ_CACHE_META_HASH_BUCKET_NUM];
            std::unordered_map<std::string, RCacheChunkPtr> index[READ_CACHE_META_HASH_BUCKET_NUM];  // read cache index

            RCacheFlowPtr flow[READ_CACHE_TIER_BUTT]; // read cache data

            SpinLock evictMqLock[READ_CACHE_TIER_BUTT][MQ_TYPE_BUTT];
            std::list<RCacheChunkPtr> evictMq[READ_CACHE_TIER_BUTT][MQ_TYPE_BUTT];  // read cache evict list

            SpinLock truncateLock[READ_CACHE_TIER_BUTT];
            std::list<RCacheChunkPtr> truncateQ[READ_CACHE_TIER_BUTT]; // truncate cache list

            CacheSliceOperator mSliceOperator;
            DEFINE_REF_COUNT_VARIABLE
        };

        using RCachePtr = Ref<RCache>;
    }
}

#endif // BOOSTIO_RCACHE_H
