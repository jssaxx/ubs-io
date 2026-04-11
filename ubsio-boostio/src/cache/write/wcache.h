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
#include "ufs_helper.h"
#include "wcache_tier.h"
#include "rcache_manager.h"
#include "cm.h"

namespace ock {
namespace bio {
class WCache;
using WCachePtr = Ref<WCache>;
class WCache {
public:
    WCache(uint64_t procId, uint64_t flowId, uint16_t ptId, uint64_t ptv, uint16_t diskId, bool isDegrade)
        : mProcId(procId), mFlowId(flowId), mPtId(ptId), mPtv(ptv), mDiskId(diskId), mIsDegrade(isDegrade)
    {}

    using EvictCallback = std::function<BResult(uint16_t ptId, const Key &key, WCacheSliceRefPtr sliceRef)>;
    using RetryCallback = std::function<void(uint64_t flowId, WCacheTierType cacheTier)>;

    BResult Init(const ExecutorServicePtr evictService[MAX_WCACHE_TIER], const RCacheManagerPtr rCacheManager,
        bool isRecover);
    void Exit();

    void RegOp(GetLocDiskStatus getLocDiskStatus, CheckLocRole locRole, const GetGlobEvictOffset evictOffset,
        EvictCallback evictCallback, const RetryCallback retryCallback);

    static void GetCacheResource(uint64_t &memCap, uint64_t &memUsed, uint64_t &diskCap, uint64_t &diskUsed);

    BResult GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult Put(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
        WCacheSliceRefPtr &destSliceRef, CacheAttr &attr);

    BResult PutImpl(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
        WCacheSliceRefPtr &destSliceRef, CacheAttr &attr);

    BResult Delete(const Key &key, const WCacheSliceRefPtr &sliceRef);

    BResult Seal(WCacheTierType type);

    void Destroy();

    inline void SetDegradeState(bool flag)
    {
        mIsDegrade = flag;
    }

    inline bool GetDegradeState() const
    {
        return mIsDegrade;
    }

    inline void SetState(bool isNormal)
    {
        mIsNormal.store(isNormal);
    }

    inline bool GetState() const
    {
        return mIsNormal.load();
    }

    inline bool IsIoFinish() const
    {
        return mOnFlyRef == 0;
    }

    void StartEvictTask(WCacheTierType type);

    void RetryEvictTask(WCacheTierType type);

    uint64_t GetCapacity(WCacheTierType type);

    uint64_t GetVirCapacity(WCacheTierType type);

    uint64_t GetEvictOffset();

    inline uint64_t GetProcId() const
    {
        return mProcId;
    }

    inline uint64_t GetFlowId() const
    {
        return mFlowId;
    }

    inline uint16_t GetPtId() const
    {
        return mPtId;
    }

    inline uint64_t GetPtv() const
    {
        return mPtv;
    }

    inline void IncFlyIo()
    {
        mOnFlyRef += 1;
    }

    inline void DecFlyIo()
    {
        mOnFlyRef -= 1;
    }

    inline uint64_t GetFlyIo()
    {
        return mOnFlyRef.load();
    }

    inline uint64_t GetIndex()
    {
        return mIndex;
    }

    inline uint64_t GetOffset()
    {
        return mOffset;
    }

    inline void SetProcId(uint64_t procId)
    {
        mProcId = procId;
    }

    using RecoverCallback = std::function<BResult(uint16_t ptId, const Key &key, const WCacheSliceRefPtr &sliceRef)>;
    BResult Recover(RecoverCallback recoverCallback);

    void Flush(const WCachePtr &self);
    void ExpiredClear(const WCachePtr &self);
    void ProcAndCacheBrokenExpiredClear();
    bool IsEmptyEvict(WCacheTierType type);

    uint64_t GetTruncateIndex();
    BResult AllocRCacheResource(const WCacheSlicePtr &srcSlice, WCacheSlicePtr &dstSlice, bool &isRCache);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult EvictAllMemSliceToDisk();
    BResult EvictAllDiskSliceToUnderFs();

    BResult EvictFromMemToDisk(WCacheSliceRefPtr sliceRef, bool isFront = false);
    BResult EvictFromDiskToUnderFs(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront = false);

    BResult EvictFromMemToDiskImpl(WCacheSliceRefPtr sliceRef, bool isFront);
    BResult EvictFromDiskToUnderFsImpl(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront);

    BResult EvictSlice(WCacheSliceRefPtr &sliceRef);
    void FreeRCacheResource(bool &isRCache, WCacheSlicePtr &slice);
    void EvictToRCache(const WCacheSlicePtr &srcSlice, const Key &key, WCacheSlicePtr &slice, bool &isRCache);
    BResult EvictToUnderFS(const char *key, WCacheSlicePtr &slice, const size_t length);

    bool EvictMemSatisfiedCond();
    bool EvictDiskSatisfiedCond();

    BResult FlushMem();
    BResult FlushDisk();

    BResult ExpiredClearMemImpl(WCacheSliceRefPtr sliceRef);
    BResult ExpiredClearMem();

    BResult ExpiredClearDiskImpl(WCacheSliceRefPtr sliceRef);
    BResult ExpiredClearDisk();

    void PutSetIoStrategy(RealIoStrategy &ioStrategy, CacheAttr &attr);

    BResult PutByPass(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
        WCacheSliceRefPtr &destSliceRef, CacheAttr &attr);

    BResult StartEvictSlice(const Key &key, WCacheSliceRefPtr &destSliceRef, CacheAttr &attr);

private:
    uint64_t mProcId;
    uint64_t mFlowId;
    Lock indexOffsetLock;
    uint64_t mOffset;
    uint64_t mIndex;
    uint16_t mPtId;
    uint64_t mPtv;
    uint16_t mDiskId;
    bool mIsDegrade;
    bool mIsMaster{ true };
    std::atomic<bool> mIsNormal { true };
    bool mIsForced { false };
    bool mUfsEnable{ false };

    EvictCallback mEvictCallback;
    RetryCallback mRetryCallback;

    WCacheTierPtr mCacheTiers[MAX_WCACHE_TIER];

    CacheSliceOperator mSliceOperator;

    ExecutorServicePtr mEvictService[MAX_WCACHE_TIER];
    std::atomic<bool> mEvictRef[MAX_WCACHE_TIER];

    GetLocDiskStatus mGetLocDiskStatus{ nullptr };
    CheckLocRole mLocRole{ nullptr };
    GetGlobEvictOffset mGlobEvictOffset{ nullptr };

    RCacheManagerPtr mRCacheManager;
    UfsHelperPtr mUnderFs;

    std::atomic<uint64_t> mOnFlyRef;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}


#endif // BOOSTIO_WCACHE_H
