/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <cstdint>
#include "rcache.h"
#include "cache_flow.h"
#include "cache_slice.h"
#include "rcache_statistic.h"
#include "underfs.h"
#include "bio_trace.h"

using namespace ock::bio;

RCache::RCache(uint64_t ptId, uint16_t diskId):mPtId(ptId), mDiskId(diskId)
{
    for (int32_t tier = 0; tier < READ_CACHE_TIER_BUTT; tier++) {
        flow[tier] = nullptr;
        flow[tier] = nullptr;
        cacheData[tier] = 0ULL;
        gcData[tier] = 0ULL;
    }
}

RCache::~RCache()
{
    for (int32_t tier = 0; tier < READ_CACHE_TIER_BUTT; tier++) {
        flow[tier] = nullptr;
        flow[tier] = nullptr;
        cacheData[tier] = 0ULL;
        gcData[tier] = 0ULL;
    }
}

BResult RCache::InsertToIndex(const Key &key, RCacheChunkPtr &chunk)
{
    uint32_t bucket = GetHashBucketByKey(key);

    indexLock[bucket].Lock();
    auto iter = index[bucket].find(key);
    if (UNLIKELY(iter != index[bucket].end())) {
        indexLock[bucket].UnLock();
        LOG_ERROR("Insert read cache key " << key << "have exist, can not insert.");
        return BIO_EXISTS;
    }

    index[bucket][key] = chunk;
    indexLock[bucket].UnLock();

    return BIO_OK;
}

BResult RCache::DeleteFromIndex(const Key &key)
{
    uint32_t bucket = GetHashBucketByKey(key);

    indexLock[bucket].Lock();
    auto iter = index[bucket].find(key);
    if (iter == index[bucket].end()) {
        indexLock[bucket].UnLock();
        LOG_INFO("Delete read cache key:" << key << "have not exist.");
        return BIO_NOT_EXISTS;
    }
    index[bucket].erase(iter);
    indexLock[bucket].UnLock();
    return BIO_OK;
}

void RCache::AddToEvictList(RCacheTierType tierType, MqType mType, RCacheChunkPtr &chunk)
{
    evictMqLock[tierType][mType].Lock();
    evictMq[tierType][mType].PushBack(chunk);
    evictMqLock[tierType][mType].UnLock();
}

void RCache::DelFromEvictList(RCacheTierType tierType, MqType mType, RCacheChunkPtr &chunk)
{
    evictMqLock[tierType][mType].Lock();
    evictMq[tierType][mType].Remove(chunk);
    evictMqLock[tierType][mType].UnLock();
}

void RCache::AddToTruncateList(RCacheTierType tierType, RCacheChunkPtr &chunk)
{
    truncateLock[tierType].Lock();
    if (truncateQ[tierType].IsEmpty()) {
        truncateQ[tierType].PushBack(chunk);
        truncateLock[tierType].UnLock();
        return;
    }

    for (auto iter = truncateQ[tierType].Begin(); iter != nullptr;) {
        if (chunk->GetValue().indexInFlow < iter->GetValue().indexInFlow) {
            iter = iter->next[tierType];
            continue;
        }
        truncateQ[tierType].InsertAt((iter), chunk);
        truncateLock[tierType].UnLock();
        return;
    }

    truncateQ[tierType].PushBack(chunk);
    truncateLock[tierType].UnLock();
}

uint32_t RCache::GetHashBucketByKey(const Key &key)
{
    return (std::hash<std::string>{}(key)) & READ_CACHE_META_HASH_BUCKET_MASK;
}

FlowType RCache::GetFlowTypeByTierType(RCacheTierType tierType)
{
   if (tierType == READ_CACHE_TIER_MEM) {
       return FLOW_MEMORY;
   } else {
       return FLOW_DISK;
   }
}

BResult RCache::CreateRCacheFlow(RCacheTierType tier, std::vector<uint64_t> flowIds)
{
    flow[tier] = MakeRef<RCacheFlow>();
    if (UNLIKELY(flow[tier] == nullptr)) {
        LOG_ERROR("Alloc ptId" << mPtId << " read cache memory meta flow failed.");
        return BIO_ERR;
    }

    int32_t ret = flow[tier]->Initialize(mPtId, mDiskId, GetFlowTypeByTierType(tier), flowIds);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Init ptId" << mPtId << " memory meta flow failed, error code " << ret);
        Destroy();
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult RCache::Initialize()
{
    std::vector<uint32_t> prefix;
    std::vector<uint64_t> flowIds;
    std::vector<uint64_t> tempIds;

    uint32_t flowPrefix;
    for (uint32_t i = READ_CACHE_FLOW_MEM_META_PREFIX; i <= READ_CACHE_FLOW_DISK_DATA_PREFIX; i++) {
        flowPrefix = CacheFlowIdManager::GenerateCacheFlowIdPrefix(mPtId, CACHE_FLOW_ID_PREFIX_TYPE_RCACHE, i);
        prefix.push_back(flowPrefix);
    }

    FlowIdAllocator::Instance()->GenerateFlowIds(prefix, flowIds);
    if(UNLIKELY(flowIds.empty() || (flowIds.size() != READ_CACHE_FLOW_DISK_DATA_PREFIX +1))) {
        LOG_ERROR("Generate ptId" << mPtId << "flow ids failed.");
        return BIO_ERR;
    }

    tempIds.push_back(flowIds.at(READ_CACHE_FLOW_MEM_META_PREFIX));
    tempIds.push_back(flowIds.at(READ_CACHE_FLOW_MEM_DATA_PREFIX));
    auto ret = CreateRCacheFlow(READ_CACHE_TIER_MEM, tempIds);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Init ptId" << mPtId << " memory meta flow failed, error code " << ret);
        Destroy();
        return BIO_ERR;
    }

    tempIds.clear();
    tempIds.push_back(flowIds.at(READ_CACHE_FLOW_DISK_META_PREFIX));
    tempIds.push_back(flowIds.at(READ_CACHE_FLOW_DISK_DATA_PREFIX));
    ret = CreateRCacheFlow(READ_CACHE_TIER_DISK, tempIds);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Init ptId" << mPtId << "disk meta flow failed, error code " << ret);
        Destroy();
        return BIO_ERR;
    }

    for (auto & i : truncateQ) {
        i.Initialize(RCACHE_TRUNK_LIST_TYPE_TRUNCATE);
    }

    for (auto & i : evictMq) {
        for (auto & j : i) {
            j.Initialize(RCACHE_TRUNK_LIST_TYPE_EVICT);
        }
    }

    return BIO_OK;
}

BResult RCache::Destroy()
{
    BResult ret;
    for (int32_t tier = 0; tier < READ_CACHE_TIER_BUTT; tier++) {
        if (flow[tier] != nullptr) {
            ret = flow[tier]->Destroy();
            if (ret != BIO_OK) {
                LOG_ERROR("Destroy pt id " << mPtId << "tier type" << tier << "flow failed, error code " << ret);
            }
        }

        for (uint32_t i = 0; i < MQ_TYPE_BUTT; i++) {
            evictMqLock[tier][i].Lock();
            while (!evictMq[tier][i].IsEmpty()) {
                evictMq[tier][i].PopFront();
            }
            evictMqLock[tier][i].UnLock();
        }

        truncateLock[tier].Lock();
        while (!truncateQ[tier].IsEmpty()) {
            truncateQ[tier].PopFront();
        }
        truncateLock[tier].UnLock();
    }

    for (uint32_t i = 0; i < READ_CACHE_META_HASH_BUCKET_NUM; i++) {
        indexLock[i].Lock();
        for (auto iter = index[i].begin(); iter != index[i].end();iter++) {
            index[i].erase(iter);
        }
        indexLock[i].UnLock();
    }

    return BIO_OK;
}

BResult RCache::AllocChunk(const Key key, const RCacheValue value, RCacheChunkPtr &chunk)
{
    uint32_t keyLen = strlen(key) + 1;

    Key chunkKey = new(std::nothrow) char[keyLen];
    if (UNLIKELY(chunkKey == nullptr)) {
        LOG_ERROR("Alloc chunk key memory failed.");
        return BIO_ALLOC_FAIL;
    }

    (void)memcpy_s(chunkKey, keyLen, key, keyLen);

    chunk = MakeRef<RCacheChunk>(chunkKey, value);
    if (UNLIKELY(chunk == nullptr)) {
        delete [] chunkKey;
        LOG_ERROR("Alloc chunk for read cache key failed.");
        return BIO_ALLOC_FAIL;
    }

   return BIO_OK;
}

BResult RCache::GetSliceFromChunkIO(RCacheTierType tier, const RCacheChunkPtr &chunk, WCacheSlicePtr &slicePtr,
    uint64_t offset, uint64_t len, uint64_t &realLen)
{
    RCacheValue value = chunk->GetValue();
    std::vector<FlowAddr> flowAdd;

    if (UNLIKELY(offset >= value.length)) {
        LOG_ERROR("Flow offset:" << value.flowOffset << ", length:" << value.length <<
            ", input offset:" << offset << ", len:" << len);
        return BIO_INVALID_PARAM;
    }

    realLen = value.length - offset;
    if (realLen > len) {
        realLen = len;
    }

    BResult ret = flow[tier]->GetDataFlow()->GetAddrByOffset(value.flowOffset + offset, realLen, flowAdd);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get key: " << chunk->GetKey() << ", tier:" << tier << ", flowOffset:" << value.flowOffset <<
            ", length:" << value.length << " failed.");
        return ret;
    }

    uint64_t flowId = flow[tier]->GetDataFlow()->GetFlowId();
    FlowType flowType = GetFlowTypeByTierType(tier);
    slicePtr = MakeRef<WCacheSlice>(flowId, value.flowOffset + offset, 0ULL, realLen, flowAdd, flowType);
    if (UNLIKELY(slicePtr == nullptr)) {
        LOG_ERROR("Alloc slice ptr for read cache failed.");
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult RCache::GetSliceFromChunk(RCacheTierType tier, const RCacheChunkPtr &chunk, WCacheSlicePtr &slicePtr)
{
    RCacheValue value = chunk->GetValue();
    std::vector<FlowAddr> flowAdd;

    BResult ret = flow[tier]->GetDataFlow()->GetAddrByOffset(value.flowOffset, value.length, flowAdd);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get key " << chunk->GetKey() << " tier " << tier << " offset " << value.flowOffset <<
            " len " << value.length << " failed.");
        return ret;
    }

    uint64_t flowId = flow[tier]->GetDataFlow()->GetFlowId();
    FlowType flowType = GetFlowTypeByTierType(tier);
    slicePtr = MakeRef<WCacheSlice>(flowId, value.flowOffset, 0ULL, value.length, flowAdd, flowType);
    if (UNLIKELY(slicePtr == nullptr)) {
        LOG_ERROR("Alloc slice ptr for read cache failed.");
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult RCache::AllocResources(uint64_t length, WCacheSlicePtr &slice)
{
    uint64_t flowId = flow[READ_CACHE_TIER_MEM]->GetDataFlow()->GetFlowId();
    uint64_t offset;
    uint64_t indexInFlow;
    std::vector<FlowAddr> flowAdd;

    flow[READ_CACHE_TIER_MEM]->AllocOffset(length, offset, indexInFlow);
    BResult ret = flow[READ_CACHE_TIER_MEM]->GetDataFlow()->GetAddrByOffset(offset, length, flowAdd);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get tier:" << READ_CACHE_TIER_MEM << ", offset:" << offset << ", len:" <<
            length << ", flow address failed.");
        return ret;
    }

    slice = MakeRef<WCacheSlice>(flowId, offset, indexInFlow, length, flowAdd, FLOW_MEMORY);
    if (UNLIKELY(slice == nullptr)) {
        LOG_ERROR("Alloc slice memory for read cache failed.");
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult RCache::Put(const Key &key, const WCacheSlicePtr &slice)
{
    RCacheValue value(slice->GetIndexInFlow(), slice->GetOffsetInFlow(), slice->GetLength());

    RCacheChunkPtr chunk = nullptr;
    auto ret = AllocChunk(key, value, chunk);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Alloc chunk for read cache key " << key << "failed.");
        return BIO_ERR;
    }
    chunk->SetMqType(MQ_COLD);
    chunk->SetTierType(READ_CACHE_TIER_MEM);

    LOG_INFO("Read cache Put, key:" << chunk->GetKey() << ", type:" << chunk->GetTierType() << ", length:" <<
        chunk->GetValue().length << ", flowoffset:" << chunk->GetValue().flowOffset << ", indexofflow:" <<
        chunk->GetValue().indexInFlow);

    ret = InsertToIndex(chunk->GetKey(), chunk);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Insert read cache key " << key << " to index failed.");
        return BIO_ERR;
    }

    AddToEvictList(READ_CACHE_TIER_MEM, MQ_COLD, chunk);
    AddToTruncateList(READ_CACHE_TIER_MEM, chunk);

    IncCacheData(READ_CACHE_TIER_MEM, chunk->GetValue().length);
    return BIO_OK;
}

BResult RCache::Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
    uint64_t &realLen)
{
    uint32_t bucket = GetHashBucketByKey(key);
    WCacheSlicePtr newSlicePtr = nullptr;

    indexLock[bucket].Lock();
    auto iter = index[bucket].find(key);
    if (iter == index[bucket].end()) {
        indexLock[bucket].UnLock();
        RCacheStatistic::Instance().IncMisCount();
        return BIO_NOT_EXISTS;
    }
    RCacheChunkPtr chunk = iter->second;
    indexLock[bucket].UnLock();

    chunk->lock.lock();
    if (chunk->GetState() != 0) {
        LOG_ERROR("Already delete, " << key);
        chunk->lock.unlock();
        return BIO_NOT_EXISTS;
    }

    RCacheStatistic::Instance().IncHisCount();
    auto tier = chunk->GetTierType();
    auto ret = GetSliceFromChunkIO(tier, chunk, newSlicePtr, offset, slice->GetLength(), realLen);
    if (UNLIKELY(ret != BIO_OK) || UNLIKELY(newSlicePtr == nullptr)) {
        LOG_ERROR("Alloc slice for read cache key:" << key << ", type:" << tier << ", length:"
        	<< chunk->GetValue().length << ", flowoffset:" << chunk->GetValue().flowOffset
        	<< ", indexofflow:" << chunk->GetValue().indexInFlow);
        chunk->lock.unlock();
        return BIO_ALLOC_FAIL;
    }

    ret = sliceWriter(newSlicePtr.Get(), slice.Get());
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Write read cache key:" << key << "data to buffer failed.");
        chunk->lock.unlock();
        return BIO_INNER_ERR;
    }
    chunk->lock.unlock();

    auto mqType = chunk->GetMqType();
    evictMqLock[tier][mqType].Lock();
    evictMq[tier][mqType].Remove(chunk);
    evictMq[tier][mqType].PushBack(chunk);
    evictMqLock[tier][mqType].UnLock();
    return BIO_OK;
}

BResult RCache::Load(const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen)
{
    UnderFsPtr underFsPtr = UnderFs::Instance();
    WCacheSlicePtr toSlicePtr = nullptr;

    RCacheChunkPtr chunk = nullptr;
    uint64_t flowOffset;
    uint64_t indexInFlow;

    UnderFs::ObjStat stat;
    auto ret = underFsPtr->Stat(key, stat);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get key " << key << " stat from under fs failed, error code: " << ret);
        return ret;
    }
    uint64_t totalLen = stat.size;

    if (UNLIKELY(offset >= totalLen)) {
        LOG_ERROR("Input offset:" << offset << ", len:" << len << ", realLen:" << totalLen);
        return BIO_INVALID_PARAM;
    }

    realLen = totalLen - offset;
    if (realLen > len) {
        realLen = len;
    }

    flow[READ_CACHE_TIER_MEM]->AllocOffset(realLen, flowOffset, indexInFlow);
    RCacheValue chunkValue(indexInFlow, flowOffset, realLen);
    ret = AllocChunk(key, chunkValue, chunk);
    if (UNLIKELY(ret != BIO_OK) || UNLIKELY(chunk == nullptr)) {
        LOG_ERROR("Alloc chunk for read cache key " << key << "failed.");
        return BIO_ALLOC_FAIL;
    }

    ret = GetSliceFromChunk(READ_CACHE_TIER_MEM, chunk, toSlicePtr);
    if (UNLIKELY(ret != BIO_OK) || UNLIKELY(toSlicePtr == nullptr)) {
        LOG_ERROR("Alloc slice for read cache key " << key << "failed.");
        return BIO_ALLOC_FAIL;
    }

    char *value = new(std::nothrow) char[realLen];
    if (UNLIKELY(value == nullptr)) {
        LOG_ERROR("Alloc memory for key " << key << "failed.");
        return BIO_ALLOC_FAIL;
    }

    ret = underFsPtr->Get(key, value, realLen, offset); // 先调用桩接口
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Read data from under fs key " << key << "failed.");
        delete [] value;
        return BIO_ALLOC_FAIL;
    }

    ret = mSliceOperator.Copy(value, toSlicePtr.Get());
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Read key "<< key << " data to read cache failed.");
        delete [] value;
        return BIO_ALLOC_FAIL;
    }

    delete [] value;

    chunk->SetMqType(MQ_COLD);
    chunk->SetTierType(READ_CACHE_TIER_MEM);

    ret = InsertToIndex(chunk->GetKey(), chunk);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Insert read cache key" << key << "to index failed.");
        return BIO_ERR;
    }

    AddToEvictList(READ_CACHE_TIER_MEM, MQ_COLD, chunk);
    AddToTruncateList(READ_CACHE_TIER_MEM, chunk);
    IncCacheData(READ_CACHE_TIER_MEM, chunk->GetValue().length);
    return BIO_OK;
}

BResult RCache::Delete(const Key &key)
{
    uint32_t bucket = GetHashBucketByKey(key);
    RCacheChunkPtr chunk = nullptr;

    indexLock[bucket].Lock();
    auto iter = index[bucket].find(key);
    if (UNLIKELY(iter == index[bucket].end())) {
        indexLock[bucket].UnLock();
        LOG_INFO("Get read cache key:" << key << " have not exist.");
        return BIO_NOT_EXISTS;
    }
    chunk = iter->second;
    chunk->SetState(1);
    indexLock[bucket].UnLock();

    DelFromEvictList(chunk->GetTierType(), chunk.Get()->GetMqType(), chunk);

    IncGCData(chunk->GetTierType(), chunk->GetValue().length);
    return BIO_OK;
}

BResult RCache::EvictMemData(const uint64_t needEvictData, uint64_t &haveEvictData)
{
    haveEvictData = 0ULL;

    RCacheChunkPtr chunk;
    RCacheChunkPtr newChunk;
    WCacheSlicePtr fromSlicePtr = nullptr;
    WCacheSlicePtr toSlicePtr = nullptr;
    SlicePtr from;
    SlicePtr to;

    while (haveEvictData < needEvictData) {
        truncateLock[READ_CACHE_TIER_MEM].Lock();
        if (truncateQ[READ_CACHE_TIER_MEM].IsEmpty()) {
            truncateLock[READ_CACHE_TIER_MEM].UnLock();
            break;
        }
        chunk = truncateQ[READ_CACHE_TIER_MEM].End();
        truncateQ[READ_CACHE_TIER_MEM].PopBack();
        truncateLock[READ_CACHE_TIER_MEM].UnLock();
        BIO_TRACE_START(RCACHE_TRACE_EVICT2DISK);
        chunk->lock.lock();
        uint64_t flowOffset = 0;
        uint64_t indexInFlow = 0;
        flow[READ_CACHE_TIER_DISK]->AllocOffset(chunk->GetValue().length, flowOffset, indexInFlow);
        RCacheValue chunkValue(indexInFlow, flowOffset, chunk->GetValue().length);
        auto ret = AllocChunk(chunk->GetKey(), chunkValue, newChunk);
        if (UNLIKELY(ret != BIO_OK) || (chunk == nullptr)) {
            LOG_ERROR("Alloc chunk for read cache key " << chunk->GetKey() << " failed.");
            chunk->lock.unlock();
            BIO_TRACE_END(RCACHE_TRACE_EVICT2DISK, ret);
            return BIO_ALLOC_FAIL;
        }

        ret = GetSliceFromChunk(READ_CACHE_TIER_MEM, chunk, fromSlicePtr);
        if (UNLIKELY(ret != BIO_OK) || (fromSlicePtr == nullptr)) {
            LOG_ERROR("RCache alloc mem tier slice failed, " << chunk->ToString());
            chunk->lock.unlock();
            BIO_TRACE_END(RCACHE_TRACE_EVICT2DISK, ret);
            return BIO_ALLOC_FAIL;
        }

        ret = GetSliceFromChunk(READ_CACHE_TIER_DISK, newChunk, toSlicePtr);
        if (UNLIKELY(ret != BIO_OK) || (toSlicePtr == nullptr)) {
            LOG_ERROR("RCache alloc disk tier slice failed,  " << newChunk->ToString());
            BIO_TRACE_END(RCACHE_TRACE_EVICT2DISK, ret);
            return BIO_ALLOC_FAIL;
        }

        ret = mSliceOperator.Copy(fromSlicePtr.Get(), toSlicePtr.Get());
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("RCache copy mem tier slice to disk tier slice failed, " << chunk->ToString());
            BIO_TRACE_END(RCACHE_TRACE_EVICT2DISK, ret);
            return BIO_ALLOC_FAIL;
        }

        DelFromEvictList(READ_CACHE_TIER_MEM, chunk.Get()->GetMqType(), chunk);
        chunk->SetValue(newChunk->GetValue());
        chunk->SetTierType(READ_CACHE_TIER_DISK);
        AddToEvictList(READ_CACHE_TIER_DISK, MQ_COLD, chunk);
        AddToTruncateList(READ_CACHE_TIER_DISK, chunk);
        flow[READ_CACHE_TIER_MEM]->UpdateDataTruncOffset(chunk->GetValue().flowOffset, chunk->GetValue().length);
        haveEvictData += chunk->GetValue().length;
        LOG_INFO("RCache evict chunk to disk tier success, " << chunk->ToString());
        chunk->lock.unlock();
        BIO_TRACE_END(RCACHE_TRACE_EVICT2DISK, BIO_OK);
    }

    uint64_t truncateOffset = flow[READ_CACHE_TIER_MEM]->GetDataTruncOffset();
    auto ret = flow[READ_CACHE_TIER_MEM]->GetDataFlow()->TruncateOffset(truncateOffset);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Truncate read cache key "<< chunk->GetKey() << "mem data flow to " << truncateOffset << " failed." << ret);
        return BIO_ALLOC_FAIL;
    }

    DecCacheData(READ_CACHE_TIER_MEM, haveEvictData);
    IncCacheData(READ_CACHE_TIER_DISK, haveEvictData);
    return BIO_OK;
}

BResult RCache::EvictDiskData(const uint64_t needEvictData, uint64_t &haveEvictData)
{
    haveEvictData = 0ULL;

    RCacheChunkPtr chunk;
    while (haveEvictData < needEvictData) {
        truncateLock[READ_CACHE_TIER_DISK].Lock();
        if (truncateQ[READ_CACHE_TIER_DISK].IsEmpty()) {
            truncateLock[READ_CACHE_TIER_DISK].UnLock();
            break;
        }
        BIO_TRACE_START(RCACHE_TRACE_EVICT2NULL);
        chunk = truncateQ[READ_CACHE_TIER_DISK].End();
        truncateQ[READ_CACHE_TIER_DISK].PopBack();
        truncateLock[READ_CACHE_TIER_DISK].UnLock();
        chunk->lock.lock();
        auto ret = DeleteFromIndex(chunk->GetKey());
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_INFO("Get read cache key:" << chunk->GetKey() << "not exist.");
            chunk->lock.unlock();
            BIO_TRACE_END(RCACHE_TRACE_EVICT2NULL, 0);
            continue;
        }
        
        DelFromEvictList(chunk->GetTierType(), chunk.Get()->GetMqType(), chunk);
        flow[READ_CACHE_TIER_DISK]->UpdateDataTruncOffset(chunk->GetValue().flowOffset, chunk->GetValue().length);
        haveEvictData += chunk->GetValue().length;
        LOG_INFO("Delete chunk, key: " <<chunk->GetKey() << ", type:" << chunk->GetTierType() << ", length:" <<
            chunk->GetValue().length << ", flowOffset:" << chunk->GetValue().flowOffset << ", indexInFlow:" <<
            chunk->GetValue().indexInFlow);
        chunk->lock.unlock();
        
        delete [] chunk->GetKey();
        BIO_TRACE_END(RCACHE_TRACE_EVICT2NULL, ret);
    }

    uint64_t truncateOffset = flow[READ_CACHE_TIER_DISK]->GetDataTruncOffset();
    auto ret = flow[READ_CACHE_TIER_DISK]->GetDataFlow()->TruncateOffset(truncateOffset);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Truncate disk data to offset " << truncateOffset << " flow failed." << ret);
        return BIO_ALLOC_FAIL;
    }

    DecCacheData(READ_CACHE_TIER_DISK, haveEvictData);
    return BIO_OK;
}
