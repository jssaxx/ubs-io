/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_WCACHE_H
#define BOOSTIO_WCACHE_H

#include <set>
#include <unordered_map>
#include "bio_execution.h"
#include "bio_ref.h"
#include "cache_def.h"
#include "cache_slice.h"
#include "cache_slice_operator.h"
#include "flow.h"
#include "underfs.h"
#include "wcache_tier.h"
#include "rcache_manager.h"

namespace ock {
namespace bio {
class WCache {
public:
    using EvictCallback = std::function<BResult(uint64_t ptId, const Key &key)>;
    BResult Init(uint64_t flowId, uint64_t ptv, uint16_t diskId, const ExecutorServicePtr &exeService,
        EvictCallback evictCallback);

    void Exit();

    BResult GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult Put(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
        WCacheSliceRefPtr &destSliceRef, CacheAttr &attr);

    BResult EvictAllMemSliceToDisk();

    BResult EvictAllDiskSliceToUnderFs(const RCacheManagerPtr &rCacheManager);

    uint64_t GetCapacity();

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    BResult EvictFromMemToDisk(WCacheSliceRefPtr slice);
    BResult EvictFromDiskToUnderFs(const RCacheManagerPtr &rCacheManager, const WCacheSliceRefPtr &sliceRef);

private:
    uint64_t mFlowId;
    uint64_t mPtv;
    EvictCallback mEvictCallback;

    WCacheTierPtr mCacheTiers[MAX_WCACHE_TIER];

    CacheSliceOperator mSliceOperator;

    ExecutorServicePtr mExeService;

    UnderFsPtr mUnderFs;

    std::atomic<uint64_t> mFlyCnt { 0 };

    DEFINE_REF_COUNT_VARIABLE;
};
using WCachePtr = Ref<WCache>;
}
}


#endif // BOOSTIO_WCACHE_H
