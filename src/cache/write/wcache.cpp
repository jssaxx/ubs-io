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
BResult WCache::Init(uint64_t flowId, const ExecutorServicePtr &exeService, EvictCallback evictCallback)
{
    for (int i = 0; i < MAX_WCACHE_TIER; ++i) {
        auto cacheTier = MakeRef<WCacheTier>();
        ChkTrueNot(cacheTier != nullptr, BIO_ALLOC_FAIL);
        auto ret = cacheTier->Init(static_cast<WCacheTierType>(i), flowId);
        ChkTrue(ret == BIO_OK, ret, "Failed to init cacheTier, WCacheTierType:" << i << " flowId:" << flowId);
        mCacheTiers[i] = cacheTier;
    }

    mFlowId = flowId;
    mExeService = exeService;
    mEvictCallback = std::move(evictCallback);
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
    WCacheSliceRefPtr &destSliceRef, CacheAttr &attr)
{
    // put it memory tier cache.
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    destSliceRef = memCache->Write(key, srcSlice, sliceReader);
    ChkTrueNot(destSliceRef != nullptr, BIO_INNER_ERR);

    if (attr.strategy == WRITE_BACK) {
        // write back.
        LOG_INFO("Write back, key:" << key << ", FlyCnt:" << mFlyCnt++);
        bool success = mExeService->Execute([&, destSliceRef]() { EvictFromMemToDisk(destSliceRef); });
        uint64_t sleepCnt = 0;
        while (!success && sleepCnt != NO_32) {
            sleep(1);
            success = mExeService->Execute([&, destSliceRef]() { EvictFromMemToDisk(destSliceRef); });
            sleepCnt++;
        }
        ChkTrueNot(success, BIO_INNER_ERR);
    } else {
        // write through
        LOG_INFO("Write through, key:" << key);
        EvictFromMemToDisk(destSliceRef);
    }

    return BIO_OK;
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
        offset << ", length:" << length << ", FlyCnt:" << mFlyCnt--);

    diskCache->AddEvictQueue(sliceRef);

    // when update slice finished, then release resource of flow.
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this](const WCacheSlicePtr &oldSlice) {
        auto &memCache = mCacheTiers[WCACHE_MEMORY];
        auto ret = memCache->Evict(oldSlice);
        DecreaseRef();
        ChkTrueExNot(ret == BIO_OK);
    };

    sliceRef->SetSlice(diskMetaDataSlice.dataSlice, callback);
    BIO_TRACE_END(WCACHE_TRACE_EVICT2DISK, 0);
    return BIO_OK;
}

BResult WCache::EvictFromDiskToUnderFs(const RCacheManagerPtr &rCacheManager, const WCacheSliceRefPtr &sliceRef)
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

    if (slice->GetSliceState() == 0) {
        ret = mUnderFs->Put(key, value, sliceMeta->length);
        delete[] value;
        ChkTrue(ret == BIO_OK, ret, "Failed to put slice to underfs, key:" << key << ", length:" << sliceMeta->length);

        LOG_INFO("Evict data to rcache, key:" << key << ", length:" << sliceMeta->length << ".");

        BIO_TRACE_START(WCACHE_TRACE_PUT_RCACHE);
        // malloc memory from read cache, and copy slice to this slice.
        uint64_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());
        WCacheSlicePtr writeSlice = nullptr;
        rCacheManager->AllocResources(ptId, slice->GetLength(), writeSlice);
        ret = mSliceOperator.Copy(slice.Get(), writeSlice.Get());
        ChkTrueNot(ret == BIO_OK, ret);
        ret = rCacheManager->Put(ptId, key, writeSlice);
        ChkTrue(ret == BIO_OK, ret, "Failed to put slice to rcache, ptId:" << ptId << " key:" << key);
        BIO_TRACE_END(WCACHE_TRACE_PUT_RCACHE, 0);
    }

    // when update slice finished, then release resource of flow.
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceMeta](const WCacheSlicePtr &oldSlice) {
        auto &diskCache = mCacheTiers[WCACHE_DISK];
        auto ret = diskCache->Evict(oldSlice);
        if (UNLIKELY(ret != BIO_OK)) {
            DecreaseRef();
            LOG_ERROR("failed to evict old slice." << ret << ", slice:" << oldSlice->ToString());
            return;
        }
        if (oldSlice->GetSliceState() == 0) {
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
    for (auto &sliceRef : evictSliceQueue) {
        auto ret = EvictFromMemToDisk(sliceRef);
        ChkNot(ret == BIO_OK);
    }
    return BIO_OK;
}

BResult WCache::EvictAllDiskSliceToUnderFs(const RCacheManagerPtr &rCacheManager)
{
    auto evictSliceQueue = mCacheTiers[WCACHE_DISK]->GetEvictSliceQueue();
    for (auto &sliceRef : evictSliceQueue) {
        auto ret = EvictFromDiskToUnderFs(rCacheManager, sliceRef);
        ChkNot(ret == BIO_OK);
    }
    return BIO_OK;
}

uint64_t WCache::GetCapacity()
{
    auto diskCache = mCacheTiers[WCACHE_DISK];
    return diskCache->GetDataCapacity();
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
