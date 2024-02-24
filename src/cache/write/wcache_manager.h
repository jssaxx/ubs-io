/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_WCACHE_MANAGER_H
#define BOOSTIO_WCACHE_MANAGER_H

#include <atomic>
#include <unordered_map>
#include "bio_err.h"
#include "bio_ref.h"
#include "cache_def.h"
#include "slice.h"
#include "wcache.h"
#include "wcache_index.h"
#include "rcache_manager.h"

namespace ock {
namespace bio {
    constexpr uint32_t WRITE_CACHE_FLOW_MEM_META_PREFIX = 0;
class WCacheManager;
using WCacheManagerPtr = Ref<WCacheManager>;
class WCacheManager {
public:
    WCacheManager() = default;

    ~WCacheManager() = default;

    inline static WCacheManagerPtr &Instance()
    {
        static auto instance = MakeRef<WCacheManager>();
        return instance;
    }

    BResult Init(const RCacheManagerPtr &rCacheManager);

    void Exit();

public:
    BResult AllocateFlowId(uint16_t ptId, uint64_t &flowId);

    BResult CreateWCache(uint64_t flowId, uint64_t ptId, uint64_t ptv, uint16_t diskId);

    BResult DeleteWCache(uint64_t ptId);

    BResult GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr);

    BResult Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
                uint64_t &realLen);

    BResult Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat);

    BResult Delete(uint64_t ptId, const Key &key);

    BResult Flush(uint64_t ptId, uint64_t ptv);

    BResult ExpiredClear(uint64_t ptId, uint64_t ptv);

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    WCachePtr GetWCache(uint64_t flowId);
    BResult Read(uint64_t offset, const WCacheSlicePtr &srcSlice, const RCacheSlicePtr &destSlice, const SliceWriter &sliceWriter,
                 uint64_t &realLen);
    BResult FlushImpl(uint64_t ptId, uint64_t ptv);

private:
    ReadWriteLock mWCacheManagerLock;
    std::unordered_map<uint64_t, WCachePtr> mWCacheManager;

    RCacheManagerPtr mRCacheManager;

    bool mRunning = true;
    ExecutorServicePtr mMemEvictService{ nullptr };
    ExecutorServicePtr mDiskEvictService{ nullptr };

    WCacheIndexPtr mCacheIndex;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}


#endif // BOOSTIO_WCACHE_MANAGER_H
