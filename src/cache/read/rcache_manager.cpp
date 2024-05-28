/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "bio_log.h"
#include "cache_flow.h"
#include "flow_manager.h"
#include "bio_def.h"
#include "bio_trace.h"
#include "bio_monotonic.h"
#include "rcache_manager.h"
#ifdef USE_DEBUG_TOOLS
#include "bio_tracepoint_helper.h"
#endif

using namespace ock::bio;

RCacheManager::RCacheManager() {}

RCacheManager::~RCacheManager() {}

static constexpr uint32_t FLUSH_RETRY_MAX_TIME = 1000000;
static constexpr uint32_t FLUSH_INTERAL_TIME = 100000;

BResult RCacheManager::Init()
{
    LVOS_TP_START(NO_PROCESS_RCACHE_EVICT, 0);
    rCacheEvict = MakeRef<RCacheEvict>();
    LVOS_TP_END;

    BResult ret = BIO_OK;
    LVOS_TP_START(NO_PROCESS_RCACHE_GC, 0);
    ret = rCacheEvict->Initialize();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init rcache evict, ret:" << ret << ".");
        return ret;
    }

    rCacheGCPtr = MakeRef<RCacheGC>();
    LVOS_TP_END;
    ret = rCacheGCPtr->Initialize();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init rcache GC, ret:" << ret << ".");
        return ret;
    }
    return ret;
}

uint64_t RCacheManager::GetGCData()
{
    return 0ULL;
}

void RCacheManager::Exit()
{
    cacheLock.LockWrite();
    for (auto iter = cache.begin(); iter != cache.end(); iter++) {
        iter->second = nullptr;
    }
    cache.clear();
    cacheLock.UnLock();

    rCacheEvict->Destroy();
    rCacheGCPtr->Destroy();
}

const RCachePtr RCacheManager::GetRCacheInstanceByPtId(uint64_t ptId)
{
    cacheLock.LockRead();
    auto iter = cache.find(ptId);
    if (UNLIKELY(iter == cache.end())) {
        cacheLock.UnLock();
        LOG_WARN("Read cache pt id " << ptId << " do not exist.");
        return nullptr;
    }
    RCachePtr cachePtr = iter->second;
    cacheLock.UnLock();
    return cachePtr;
}

BResult RCacheManager::AllocResources(uint64_t ptId, uint64_t len, WCacheSlicePtr &slice)
{
    BIO_TRACE_START(RCACHE_TRACE_PUT_GET_SLICE);
    RCachePtr cachePtr = GetRCacheInstanceByPtId(ptId);
    if (UNLIKELY(cachePtr == nullptr)) {
        BIO_TRACE_END(RCACHE_TRACE_PUT_GET_SLICE, BIO_NOT_EXISTS);
        return BIO_NOT_EXISTS;
    }
    auto ret = cachePtr->AllocResources(len, slice);
    BIO_TRACE_END(RCACHE_TRACE_PUT_GET_SLICE, ret);
    return ret;
}

BResult RCacheManager::Put(uint64_t ptId, const Key &key, const WCacheSlicePtr &slice)
{
    BIO_TRACE_START(RCACHE_TRACE_PUT);
    RCachePtr cachePtr = GetRCacheInstanceByPtId(ptId);
    if (UNLIKELY(cachePtr == nullptr)) {
        BIO_TRACE_END(RCACHE_TRACE_PUT, BIO_NOT_EXISTS);
        return BIO_NOT_EXISTS;
    }
    auto ret = cachePtr->Put(key, slice);
    BIO_TRACE_END(RCACHE_TRACE_PUT, ret);
    return ret;
};

BResult RCacheManager::Get(uint64_t ptId, const Key &key, uint64_t offset, const RCacheSlicePtr &slice,
    const SliceWriter &sliceWriter, uint64_t &realLen)
{
    RCachePtr cachePtr = GetRCacheInstanceByPtId(ptId);
    ChkTrue(UNLIKELY(cachePtr != nullptr), BIO_NOT_EXISTS, "Get read cache instance failed, ptId:" << ptId << ".");
    return cachePtr->Get(key, offset, slice, sliceWriter, realLen);
}

BResult RCacheManager::Load(uint64_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen)
{
    BIO_TRACE_START(RCACHE_TRACE_LOAD);
    RCachePtr cachePtr = GetRCacheInstanceByPtId(ptId);
    ChkTrue(UNLIKELY(cachePtr != nullptr), BIO_NOT_EXISTS, "Get read cache instance failed, ptId:" << ptId << ".");
    auto ret = cachePtr->Load(key, offset, len, realLen);
    BIO_TRACE_END(RCACHE_TRACE_LOAD, ret);
    return ret;
}

BResult RCacheManager::Delete(uint64_t ptId, const Key &key)
{
    RCachePtr cachePtr = nullptr;
    LVOS_TP_START(RCACHE_MANAGER_DELETE_ERR, &cachePtr, nullptr);
    cachePtr = GetRCacheInstanceByPtId(ptId);
    LVOS_TP_END;
    ChkTrue(UNLIKELY(cachePtr != nullptr), BIO_NOT_EXISTS, "Get read cache instance failed, ptId:" << ptId << ".");
    return cachePtr->Delete(key);
}

BResult RCacheManager::CreateRCache(uint64_t ptId, uint64_t ptv, uint16_t diskId)
{
    cacheLock.LockWrite();
    LVOS_TP_START(NO_PROCESS_RCACHE_FIND, 0);
    auto iter = cache.find(ptId);
    if (iter != cache.end()) {
        LOG_INFO("Exist, ptId:" << ptId << " have associated read cache object.");
        cacheLock.UnLock();
        return BIO_OK;
    }
    LVOS_TP_END;

    uint32_t workIndex = mWorkIndex++;
    RCachePtr cacheObj = nullptr;
    LVOS_TP_START(RCACHE_ALLOC_OBJ_FAIL, &cacheObj, nullptr);
    cacheObj = MakeRef<RCache>(ptId, ptv, diskId, workIndex);
    LVOS_TP_END;
    if (UNLIKELY(cacheObj == nullptr)) {
        LOG_ERROR("Create read cache object memory failed.");
        cacheLock.UnLock();
        return BIO_ALLOC_FAIL;
    }

    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(RCACHE_INIT_OBJ_FAIL, &ret, BIO_ERR);
    ret = cacheObj->Initialize();
    LVOS_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Initialize read cache object ptId:" << ptId << " failed, error code " << ret);
        cacheLock.UnLock();
        return ret;
    }

    cache[ptId] = cacheObj;
    cacheLock.UnLock();

    ret = rCacheEvict->Start(cacheObj);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Start ptId:" << ptId << " read cache to evict service failed, error code " << ret);
        DeleteRCache(ptId);
        return BIO_ALLOC_FAIL;
    }

    ret = rCacheGCPtr->Start(cacheObj);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Start ptId:" << ptId << " read cache to GC service failed, error code " << ret);
        DeleteRCache(ptId);
        return BIO_ALLOC_FAIL;
    }

    LOG_INFO("Create cache, flowId:" << cacheObj->GetFlowId() << ", ptId:" << ptId << ", ptv:" << ptv);
    return BIO_OK;
}

BResult RCacheManager::DeleteRCache(uint64_t ptId)
{
    cacheLock.LockWrite();
    auto iter = cache.find(ptId);
    if (iter == cache.end()) {
        cacheLock.UnLock();
        return BIO_OK;
    }

    RCachePtr cachePtr = iter->second;
    cachePtr->Destroy();

    LVOS_TP_START(NO_PROCESS_RCACHE_RELEASE, 0);
    cache.erase(iter);
    LVOS_TP_END;
    cacheLock.UnLock();

    LOG_INFO("Delete rcache, flowId:" << cachePtr->GetFlowId() << ", ptId:" << ptId << ", ptv:" << cachePtr->GetPtv());
    return BIO_OK;
}

BResult RCacheManager::RecoverCache(FlowPtr dataFlow)
{
    LOG_INFO("Recover rcache, flowId:" << dataFlow->GetFlowId());
    LVOS_TP_START(NO_PROCESS_CACHE_RECOVER, 0);
    dataFlow->Seal();
    FlowManager::Instance()->DestroyObject(dataFlow->GetFlowType(), dataFlow->GetFlowId());
    LVOS_TP_END;
    return BIO_OK;
}

BResult RCacheManager::ExpiredClear(uint64_t ptId, uint64_t ptv)
{
    LOG_INFO("RCache expired clear, ptId:" << ptId << ", ptv:" << ptv << ".");
    RCachePtr rCache = GetRCacheInstanceByPtId(ptId);
    if (UNLIKELY(rCache == nullptr)) {
        LOG_INFO("No needed, not exist, ptId:" << ptId << ", ptv:" << ptv);
        return BIO_OK;
    }

    if (rCache->GetPtv() >= ptv) {
        LOG_INFO("No needed, matched, ptId:" << ptId << ", ptv:" << rCache->GetPtv());
        return BIO_OK;
    }

    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeUs();
    BResult ret;

    do {
        isRetry = false;
        ret = ExpiredClearImpl(rCache);
        if (ret != BIO_OK) {
            retryTime = Monotonic::TimeUs() - startTime;
            if (retryTime < FLUSH_RETRY_MAX_TIME) {
                isRetry = true;
                usleep(FLUSH_INTERAL_TIME);
            }
        }
        LVOS_TP_START(RCACHE_EVICT_OK, &isRetry, false);
        LVOS_TP_END;
    } while (isRetry);

    return DeleteRCache(ptId);
}

BResult RCacheManager::ExpiredClearImpl(RCachePtr rCache)
{
    rCache->SetDelete();

    auto ret = rCacheEvict->Stop(rCache);
    LVOS_TP_START(RCACHE_EVICT_ERR, &ret, BIO_ERR);
    LVOS_TP_END;
    if ((ret != BIO_OK) && (ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Stop ptId " << rCache->GetPtId() << " read cache evict service failed:" << ret);
        return ret;
    }

    ret = rCacheGCPtr->Stop(rCache);
    if ((ret != BIO_OK) && (ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Stop ptId " << rCache->GetPtId() << " read cache GC service failed:" << ret);
        return ret;
    }

    if (!rCache->IsEmptyEvict()) {
        return BIO_INNER_RETRY;
    }

    return BIO_OK;
}
