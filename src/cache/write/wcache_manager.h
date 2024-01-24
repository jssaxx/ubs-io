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
class WCacheManager {
public:
    WCacheManager() = default;

    ~WCacheManager() = default;

    BResult Init(const RCacheManagerPtr &rCacheManager);

    void Exit();

public:
    BResult AllocateFlowId(uint16_t ptId, uint64_t &flowId);

    BResult CreateWCache(uint64_t flowId);

    BResult DeleteWCache(uint64_t ptId);

    BResult GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader);

    BResult Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter);

    BResult Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat);

    BResult Delete(uint64_t ptId, const Key &key);

    BResult Flush(uint64_t ptId);

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    WCachePtr GetWCache(uint64_t flowId);
    BResult Read(uint64_t offset, const WCacheSlicePtr &srcSlice, const RCacheSlicePtr &destSlice, const SliceWriter &sliceWriter);
    void RunEvictThread();

private:
    ReadWriteLock mWCacheManagerLock;
    std::unordered_map<uint64_t, WCachePtr> mWCacheManager;

    RCacheManagerPtr mRCacheManager;

    bool mRunning = true;
    ExecutorServicePtr mExeService{ nullptr };

    WCacheIndexPtr mCacheIndex;

    // TODO: support memory threshold. Right now memory slice was evicted to disk asap.
    uint64_t mMemCapacityThreshold{ 10 * 1024 };         // 10GB, uint is MB
    uint64_t mDiskCapacityThreshold{ 4 * 1024 * 1024L }; // 4TB, unit is MB

    DEFINE_REF_COUNT_VARIABLE;
};
using WCacheManagerPtr = Ref<WCacheManager>;
}
}


#endif // BOOSTIO_WCACHE_MANAGER_H
