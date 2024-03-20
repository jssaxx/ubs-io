/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "wcache_tier.h"
#include "flow_manager.h"
#include "securec.h"
#include "cache_flow.h"
#include "cm.h"

namespace ock {
namespace bio {
BResult WCacheTier::Init(WCacheTierType cacheTier, uint64_t flowId, uint16_t diskId)
{
    FlowType flowType;
    auto ret = ToFlowType(cacheTier, flowType);
    ChkTrueNot(ret == BIO_OK, ret);

    uint64_t metaInnerType = (cacheTier == WCACHE_MEMORY) ? WCACHE_FLOW_MEM_META_PREFIX : WCACHE_FLOW_DISK_META_PREFIX;
    uint64_t dataInnerType = (cacheTier == WCACHE_MEMORY) ? WCACHE_FLOW_MEM_DATA_PREFIX : WCACHE_FLOW_DISK_DATA_PREFIX;

    auto &flowManager = FlowManager::Instance();

    uint64_t metaFlowId = flowId | (metaInnerType << CACHE_FLOW_ID_PREFIX_SHIFT); /* meta */
    mMetaFlow = flowManager->CreateObject(flowType, metaFlowId, diskId);
    ChkTrue(mMetaFlow != nullptr, BIO_ALLOC_FAIL,
        "Failed to create metaflow, flowType:" << flowType << " metaFlowId" << metaFlowId);

    LOG_INFO("Meta flowId:" << metaFlowId << ", flowType:" << flowType);

    uint64_t dataFlowId = flowId | (dataInnerType << CACHE_FLOW_ID_PREFIX_SHIFT); /* data */
    mDataFlow = flowManager->CreateObject(flowType, dataFlowId, diskId);
    ChkTrue(mDataFlow != nullptr, BIO_ALLOC_FAIL,
        "Failed to create dataflow, flowType:" << flowType << " dataFlowId" << dataFlowId);

    LOG_INFO("Data flowId:" << dataFlowId << ", flowType:" << flowType);

    mFlowTruncateCursor = MakeRef<WFlowTruncateCursor>();
    ChkTrueNot(mFlowTruncateCursor != nullptr, BIO_ALLOC_FAIL);

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
    sliceMeta.magic = slice->GetFlowId();
    sliceMeta.hasEvict = 0;
    auto ret = mSliceOperator.Copy(reinterpret_cast<const char *>(&sliceMeta), metaSlice.Get());
    ChkTrueNot(ret == BIO_OK, nullptr);

    // fill data flow.
    auto dataSlice = GetSlice(mDataFlow, slice->GetOffsetInFlow(), slice->GetIndexInFlow(), slice->GetLength());
    ret = sliceReader(slice.Get(), dataSlice.Get());
    ChkTrueNot(ret == BIO_OK, nullptr);

    auto sliceRef = MakeRef<WCacheSliceRef>(dataSlice);
    ChkTrueNot(sliceRef != nullptr, nullptr);

    return sliceRef;
}

void WCacheTier::AddEvictQueue(WCacheSliceRefPtr sliceRef)
{
    mEvictSliceQueueLock.Lock();
    mEvictSliceQueue.emplace_back(sliceRef);
    mEvictSliceQueueLock.UnLock();
}

void WCacheTier::DelEvictQueue(WCacheSliceRefPtr sliceRef)
{
    mEvictSliceQueueLock.Lock();
    mEvictSliceQueue.remove(sliceRef);
    mEvictSliceQueueLock.UnLock();
}

void WCacheTier::RetryEvictSliceQueue(std::list<WCacheSliceRefPtr>::iterator start,
    std::list<WCacheSliceRefPtr>::iterator end)
{
    mEvictSliceQueueLock.Lock();
    mEvictSliceQueue.insert(mEvictSliceQueue.begin(), start, end);
    mEvictSliceQueueLock.UnLock();
}

bool WCacheTier::IsEmptyEvictSliceQueue()
{
    bool isEmpty;
    mEvictSliceQueueLock.Lock();
    isEmpty = mEvictSliceQueue.empty();
    mEvictSliceQueueLock.UnLock();
    return isEmpty;
}

std::list<WCacheSliceRefPtr> WCacheTier::GetEvictSliceQueue()
{
    mEvictSliceQueueLock.Lock();
    auto evictSliceQueue = std::move(mEvictSliceQueue);
    mEvictSliceQueueLock.UnLock();
    return evictSliceQueue;
}

BResult WCacheTier::GetMetaSlice(uint64_t indexInFlow, WCacheSlicePtr &slice)
{
    mMetaFlow->GetFlowId();
    slice = GetSlice(mMetaFlow, indexInFlow * sizeof(WFlowSliceMeta), indexInFlow, sizeof(WFlowSliceMeta));
    ChkTrue(slice != nullptr, BIO_ERR, "Failed to get meta slice, flowId" <<
        mMetaFlow->GetFlowId() << " indexInFlow:" << indexInFlow);
    return BIO_OK;
}

BResult WCacheTier::GetDataSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    slice = GetSlice(mDataFlow, sliceKey);
    ChkTrueNot(slice != nullptr, BIO_ERR);
    return BIO_OK;
}

BResult WCacheTier::GetMetaDataSlice(uint64_t indexInFlow, uint64_t offset, uint64_t length,
    WFlowMetaDataSlice &metaDataSlice)
{
    auto metaSlice = GetSlice(mMetaFlow, indexInFlow * sizeof(WFlowSliceMeta), indexInFlow, sizeof(WFlowSliceMeta));
    ChkTrue(metaSlice != nullptr, BIO_ERR,
        "Failed to get slice, metaFlow id: " << mMetaFlow->GetFlowId() << "indexInFlow:" << indexInFlow);

    auto dataSlice = GetSlice(mDataFlow, offset, indexInFlow, length);
    ChkTrue(dataSlice != nullptr, BIO_ERR,
        "Failed to get slice, dataFlow id:" << mDataFlow->GetFlowId() << " offset:" << offset << " length:" << length);

    metaDataSlice.metaSlice = metaSlice;
    metaDataSlice.dataSlice = dataSlice;
    return BIO_OK;
}

uint64_t WCacheTier::GetMetaVirCapacity()
{
    return mMetaFlow->GetTotalLen();
}

uint64_t WCacheTier::GetMetaEvictOffset()
{
    return mMetaFlow->GetTruncateOffset();
}

uint64_t WCacheTier::GetDataCapacity()
{
    return mDataFlow->GetValidLen();
}

uint64_t WCacheTier::GetDataVirCapacity()
{
    return mDataFlow->GetTotalLen();
}

uint64_t WCacheTier::GetDataEvictOffset()
{
    return mDataFlow->GetTruncateOffset();
}

BResult WCacheTier::Destroy()
{
    if (mMetaFlow != nullptr) {
        mMetaFlow->Seal();
        FlowManager::Instance()->DestroyObject(mMetaFlow->GetFlowType(), mMetaFlow->GetFlowId());
        mMetaFlow = nullptr;
    }

    if (mDataFlow != nullptr) {
        mDataFlow->Seal();
        FlowManager::Instance()->DestroyObject(mDataFlow->GetFlowType(), mDataFlow->GetFlowId());
        mDataFlow = nullptr;
    }

    return BIO_OK;
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
    ChkTrue(ret == BIO_OK, ret,
        "Failed to truncateOffset in metaFlow, FlowId:" << truncateSlice->GetFlowId() << ", fLowType:" <<
        truncateSlice->GetFlowType() << ", flowOffset:" << truncateSlice->GetOffsetInFlow() << ", flowIndex:" <<
        slice->GetIndexInFlow() << ", len:" << truncateSlice->GetLength());

    ret = mDataFlow->TruncateOffset(truncateSlice->GetOffsetInFlow() + truncateSlice->GetLength());
    ChkTrue(ret == BIO_OK, ret,
        "Failed to truncateOffset in dataFlow, FlowId:" << truncateSlice->GetFlowId() << ", fLowType:" <<
        truncateSlice->GetFlowType() << ", flowOffset:" << truncateSlice->GetOffsetInFlow() << ", flowIndex:" <<
        slice->GetIndexInFlow() << ", len:" << truncateSlice->GetLength());

    return BIO_OK;
}

inline WCacheSlicePtr WCacheTier::GetSlice(const FlowPtr &flow, const SliceKey &sliceKey)
{
    std::vector<FlowAddr> flowAddrs;
    auto ret = flow->GetAddrByOffset(sliceKey.flowOffset, sliceKey.length, flowAddrs);
    ChkTrueNot(ret == BIO_OK, nullptr);

    return MakeRef<WCacheSlice>(sliceKey.flowId, sliceKey.flowOffset, sliceKey.indexInFlow, sliceKey.length, flowAddrs, flow->GetFlowType());
}

inline WCacheSlicePtr WCacheTier::GetSlice(const FlowPtr &flow, uint64_t offset, uint64_t index, uint64_t length)
{
    std::vector<FlowAddr> flowAddrs;
    auto ret = flow->GetAddrByOffset(offset, length, flowAddrs);
    ChkTrueNot(ret == BIO_OK, nullptr);

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