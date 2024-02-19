/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "cache.h"
#include "underfs.h"
#include "bio.h"
#include "bio_trace.h"

namespace ock {
namespace bio {
BResult Cache::Init()
{
    mRCacheManager = RCacheManager::Instance();
    ChkTrueNot(mRCacheManager != nullptr, BIO_ALLOC_FAIL);
    auto ret = mRCacheManager->Init();
    ChkTrueNot(ret == BIO_OK, ret);

    mWCacheManager = WCacheManager::Instance();
    ChkTrueNot(mWCacheManager != nullptr, BIO_ALLOC_FAIL);
    ret = mWCacheManager->Init(mRCacheManager);
    ChkTrueNot(ret == BIO_OK, ret);

    return BIO_OK;
}

void Cache::Exit()
{
    mWCacheManager->Exit();
    mRCacheManager->Exit();
}

BResult Cache::RegisterCacheClient(uint64_t &cacheId)
{
    return mWCacheManager->AllocateFlowId(0, cacheId);
}

BResult Cache::CreateWCache(uint64_t cacheId, uint64_t ptId, uint64_t flowId)
{
    BIO_TRACE_START(WCACHE_TRACE_CREATE_OBJ);
    auto ret = mWCacheManager->CreateWCache(flowId);
    BIO_TRACE_END(WCACHE_TRACE_CREATE_OBJ, ret);
    ChkTrue(ret == BIO_OK, ret, "Failed to create WCache, cacheId:"
        << cacheId << ", ptId:" << ptId << ", flowId:" << flowId << ".");
    LOG_DEBUG("Create wcache success, cacheId:" << cacheId << ", ptId:" << ptId << ", flowId:" << flowId << ".");
    return BIO_OK;
}

BResult Cache::CreateRCache(uint64_t ptId)
{
    BIO_TRACE_START(RCACHE_TRACE_CREATE_OBJ);
    auto ret = mRCacheManager->CreateRCache(ptId);
    BIO_TRACE_END(RCACHE_TRACE_CREATE_OBJ, ret);
    ChkTrue(ret == BIO_OK, ret, "Failed to create RCache, ptId:" << ptId);

    return BIO_OK;
}

BResult Cache::DeleteCache(uint64_t ptId)
{
    BIO_TRACE_START(WCACHE_TRACE_DESTROY_OBJ);
    mRCacheManager->DeleteRCache(ptId);
    BIO_TRACE_END(WCACHE_TRACE_DESTROY_OBJ, 0);

    BIO_TRACE_START(RCACHE_TRACE_DESTROY_OBJ);
    mWCacheManager->DeleteWCache(ptId);
    BIO_TRACE_END(RCACHE_TRACE_DESTROY_OBJ, 0);
    return BIO_OK;
}

BResult Cache::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    return mWCacheManager->GetWCacheSlice(sliceKey, slice);
}

BResult Cache::Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr)
{
    BIO_TRACE_START(WCACHE_TRACE_PUT);
    auto ret = mWCacheManager->Put(key, slice, sliceReader, attr);
    BIO_TRACE_END(WCACHE_TRACE_PUT, ret);
    return ret;
}

BResult Cache::Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
                   uint64_t &realLen)
{
    BIO_TRACE_START(WCACHE_TRACE_GET);
    auto ret = mWCacheManager->Get(key, offset, slice, sliceWriter, realLen);
    BIO_TRACE_END(WCACHE_TRACE_GET, ret);
    if (ret == BIO_OK) {
        LOG_INFO("write cache hit, key:" << key << ", offset:" << offset << ", length:" << slice->GetLength() << ".");
        return BIO_OK;
    }

    if (ret == BIO_NOT_EXISTS) {
        ret = mRCacheManager->Get(slice->GetPtId(), key, offset, slice.Get(), sliceWriter, realLen);
        if (UNLIKELY(ret != BIO_OK) && ret != BIO_NOT_EXISTS) {
            LOG_ERROR("Get key " << key << " read data from read cache failed, ret:" << ret);
        } else if (ret == BIO_OK) {
            LOG_INFO("read cache hit, key:" << key << ", offset:" << offset << ", length:" << slice->GetLength() << ".");
        }
    }

    if (ret == BIO_NOT_EXISTS) {
        ret = mRCacheManager->Load(slice->GetPtId(), key, 0, BIO_IO_MAX_LEN, realLen);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Load key " << key << " read data from under fs failed.");
            return ret;
        }

        ret = mRCacheManager->Get(slice->GetPtId(), key, offset, slice.Get(), sliceWriter, realLen);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Get key " << key << " read data from read cache failed.");
            return ret;
        }
        LOG_INFO("Load and read cache hit, key:" << key << ", offset:" << offset << ", length:" << slice->GetLength() << ".");
    }
    return ret;
}

BResult Cache::Load(uint64_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen)
{
    auto ret = mRCacheManager->Load(ptId, key, 0, BIO_IO_MAX_LEN, realLen);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Load failed, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ", offset:" <<
            offset << ", len:" << len << ".");
    }
    return ret;
}

BResult Cache::Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat)
{
    BIO_TRACE_START(WCACHE_TRACE_STAT);
    auto ret = mWCacheManager->Stat(ptId, key, cacheObjStat);
    BIO_TRACE_END(WCACHE_TRACE_STAT, ret);
    if ((ret == BIO_OK) || (ret != BIO_NOT_EXISTS)) {
        return ret;
    }

    UnderFsPtr underFsPtr = UnderFs::Instance();
    UnderFs::ObjStat stat;
    ret = underFsPtr->Stat(key, stat);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get key " << key << " stat from under fs failed, error code: " << ret);
        return ret;
    }

    cacheObjStat.time = stat.mTime;
    cacheObjStat.size = stat.size;
    return ret;
}

BResult Cache::Delete(uint64_t ptId, const Key &key)
{
    BIO_TRACE_START(WCACHE_TRACE_DEL);
    BResult ret = mWCacheManager->Delete(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_DEL, ret);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Write cache delete failed, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ".");
        return ret;
    } else if (ret == BIO_OK) {
        LOG_INFO("Write cache delete finish, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ".");
        return BIO_OK;
    }

    ret = mRCacheManager->Delete(ptId, key);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Read cache delete failed, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ".");
        return ret;
    } else {
        LOG_INFO("Read cache delete finish, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ".");
    }

    UnderFsPtr underFsPtr = UnderFs::Instance();
    ret = underFsPtr->Delete(key);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Delete key " << key << " from under fs failed, ret: " << ret);
        return ret;
    } else {
        LOG_INFO("UnderFS delete finish, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ".");
        ret = BIO_OK;
    }

    return ret;
}
}
}