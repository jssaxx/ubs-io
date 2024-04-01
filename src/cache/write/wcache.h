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
class WCache;
using WCachePtr = Ref<WCache>;
class WCache {
public:
    WCache(uint64_t procId, uint64_t flowId, uint64_t ptId, uint64_t ptv, uint16_t diskId)
        : mProcId(procId), mFlowId(flowId), mPtId(ptId), mPtv(ptv), mDiskId(diskId)
    {}

    using EvictCallback = std::function<BResult(uint64_t ptId, const Key &key, WCacheSliceRefPtr sliceRef)>;
    using RetryCallback = std::function<void(uint64_t flowId, WCacheTierType cacheTier)>;
    BResult Init(const ExecutorServicePtr evictService[MAX_WCACHE_TIER],
        const RCacheManagerPtr rCacheManager);

    void RegOp(GetLocDiskStatus getLocDiskStatus, CheckLocRole locRole, const GetGlobEvictOffset evictOffset,
        EvictCallback evictCallback, const RetryCallback retryCallback);

    void Exit();

    BResult GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult Put(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
        WCacheSliceRefPtr &destSliceRef, CacheAttr &attr, bool isDegrade);

    BResult Delete(const Key &key, const WCacheSliceRefPtr &sliceRef);

    BResult Destroy();

    void StartEvictTask(WCacheTierType type);

    void RetryEvictTask(WCacheTierType type);

    uint64_t GetCapacity(WCacheTierType type);

    uint64_t GetVirCapacity(WCacheTierType type);

    uint64_t GetEvictOffset();

    uint64_t GetProcId()
    {
        return mProcId;
    }

    uint64_t GetFlowId()
    {
        return mFlowId;
    }

    uint64_t GetPtId()
    {
        return mPtId;
    }

    uint64_t GetPtv()
    {
        return mPtv;
    }

    using RecoverCallback = std::function<BResult(uint64_t ptId, const Key &key, const WCacheSliceRefPtr &sliceRef)>;
    BResult Recover(RecoverCallback recoverCallback);

    void Flush();
    void ExpiredClear();
    bool IsEmptyEvict();

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    BResult EvictAllMemSliceToDisk();
    BResult EvictAllDiskSliceToUnderFs();

    BResult EvictFromMemToDisk(WCacheSliceRefPtr sliceRef);
    BResult EvictFromDiskToUnderFs(WCacheSliceRefPtr sliceRef, bool isMaster);

    BResult FlushImpl();

    BResult ExpiredClearMemImpl(WCacheSliceRefPtr sliceRef);
    BResult ExpiredClearMem();

    BResult ExpiredClearDiskImpl(WCacheSliceRefPtr sliceRef);
    BResult ExpiredClearDisk();

    BResult PutByPass(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
        WCacheSliceRefPtr &destSliceRef, CacheAttr &attr);

private:
    uint64_t mProcId;
    uint64_t mFlowId;
    uint64_t mPtId;
    uint64_t mPtv;
    uint16_t mDiskId;
    EvictCallback mEvictCallback;
    RetryCallback mRetryCallback;

    WCacheTierPtr mCacheTiers[MAX_WCACHE_TIER];

    CacheSliceOperator mSliceOperator;

    ExecutorServicePtr mEvictService[MAX_WCACHE_TIER];
    std::atomic<bool> mEvictRef[MAX_WCACHE_TIER];

    GetLocDiskStatus mGetLocDiskStatus { nullptr };
    CheckLocRole mLocRole { nullptr };
    GetGlobEvictOffset mGlobEvictOffset { nullptr };

    RCacheManagerPtr mRCacheManager;

    UnderFsPtr mUnderFs;

    bool mIsMaster { false };

    DEFINE_REF_COUNT_VARIABLE;
};
}
}


#endif // BOOSTIO_WCACHE_H
