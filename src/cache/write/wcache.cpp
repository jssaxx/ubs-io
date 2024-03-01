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

namespace ock {
namespace bio {
BResult WCache::Init(const ExecutorServicePtr evictService[MAX_WCACHE_TIER],
    const GetGlobEvictOffset evictOffset, EvictCallback evictCallback, const RetryCallback retryCallback,
    const RCacheManagerPtr rCacheManager)
{
    for (int i = 0; i < MAX_WCACHE_TIER; ++i) {
        auto cacheTier = MakeRef<WCacheTier>();
        ChkTrueNot(cacheTier != nullptr, BIO_ALLOC_FAIL);
        auto ret = cacheTier->Init(static_cast<WCacheTierType>(i), mFlowId, mDiskId);
        ChkTrue(ret == BIO_OK, ret, "Failed to init cacheTier, WCacheTierType:" << i << " flowId:" << mFlowId);
        mCacheTiers[i] = cacheTier;
    }

    mEvictService[WCACHE_MEMORY] = evictService[WCACHE_MEMORY];
    mEvictService[WCACHE_DISK] = evictService[WCACHE_DISK];
    mEvictRef[WCACHE_MEMORY] = false;
    mEvictRef[WCACHE_DISK] = false;
    mGlobEvictOffset = evictOffset;
    mEvictCallback = std::move(evictCallback);
    mRetryCallback = std::move(retryCallback);
    mRCacheManager = rCacheManager;
    mUnderFs = UnderFs::Instance();

    return BIO_OK;
}

void WCache::Exit() {}

BResult WCache::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    return memCache->GetDataSlice(sliceKey, slice);
}

BResult WCache::Put(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr, bool isDegrade)
{
    // degraded write through to underfs
    if (UNLIKELY(isDegrade)) {
        return PutByPass(key, srcSlice, sliceReader, destSliceRef);
    }

    // put it memory tier cache.
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    destSliceRef = memCache->Write(key, srcSlice, sliceReader);
    ChkTrueNot(destSliceRef != nullptr, BIO_INNER_ERR);

    if (attr.strategy == WRITE_BACK) {
        // write back.
        LOG_INFO("Write back, key:" << key << ", FlyCnt:" << ++mFlyCnt);
        StartEvictTask(WCACHE_MEMORY);
    } else {
        // write through
        LOG_INFO("Write through, key:" << key);
        EvictFromMemToDisk(destSliceRef);
    }

    return BIO_OK;
}

BResult WCache::PutByPass(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef)
{
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    destSliceRef = memCache->Write(key, srcSlice, sliceReader);
    ChkTrueNot(destSliceRef != nullptr, BIO_INNER_ERR);

    auto *value = new char[srcSlice->GetLength()];
    ChkTrueNot(value != nullptr, BIO_ALLOC_FAIL);

    auto ret = mSliceOperator.Copy(destSliceRef->GetSlice().Get(), value);
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

    return BIO_OK;
}

BResult WCache::Seal()
{
    BResult ret;

    ret = mCacheTiers[WCACHE_MEMORY]->Seal();
    if (ret != BIO_OK) {
        LOG_ERROR("Seal mem cacheTier fail:" << ret << ", flowId:" << mFlowId);
        return ret;
    }

    ret = mCacheTiers[WCACHE_DISK]->Seal();
    if (ret != BIO_OK) {
        LOG_ERROR("Seal disk cacheTier fail:" << ret << ", flowId:" << mFlowId);
        return ret;
    }

    return BIO_OK;
}

void WCache::StartEvictTask(WCacheTierType type)
{
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
        mEvictRef[type] = false;
    }
    return;
}

void WCache::RetryEvictTask(WCacheTierType type)
{
    bool isSucceed;
    if (type == WCACHE_MEMORY) {
        isSucceed = mEvictService[type]->Execute([this]() { EvictAllMemSliceToDisk(); });
    } else {
        isSucceed = mEvictService[type]->Execute([this]() { EvictAllDiskSliceToUnderFs(); });
    }

    if (!isSucceed) {
        mEvictRef[type] = false;
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

        if (sliceMeta.offset < dataRangeStart ||
            sliceMeta.offset + sliceMeta.length > dataRangeEnd) {
            continue;
        }

        SliceKey sliceKey(mFlowId, sliceMeta.offset, FLOW_DISK, sliceMeta.length, flowIndex);

        WCacheSlicePtr dataSlice = nullptr;
        ret = diskCache->GetDataSlice(sliceKey, dataSlice);
        ChkTrue(ret == BIO_OK, ret, "Failed to get data slice:" << ret);

        auto sliceRef = MakeRef<WCacheSliceRef>(dataSlice);
        ChkTrueNot(sliceRef != nullptr, BIO_ERR);

        diskCache->AddEvictQueue(sliceRef);

        ret = recoverCallback(mPtId, sliceMeta.key, sliceRef);
        ChkTrueNot(ret == BIO_OK, ret);
    }

    return BIO_OK;
}

void WCache::Flush()
{
    bool expectval = false;
    if (!mEvictRef[WCACHE_DISK].compare_exchange_weak(expectval, true)) {
        return;
    }

    bool isSucceed = mEvictService[WCACHE_DISK]->Execute([this]() { FlushImpl(); });
    if (!isSucceed) {
        mEvictRef[WCACHE_DISK] = false;
    }
    return;
}

void WCache::ExpiredClear()
{
    bool expectval = false;
    if (!mEvictRef[WCACHE_DISK].compare_exchange_weak(expectval, true)) {
        return;
    }

    bool isSucceed = mEvictService[WCACHE_DISK]->Execute([this]() { ExpiredClearImpl(); });
    if (!isSucceed) {
        mEvictRef[WCACHE_DISK] = false;
    }
    return;
}

BResult WCache::EvictFromMemToDisk(WCacheSliceRefPtr sliceRef)
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
        "Failed to get meta data slice in WCACHE_MEMORY, indexInFlow:" << indexInFlow << " offset:"
        << offset << " length:" << length);

    auto &diskCache = mCacheTiers[WCACHE_DISK];
    WFlowMetaDataSlice diskMetaDataSlice;
    ret = diskCache->GetMetaDataSlice(indexInFlow, offset, length, diskMetaDataSlice);
    ChkTrueNot(ret == BIO_OK, ret);

    ret = mSliceOperator.Copy(memMetaDataSlice.dataSlice.Get(), diskMetaDataSlice.dataSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    ret = mSliceOperator.Copy(memMetaDataSlice.metaSlice.Get(), diskMetaDataSlice.metaSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    LOG_INFO("Evict memory to disk, flowId:" << slice->GetFlowId() << ", indexInFlow:" << indexInFlow << ", offset:" <<
        offset << ", length:" << length << ", FlyCnt:" << --mFlyCnt);

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

BResult WCache::EvictFromDiskToUnderFs(WCacheSliceRefPtr sliceRef, bool isMaster)
{
    auto &diskCache = mCacheTiers[WCACHE_DISK];
    auto slice = sliceRef->GetSlice();
    WCacheSlicePtr metaSlice = nullptr;
    auto ret = diskCache->GetMetaSlice(slice->GetIndexInFlow(), metaSlice);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to to evict from dist to underfs, flowId:" << slice->GetFlowId() << ", index:"
        << slice->GetIndexInFlow() << ", offset:" << slice->GetOffsetInFlow());

    BIO_TRACE_START(WCACHE_TRACE_EVICT2UNDERFS);

    LOG_INFO("Evict flowId:" << slice->GetFlowId() << ", index:" << slice->GetIndexInFlow() << ", offset:" << slice->GetOffsetInFlow());

    auto sliceMeta = std::make_shared<WFlowSliceMeta>();
    ChkTrueNot(sliceMeta != nullptr, BIO_ALLOC_FAIL);
    ret = mSliceOperator.Copy(metaSlice.Get(), (char *)sliceMeta.get());
    ChkTrueNot(ret == BIO_OK, ret);

    auto &key = sliceMeta->key;
    ChkTrueNot(sliceMeta->length == slice->GetLength(), BIO_INNER_ERR);
    auto *value = new char[sliceMeta->length];
    ChkTrueNot(value != nullptr, BIO_ALLOC_FAIL);

    ret = mSliceOperator.Copy(slice.Get(), value);
    if (UNLIKELY(ret != BIO_OK)) {
        delete[] value;
        LOG_ERROR("failed to copy slice to value. ret:" << ret << ",slice:" << slice->ToString());
        return ret;
    }

    if (sliceRef->GetState() == SLICE_VALID) {
        ret = mUnderFs->Put(key, value, sliceMeta->length);
        delete[] value;
        ChkTrue(ret == BIO_OK, ret, "Failed to put slice to underfs, key:" << key << ", length:" << sliceMeta->length);

        LOG_INFO("Evict data to rcache, key:" << key << ", length:" << sliceMeta->length << ".");

        BIO_TRACE_START(WCACHE_TRACE_PUT_RCACHE);
        // malloc memory from read cache, and copy slice to this slice.
        uint64_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());
        WCacheSlicePtr writeSlice = nullptr;
        mRCacheManager->AllocResources(ptId, slice->GetLength(), writeSlice);
        ret = mSliceOperator.Copy(slice.Get(), writeSlice.Get());
        ChkTrueNot(ret == BIO_OK, ret);
        ret = mRCacheManager->Put(ptId, key, writeSlice);
        ChkTrue(ret == BIO_OK, ret, "Failed to put slice to rcache, ptId:" << ptId << " key:" << key);
        BIO_TRACE_END(WCACHE_TRACE_PUT_RCACHE, 0);
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
            mEvictCallback(ptId, sliceMeta->key);
        }
        DecreaseRef();
    };

    sliceRef->SetSlice(nullptr, callback);
    BIO_TRACE_END(WCACHE_TRACE_EVICT2UNDERFS, 0);
    return BIO_OK;
}

BResult WCache::EvictAllMemSliceToDisk()
{
    auto evictSliceQueue = mCacheTiers[WCACHE_MEMORY]->GetEvictSliceQueue();
    auto sliceIter = evictSliceQueue.begin();
    for (auto &sliceRef : evictSliceQueue) {
        auto ret = EvictFromMemToDisk(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_MEMORY]->RetryEvictSliceQueue(sliceIter, evictSliceQueue.end());
            mRetryCallback(mFlowId, WCACHE_MEMORY);
            return ret;
        }
        ++sliceIter;
    }

    mEvictRef[WCACHE_MEMORY] = false;
    return BIO_OK;
}

BResult WCache::EvictAllDiskSliceToUnderFs()
{
    bool isMaster;
    uint64_t globEvictOffset;
    auto ret = mGlobEvictOffset(static_cast<uint16_t>(mPtId), mFlowId, isMaster, globEvictOffset);
    if (ret != BIO_OK) {
        LOG_WARN("Get evict offset fail:" << ret << ", ptId:" << mPtId << ", flowId:" << mFlowId);
        mRetryCallback(mFlowId, WCACHE_DISK);
        return ret;
    }

    auto evictSliceQueue = mCacheTiers[WCACHE_DISK]->GetEvictSliceQueue();
    auto sliceIter = evictSliceQueue.begin();
    for (auto &sliceRef : evictSliceQueue) {
        auto slice = sliceRef->GetSlice();
        uint64_t sliceEvictOffset = slice->GetOffsetInFlow() + slice->GetLength();
        if (globEvictOffset < sliceEvictOffset) {
            mCacheTiers[WCACHE_DISK]->RetryEvictSliceQueue(sliceIter, evictSliceQueue.end());
            mRetryCallback(mFlowId, WCACHE_DISK);
            return BIO_OK;
        }
        auto ret = EvictFromDiskToUnderFs(sliceRef, isMaster);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_DISK]->RetryEvictSliceQueue(sliceIter, evictSliceQueue.end());
            mRetryCallback(mFlowId, WCACHE_DISK);
            return ret;
        }
        ++sliceIter;
    }

    mEvictRef[WCACHE_DISK] = false;
    return BIO_OK;
}

BResult WCache::FlushImpl()
{
    auto evictSliceQueue = mCacheTiers[WCACHE_DISK]->GetEvictSliceQueue();
    auto sliceIter = evictSliceQueue.begin();
    for (auto &sliceRef : evictSliceQueue) {
        auto ret = EvictFromDiskToUnderFs(sliceRef, true);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_DISK]->RetryEvictSliceQueue(sliceIter, evictSliceQueue.end());
            mEvictRef[WCACHE_DISK] = false;
            return ret;
        }
        ++sliceIter;
    }

    mEvictRef[WCACHE_DISK] = false;
    return BIO_OK;
}

BResult WCache::ExpiredClearDisk(WCacheSliceRefPtr sliceRef)
{
    auto &diskCache = mCacheTiers[WCACHE_DISK];
    auto slice = sliceRef->GetSlice();
    WCacheSlicePtr metaSlice = nullptr;
    auto ret = diskCache->GetMetaSlice(slice->GetIndexInFlow(), metaSlice);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to to evict from dist to underfs, flowId:" << slice->GetFlowId() << ", index:"
        << slice->GetIndexInFlow() << ", offset:" << slice->GetOffsetInFlow());

    LOG_INFO("Evict flowId:" << slice->GetFlowId() << ", index:" << slice->GetIndexInFlow() <<
        ", offset:" << slice->GetOffsetInFlow());

    auto sliceMeta = std::make_shared<WFlowSliceMeta>();
    ChkTrueNot(sliceMeta != nullptr, BIO_ALLOC_FAIL);
    ret = mSliceOperator.Copy(metaSlice.Get(), (char *)sliceMeta.get());
    ChkTrueNot(ret == BIO_OK, ret);

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
            mEvictCallback(ptId, sliceMeta->key);
        }
        DecreaseRef();
    };

    sliceRef->SetSlice(nullptr, callback);
    return BIO_OK;
}

BResult WCache::ExpiredClearImpl()
{
    auto evictSliceQueue = mCacheTiers[WCACHE_DISK]->GetEvictSliceQueue();
    auto sliceIter = evictSliceQueue.begin();
    for (auto &sliceRef : evictSliceQueue) {
        auto ret = ExpiredClearDisk(sliceRef);
        if (ret != BIO_OK) {
            mCacheTiers[WCACHE_DISK]->RetryEvictSliceQueue(sliceIter, evictSliceQueue.end());
            mEvictRef[WCACHE_DISK] = false;
            return ret;
        }
        ++sliceIter;
    }

    mEvictRef[WCACHE_DISK] = false;
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
