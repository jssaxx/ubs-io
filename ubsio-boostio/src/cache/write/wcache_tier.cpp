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

#include "wcache_tier.h"
#include <unistd.h>
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
BResult WCacheTier::Init(WCacheTierType cacheTier, uint64_t flowId, uint16_t diskId, bool useCompactMeta)
{
    type = cacheTier;
    mUseCompactMeta = useCompactMeta;
    mMetaEntrySize = mUseCompactMeta ? sizeof(WFlowCompactSliceMeta) : sizeof(WFlowSliceMeta);
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

    uint64_t preTruncateSliceIndex =
        (mMetaFlow->GetTruncateOffset() + mMetaEntrySize - 1) / mMetaEntrySize;
    mFlowTruncateCursor = MakeRef<WFlowTruncateCursor>(preTruncateSliceIndex);
    ChkTrueNot(mFlowTruncateCursor != nullptr, BIO_ALLOC_FAIL);

    return BIO_OK;
}

BResult WCacheTier::Write(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader,
    WCacheSliceRefPtr &destSliceRef)
{
    // fill meta flow.
    BResult res;
    if (slice->GetIndexInFlow() != 0 && UINT64_MAX / slice->GetIndexInFlow() < mMetaEntrySize) {
        LOG_ERROR("Index in flow error, index:" << slice->GetIndexInFlow() << ", flowMetaSize:" <<
            mMetaEntrySize << ".");
        return BIO_INNER_RETRY;
    }
    auto metaFlowOffset = slice->GetIndexInFlow() * mMetaEntrySize;
    WCacheSlicePtr metaSlice;
    BIO_TP_START(WCACHE_GET_MEM_SLICE_FAIL, &res, BIO_INNER_RETRY);
    res = GetSlice(mMetaFlow, metaFlowOffset, slice->GetIndexInFlow(), mMetaEntrySize, metaSlice);
    BIO_TP_END;
    ChkTrue(res == BIO_OK, res, "Failed to get meta slice, flowId" <<
        mMetaFlow->GetFlowId() << " ret:" << res);
    BResult ret = BIO_OK;
    if (mUseCompactMeta) {
        WFlowCompactSliceMeta sliceMeta{};
        ret = memcpy_s(sliceMeta.key, sizeof(sliceMeta.key), key, strlen(key) + 1UL);
        ChkTrue(ret == BIO_OK, BIO_INNER_RETRY, "Copy compact wcache key failed, keyLength:" << strlen(key) << ".");
        ret = mSliceOperator.Copy(reinterpret_cast<const char *>(&sliceMeta), metaSlice.Get());
    } else {
        WFlowSliceMeta sliceMeta{};
        ret = memcpy_s(sliceMeta.key, sizeof(sliceMeta.key), key, strlen(key) + 1UL);
        ChkTrue(ret == BIO_OK, BIO_INNER_RETRY, "Copy wcache key failed, keyLength:" << strlen(key) << ".");
        sliceMeta.offset = slice->GetOffsetInFlow();
        sliceMeta.length = slice->GetLength();
        sliceMeta.magic = slice->GetFlowId();
        sliceMeta.hasEvict = 0;
        ret = mSliceOperator.Copy(reinterpret_cast<const char *>(&sliceMeta), metaSlice.Get());
    }
    ChkTrueNot(ret == BIO_OK, ret);

    // fill data flow.
    WCacheSlicePtr dataSlice;
    res = GetSlice(mDataFlow, slice->GetOffsetInFlow(), slice->GetIndexInFlow(),
        slice->GetLength(), dataSlice);
    ChkTrue(res == BIO_OK, res, "Failed to get data slice, flowId:" <<
        mDataFlow->GetFlowId() << " ret:" << res);
    BIO_TRACE_START(WCACHE_SLICE_READER);
    ret = sliceReader(slice.Get(), dataSlice.Get());
    BIO_TRACE_END(WCACHE_SLICE_READER, ret);
    ChkTrueNot(ret == BIO_OK, ret);

    if (BioConfig::Instance()->GetDaemonConfig().enableCrc) {
        ret = dataSlice->VerifyDataCrc(slice->GetDataCrc(), 0, dataSlice->GetLength(), dataSlice.Get());
        if (ret != BIO_OK) {
            LOG_ERROR("Server put failed to verify the CRC fail, key:" << key << ", ret:" << ret);
            return ret;
        }
    }

    destSliceRef = MakeRef<WCacheSliceRef>(dataSlice, SLICE_PENDING);
    ChkTrueNot(destSliceRef != nullptr, BIO_INNER_RETRY);
    LOG_DEBUG("Wcache write memory success, key: " << key << ".");
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

uint64_t WCacheTier::GetTruncateIndex()
{
    return mFlowTruncateCursor->GetPreTruncateSliceIndex();
}

BResult WCacheTier::GetMetaSlice(uint64_t indexInFlow, WCacheSlicePtr &slice)
{
    BResult ret = BIO_ERR;
    BIO_TP_START(WCACHE_GET_META_SLICE_FAIL, ret, BIO_ERR);
    ret = GetSlice(mMetaFlow, indexInFlow * mMetaEntrySize, indexInFlow, mMetaEntrySize, slice);
    BIO_TP_END;
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
    WCacheSlicePtr metaSlice;
    BResult ret = GetSlice(mMetaFlow, indexInFlow * mMetaEntrySize, indexInFlow, mMetaEntrySize, metaSlice);
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
    BResult ret = BIO_OK;
#ifdef DEBUG_UT
    BIO_TP_START(FLOW_DATA_FLOW_ERR, &ret, BIO_ERR);
    BIO_TP_END;
    if (ret == BIO_OK) {
        mMetaFlow = MakeRef<Flow>(FLOW_META, FLOW_MEMORY, 0, 0, 0, 0);
    } else {
        mDataFlow = MakeRef<Flow>(FLOW_DATA, FLOW_MEMORY, 0, 0, 0, 0);
    }
#endif
    if (mMetaFlow != nullptr) {
        ret = mMetaFlow->Seal();
        if (ret != BIO_OK) {
            LOG_ERROR("Wcache tier seal metaflow failed, ret " << ret);
            return ret;
        }
    }

    if (mDataFlow != nullptr) {
        ret = mDataFlow->Seal();
        if (ret != BIO_OK) {
            LOG_ERROR("Wcache tier seal dataflow failed, ret " << ret);
            return ret;
        }
    }

    return ret;
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

BResult WCacheTier::ReleaseFaultedResources()
{
    FlowPtr *flows[] = { &mMetaFlow, &mDataFlow };
    for (const auto *flowPtr : flows) {
        const FlowPtr &flow = *flowPtr;
        if (flow == nullptr) {
            continue;
        }
        // The tier and FlowManager own two stable references. Any extra reference is an existing preload task.
        while (flow->GetRef() > 2) {
            usleep(1000);
        }
        BResult ret = BIO_OK;
        if (type == WCACHE_MEMORY) {
            ret = flow->Seal();
            if (LIKELY(ret == BIO_OK)) {
                ret = FlowManager::Instance()->DestroyObject(flow->GetFlowType(), flow->GetFlowId());
            }
        } else {
            ret = FlowManager::Instance()->AbandonObject(flow->GetFlowId());
        }
        if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
            return ret;
        }
    }
    return BIO_OK;
}

BResult WCacheTier::Evict(const WCacheSlicePtr &slice)
{
    if (slice == nullptr) {
        LOG_ERROR("slice is null.");
        return BIO_INNER_ERR;
    }
    uint64_t preTruncateSliceIndex = 0;
    auto truncateSlice = mFlowTruncateCursor->GetTruncateSlice(slice, preTruncateSliceIndex);
    if (truncateSlice == nullptr) {
        return BIO_OK;
    }

    auto ret = mMetaFlow->TruncateOffset(preTruncateSliceIndex * mMetaEntrySize);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to truncateOffset in metaFlow, FlowId:" << truncateSlice->GetFlowId() << ", fLowType:" <<
        truncateSlice->GetFlowType() << ", flowOffset:" << truncateSlice->GetOffsetInFlow() << ", flowIndex:" <<
        slice->GetIndexInFlow() << ", len:" << truncateSlice->GetLength());

    ret = mDataFlow->TruncateOffset(truncateSlice->GetOffsetInFlow() + truncateSlice->GetLength());
    ChkTrue(ret == BIO_OK, ret,
        "Failed to truncateOffset in dataFlow, FlowId:" << truncateSlice->GetFlowId() << ", fLowType:" <<
        truncateSlice->GetFlowType() << ", flowOffset:" << truncateSlice->GetOffsetInFlow() << ", flowIndex:" <<
        slice->GetIndexInFlow() << ", len:" << truncateSlice->GetLength());
    LOG_DEBUG("Evict success, flowId:" << truncateSlice->GetFlowId() << ", fLowType:" << truncateSlice->GetFlowType() <<
        ", flowOffset:" << truncateSlice->GetOffsetInFlow() << ", flowIndex:" << slice->GetIndexInFlow() << ", len:" <<
        truncateSlice->GetLength());

    return BIO_OK;
}

void WCacheTier::MarkEvictedIndex(uint64_t indexInFlow)
{
    mFlowTruncateCursor->MarkEvictedIndex(indexInFlow);
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

bool WCacheTier::IsEmptyCursorEvictSlices()
{
    return mFlowTruncateCursor->IsEmptyEvictSlices();
}

void  WCacheTier::SetGlobMinTruncateIndex(uint64_t globMinTruncateIndex)
{
    mFlowTruncateCursor->SetGlobMinTruncateIndex(globMinTruncateIndex);
}

void WCacheTier::SetIsNormal(bool isNormal)
{
    mFlowTruncateCursor->SetIsNormal(isNormal);
}

WCacheSlicePtr WFlowTruncateCursor::GetTruncateSlice(const WCacheSlicePtr &slice)
{
    uint64_t preTruncateSliceIndex = 0;
    return GetTruncateSlice(slice, preTruncateSliceIndex);
}

WCacheSlicePtr WFlowTruncateCursor::GetTruncateSlice(const WCacheSlicePtr &slice,
    uint64_t &preTruncateSliceIndex)
{
    std::lock_guard<std::mutex> lock(mEvictedSliceListLock);
    preTruncateSliceIndex = mPreTruncateSliceIndex;
    if (slice == nullptr) {
        LOG_ERROR("Get truncate slice failed, slice is null.");
        return nullptr;
    }
    LOG_DEBUG("FlowId:" << slice->GetFlowId() << ", fLowType:" << slice->GetFlowType() << ", flowOffset:" <<
                        slice->GetOffsetInFlow() << ", flowIndex:" << slice->GetIndexInFlow() <<
                        ", len:" << slice->GetLength());

    // insert to set and sort by indexInFlow.
    uint64_t sliceIndex = slice->GetIndexInFlow();
    if (sliceIndex == NO_MAX_VALUE64) {
        LOG_ERROR("Get truncate slice failed, index reaches uint64 max, flowId:" << slice->GetFlowId());
        return nullptr;
    }
    if (sliceIndex >= mPreTruncateSliceIndex) {
        mEvictedSlices.emplace(slice);
    } else {
        LOG_DEBUG("Ignore stale evicted slice, flowId:" << slice->GetFlowId() << ", flowIndex:" << sliceIndex <<
            ", truncateIndex:" << mPreTruncateSliceIndex);
    }

    // obtain the last truncate slice from the indexInFlow that is truncated last time.
    WCacheSlicePtr truncateSlice = nullptr;
    uint64_t truncateSliceIndex = mPreTruncateSliceIndex;
    size_t maxProcessCount = mEvictedSlices.size() + mEvictedIndexes.size();
    for (size_t processedCount = 0; processedCount < maxProcessCount; ++processedCount) {
        auto evictSliceIt = mEvictedSlices.begin();
        if (evictSliceIt != mEvictedSlices.end() &&
            evictSliceIt->Get()->GetIndexInFlow() < truncateSliceIndex) {
            mEvictedSlices.erase(evictSliceIt);
            continue;
        }

        auto evictedIndexIt = mEvictedIndexes.begin();
        if (evictedIndexIt != mEvictedIndexes.end() && *evictedIndexIt < truncateSliceIndex) {
            mEvictedIndexes.erase(evictedIndexIt);
            continue;
        }

        if (truncateSliceIndex == NO_MAX_VALUE64) {
            LOG_ERROR("Stop advancing truncate cursor at uint64 max.");
            break;
        }

        if (evictSliceIt != mEvictedSlices.end() &&
            evictSliceIt->Get()->GetIndexInFlow() == truncateSliceIndex) {
            truncateSlice = evictSliceIt->Get();
            mEvictedSlices.erase(evictSliceIt);
            ++truncateSliceIndex;
            mPreTruncateSliceIndex = truncateSliceIndex;
            continue;
        }

        if (evictedIndexIt == mEvictedIndexes.end() || *evictedIndexIt != truncateSliceIndex) {
            break;
        }
        mEvictedIndexes.erase(evictedIndexIt);
        ++truncateSliceIndex;
        mPreTruncateSliceIndex = truncateSliceIndex;
    }

    preTruncateSliceIndex = mPreTruncateSliceIndex;
    return truncateSlice;
}

void WFlowTruncateCursor::MarkEvictedIndex(uint64_t indexInFlow)
{
    std::lock_guard<std::mutex> lock(mEvictedSliceListLock);
    if (indexInFlow == NO_MAX_VALUE64) {
        LOG_ERROR("Mark evicted index failed, index reaches uint64 max.");
        return;
    }
    if (indexInFlow < mPreTruncateSliceIndex) {
        return;
    }
    mEvictedIndexes.emplace(indexInFlow);
    while (mPreTruncateSliceIndex != NO_MAX_VALUE64 &&
        mEvictedIndexes.erase(mPreTruncateSliceIndex) > 0) {
        ++mPreTruncateSliceIndex;
    }
}

inline uint64_t WFlowTruncateCursor::GetPreTruncateSliceIndex()
{
    return mPreTruncateSliceIndex;
}

void WFlowTruncateCursor::SetGlobMinTruncateIndex(uint64_t globMinTruncateIndex)
{
    mGlobTruncateIndex = globMinTruncateIndex;
}

inline bool WFlowTruncateCursor::IsEmptyEvictSlices()
{
    return mEvictedSlices.empty() && mEvictedIndexes.empty();
}

void WFlowTruncateCursor::SetIsNormal(bool isNormal)
{
    mIsNormal = isNormal;
}
}
}
