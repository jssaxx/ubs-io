/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "bio_log.h"
#include "rcache_manager.h"
#include "bio_def.h"
#include "bio_trace.h"

using namespace ock::bio;

RCacheManager::RCacheManager() {}

RCacheManager::~RCacheManager() {}

BResult RCacheManager::Init()
{
    rCacheEvict = MakeRef<RCacheEvict>();
    BResult ret = rCacheEvict->Initialize();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init rcache evict, ret:" << ret << ".");
        return ret;
    }

    rCacheGCPtr = MakeRef<RCacheGC>();
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

void RCacheManager::Exit() {}
const RCachePtr RCacheManager::GetRCacheInstanceByPtId(uint64_t ptId)
{
    RCachePtr cachePtr = nullptr;
    cacheLock.LockRead();
    auto iter =  cache.find(ptId);
    if (UNLIKELY(iter == cache.end())) {
        LOG_ERROR("Read cache pt id " << ptId << " do not exist.");
        return nullptr;
    }
    cachePtr = iter->second;
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
    LOG_INFO("Put key:" << key);
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
    LOG_INFO("Get key:" << key);
    BIO_TRACE_START(RCACHE_TRACE_GET);
    RCachePtr cachePtr = GetRCacheInstanceByPtId(ptId);
    if (UNLIKELY(cachePtr == nullptr)) {
        BIO_TRACE_END(RCACHE_TRACE_GET, BIO_NOT_EXISTS);
        return BIO_NOT_EXISTS;
    }
    auto ret = cachePtr->Get(key, offset, slice, sliceWriter, realLen);
    BIO_TRACE_END(RCACHE_TRACE_GET, ret);
    return ret;
}

BResult RCacheManager::Load(uint64_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen)
{
    BIO_TRACE_START(RCACHE_TRACE_LOAD);
    RCachePtr cachePtr = GetRCacheInstanceByPtId(ptId);
    if (UNLIKELY(cachePtr == nullptr)) {
        BIO_TRACE_END(RCACHE_TRACE_LOAD, BIO_NOT_EXISTS);
        return BIO_NOT_EXISTS;
    }

    auto ret = cachePtr->Load(key, offset, len, realLen);
    BIO_TRACE_END(RCACHE_TRACE_LOAD, ret);
    return ret;
}

BResult RCacheManager::Delete(uint64_t ptId, const Key &key)
{
    BIO_TRACE_START(RCACHE_TRACE_DEL);
    RCachePtr cachePtr = GetRCacheInstanceByPtId(ptId);
    if (UNLIKELY(cachePtr == nullptr)) {
        BIO_TRACE_END(RCACHE_TRACE_DEL, BIO_NOT_EXISTS);
        return BIO_NOT_EXISTS;
    }

    auto ret = cachePtr->Delete(key);
    BIO_TRACE_END(RCACHE_TRACE_DEL, ret);
    return ret;
}

BResult RCacheManager::CreateRCache(uint64_t ptId, uint16_t diskId)
{
    cacheLock.LockWrite();
    auto iter = cache.find(ptId);
    if (iter != cache.end()) {
        LOG_INFO("Pt id" << ptId << "have associated read cache object.");
        cacheLock.UnLock();
        return BIO_OK;
    }

    auto cacheObj = MakeRef<RCache>(ptId, diskId);
    if (UNLIKELY(cacheObj == nullptr)) {
        LOG_ERROR("Create read cache object memory failed.");
        cacheLock.UnLock();
        return BIO_ALLOC_FAIL;
    }

    auto ret = cacheObj->Initialize();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Initialize read cache object ptId" << ptId <<" failed, error code"<<ret);
        cacheLock.UnLock();
        return ret;
    }

    cache[ptId] = cacheObj;
    cacheLock.UnLock();

    ret = rCacheEvict->Start(cacheObj);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Start ptId" << ptId << "read cache to evict service failed, error code" << ret);
        DeleteRCache(ptId);
        return BIO_ALLOC_FAIL;
    }

    ret = rCacheGCPtr->Start(cacheObj);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Start ptId" << ptId << "read cache to GC service failed, error code" << ret);
        DeleteRCache(ptId);
        return BIO_ALLOC_FAIL;
    }

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
    auto ret = rCacheEvict->Stop(cachePtr);
    if (UNLIKELY(ret != BIO_OK)) {
        cacheLock.UnLock();
        LOG_ERROR("Stop ptId" << cachePtr->GetPtId() << "read cache evict service failed, error code" << ret);
        return BIO_ALLOC_FAIL;
    }

    ret = rCacheGCPtr->Stop(cachePtr);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Stop ptId" << cachePtr->GetPtId() << "read cache GC service failed, error code" << ret);
        return BIO_ALLOC_FAIL;
    }

    ret = cachePtr.Get()->Destroy();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Destroy ptId" << ptId << " read cache object failed, error code " << ret);
    }

    cache.erase(iter);
    cacheLock.UnLock();
    return BIO_OK;
}

BResult RCacheManager::RecoverCache(FlowPtr metaFlow)
{
    return BIO_OK;
}
