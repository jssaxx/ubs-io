/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "wcache_tier.h"
#include "flow_manager.h"
#include "securec.h"
#include "cache_flow.h"
#include "bio_crc_util.h"
#include "bio_config_instance.h"
#include "cm.h"
#include "bio_trace.h"
#include "cache_overload_ctrl.h"

namespace ock {
namespace bio {
BResult WCacheTier::Init(WCacheTierType cacheTier, uint64_t flowId, uint16_t diskId)
{
    type = cacheTier;
    FlowType flowType;
    auto ret = ToFlowType(cacheTier, flowType);
    ChkTrueNot(ret == BIO_OK, ret);

    uint64_t metaInnerType = (cacheTier == WCACHE_MEMORY) ? WCACHE_FLOW_MEM_META_PREFIX : WCACHE_FLOW_DISK_META_PREFIX;
    uint64_t dataInnerType = (cacheTier == WCACHE_MEMORY) ? WCACHE_FLOW_MEM_DATA_PREFIX : WCACHE_FLOW_DISK_DATA_PREFIX;

    auto &flowManager = FlowManager::Instance();

    uint64_t metaFlowId = flowId | (metaInnerType << CACHE_FLOW_ID_PREFIX_SHIFT); /* meta */
    mMetaFlow = flowManager->CreateObject(FLOW_META, flowType, metaFlowId, diskId);
    ChkTrue(mMetaFlow != nullptr, BIO_ALLOC_FAIL,
        "Failed to create metaflow, flowType:" << flowType << " metaFlowId" << metaFlowId);

    LOG_INFO("Meta flowId:" << metaFlowId << ", flowType:" << flowType);

    uint64_t dataFlowId = flowId | (dataInnerType << CACHE_FLOW_ID_PREFIX_SHIFT); /* data */
    mDataFlow = flowManager->CreateObject(FLOW_DATA, flowType, dataFlowId, diskId);
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
    LVOS_TP_START(WCACHE_GET_MEM_SLICE_FAIL, &res, BIO_INNER_RETRY);
    res = GetSlice(mMetaFlow, metaFlowOffset, slice->GetIndexInFlow(), sizeof(WFlowSliceMeta), metaSlice);
    LVOS_TP_END;
    ChkTrue(res == BIO_OK, res, "Failed to get meta slice, flowId" <<
        mMetaFlow->GetFlowId() << " ret:" << res);
    WFlowSliceMeta sliceMeta{};
    auto ret = memcpy_s(sliceMeta.key, (NO_512 - NO_32), key, strlen(key));
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
    WCacheSlicePtr dataSlice;
    res = GetSlice(mDataFlow, slice->GetOffsetInFlow(), slice->GetIndexInFlow(),
        slice->GetLength(), dataSlice);
    ChkTrue(res == BIO_OK, res, "Failed to get data slice, flowId:" <<
        mDataFlow->GetFlowId() << " ret:" << res);
    ret = sliceReader(slice.Get(), dataSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    if (BioConfig::Instance()->GetDaemonConfig().enableCrc) {
        ret = dataSlice->VerifyDataCrc(slice->GetDataCrc(), 0, dataSlice->GetLength(), dataSlice.Get());
        if (ret != BIO_OK) {
            LOG_ERROR("Server put failed to verify the CRC fail, key:" << key << ", ret:" << ret);
            return ret;
        }
    }

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

void WCacheTier::AddEvictNegotiateIndexMap(uint64_t indexInFlow, uint8_t refNum)
{
    uint64_t indexInMap = indexInFlow / ARRAY_SIZE_IN_NEGOTIATE_MAP;
    uint64_t indexInArray = indexInFlow % ARRAY_SIZE_IN_NEGOTIATE_MAP;
    auto array = mNegotiateIndexMap.find(indexInMap);
    if (array == mNegotiateIndexMap.end()) {
        mNegotiateIndexMapLock.Lock();
        array = mNegotiateIndexMap.find(indexInMap);
        if (array == mNegotiateIndexMap.end()) {
            std::array<uint8_t, ARRAY_SIZE_IN_NEGOTIATE_MAP> newArray{};
            newArray.fill(INVALID_REF_NUM);
            mNegotiateIndexMap.emplace(indexInMap, newArray);
        }
        mNegotiateIndexMapLock.UnLock();
    }
    mNegotiateIndexMap[indexInMap][indexInArray] = refNum;
}


void WCacheTier::AddEvictNegotiateMap(WCacheSliceRefPtr &sliceRef)
{
    mEvictNegotiateMapLock.Lock();
    mEvictNegotiateMap.emplace(sliceRef->GetSlice()->GetIndexInFlow(), sliceRef);
    mEvictNegotiateMapLock.UnLock();
}

void WCacheTier::DelEvictNegotiateMap(WCacheReplicaSlicePtr repSlicePtr)
{
    mEvictNegotiateMapLock.Lock();
    mEvictNegotiateMap.erase(repSlicePtr->GeNegotiateOffset());
    mEvictNegotiateMapLock.UnLock();
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

bool WCacheTier::IsEmptyNegotiateMap()
{
    bool isEmpty;
    mEvictNegotiateMapLock.Lock();
    isEmpty = mEvictNegotiateMap.empty();
    mEvictNegotiateMapLock.UnLock();
    return isEmpty;
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

void WCacheTier::GetNegotiateSlice(std::vector<uint64_t> &indexVec, uint32_t limit)
{
    LOG_DEBUG("Begin get slice ,cur index :" << mCurNegotiateIndex << "flow:" << mMetaFlow->GetFlowId());
    uint64_t startIndexInMap = mCurNegotiateIndex / ARRAY_SIZE_IN_NEGOTIATE_MAP;
    uint64_t startIndexInArray = mCurNegotiateIndex % ARRAY_SIZE_IN_NEGOTIATE_MAP;
    for (uint64_t indexInMap = startIndexInMap;; ++indexInMap) {
        auto it = mNegotiateIndexMap.find(startIndexInMap);
        if (it == mNegotiateIndexMap.end()) {
            return;
        }
        auto array = it->second;
        for (uint64_t indexInArray = startIndexInArray; indexInArray < ARRAY_SIZE_IN_NEGOTIATE_MAP; ++indexInArray) {
            if (array[indexInArray] != INVALID_REF_NUM && array[indexInArray] > NO_U64_0) {
                LOG_DEBUG("Select this slice ,flow:" << mMetaFlow->GetFlowId() << ",indexInMap :"
                << indexInMap << ",indexInArray:" << indexInArray);
                indexVec.emplace_back(indexInMap * ARRAY_SIZE_IN_NEGOTIATE_MAP + indexInArray);
            } else {
                return;
            }
            if (indexVec.size() == limit) {
                return;
            }
        }
        startIndexInArray = 0;
    }
}

BResult WCacheTier::GetMetaSlice(uint64_t indexInFlow, WCacheSlicePtr &slice)
{
    mMetaFlow->GetFlowId();
    BResult ret = BIO_ERR;
    LVOS_TP_START(WCACHE_GET_META_SLICE_FAIL, ret, BIO_ERR);
    ret = GetSlice(mMetaFlow, indexInFlow * sizeof(WFlowSliceMeta), indexInFlow, sizeof(WFlowSliceMeta), slice);
    LVOS_TP_END;
    ChkTrue(ret == BIO_OK, ret,
        "Failed to get meta slice, flowId " << mMetaFlow->GetFlowId() << " indexInFlow:" << indexInFlow);
    return BIO_OK;
}

BResult WCacheTier::GetDataSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    BResult ret = GetSlice(mDataFlow, sliceKey, slice);
    ChkTrueNot(ret == BIO_OK, ret);
    return BIO_OK;
}

BResult WCacheTier::GetMetaDataSlice(uint64_t indexInFlow, uint64_t offset, uint64_t length,
    WFlowMetaDataSlice &metaDataSlice)
{
    BResult ret;
    WCacheSlicePtr metaSlice;
    ret = GetSlice(mMetaFlow, indexInFlow * sizeof(WFlowSliceMeta), indexInFlow, sizeof(WFlowSliceMeta),
        metaSlice);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to get slice, metaFlow id: " << mMetaFlow->GetFlowId() << " indexInFlow:" << indexInFlow);

    WCacheSlicePtr dataSlice;
    ret = GetSlice(mDataFlow, offset, indexInFlow, length, dataSlice);
    ChkTrue(ret == BIO_OK, ret,
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

void WCacheTier::Destroy()
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
}

BResult WCacheTier::Evict(const WCacheSlicePtr &slice)
{
    auto truncateSlice = mFlowTruncateCursor->GetTruncateSlice(slice);
    if (truncateSlice == nullptr) {
        return BIO_OK;
    }
    LOG_INFO("FlowId:" << truncateSlice->GetFlowId() << ", fLowType:" << truncateSlice->GetFlowType() <<
        ", flowOffset:" << truncateSlice->GetOffsetInFlow() << ", flowIndex:" << slice->GetIndexInFlow() <<
        ", len:" << truncateSlice->GetLength());

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

inline BResult WCacheTier::GetSlice(const FlowPtr &flow, const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    std::vector<FlowAddr> flowAddrs;
    auto ret = flow->GetAddrByOffset(sliceKey.flowOffset, sliceKey.length, flowAddrs);
    ChkTrueNot(ret == BIO_OK, ret);

    slice = MakeRef<WCacheSlice>(sliceKey.flowId, sliceKey.flowOffset, sliceKey.indexInFlow,
        sliceKey.length, flowAddrs, flow->GetFlowType());
    if (slice == nullptr) {
        return BIO_INNER_RETRY;
    }
    return BIO_OK;
}

inline BResult WCacheTier::GetSlice(const FlowPtr &flow, uint64_t offset, uint64_t index, uint64_t length,
    WCacheSlicePtr &slice)
{
    std::vector<FlowAddr> flowAddrs;
    auto ret = flow->GetAddrByOffset(offset, length, flowAddrs);
    ChkTrueNot(ret == BIO_OK, ret);

    slice = MakeRef<WCacheSlice>(flow->GetFlowId(), offset, index, length, flowAddrs, flow->GetFlowType());
    if (slice == nullptr) {
        return BIO_INNER_RETRY;
    }
    return BIO_OK;
}

WCacheSlicePtr WFlowTruncateCursor::GetTruncateSlice(const WCacheSlicePtr &slice)
{
    std::lock_guard<std::mutex> lock(mEvictedSliceListLock);

    LOG_TRACE("FlowId:" << slice->GetFlowId() << ", fLowType:" << slice->GetFlowType() << ", flowOffset:" <<
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

BResult WCacheTier::UpdateNegotiateState(uint64_t indexInFlow)
{
    BIO_TRACE_START(WCACHE_TRACE_NEGOTIATE_UPDATE)
    uint64_t indexInMap = mCurNegotiateIndex / ARRAY_SIZE_IN_NEGOTIATE_MAP;
    uint64_t indexInArray = mCurNegotiateIndex % ARRAY_SIZE_IN_NEGOTIATE_MAP;
    auto it = mNegotiateIndexMap.find(indexInMap);
    if (it == mNegotiateIndexMap.end()) {
        LOG_DEBUG("Not find slice,flow:" << mDataFlow->GetFlowId() << ",indexInFlow:" << indexInFlow);
        return BIO_NOT_EXISTS;
    }
    if (it->second[indexInArray] == INVALID_REF_NUM) {
        LOG_DEBUG("Invalid index,flow:" << mDataFlow->GetFlowId() << ",indexInFlow:" << indexInFlow);
        return BIO_NOT_EXISTS;
    }
    uint8_t curRefNum = --it->second[indexInArray];
    if (curRefNum == 0) {
        LOG_DEBUG("Negotiate success,flow:" << mDataFlow->GetFlowId() << ",indexInFlow:"
            << indexInFlow << ",curIndex:" << mCurNegotiateIndex);
        EvictNegotiateMapToQueue(indexInFlow);
        mCurNegotiateIndex = std::min(mCurNegotiateIndex, indexInFlow) + NO_1;
    }
    if (indexInArray == ARRAY_SIZE_IN_NEGOTIATE_MAP - NO_1) {
        DelEvictIndexArray(indexInMap);
    }
    BIO_TRACE_END(WCACHE_TRACE_NEGOTIATE_UPDATE, BIO_OK)
    return BIO_OK;
}

void WCacheTier::FlushNegotiateMap()
{
    mEvictNegotiateMapLock.Lock();
    auto size = mEvictNegotiateMap.size();
    while (!mEvictNegotiateMap.empty()) {
        auto it = mEvictNegotiateMap.begin();
        LOG_DEBUG("Flush mEvictNegotiateQueue, put evictQueue ,flowId:" <<it->second->GetSlice()->GetFlowId()
        << ", IndexInFlow:" <<it->second->GetSlice()->GetIndexInFlow());
        AddEvictQueue(it->second);
        mEvictNegotiateMap.erase(it);
    }
    mEvictNegotiateMapLock.UnLock();
    LOG_DEBUG("Flush mEvictNegotiateQueue ,size :" << size << ".");
}

void WCacheTier::EvictNegotiateMapToQueue(uint64_t indexInFlow)
{
    mEvictNegotiateMapLock.Lock();
    auto it = mEvictNegotiateMap.find(indexInFlow);
    if (it == mEvictNegotiateMap.end()) {
        mEvictNegotiateMapLock.UnLock();
        return;
    }
    BIO_TRACE_START(WCACHE_ADD_EVICT_QUEUE)
    AddEvictQueue(it->second);
    BIO_TRACE_END(WCACHE_ADD_EVICT_QUEUE, BIO_OK)
    mEvictNegotiateMap.erase(it);
    mEvictNegotiateMapLock.UnLock();
}

void WCacheTier::DelEvictIndexArray(uint64_t indexInMap)
{
    BIO_TRACE_START(WCACHE_DEL_NEGOTIATE_ARRAY)
    mNegotiateIndexMapLock.Lock();
    mNegotiateIndexMap.erase(indexInMap);
    LOG_DEBUG("Del array in map, index: " << indexInMap << ",flow:"<<mDataFlow->GetFlowId());
    mNegotiateIndexMapLock.UnLock();
    BIO_TRACE_END(WCACHE_DEL_NEGOTIATE_ARRAY, BIO_OK)
}
}
}