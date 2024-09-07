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
#include "cm.h"

namespace ock {
namespace bio {
class WCache;
using WCachePtr = Ref<WCache>;
class WCache {
public:
    WCache(uint64_t procId, uint64_t flowId, uint64_t ptId, uint64_t ptv, uint16_t diskId, bool isDegrade)
        : mProcId(procId), mFlowId(flowId), mPtId(ptId), mPtv(ptv), mDiskId(diskId), mIsDegrade(isDegrade)
    {}

    using EvictCallback = std::function<BResult(uint64_t ptId, const Key &key, WCacheSliceRefPtr sliceRef)>;
    using RetryCallback = std::function<void(uint64_t flowId, WCacheTierType cacheTier)>;

    BResult Init(const ExecutorServicePtr evictNegoService, const ExecutorServicePtr evictService[MAX_WCACHE_TIER],
        const RCacheManagerPtr rCacheManager, bool isRecover);
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
        mIsNormal = isNormal;
    }

    inline bool GetState() const
    {
        return mIsNormal;
    }

    void StartEvictTask(WCacheTierType type);

    void StartEvictNegotiateTask();

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

    inline uint64_t GetPtId() const
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

    std::map<uint64_t, std::array<uint8_t, NO_256>> *GetEvictNegotiateIndexMap()
    {
        return mCacheTiers[WCACHE_MEMORY]->GetEvictMapPtr();
    }

    using RecoverCallback = std::function<BResult(uint64_t ptId, const Key &key, const WCacheSliceRefPtr &sliceRef)>;
    BResult Recover(RecoverCallback recoverCallback);

    void Flush();
    void ExpiredClear();
    bool IsEmptyEvict(WCacheTierType type);
    bool IsEmptyNegotiate();

    void MasterEvictNegotiate(uint64_t indexs[], std::vector<bool> &result, uint32_t count);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult EvictAllMemSliceToDisk();
    BResult EvictAllDiskSliceToUnderFs();

    BResult EvictFromMemToDisk(WCacheSliceRefPtr sliceRef, bool isFront = false);
    BResult EvictFromDiskToUnderFs(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront = false);

    BResult EvictFromMemToDiskImpl(WCacheSliceRefPtr sliceRef, bool isFront);
    BResult EvictFromDiskToUnderFsImpl(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront);

    BResult EvictSlice(WCacheSliceRefPtr &sliceRef);
    BResult EvictToRcache(const WCacheSlicePtr &slice, const Key &key, void *value);

    void EvictNegotiate();

    void AddEvictNegotiateQueue(WCacheSliceRefPtr sliceRef, uint8_t refNum);

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

    BResult GetPtMasterNode(uint32_t &masterNid);

    BResult StartEvictSlice(const Key &key, WCacheSliceRefPtr &destSliceRef, CacheAttr &attr);

private:
    uint64_t mProcId;
    uint64_t mFlowId;
    uint64_t mPtId;
    uint64_t mPtv;
    uint16_t mDiskId;
    uint16_t mCopyNum{ 0 };
    bool mIsDegrade;
    bool mIsMaster{ true };
    bool mIsNormal{ true };
    bool mIsForced { false };
    std::atomic<bool> mIsStartEvictNegotiate{ false };
    std::atomic<bool> mIsMasterStartEvictNegotiate{ false };
    EvictCallback mEvictCallback;
    RetryCallback mRetryCallback;

    WCacheTierPtr mCacheTiers[MAX_WCACHE_TIER];

    CacheSliceOperator mSliceOperator;

    ExecutorServicePtr mEvictService[MAX_WCACHE_TIER];
    ExecutorServicePtr mEvictNegotiateService;
    std::atomic<bool> mEvictRef[MAX_WCACHE_TIER];

    GetLocDiskStatus mGetLocDiskStatus{ nullptr };
    CheckLocRole mLocRole{ nullptr };
    GetGlobEvictOffset mGlobEvictOffset{ nullptr };

    RCacheManagerPtr mRCacheManager;
    UnderFsPtr mUnderFs;

    std::atomic<uint64_t> mOnFlyRef;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}


#endif // BOOSTIO_WCACHE_H
