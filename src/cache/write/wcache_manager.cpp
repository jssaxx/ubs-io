/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "wcache_manager.h"
#include "bio_log.h"
#include "bio_trace.h"
#include "flow_id_allocator.h"
#include "cache_flow.h"

namespace ock {
namespace bio {
BResult WCacheManager::Init(const RCacheManagerPtr &rCacheManager)
{
    mCacheIndex = MakeRef<WCacheIndex>();
    ChkTrueNot(mCacheIndex != nullptr, BIO_ALLOC_FAIL);

    mExeService = ExecutorService::Create(10, NO_8192);
    if (mExeService == nullptr) {
        LOG_ERROR("Failed to start execution service for wflow evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mExeService->SetThreadName("wflow-evict-thread");
    auto result = mExeService->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    mExeService->Execute([this]() { RunEvictThread(); });

    mRCacheManager = rCacheManager;
    return BIO_OK;
}

void WCacheManager::Exit()
{
    mRunning = false;
    mExeService->Stop();
}

BResult WCacheManager::AllocateFlowId(uint16_t ptId, uint64_t &flowId)
{
    BIO_TRACE_START(WCACHE_TRACE_ALLOC_ID);
    auto flowIdAllocator = FlowIdAllocator::Instance();
    flowId = flowIdAllocator->GenerateFlowId(((uint64_t)ptId & 0x1FFF) << 11);
    BIO_TRACE_END(WCACHE_TRACE_ALLOC_ID, 0);
    return BIO_OK;
}

BResult WCacheManager::CreateWCache(uint64_t flowId)
{
    auto wcache = MakeRef<WCache>();
    ChkTrueNot(wcache != nullptr, BIO_ALLOC_FAIL);

    WCache::EvictCallback evictCallback = [this](uint64_t ptId, const Key &key) -> BResult {
        mCacheIndex->Delete(ptId, key);
        return BIO_OK;
    };

    BIO_TRACE_START(WCACHE_TRACE_CREATE_OBJ);
    auto ret = wcache->Init(flowId, mExeService, evictCallback);
    ChkTrue(ret == BIO_OK, ret, "Failed to init WCache, flowId:" << flowId);
    BIO_TRACE_END(WCACHE_TRACE_CREATE_OBJ, ret);

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        mWCacheManager.emplace(flowId, wcache);
    }

    LOG_INFO("Create cache:" << flowId);

    return BIO_OK;
}

BResult WCacheManager::DeleteWCache(uint64_t ptId)
{
    BIO_TRACE_START(WCACHE_TRACE_DESTROY_OBJ);
    BIO_TRACE_END(WCACHE_TRACE_DESTROY_OBJ, 0);
    return BIO_OK;
}

BResult WCacheManager::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    ChkTrueNot(sliceKey.Validate(), BIO_INVALID_PARAM);

    auto wcache = GetWCache(sliceKey.flowId);
    if (wcache == nullptr) {
        LOG_ERROR("failed to get flow by id:" << sliceKey.flowId);
        return BIO_NOT_EXISTS;
    }
    BIO_TRACE_START(WCACHE_TRACE_GET_SLICE);
    auto ret = wcache->GetWCacheSlice(sliceKey, slice);
    BIO_TRACE_END(WCACHE_TRACE_GET_SLICE, ret);
    return ret;
}

BResult WCacheManager::Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader)
{
    ChkTrueNot(key != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(slice != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(sliceReader != nullptr, BIO_INVALID_PARAM);

    uint64_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());

    LOG_INFO("Put key:" << key << ", pt:" << ptId << ", flowId:" << slice->GetFlowId());

    BIO_TRACE_START(WCACHE_TRACE_PUT);

    // 1. Check whether the key is duplicate
    auto keySlice = mCacheIndex->Aquire(ptId, key);
    if (keySlice != nullptr) {
        LOG_ERROR("Put key is duplicate, key:" << key);
        // TODO::duplicate key handle
        return BIO_KEY_CONFLICT;
    }

    // 2. Get write flow
    auto wcache = GetWCache(slice->GetFlowId());
    if (wcache == nullptr) {
        LOG_ERROR("failed to get flow by id:" << slice->GetFlowId() << ", key:" << key << ".");
        return BIO_NOT_EXISTS;
    }

    // 3. write slice to flow
    WCacheSliceRefPtr sliceRef = nullptr;
    auto ret = wcache->Put(key, slice, sliceReader, sliceRef);
    if (ret != BIO_OK) {
        LOG_ERROR("Write slice to flow failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }

    // 4. Insert slice to index.
    ret = mCacheIndex->Insert(ptId, key, sliceRef);
    if (ret != BIO_OK) {
        LOG_ERROR("Insert slice to index failed, ret:" << ret << ", key:" << key << ".");
    }

    BIO_TRACE_END(WCACHE_TRACE_PUT, ret);
    return ret;
}

BResult WCacheManager::Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter)
{
    ChkTrueNot(key != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(slice != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(sliceWriter != nullptr, BIO_INVALID_PARAM);

    uint64_t ptId = slice->GetPtId();

    LOG_INFO("Get key:" << key << ", pt:" << ptId);

    BIO_TRACE_START(WCACHE_TRACE_GET);

    // 1. Get key slice ref from index.
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (sliceRef == nullptr) {
        LOG_WARN("Aquire key :" << key << " slice not exist.");
        return BIO_NOT_EXISTS;
    }

    // 2. Read data from flow.
    auto ret = Read(offset, sliceRef->GetSlice(), slice, sliceWriter);
    mCacheIndex->Release(ptId, sliceRef);
    if (ret != BIO_OK) {
        LOG_ERROR("Read data from flow failed, key :" << key << ".");
    }
    BIO_TRACE_END(WCACHE_TRACE_GET, ret);
    return ret;
}

BResult WCacheManager::Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat)
{
    BIO_TRACE_START(WCACHE_TRACE_STAT);
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_STAT, 0);
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

    BIO_TRACE_START(WCACHE_TRACE_DEL);

    // 1. Aquire slice ref from index.
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (sliceRef == nullptr) {
        LOG_ERROR("Aquire key slice failed, key:" << key << ".");
        return BIO_NOT_EXISTS;
    }

    auto ret = mCacheIndex->Delete(ptId, key);
    if (ret != BIO_OK) {
        LOG_ERROR("failed to delete. key:" << key << ", ret:" << ret);
        mCacheIndex->Release(ptId, sliceRef);
        return ret;
    }

    auto slice = sliceRef->GetSlice();
    slice->SetSliceState(1);

    mCacheIndex->Release(ptId, sliceRef);
    BIO_TRACE_END(WCACHE_TRACE_DEL, 0);
    return BIO_OK;
}

BResult WCacheManager::Flush(uint64_t ptId)
{
    std::list<WCachePtr> evictFlows;
    std::atomic<uint64_t> evictNum { 0 };
    BResult evictRet = BIO_OK;
    while (evictNum != 0) {
        {
            WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
            for (const auto &flowIt : mWCacheManager) {
                if (ptId != NO_MAX_VALUE64 && flowIt.first != ptId) {
                    continue;
                }
                evictFlows.emplace_back(flowIt.second);
                evictNum++;
            }
        }

        for (const auto &flow : evictFlows) {
            mExeService->Execute([&]() {
                auto ret = flow->EvictAllDiskSliceToUnderFs(mRCacheManager);
                if (ret != BIO_OK) {
                    LOG_ERROR("Evict fail, ret:" << ret);
                    evictRet = ret;
                }
                evictNum--;
            });
        }
        evictFlows.clear();
        sleep(1);
    }
    return evictRet;
}

WCachePtr WCacheManager::GetWCache(uint64_t flowId)
{
    ReadLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    auto wflowIt = mWCacheManager.find(flowId);
    if (wflowIt == mWCacheManager.end()) {
        return nullptr;
    }
    return wflowIt->second;
}

BResult WCacheManager::Read(uint64_t offset, const WCacheSlicePtr &srcSlice, const RCacheSlicePtr &destSlice,
    const SliceWriter &sliceWriter)
{
    // split slice, get real range from slice.
    auto newSlice = srcSlice->Split(offset, destSlice->GetLength());
    if (newSlice == nullptr) {
        LOG_ERROR("failed to split slice. offset:" << offset << ", length:" << destSlice->GetLength());
        return BIO_READ_EXCEED;
    }

    // write slice to dest slice.
    auto ret = sliceWriter(newSlice, destSlice.Get());
    ChkTrueNot(ret == BIO_OK, ret);

    return BIO_OK;
}

void WCacheManager::RunEvictThread()
{
    std::list<WCachePtr> evictFlows;
    while (mRunning) {
        {
            WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
            uint64_t totalCapacity = 0;
            for (const auto &flowIt : mWCacheManager) {
                auto flow = flowIt.second;
                totalCapacity += flow->GetCapacity();
                if (totalCapacity > mDiskCapacityThreshold) {
                    evictFlows.emplace_back(flow);
                }
            }
            LOG_INFO("totalCapacity:" << totalCapacity << ", mDiskCapacityThreshold:" << mDiskCapacityThreshold);
        }

        for (const auto &flow : evictFlows) {
            mExeService->Execute([&]() { flow->EvictAllDiskSliceToUnderFs(mRCacheManager); });
        }
        evictFlows.clear();
        sleep(10);
    }
}
}
}
