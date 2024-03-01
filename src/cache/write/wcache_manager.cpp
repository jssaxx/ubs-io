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
constexpr uint16_t RETRY_EVICT_THREAD_NUM = 1;
constexpr uint32_t RETRY_EVICT_QUEUE_SIZE = 8192;
constexpr uint32_t FLUSH_RETRY_MAX_TIME = 1000000;
constexpr uint32_t FLUSH_INTERAL_TIME = 100000;

BResult WCacheManager::Init(const RCacheManagerPtr &rCacheManager)
{
    mCacheIndex = MakeRef<WCacheIndex>();
    ChkTrueNot(mCacheIndex != nullptr, BIO_ALLOC_FAIL);

    BResult result;

    mEvictService[WCACHE_MEMORY] = ExecutorService::Create(MEM_EVICT_THREAD_NUM, MEM_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mEvictService[WCACHE_MEMORY] == nullptr)) {
        LOG_ERROR("Failed to start execution service for mem evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    mEvictService[WCACHE_MEMORY]->SetThreadName("wcache-evict-mem");
    result = mEvictService[WCACHE_MEMORY]->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    mEvictService[WCACHE_DISK] = ExecutorService::Create(MEM_EVICT_THREAD_NUM, MEM_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mEvictService[WCACHE_DISK] == nullptr)) {
        LOG_ERROR("Failed to start execution service for disk evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    mEvictService[WCACHE_DISK]->SetThreadName("wcache-evict-disk");
    result = mEvictService[WCACHE_DISK]->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    mRetryEvictService = ExecutorService::Create(RETRY_EVICT_THREAD_NUM, RETRY_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mRetryEvictService == nullptr)) {
        LOG_ERROR("Failed to start execution service for retry evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mRetryEvictService->SetThreadName("wcache-retry-evict");
    result = mRetryEvictService->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    result = mRetryEvictService->Execute([this]() { RetryEvictThread(); });
    ChkTrueNot(result, BIO_INNER_ERR);

    mRCacheManager = rCacheManager;
    return BIO_OK;
}

void WCacheManager::Exit()
{
    mRunning = false;
    mEvictService[WCACHE_MEMORY]->Stop();
    mEvictService[WCACHE_DISK]->Stop();
    mRetryEvictService->Stop();
}

BResult WCacheManager::AllocateFlowId(uint16_t ptId, uint64_t &flowId)
{
    BIO_TRACE_START(WCACHE_TRACE_ALLOC_ID);
    auto flowIdAllocator = FlowIdAllocator::Instance();
    uint64_t flowPrefix = CacheFlowIdManager::GenerateCacheFlowIdPrefix(ptId, CACHE_FLOW_ID_PREFIX_TYPE_WCACHE, 0);
    flowId = flowIdAllocator->GenerateFlowId(flowPrefix);
    BIO_TRACE_END(WCACHE_TRACE_ALLOC_ID, 0);
    return BIO_OK;
}

BResult WCacheManager::CreateWCache(uint64_t procId, uint64_t flowId, uint64_t ptId, uint64_t ptv, uint16_t diskId)
{
    auto wcache = MakeRef<WCache>(procId, flowId, ptId, ptv, diskId);
    ChkTrueNot(wcache != nullptr, BIO_ALLOC_FAIL);

    WCache::EvictCallback evictCallback = [this](uint64_t ptId, const Key &key) -> BResult {
        mCacheIndex->Delete(ptId, key);
        return BIO_OK;
    };

    WCache::RetryCallback retryCallback = [this](uint64_t flowId, WCacheTierType cacheTier) -> void {
        auto wcache = GetWCache(flowId);
        if (UNLIKELY(wcache == nullptr)) {
            LOG_ERROR("Failed to get flow by id:" << flowId);
            return;
        }
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        mRetryManager[cacheTier].push_back(wcache);
    };

    auto ret = wcache->Init(mEvictService, mEvictOffset, evictCallback,
        retryCallback, mRCacheManager);
    ChkTrue(ret == BIO_OK, ret, "Failed to init WCache, flowId:" << flowId);

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        mWCacheManager.emplace(flowId, wcache);
    }

    LOG_INFO("Create cache, flowId:" << flowId);

    return BIO_OK;
}

BResult WCacheManager::DeleteWCache(uint64_t ptId)
{
    return BIO_OK;
}

BResult WCacheManager::RecoverCache(FlowPtr metaFlow)
{
    uint64_t metaFlowId = metaFlow->GetFlowId();
    uint64_t ptId = CacheFlowIdManager::GetPtId(metaFlowId);
    uint64_t flowPrefix = CacheFlowIdManager::GenerateCacheFlowIdPrefix(ptId, CACHE_FLOW_ID_PREFIX_TYPE_WCACHE, 0);
    uint64_t innerFlowId = metaFlowId & FLOW_ID_MASK;
    uint64_t flowId = (flowPrefix << FLOW_ID_SHIFT) | innerFlowId;
    uint32_t diskId = metaFlow->GetMediaId();

    LOG_INFO("Recover wcache, ptId:" << ptId << ", flowId:" << flowId);

    auto ret = CreateWCache(0, flowId, ptId, 0, static_cast<uint16_t>(diskId));
    ChkTrue(ret == BIO_OK, ret, "Failed to create wcache, flowId:" << flowId);

    // 2. Get write flow
    auto wcache = GetWCache(flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("Failed to get wcache, flowId:" << flowId);
        return BIO_NOT_EXISTS;
    }

    WCache::RecoverCallback recoverCallback = [this](uint64_t ptId, const Key &key,
        const WCacheSliceRefPtr &sliceRef) -> BResult {
        BIO_TRACE_START(WCACHE_TRACE_RECOVER);
        LOG_INFO("Recover key:" << key << ", pt:" << ptId << ", flowId:" << sliceRef->GetSlice()->GetFlowId() <<
            ", flowOffset:" << sliceRef->GetSlice()->GetOffsetInFlow() <<
            ", length:" << sliceRef->GetSlice()->GetLength());
        auto ret = mCacheIndex->Insert(ptId, key, sliceRef);
        BIO_TRACE_END(WCACHE_TRACE_RECOVER, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Insert slice to index failed, ret:" << ret << ", key:" << key << ".");
            return ret;
        }
        return BIO_OK;
    };

    ret = wcache->Recover(recoverCallback);
    if (ret != BIO_OK) {
        LOG_ERROR("Recover fail:" << ret << ", flowId:" << flowId);
        return ret;
    }

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
    BIO_TRACE_START(WCACHE_TRACE_PUT_QUERY_EXIST);
    auto keySlice = mCacheIndex->Aquire(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_PUT_QUERY_EXIST, 0);
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
    BIO_TRACE_START(WCACHE_TRACE_PUT_WRITE_FLOW);
    auto ret = wcache->Put(key, slice, sliceReader, sliceRef, attr);
    BIO_TRACE_END(WCACHE_TRACE_PUT_WRITE_FLOW, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Write slice to flow failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }

    // 4. Insert slice to index.
    BIO_TRACE_START(WCACHE_TRACE_PUT_INSERT_INDEX);
    ret = mCacheIndex->Insert(ptId, key, sliceRef);
    BIO_TRACE_END(WCACHE_TRACE_PUT_INSERT_INDEX, ret);
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
    BIO_TRACE_START(WCACHE_TRACE_GET_QUERY_INDEX);
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_GET_QUERY_INDEX, 0);
    if (UNLIKELY(sliceRef == nullptr)) {
        LOG_WARN("Aquire key :" << key << " slice not exist.");
        return BIO_NOT_EXISTS;
    }

    // 2. Read data from flow.
    BIO_TRACE_START(WCACHE_TRACE_GET_READ_DATA);
    auto ret = Read(offset, sliceRef->GetSlice(), slice, sliceWriter, realLen);
    BIO_TRACE_END(WCACHE_TRACE_GET_READ_DATA, ret);
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

void WCacheManager::RegGetGlobEvictOffset(GetGlobEvictOffset evictOffset)
{
    LOG_INFO("Register get evict offset func");
    mEvictOffset = evictOffset;
}

BResult WCacheManager::GetEvictOffset(uint64_t flowId, uint64_t &flowOffset)
{
    auto wcache = GetWCache(flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("Failed to get flow by id:" << flowId);
        return BIO_NOT_EXISTS;
    }

    BIO_TRACE_START(WCACHE_TRACE_GET_EVICT_OFFSET);
    flowOffset = wcache->GetEvictOffset();
    BIO_TRACE_END(WCACHE_TRACE_GET_EVICT_OFFSET, BIO_OK);

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

    if (ret != BIO_OK) {
        return ret;
    }

    ret = SealOldCache(ptId, ptv);
    ChkTrueNot(ret == BIO_OK, ret);

    return ret;
}

BResult WCacheManager::FlushImpl(uint64_t ptId, uint64_t ptv)
{
    std::list<WCachePtr> flushList;

    ScanOldCache(ptId, ptv, flushList);

    for (const auto &flow : flushList) {
        flow->Flush();
    }

    return (flushList.size() != 0) ? BIO_INNER_RETRY : BIO_OK;
}

BResult WCacheManager::ExpiredClear(uint64_t ptId, uint64_t ptv)
{
    LOG_INFO("Standby handle:" << "ptId:" << ptId << ", version:" << ptv);

    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeUs();
    BResult ret;

    do {
        isRetry = false;
        ret = ExpiredClearImpl(ptId, ptv);
        if (ret != BIO_OK) {
            retryTime = Monotonic::TimeUs() - startTime;
            if (retryTime < FLUSH_RETRY_MAX_TIME) {
                isRetry = true;
                usleep(FLUSH_INTERAL_TIME);
            }
        }
    } while (isRetry);

    if (ret != BIO_OK) {
        return ret;
    }

    ret = SealOldCache(ptId, ptv);
    ChkTrueNot(ret == BIO_OK, ret);

    return ret;
}

BResult WCacheManager::ExpiredClearImpl(uint64_t ptId, uint64_t ptv)
{
    std::list<WCachePtr> expiredList;

    ScanOldCache(ptId, ptv, expiredList);

    for (const auto &flow : expiredList) {
        flow->ExpiredClear();
    }

    return (expiredList.size() != 0) ? BIO_INNER_RETRY : BIO_OK;
}

void WCacheManager::ScanOldCache(uint64_t ptId, uint64_t ptv, std::list<WCachePtr> &list)
{
    WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    for (const auto &flowIt : mWCacheManager) {
        uint64_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
        if (ptId != flowPtId) {
            continue;
        }
        if (flowIt.second->GetPtv() >= ptv) {
            continue;
        }
        if (flowIt.second->GetCapacity(WCACHE_MEMORY) == 0 &&
            flowIt.second->GetCapacity(WCACHE_DISK) == 0) {
            continue;
        }
        LOG_INFO("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() <<
            ", flowId:" << flowIt.first <<
            ", Mem:" << flowIt.second->GetCapacity(WCACHE_MEMORY) <<
            ", Disk:" << flowIt.second->GetCapacity(WCACHE_DISK));
        list.emplace_back(flowIt.second);
    }

    return;
}

BResult WCacheManager::SealOldCache(uint64_t ptId, uint64_t ptv)
{
    std::list<WCachePtr> sealList;

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (const auto &flowIt : mWCacheManager) {
            uint64_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
            if (ptId != flowPtId) {
                continue;
            }
            if (flowIt.second->GetPtv() >= ptv) {
                continue;
            }
            LOG_INFO("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() <<
                ", flowId:" << flowIt.first <<
                ", Vir Mem:" << flowIt.second->GetVirCapacity(WCACHE_MEMORY) <<
                ", Vir Disk:" << flowIt.second->GetVirCapacity(WCACHE_DISK));
            sealList.emplace_back(flowIt.second);
        }
    }
    
    for (const auto &flow : sealList) {
        auto ret = flow->Seal();
        if (ret != BIO_OK) {
            LOG_ERROR("Seal fail:" << ret << ", flowId::" << flow->GetFlowId());
            return ret;
        }
    }

    return BIO_OK;
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

void WCacheManager::RetryEvictThread()
{
    std::vector<WCachePtr> retryFlows;
    while (mRunning) {
        {
            WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
            retryFlows = std::move(mRetryManager[WCACHE_MEMORY]);
        }

        for (const auto &flow : retryFlows) {
            flow->RetryEvictTask(WCACHE_MEMORY);
        }
        retryFlows.clear();
        {
            WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
            retryFlows = std::move(mRetryManager[WCACHE_DISK]);
        }

        for (const auto &flow : retryFlows) {
            flow->RetryEvictTask(WCACHE_DISK);
        }
        retryFlows.clear();
        sleep(1);
    }
}
}
}
