/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "wcache.h"

#include <utility>
#include "cache_slice_operator.h"
#include "securec.h"
#include "flow_manager.h"
#include "cache_flow.h"

namespace ock {
namespace bio {
BResult WCache::Init(uint64_t flowId, const ExecutorServicePtr &exeService, EvictCallback evictCallback)
{
    for (int i = 0; i < MAX_WCACHE_TIER; ++i) {
        auto cacheTier = MakeRef<WCacheTier>();
        ASSERT_RETURN(cacheTier != nullptr, BIO_ALLOC_FAIL);
        auto ret = cacheTier->Init(static_cast<WCacheTierType>(i), flowId);
        ASSERT_RETURN(ret == BIO_OK, ret);
        mCacheTiers[i] = cacheTier;
    }

    mFlowId = flowId;
    mExeService = exeService;
    mEvictCallback = std::move(evictCallback);

    return BIO_OK;
}

void WCache::Exit() {}

BResult WCache::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    return memCache->GetDataSlice(sliceKey, slice);
}

BResult WCache::Put(const Key &key, const WCacheSlicePtr &srcSlice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef)
{
    // put it memory tier cache.
    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    destSliceRef = memCache->Write(key, srcSlice, sliceReader);
    ASSERT_RETURN(destSliceRef != nullptr, BIO_INNER_ERR);

    // write back or write through?
    // TODO: make it configurable.
    if (true) {
        // write back.
        // and evict slice from memory to disk asap.
        auto success = mExeService->Execute([&, destSliceRef]() { EvictFromMemToDisk(destSliceRef); });
        ASSERT_RETURN(success, BIO_INNER_ERR);
    } else {
        // write through
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

    auto &memCache = mCacheTiers[WCACHE_MEMORY];
    WFlowMetaDataSlice memMetaDataSlice;
    auto ret = memCache->GetMetaDataSlice(indexInFlow, offset, length, memMetaDataSlice);
    ASSERT_RETURN(ret == BIO_OK, ret);

    auto &diskCache = mCacheTiers[WCACHE_DISK];
    WFlowMetaDataSlice diskMetaDataSlice;
    ret = diskCache->GetMetaDataSlice(indexInFlow, offset, length, diskMetaDataSlice);
    ASSERT_RETURN(ret == BIO_OK, ret);

    ret = mSliceOperator.Copy(memMetaDataSlice.dataSlice.Get(), diskMetaDataSlice.dataSlice.Get());
    ASSERT_RETURN(ret == BIO_OK, ret);

    ret = mSliceOperator.Copy(memMetaDataSlice.metaSlice.Get(), diskMetaDataSlice.metaSlice.Get());
    ASSERT_RETURN(ret == BIO_OK, ret);

    LOG_INFO("Evict memory to disk, flowId:" << slice->GetFlowId() << ", indexInFlow:" << indexInFlow << ", offset:" <<
        offset << ", length:" << length << ".");

    diskCache->AddEvictQueue(sliceRef);

    // when update slice finished, then release resource of flow.
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this](const WCacheSlicePtr &oldSlice) {
        auto &memCache = mCacheTiers[WCACHE_MEMORY];
        auto ret = memCache->Evict(oldSlice);
        DecreaseRef();
        ASSERT_RET_VOID(ret == BIO_OK);
    };

    sliceRef->SetSlice(diskMetaDataSlice.dataSlice, callback);
    return BIO_OK;
}

BResult WCache::EvictFromDiskToUnderFs(const RCacheManagerPtr &rCacheManager, const WCacheSliceRefPtr &sliceRef)
{
    auto &diskCache = mCacheTiers[WCACHE_DISK];
    auto slice = sliceRef->GetSlice();
    WCacheSlicePtr metaSlice = nullptr;
    auto ret = diskCache->GetMetaSlice(slice->GetIndexInFlow(), metaSlice);
    ASSERT_RETURN(ret == BIO_OK, ret);

    LOG_INFO("Evict flowId:" << slice->GetFlowId() << ", index:" << slice->GetIndexInFlow() << ", offset:" << slice->GetOffsetInFlow());

    auto sliceMeta = std::make_shared<WFlowSliceMeta>();
    ASSERT_RETURN(sliceMeta != nullptr, BIO_ALLOC_FAIL);
    ret = mSliceOperator.Copy(metaSlice.Get(), (char *)sliceMeta.get());
    ASSERT_RETURN(ret == BIO_OK, ret);

    auto &key = sliceMeta->key;
    ASSERT_RETURN(sliceMeta->length == slice->GetLength(), BIO_INNER_ERR);
    auto *value = new char[sliceMeta->length];
    ASSERT_RETURN(value != nullptr, BIO_ALLOC_FAIL);

    ret = mSliceOperator.Copy(slice.Get(), value);
    if (ret != BIO_OK) {
        delete[] value;
        LOG_ERROR("failed to copy slice to value. ret:" << ret << ",slice:" << slice->ToString());
        return ret;
    }

    if (slice->GetSliceState() == 0) {
        ret = mUnderFs->Put(key, value, sliceMeta->length);
        delete[] value;
        ASSERT_RETURN(ret == BIO_OK, ret);

        LOG_INFO("Evict data to rcache, key:" << key << ", length:" << sliceMeta->length << ".");

        // malloc memory from read cache, and copy slice to this slice.
        uint64_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());
        WCacheSlicePtr writeSlice = nullptr;
        rCacheManager->AllocResources(ptId, slice->GetLength(), writeSlice);
        ret = mSliceOperator.Copy(slice.Get(), writeSlice.Get());
        ASSERT_RETURN(ret == BIO_OK, ret);
        ret = rCacheManager->Put(ptId, key, writeSlice);
        ASSERT_RETURN(ret == BIO_OK, ret);
    }

    // when update slice finished, then release resource of flow.
    IncreaseRef();
    WCacheSliceRef::SetSliceCallback callback = [this, sliceMeta](const WCacheSlicePtr &oldSlice) {
        auto &diskCache = mCacheTiers[WCACHE_DISK];
        auto ret = diskCache->Evict(oldSlice);
        if (ret != BIO_OK) {
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

    return BIO_OK;
}

BResult WCache::EvictAllMemSliceToDisk()
{
    auto evictSliceQueue = mCacheTiers[WCACHE_MEMORY]->GetEvictSliceQueue();
    for (auto &sliceRef : evictSliceQueue) {
        auto ret = EvictFromMemToDisk(sliceRef);
        ASSERT(ret == BIO_OK);
    }
    return BIO_OK;
}

BResult WCache::EvictAllDiskSliceToUnderFs(const RCacheManagerPtr &rCacheManager)
{
    auto evictSliceQueue = mCacheTiers[WCACHE_DISK]->GetEvictSliceQueue();
    for (auto &sliceRef : evictSliceQueue) {
        auto ret = EvictFromDiskToUnderFs(rCacheManager, sliceRef);
        ASSERT(ret == BIO_OK);
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
