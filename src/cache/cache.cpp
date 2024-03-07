/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "cache.h"
#include "cache_flow.h"
#include "underfs.h"
#include "flow_manager.h"
#include "bio.h"
#include "bio_trace.h"
#include "cm.h"

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

BResult Cache::Recover()
{
    std::map<uint64_t, FlowPtr> flowMaps;

    auto ret = FlowManager::Instance()->GetAllObject(FLOW_DISK, flowMaps);
    if (ret != BIO_OK) {
        LOG_ERROR("Get flow list fail:" << ret);
        return ret;
    }

    for (auto &elem : flowMaps) {
        uint64_t type = CacheFlowIdManager::GetType(elem.first);
        uint64_t innerType = CacheFlowIdManager::GetInnerType(elem.first);
        if (static_cast<uint16_t>(type) == CACHE_FLOW_ID_PREFIX_TYPE_WCACHE &&
            static_cast<uint32_t>(innerType) == WCACHE_FLOW_DISK_META_PREFIX) {
            ret = mWCacheManager->RecoverCache(elem.second);
            ChkTrueNot(ret == BIO_OK, ret);
        } else if (static_cast<uint16_t>(type) == CACHE_FLOW_ID_PREFIX_TYPE_RCACHE &&
            static_cast<uint32_t>(innerType) == RCACHE_FLOW_DISK_DATA_PREFIX) {
            ret = mRCacheManager->RecoverCache(elem.second);
            ChkTrueNot(ret == BIO_OK, ret);
        }
    }
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

BResult Cache::CreateWCache(uint64_t procId, uint64_t ptId, uint64_t ptv, uint16_t diskId, uint64_t flowId)
{
    BIO_TRACE_START(WCACHE_TRACE_CREATE_OBJ);
    auto ret = mWCacheManager->CreateWCache(procId, flowId, ptId, ptv, diskId);
    BIO_TRACE_END(WCACHE_TRACE_CREATE_OBJ, ret);
    ChkTrue(ret == BIO_OK, ret, "Failed to create WCache, procId:"
        << procId << ", ptId:" << ptId << ", flowId:" << flowId << ".");
    LOG_DEBUG("Create wcache success, cacheId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId << ".");
    return BIO_OK;
}

BResult Cache::CreateRCache(uint64_t ptId, uint16_t diskId)
{
    BIO_TRACE_START(RCACHE_TRACE_CREATE_OBJ);
    auto ret = mRCacheManager->CreateRCache(ptId, diskId);
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
    uint64_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());
    bool isDegrade = false;
    auto ret = mCheckDegrade(static_cast<uint16_t>(ptId), isDegrade);
    ChkTrueNot(ret == BIO_OK, ret);

    BIO_TRACE_START(WCACHE_TRACE_PUT);
    ret = mWCacheManager->Put(key, slice, sliceReader, attr, isDegrade);
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

    UnderFs::ObjStat stat;
    ret = UnderFs::Instance()->Stat(key, stat);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get key " << key << " stat from under fs failed, error code: " << ret);
    } else {
        cacheObjStat.time = stat.time;
        cacheObjStat.size = stat.size;
        LOG_INFO("UnderFS stat success, key:" << key << ", size:" << cacheObjStat.size << ", time:" <<
            cacheObjStat.time << ".");
    }
    return ret;
}

BResult Cache::List(char *prefix, uint16_t ptId, uint32_t flag, std::unordered_map<std::string, CacheObjStat> &objs)
{
    BResult ret = mWCacheManager->List(prefix, ptId, objs);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_WARN("Write cache list failed, ret:" << ret << ", prefix:" << prefix << ", ptId:" << ptId << ".");
    }
    if (UNLIKELY(objs.size() >= 1000U)) {
        return BIO_OK;
    }

    if (flag == 1) {
        std::unordered_map<std::string, UnderFs::ObjStat> underStatInfo;
        ret = UnderFs::Instance()->List(prefix, underStatInfo);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("UnderFS list failed, ret:" << ret << ", prefix:" << prefix << ".");
        }
        for (auto &info : underStatInfo) {
            if (objs.size() >= 1000U) {
                return BIO_OK;
            }
            LOG_INFO("UnderFS list success, key:" << info.first << ", size:" <<
                info.second.size << ", time:" << info.second.time << ".");
            objs.insert({ info.first, { info.second.size, info.second.time } });
        }
    }

    return BIO_OK;
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

void Cache::RegGetLocDiskId(GetLocDiskId getLocDiskId)
{
    mGetLocDiskId = getLocDiskId;
}

void Cache::RegCheckDegrade(CheckDegrade checkDegrade)
{
    mCheckDegrade = checkDegrade;
}

void Cache::RegGetGlobEvictOffset(GetGlobEvictOffset evictOffset)
{
    mWCacheManager->RegGetGlobEvictOffset(evictOffset);
}

void Cache::RegCacheMalloc(CacheMalloc memMalloc)
{
    mMemMalloc = memMalloc;
}

void Cache::RegCacheFree(CacheFree memFree)
{
    mMemFree = memFree;
}

BResult Cache::GetEvictOffset(uint64_t flowId, uint64_t &flowOffset)
{
    return mWCacheManager->GetEvictOffset(flowId, flowOffset);
}

BResult Cache::Flush(uint64_t ptId, uint64_t ptv)
{
    BResult ret = ExtraCreateRCache(ptId);
    if (ret != BIO_OK) {
        LOG_ERROR("Extra create rcache fail:" << ret << ", ptId:" << ptId);
        return ret;
    }

    BIO_TRACE_START(WCACHE_TRACE_FLUSH);
    ret = mWCacheManager->Flush(ptId, ptv);
    BIO_TRACE_END(WCACHE_TRACE_FLUSH, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Flush failed:" << ret << ", ptId:" << ptId << ", version:" << ptv);
        return ret;
    }
    return BIO_OK;
}

BResult Cache::ExpiredClear(uint64_t ptId, uint64_t ptv, bool retained)
{
    BResult ret;

    if (retained) {
        ret = ExtraCreateRCache(ptId);
        if (ret != BIO_OK) {
            LOG_ERROR("Extra create rcache fail:" << ret << ", ptId:" << ptId);
            return ret;
        }
    } else {
        BIO_TRACE_START(RCACHE_TRACE_CLEAR_EXPIRED);
        ret = mRCacheManager->ExpiredClear(ptId, ptv);
        BIO_TRACE_END(RCACHE_TRACE_CLEAR_EXPIRED, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Expired clear fail:" << ret << ", ptId:" << ptId << ", version:" << ptv);
            return ret;
        }
    }

    BIO_TRACE_START(WCACHE_TRACE_CLEAR_EXPIRED);
    ret = mWCacheManager->ExpiredClear(ptId, ptv);
    BIO_TRACE_END(WCACHE_TRACE_CLEAR_EXPIRED, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Expired clear fail:" << ret << ", ptId:" << ptId << ", version:" << ptv);
        return ret;
    }
    return BIO_OK;
}

BResult Cache::ExtraCreateRCache(uint64_t ptId)
{
    uint16_t diskId;
    BResult ret = mGetLocDiskId(static_cast<uint16_t>(ptId), diskId);
    if (ret != BIO_OK) {
        LOG_ERROR("Get loc disk fail:" << ret << ", ptId:" << ptId);
        return ret;
    }

    ret = CreateRCache(ptId, diskId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create read cache fail:" << ret << ", ptId:" << ptId);
        return ret;
    }

    return BIO_OK;
}
}
}
