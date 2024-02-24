/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "wcache_manager.h"
#include "bio_log.h"
#include "bio_trace.h"
#include "bio_monotonic.h"
#include "flow_id_allocator.h"
#include "cache_flow.h"

namespace ock {
namespace bio {

constexpr uint16_t MEM_EVICT_THREAD_NUM = 4;
constexpr uint32_t MEM_EVICT_QUEUE_SIZE = 8192;
constexpr uint16_t DISK_EVICT_THREAD_NUM = 8;
constexpr uint32_t DISK_EVICT_QUEUE_SIZE = 8192;
constexpr uint32_t FLUSH_RETRY_MAX_TIME = 1000000;
constexpr uint32_t FLUSH_INTERAL_TIME = 10000;

BResult WCacheManager::Init(const RCacheManagerPtr &rCacheManager)
{
    mCacheIndex = MakeRef<WCacheIndex>();
    ChkTrueNot(mCacheIndex != nullptr, BIO_ALLOC_FAIL);

    BResult result;

    mMemEvictService = ExecutorService::Create(MEM_EVICT_THREAD_NUM, MEM_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mMemEvictService == nullptr)) {
        LOG_ERROR("Failed to start execution service for wflow evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    mMemEvictService->SetThreadName("wcache-evict-mem");
    result = mMemEvictService->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    mDiskEvictService = ExecutorService::Create(MEM_EVICT_THREAD_NUM, MEM_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mDiskEvictService == nullptr)) {
        LOG_ERROR("Failed to start execution service for wflow evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    mDiskEvictService->SetThreadName("wcache-evict-disk");
    result = mDiskEvictService->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    mRCacheManager = rCacheManager;
    return BIO_OK;
}

void WCacheManager::Exit()
{
    mRunning = false;
    mMemEvictService->Stop();
}

BResult WCacheManager::AllocateFlowId(uint16_t ptId, uint64_t &flowId)
{
    BIO_TRACE_START(WCACHE_TRACE_ALLOC_ID);
    auto flowIdAllocator = FlowIdAllocator::Instance();
    uint64_t flowPrefix = CacheFlowIdManager::GenerateCacheFlowIdPrefix(ptId, CACHE_FLOW_ID_PREFIX_TYPE_WCACHE,
        WRITE_CACHE_FLOW_MEM_META_PREFIX);
    flowId = flowIdAllocator->GenerateFlowId(flowPrefix);
    BIO_TRACE_END(WCACHE_TRACE_ALLOC_ID, 0);
    return BIO_OK;
}

BResult WCacheManager::CreateWCache(uint64_t flowId, uint64_t ptId, uint64_t ptv, uint16_t diskId)
{
    auto wcache = MakeRef<WCache>(flowId, ptId, ptv);
    ChkTrueNot(wcache != nullptr, BIO_ALLOC_FAIL);

    WCache::EvictCallback evictCallback = [this](uint64_t ptId, const Key &key) -> BResult {
        mCacheIndex->Delete(ptId, key);
        return BIO_OK;
    };

    auto ret = wcache->Init(diskId, mMemEvictService, mDiskEvictService, evictCallback, mRCacheManager);
    ChkTrue(ret == BIO_OK, ret, "Failed to init WCache, flowId:" << flowId);

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        mWCacheManager.emplace(flowId, wcache);
    }

    LOG_INFO("Create cache:" << flowId);

    return BIO_OK;
}

BResult WCacheManager::DeleteWCache(uint64_t ptId)
{
    return BIO_OK;
}

BResult WCacheManager::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    ChkTrueNot(sliceKey.Validate(), BIO_INVALID_PARAM);

    auto wcache = GetWCache(sliceKey.flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("failed to get flow by id:" << sliceKey.flowId);
        return BIO_NOT_EXISTS;
    }
    BIO_TRACE_START(WCACHE_TRACE_GET_SLICE);
    auto ret = wcache->GetWCacheSlice(sliceKey, slice);
    BIO_TRACE_END(WCACHE_TRACE_GET_SLICE, ret);
    return ret;
}

BResult WCacheManager::Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr)
{
    ChkTrueNot(key != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(slice != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(sliceReader != nullptr, BIO_INVALID_PARAM);

    uint64_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());

    LOG_DEBUG("Put key:" << key << ", pt:" << ptId << ", flowId:" << slice->GetFlowId());

    // 1. Check whether the key is duplicate
    auto keySlice = mCacheIndex->Aquire(ptId, key);
    if (UNLIKELY(keySlice != nullptr)) {
        LOG_ERROR("Put key is duplicate, key:" << key);
        // TODO::duplicate key handle
        return BIO_KEY_CONFLICT;
    }

    // 2. Get write flow
    auto wcache = GetWCache(slice->GetFlowId());
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("Failed to get flow by id:" << slice->GetFlowId() << ", key:" << key << ".");
        return BIO_NOT_EXISTS;
    }

    // 3. write slice to flow
    WCacheSliceRefPtr sliceRef = nullptr;
    auto ret = wcache->Put(key, slice, sliceReader, sliceRef, attr);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Write slice to flow failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }

    // 4. Insert slice to index.
    ret = mCacheIndex->Insert(ptId, key, sliceRef);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Insert slice to index failed, ret:" << ret << ", key:" << key << ".");
    }

    return ret;
}

BResult WCacheManager::Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
    uint64_t &realLen)
{
    ChkTrueNot(key != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(slice != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(sliceWriter != nullptr, BIO_INVALID_PARAM);

    uint64_t ptId = slice->GetPtId();

    LOG_DEBUG("Get key:" << key << ", pt:" << ptId);

    // 1. Get key slice ref from index.
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (UNLIKELY(sliceRef == nullptr)) {
        LOG_WARN("Aquire key :" << key << " slice not exist.");
        return BIO_NOT_EXISTS;
    }

    // 2. Read data from flow.
    auto ret = Read(offset, sliceRef->GetSlice(), slice, sliceWriter, realLen);
    mCacheIndex->Release(ptId, sliceRef);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Read data from flow failed, key :" << key << ".");
    }
    return ret;
}

BResult WCacheManager::Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat)
{
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (sliceRef != nullptr) {
        cacheObjStat.size = sliceRef->GetSlice()->GetLength();
        cacheObjStat.time = time(nullptr);

        mCacheIndex->Release(ptId, sliceRef);
        return BIO_OK;
    }

    return BIO_NOT_EXISTS;
}

BResult WCacheManager::Delete(uint64_t ptId, const Key &key)
{
    ChkTrueNot(key != nullptr, BIO_INVALID_PARAM);

    // 1. Aquire slice ref from index.
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (UNLIKELY(sliceRef == nullptr)) {
        LOG_WARN("Aquire key slice failed, key:" << key << ".");
        return BIO_NOT_EXISTS;
    }

    auto ret = mCacheIndex->Delete(ptId, key);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to delete. key:" << key << ", ret:" << ret);
        mCacheIndex->Release(ptId, sliceRef);
        return ret;
    }

    sliceRef->SetState(SLICE_INVALID);

    mCacheIndex->Release(ptId, sliceRef);
    return BIO_OK;
}

BResult WCacheManager::Flush(uint64_t ptId, uint64_t ptv)
{
    LOG_INFO("Master handle:" << "ptId:" << ptId << ", version:" << ptv);

    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeUs();
    BResult ret;

    do {
        isRetry = false;
        ret = FlushImpl(ptId, ptv);
        if (ret != BIO_OK) {
            retryTime = Monotonic::TimeUs() - startTime;
            if (retryTime < FLUSH_RETRY_MAX_TIME) {
                isRetry = true;
                usleep(FLUSH_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ret;
}

BResult WCacheManager::ExpiredClear(uint64_t ptId, uint64_t ptv)
{
    LOG_INFO("Standby handle:" << "ptId:" << ptId << ", version:" << ptv);
    return BIO_OK;
}

BResult WCacheManager::FlushImpl(uint64_t ptId, uint64_t ptv)
{
    std::list<WCachePtr> evictFlows;

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (const auto &flowIt : mWCacheManager) {
            uint64_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
            if (ptId != flowPtId) {
                continue;
            }
            if (flowIt.second->GetCapacity(WCACHE_MEMORY) == 0 &&
                flowIt.second->GetCapacity(WCACHE_DISK) == 0) {
                continue;
            }
            LOG_INFO("Flow ptId:" << flowPtId << ", flowId:" << flowIt.first <<
                ", Mem:" << flowIt.second->GetCapacity(WCACHE_MEMORY) <<
                ", Disk:" << flowIt.second->GetCapacity(WCACHE_DISK));
            evictFlows.emplace_back(flowIt.second);
        }
    }
    
    for (const auto &flow : evictFlows) {
        mMemEvictService->Execute([&]() {
            flow->StartEvictTask(WCACHE_MEMORY);
            flow->StartEvictTask(WCACHE_DISK);
        });
    }

    return (evictFlows.size() != 0) ? BIO_INNER_RETRY : BIO_OK;
}

inline WCachePtr WCacheManager::GetWCache(uint64_t flowId)
{
    ReadLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    auto wflowIt = mWCacheManager.find(flowId);
    if (UNLIKELY(wflowIt == mWCacheManager.end())) {
        return nullptr;
    }
    return wflowIt->second;
}

BResult WCacheManager::Read(uint64_t offset, const WCacheSlicePtr &srcSlice, const RCacheSlicePtr &destSlice,
    const SliceWriter &sliceWriter, uint64_t &realLen)
{
    if (UNLIKELY(offset >= srcSlice->GetLength())) {
        LOG_ERROR("Failed to split slice. offset:" << offset << ", real length:" << srcSlice->GetLength());
        return BIO_READ_EXCEED;
    }

    realLen = srcSlice->GetLength() - offset;
    if (realLen > destSlice->GetLength()) {
        realLen = destSlice->GetLength();
    }
    // split slice, get real range from slice.
    auto newSlice = srcSlice->Split(offset, realLen);
    if (UNLIKELY(newSlice == nullptr)) {
        LOG_ERROR("Failed to split slice. offset:" << offset << ", length:" << realLen);
        return BIO_READ_EXCEED;
    }

    // write slice to dest slice.
    auto ret = sliceWriter(newSlice, destSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    return BIO_OK;
}
}
}
