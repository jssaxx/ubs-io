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
enum class WCacheAccessState : uint8_t {
    READ_WRITE = 0,
    READ_ONLY,
};

class WCache {
public:
    WCache(uint64_t procId, uint64_t flowId, uint16_t ptId, uint64_t ptv, uint16_t diskId, bool isDegrade)
        : mProcId(procId), mFlowId(flowId), mPtId(ptId), mPtv(ptv), mDiskId(diskId), mIsDegrade(isDegrade)
    {}
    ~WCache();

    using RecordMetaDeleteEventCallback = std::function<BResult(uint16_t ptId, const Key &key,
        WCacheSliceRefPtr sliceRef,
        const UbsIoMetaEventBatchPtr &batch)>;
    using RetryCallback = std::function<void(uint64_t flowId, WCacheTierType cacheTier)>;
    using ScheduleEvictCallback = std::function<void(WCacheTierType cacheTier)>;
    using SubmitMetaEventBatchCallback = std::function<void(const UbsIoMetaEventBatchPtr &batch)>;
    using PublishIndexCallback = std::function<BResult(uint16_t, const Key &, const WCacheSliceRefPtr &)>;

    BResult Init(const ExecutorServicePtr evictService[MAX_WCACHE_TIER], const RCacheManagerPtr rCacheManager,
        bool isRecover);
    void Exit();

    void RegOp(GetLocDiskStatus getLocDiskStatus, CheckLocRole locRole, const GetGlobEvictOffset evictOffset,
        RecordMetaDeleteEventCallback recordMetaDeleteEventCallback, const RetryCallback retryCallback,
        SubmitMetaEventBatchCallback submitMetaEventBatchCallback,
        ScheduleEvictCallback scheduleEvictCallback = nullptr, PublishIndexCallback publishIndexCallback = nullptr);

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

    inline void MarkReadOnly()
    {
        mAccessState.store(WCacheAccessState::READ_ONLY);
    }

    inline bool IsWritable() const
    {
        return mAccessState.load() == WCacheAccessState::READ_WRITE;
    }

    inline bool IsIoFinish() const
    {
        return mOnFlyRef == 0;
    }

    inline bool IsEvictIoFinish() const
    {
        for (const auto &ref : mEvictOnFlyRef) {
            if (ref.load() != 0) {
                return false;
            }
        }
        return true;
    }

    void StartEvictTask(WCacheTierType type);

    void StartDirectUnderFsEvict();

    void RetryEvictTask(WCacheTierType type);

    // The manager admits the batch under its map lock, before fault cleanup can fence this flow.
    bool BeginEvictBatch(WCacheTierType type);
    BResult EvictBatch(WCacheTierType type, uint32_t maxCount, uint32_t &evictedCount);
    bool NeedEvict(WCacheTierType type);

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

    inline uint16_t GetDiskId() const
    {
        return mDiskId;
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
    inline void SetStandaloneFault()
    {
        mStandaloneFault.store(true);
    }
    inline bool IsStandaloneFault() const
    {
        return mStandaloneFault.load();
    }
    BResult ForceClearMemoryTier();
    BResult ReleaseFaultedResources();
    void ProcAndCacheBrokenExpiredClear();
    bool IsEmptyEvict(WCacheTierType type);

    uint64_t GetTruncateIndex();
    BResult AllocRCacheResource(const WCacheSlicePtr &srcSlice, WCacheSlicePtr &dstSlice, bool &isRCache);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    struct SliceOpGuard {
        const WCacheSliceRefPtr &sliceRef;
        ~SliceOpGuard()
        {
            sliceRef->OpUnLock();
        }
    };

    void CompleteMemToDisk(const WCacheSliceRefPtr &sliceRef, const WCacheSlicePtr &oldSlice,
        const WCacheSlicePtr &memoryMeta, const WCacheSlicePtr &diskMeta);
    BResult PublishMemorySlice(const Key &key, const WCacheSliceRefPtr &sliceRef);

    // Legacy fault/retry paths may clear mEvictRef before this admitted batch exits.
    // Only the batch owner returns its independent in-flight count, on every exit path.
    struct BatchGuard {
        std::atomic<bool> &evictRef;
        std::atomic<uint64_t> &onFlyRef;
        ~BatchGuard()
        {
            evictRef.store(false);
            onFlyRef.fetch_sub(1);
        }
    };

    BResult EvictAllMemSliceToDisk(uint32_t maxCount = NO_MAX_VALUE32, uint32_t *evictedCount = nullptr);
    BResult EvictAllMemSliceWithoutDisk(uint32_t maxCount = NO_MAX_VALUE32, uint32_t *evictedCount = nullptr);
    BResult EvictAllDiskSliceToUnderFs(uint32_t maxCount = NO_MAX_VALUE32, uint32_t *evictedCount = nullptr);

    BResult EvictFromMemToDisk(WCacheSliceRefPtr sliceRef, bool isFront = false,
        const UbsIoMetaEventBatchPtr &batch = nullptr);
    BResult EvictFromMemToDiscard(WCacheSliceRefPtr sliceRef, const UbsIoMetaEventBatchPtr &batch = nullptr);
    BResult EvictFromMemToUnderFs(WCacheSliceRefPtr sliceRef, const UbsIoMetaEventBatchPtr &batch = nullptr);
    BResult EvictFromDiskToUnderFs(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront = false,
        const UbsIoMetaEventBatchPtr &batch = nullptr);

    BResult EvictFromMemToDiskImpl(WCacheSliceRefPtr sliceRef, bool isFront);
    BResult EvictFromDiskToUnderFsImpl(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront,
        const UbsIoMetaEventBatchPtr &batch = nullptr);

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

    BResult PutSetIoStrategy(RealIoStrategy &ioStrategy, CacheAttr &attr);

    BResult CreateMemoryTombstone(const WCacheSlicePtr &srcSlice);
    BResult CreateDiskTombstone(const WCacheSlicePtr &slice);
    BResult EvictMemoryTombstone(WCacheSliceRefPtr &sliceRef);

    BResult PutByPass(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
        WCacheSliceRefPtr &destSliceRef, CacheAttr &attr);

    BResult StartEvictSlice(WCacheSliceRefPtr &destSliceRef, RealIoStrategy ioStrategy);
    bool IsOwnDiskNormal();

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
    std::atomic<WCacheAccessState> mAccessState { WCacheAccessState::READ_WRITE };
    std::atomic<bool> mStandaloneFault { false };
    std::atomic<bool> mIsForced { false };
    bool mUfsEnable{ false };
    bool mHasDiskCache{ true };
    bool mDirectUnderFs{ false };

    RecordMetaDeleteEventCallback mRecordMetaDeleteEventCallback;
    RetryCallback mRetryCallback;
    SubmitMetaEventBatchCallback mSubmitMetaEventBatchCallback;
    ScheduleEvictCallback mScheduleEvictCallback;
    PublishIndexCallback mPublishIndexCallback;

    WCacheTierPtr mCacheTiers[MAX_WCACHE_TIER];

    CacheSliceOperator mSliceOperator;

    ExecutorServicePtr mEvictService[MAX_WCACHE_TIER];
    std::atomic<bool> mEvictRef[MAX_WCACHE_TIER]{};

    GetLocDiskStatus mGetLocDiskStatus{ nullptr };
    CheckLocRole mLocRole{ nullptr };
    GetGlobEvictOffset mGlobEvictOffset{ nullptr };

    RCacheManagerPtr mRCacheManager;
    UfsHelperPtr mUnderFs;

    // Put admission through publication and queue progress; background eviction does not change this count.
    std::atomic<uint64_t> mOnFlyRef{ 0 };
    // Admitted global batches, including failure cleanup; queued retries hold no count.
    std::atomic<uint64_t> mEvictOnFlyRef[MAX_WCACHE_TIER]{};

    DEFINE_REF_COUNT_VARIABLE;
};
}
}


#endif // BOOSTIO_WCACHE_H
