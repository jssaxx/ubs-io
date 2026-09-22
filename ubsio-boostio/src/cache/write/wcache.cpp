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

#include "wcache.h"

#include <atomic>
#include <utility>
#include "unistd.h"
#include "bio_monotonic.h"
#include "cache_slice_operator.h"
#include "securec.h"
#include "flow_manager.h"
#include "cache_flow.h"
#include "bio_trace.h"
#include "cache_overload_ctrl.h"
#include "bio_config_instance.h"
#include "bio_crc_util.h"
#include "bio_server.h"

namespace ock {
namespace bio {
constexpr uint32_t EVICT_MEM_HLEVEL = 90;
constexpr uint32_t EVICT_DISK_HLEVEL = 98;
constexpr uint32_t MAX_EVICT_CONSULT_SIZE = 50;
constexpr uint64_t TOMBSTONE_FAIL_LOG_INTERVAL_SEC = 1;

void LogTombstoneFailure(const char *action, BResult ret, uint64_t flowId, uint64_t index)
{
    static std::atomic<uint64_t> lastLogSec(0);
    uint64_t now = Monotonic::TimeSec();
    uint64_t last = lastLogSec.load();
    if (now >= last + TOMBSTONE_FAIL_LOG_INTERVAL_SEC &&
        lastLogSec.compare_exchange_strong(last, now)) {
        LOG_WARN(action << ", ret:" << ret << ", flowId:" << flowId << ", index:" << index <<
            ". Further tombstone failures are rate-limited.");
    }
}

WCache::~WCache()
{
    if (mStandaloneFault.load()) {
        BResult ret = ReleaseFaultedResources();
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Release faulted resources failed, ret:" << ret << ", flowId:" << mFlowId << ".");
        }
    }
}

BResult WCache::Init(const ExecutorServicePtr evictService[MAX_WCACHE_TIER], const RCacheManagerPtr rCacheManager,
    bool isRecover)
{
    BResult ret = BIO_INNER_ERR;
    mHasDiskCache = BioConfig::Instance()->GetDaemonConfig().hasDiskCache;
    mUfsEnable = BioConfig::Instance()->GetUnderFsConfig().underFsType != "none";
    mDirectUnderFs = BioServer::Instance()->IsStandaloneMode() && !mHasDiskCache && mUfsEnable;
    uint32_t tierCount = mHasDiskCache ? MAX_WCACHE_TIER : WCACHE_DISK;
    for (uint32_t i = 0; i < tierCount; ++i) {
        auto cacheTier = MakeRef<WCacheTier>();
        ChkTrue(cacheTier != nullptr, BIO_ALLOC_FAIL, "Make wcache tier failed.");

        bool useCompactMeta = mDirectUnderFs && i == WCACHE_MEMORY;
        ret = cacheTier->Init(static_cast<WCacheTierType>(i), mFlowId, mDiskId, useCompactMeta);
        ChkTrue(ret == BIO_OK, ret, "Failed to init cacheTier, WCacheTierType:" << i << " flowId:" << mFlowId);
        mCacheTiers[i] = cacheTier;
    }
    if (!mHasDiskCache) {
        mCacheTiers[WCACHE_DISK] = nullptr;
    }

    mEvictService[WCACHE_MEMORY] = evictService[WCACHE_MEMORY];
    mEvictService[WCACHE_DISK] = evictService[WCACHE_DISK];
    mEvictRef[WCACHE_MEMORY] = false;
    mEvictRef[WCACHE_DISK] = false;
    mOnFlyRef = 0;
    for (auto &ref : mEvictOnFlyRef) {
        ref.store(0);
    }
    mOffset = 0;
    mIndex = 0;
    mRCacheManager = rCacheManager;
    mUnderFs = UfsHelper::Instance();
    if (isRecover) {
        mIsMaster = false; // 用于识别Put流程特殊处理
        if (BioServer::Instance()->IsStandaloneMode()) {
            MarkReadOnly();
        }
        return BIO_OK;
    }

    ret = mLocRole(static_cast<uint16_t>(mPtId), mIsMaster); // 创建时获取当时的副本主备，用于降级场景的PUT流程
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get role fail:" << ret << ", ptId:" << mPtId << " flowId:" << mFlowId);
        return ret;
    }
    if (mIsMaster) {
        mCacheTiers[WCACHE_MEMORY]->SetGlobMinTruncateIndex(NO_U64_0);
    }

    LOG_INFO("init wcache success, ptId:" << mPtId << ", flowId:" << mFlowId << ", isMaster:" << mIsMaster << ".");
    return BIO_OK;
}

void WCache::RegOp(GetLocDiskStatus getLocDiskStatus, CheckLocRole locRole, const GetGlobEvictOffset evictOffset,
    RecordMetaDeleteEventCallback recordMetaDeleteEventCallback, const RetryCallback retryCallback,
    SubmitMetaEventBatchCallback submitMetaEventBatchCallback, ScheduleEvictCallback scheduleEvictCallback,
    PublishIndexCallback publishIndexCallback)
{
    mGetLocDiskStatus = getLocDiskStatus;
    mLocRole = locRole;
    mGlobEvictOffset = evictOffset;
    mRecordMetaDeleteEventCallback = recordMetaDeleteEventCallback;
    mRetryCallback = retryCallback;
    mSubmitMetaEventBatchCallback = submitMetaEventBatchCallback;
    mScheduleEvictCallback = std::move(scheduleEvictCallback);
    mPublishIndexCallback = std::move(publishIndexCallback);
}

void WCache::Exit()
{
    mCacheTiers[WCACHE_MEMORY] = nullptr;
    mCacheTiers[WCACHE_DISK] = nullptr;
}

void WCache::GetCacheResource(uint64_t &memCap, uint64_t &memUsed, uint64_t &diskCap, uint64_t &diskUsed)
{
    auto config = BioConfig::Instance()->GetDaemonConfig();
    memCap = (static_cast<uint64_t>(config.memWriteRatio) * config.memCap) / NO_10;
    memUsed = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_MEMORY, 0);
    diskCap = 0;
    diskUsed = 0;
    if (!config.hasDiskCache) {
        return;
    }
    for (uint32_t diskId = 0; diskId < config.diskCaps.size(); diskId++) {
        // Faulted standalone disks no longer serve any PT, so their capacity
        // must not dilute the disk watermark used by overload control and the
        // WCACHE capacity reported through show resources.
        if (BioServer::Instance()->IsStandaloneDiskFault(static_cast<uint16_t>(diskId))) {
            continue;
        }
        diskCap += static_cast<uint64_t>(config.diskCaps[diskId]);
        diskUsed += FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, diskId);
    }
    diskCap = diskCap * static_cast<uint64_t>(config.diskWriteRatio) / NO_10;
}

BResult WCache::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    if (UNLIKELY(!IsWritable())) {
        return BIO_INNER_RETRY;
    }
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    return memCache->GetDataSlice(sliceKey, slice);
}

BResult WCache::Put(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    if (UNLIKELY(!IsWritable())) {
        return BIO_INNER_RETRY;
    }
    // The manager holds the in-flight reference through publication and queue admission.
    auto ret = PutImpl(key, srcSlice, sliceReader, destSliceRef, attr);
    // A failed head flush does not undo the current slice's admission or consumed range.
    if (ret == BIO_OK || (destSliceRef != nullptr && destSliceRef->GetState() != SLICE_PENDING)) {
        uint64_t newoffset = srcSlice->GetOffsetInFlow() + srcSlice->GetLength();
        uint64_t newIndex = srcSlice->GetIndexInFlow() + 1;
        indexOffsetLock.DoLock();
        mOffset = newoffset > mOffset ? newoffset : mOffset;
        mIndex = newIndex > mIndex ? newIndex : mIndex;
        indexOffsetLock.UnLock();
    }
    return ret;
}

BResult WCache::PutImpl(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    BResult ret = BIO_OK;
    // 1. degraded write through to underFS.
    if (UNLIKELY(mIsDegrade)) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_BYPASS);
        ret = PutByPass(key, srcSlice, sliceReader, destSliceRef, attr);
        BIO_TRACE_END(WCACHE_TRACE_PUT_BYPASS, ret);
        return ret;
    }

    if (!mHasDiskCache && (attr.strategy != WRITE_BACK || attr.ioStrategy > WRITE_MEM_BACK)) {
        LOG_ERROR("Write through is not supported when disk cache is disabled, flowId:" << mFlowId << ".");
        return BIO_INVALID_PARAM;
    }
    ChkTrueNot(mPublishIndexCallback != nullptr, BIO_INNER_ERR);
    RealIoStrategy ioStrategy = WRITE_DEFAULT;
    ret = PutSetIoStrategy(ioStrategy, attr);
    ChkTrueNot(ret == BIO_OK, ret);

    // 2. put it to memory tier cache.
    BIO_TP_START(WRITE_SLICE_NULL_FAIL, &ret, BIO_INNER_RETRY);
    ret = mCacheTiers[WCACHE_MEMORY]->Write(key, srcSlice, sliceReader, destSliceRef);
    BIO_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Memory cache write failed.");
        BResult tombRet = CreateMemoryTombstone(srcSlice);
        if (UNLIKELY(tombRet != BIO_OK)) {
            LogTombstoneFailure("Create memory tombstone failed", tombRet, mFlowId,
                srcSlice->GetIndexInFlow());
        }
        return ret;
    }

    // Publish complete memory data before any worker can obtain this slice from the queue.
    ret = PublishMemorySlice(key, destSliceRef);
    if (ret != BIO_OK) {
        destSliceRef->SetState(SLICE_INVALID);
        // Every memory consumer handles INVALID as a tombstone, even if marking failed.
        mCacheTiers[WCACHE_MEMORY]->AddEvictQueue(destSliceRef);
        StartEvictTask(WCACHE_MEMORY);
        return ret;
    }

    // 3. Queue only resolved slices, then preserve the existing head-flush policy.
    ret = StartEvictSlice(destSliceRef, ioStrategy);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Start evict slice failed, ret:" << ret << ", key:" << key << ".");
    }
    return ret;
}

BResult WCache::PublishMemorySlice(const Key &key, const WCacheSliceRefPtr &sliceRef)
{
    // Keep metadata writes serialized with Delete as soon as Insert makes the reference visible.
    if (!sliceRef->OpLock()) {
        return BIO_INNER_RETRY;
    }
    SliceOpGuard guard{sliceRef};
    BIO_TRACE_START(WCACHE_TRACE_PUT_INSERT_INDEX);
    auto ret = mPublishIndexCallback(mPtId, key, sliceRef);
    BIO_TRACE_END(WCACHE_TRACE_PUT_INSERT_INDEX, ret);
    if (ret != BIO_OK) {
        sliceRef->SetState(SLICE_INVALID);
    }
    if (sliceRef->GetState() == SLICE_INVALID) {
        // Only write one; a successful publication never resets a Delete's marker to zero.
        auto markRet = Delete(key, sliceRef);
        if (ret == BIO_OK) {
            ret = markRet;
        }
    }
    return ret;
}

BResult WCache::CreateMemoryTombstone(const WCacheSlicePtr &srcSlice)
{
    if (srcSlice == nullptr) {
        LogTombstoneFailure("Create memory tombstone with null slice", BIO_INVALID_PARAM, mFlowId, 0);
        return BIO_INVALID_PARAM;
    }
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    if (memCache == nullptr) {
        LogTombstoneFailure("Create memory tombstone with null tier", BIO_ERR, mFlowId, 0);
        return BIO_ERR;
    }

    uint64_t index = srcSlice->GetIndexInFlow();
    if (index != 0 && UINT64_MAX / index < sizeof(WFlowSliceMeta)) {
        LogTombstoneFailure("Memory tombstone index overflow", BIO_INNER_RETRY, mFlowId, index);
        return BIO_INNER_RETRY;
    }
    uint64_t offset = srcSlice->GetOffsetInFlow();
    uint64_t length = srcSlice->GetLength();
    WFlowMetaDataSlice metaDataSlice;
    auto ret = memCache->GetMetaDataSlice(index, offset, length, metaDataSlice);
    if (UNLIKELY(ret != BIO_OK)) {
        LogTombstoneFailure("Allocate memory tombstone slice failed", ret, mFlowId, index);
        return ret;
    }

    WCacheSliceRefPtr tombstoneRef = MakeRef<WCacheSliceRef>(metaDataSlice.dataSlice);
    if (UNLIKELY(tombstoneRef == nullptr)) {
        LogTombstoneFailure("Make memory tombstone ref failed", BIO_ALLOC_FAIL, mFlowId, index);
        return BIO_ALLOC_FAIL;
    }
    tombstoneRef->SetState(SLICE_INVALID);
    memCache->AddEvictQueue(tombstoneRef);
    StartEvictTask(WCACHE_MEMORY);
    LOG_DEBUG("Create memory tombstone success, flowId:" << mFlowId << ", index:" << index << ", offset:" <<
        offset << ", length:" << length << ".");
    return BIO_OK;
}

BResult WCache::StartEvictSlice(WCacheSliceRefPtr &destSliceRef, RealIoStrategy ioStrategy)
{
    // 2. Add evict queue.
    mCacheTiers[WCACHE_MEMORY]->AddEvictQueue(destSliceRef);
    if (mDirectUnderFs) {
        StartEvictTask(WCACHE_MEMORY);
        return BIO_OK;
    }
    // 3. put it disk tier cache.
    if (ioStrategy <= WRITE_MEM_BACK) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_MEM_BACK);
        StartEvictTask(WCACHE_MEMORY);
        BIO_TRACE_END(WCACHE_TRACE_PUT_MEM_BACK, 0);
        return BIO_OK;
    }

    // 4. write thought
    BResult ret = BIO_INNER_ERR;
    auto metaEventBatch = std::make_shared<UbsIoMetaEventBatch>();
    ChkTrueNot(metaEventBatch != nullptr, BIO_ALLOC_FAIL);
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    if (sliceRef != nullptr) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_DISK_BACK);
        ret = sliceRef->GetState() == SLICE_INVALID ? EvictMemoryTombstone(sliceRef) :
            EvictFromMemToDisk(sliceRef, true, metaEventBatch);
        BIO_TRACE_END(WCACHE_TRACE_PUT_DISK_BACK, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            mRetryCallback(mFlowId, WCACHE_MEMORY);
            LOG_DEBUG("Put key, flowId:" << sliceRef->GetSlice()->GetFlowId() <<
                ", IndexInFlow:" << sliceRef->GetSlice()->GetIndexInFlow());
            if (mSubmitMetaEventBatchCallback != nullptr) {
                mSubmitMetaEventBatchCallback(metaEventBatch);
            }
            return ret;
        }
    }

    // put it underfs tier.
    if (ioStrategy <= WRITE_DISK_BACK || mUfsEnable == false) {
        if (mSubmitMetaEventBatchCallback != nullptr) {
            mSubmitMetaEventBatchCallback(metaEventBatch);
        }
        return BIO_OK;
    }
    sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
    if (sliceRef != nullptr) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_UNDERFS_BACK);
        ret = EvictFromDiskToUnderFs(sliceRef, mIsMaster, true, metaEventBatch);
        BIO_TRACE_END(WCACHE_TRACE_PUT_UNDERFS_BACK, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
            mRetryCallback(mFlowId, WCACHE_DISK);
            if (mSubmitMetaEventBatchCallback != nullptr) {
                mSubmitMetaEventBatchCallback(metaEventBatch);
            }
            return ret;
        }
    }
    if (mSubmitMetaEventBatchCallback != nullptr) {
        mSubmitMetaEventBatchCallback(metaEventBatch);
    }
    return BIO_OK;
}

BResult WCache::PutSetIoStrategy(RealIoStrategy &ioStrategy, CacheAttr &attr)
{
    ioStrategy = attr.ioStrategy;
    if (ioStrategy == WRITE_DEFAULT) {
        if (attr.strategy == WRITE_BACK) {
            ioStrategy = WRITE_MEM_BACK;
        } else {
            ioStrategy = WRITE_DISK_BACK;
        }
    }

    auto config = BioConfig::Instance()->GetDaemonConfig();
    if (!mHasDiskCache) {
        if (ioStrategy > WRITE_MEM_BACK || attr.strategy != WRITE_BACK) {
            LOG_ERROR("Write through is not supported when disk cache is disabled, flowId:" << mFlowId << ".");
            return BIO_INVALID_PARAM;
        }
        attr.ioStrategy = WRITE_MEM_BACK;
        return BIO_OK;
    }

    uint64_t memConfig = (static_cast<uint64_t>(config.memWriteRatio) * config.memCap) / NO_10;
    uint64_t memUsed = BioServer::Instance()->GetMemUsedSize();
    uint64_t memWcache = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_MEMORY, 0);
    uint64_t memRcache = FlowManager::GetCacheUsedSize(FLOW_RCACHE, FLOW_MEMORY, 0);

    LOG_TRACE("Total mem:" << (config.memCap / NO_1MB) << ", used:" << (memUsed / NO_1MB) <<
        ", wcache:" << (memWcache / NO_1MB) << ", rcache:" << (memRcache / NO_1MB) << ", strategy:" << ioStrategy);

    uint64_t diskConfig = (static_cast<uint64_t>(config.diskWriteRatio * config.diskCaps[mDiskId])) / NO_10;
    uint64_t diskWcache = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, mDiskId);
    uint64_t diskRcache = FlowManager::GetCacheUsedSize(FLOW_RCACHE, FLOW_DISK, mDiskId);
    uint64_t diskUsed = diskWcache + diskRcache;

    LOG_TRACE("Total disk:" << (config.diskCaps[mDiskId] / NO_1MB) << ", used:" << (diskUsed / NO_1MB) <<
        ", wcache:" << (diskWcache / NO_1MB) << ", rcache:" << (diskRcache / NO_1MB) << ", strategy:" <<
        ioStrategy << ", diskId:" << mDiskId);

    bool isMemSatisfied = ((memUsed < (config.memCap * EVICT_MEM_HLEVEL / NO_100)) &&
        (memWcache < (memConfig * EVICT_MEM_HLEVEL / NO_100)));
    bool isDiskSatisfied = (diskWcache < (diskConfig * EVICT_DISK_HLEVEL / NO_100));

    if (isMemSatisfied && isDiskSatisfied && (attr.strategy == WRITE_BACK)) {
        attr.ioStrategy = WRITE_MEM_BACK;
        return BIO_OK;
    }

    if (!isMemSatisfied && isDiskSatisfied) {
        attr.ioStrategy = WRITE_DISK_BACK;
        return BIO_OK;
    }

    if (mUfsEnable) {
        attr.ioStrategy = WRITE_UNDERFS_BACK;
    }
    return BIO_OK;
}

BResult WCache::PutByPass(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    if (!mIsMaster) {
        LOG_DEBUG("Degrade in standby node, key:" << key << " flowId:" << mFlowId);
        destSliceRef = nullptr;
        return BIO_OK;
    }

    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    auto ret = memCache->Write(key, srcSlice, sliceReader, destSliceRef);
    ChkTrueNot(destSliceRef != nullptr, BIO_INNER_ERR);

    auto *value = new (std::nothrow) char[srcSlice->GetLength()];
    ChkTrueNot(value != nullptr, BIO_ALLOC_FAIL);

    ret = mSliceOperator.Copy(destSliceRef->GetSlice().Get(), value, srcSlice->GetLength());
    if (UNLIKELY(ret != BIO_OK)) {
        delete[] value;
        LOG_ERROR("Failed to copy slice to value, key:" << key << " flowId:" << mFlowId);
        return ret;
    }

    if (mUfsEnable) {
        ret = mUnderFs->Put(key, value, srcSlice->GetLength());
        delete[] value;
        ChkTrue(ret == BIO_OK, ret, "Failed to put slice to underfs, key:" << key << " flowId:" << mFlowId);
    } else {
        delete[] value;
    }

    ret = memCache->Evict(destSliceRef->GetSlice());
    ChkTrue(ret == BIO_OK, ret, "Failed to evict, key:" << key << " flowId:" << mFlowId);

    destSliceRef = nullptr;
    return BIO_OK;
}

BResult WCache::Delete(const Key &key, const WCacheSliceRefPtr &sliceRef)
{
    auto slice = sliceRef->GetSlice();
    if (slice == nullptr) {
        LOG_ERROR("slice is null.");
        return BIO_OK;
    }
    if (mDirectUnderFs) {
        return BIO_OK;
    }
    WCacheSlicePtr metaSlice = nullptr;
    BIO_TP_START(WCACHE_FLOW_DISK_FAIL, slice->GetFlowType(), FLOW_DISK);
    BIO_TP_END;
    if (slice->GetFlowType() == FLOW_MEMORY) {
        auto ret = mCacheTiers[WCACHE_MEMORY]->GetMetaSlice(slice->GetIndexInFlow(), metaSlice);
        ChkTrue(ret == BIO_OK, ret,
            "Failed to get meta slice, flowId:" << slice->GetFlowId() << ", flowIndex:" << slice->GetIndexInFlow() <<
            ", flowOffset:" << slice->GetOffsetInFlow());
    } else {
        auto ret = mCacheTiers[WCACHE_DISK]->GetMetaSlice(slice->GetIndexInFlow(), metaSlice);
        ChkTrue(ret == BIO_OK, ret,
            "Failed to get meta slice, flowId:" << slice->GetFlowId() << ", flowIndex:" << slice->GetIndexInFlow() <<
            ", flowOffset:" << slice->GetOffsetInFlow());
    }

    LOG_DEBUG("Delete key:" << key << ", flowId:" << slice->GetFlowId() << ", flowIndex:" << slice->GetIndexInFlow() <<
        ", flowOffset:" << slice->GetOffsetInFlow());
    WFlowSliceMeta sliceMeta;
    auto ret = mSliceOperator.Copy(metaSlice.Get(), (char *)&sliceMeta, sizeof(WFlowSliceMeta));
    ChkTrue(ret == BIO_OK, ret, "Slice copy failed.");

    sliceMeta.hasEvict = 1;
    ret = mSliceOperator.Copy((char *)&sliceMeta, metaSlice.Get());
    ChkTrue(ret == BIO_OK, ret, "Slice copy failed.");
    return BIO_OK;
}

BResult WCache::Seal(WCacheTierType type)
{
    return mCacheTiers[type]->Seal();
}

void WCache::Destroy()
{
    mCacheTiers[WCACHE_MEMORY]->Destroy();
    if (mCacheTiers[WCACHE_DISK] != nullptr) {
        mCacheTiers[WCACHE_DISK]->Destroy();
    }
}

void WCache::StartEvictTask(WCacheTierType type)
{
    if (mScheduleEvictCallback != nullptr && !mIsForced && GetState()) {
        mScheduleEvictCallback(type);
        return;
    }
    if (mStandaloneFault.load()) {
        mEvictRef[type].store(false);
        return;
    }
    if (type == WCACHE_DISK && !mHasDiskCache) {
        mEvictRef[type].store(false);
        return;
    }

    bool isNormal = IsOwnDiskNormal();
    if (!isNormal) {
        mEvictRef[type].store(false); // break task
        return;
    }

    bool expectval = false;
    if (!mEvictRef[type].compare_exchange_weak(expectval, true)) {
        return;
    }

    bool isSucceed;
    IncreaseRef();
    if (type == WCACHE_MEMORY) {
        isSucceed = mEvictService[type]->Execute([this]() {
            EvictAllMemSliceToDisk();
            DecreaseRef();
        });
    } else {
        isSucceed = mEvictService[type]->Execute([this]() {
            EvictAllDiskSliceToUnderFs();
            DecreaseRef();
        });
    }

    if (!isSucceed) {
        mEvictRef[type].store(false);
        DecreaseRef();
    }
    return;
}

void WCache::StartDirectUnderFsEvict()
{
    if (mDirectUnderFs) {
        StartEvictTask(WCACHE_MEMORY);
    }
}

void WCache::RetryEvictTask(WCacheTierType type)
{
    if (mScheduleEvictCallback != nullptr && !mIsForced && GetState()) {
        mScheduleEvictCallback(type);
        return;
    }
    if (mStandaloneFault.load()) {
        mEvictRef[type].store(false);
        return;
    }
    if (type == WCACHE_DISK && !mHasDiskCache) {
        mEvictRef[type].store(false);
        return;
    }

    if (mCacheTiers[type]->IsEmptyEvictSliceQueue()) {
        mEvictRef[type].store(false);
        return;
    }

    bool isNormal = IsOwnDiskNormal();
    if (!isNormal) {
        mEvictRef[type].store(false); // break task
        return;
    }

    bool isSucceed;
    IncreaseRef();
    if (type == WCACHE_MEMORY) {
        isSucceed = mEvictService[type]->Execute([this]() {
            EvictAllMemSliceToDisk();
            DecreaseRef();
        });
    } else {
        isSucceed = mEvictService[type]->Execute([this]() {
            EvictAllDiskSliceToUnderFs();
            DecreaseRef();
        });
    }

    if (!isSucceed) {
        mEvictRef[type].store(false);
        DecreaseRef();
    }
    return;
}

bool WCache::NeedEvict(WCacheTierType type)
{
    if (type >= MAX_WCACHE_TIER || mIsForced || !GetState() || mStandaloneFault.load() ||
        mCacheTiers[type] == nullptr || mCacheTiers[type]->IsEmptyEvictSliceQueue()) {
        return false;
    }
    return type == WCACHE_MEMORY ? EvictMemSatisfiedCond() : EvictDiskSatisfiedCond();
}

bool WCache::BeginEvictBatch(WCacheTierType type)
{
    if (type >= MAX_WCACHE_TIER || !GetState() || mStandaloneFault.load() || mIsForced ||
        mCacheTiers[type] == nullptr) {
        return false;
    }
    bool expected = false;
    if (!mEvictRef[type].compare_exchange_strong(expected, true)) {
        return false;
    }
    mEvictOnFlyRef[type].fetch_add(1);
    return true;
}

BResult WCache::EvictBatch(WCacheTierType type, uint32_t maxCount, uint32_t &evictedCount)
{
    evictedCount = 0;
    if (type >= MAX_WCACHE_TIER || mEvictOnFlyRef[type].load() == 0) {
        return BIO_INVALID_PARAM;
    }
    BatchGuard guard{mEvictRef[type], mEvictOnFlyRef[type]};
    BResult ret = BIO_INNER_RETRY;
    try {
        if (!mStandaloneFault.load() && IsOwnDiskNormal()) {
            ret = type == WCACHE_MEMORY ? EvictAllMemSliceToDisk(maxCount, &evictedCount) :
                EvictAllDiskSliceToUnderFs(maxCount, &evictedCount);
        }
    } catch (const std::bad_alloc &) {
        ret = BIO_ALLOC_FAIL;
    } catch (...) {
        LOG_ERROR("Global eviction batch threw, flowId:" << mFlowId << ", tier:" << type);
        ret = BIO_INNER_ERR;
    }
    return ret;
}

bool WCache::IsOwnDiskNormal()
{
    bool isNormal = false;
    if (IsWritable() || !BioServer::Instance()->IsStandaloneMode()) {
        mGetLocDiskStatus(mPtId, mDiskId, isNormal);
        return isNormal;
    }

    CmDiskStatus diskStatus = CM_DISK_FAULT;
    return !BioServer::Instance()->IsStandaloneDiskFault(mDiskId) &&
        BioServer::Instance()->GetDiskStatusFromNodeView(mDiskId, diskStatus) == BIO_OK &&
        diskStatus == CM_DISK_NORMAL;
}

uint64_t WCache::GetCapacity(WCacheTierType type)
{
    if (mCacheTiers[type] == nullptr) {
        return 0;
    }
    return mCacheTiers[type]->GetDataCapacity();
}

uint64_t WCache::GetVirCapacity(WCacheTierType type)
{
    if (mCacheTiers[type] == nullptr) {
        return 0;
    }
    return mCacheTiers[type]->GetDataVirCapacity();
}

uint64_t WCache::GetEvictOffset()
{
    if (!mHasDiskCache || mCacheTiers[WCACHE_DISK] == nullptr) {
        return 0;
    }
    return mCacheTiers[WCACHE_DISK]->GetDataEvictOffset();
}

BResult WCache::Recover(RecoverCallback recoverCallback)
{
    if (!mHasDiskCache) {
        return BIO_OK;
    }

    auto &diskCache = mCacheTiers[WCACHE_DISK];
    uint64_t truncateOffset = diskCache->GetMetaEvictOffset();
    uint64_t virCap = diskCache->GetMetaVirCapacity();
    WFlowSliceMeta sliceMeta;
    uint64_t sliceSize = sizeof(WFlowSliceMeta);
    uint64_t count = virCap / sliceSize;
    uint64_t startIndex = (truncateOffset + sliceSize - 1) / sliceSize;
    if ((truncateOffset % sliceSize + sliceSize * count) > virCap) {
        count--;
    }
    uint64_t dataRangeStart = diskCache->GetDataEvictOffset();
    uint64_t dataRangeEnd = dataRangeStart + diskCache->GetDataVirCapacity();
    for (uint64_t index = 0; index < count; index++) {
        uint64_t flowIndex = startIndex + index;
        WCacheSlicePtr metaSlice = nullptr;
        auto ret = diskCache->GetMetaSlice(flowIndex, metaSlice);
        ChkTrue(ret == BIO_OK, ret, "Failed to get meta slice:" << ret);

        ret = mSliceOperator.Copy(metaSlice.Get(), (char *)&sliceMeta, sizeof(WFlowSliceMeta));
        ChkTrueNot(ret == BIO_OK, ret);

        if (sliceMeta.magic != mFlowId) {
            diskCache->MarkEvictedIndex(flowIndex);
            continue;
        }

        if (sliceMeta.length == 0 || sliceMeta.length > UINT32_MAX || sliceMeta.offset < dataRangeStart ||
            sliceMeta.offset > dataRangeEnd || sliceMeta.length > dataRangeEnd - sliceMeta.offset) {
            diskCache->MarkEvictedIndex(flowIndex);
            continue;
        }

        SliceKey sliceKey(mFlowId, sliceMeta.offset, FLOW_DISK, sliceMeta.length, flowIndex);

        WCacheSlicePtr dataSlice = nullptr;
        ret = diskCache->GetDataSlice(sliceKey, dataSlice, true);
        ChkTrue(ret == BIO_OK, ret, "Failed to get data slice:" << ret);

        auto sliceRef = MakeRef<WCacheSliceRef>(dataSlice,
            sliceMeta.hasEvict == 0 ? SLICE_PENDING : SLICE_INVALID);
        ChkTrueNot(sliceRef != nullptr, BIO_ERR);

        // In standalone mode, KV cache data is evictable, so recovered slices must rejoin the disk eviction queue.
        if (mUfsEnable || BioServer::Instance()->IsStandaloneMode()) {
            diskCache->AddEvictQueue(sliceRef);
        }

        if (sliceMeta.hasEvict == 0) {
            ret = recoverCallback(mPtId, sliceMeta.key, sliceRef);
            if (ret != BIO_OK) {
                sliceRef->SetState(SLICE_INVALID);
            }
            ChkTrueNot(ret == BIO_OK, ret);
        }
    }

    return BIO_OK;
}

void WCache::Flush(const WCachePtr &self)
{
    BIO_TP_START(NO_PROCESS_WCACHE_FLUSH, 0);
    if (mStandaloneFault.load()) {
        return;
    }
    mIsForced = true;
    bool expectval = false;
    bool isSucceed = false;
    {
        if (mEvictRef[WCACHE_MEMORY].compare_exchange_weak(expectval, true)) {
            isSucceed = mEvictService[WCACHE_MEMORY]->Execute([self]() { self->FlushMem(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_MEMORY] = false;
            }
        }
    }

    if (mHasDiskCache && mUfsEnable) {
        if (mEvictRef[WCACHE_DISK].compare_exchange_weak(expectval, true)) {
            isSucceed = mEvictService[WCACHE_DISK]->Execute([self]() { self->FlushDisk(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_DISK] = false;
            }
        }
    }
    BIO_TP_END;
    return;
}

void WCache::ExpiredClear(const WCachePtr &self)
{
    BIO_TP_START(NO_PROCESS_WCACHE_EXPIRED_CLEAR, 0);
    if (mStandaloneFault.load()) {
        return;
    }
    mIsForced = true;
    bool expectval = false;
    bool isSucceed = false;
    {
        if (mEvictRef[WCACHE_MEMORY].compare_exchange_weak(expectval, true)) {
            isSucceed = mEvictService[WCACHE_MEMORY]->Execute([self]() { self->ExpiredClearMem(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_MEMORY] = false;
            }
        }
    }

    if (mHasDiskCache && mUfsEnable) {
        if (mEvictRef[WCACHE_DISK].compare_exchange_weak(expectval, true)) {
            isSucceed = mEvictService[WCACHE_DISK]->Execute([self]() { self->ExpiredClearDisk(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_DISK] = false;
            }
        }
    }
    BIO_TP_END;
}

BResult WCache::ForceClearMemoryTier()
{
    bool expected = false;
    if (!mEvictRef[WCACHE_MEMORY].compare_exchange_strong(expected, true)) {
        return BIO_INNER_RETRY;
    }
    mIsForced = true;
    return ExpiredClearMem();
}

BResult WCache::ReleaseFaultedResources()
{
    BResult ret = mCacheTiers[WCACHE_MEMORY] == nullptr ? BIO_OK :
        mCacheTiers[WCACHE_MEMORY]->ReleaseFaultedResources();
    if (mCacheTiers[WCACHE_DISK] != nullptr) {
        BResult diskRet = mCacheTiers[WCACHE_DISK]->ReleaseFaultedResources();
        if (ret == BIO_OK) {
            ret = diskRet;
        }
    }
    return ret;
}

void WCache::ProcAndCacheBrokenExpiredClear()
{
    BIO_TP_START(NO_PROCESS_WCACHE_EXPIRED_CLEAR, 0);
    mCacheTiers[WCACHE_MEMORY]->SetIsNormal(false);

    if (!IsEmptyEvict(WCACHE_MEMORY)) {
        StartEvictTask(WCACHE_MEMORY);
    } else if (mHasDiskCache && mUfsEnable && !IsEmptyEvict(WCACHE_DISK)) {
        StartEvictTask(WCACHE_DISK);
    }
    BIO_TP_END;
}

bool WCache::IsEmptyEvict(WCacheTierType type)
{
    if (mCacheTiers[type] == nullptr) {
        return true;
    }

    if (!IsIoFinish() || !IsEvictIoFinish()) {
        LOG_DEBUG("OnFly Put cnt:" << mOnFlyRef << ", memory evict cnt:" << mEvictOnFlyRef[WCACHE_MEMORY] <<
            ", disk evict cnt:" << mEvictOnFlyRef[WCACHE_DISK] << ", flowId:" << mFlowId);
        return false;
    }

    if (!mCacheTiers[type]->IsEmptyEvictSliceQueue() ||
        mEvictRef[type] == true) {
        LOG_TRACE("Evict slice queue status:" << !mCacheTiers[type]->IsEmptyEvictSliceQueue() <<
            ", type:" << type << ", flowId:" << mFlowId);
        LOG_TRACE("Evict task status:" << mEvictRef[type] <<
            ", type:" << type << ", flowId:" << mFlowId);
        return false;
    }

    return true;
}

BResult WCache::EvictFromMemToDiskImpl(WCacheSliceRefPtr sliceRef, bool isFront)
{
    auto slice = sliceRef->GetSlice();
    if (slice == nullptr) {
        LOG_ERROR("slice is null.");
        return BIO_INNER_ERR;
    }
    auto indexInFlow = slice->GetIndexInFlow();
    auto offset = slice->GetOffsetInFlow();
    auto length = slice->GetLength();

    BIO_TRACE_START(WCACHE_TRACE_EVICT2DISK_SUM);
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    WFlowMetaDataSlice memMetaDataSlice;
    BIO_TRACE_START(WCACHE_TRACE_ED_GETMETASLICE);
    auto ret = memCache->GetMetaDataSlice(indexInFlow, offset, length, memMetaDataSlice);
    BIO_TRACE_END(WCACHE_TRACE_ED_GETMETASLICE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        BIO_TRACE_END(WCACHE_TRACE_EVICT2DISK_SUM, ret);
        LOG_ERROR("Failed to get meta data slice from memory tier, ret:" << ret << "indexInFlow:" << indexInFlow <<
            ", offset:" << offset << ", length:" << length << ", flowId:" << mFlowId << ".");
        return ret;
    }

    auto &diskCache = mCacheTiers[WCACHE_DISK];
    WFlowMetaDataSlice diskMetaDataSlice;
    BIO_TP_START(WCACHE_GET_DISK_SLICE_FAIL, &ret, BIO_INNER_RETRY);
    BIO_TRACE_START(WCACHE_TRACE_ED_GETDATASLICE);
    ret = diskCache->GetMetaDataSlice(indexInFlow, offset, length, diskMetaDataSlice);
    BIO_TRACE_END(WCACHE_TRACE_ED_GETDATASLICE, ret);
    BIO_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        BIO_TRACE_END(WCACHE_TRACE_EVICT2DISK_SUM, ret);
        LOG_ERROR("Failed to get meta data slice from disk tier, ret:" << ret << "indexInFlow:" << indexInFlow <<
            ", offset:" << offset << ", length:" << length << ", flowId:" << mFlowId << ".");
        return ret;
    }

    BIO_TRACE_START(WCACHE_TRACE_ED_CPYMETASLICE);
    ret = mSliceOperator.Copy(memMetaDataSlice.dataSlice.Get(), diskMetaDataSlice.dataSlice.Get());
    BIO_TRACE_END(WCACHE_TRACE_ED_CPYMETASLICE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Slice copy failed, ret:" << ret << ".");
        return ret;
    }

    BIO_TRACE_START(WCACHE_TRACE_ED_CPYDATASLICE);
    ret = mSliceOperator.Copy(memMetaDataSlice.metaSlice.Get(), diskMetaDataSlice.metaSlice.Get());
    BIO_TRACE_END(WCACHE_TRACE_ED_CPYDATASLICE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Slice copy failed, ret:" << ret << ".");
        return ret;
    }

    LOG_DEBUG("Evict memory to disk, flowId:" << slice->GetFlowId() << ", indexInFlow:" << indexInFlow << ", offset:" <<
        offset << ", length:" << length << ", Glob:" << mFlowId << ", isFront:" << isFront);

    // when update slice finished, then release resource of flow.
    auto memoryMeta = memMetaDataSlice.metaSlice;
    auto diskMeta = diskMetaDataSlice.metaSlice;
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef, memoryMeta, diskMeta](const WCacheSlicePtr &oldSlice) {
        CompleteMemToDisk(sliceRef, oldSlice, memoryMeta, diskMeta);
        DecreaseRef();
    };
    IncreaseRef();

    BIO_TRACE_START(WCACHE_TRACE_ED_SETSLICE);
    diskMetaDataSlice.dataSlice->SetDataCrc(slice->GetDataCrc());
    sliceRef->SetSlice(diskMetaDataSlice.dataSlice, callback);
    BIO_TRACE_END(WCACHE_TRACE_ED_SETSLICE, BIO_OK);
    BIO_TRACE_END(WCACHE_TRACE_EVICT2DISK_SUM, BIO_OK);
    return BIO_OK;
}

void WCache::CompleteMemToDisk(const WCacheSliceRefPtr &sliceRef, const WCacheSlicePtr &oldSlice,
    const WCacheSlicePtr &memoryMeta, const WCacheSlicePtr &diskMeta)
{
    if (oldSlice == nullptr) {
        return;
    }
    if (!mStandaloneFault.load()) {
        BResult ret = BIO_OK;
        try {
            // All users of the old memory slice have released it before SetSlice invokes us.
            WFlowSliceMeta meta;
            ret = mSliceOperator.Copy(memoryMeta.Get(), reinterpret_cast<char *>(&meta), sizeof(meta));
            if (ret == BIO_OK && meta.hasEvict == 1) {
                // Never copy zero back: a newer Delete may already have marked the SSD record.
                ret = mSliceOperator.Copy(reinterpret_cast<const char *>(&meta), diskMeta.Get());
            }
        } catch (const std::bad_alloc &) {
            ret = BIO_ALLOC_FAIL;
        } catch (...) {
            ret = BIO_INNER_ERR;
        }
        if (ret != BIO_OK) {
            // Best effort for the cache workload: stale recovery metadata is tolerated on failure.
            LOG_WARN("Sync disk deletion marker failed, continue memory reclamation, flowId:" << mFlowId <<
                ", index:" << oldSlice->GetIndexInFlow() << ", ret:" << ret << ".");
        }
    }
    auto ret = mCacheTiers[WCACHE_MEMORY]->Evict(oldSlice);
    if (ret != BIO_OK) {
        LOG_WARN("Reclaim old memory slice failed, flowId:" << mFlowId <<
            ", index:" << oldSlice->GetIndexInFlow() << ", ret:" << ret << ".");
    }
    if (!mStandaloneFault.load()) {
        mCacheTiers[WCACHE_DISK]->AddEvictQueue(sliceRef);
        StartEvictTask(WCACHE_DISK);
    }
}

BResult WCache::EvictFromMemToDiscard(WCacheSliceRefPtr sliceRef, const UbsIoMetaEventBatchPtr &batch)
{
    if (sliceRef->GetState() == SLICE_PENDING) {
        return BIO_INNER_RETRY;
    }
    auto slice = sliceRef->GetSlice();
    if (slice == nullptr) {
        LOG_ERROR("slice is null.");
        return BIO_INNER_ERR;
    }

    WCacheSlicePtr metaSlice = nullptr;
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    auto ret = memCache->GetMetaSlice(slice->GetIndexInFlow(), metaSlice);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to get memory meta slice, flowId:" << slice->GetFlowId() << ", flowIndex:" <<
        slice->GetIndexInFlow() << ", flowOffset:" << slice->GetOffsetInFlow());

    std::shared_ptr<WFlowSliceMeta> sliceMeta = nullptr;
    try {
        sliceMeta = std::make_shared<WFlowSliceMeta>();
    } catch (const std::bad_alloc &e) {
        return BIO_ALLOC_FAIL;
    }
    ret = mSliceOperator.Copy(metaSlice.Get(), reinterpret_cast<char *>(sliceMeta.get()), sizeof(WFlowSliceMeta));
    ChkTrue(ret == BIO_OK, ret, "Slice copy failed, ret:" << ret << ".");

    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef, sliceMeta, batch](const WCacheSlicePtr &oldSlice) {
        if (oldSlice == nullptr) {
            LOG_ERROR("old slice is null.");
            DecreaseRef();
            return;
        }

        auto &memCache = mCacheTiers[WCACHE_MEMORY];
        BIO_TRACE_START(WCACHE_TRACE_DISCARD_EVICTSLICE);
        auto ret = memCache->Evict(oldSlice);
        BIO_TRACE_END(WCACHE_TRACE_DISCARD_EVICTSLICE, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Failed to discard memory slice, ret:" << ret << ", slice:" << oldSlice->ToString() << ".");
            DecreaseRef();
            return;
        }

        if (sliceRef->GetState() == SLICE_VALID) {
            mRecordMetaDeleteEventCallback(static_cast<uint16_t>(mPtId), sliceMeta->key, sliceRef, batch);
            sliceRef->SetState(SLICE_INVALID);
        }
        DecreaseRef();
    };

    sliceRef->SetSlice(nullptr, callback);
    return BIO_OK;
}

BResult WCache::EvictFromMemToUnderFs(WCacheSliceRefPtr sliceRef, const UbsIoMetaEventBatchPtr &batch)
{
    if (sliceRef->GetState() == SLICE_PENDING) {
        return BIO_INNER_RETRY;
    }
    auto dataSlice = sliceRef->GetSlice();
    if (UNLIKELY(dataSlice == nullptr)) {
        LOG_ERROR("Direct underfs eviction slice is null, flowId:" << mFlowId << ".");
        return BIO_INNER_ERR;
    }

    WCacheSlicePtr metaSlice = nullptr;
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    BResult ret = memCache->GetMetaSlice(dataSlice->GetIndexInFlow(), metaSlice);
    ChkTrue(ret == BIO_OK, ret, "Get compact wcache metadata failed, flowId:" << mFlowId << ", index:" <<
        dataSlice->GetIndexInFlow() << ".");

    WFlowCompactSliceMeta sliceMeta{};
    ret = mSliceOperator.Copy(metaSlice.Get(), reinterpret_cast<char *>(&sliceMeta), sizeof(sliceMeta));
    ChkTrue(ret == BIO_OK, ret, "Copy compact wcache metadata failed, flowId:" << mFlowId << ".");
    size_t keyLen = strnlen(sliceMeta.key, sizeof(sliceMeta.key));
    ChkTrue(keyLen > 0 && keyLen < sizeof(sliceMeta.key), BIO_INNER_ERR,
        "Invalid compact wcache key, flowId:" << mFlowId << ", keyLength:" << keyLen << ".");

    if (sliceRef->GetState() == SLICE_VALID) {
        ret = EvictToUnderFS(sliceMeta.key, dataSlice, dataSlice->GetLength());
        ChkTrue(ret == BIO_OK, ret, "Direct underfs eviction failed, flowId:" << mFlowId << ", index:" <<
            dataSlice->GetIndexInFlow() << ".");
    }

    std::string key;
    try {
        key.assign(sliceMeta.key, keyLen);
    } catch (const std::bad_alloc &) {
        return BIO_ALLOC_FAIL;
    }
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef, key, batch](const WCacheSlicePtr &oldSlice) {
        auto ret = mCacheTiers[WCACHE_MEMORY]->Evict(oldSlice);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Release direct underfs wcache slice failed, ret:" << ret << ", flowId:" << mFlowId << ".");
            DecreaseRef();
            return;
        }
        if (sliceRef->GetState() == SLICE_VALID) {
            mRecordMetaDeleteEventCallback(static_cast<uint16_t>(mPtId), const_cast<char *>(key.c_str()), sliceRef,
                batch);
            sliceRef->SetState(SLICE_INVALID);
        }
        DecreaseRef();
    };
    sliceRef->SetSlice(nullptr, callback);
    return BIO_OK;
}

BResult WCache::EvictToUnderFS(const char *key, WCacheSlicePtr &slice, const size_t length)
{
    BResult ret = BIO_INNER_ERR;
    std::vector<FlowAddr> addrVec = slice->GetAddrs();
    if (LIKELY(addrVec.size() == 1)) {
        ret = mUnderFs->Put(key, reinterpret_cast<char *>(addrVec[0].chunkId + addrVec[0].chunkOffset), length);
    } else {
        void *value = nullptr;
        int32_t allocRet = posix_memalign(&value, NO_4096, length);
        ChkTrue(allocRet == 0 && value != nullptr, BIO_ALLOC_FAIL,
            "Alloc aligned memory failed, ret:" << allocRet << ", length:" << length << ".");
        ret = mSliceOperator.Copy(slice.Get(), reinterpret_cast<char *>(value), length);
        if (UNLIKELY(ret != BIO_OK)) {
            free(value);
            LOG_ERROR("failed to copy slice to value. ret:" << ret << ", slice:" << slice->ToString());

            return ret;
        }
        ret = mUnderFs->Put(key, reinterpret_cast<char *>(value), length);
        free(value);
    }
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to put data slice to underFs, ret:" << ret <<", key:" << key << ", length:" << length << ".");
    }
    return ret;
}

BResult WCache::EvictFromDiskToUnderFsImpl(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront,
    const UbsIoMetaEventBatchPtr &batch)
{
    if (sliceRef->GetState() == SLICE_PENDING) {
        return BIO_INNER_RETRY;
    }
    // 1. 获取待淘汰对象的data slice和meta slice.
    auto &diskCache = mCacheTiers[WCACHE_DISK];
    auto dataSlice = sliceRef->GetSlice();
    if (dataSlice == nullptr) {
        LOG_ERROR("slice is null.");
        return BIO_INNER_ERR;
    }
    WCacheSlicePtr metaSlice = nullptr;
    auto ret = diskCache->GetMetaSlice(dataSlice->GetIndexInFlow(), metaSlice);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to to evict from disk to underfs, flowId:" << dataSlice->GetFlowId() << ", index:" <<
        dataSlice->GetIndexInFlow() << ", offset:" << dataSlice->GetOffsetInFlow());

    LOG_DEBUG("Evict flowId:" << dataSlice->GetFlowId() << ", index:" << dataSlice->GetIndexInFlow() << ", offset:" <<
        dataSlice->GetOffsetInFlow() << ", Glob:" << mFlowId << ", isFront:" << isFront);

    // 2. 读取Slice的元数据并校验元数据有效性.
    std::shared_ptr<WFlowSliceMeta> sliceMeta = nullptr;
    try {
        sliceMeta = std::make_shared<WFlowSliceMeta>();
    } catch (const std::bad_alloc& e) {
        return BIO_ALLOC_FAIL;
    }
    ret = mSliceOperator.Copy(metaSlice.Get(), (char *)sliceMeta.get(), sizeof(WFlowSliceMeta));
    ChkTrue(ret == BIO_OK, ret, "Slice copy failed, ret:" << ret << ".");
    ChkTrue(sliceMeta->length == dataSlice->GetLength(), BIO_INNER_ERR, "Check data slice length failed.");

    // 3. 根据Slice的状态决定是否执行数据淘汰.
    if (sliceRef->GetState() != SLICE_INVALID && mUfsEnable) {
        auto &key = sliceMeta->key;
        bool isFromRCache = true;
        WCacheSlicePtr rcWriteSlice = nullptr;
        // 3.1. 申请资源, 首先尝试从RCache中申请，失败则申请系统资源.
        ret = AllocRCacheResource(dataSlice, rcWriteSlice, isFromRCache);
        ChkTrue(ret == BIO_OK, BIO_ALLOC_FAIL, "Alloc rcache resource failed, ret:" << ret << ", key:" << key << ".");

        // 3.2. 将数据写到underFS中, 不处理异常防止RCache出现空洞.
        BIO_TRACE_START(WCACHE_TRACE_EVICT2UNDERFS);
        ret = EvictToUnderFS(key, rcWriteSlice, sliceMeta->length);
        BIO_TRACE_END(WCACHE_TRACE_EVICT2UNDERFS, ret);

        // 3.3 根据资源来历决定是否将数据写到RCache中, 最后释放资源.
        EvictToRCache(dataSlice, key, rcWriteSlice, isFromRCache);
        FreeRCacheResource(isFromRCache, rcWriteSlice);
        // The head remains indexed after early publication; a failed UFS write must keep it retryable.
        ChkTrueNot(ret == BIO_OK, ret);
    }

    // 4. 释放WCache的FLOW资源.
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef, sliceMeta, batch](const WCacheSlicePtr &oldSlice) {
        if (mStandaloneFault.load()) {
            if (sliceRef->GetState() == SLICE_VALID) {
                mRecordMetaDeleteEventCallback(mPtId, sliceMeta->key, sliceRef, batch);
                sliceRef->SetState(SLICE_INVALID);
            }
            DecreaseRef();
            return;
        }
        if (oldSlice == nullptr) {
            DecreaseRef();
            return;
        }
        auto &diskCache = mCacheTiers[WCACHE_DISK];
        auto ret = diskCache->Evict(oldSlice);
        if (UNLIKELY(ret != BIO_OK)) {
            DecreaseRef();
            LOG_ERROR("Failed to evict old slice, ret:" << ret << ", slice:" << oldSlice->ToString() << ".");
            return;
        }
        if (sliceRef->GetState() == SLICE_VALID) {
            uint16_t ptId = CacheFlowIdManager::GetPtId(oldSlice->GetFlowId());
            mRecordMetaDeleteEventCallback(ptId, sliceMeta->key, sliceRef, batch);
            sliceRef->SetState(SLICE_INVALID);
        }
        DecreaseRef();
    };

    sliceMeta->hasEvict = 1;
    ret = mSliceOperator.Copy((char *)sliceMeta.get(), metaSlice.Get());
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Slice copy failed, ret:" << ret << ".");
        DecreaseRef();
    } else {
        sliceRef->SetSlice(nullptr, callback);
    }
    return ret;
}

BResult WCache::EvictFromMemToDisk(WCacheSliceRefPtr sliceRef, bool isFront, const UbsIoMetaEventBatchPtr &batch)
{
    if (!sliceRef->OpLock()) {
        return BIO_INNER_RETRY;
    }
    SliceOpGuard guard{sliceRef};
    BResult ret = BIO_OK;
    if (mHasDiskCache) {
        ret = EvictFromMemToDiskImpl(sliceRef, isFront);
    } else if (mDirectUnderFs) {
        ret = EvictFromMemToUnderFs(sliceRef, batch);
    } else {
        ret = EvictFromMemToDiscard(sliceRef, batch);
    }
    return ret;
}

BResult WCache::EvictFromDiskToUnderFs(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront,
    const UbsIoMetaEventBatchPtr &batch)
{
    if (!sliceRef->OpLock()) {
        return BIO_INNER_RETRY;
    }
    SliceOpGuard guard{sliceRef};
    BResult ret = EvictFromDiskToUnderFsImpl(sliceRef, isMaster, isFront, batch);
    return ret;
}

BResult WCache::AllocRCacheResource(const WCacheSlicePtr &srcSlice, WCacheSlicePtr &dstSlice, bool &isRCache)
{
    BResult ret = BIO_INNER_ERR;
    uint16_t ptId = CacheFlowIdManager::GetPtId(srcSlice->GetFlowId());
    void *memAddr = nullptr;
    BIO_TP_START(NO_PROCESS_RESOURCE_ENOUGH, 0);
    bool enoughResource = (mRCacheManager != nullptr) && mRCacheManager->IsResourceEnough(ptId);
    if (enoughResource) {
        mRCacheManager->AllocResources(ptId, srcSlice->GetLength(), dstSlice);
    }
    BIO_TP_END;

    if (UNLIKELY(dstSlice == nullptr)) {
        memAddr = malloc(srcSlice->GetLength());
        ChkTrue(memAddr != nullptr, BIO_ALLOC_FAIL, "Alloc aligned memory failed, length:" <<
            srcSlice->GetLength() << ".");
        isRCache = false;
        MrInfo mrInfo = { reinterpret_cast<uint64_t>(memAddr), srcSlice->GetLength() };
        std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
        dstSlice = MakeRef<WCacheSlice>(0, 0, 0, srcSlice->GetLength(), addrVec, FLOW_MEMORY);
    }

    BIO_TP_START(ALLOC_DEST_SLICE_NULL, &dstSlice, nullptr);
    BIO_TP_END;
    if (LIKELY(dstSlice != nullptr)) {
        ret = mSliceOperator.Copy(srcSlice.Get(), dstSlice.Get());
        if (UNLIKELY(ret != BIO_OK)) {
            if (memAddr != nullptr) {
                free(memAddr);
            }
            LOG_WARN("Slice copy failed, ret:" << ret << ", slice:" << srcSlice->ToString() << ".");
            return ret;
        }
    } else {
        if (memAddr != nullptr) {
            free(memAddr);
        }
        return BIO_ALLOC_FAIL;
    }

    if (BioConfig::Instance()->GetDaemonConfig().enableCrc) {
        ret = dstSlice->VerifyDataCrc(srcSlice->GetDataCrc(), 0, dstSlice->GetLength(), dstSlice.Get());
        if (ret != BIO_OK) {
            LOG_ERROR("Evict to rcache verify the crc failed, ret: "<< ret << ".");
            if (memAddr != nullptr) {
                free(memAddr);
            }
        }
    }
    return ret;
}

void WCache::FreeRCacheResource(bool &isRCache, WCacheSlicePtr &slice)
{
    if (!isRCache) {
        free(reinterpret_cast<char *>(slice->GetAddrs()[0].chunkId));
    }
}

void WCache::EvictToRCache(const WCacheSlicePtr &srcSlice, const Key &key, WCacheSlicePtr &slice, bool &isRCache)
{
    if (UNLIKELY(!isRCache || mRCacheManager == nullptr)) {
        return;
    }
    uint64_t ptId = CacheFlowIdManager::GetPtId(srcSlice->GetFlowId());
    BIO_TRACE_START(WCACHE_TRACE_PUT_RCACHE);
    auto ret = mRCacheManager->Put(ptId, key, slice);
    BIO_TRACE_END(WCACHE_TRACE_PUT_RCACHE, ret);
    ChkTrueVoid(ret == BIO_OK, "Failed to put slice to rcache, ptId:" << ptId << " key:" << key << ".");
}

bool WCache::EvictMemSatisfiedCond()
{
    auto config = BioConfig::Instance()->GetDaemonConfig();

    uint64_t wcacheMemCap = (static_cast<uint64_t>(config.memWriteRatio) * config.memCap) / NO_10;
    uint64_t wcacheMemWaterSize = wcacheMemCap * config.wcacheMemEvictLevel / NO_100;
    uint64_t wcacheMemUsed = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_MEMORY, 0);
    bool memOverWater = wcacheMemUsed > wcacheMemWaterSize;
    if (!mHasDiskCache) {
        return memOverWater;
    }

    uint64_t diskCap = static_cast<uint64_t>(config.diskCaps[mDiskId]);
    uint64_t wcacheDiskCap = diskCap * static_cast<uint64_t>(config.diskWriteRatio) / NO_10;
    uint64_t wcacheDiskUsed = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, mDiskId);
    if (memOverWater && (wcacheDiskUsed < wcacheDiskCap)) {
        return true;
    } else {
        return false;
    }
}

bool WCache::EvictDiskSatisfiedCond()
{
    if (!mHasDiskCache) {
        return false;
    }

    auto config = BioConfig::Instance()->GetDaemonConfig();
    uint64_t diskCap = static_cast<uint64_t>(config.diskCaps[mDiskId]);

    uint64_t wcacheDiskCap = diskCap * static_cast<uint64_t>(config.diskWriteRatio) / NO_10;
    uint64_t wcacheDiskWaterSize = wcacheDiskCap * config.wcacheDiskEvictLevel / NO_100;
    uint64_t wcacheDiskUsed = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, mDiskId);
    if (wcacheDiskUsed > wcacheDiskWaterSize) {
        return true;
    } else {
        return false;
    }
}

BResult WCache::EvictMemoryTombstone(WCacheSliceRefPtr &sliceRef)
{
    if (!sliceRef->OpLock()) {
        return BIO_INNER_RETRY;
    }
    SliceOpGuard guard{sliceRef};
    auto slice = sliceRef->GetSlice();
    if (slice == nullptr) {
        return BIO_INNER_ERR;
    }

    if (mHasDiskCache) {
        auto ret = CreateDiskTombstone(slice);
        if (UNLIKELY(ret != BIO_OK)) {
            LogTombstoneFailure("Create disk tombstone failed", ret, mFlowId, slice->GetIndexInFlow());
            return ret;
        }
    }

    // A Delete may invalidate a published object while a Get still reads its memory.
    WCacheSliceRef::SetSliceCallback callback = [this](const WCacheSlicePtr &oldSlice) {
        auto ret = mCacheTiers[WCACHE_MEMORY]->Evict(oldSlice);
        if (ret != BIO_OK) {
            LOG_WARN("Reclaim memory tombstone failed, flowId:" << mFlowId << ", ret:" << ret << ".");
        }
        DecreaseRef();
    };
    IncreaseRef();
    sliceRef->SetSlice(nullptr, callback);
    return BIO_OK;
}

BResult WCache::CreateDiskTombstone(const WCacheSlicePtr &slice)
{
    if (slice == nullptr) {
        LogTombstoneFailure("Create disk tombstone with null slice", BIO_INVALID_PARAM, mFlowId, 0);
        return BIO_INVALID_PARAM;
    }
    auto &diskCache = mCacheTiers[WCACHE_DISK];
    if (diskCache == nullptr) {
        LogTombstoneFailure("Create disk tombstone with null tier", BIO_ERR, mFlowId, 0);
        return BIO_ERR;
    }

    uint64_t index = slice->GetIndexInFlow();
    if (index != 0 && UINT64_MAX / index < sizeof(WFlowSliceMeta)) {
        LogTombstoneFailure("Disk tombstone index overflow", BIO_INNER_RETRY, mFlowId, index);
        return BIO_INNER_RETRY;
    }
    uint64_t offset = slice->GetOffsetInFlow();
    uint64_t length = slice->GetLength();
    WFlowMetaDataSlice metaDataSlice;
    auto ret = diskCache->GetMetaDataSlice(index, offset, length, metaDataSlice);
    if (UNLIKELY(ret != BIO_OK)) {
        LogTombstoneFailure("Allocate disk tombstone slice failed", ret, mFlowId, index);
        return ret;
    }

    // Persist an already-evicted marker so recovery rebuilds the same INVALID
    // slice at the same index instead of seeing a gap in the disk flow.
    WFlowSliceMeta tombMeta{};
    tombMeta.magic = mFlowId;
    tombMeta.offset = offset;
    tombMeta.length = length;
    tombMeta.hasEvict = 1;
    ret = mSliceOperator.Copy(reinterpret_cast<const char *>(&tombMeta), metaDataSlice.metaSlice.Get());
    if (UNLIKELY(ret != BIO_OK)) {
        LogTombstoneFailure("Write disk tombstone meta failed", ret, mFlowId, index);
        return ret;
    }

    WCacheSliceRefPtr tombstoneRef = MakeRef<WCacheSliceRef>(metaDataSlice.dataSlice);
    if (UNLIKELY(tombstoneRef == nullptr)) {
        LogTombstoneFailure("Make disk tombstone ref failed", BIO_ALLOC_FAIL, mFlowId, index);
        return BIO_ALLOC_FAIL;
    }
    tombstoneRef->SetState(SLICE_INVALID);
    diskCache->AddEvictQueue(tombstoneRef);
    StartEvictTask(WCACHE_DISK);
    LOG_DEBUG("Create disk tombstone success, flowId:" << mFlowId << ", index:" << index << ", offset:" << offset <<
        ", length:" << length << ".");
    return BIO_OK;
}

BResult WCache::EvictAllMemSliceToDisk(uint32_t maxCount, uint32_t *evictedCount)
{
    if (!mHasDiskCache) {
        return EvictAllMemSliceWithoutDisk(maxCount, evictedCount);
    }

    uint32_t processed = 0;
    bool isSatisfied = EvictMemSatisfiedCond();
    while ((isSatisfied || (evictedCount == nullptr && mIsForced)) && processed < maxCount &&
        !mStandaloneFault.load()) {
        WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
        if (sliceRef == nullptr) {
            break;
        }
        CacheOverloadCtrl::Instance().AddBandwidth(BW_STAT_EVICT_TO_DISK, sliceRef->GetSlice()->GetLength());
        auto ret = (sliceRef->GetState() == SLICE_INVALID) ? EvictMemoryTombstone(sliceRef) :
            EvictFromMemToDisk(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            LOG_WARN("Evict all mem slice memory, need delayed internal retry, flowId:" <<
                sliceRef->GetSlice()->GetFlowId() << ", IndexInFlow:" << sliceRef->GetSlice()->GetIndexInFlow());
            mRetryCallback(mFlowId, WCACHE_MEMORY);
            return ret;
        }
        ++processed;
        if (evictedCount != nullptr) {
            *evictedCount = processed;
        }
        isSatisfied = EvictMemSatisfiedCond();
    }

    if (evictedCount == nullptr) {
        mEvictRef[WCACHE_MEMORY].store(false);
    }
    return BIO_OK;
}

BResult WCache::EvictAllMemSliceWithoutDisk(uint32_t maxCount, uint32_t *evictedCount)
{
    uint32_t processed = 0;
    bool isSatisfied = EvictMemSatisfiedCond();
    auto metaEventBatch = std::make_shared<UbsIoMetaEventBatch>();
    ChkTrueNot(metaEventBatch != nullptr, BIO_ALLOC_FAIL);
    while ((isSatisfied || (evictedCount == nullptr && mIsForced)) && processed < maxCount &&
        !mStandaloneFault.load()) {
        WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
        if (sliceRef == nullptr) {
            break;
        }
        auto slice = sliceRef->GetSlice();
        ++processed;
        if (slice == nullptr) {
            if (evictedCount != nullptr) {
                ++*evictedCount;
            }
            continue;
        }
        if (sliceRef->GetState() == SLICE_INVALID) {
            auto tombRet = EvictMemoryTombstone(sliceRef);
            if (tombRet != BIO_OK) {
                mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
                mRetryCallback(mFlowId, WCACHE_MEMORY);
                if (mSubmitMetaEventBatchCallback != nullptr) {
                    mSubmitMetaEventBatchCallback(metaEventBatch);
                }
                return tombRet;
            }
            if (evictedCount != nullptr) {
                ++*evictedCount;
            }
            isSatisfied = EvictMemSatisfiedCond();
            continue;
        }
        auto ret = EvictFromMemToDisk(sliceRef, false, metaEventBatch);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            LOG_WARN("Evict memory slice without disk failed, need delayed internal retry, flowId:" <<
                slice->GetFlowId() << ", IndexInFlow:" << slice->GetIndexInFlow() << ".");
            mRetryCallback(mFlowId, WCACHE_MEMORY);
            if (mSubmitMetaEventBatchCallback != nullptr) {
                mSubmitMetaEventBatchCallback(metaEventBatch);
            }
            return ret;
        }
        if (evictedCount != nullptr) {
            ++*evictedCount;
        }
        isSatisfied = EvictMemSatisfiedCond();
    }

    if (mSubmitMetaEventBatchCallback != nullptr) {
        mSubmitMetaEventBatchCallback(metaEventBatch);
    }
    if (evictedCount == nullptr) {
        mEvictRef[WCACHE_MEMORY].store(false);
    }
    return BIO_OK;
}

BResult WCache::EvictAllDiskSliceToUnderFs(uint32_t maxCount, uint32_t *evictedCount)
{
    if (!mHasDiskCache) {
        if (evictedCount == nullptr) {
            mEvictRef[WCACHE_DISK].store(false);
        }
        return BIO_OK;
    }

    bool isMaster;
    auto ret = mLocRole(static_cast<uint16_t>(mPtId), isMaster);
    ChkTrue(ret == BIO_OK, ret, "Get local role fail:" << ret << ", ptId:" << mPtId);

    bool isSatisfied = false;
    BIO_TP_START(WCACHE_CHECK_RCACHE_LEVEL_FAIL, &isSatisfied, false);
    isSatisfied = EvictDiskSatisfiedCond();
    BIO_TP_END;
    if (!isSatisfied && (evictedCount != nullptr || !mIsForced)) {
        if (evictedCount == nullptr) {
            mRetryCallback(mFlowId, WCACHE_DISK);
        }
        return BIO_OK;
    }

    auto metaEventBatch = std::make_shared<UbsIoMetaEventBatch>();
    ChkTrueNot(metaEventBatch != nullptr, BIO_ALLOC_FAIL);

    uint64_t globEvictOffset = NO_MAX_VALUE64;
    if (!isMaster && !mIsForced) {
        BIO_TP_START(WCACHE_GET_EVICT_OFFSET_FAIL, &ret, BIO_INNER_RETRY);
        ret = mGlobEvictOffset(static_cast<uint16_t>(mPtId), mFlowId, globEvictOffset);
        BIO_TP_END;
        if ((ret != BIO_OK) && (ret != BIO_NOT_EXISTS)) {
            LOG_WARN("Get evict offset fail:" << ret << ", ptId:" << mPtId << ", flowId:" << mFlowId);
            mRetryCallback(mFlowId, WCACHE_DISK);
            return ret;
        }
    }

    uint32_t processed = 0;
    while ((isSatisfied || (evictedCount == nullptr && mIsForced)) && processed < maxCount &&
        !mStandaloneFault.load()) {
        WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
        if (sliceRef == nullptr) {
            break;
        }
        auto slice = sliceRef->GetSlice();
        ++processed;
        if (slice == nullptr) {
            if (evictedCount != nullptr) {
                ++*evictedCount;
            }
            continue;
        }
        if (sliceRef->GetState() == SLICE_INVALID) {
            // Tombstone: skip disk read and UFS write, but keep the index
            // contiguous so the disk flow truncation can advance normally.
            ret = mCacheTiers[WCACHE_DISK]->Evict(slice);
            if (ret != BIO_OK) {
                mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
                mRetryCallback(mFlowId, WCACHE_DISK);
                if (mSubmitMetaEventBatchCallback != nullptr) {
                    mSubmitMetaEventBatchCallback(metaEventBatch);
                }
                return ret;
            }
            if (evictedCount != nullptr) {
                ++*evictedCount;
            }
            isSatisfied = EvictDiskSatisfiedCond();
            continue;
        }
        uint64_t sliceEvictOffset = slice->GetOffsetInFlow() + slice->GetLength();
        if (globEvictOffset < sliceEvictOffset) {
            mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
            mRetryCallback(mFlowId, WCACHE_DISK);
            if (mSubmitMetaEventBatchCallback != nullptr) {
                mSubmitMetaEventBatchCallback(metaEventBatch);
            }
            return BIO_OK;
        }
        auto ret = EvictFromDiskToUnderFs(sliceRef, isMaster, false, metaEventBatch);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
            mRetryCallback(mFlowId, WCACHE_DISK);
            if (mSubmitMetaEventBatchCallback != nullptr) {
                mSubmitMetaEventBatchCallback(metaEventBatch);
            }
            return ret;
        }
        if (evictedCount != nullptr) {
            ++*evictedCount;
        }
        isSatisfied = EvictDiskSatisfiedCond();
    }

    if (mSubmitMetaEventBatchCallback != nullptr) {
        mSubmitMetaEventBatchCallback(metaEventBatch);
    }
    if (evictedCount == nullptr) {
        mEvictRef[WCACHE_DISK].store(false);
    }
    return BIO_OK;
}

BResult WCache::FlushMem()
{
    LOG_TRACE("Flush mem, flowId:" << mFlowId);
    mCacheTiers[WCACHE_MEMORY]->SetIsNormal(false);
    auto metaEventBatch = std::make_shared<UbsIoMetaEventBatch>();
    ChkTrueNot(metaEventBatch != nullptr, BIO_ALLOC_FAIL);
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    while (sliceRef != nullptr) {
        LOG_DEBUG("Expired clear memory, flowId:" << sliceRef->GetSlice()->GetFlowId() << ", IndexInFlow:" <<
            sliceRef->GetSlice()->GetIndexInFlow());
        auto ret = (sliceRef->GetState() == SLICE_INVALID) ? EvictMemoryTombstone(sliceRef) :
            EvictFromMemToDisk(sliceRef, false, metaEventBatch);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            LOG_DEBUG("Flush memory fail, flowId:" << sliceRef->GetSlice()->GetFlowId() <<
                ", IndexInFlow:" << sliceRef->GetSlice()->GetIndexInFlow());
            mEvictRef[WCACHE_MEMORY] = false;
            if (mSubmitMetaEventBatchCallback != nullptr) {
                mSubmitMetaEventBatchCallback(metaEventBatch);
            }
            return ret;
        }
        sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    }

    if (mSubmitMetaEventBatchCallback != nullptr) {
        mSubmitMetaEventBatchCallback(metaEventBatch);
    }
    mEvictRef[WCACHE_MEMORY].store(false);
    return BIO_OK;
}

BResult WCache::FlushDisk()
{
    if (!mHasDiskCache || mCacheTiers[WCACHE_DISK] == nullptr) {
        mEvictRef[WCACHE_DISK].store(false);
        return BIO_OK;
    }

    LOG_TRACE("Flush disk, flowId:" << mFlowId);
    auto metaEventBatch = std::make_shared<UbsIoMetaEventBatch>();
    ChkTrueNot(metaEventBatch != nullptr, BIO_ALLOC_FAIL);
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
    while (sliceRef != nullptr) {
        auto ret = (sliceRef->GetState() == SLICE_INVALID) ? mCacheTiers[WCACHE_DISK]->Evict(sliceRef->GetSlice()) :
            EvictFromDiskToUnderFs(sliceRef, true, false, metaEventBatch);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
            mEvictRef[WCACHE_DISK] = false;
            if (mSubmitMetaEventBatchCallback != nullptr) {
                mSubmitMetaEventBatchCallback(metaEventBatch);
            }
            return ret;
        }
        sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
    }

    if (mSubmitMetaEventBatchCallback != nullptr) {
        mSubmitMetaEventBatchCallback(metaEventBatch);
    }
    mEvictRef[WCACHE_DISK].store(false);
    return BIO_OK;
}

BResult WCache::ExpiredClearMemImpl(WCacheSliceRefPtr sliceRef)
{
    if (sliceRef->GetState() == SLICE_PENDING) {
        return BIO_INNER_RETRY;
    }
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef](const WCacheSlicePtr &oldSlice) {
        if (oldSlice == nullptr) {
            LOG_ERROR("old slice is null.");
            DecreaseRef();
            return;
        }
        auto &memCache = mCacheTiers[WCACHE_MEMORY];
        auto ret = memCache->Evict(oldSlice);
        if (UNLIKELY(ret != BIO_OK)) {
            DecreaseRef();
            LOG_ERROR("failed to evict old slice." << ret << ", slice:" << oldSlice->ToString());
            return;
        }
        DecreaseRef();
    };

    sliceRef->SetSlice(nullptr, callback);
    return BIO_OK;
}

BResult WCache::ExpiredClearMem()
{
    mCacheTiers[WCACHE_MEMORY]->SetIsNormal(false);
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    while (sliceRef != nullptr) {
        LOG_DEBUG("Expired clear memory, flowId:" << sliceRef->GetSlice()->GetFlowId() << ", IndexInFlow:" <<
            sliceRef->GetSlice()->GetIndexInFlow());
        auto ret = ExpiredClearMemImpl(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            LOG_DEBUG("Expired clear memory fail, flowId:" << sliceRef->GetSlice()->GetFlowId() <<
                ", IndexInFlow:" << sliceRef->GetSlice()->GetIndexInFlow());
            mEvictRef[WCACHE_MEMORY] = false;
            return ret;
        }

        sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    }

    mEvictRef[WCACHE_MEMORY].store(false);
    return BIO_OK;
}

BResult WCache::ExpiredClearDiskImpl(WCacheSliceRefPtr sliceRef)
{
    if (sliceRef->GetState() == SLICE_PENDING) {
        return BIO_INNER_RETRY;
    }
    if (!mHasDiskCache || mCacheTiers[WCACHE_DISK] == nullptr) {
        return BIO_OK;
    }

    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef](const WCacheSlicePtr &oldSlice) {
        if (mStandaloneFault.load()) {
            sliceRef->SetState(SLICE_INVALID);
            DecreaseRef();
            return;
        }
        if (oldSlice == nullptr) {
            LOG_ERROR("old slice is null.");
            DecreaseRef();
            return;
        }
        auto &diskCache = mCacheTiers[WCACHE_DISK];
        auto ret = diskCache->Evict(oldSlice);
        if (UNLIKELY(ret != BIO_OK)) {
            DecreaseRef();
            LOG_ERROR("failed to evict old slice." << ret << ", slice:" << oldSlice->ToString());
            return;
        }
        DecreaseRef();
    };

    sliceRef->SetSlice(nullptr, callback);
    return BIO_OK;
}

BResult WCache::ExpiredClearDisk()
{
    if (!mHasDiskCache || mCacheTiers[WCACHE_DISK] == nullptr) {
        mEvictRef[WCACHE_DISK].store(false);
        return BIO_OK;
    }

    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
    while (sliceRef != nullptr) {
        auto ret = ExpiredClearDiskImpl(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
            mEvictRef[WCACHE_DISK] = false;
            return ret;
        }
        sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
    }

    mEvictRef[WCACHE_DISK].store(false);
    return BIO_OK;
}

BResult WCacheTier::ToFlowType(WCacheTierType tier, FlowType &flowType)
{
    switch (tier) {
        case WCACHE_MEMORY:
            flowType = FLOW_MEMORY;
            return BIO_OK;
        case WCACHE_DISK:
            flowType = FLOW_DISK;
            return BIO_OK;
        default:
            return BIO_ERR;
    }
}

uint64_t WCache::GetTruncateIndex()
{
    return mCacheTiers[WCACHE_MEMORY]->GetTruncateIndex();
}

}
}
