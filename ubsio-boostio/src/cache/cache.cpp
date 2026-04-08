/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "cache.h"
#include "cache_flow.h"
#include "ufs_helper.h"
#include "flow_manager.h"
#include "bio.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "cm.h"
#include "cache_overload_ctrl.h"

namespace ock {
namespace bio {
BResult Cache::Init()
{
    mRCacheManager = RCacheManager::Instance();
    ChkTrueNot(mRCacheManager != nullptr, BIO_ALLOC_FAIL);

    BResult ret = BIO_OK;
    BIO_TP_START(RCACHE_MANAGER_INIT_FAIL, &ret, BIO_ERR);
    ret = mRCacheManager->Init();
    BIO_TP_END;
    ChkTrue(ret == BIO_OK, ret, "Initialize read cache manager failed, ret:" << ret << ".");

    mWCacheManager = WCacheManager::Instance();
    ChkTrueNot(mWCacheManager != nullptr, BIO_ALLOC_FAIL);

    ret = mWCacheManager->Init(mRCacheManager);
    ChkTrue(ret == BIO_OK, ret, "Initialize write cache manager failed, ret:" << ret << ".");

    ret = CacheOverloadCtrl::Instance().Initialize();
    ChkTrue(ret == BIO_OK, ret, "Initialize overload ctrl failed, ret:" << ret << ".");
    mUfsEnable = BioConfig::Instance()->GetUnderFsConfig().underFsType != "none";
    return BIO_OK;
}

BResult Cache::Recover()
{
    BResult ret = BIO_OK;
    std::map<uint64_t, FlowPtr> flowMaps;
    BIO_TP_START(CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL, &ret, BIO_NOT_READY);
    ret = FlowManager::Instance()->GetAllObject(FLOW_DISK, flowMaps);
    BIO_TP_END;
    if (ret != BIO_OK) {
        LOG_ERROR("Get flow list fail:" << ret);
        return ret;
    }

    uint64_t type = 0;
    uint64_t innerType = 0;
    for (auto &elem : flowMaps) {
        BIO_TP_START(CACHE_RECOVER_TYPE_FAIL, &type, READ_CACHE);
        type = CacheFlowIdManager::GetType(elem.first);
        BIO_TP_END;
        BIO_TP_START(CACHE_RECOVER_TYPE_INNER_FAIL, &innerType, RCACHE_FLOW_DISK_DATA_PREFIX);
        innerType = CacheFlowIdManager::GetInnerType(elem.first);
        BIO_TP_END;
        if (static_cast<uint16_t>(type) == WRITE_CACHE &&
            static_cast<uint32_t>(innerType) == WCACHE_FLOW_DISK_META_PREFIX) {
            ret = mWCacheManager->RecoverCache(elem.second);
            ChkTrue(ret == BIO_OK, ret, "Recover cache failed, ret:" << ret << ".");
        } else if (static_cast<uint16_t>(type) == READ_CACHE &&
            static_cast<uint32_t>(innerType) == RCACHE_FLOW_DISK_DATA_PREFIX) {
            BIO_TP_START(CACHE_RECOVER_CACHE_FAIL, &ret, BIO_ERR);
            ret = mRCacheManager->RecoverCache(elem.second);
            BIO_TP_END;
            ChkTrue(ret == BIO_OK, ret, "Recover cache failed, ret:" << ret << ".");
        }
    }

    return BIO_OK;
}

void Cache::Exit()
{
    mWCacheManager->Exit();
    mRCacheManager->Exit();
}

BResult Cache::CreateWCache(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId, bool isDegrade)
{
    uint16_t diskId;
    BResult ret = mGetLocDiskId(static_cast<uint16_t>(ptId), diskId);
    if (ret != BIO_OK) {
        LOG_ERROR("Get loc disk fail:" << ret << ", ptId:" << ptId);
        return BIO_CHECK_PT_FAIL;
    }

    bool isPtDegrade = false;
    ret = mCheckDegrade(ptId, isPtDegrade);
    if (ret != BIO_OK) {
        LOG_ERROR("Check pt degrade failed:" << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    LOG_INFO("Create wcache, flowId:" << flowId << ", isDegrade:" << isDegrade << ", isPtDegrade:" << isPtDegrade);

    bool mixDegrade = (isDegrade || isPtDegrade);

    BIO_TRACE_START(WCACHE_TRACE_CREATE_OBJ);
    ret = mWCacheManager->CreateWCache(procId, flowId, ptId, ptv, diskId, mixDegrade);
    BIO_TRACE_END(WCACHE_TRACE_CREATE_OBJ, ret);
    ChkTrue(ret == BIO_OK, ret,
        "Failed to create WCache, procId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId << ".");

    return BIO_OK;
}

BResult Cache::DestroyWCache(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId)
{
    BResult ret = BIO_OK;
    BIO_TRACE_START(WCACHE_TRACE_DESTROY_OBJ);
    ret = mWCacheManager->DestroyWCache(procId, flowId, ptId, ptv);
    BIO_TRACE_END(WCACHE_TRACE_DESTROY_OBJ, ret);
    LOG_TRACE("Destroy wcache finish, ret:" << ret << ", cacheId:" << procId << ", ptId:" << ptId << ", flowId:" <<
        flowId << ".");
    return ret;
}

BResult Cache::CreateRCache(uint16_t ptId, uint64_t ptv)
{
    uint16_t diskId;
    BResult ret = mGetLocDiskId(ptId, diskId);
    if (ret != BIO_OK) {
        LOG_ERROR("Get loc disk fail:" << ret << ", ptId:" << ptId);
        return BIO_CHECK_PT_FAIL;
    }

    BIO_TRACE_START(RCACHE_TRACE_CREATE_OBJ);
    ret = mRCacheManager->CreateRCache(ptId, ptv, diskId);
    BIO_TRACE_END(RCACHE_TRACE_CREATE_OBJ, ret);
    ChkTrue(ret == BIO_OK, ret, "Failed to create RCache, ptId:" << ptId);

    return BIO_OK;
}

BResult Cache::DestroyRCache(uint16_t ptId)
{
    return mRCacheManager->DeleteRCache(ptId);
}

BResult Cache::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    return mWCacheManager->GetWCacheSlice(sliceKey, slice);
}

BResult Cache::ServiceUngradeFlush()
{
    return mWCacheManager->ServiceUngradeFlush();
}

BResult Cache::Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr)
{
    // 1. 获取PT视图状态和服务状态, 综合得到本次Put的IO是否需要降级处理.
    bool isDegrade = mCheckService();
    bool isPtDegrade = false;
    uint16_t ptId = CacheFlowIdManager::GetPtId(slice->GetFlowId());
    auto ret = mCheckDegrade(static_cast<uint16_t>(ptId), isPtDegrade);
    if (ret != BIO_OK) {
        LOG_ERROR("Check degrade failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return ret;
    }
    bool mixDegrade = (isDegrade || isPtDegrade);

    // 2. 写入到write cache中.
    BIO_TRACE_START(WCACHE_TRACE_PUT);
    ret = mWCacheManager->Put(key, slice, sliceReader, attr, mixDegrade);
    BIO_TRACE_END(WCACHE_TRACE_PUT, ret);
    if (ret == BIO_OK) {
        CacheOverloadCtrl::Instance().AddBandwidth(BW_STAT_FRONT_WRITE, slice->GetLength());
    }
    return ret;
}

BResult Cache::GetFromUnderFS(const Key &key, WCacheSlicePtr &slice, const size_t length, const uint64_t offset)
{
    BResult ret = BIO_INNER_ERR;
    std::vector<FlowAddr> addrVec = slice->GetAddrs();
    if (LIKELY(addrVec.size() == NO_1)) {
        ret = UfsHelper::Instance()->Get(key, reinterpret_cast<char *>(addrVec[0].chunkId + addrVec[0].chunkOffset),
            length, offset);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to get from underFs failed, ret:" << ret << ", key:" << key << ", len:" << length << ".");
        }
    } else {
        void *value = aligned_alloc(NO_4096, length);
        ChkTrue(value != nullptr, BIO_ALLOC_FAIL, "Alloc memory aligned failed.");
        ret = UfsHelper::Instance()->Get(key, reinterpret_cast<char *>(value), length, offset);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to get from underFs failed, ret:" << ret << ", key:" << key << ", len:" << length << ".");
            free(value);
            return ret;
        }
        ret = mSliceOperator.Copy(reinterpret_cast<char *>(value), slice.Get());
        free(value);
        if (ret != BIO_OK) {
            LOG_ERROR("Slice copy failed, ret:" << ret << ", key:" << key << ", length:" << length << ".");
        }
    }
    return ret;
}

BResult Cache::GetValueLengthFromUnderFS(const Key &key, uint64_t readLen, uint64_t offset, uint64_t &totalLen,
    uint64_t &realLen)
{
    UfsHelper::ObjStat stat;
    auto ret = UfsHelper::Instance()->Stat(key, stat);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Stat key from under fs failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }

    totalLen = static_cast<uint64_t>(stat.size);
    if (UNLIKELY(offset >= totalLen)) {
        LOG_ERROR("Read exceed, input offset:" << offset << ", input length:" << readLen << ", totalLen:" << totalLen);
        return BIO_READ_EXCEED;
    }
    realLen = totalLen - offset;
    if (realLen > readLen) {
        realLen = readLen;
    }
    return BIO_OK;
}

BResult Cache::CalculateDataCrc(const SlicePtr &value, SlicePtr slice)
{
    uint32_t dataCrc = 0;
    auto ret = value->CalculateDataCrc(dataCrc, 0, value->GetLength());
    if (ret != BIO_OK) {
        LOG_ERROR("Calculate crc fail, ret" << ret << ".");
    } else {
        slice->SetDataCrc(dataCrc);
    }
    return ret;
}

inline BResult Cache::WriteToDesSlice(const SliceWriter &sliceWriter, SlicePtr fromSlice, SlicePtr destSlice,
    bool crcFlag, const Key &key)
{
    auto ret = sliceWriter(fromSlice, destSlice);
    if (ret != BIO_OK) {
        LOG_ERROR("Call slice writer failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }
    if (crcFlag) {
        ret = CalculateDataCrc(fromSlice, destSlice);
    }
    if (ret != BIO_OK) {
        LOG_ERROR("Call slice writer calculate crc failed, ret:" << ret << ", key:" << key << ".");
    }
    return ret;
}

BResult Cache::GetExternal(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
    uint64_t &realLen)
{
    // 1. 获取key的信息, 计算value的总长度和此次读取长度.

    uint64_t totalLen = 0;
    BResult ret = BIO_ERR;
    BIO_TP_START(GET_UNDERFS_NO_STAT, &ret, BIO_OK);
    ret = GetValueLengthFromUnderFS(key, slice->GetLength(), offset, totalLen, realLen);
    if (ret != BIO_OK) {
        LOG_ERROR("Get key info from under fs failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }
    BIO_TP_END;
    BIO_TP_START(GET_UNDERFS_MODIFY_REALLENGTH, &realLen, NO_60*NO_100);
    BIO_TP_END;

    // 2. 申请内存资源, 首先尝试从RCache中申请, 若失败则申请临时系统内存.
    bool isFromRCache = true;
    WCacheSlicePtr wcSlicePtr = nullptr;
    bool enoughResource = mRCacheManager->IsResourceEnough(slice->GetPtId());
    BIO_TP_START(GET_UNDERFS_NOT_ENOUGHRESOURCE, &enoughResource, false);
    BIO_TP_END;
    void *memAddr = nullptr;
    if (enoughResource) {
        BIO_TP_START(GET_EXTERNAL_RCACHE_MALLOC_FAIL, &wcSlicePtr, nullptr);
        mRCacheManager->AllocResources(slice->GetPtId(), totalLen, wcSlicePtr);
        BIO_TP_END
        if (LIKELY(wcSlicePtr == nullptr)) {
            memAddr = aligned_alloc(NO_4096, realLen);
            ChkTrue(memAddr != nullptr, BIO_ALLOC_FAIL, "Alloc aligned memory failed, size:" << realLen << ".");
            isFromRCache = false;
            MrInfo mrInfo = { reinterpret_cast<uint64_t>(memAddr), static_cast<uint32_t>(realLen) };
            std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
            wcSlicePtr = MakeRef<WCacheSlice>(0, 0, 0, realLen, addrVec, FLOW_MEMORY);
        }
    } else {
        memAddr = aligned_alloc(NO_4096, realLen);
        ChkTrue(memAddr != nullptr, BIO_ALLOC_FAIL, "Alloc aligned memory failed, size:" << realLen << ".");
        isFromRCache = false;
        MrInfo mrInfo = { reinterpret_cast<uint64_t>(memAddr), static_cast<uint32_t>(realLen) };
        std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
        wcSlicePtr = MakeRef<WCacheSlice>(0, 0, 0, realLen, addrVec, FLOW_MEMORY);
    }
    if (wcSlicePtr == nullptr) {
        LOG_ERROR("Alloc wCacheSlicePtr failed.");
        if (memAddr != nullptr) {
            free(memAddr);
            memAddr = nullptr;
        }
        return BIO_ALLOC_FAIL;
    }
    bool crcFlag = false;
    crcFlag = BioConfig::Instance()->GetDaemonConfig().enableCrc;
    BIO_TP_START(GET_UNDERFS_ENABLE_CRC, &crcFlag, true);
    BIO_TP_END;
    ret = BIO_INNER_ERR;
    do {
        // 3. 从underFS读取数据.
        if (isFromRCache) {
            ret = GetFromUnderFS(key, wcSlicePtr, totalLen, 0);
        } else {
            ret = GetFromUnderFS(key, wcSlicePtr, realLen, offset);
        }
        BIO_TP_START(GET_EXTERNAL_GETUNDERFS_OK, &ret, BIO_OK);
        BIO_TP_END;

        if (ret != BIO_OK) {
            break;
        }

        // 4. 将数据写到目的Slice中.
        if (isFromRCache) {
            SlicePtr partailSlice = nullptr;
            ret = mSliceOperator.GetSliceFromSliceIO(partailSlice, wcSlicePtr.Get(), offset, realLen);
            if (ret != BIO_OK) {
                LOG_ERROR("Get partial slice from whole slice fail.");
                break;
            }
            ret = WriteToDesSlice(sliceWriter, partailSlice, slice.Get(), crcFlag, key);
        } else {
            ret = WriteToDesSlice(sliceWriter, wcSlicePtr.Get(), slice.Get(), crcFlag, key);
        }
        if (ret != BIO_OK) {
            LOG_ERROR("Call slice writer to dest slice failed, key:" << key << ", ret" << ret << ".");
            break;
        }

        // 5. 根据资源来历决定是否写入到RCache.
        if (isFromRCache) {
            if (crcFlag) {
                ret = CalculateDataCrc(wcSlicePtr.Get(), wcSlicePtr.Get());
            }
            if (ret != BIO_OK) {
                break;
            }
            ret = mRCacheManager->Put(slice->GetPtId(), key, wcSlicePtr);
            LOG_DEBUG("Get key info from under insert  read cache, ret:" << ret << ", key:" << key << ".");
        }
    } while (false);

    if (!isFromRCache) {
        free(reinterpret_cast<char *>(wcSlicePtr->GetAddrs()[0].chunkId));
    }
    return ret;
}

BResult Cache::ParseKeyAddr(const Key &key, uint16_t ptId, BatchKeyAddrInfo *info)
{
    return mWCacheManager->ParseKeyAddr(key, ptId, info);
}

BResult Cache::Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
    uint64_t &realLen)
{
    BResult ret = BIO_INNER_ERR;
    // 1. 首先从WCache中读取数据, 对象不存在则执行步骤2.
    BIO_TP_START(WCACHE_GET_OK, &ret, BIO_OK);
    BIO_TP_START(WCACHE_NOT_EXIST, &ret, BIO_NOT_EXISTS);
    BIO_TRACE_START(WCACHE_TRACE_GET);
    ret = mWCacheManager->Get(key, offset, slice, sliceWriter, realLen);
    BIO_TRACE_END(WCACHE_TRACE_GET, ret);
    BIO_TP_END;
    BIO_TP_END;
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Write cache get failed, ret:" << ret << ", key:" << key << ", offset:" << offset << ", length:" <<
            (slice == nullptr ? 0 : slice->GetLength()) << ".");
        return ret;
    }

    WCacheStatistic::Instance().IncTotalCount();
    if (ret == BIO_OK) {
        LOG_DEBUG("Write cache hit, key:" << key << ", offset:" << offset << ", len:" <<
            (slice == nullptr ? 0 : slice->GetLength()) << ".");
        WCacheStatistic::Instance().IncHitCount();
        return BIO_OK;
    }

    // 2. 然后从RCache中读取数据, 对象不存在则执行步骤3.
    BIO_TRACE_START(RCACHE_TRACE_GET);
    BIO_TP_START(RCACHE_NOT_EXIST, &ret, BIO_NOT_EXISTS);
    ret = mRCacheManager->Get(slice->GetPtId(), key, offset, slice.Get(), sliceWriter, realLen);
    BIO_TP_END;
    BIO_TRACE_END(RCACHE_TRACE_GET, ret);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Read cache get failed, ret:" << ret << ", key:" << key << ", offset:" << offset << ", length:" <<
            (slice == nullptr ? 0 : slice->GetLength()) << ".");
        return ret;
    }

    RCacheStatistic::Instance().IncTotalCount();
    if (ret == BIO_OK) {
        LOG_DEBUG("Read cache hit, key:" << key << ", offset:" << offset << ", len:" << slice->GetLength() << ".");
        RCacheStatistic::Instance().IncHitCount();
        return BIO_OK;
    }
    if (!mUfsEnable) { // 未使能underfs则直接返回结果.
        return ret;
    }

    // 3. 最后从外部存储中读取数据.
    BIO_TRACE_START(EXTERNAL_TRACE_GET);
    ret = GetExternal(key, offset, slice, sliceWriter, realLen);
    BIO_TRACE_END(EXTERNAL_TRACE_GET, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("External storage get failed, ret:" << ret << ", key:" << key << ", offset:" << offset <<
            ", length:" << slice->GetLength() << ".");
        return ret;
    }
    DiskStatistic::Instance().IncHitCount();
    return BIO_OK;
}

BResult Cache::Load(uint16_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen)
{
    return mRCacheManager->Load(ptId, key, 0, BIO_IO_MAX_LEN, realLen);
}

BResult Cache::Stat(uint16_t ptId, const Key &key, CacheObjStat &cacheObjStat)
{
    BIO_TRACE_START(WCACHE_TRACE_STAT);
    auto ret = mWCacheManager->Stat(ptId, key, cacheObjStat);
    BIO_TRACE_END(WCACHE_TRACE_STAT, ret);
    if ((ret == BIO_OK) || (ret != BIO_NOT_EXISTS)) {
        return ret;
    }
    if (!mUfsEnable) {  // 未使能underfs则直接返回结果.
        return ret;
    }

    UfsHelper::ObjStat stat;
    ret = UfsHelper::Instance()->Stat(key, stat);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get key " << key << " stat from under fs failed, error code: " << ret);
    } else {
        cacheObjStat.time = stat.time;
        cacheObjStat.size = stat.size;
        LOG_DEBUG("UnderFS stat success, key:" << key << ", size:" << cacheObjStat.size << ", time:" <<
            cacheObjStat.time << ".");
    }
    return ret;
}

bool Cache::Exist(uint16_t ptId, const Key &key)
{
    BIO_TRACE_START(WCACHE_TRACE_EXIST);
    auto ret = mWCacheManager->Exist(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_EXIST, ret ? BIO_OK : BIO_NOT_EXISTS);
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

    if (mUfsEnable && force) {
        std::unordered_map<std::string, UfsHelper::ObjStat> underStatInfo;
        BIO_TP_START(UNDERFS_INIT_FAIL, &ret, BIO_ERR);
        ret = UfsHelper::Instance()->List(prefix, underStatInfo);
        BIO_TP_END;
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("UnderFS list failed, ret:" << ret << ", prefix:" << prefix << ".");
            return ret;
        }
        for (auto &info : underStatInfo) {
            if (objs.size() >= 1000U) {
                return BIO_OK;
            }
            LOG_DEBUG("UnderFS list success, key:" << info.first << ", size:" << info.second.size << ", time:" <<
                info.second.time << ".");
            objs.insert({ info.first, { info.second.size, info.second.time } });
        }
    }

    return BIO_OK;
}

BResult Cache::Delete(uint16_t ptId, const Key &key)
{
    BIO_TRACE_START(WCACHE_TRACE_DEL);
    BResult ret = mWCacheManager->Delete(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_DEL, ret);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Write cache delete failed, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ".");
        return ret;
    }

    BIO_TRACE_START(RCACHE_TRACE_DEL);
    BIO_TP_START(CACHE_DELETE_RCACHE_MANAGER_ERR, &ret, BIO_ERR);
    ret = mRCacheManager->Delete(ptId, key);
    BIO_TP_END;
    BIO_TRACE_END(RCACHE_TRACE_DEL, ret);
    if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
        LOG_ERROR("Read cache delete failed, ret:" << ret << ", key:" << key << ", ptId:" << ptId << ".");
        return ret;
    }

    if (mUfsEnable) {  // 使能underfs才去从后端删除key.
        ret = UfsHelper::Instance()->Delete(key);
        if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
            LOG_ERROR("Under fs delete failed, ret:" << ret << ", key " << key << ".");
            return ret;
        }
    }

    if (ret == BIO_NOT_EXISTS) {
        LOG_WARN("Not found key in delete process, key " << key << ", ptId:" << ptId << ".");
        ret = BIO_OK;
    }
    return ret;
}

FlowCache Cache::GetFlowCache(uint64_t flowId)
{
    uint64_t type = CacheFlowIdManager::GetType(flowId);
    if (static_cast<CacheType>(type) == WRITE_CACHE) {
        return FLOW_WCACHE;
    } else {
        return FLOW_RCACHE;
    }
}

void Cache::RegGetLocDiskId(GetLocDiskId getLocDiskId)
{
    mGetLocDiskId = getLocDiskId;
}

void Cache::RegGetLocDiskStatus(GetLocDiskStatus getLocDiskStatus)
{
    mWCacheManager->RegGetLocDiskStatus(getLocDiskStatus);
}

void Cache::RegCheckServiceState(CheckServiceState checkService)
{
    mCheckService = checkService;
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

BResult Cache::Flush(uint16_t ptId, uint64_t ptv)
{
    BIO_TRACE_START(WCACHE_TRACE_FLUSH);
    auto ret = mWCacheManager->Flush(ptId, ptv);
    BIO_TRACE_END(WCACHE_TRACE_FLUSH, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_WARN("Flush failed:" << ret << ", ptId:" << ptId << ", pt version:" << ptv << ".");
    }
    return ret;
}

BResult Cache::ExpiredClear(uint16_t ptId, uint64_t ptv)
{
    BIO_TRACE_START(RCACHE_TRACE_CLEAR_EXPIRED);
    BResult ret = mRCacheManager->ExpiredClear(ptId, ptv);
    BIO_TRACE_END(RCACHE_TRACE_CLEAR_EXPIRED, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Rcache expired clear fail:" << ret << ", ptId:" << ptId << ", version:" << ptv);
        return ret;
    }

    BIO_TRACE_START(WCACHE_TRACE_CLEAR_EXPIRED);
    ret = mWCacheManager->ExpiredClear(ptId, ptv);
    BIO_TRACE_END(WCACHE_TRACE_CLEAR_EXPIRED, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Wcache expired clear fail:" << ret << ", ptId:" << ptId << ", version:" << ptv);
    }
    return ret;
}

BResult Cache::ExtraCreateRCache(uint16_t ptId, uint64_t ptv)
{
    return CreateRCache(ptId, ptv);
}

void Cache::GetCacheResources(CacheResDescription &desc, CacheType type)
{
    if (type == WRITE_CACHE) {
        WCache::GetCacheResource(desc.memCapacity, desc.memUsedSize, desc.diskCapacity, desc.diskUsedSize);
    } else if (type == READ_CACHE) {
        RCache::GetCacheResource(desc.memCapacity, desc.memUsedSize, desc.diskCapacity, desc.diskUsedSize);
    }
}

BResult Cache::EvictNegotiate(uint64_t &flowId, uint64_t &truncateIndex)
{
    return mWCacheManager->GetTruncateIndex(flowId, truncateIndex);
}

BResult Cache::ProcBrokenSyncFlow(uint64_t flowId, uint64_t index, uint64_t offset, bool &needDestroy)
{
    return mWCacheManager->ProcBrokenSyncOldFlow(flowId, index, offset, needDestroy);
}

}
}
