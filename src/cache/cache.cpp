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
#include "cache_overload_ctrl.h"

namespace ock {
namespace bio {
BResult Cache::Init()
{
    mRCacheManager = RCacheManager::Instance();
    ChkTrueNot(mRCacheManager != nullptr, BIO_ALLOC_FAIL);
    auto ret = mRCacheManager->Init();
    ChkTrue(ret == BIO_OK, ret, "Initialize read cache manager failed, ret:" << ret << ".");

    mWCacheManager = WCacheManager::Instance();
    ChkTrueNot(mWCacheManager != nullptr, BIO_ALLOC_FAIL);
    ret = mWCacheManager->Init(mRCacheManager);
    ChkTrue(ret == BIO_OK, ret, "Initialize write cache manager failed, ret:" << ret << ".");

    ret = CacheOverloadCtrl::Instance().Initialize();
    ChkTrue(ret == BIO_OK, ret, "Initialize overload ctrl failed, ret:" << ret << ".");

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

BResult Cache::CreateWCache(uint64_t procId, uint64_t ptId, uint64_t ptv, uint16_t diskId, uint64_t flowId)
{
    BIO_TRACE_START(WCACHE_TRACE_CREATE_OBJ);
    auto ret = mWCacheManager->CreateWCache(procId, flowId, ptId, ptv, diskId);
    BIO_TRACE_END(WCACHE_TRACE_CREATE_OBJ, ret);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to create WCache, procId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId << ".");
    LOG_DEBUG("Create wcache success, cacheId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId << ".");
    return BIO_OK;
}

BResult Cache::CreateRCache(uint64_t ptId, uint64_t ptv, uint16_t diskId)
{
    BIO_TRACE_START(RCACHE_TRACE_CREATE_OBJ);
    auto ret = mRCacheManager->CreateRCache(ptId, ptv, diskId);
    BIO_TRACE_END(RCACHE_TRACE_CREATE_OBJ, ret);
    ChkTrue(ret == BIO_OK, ret, "Failed to create RCache, ptId:" << ptId);

    return BIO_OK;
}

BResult Cache::DestroyWCache(uint64_t procId, uint64_t ptId, uint64_t ptv, uint64_t flowId)
{
    BIO_TRACE_START(WCACHE_TRACE_DESTROY_OBJ);
    auto ret = mWCacheManager->DestroyWCache(procId, flowId, ptId, ptv);
    BIO_TRACE_END(WCACHE_TRACE_DESTROY_OBJ, ret);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to destroy WCache, procId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId << ".");
    LOG_DEBUG("Destroy wcache success, cacheId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId << ".");
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
    if (ret != BIO_OK) {
        LOG_ERROR("Check degrade failed, ret:" << ret << ", ptId:" << ptId << ", isDegrade:" << isDegrade << ".");
        return ret;
    }

    BIO_TRACE_START(WCACHE_TRACE_PUT);
    ret = mWCacheManager->Put(key, slice, sliceReader, attr, isDegrade);
    BIO_TRACE_END(WCACHE_TRACE_PUT, ret);

    if (ret == BIO_OK) {
        CacheOverloadCtrl::Instance().AddBandwidth(BW_STAT_FRONT_WRITE, slice->GetLength());
    }
    return ret;
}

BResult Cache::Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
    uint64_t &realLen)
{
    BIO_TRACE_START(WCACHE_TRACE_GET);
    auto ret = mWCacheManager->Get(key, offset, slice, sliceWriter, realLen);
    BIO_TRACE_END(WCACHE_TRACE_GET, ret);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Write cache get failed, ret:" << ret << ", key:" << key << ", offset:" << offset << ", length:" <<
            slice->GetLength() << ".");
        return ret;
    } else if (ret == BIO_OK) {
        LOG_INFO("Write cache hit, key:" << key << ", offset:" << offset << ", len:" << slice->GetLength() << ".");
        return BIO_OK;
    }

    BIO_TRACE_START(RCACHE_TRACE_GET);
    ret = mRCacheManager->Get(slice->GetPtId(), key, offset, slice.Get(), sliceWriter, realLen);
    BIO_TRACE_END(RCACHE_TRACE_GET, ret);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Get key " << key << " from read cache failed, ret:" << ret);
        return ret;
    } else if (ret == BIO_OK) {
        LOG_INFO("Read cache hit, key:" << key << ", offset:" << offset << ", len:" << slice->GetLength() << ".");
        return BIO_OK;
    }

    ret = mRCacheManager->Load(slice->GetPtId(), key, 0, BIO_IO_MAX_LEN, realLen);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Read cache load failed, ret:" << ret << ", key " << key << ".");
    } else {
        ret = mRCacheManager->Get(slice->GetPtId(), key, offset, slice.Get(), sliceWriter, realLen);
        if (LIKELY(ret == BIO_OK)) {
            LOG_INFO("Read cache hit, key:" << key << ", offset:" << offset << ", len:" << slice->GetLength() << ".");
        }
    }
    return ret;
}

BResult Cache::Load(uint64_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen)
{
    return mRCacheManager->Load(ptId, key, 0, BIO_IO_MAX_LEN, realLen);
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

BResult Cache::List(char *prefix, uint16_t ptId, bool force, std::unordered_map<std::string, CacheObjStat> &objs)
{
    BResult ret = mWCacheManager->List(prefix, ptId, objs);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_WARN("Write cache list failed, ret:" << ret << ", prefix:" << prefix << ", ptId:" << ptId << ".");
    }
    if (UNLIKELY(objs.size() >= 1000U)) {
        return BIO_OK;
    }

    if (force) {
        std::unordered_map<std::string, UnderFs::ObjStat> underStatInfo;
        ret = UnderFs::Instance()->List(prefix, underStatInfo);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("UnderFS list failed, ret:" << ret << ", prefix:" << prefix << ".");
        }
        for (auto &info : underStatInfo) {
            if (objs.size() >= 1000U) {
                return BIO_OK;
            }
            LOG_INFO("UnderFS list success, key:" << info.first << ", size:" << info.second.size << ", time:" <<
                info.second.time << ".");
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
    }

    BIO_TRACE_START(RCACHE_TRACE_DEL);
    ret = mRCacheManager->Delete(ptId, key);
    BIO_TRACE_END(RCACHE_TRACE_DEL, ret);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Read cache delete failed, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = UnderFs::Instance()->Delete(key);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Under fs delete failed, ret:" << ret << ", key " << key << ".");
        return ret;
    }

    if (ret == BIO_NOT_EXISTS) {
        LOG_WARN("Not found key in delete process, key " << key << ", ptId:" << ptId << ".");
        ret = BIO_OK;
    }
    return ret;
}

void Cache::RegGetLocDiskId(GetLocDiskId getLocDiskId)
{
    mGetLocDiskId = getLocDiskId;
}

void Cache::RegGetLocDiskStatus(GetLocDiskStatus getLocDiskStatus)
{
    mWCacheManager->RegGetLocDiskStatus(getLocDiskStatus);
}

void Cache::RegCheckDegrade(CheckDegrade checkDegrade)
{
    mCheckDegrade = checkDegrade;
}

void Cache::RegGetGlobEvictOffset(GetGlobEvictOffset evictOffset)
{
    mWCacheManager->RegGetGlobEvictOffset(evictOffset);
}

void Cache::RegCheckLocRole(CheckLocRole locRole)
{
    mWCacheManager->RegCheckLocRole(locRole);
}

BResult Cache::GetEvictOffset(uint64_t flowId, uint64_t &flowOffset)
{
    return mWCacheManager->GetEvictOffset(flowId, flowOffset);
}

BResult Cache::HandleProcBroken(uint32_t procId)
{
    return mWCacheManager->HandleProcBroken(procId);
}

BResult Cache::Flush(uint64_t ptId, uint64_t ptv)
{
    BIO_TRACE_START(WCACHE_TRACE_FLUSH);
    auto ret = mWCacheManager->Flush(ptId, ptv);
    BIO_TRACE_END(WCACHE_TRACE_FLUSH, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Flush failed:" << ret << ", ptId:" << ptId << ", version:" << ptv);
        return ret;
    }
    return BIO_OK;
}

BResult Cache::ExpiredClear(uint64_t ptId, uint64_t ptv)
{
    BResult ret;

    BIO_TRACE_START(RCACHE_TRACE_CLEAR_EXPIRED);
    ret = mRCacheManager->ExpiredClear(ptId, ptv);
    BIO_TRACE_END(RCACHE_TRACE_CLEAR_EXPIRED, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Expired clear fail:" << ret << ", ptId:" << ptId << ", version:" << ptv);
        return ret;
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

BResult Cache::ExtraCreateRCache(uint64_t ptId, uint64_t ptv)
{
    uint16_t diskId;
    BResult ret = mGetLocDiskId(static_cast<uint16_t>(ptId), diskId);
    if (ret != BIO_OK) {
        LOG_ERROR("Get loc disk fail:" << ret << ", ptId:" << ptId);
        return ret;
    }

    ret = CreateRCache(ptId, ptv, diskId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create read cache fail:" << ret << ", ptId:" << ptId);
        return ret;
    }

    return BIO_OK;
}

void Cache::GetCacheResources(uint64_t &memTotal, uint64_t &memUsed, uint64_t &diskTotal, uint64_t &diskUsed)
{
    memTotal = 1;
    memUsed = 0;
    diskTotal = 1;
    diskUsed = 0;
}
}
}
