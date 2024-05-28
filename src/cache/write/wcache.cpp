/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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
#include "bio_server.h"

namespace ock {
namespace bio {
constexpr uint32_t EVICT_MEM_HLEVEL = 90;
constexpr uint32_t EVICT_DISK_HLEVEL = 98;

BResult WCache::Init(const ExecutorServicePtr evictService[MAX_WCACHE_TIER], const RCacheManagerPtr rCacheManager,
    bool isRecover)
{
    for (int i = 0; i < MAX_WCACHE_TIER; ++i) {
        auto cacheTier = MakeRef<WCacheTier>();
        LVOS_TP_START(WCACHE_TIER_ALLOC_FAIL, &cacheTier, nullptr);
        LVOS_TP_END;
        ChkTrueNot(cacheTier != nullptr, BIO_ALLOC_FAIL);
        LVOS_TP_START(WCACHE_TIER_TYPE_FAIL, &i, MAX_WCACHE_TIER);
        LVOS_TP_END;
        auto ret = cacheTier->Init(static_cast<WCacheTierType>(i), mFlowId, mDiskId);
        ChkTrue(ret == BIO_OK, ret, "Failed to init cacheTier, WCacheTierType:" << i << " flowId:" << mFlowId);
        mCacheTiers[i] = cacheTier;
    }

    mEvictService[WCACHE_MEMORY] = evictService[WCACHE_MEMORY];
    mEvictService[WCACHE_DISK] = evictService[WCACHE_DISK];
    mEvictRef[WCACHE_MEMORY] = false;
    mEvictRef[WCACHE_DISK] = false;
    mOnFlyRef = 0;
    mRCacheManager = rCacheManager;
    mUnderFs = UnderFs::Instance();

    if (isRecover) {
        mIsMaster = false; // 用于识别Put流程特殊处理
        return BIO_OK;
    }

    auto ret = mLocRole(static_cast<uint16_t>(mPtId), mIsMaster); // 创建时获取当时的副本主备，用于降级场景的PUT流程
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get role fail:" << ret << ", ptId:" << mPtId << " flowId:" << mFlowId);
        return ret;
    }

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
    mOnFlyRef += NO_1;
    auto ret = PutImpl(key, srcSlice, sliceReader, destSliceRef, attr);
    mOnFlyRef -= NO_1;
    return ret;
}

BResult WCache::PutImpl(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    BResult ret;

    // degraded write through to underfs
    if (UNLIKELY(mIsDegrade)) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_BYPASS);
        ret = PutByPass(key, srcSlice, sliceReader, destSliceRef, attr);
        BIO_TRACE_END(WCACHE_TRACE_PUT_BYPASS, ret);
        return ret;
    }

    // put it memory tier cache.
    LVOS_TP_START(WRITE_SLICE_NULL_FAIL, &ret, BIO_INNER_RETRY);
    ret = mCacheTiers[WCACHE_MEMORY]->Write(key, srcSlice, sliceReader, destSliceRef);
    LVOS_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Memory cache write failed.");
        return ret;
    }
    mCacheTiers[WCACHE_MEMORY]->AddEvictQueue(destSliceRef);

    RealIoStrategy ioStratege;
    PutSetIoStratege(ioStratege, attr);

    // put it disk tier cache.
    if (ioStratege <= WRITE_MEM_BACK) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_MEM_BACK);
        StartEvictTask(WCACHE_MEMORY); // write back
        BIO_TRACE_END(WCACHE_TRACE_PUT_MEM_BACK, 0);
        return BIO_OK;
    }
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    if (sliceRef != nullptr) {
        BIO_TRACE_START(WCACHE_TRACE_PUT_DISK_BACK);
        ret = EvictFromMemToDisk(sliceRef, true);
        BIO_TRACE_END(WCACHE_TRACE_PUT_DISK_BACK, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
            return ret;
        }
    }

    // put it underfs tier.
    if (ioStratege <= WRITE_DISK_BACK) {
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

void WCache::PutSetIoStratege(RealIoStrategy &ioStratege, CacheAttr &attr)
{
    ioStratege = attr.ioStratege;

    if (ioStratege == WRITE_DEFALUT) {
        if (attr.strategy == WRITE_BACK) {
            ioStratege = WRITE_MEM_BACK;
        } else {
            ioStratege = WRITE_DISK_BACK;
        }
    }

    auto config = BioConfig::Instance()->GetDaemonConfig();
    uint64_t memConfig = (static_cast<uint64_t>(config.memWriteRatio) * config.memCap) / NO_10;
    uint64_t memUsed = BioServer::Instance()->GetNetEngine()->GetUsedBlockSize();
    uint64_t memWcache = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_MEMORY, 0);
    uint64_t memRcache = FlowManager::GetCacheUsedSize(FLOW_RCACHE, FLOW_MEMORY, 0);

    LOG_DEBUG("Total mem:" << (config.memCap / NO_1MB) << ", used:" << (memUsed / NO_1MB) <<
        ", wcache:" << (memWcache / NO_1MB) << ", rcache:" << (memRcache / NO_1MB) << ", stratege:" << ioStratege);

    uint64_t diskConfig = (static_cast<uint64_t>(config.diskWriteRatio * config.diskCaps[mDiskId])) / NO_10;
    uint64_t diskWcache = FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, mDiskId);
    uint64_t diskRcache = FlowManager::GetCacheUsedSize(FLOW_RCACHE, FLOW_DISK, mDiskId);
    uint64_t diskUsed = diskWcache + diskRcache;

    LOG_DEBUG("Total disk:" << (config.diskCaps[mDiskId] / NO_1MB) << ", used:" << (diskUsed / NO_1MB) <<
        ", wcache:" << (diskWcache / NO_1MB) << ", rcache:" << (diskRcache / NO_1MB) << ", stratege:" <<
        ioStratege << ", diskId:" << mDiskId);

    bool isMemSatisfied = ((memUsed < (config.memCap * EVICT_MEM_HLEVEL / NO_100)) &&
        (memWcache < (memConfig * EVICT_MEM_HLEVEL / NO_100)));
    bool isDiskSatisfied = (diskWcache < (diskConfig * EVICT_DISK_HLEVEL / NO_100));

    if (isMemSatisfied && isDiskSatisfied && (attr.strategy == WRITE_BACK)) {
        attr.ioStratege = WRITE_MEM_BACK;
        return;
    }

    if (!isMemSatisfied && isDiskSatisfied) {
        attr.ioStratege = WRITE_DISK_BACK;
        return;
    }

    attr.ioStratege = WRITE_UNDERFS_BACK;
    return;
}

BResult WCache::PutByPass(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    if (!mIsMaster) {
        LOG_INFO("Degrade in standy node, key:" << key << " flowId:" << mFlowId);
        destSliceRef = nullptr;
        return BIO_OK;
    }

    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    auto ret = memCache->Write(key, srcSlice, sliceReader, destSliceRef);
    ChkTrueNot(destSliceRef != nullptr, BIO_INNER_ERR);

    auto *value = new char[srcSlice->GetLength()];
    ChkTrueNot(value != nullptr, BIO_ALLOC_FAIL);

    ret = mSliceOperator.Copy(destSliceRef->GetSlice().Get(), value);
    if (UNLIKELY(ret != BIO_OK)) {
        delete[] value;
        LOG_ERROR("Failed to copy slice to value, key:" << key << " flowId:" << mFlowId);
        return ret;
    }

    ret = mUnderFs->Put(key, value, srcSlice->GetLength());
    delete[] value;
    ChkTrue(ret == BIO_OK, ret, "Failed to put slice to underfs, key:" << key << " flowId:" << mFlowId);

    ret = memCache->Evict(destSliceRef->GetSlice());
    ChkTrue(ret == BIO_OK, ret, "Failed to evict, key:" << key << " flowId:" << mFlowId);

    destSliceRef = nullptr;
    return BIO_OK;
}

BResult WCache::Delete(const Key &key, const WCacheSliceRefPtr &sliceRef)
{
    auto slice = sliceRef->GetSlice();
    WCacheSlicePtr metaSlice = nullptr;
    LVOS_TP_START(WCACHE_FLOW_DISK_FAIL, slice->GetFlowType(), FLOW_DISK);
    LVOS_TP_END;
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

    LOG_INFO("Delete key:" << key << ", flowId:" << slice->GetFlowId() << ", flowIndex:" << slice->GetIndexInFlow() <<
        ", flowOffset:" << slice->GetOffsetInFlow());
    WFlowSliceMeta sliceMeta;
    auto ret = mSliceOperator.Copy(metaSlice.Get(), (char *)&sliceMeta);
    ChkTrue(ret == BIO_OK, ret, "Slice copy failed.");

    sliceMeta.hasEvict = 1;
    ret = mSliceOperator.Copy((char *)&sliceMeta, metaSlice.Get());
    ChkTrue(ret == BIO_OK, ret, "Slice copy failed.");
    return BIO_OK;
}

BResult WCache::Seal(WCacheTierType type)
{
    BResult ret;

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
    if (type == WCACHE_MEMORY) {
        isSucceed = mEvictService[type]->Execute([this]() { EvictAllMemSliceToDisk(); });
    } else {
        isSucceed = mEvictService[type]->Execute([this]() { EvictAllDiskSliceToUnderFs(); });
    }

    if (!isSucceed) {
        mEvictRef[type].store(false);
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
        LOG_WARN("Disk fault or Pt rebalance, no need, flowId:" << mFlowId);
        return;
    }

    bool isSucceed;
    if (type == WCACHE_MEMORY) {
        isSucceed = mEvictService[type]->Execute([this]() { EvictAllMemSliceToDisk(); });
    } else {
        isSucceed = mEvictService[type]->Execute([this]() { EvictAllDiskSliceToUnderFs(); });
    }

    if (!isSucceed) {
        mEvictRef[type].store(false);
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

        ret = mSliceOperator.Copy(metaSlice.Get(), (char *)&sliceMeta);
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

        diskCache->AddEvictQueue(sliceRef);

        if (sliceMeta.hasEvict == 0) {
            ret = recoverCallback(mPtId, sliceMeta.key, sliceRef);
            ChkTrueNot(ret == BIO_OK, ret);
        } else {
            sliceRef->SetState(SLICE_INVALID);
        }
    }

    return BIO_OK;
}

void WCache::Flush()
{
    LVOS_TP_START(NO_PROCESS_WCACHE_FLUSH, 0);
    mIsForced = true;

    {
        bool expectval = false;
        if (mEvictRef[WCACHE_MEMORY].compare_exchange_weak(expectval, true)) {
            bool isSucceed = mEvictService[WCACHE_MEMORY]->Execute([this]() { FlushMem(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_MEMORY] = false;
            }
        }
    }

    {
        bool expectval = false;
        if (mEvictRef[WCACHE_DISK].compare_exchange_weak(expectval, true)) {
            bool isSucceed = mEvictService[WCACHE_DISK]->Execute([this]() { FlushDisk(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_DISK] = false;
            }
        }
    }

    LVOS_TP_END;
    return;
}

void WCache::ExpiredClear()
{
    LVOS_TP_START(NO_PROCESS_WCACHE_EXPIRED_CLEAR, 0);
    mIsForced = true;

    {
        bool expectval = false;
        if (mEvictRef[WCACHE_MEMORY].compare_exchange_weak(expectval, true)) {
            bool isSucceed = mEvictService[WCACHE_MEMORY]->Execute([this]() { ExpiredClearMem(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_MEMORY] = false;
            }
        }
    }

    {
        bool expectval = false;
        if (mEvictRef[WCACHE_DISK].compare_exchange_weak(expectval, true)) {
            bool isSucceed = mEvictService[WCACHE_DISK]->Execute([this]() { ExpiredClearDisk(); });
            if (!isSucceed) {
                mEvictRef[WCACHE_DISK] = false;
            }
        }
    }
    LVOS_TP_END;
}

bool WCache::IsEmptyEvict(WCacheTierType type)
{
    if (mOnFlyRef != 0) {
        LOG_INFO("OnFly io cnt:" << mOnFlyRef << ", flowId:" << mFlowId);
        return false;
    }

    if (!mCacheTiers[type]->IsEmptyEvictSliceQueue() ||
        mEvictRef[type] == true) {
        LOG_DEBUG("Evict slice queue status:" << !mCacheTiers[type]->IsEmptyEvictSliceQueue() <<
            ", type:" << type << ", flowId:" << mFlowId);
        LOG_DEBUG("Evict task status:" << mEvictRef[type] <<
            ", type:" << type << ", flowId:" << mFlowId);
        return false;
    }

    return true;
}

BResult WCache::EvictFromMemToDisk(WCacheSliceRefPtr sliceRef, bool isFront)
{
    auto slice = sliceRef->GetSlice();
    auto indexInFlow = slice->GetIndexInFlow();
    auto offset = slice->GetOffsetInFlow();
    auto length = slice->GetLength();

    BIO_TRACE_START(WCACHE_TRACE_EVICT2DISK);

    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    WFlowMetaDataSlice memMetaDataSlice;
    auto ret = memCache->GetMetaDataSlice(indexInFlow, offset, length, memMetaDataSlice);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to get meta data slice in WCACHE_MEMORY, indexInFlow:" << indexInFlow << " offset:" << offset <<
        " length:" << length);

    auto &diskCache = mCacheTiers[WCACHE_DISK];
    WFlowMetaDataSlice diskMetaDataSlice;
    LVOS_TP_START(WCACHE_GET_DISK_SLICE_FAIL, &ret, BIO_INNER_RETRY);
    ret = diskCache->GetMetaDataSlice(indexInFlow, offset, length, diskMetaDataSlice);
    LVOS_TP_END;
    ChkTrueNot(ret == BIO_OK, ret);

    ret = mSliceOperator.Copy(memMetaDataSlice.dataSlice.Get(), diskMetaDataSlice.dataSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    ret = mSliceOperator.Copy(memMetaDataSlice.metaSlice.Get(), diskMetaDataSlice.metaSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    LOG_INFO("Evict memory to disk, flowId:" << slice->GetFlowId() << ", indexInFlow:" << indexInFlow << ", offset:" <<
        offset << ", length:" << length << ", Glob:" << mFlowId << ", isFront:" << isFront);

    // when update slice finished, then release resource of flow.
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef](const WCacheSlicePtr &oldSlice) {
        auto &memCache = mCacheTiers[WCACHE_MEMORY];
        auto ret = memCache->Evict(oldSlice);
        auto &diskCache = mCacheTiers[WCACHE_DISK];
        diskCache->AddEvictQueue(sliceRef);
        StartEvictTask(WCACHE_DISK);
        DecreaseRef();
        ChkTrueExNot(ret == BIO_OK);
    };

    sliceRef->SetSlice(diskMetaDataSlice.dataSlice, callback);
    BIO_TRACE_END(WCACHE_TRACE_EVICT2DISK, 0);
    return BIO_OK;
}

BResult WCache::EvictFromDiskToUnderFs(WCacheSliceRefPtr sliceRef, bool isMaster, bool isFront)
{
    auto &diskCache = mCacheTiers[WCACHE_DISK];
    auto slice = sliceRef->GetSlice();
    WCacheSlicePtr metaSlice = nullptr;
    auto ret = diskCache->GetMetaSlice(slice->GetIndexInFlow(), metaSlice);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to to evict from disk to underfs, flowId:" << slice->GetFlowId() << ", index:" <<
        slice->GetIndexInFlow() << ", offset:" << slice->GetOffsetInFlow());

    BIO_TRACE_START(WCACHE_TRACE_EVICT2UNDERFS);

    LOG_INFO("Evict flowId:" << slice->GetFlowId() << ", index:" << slice->GetIndexInFlow() << ", offset:" <<
        slice->GetOffsetInFlow() << ", Glob:" << mFlowId << ", isFront:" << isFront);

    auto sliceMeta = std::make_shared<WFlowSliceMeta>();
    ChkTrueNot(sliceMeta != nullptr, BIO_ALLOC_FAIL);
    ret = mSliceOperator.Copy(metaSlice.Get(), (char *)sliceMeta.get());
    ChkTrueNot(ret == BIO_OK, ret);

    if (sliceRef->GetState() == SLICE_VALID && isMaster) {
        auto &key = sliceMeta->key;
        ChkTrueNot(sliceMeta->length == slice->GetLength(), BIO_INNER_ERR);
        void *value = aligned_alloc(NO_4096, NO_4194304);
        ChkTrueNot(value != nullptr, BIO_ALLOC_FAIL);

        ret = mSliceOperator.Copy(slice.Get(), reinterpret_cast<char *>(value));
        if (UNLIKELY(ret != BIO_OK)) {
            free(value);
            LOG_ERROR("failed to copy slice to value. ret:" << ret << ", slice:" << slice->ToString());
            return ret;
        }
        ret = mUnderFs->Put(key, reinterpret_cast<char *>(value), sliceMeta->length);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to put slice to underfs, key:" << key << ", length:" << sliceMeta->length);
            free(value);
            return ret;
        }

        LOG_INFO("Evict data to rcache, key:" << key << ", length:" << sliceMeta->length << ".");

        BIO_TRACE_START(WCACHE_TRACE_PUT_RCACHE);
        ret = EvictToRcache(slice, key, value); // 淘汰到读Cache失败时，不做中止
        BIO_TRACE_END(WCACHE_TRACE_PUT_RCACHE, ret);

        free(value);
    }

    // when update slice finished, then release resource of flow.
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceRef, sliceMeta](const WCacheSlicePtr &oldSlice) {
        auto &diskCache = mCacheTiers[WCACHE_DISK];
        auto ret = diskCache->Evict(oldSlice);
        if (UNLIKELY(ret != BIO_OK)) {
            DecreaseRef();
            LOG_ERROR("failed to evict old slice." << ret << ", slice:" << oldSlice->ToString());
            return;
        }
        if (sliceRef->GetState() == SLICE_VALID) {
            uint64_t ptId = CacheFlowIdManager::GetPtId(oldSlice->GetFlowId());
            mEvictCallback(ptId, sliceMeta->key, sliceRef);
        }
        DecreaseRef();
    };

    sliceMeta->hasEvict = 1;
    ret = mSliceOperator.Copy((char *)sliceMeta.get(), metaSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    sliceRef->SetSlice(nullptr, callback);
    BIO_TRACE_END(WCACHE_TRACE_EVICT2UNDERFS, 0);
    return BIO_OK;
}

BResult WCache::EvictToRcache(const WCacheSlicePtr &slice, const Key &key, void *value)
{
    // check read cache resources used
    auto config = BioConfig::Instance()->GetDaemonConfig();
    uint64_t diskCap = static_cast<uint64_t>(config.diskCaps[mDiskId]);

    uint64_t rcacheMemCap = (static_cast<uint64_t>(config.memReadRatio) * config.memCap) / NO_10;
    uint64_t rcacheMemUsed = FlowManager::GetCacheUsedSize(FLOW_RCACHE, FLOW_MEMORY, 0);
    uint64_t rcacheDiskCap = diskCap * static_cast<uint64_t>(config.diskReadRatio) / NO_10;
    uint64_t rcacheDiskUsed = FlowManager::GetCacheUsedSize(FLOW_RCACHE, FLOW_DISK, mDiskId);
    if (rcacheMemUsed >= rcacheMemCap || rcacheDiskUsed >= rcacheDiskCap) {
        return BIO_ERR;
    }

    // malloc memory from read cache, and copy slice to this slice.
    uint64_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());
    WCacheSlicePtr writeSlice = nullptr;
    mRCacheManager->AllocResources(ptId, slice->GetLength(), writeSlice);
    auto ret = mSliceOperator.Copy(reinterpret_cast<char *>(value), writeSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);
    ret = mRCacheManager->Put(ptId, key, writeSlice);
    ChkTrue(ret == BIO_OK, ret, "Failed to put slice to rcache, ptId:" << ptId << " key:" << key);
    return BIO_OK;
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

    uint64_t rcacheMemCap = (static_cast<uint64_t>(config.memReadRatio) * config.memCap) / NO_10;
    uint64_t rcacheMemUsed = FlowManager::GetCacheUsedSize(FLOW_RCACHE, FLOW_MEMORY, 0);
    uint64_t rcacheDiskCap = diskCap * static_cast<uint64_t>(config.diskReadRatio) / NO_10;
    uint64_t rcacheDiskUsed = FlowManager::GetCacheUsedSize(FLOW_RCACHE, FLOW_DISK, mDiskId);
    if (rcacheMemUsed >= rcacheMemCap || rcacheDiskUsed >= rcacheDiskCap) {
        return false;
    }

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

    bool isSatisfied;
    LVOS_TP_START(WCACHE_CHECK_RCACHE_LEVEL_FAIL, &isSatisfied, false);
    isSatisfied = EvictDiskSatisfiedCond();
    LVOS_TP_END;

    if (!isSatisfied && !mIsForced) {
        mRetryCallback(mFlowId, WCACHE_DISK);
        return BIO_OK;
    }

    uint64_t globEvictOffset = NO_MAX_VALUE64;
    if (!isMaster && !mIsForced) {
        LVOS_TP_START(WCACHE_GET_EVICT_OFFSET_FAIL, &ret, BIO_INNER_RETRY);
        ret = mGlobEvictOffset(static_cast<uint16_t>(mPtId), mFlowId, globEvictOffset);
        LVOS_TP_END;
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
    LOG_DEBUG("Flush mem, flowId:" << mFlowId);
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    while (sliceRef != nullptr) {
        auto ret = EvictFromMemToDisk(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
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
    LOG_DEBUG("Flush disk, flowId:" << mFlowId);
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
    WCacheSliceRefPtr sliceRef = mCacheTiers[WCACHE_MEMORY]->GetEvictSlice();
    while (sliceRef != nullptr) {
        auto ret = ExpiredClearMemImpl(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictQueue(sliceRef);
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
}
}
