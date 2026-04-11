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

#include <utility>
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

BResult WCache::Init(const ExecutorServicePtr evictNegoService, const ExecutorServicePtr evictService[MAX_WCACHE_TIER],
    const RCacheManagerPtr rCacheManager, bool isRecover)
{
    BResult ret = BIO_INNER_ERR;
    for (int i = 0; i < MAX_WCACHE_TIER; ++i) {
        auto cacheTier = MakeRef<WCacheTier>();
        ChkTrue(cacheTier != nullptr, BIO_ALLOC_FAIL, "Make wcache tier failed.");

        ret = cacheTier->Init(static_cast<WCacheTierType>(i), mFlowId, mDiskId);
        ChkTrue(ret == BIO_OK, ret, "Failed to init cacheTier, WCacheTierType:" << i << " flowId:" << mFlowId);
        mCacheTiers[i] = cacheTier;
    }

    mEvictService[WCACHE_MEMORY] = evictService[WCACHE_MEMORY];
    mEvictService[WCACHE_DISK] = evictService[WCACHE_DISK];
    mEvictNegotiateService = evictNegoService;
    mEvictRef[WCACHE_MEMORY] = false;
    mEvictRef[WCACHE_DISK] = false;
    mOnFlyRef = 0;
    mOffset = 0;
    mIndex = 0;
    mRCacheManager = rCacheManager;
    mUfsEnable = BioConfig::Instance()->GetUnderFsConfig().underFsType != "none";
    mUnderFs = UfsHelper::Instance();
    if (isRecover) {
        mIsMaster = false; // 用于识别Put流程特殊处理
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
    mCopyNum = BioConfig::Instance()->GetCmConfig().copyNum;
    LOG_INFO("init wcache success, ptId:" << mPtId << ", flowId:" << mFlowId << ", isMaster:" << mIsMaster << ".");
    return BIO_OK;
}

void WCache::RegOp(GetLocDiskStatus getLocDiskStatus, CheckLocRole locRole, const GetGlobEvictOffset evictOffset,
    EvictCallback evictCallback, const RetryCallback retryCallback)
{
    mGetLocDiskStatus = getLocDiskStatus;
    mLocRole = locRole;
    mGlobEvictOffset = evictOffset;
    mEvictCallback = evictCallback;
    mRetryCallback = retryCallback;
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
    for (uint32_t diskId = 0; diskId < config.diskCaps.size(); diskId++) {
        diskCap += static_cast<uint64_t>(config.diskCaps[diskId]);
        diskUsed += FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, diskId);
    }
    diskCap = diskCap * static_cast<uint64_t>(config.diskWriteRatio) / NO_10;
}

BResult WCache::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    return memCache->GetDataSlice(sliceKey, slice);
}

BResult WCache::Put(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    IncFlyIo();
    auto ret = PutImpl(key, srcSlice, sliceReader, destSliceRef, attr);
    DecFlyIo();
    if (ret == BIO_OK) {
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

    // 2. put it to memory tier cache.
    BIO_TP_START(WRITE_SLICE_NULL_FAIL, &ret, BIO_INNER_RETRY);
    ret = mCacheTiers[WCACHE_MEMORY]->Write(key, srcSlice, sliceReader, destSliceRef);
    BIO_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Memory cache write failed.");
        return ret;
    }

    // 3. start evict slice.
    ret = StartEvictSlice(key, destSliceRef, attr);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Start evict slice failed, ret:" << ret << ", key:" << key << ".");
    }
    return ret;
}

BResult WCache::StartEvictSlice(const Key &key, WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    // 1. 计算IO写入策略.
    RealIoStrategy ioStrategy = WRITE_DEFAULT;
    PutSetIoStrategy(ioStrategy, attr);

    // 2. Add evict queue.
    mCacheTiers[WCACHE_MEMORY]->AddEvictQueue(destSliceRef);
    // 3. put it disk tier cache.
    if (ioStrategy <= WRITE_MEM_BACK) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_MEM_BACK);
        StartEvictTask(WCACHE_MEMORY);
        BIO_TRACE_END(WCACHE_TRACE_PUT_MEM_BACK, 0);
        return BIO_OK;
    }

    // 4. write thought
    BResult ret = BIO_INNER_ERR;
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    if (sliceRef != nullptr) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_DISK_BACK);
        ret = EvictFromMemToDisk(sliceRef, true);
        BIO_TRACE_END(WCACHE_TRACE_PUT_DISK_BACK, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            LOG_DEBUG("Put key, flowId:" << sliceRef->GetSlice()->GetFlowId() <<
                ", IndexInFlow:" << sliceRef->GetSlice()->GetIndexInFlow());
            return ret;
        }
    }

    // put it underfs tier.
    if (ioStrategy <= WRITE_DISK_BACK || mUfsEnable == false) {
        return BIO_OK;
    }
    sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
    if (sliceRef != nullptr) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_UNDERFS_BACK);
        ret = EvictFromDiskToUnderFs(sliceRef, mIsMaster, true);
        BIO_TRACE_END(WCACHE_TRACE_PUT_UNDERFS_BACK, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
            return ret;
        }
    }
    return BIO_OK;
}

void WCache::PutSetIoStrategy(RealIoStrategy &ioStrategy, CacheAttr &attr)
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
    uint64_t memConfig = (static_cast<uint64_t>(config.memWriteRatio) * config.memCap) / NO_10;
    uint64_t memUsed = BioServer::Instance()->GetNetEngine()->GetUsedBlockSize();
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
        return;
    }

    if (!isMemSatisfied && isDiskSatisfied) {
        attr.ioStrategy = WRITE_DISK_BACK;
        return;
    }

    if (mUfsEnable) {
        attr.ioStrategy = WRITE_UNDERFS_BACK;
    }
    return;
}

BResult WCache::PutByPass(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    if (!mIsMaster) {
        LOG_DEBUG("Degrade in standy node, key:" << key << " flowId:" << mFlowId);
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
    BResult ret = BIO_OK;

    ret = mCacheTiers[type]->Seal();
    if (ret != BIO_OK) {
        LOG_ERROR("Seal cacheTier fail:" << ret << ", type:" << type << ", flowId:" << mFlowId);
        return ret;
    }

    return BIO_OK;
}

void WCache::Destroy()
{
    mCacheTiers[WCACHE_MEMORY]->Destroy();
    mCacheTiers[WCACHE_DISK]->Destroy();
}

void WCache::StartEvictTask(WCacheTierType type)
{
    bool isNormal = false;
    mGetLocDiskStatus(mPtId, mDiskId, isNormal);
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

void WCache::RetryEvictTask(WCacheTierType type)
{
    if (mCacheTiers[type]->IsEmptyEvictSliceQueue()) {
        mEvictRef[type].store(false);
        return;
    }

    bool isNormal = false;
    mGetLocDiskStatus(mPtId, mDiskId, isNormal);
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

uint64_t WCache::GetCapacity(WCacheTierType type)
{
    return mCacheTiers[type]->GetDataCapacity();
}

uint64_t WCache::GetVirCapacity(WCacheTierType type)
{
    return mCacheTiers[type]->GetDataVirCapacity();
}

uint64_t WCache::GetEvictOffset()
{
    return mCacheTiers[WCACHE_DISK]->GetDataEvictOffset();
}

BResult WCache::Recover(RecoverCallback recoverCallback)
{
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
            continue;
        }

        if (sliceMeta.offset < dataRangeStart || sliceMeta.offset + sliceMeta.length > dataRangeEnd) {
            continue;
        }

        SliceKey sliceKey(mFlowId, sliceMeta.offset, FLOW_DISK, sliceMeta.length, flowIndex);

        WCacheSlicePtr dataSlice = nullptr;
        ret = diskCache->GetDataSlice(sliceKey, dataSlice);
        ChkTrue(ret == BIO_OK, ret, "Failed to get data slice:" << ret);

        auto sliceRef = MakeRef<WCacheSliceRef>(dataSlice);
        ChkTrueNot(sliceRef != nullptr, BIO_ERR);

        if (mUfsEnable) {
            diskCache->AddEvictQueue(sliceRef);
        }

        if (sliceMeta.hasEvict == 0) {
            ret = recoverCallback(mPtId, sliceMeta.key, sliceRef);
            ChkTrueNot(ret == BIO_OK, ret);
        } else {
            sliceRef->SetState(SLICE_INVALID);
        }
    }

    return BIO_OK;
}

void WCache::Flush(const WCachePtr &self)
{
    BIO_TP_START(NO_PROCESS_WCACHE_FLUSH, 0);
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

    if (mUfsEnable) {
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

    if (mUfsEnable) {
        if (mEvictRef[WCACHE_DISK].compare_exchange_weak(expectval, true)) {
            isSucceed = mEvictService[WCACHE_DISK]->Execute([self]() { self->ExpiredClearDisk(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_DISK] = false;
            }
        }
    }
    BIO_TP_END;
}

void WCache::ProcAndCacheBrokenExpiredClear()
{
    BIO_TP_START(NO_PROCESS_WCACHE_EXPIRED_CLEAR, 0);
    mCacheTiers[WCACHE_MEMORY]->SetIsNormal(false);

    if (!IsEmptyEvict(WCACHE_MEMORY)) {
        StartEvictTask(WCACHE_MEMORY);
    } else if (mUfsEnable && !IsEmptyEvict(WCACHE_DISK)) {
        StartEvictTask(WCACHE_DISK);
    }
    BIO_TP_END;
}

bool WCache::IsEmptyEvict(WCacheTierType type)
{
    if (mOnFlyRef != 0) {
        LOG_DEBUG("OnFly io cnt:" << mOnFlyRef << ", flowId:" << mFlowId);
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
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef](const WCacheSlicePtr &oldSlice) {
        auto &memCache = mCacheTiers[WCACHE_MEMORY];
        BIO_TRACE_START(WCACHE_TRACE_ED_EVICTSLICE);
        auto ret = memCache->Evict(oldSlice);
        if (mUfsEnable) {
            auto &diskCache = mCacheTiers[WCACHE_DISK];
            diskCache->AddEvictQueue(sliceRef);
            StartEvictTask(WCACHE_DISK);
        }
        BIO_TRACE_END(WCACHE_TRACE_ED_EVICTSLICE, BIO_OK);
        DecreaseRef();
        ChkTrueExNot(ret == BIO_OK);
    };

    BIO_TRACE_START(WCACHE_TRACE_ED_SETSLICE);
    diskMetaDataSlice.dataSlice->SetDataCrc(slice->GetDataCrc());
    sliceRef->SetSlice(diskMetaDataSlice.dataSlice, callback);
    BIO_TRACE_END(WCACHE_TRACE_ED_SETSLICE, BIO_OK);
    BIO_TRACE_END(WCACHE_TRACE_EVICT2DISK_SUM, BIO_OK);
    return BIO_OK;
}

BResult WCache::EvictToUnderFS(const char *key, WCacheSlicePtr &slice, const size_t length)
{
    BResult ret = BIO_INNER_ERR;
    std::vector<FlowAddr> addrVec = slice->GetAddrs();
    if (LIKELY(addrVec.size() == 1)) {
        ret = mUnderFs->Put(key, reinterpret_cast<char *>(addrVec[0].chunkId + addrVec[0].chunkOffset), length);
    } else {
        void *value = aligned_alloc(NO_4096, NO_4194304);
        ChkTrue(value != nullptr, BIO_ALLOC_FAIL, "Alloc memory aligned failed.");
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

BResult WCache::EvictFromDiskToUnderFsImpl(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront)
{
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
    if (sliceRef->GetState() == SLICE_VALID) {
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
    }

    // 4. 释放WCache的FLOW资源.
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef, sliceMeta](const WCacheSlicePtr &oldSlice) {
        auto &diskCache = mCacheTiers[WCACHE_DISK];
        auto ret = diskCache->Evict(oldSlice);
        if (UNLIKELY(ret != BIO_OK)) {
            DecreaseRef();
            LOG_ERROR("Failed to evict old slice, ret:" << ret << ", slice:" << oldSlice->ToString() << ".");
            return;
        }
        if (sliceRef->GetState() == SLICE_VALID) {
            uint16_t ptId = CacheFlowIdManager::GetPtId(oldSlice->GetFlowId());
            mEvictCallback(ptId, sliceMeta->key, sliceRef);
        }
        DecreaseRef();
    };

    sliceMeta->hasEvict = 1;
    ret = mSliceOperator.Copy((char *)sliceMeta.get(), metaSlice.Get());
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Slice copy failed, ret:" << ret << ".");
    } else {
        sliceRef->SetSlice(nullptr, callback);
    }
    return ret;
}

BResult WCache::EvictFromMemToDisk(WCacheSliceRefPtr sliceRef, bool isFront)
{
    if (!isFront && !sliceRef->OpLock()) {
        return BIO_INNER_RETRY;
    }
    BResult ret = EvictFromMemToDiskImpl(sliceRef, isFront);
    sliceRef->OpUnLock();
    return ret;
}

BResult WCache::EvictFromDiskToUnderFs(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront)
{
    if (!isFront && !sliceRef->OpLock()) {
        return BIO_INNER_RETRY;
    }
    BResult ret = EvictFromDiskToUnderFsImpl(sliceRef, isMaster, isFront);
    sliceRef->OpUnLock();
    return ret;
}

BResult WCache::AllocRCacheResource(const WCacheSlicePtr &srcSlice, WCacheSlicePtr &dstSlice, bool &isRCache)
{
    BResult ret = BIO_INNER_ERR;
    uint16_t ptId = CacheFlowIdManager::GetPtId(srcSlice->GetFlowId());
    void *memAddr = nullptr;
    BIO_TP_START(NO_PROCESS_RESOURCE_ENOUGH, 0);
    bool enoughResource = mRCacheManager->IsResourceEnough(ptId);
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
    if (UNLIKELY(!isRCache)) {
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
    uint64_t diskCap = static_cast<uint64_t>(config.diskCaps[mDiskId]);

    uint64_t wcacheMemCap = (static_cast<uint64_t>(config.memWriteRatio) * config.memCap) / NO_10;
    uint64_t wcacheMemWaterSize = wcacheMemCap * config.wcacheMemEvictLevel / NO_100;
    uint64_t wcacheMemUsed = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_MEMORY, 0);

    uint64_t wcacheDiskCap = diskCap * static_cast<uint64_t>(config.diskWriteRatio) / NO_10;
    uint64_t wcacheDiskUsed = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, mDiskId);
    if ((wcacheMemUsed > wcacheMemWaterSize) && (wcacheDiskUsed < wcacheDiskCap)) {
        return true;
    } else {
        return false;
    }
}

bool WCache::EvictDiskSatisfiedCond()
{
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

BResult WCache::EvictAllMemSliceToDisk()
{
    bool isSatisfied = EvictMemSatisfiedCond();
    while (isSatisfied || mIsForced) {
        WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
        if (sliceRef == nullptr) {
            break;
        }
        CacheOverloadCtrl::Instance().AddBandwidth(BW_STAT_EVICT_TO_DISK, sliceRef->GetSlice()->GetLength());
        auto ret = EvictFromMemToDisk(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            LOG_WARN("Evict all mem slice memory, need delayed internal retry, flowId:" <<
                sliceRef->GetSlice()->GetFlowId() << ", IndexInFlow:" << sliceRef->GetSlice()->GetIndexInFlow());
            mRetryCallback(mFlowId, WCACHE_MEMORY);
            return ret;
        }
        isSatisfied = EvictMemSatisfiedCond();
    }

    mEvictRef[WCACHE_MEMORY].store(false);
    return BIO_OK;
}

BResult WCache::EvictAllDiskSliceToUnderFs()
{
    bool isMaster;
    auto ret = mLocRole(static_cast<uint16_t>(mPtId), isMaster);
    ChkTrue(ret == BIO_OK, ret, "Get local role fail:" << ret << ", ptId:" << mPtId);

    bool isSatisfied = false;
    BIO_TP_START(WCACHE_CHECK_RCACHE_LEVEL_FAIL, &isSatisfied, false);
    isSatisfied = EvictDiskSatisfiedCond();
    BIO_TP_END;
    if (!isSatisfied && !mIsForced) {
        mRetryCallback(mFlowId, WCACHE_DISK);
        return BIO_OK;
    }

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

    while (isSatisfied || mIsForced) {
        WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
        if (sliceRef == nullptr) {
            break;
        }
        auto slice = sliceRef->GetSlice();
        if (slice == nullptr) {
            break;
        }
        uint64_t sliceEvictOffset = slice->GetOffsetInFlow() + slice->GetLength();
        if (globEvictOffset < sliceEvictOffset) {
            mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
            mRetryCallback(mFlowId, WCACHE_DISK);
            return BIO_OK;
        }
        auto ret = EvictFromDiskToUnderFs(sliceRef, isMaster);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_DISK]->RetryEvictQueue(sliceRef);
            mRetryCallback(mFlowId, WCACHE_DISK);
            return ret;
        }
        isSatisfied = EvictDiskSatisfiedCond();
    }

    mEvictRef[WCACHE_DISK].store(false);
    return BIO_OK;
}

BResult WCache::FlushMem()
{
    LOG_TRACE("Flush mem, flowId:" << mFlowId);
    mCacheTiers[WCACHE_MEMORY]->SetIsNormal(false);
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    while (sliceRef != nullptr) {
        LOG_DEBUG("Expired clear memory, flowId:" << sliceRef->GetSlice()->GetFlowId() << ", IndexInFlow:" <<
            sliceRef->GetSlice()->GetIndexInFlow());
        auto ret = EvictFromMemToDisk(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            LOG_DEBUG("Flush memory fail, flowId:" << sliceRef->GetSlice()->GetFlowId() <<
                ", IndexInFlow:" << sliceRef->GetSlice()->GetIndexInFlow());
            mEvictRef[WCACHE_MEMORY] = false;
            return ret;
        }
        sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    }

    mEvictRef[WCACHE_MEMORY].store(false);
    return BIO_OK;
}

BResult WCache::FlushDisk()
{
    LOG_TRACE("Flush disk, flowId:" << mFlowId);
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_DISK]->GetEvictSlice();
    while (sliceRef != nullptr) {
        auto ret = EvictFromDiskToUnderFs(sliceRef, true);
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

BResult WCache::ExpiredClearMemImpl(WCacheSliceRefPtr sliceRef)
{
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef](const WCacheSlicePtr &oldSlice) {
        if (oldSlice == nullptr) {
            LOG_ERROR("old slice is null.");
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
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef](const WCacheSlicePtr &oldSlice) {
        if (oldSlice == nullptr) {
            LOG_ERROR("old slice is null.");
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
