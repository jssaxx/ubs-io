/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "wcache_tier.h"
#include "flow_manager.h"
#include "securec.h"

namespace ock {
namespace bio {
BResult WCacheTier::Init(WCacheTierType cacheTier, uint64_t flowId)
{
    FlowType flowType;
    auto ret = ToFlowType(cacheTier, flowType);
    ASSERT_RETURN(ret == BIO_OK, ret);

    auto &flowManager = FlowManager::Instance();

    uint64_t metaFlowId = flowId | (((uint64_t)((1 << 8) | (cacheTier << 1) | 0) & 0x7FF) << 40) /* meta */;
    mMetaFlow = flowManager->CreateObject(flowType, metaFlowId);
    ASSERT_RETURN(mMetaFlow != nullptr, BIO_ALLOC_FAIL);

    uint64_t dataFlowId = flowId | (((uint64_t)((1 << 8) | (cacheTier << 1) | 1) & 0x7FF) << 40) /* data */;
    mDataFlow = flowManager->CreateObject(flowType, dataFlowId);
    ASSERT_RETURN(mDataFlow != nullptr, BIO_ALLOC_FAIL);

    mFlowTruncateCursor = MakeRef<WFlowTruncateCursor>();
    ASSERT_RETURN(mFlowTruncateCursor != nullptr, BIO_ALLOC_FAIL);

    return BIO_OK;
}

WCacheSliceRefPtr WCacheTier::Write(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader)
{
    // fill meta flow.
    auto metaFlowOffset = slice->GetIndexInFlow() * sizeof(WFlowSliceMeta);
    auto metaSlice = GetSlice(mMetaFlow, metaFlowOffset, slice->GetIndexInFlow(), sizeof(WFlowSliceMeta));
    WFlowSliceMeta sliceMeta{};
    memcpy_s(sliceMeta.key, NO_512, key, strlen(key));
    sliceMeta.offset = slice->GetOffsetInFlow();
    sliceMeta.length = slice->GetLength();
    auto ret = mSliceOperator.Copy(reinterpret_cast<const char *>(&sliceMeta), metaSlice.Get());
    ASSERT_RETURN(ret == BIO_OK, nullptr);

    // fill data flow.
    auto dataSlice = GetSlice(mDataFlow, slice->GetOffsetInFlow(), slice->GetIndexInFlow(), slice->GetLength());
    ret = sliceReader(slice.Get(), dataSlice.Get());
    ASSERT_RETURN(ret == BIO_OK, nullptr);

    auto sliceRef = MakeRef<WCacheSliceRef>(dataSlice);
    ASSERT_RETURN(sliceRef != nullptr, nullptr);

    {
        std::lock_guard<std::mutex> lock(mEvictSliceQueueLock);
        mEvictSliceQueue.emplace_back(sliceRef);
    }

    return sliceRef;
}

void WCacheTier::AddEvictQueue(WCacheSliceRefPtr &sliceRef)
{
    std::lock_guard<std::mutex> lock(mEvictSliceQueueLock);
    mEvictSliceQueue.emplace_back(sliceRef);
}

std::list<WCacheSliceRefPtr> WCacheTier::GetEvictSliceQueue()
{
    std::lock_guard<std::mutex> lock(mEvictSliceQueueLock);
    auto evictSliceQueue = std::move(mEvictSliceQueue);
    return evictSliceQueue;
}

BResult WCacheTier::GetMetaSlice(uint64_t indexInFlow, WCacheSlicePtr &slice)
{
    slice = GetSlice(mMetaFlow, indexInFlow * sizeof(WFlowSliceMeta), indexInFlow, sizeof(WFlowSliceMeta));
    ASSERT_RETURN(slice != nullptr, BIO_ERR);
    return BIO_OK;
}

BResult WCacheTier::GetDataSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    slice = GetSlice(mDataFlow, sliceKey);
    ASSERT_RETURN(slice != nullptr, BIO_ERR);
    return BIO_OK;
}

BResult WCacheTier::GetMetaDataSlice(uint64_t indexInFlow, uint64_t offset, uint64_t length,
    WFlowMetaDataSlice &metaDataSlice)
{
    auto metaSlice = GetSlice(mMetaFlow, indexInFlow * sizeof(WFlowSliceMeta), indexInFlow, sizeof(WFlowSliceMeta));
    ASSERT_RETURN(metaSlice != nullptr, BIO_ERR);

    auto dataSlice = GetSlice(mDataFlow, offset, indexInFlow, length);
    ASSERT_RETURN(dataSlice != nullptr, BIO_ERR);

    metaDataSlice.metaSlice = metaSlice;
    metaDataSlice.dataSlice = dataSlice;
    return BIO_OK;
}

uint64_t WCacheTier::GetDataCapacity()
{
    return mDataFlow->GetValidLen();
}

BResult WCacheTier::Evict(const WCacheSlicePtr &slice)
{
    auto truncateSlice = mFlowTruncateCursor->GetTruncateSlice(slice);
    if (truncateSlice == nullptr) {
        return BIO_OK;
    }

    LOG_INFO("FlowId:" << truncateSlice->GetFlowId() << ", fLowType:" << truncateSlice->GetFlowType() <<
        ", flowOffset:" << truncateSlice->GetOffsetInFlow() << ", flowIndex:" << slice->GetIndexInFlow() << ", len:" << truncateSlice->GetLength());

    auto ret = mMetaFlow->TruncateOffset((truncateSlice->GetIndexInFlow() + 1) * sizeof(WFlowSliceMeta));
    ASSERT_RETURN(ret == BIO_OK, ret);

    ret = mDataFlow->TruncateOffset(truncateSlice->GetOffsetInFlow() + truncateSlice->GetLength());
    ASSERT_RETURN(ret == BIO_OK, ret);

    return BIO_OK;
}

WCacheSlicePtr WCacheTier::GetSlice(const FlowPtr &flow, const SliceKey &sliceKey)
{
    std::vector<FlowAddr> flowAddrs;
    auto ret = flow->GetAddrByOffset(sliceKey.flowOffset, sliceKey.length, flowAddrs);
    ASSERT_RETURN(ret == BIO_OK, nullptr);

    return MakeRef<WCacheSlice>(sliceKey.flowId, sliceKey.flowOffset, sliceKey.indexInFlow, sliceKey.length, flowAddrs, flow->GetFlowType());
}

WCacheSlicePtr WCacheTier::GetSlice(const FlowPtr &flow, uint64_t offset, uint64_t index, uint64_t length)
{
    std::vector<FlowAddr> flowAddrs;
    auto ret = flow->GetAddrByOffset(offset, length, flowAddrs);
    ASSERT_RETURN(ret == BIO_OK, nullptr);

    return MakeRef<WCacheSlice>(flow->GetFlowId(), offset, index, length, flowAddrs, flow->GetFlowType());
}

WCacheSlicePtr WFlowTruncateCursor::GetTruncateSlice(const WCacheSlicePtr &slice)
{
    std::lock_guard<std::mutex> lock(mEvictedSliceListLock);

    LOG_INFO("FlowId:" << slice->GetFlowId() << ", fLowType:" << slice->GetFlowType() <<
        ", flowOffset:" << slice->GetOffsetInFlow() << ", flowIndex:" << slice->GetIndexInFlow() << ", len:" << slice->GetLength());

    // insert to set and sort by indexInFlow.
    mEvictedSlices.emplace(slice);

    // obtain the last truncate slice from the indexInFlow that is truncated last time.
    WCacheSlicePtr truncateSlice = nullptr;
    uint64_t truncateSliceIndex = mPreTruncateSliceIndex;
    auto evictSliceIt = mEvictedSlices.begin();
    while (evictSliceIt != mEvictedSlices.end()) {
        auto evictSlice = evictSliceIt->Get();
        if (evictSlice->GetIndexInFlow() == truncateSliceIndex) {
            truncateSlice = evictSlice;
            mEvictedSlices.erase(evictSliceIt);
            evictSliceIt = mEvictedSlices.begin();
            truncateSliceIndex++;
            mPreTruncateSliceIndex = truncateSliceIndex;
        } else {
            break;
        }
    }

    return truncateSlice;
}
}
}