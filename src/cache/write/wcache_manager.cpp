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

    mEvictService[WCACHE_DISK] = ExecutorService::Create(DISK_EVICT_THREAD_NUM, DISK_EVICT_QUEUE_SIZE);
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

    WCache::EvictCallback evictCallback = [this](uint64_t ptId, const Key &key, WCacheSliceRefPtr sliceRef) -> BResult {
        mCacheIndex->Delete(ptId, key, sliceRef);
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

    auto ret = wcache->Init(mEvictService, mRCacheManager);
    ChkTrue(ret == BIO_OK, ret, "Failed to init WCache, flowId:" << flowId);

    wcache->RegOp(mGetLocDiskStatus, mEvictOffset, evictCallback, retryCallback);

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        mWCacheManager.emplace(flowId, wcache);
    }

    LOG_INFO("Create cache, procId:" << procId << ", flowId:" << flowId << ", ptId:" << ptId << ", ptv:" << ptv);

    return BIO_OK;
}

BResult WCacheManager::DeleteWCache(uint64_t flowId)
{
    mWCacheManagerLock.LockWrite();
    auto iter = mWCacheManager.find(flowId);
    if (iter == mWCacheManager.end()) {
        mWCacheManagerLock.UnLock();
        return BIO_OK;
    }

    WCachePtr wcache = iter->second;
    auto ret = wcache->Destroy();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Destroy cache, flowId:" << wcache->GetFlowId() << " fail:" << ret);
    }

    mWCacheManager.erase(iter);
    mWCacheManagerLock.UnLock();

    LOG_INFO("Delete cache, procId:" << wcache->GetProcId() << ", flowId:" << wcache->GetFlowId() << ", ptId:" <<
        wcache->GetPtId() << ", ptv:" << wcache->GetPtv());
    return BIO_OK;
}

BResult WCacheManager::RecoverCache(FlowPtr metaFlow)
{
    uint64_t flowId = CacheFlowIdManager::GenOutFlowId(metaFlow->GetFlowId());
    uint64_t ptId = CacheFlowIdManager::GetPtId(flowId);
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

BResult WCacheManager::Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader,
    CacheAttr &attr, bool isDegrade)
{
    ChkTrue(key != nullptr, BIO_INVALID_PARAM, "Key is nullptr.");
    ChkTrue(slice != nullptr, BIO_INVALID_PARAM, "Slice is nullptr.");
    ChkTrue(sliceReader != nullptr, BIO_INVALID_PARAM, "Slice reader is nullptr.");

    // 1. Get write flow
    auto wcache = GetWCache(slice->GetFlowId());
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("Failed to get wcache flow by id:" << slice->GetFlowId() << ", key:" << key << ".");
        return BIO_NOT_EXISTS;
    }

    // 2. write slice to flow
    WCacheSliceRefPtr sliceRef = nullptr;
    BIO_TRACE_START(WCACHE_TRACE_PUT_WRITE_FLOW);
    auto ret = wcache->Put(key, slice, sliceReader, sliceRef, attr, isDegrade);
    BIO_TRACE_END(WCACHE_TRACE_PUT_WRITE_FLOW, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Write slice to flow failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }

    // 3. is degrade
    if (UNLIKELY(isDegrade)) {
        return BIO_OK;
    }

    // 4. Insert slice to index.
    BIO_TRACE_START(WCACHE_TRACE_PUT_INSERT_INDEX);
    uint64_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());
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
    ChkTrue(key != nullptr, BIO_INVALID_PARAM, "Key is nullptr.");
    ChkTrue(slice != nullptr, BIO_INVALID_PARAM, "Slice is nullptr.");
    ChkTrue(sliceWriter != nullptr, BIO_INVALID_PARAM, "Slice writer is nullptr.");

    uint64_t ptId = slice->GetPtId();
    BIO_TRACE_START(WCACHE_TRACE_GET_QUERY_INDEX);
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_GET_QUERY_INDEX, ((sliceRef == nullptr) ? BIO_NOT_EXISTS : BIO_OK));
    if (UNLIKELY(sliceRef == nullptr)) {
        return BIO_NOT_EXISTS;
    }

    BIO_TRACE_START(WCACHE_TRACE_GET_READ_DATA);
    auto ret = Read(offset, sliceRef->GetSlice(), slice, sliceWriter, realLen);
    BIO_TRACE_END(WCACHE_TRACE_GET_READ_DATA, ret);
    sliceRef->Release();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("WCache Read data failed, key :" << key << ", offset:" << offset << ", length:" <<
            sliceRef->GetSlice()->GetLength() << ".");
    }
    return ret;
}

BResult WCacheManager::Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat)
{
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (sliceRef != nullptr) {
        cacheObjStat.size = sliceRef->GetSlice()->GetLength();
        cacheObjStat.time = time(nullptr);
        sliceRef->Release();
        return BIO_OK;
    }
    return BIO_NOT_EXISTS;
}

BResult WCacheManager::List(char *prefix, uint16_t ptId, std::unordered_map<std::string, CacheObjStat> &objs)
{
    ChkTrueNot(prefix != nullptr, BIO_INVALID_PARAM);
    return mCacheIndex->FuzzyAquire(ptId, prefix, objs);
}

BResult WCacheManager::Delete(uint64_t ptId, const Key &key)
{
    ChkTrue(key != nullptr, BIO_INVALID_PARAM, "Key is nullptr.");

    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (UNLIKELY(sliceRef == nullptr)) {
        LOG_WARN("Write cache aquire slice failed, key:" << key << ", ptId:" << ptId << ".");
        return BIO_NOT_EXISTS;
    }

    auto slice = sliceRef->GetSlice();
    uint64_t flowId = CacheFlowIdManager::GenOutFlowId(slice->GetFlowId());
    auto wcache = GetWCache(flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("Failed to get flow, flowId:" << flowId << ", key:" << key << ".");
        return BIO_NOT_EXISTS;
    }

    auto ret = wcache->Delete(key, sliceRef);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Delete slice from flow failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }

    ret = mCacheIndex->Delete(ptId, key, sliceRef);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to delete. key:" << key << ", ret:" << ret);
        sliceRef->Release();
        return ret;
    }

    sliceRef->SetState(SLICE_INVALID);
    sliceRef->Release();
    return BIO_OK;
}

void WCacheManager::RegGetLocDiskStatus(GetLocDiskStatus getLocDiskStatus)
{
    LOG_INFO("Register get loc disk status func");
    mGetLocDiskStatus = getLocDiskStatus;
}

void WCacheManager::RegGetGlobEvictOffset(GetGlobEvictOffset evictOffset)
{
    LOG_INFO("Register get evict offset func");
    mEvictOffset = evictOffset;
}

void WCacheManager::RegCheckLocRole(CheckLocRole localRole)
{
    LOG_INFO("Register check loc role func");
    mLocRole = localRole;
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
    LOG_INFO("Master handle:" << "ptId:" << ptId << ", ptv:" << ptv);

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

    ret = ClearOldCache(ptId, ptv);
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
    LOG_INFO("Standby handle:" << "ptId:" << ptId << ", ptv:" << ptv);

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

    ret = ClearOldCache(ptId, ptv);
    ChkTrueNot(ret == BIO_OK, ret);

    mCacheIndex->ExpiredClear(ptId);

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
        if (flowIt.second->IsEmptyEvict()) {
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

BResult WCacheManager::ClearOldCache(uint64_t ptId, uint64_t ptv)
{
    std::list<uint64_t> clearList;

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
            clearList.emplace_back(flowIt.first);
        }
    }
    
    for (const auto &flowId : clearList) {
        auto ret = DeleteWCache(flowId);
        if (ret != BIO_OK) {
            LOG_ERROR("Clear fail:" << ret << ", flowId::" << flowId);
        }
    }

    return BIO_OK;
}

BResult WCacheManager::HandleProcBroken(uint64_t procId)
{
    LOG_INFO("Handle proc broken:" << procId);

    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeUs();
    BResult ret;

    do {
        isRetry = false;
        ret = HandleProcBrokenImpl(procId);
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

    ret = ClearProcCache(procId);
    ChkTrueNot(ret == BIO_OK, ret);

    return ret;
}

BResult WCacheManager::HandleProcBrokenImpl(uint64_t procId)
{
    std::list<WCachePtr> brokenList;
    bool isMaster;

    ScanProcCache(procId, brokenList);

    for (const auto &flow : brokenList) {
        uint64_t flowPtId = flow->GetPtId();
        auto ret = mLocRole(static_cast<uint16_t>(flowPtId), isMaster);
        if (ret != BIO_OK) {
            LOG_ERROR("Get local role fail:" << ret << ", ptId:" << flowPtId);
            continue;
        }
        if (isMaster) {
            flow->Flush();
        } else {
            flow->ExpiredClear();
        }
    }

    return (brokenList.size() != 0) ? BIO_INNER_RETRY : BIO_OK;
}

void WCacheManager::ScanProcCache(uint64_t procId, std::list<WCachePtr> &list)
{
    WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    for (const auto &flowIt : mWCacheManager) {
        uint64_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
        if (procId != flowIt.second->GetProcId()) {
            continue;
        }
        if (flowIt.second->IsEmptyEvict()) {
            continue;
        }
        LOG_INFO("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() <<
            ", flowId:" << flowIt.first << ", procId:" << procId <<
            ", Mem:" << flowIt.second->GetCapacity(WCACHE_MEMORY) <<
            ", Disk:" << flowIt.second->GetCapacity(WCACHE_DISK));
        list.emplace_back(flowIt.second);
    }

    return;
}

BResult WCacheManager::ClearProcCache(uint32_t procId)
{
    std::list<uint64_t> clearList;

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (const auto &flowIt : mWCacheManager) {
            uint64_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
            if (procId != flowIt.second->GetProcId()) {
                continue;
            }
            LOG_INFO("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() <<
                ", flowId:" << flowIt.first << ", procId:" << procId <<
                ", Vir Mem:" << flowIt.second->GetVirCapacity(WCACHE_MEMORY) <<
                ", Vir Disk:" << flowIt.second->GetVirCapacity(WCACHE_DISK));
            clearList.emplace_back(flowIt.first);
        }
    }
    
    for (const auto &flowId : clearList) {
        auto ret = DeleteWCache(flowId);
        if (ret != BIO_OK) {
            LOG_ERROR("Clear fail:" << ret << ", flowId::" << flowId);
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
        LOG_ERROR("Failed to split slice. offset:" << offset << ", length:" << srcSlice->GetLength());
        return BIO_READ_EXCEED;
    }

    realLen = srcSlice->GetLength() - offset;
    if (realLen > destSlice->GetLength()) {
        realLen = destSlice->GetLength();
    }
    auto newSlice = srcSlice->Split(offset, realLen);
    if (UNLIKELY(newSlice == nullptr)) {
        LOG_ERROR("Failed to split slice. offset:" << offset << ", length:" << realLen);
        return BIO_READ_EXCEED;
    }

    auto ret = sliceWriter(newSlice, destSlice.Get());
    if (ret != BIO_OK) {
        LOG_ERROR("Call slice writer to dst slice failed, ret:" << ret << ", offset:" << offset << ", length:" <<
            realLen << ".");
    }
    return ret;
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
