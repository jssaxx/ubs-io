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

BResult WCacheTier::Write(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef)
{
    // fill meta flow.
    BResult res;
    auto metaFlowOffset = slice->GetIndexInFlow() * sizeof(WFlowSliceMeta);
    WCacheSlicePtr metaSlice;
    LVOS_TP_START(WCACHE_GET_MEM_SLICE_FAIL, &metaSlice, nullptr);
    metaSlice = GetSlice(mMetaFlow, metaFlowOffset, slice->GetIndexInFlow(), sizeof(WFlowSliceMeta), res);
    LVOS_TP_END;
    ChkTrue(metaSlice != nullptr, res, "Failed to get meta slice, flowId" <<
        mMetaFlow->GetFlowId() << " ret:" << res);
    WFlowSliceMeta sliceMeta{};
    auto ret = memcpy_s(sliceMeta.key, NO_512, key, strlen(key));
    if (ret != 0) {
        return BIO_INNER_RETRY;
    }
    sliceMeta.offset = slice->GetOffsetInFlow();
    sliceMeta.length = slice->GetLength();
    sliceMeta.magic = slice->GetFlowId();
    sliceMeta.hasEvict = 0;
    ret = mSliceOperator.Copy(reinterpret_cast<const char *>(&sliceMeta), metaSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    // fill data flow.
    WCacheSlicePtr dataSlice = GetSlice(mDataFlow, slice->GetOffsetInFlow(), slice->GetIndexInFlow(),
        slice->GetLength(), res);
    ChkTrue(dataSlice != nullptr, res, "Failed to get data slice, flowId" <<
        mDataFlow->GetFlowId() << " ret:" << res);
    ret = sliceReader(slice.Get(), dataSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    destSliceRef = MakeRef<WCacheSliceRef>(dataSlice);
    ChkTrueNot(destSliceRef != nullptr, BIO_INNER_RETRY);
    return BIO_OK;
}

void WCacheTier::AddEvictQueue(WCacheSliceRefPtr sliceRef)
{
    mEvictSliceQueueLock.Lock();
    mEvictSliceQueue.emplace_back(sliceRef);
    mEvictSliceQueueLock.UnLock();
}

void WCacheTier::RetryEvictQueue(WCacheSliceRefPtr sliceRef)
{
    mEvictSliceQueueLock.Lock();
    mEvictSliceQueue.push_front(sliceRef);
    mEvictSliceQueueLock.UnLock();
}

void WCacheTier::DelEvictQueue(WCacheSliceRefPtr sliceRef)
{
    mEvictSliceQueueLock.Lock();
    mEvictSliceQueue.remove(sliceRef);
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

WCacheSliceRefPtr WCacheTier::GetEvictSlice()
{
    WCacheSliceRefPtr sliceRef = nullptr;
    mEvictSliceQueueLock.Lock();
    if (mEvictSliceQueue.empty()) {
        mEvictSliceQueueLock.UnLock();
        return nullptr;
    }
    sliceRef = mEvictSliceQueue.front();
    mEvictSliceQueue.pop_front();
    mEvictSliceQueueLock.UnLock();
    return sliceRef;
}

BResult WCacheTier::GetMetaSlice(uint64_t indexInFlow, WCacheSlicePtr &slice)
{
    mMetaFlow->GetFlowId();
    BResult ret = BIO_ERR;
    LVOS_TP_START(WCACHE_GET_META_SLICE_FAIL, slice, nullptr);
    slice = GetSlice(mMetaFlow, indexInFlow * sizeof(WFlowSliceMeta), indexInFlow, sizeof(WFlowSliceMeta), ret);
    LVOS_TP_END;
    ChkTrue(slice != nullptr, ret,
        "Failed to get meta slice, flowId " << mMetaFlow->GetFlowId() << " indexInFlow:" << indexInFlow);
    return BIO_OK;
}

BResult WCacheTier::GetDataSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    BResult ret;
    slice = GetSlice(mDataFlow, sliceKey, ret);
    ChkTrueNot(slice != nullptr, ret);
    return BIO_OK;
}

BResult WCacheTier::GetMetaDataSlice(uint64_t indexInFlow, uint64_t offset, uint64_t length,
    WFlowMetaDataSlice &metaDataSlice)
{
    BResult ret;
    auto metaSlice =
        GetSlice(mMetaFlow, indexInFlow * sizeof(WFlowSliceMeta), indexInFlow, sizeof(WFlowSliceMeta), ret);
    ChkTrue(metaSlice != nullptr, ret,
        "Failed to get slice, metaFlow id: " << mMetaFlow->GetFlowId() << " indexInFlow:" << indexInFlow);

    auto dataSlice = GetSlice(mDataFlow, offset, indexInFlow, length, ret);
    ChkTrue(dataSlice != nullptr, ret,
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

BResult WCacheTier::Seal()
{
    if (mMetaFlow != nullptr) {
        mMetaFlow->Seal();
    }

    if (mDataFlow != nullptr) {
        mDataFlow->Seal();
    }

    return BIO_OK;
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
        ", flowOffset:" << truncateSlice->GetOffsetInFlow() << ", flowIndex:" << slice->GetIndexInFlow() << ", len:" <<
        truncateSlice->GetLength());

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

inline WCacheSlicePtr WCacheTier::GetSlice(const FlowPtr &flow, const SliceKey &sliceKey, BResult &ret)
{
    std::vector<FlowAddr> flowAddrs;
    ret = flow->GetAddrByOffset(sliceKey.flowOffset, sliceKey.length, flowAddrs);
    ChkTrueNot(ret == BIO_OK, nullptr);

    return MakeRef<WCacheSlice>(sliceKey.flowId, sliceKey.flowOffset, sliceKey.indexInFlow, sliceKey.length, flowAddrs,
        flow->GetFlowType());
}

inline WCacheSlicePtr WCacheTier::GetSlice(const FlowPtr &flow, uint64_t offset, uint64_t index, uint64_t length,
    BResult &ret)
{
    std::vector<FlowAddr> flowAddrs;
    ret = flow->GetAddrByOffset(offset, length, flowAddrs);
    ChkTrueNot(ret == BIO_OK, nullptr);

    return MakeRef<WCacheSlice>(flow->GetFlowId(), offset, index, length, flowAddrs, flow->GetFlowType());
}

WCacheSlicePtr WFlowTruncateCursor::GetTruncateSlice(const WCacheSlicePtr &slice)
{
    std::lock_guard<std::mutex> lock(mEvictedSliceListLock);

    LOG_DEBUG("FlowId:" << slice->GetFlowId() << ", fLowType:" << slice->GetFlowType() << ", flowOffset:" <<
        slice->GetOffsetInFlow() << ", flowIndex:" << slice->GetIndexInFlow() << ", len:" << slice->GetLength());

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