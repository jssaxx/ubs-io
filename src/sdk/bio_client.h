/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BIO_CLIENT_H
#define BIO_CLIENT_H

#include <atomic>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <utility>
#include "net_engine.h"
#include "net_common.h"
#include "bio_ref.h"
#include "bio.h"
#include "bio_lock.h"
#include "mirror_client.h"
#include "bio_client_net.h"
#include "ufs_helper.h"
#include "cli.h"
#include "sdk_diagnose.h"

#ifdef USE_PROMETHEUS
#include "prometheus_manager.h"
#endif

namespace ock {
namespace bio {
class BioClient;
using BioClientPtr = Ref<BioClient>;
class BioClient {
public:
    static constexpr uint32_t DEFAULT_MAX_CACHE_SIZE = 1024;

    static BioClientPtr &Instance()
    {
        static auto instance = MakeRef<BioClient>();
        return instance;
    }

    BResult Start(WorkerMode mode, const ClientOptionsConfig &optConf);
    void Exit();

    inline bool Ready() const
    {
        return mStarted;
    }

    inline void SetStartWorker(bool value)
    {
        std::lock_guard<std::mutex> lock(mStartLock);
        mStarted = value;
    }

    inline BResult CalculateLocation(const uint64_t objectId, AffinityStrategy affinity, ObjLocation &location)
    {
        uint16_t ptId = mMirror->SelectingPt(objectId, affinity);
        if (UNLIKELY(ptId == UINT16_MAX)) {
            return BIO_CHECK_PT_FAIL;
        }
        location.location[0] = (static_cast<uint64_t>(ptId) & 0x000000000000FFFF);
        location.location[1] = 0ULL;
        return BIO_OK;
    }

    inline BResult ParseLocation(ObjLocation &location)
    {
        return mMirror->ParseLocation(location);
    }

    inline BResult Put(MirrorClient::MirrorPut &param)
    {
        BResult ret = mMirror->Put(param);
        if (UNLIKELY(ret == BIO_INNER_RETRY || ret == BIO_CHECK_PT_FAIL || ret == BIO_QUOTA_NOT_ENOUGH ||
            ret == BIO_QUOTA_TIMEOUT)) {
            if (UfsHelper::Instance()->GetConfig().underFsType == "none") {
                CLIENT_LOG_WARN("Put underFs failed, underFs type is none, key:" << param.key << ",ret:" << ret << ".");
                return ret;
            }
            BIO_TRACE_START(SDK_TRACE_PUT_TO_UNDERFS);
            ret = UfsHelper::Instance()->Put(param.key, param.value, param.length);
            BIO_TRACE_END(SDK_TRACE_PUT_TO_UNDERFS, ret);
        }
        return ret;
    }

    inline BResult Put(MirrorClient::MirrorPut &param, CacheSpaceDesc &spaceInfo)
    {
        return mMirror->Put(param, spaceInfo);
    }

    inline BResult Get(MirrorClient::MirrorGet &param, uint64_t &length)
    {
        BResult ret = mMirror->Get(param, length);
        LVOS_TP_START(SDK_MIRROR_CLIENT_GET_RETRY, &ret, BIO_INNER_RETRY);
        LVOS_TP_END;
        if (UNLIKELY(ret == BIO_INNER_RETRY || ret == BIO_CHECK_PT_FAIL || ret == BIO_LOAD_ALLOC_FAIL)) {
            BIO_TRACE_START(SDK_TRACE_GET_TO_UNDERFS);
            if (UfsHelper::Instance()->GetConfig().underFsType == "none") {
                CLIENT_LOG_WARN("Get underFs failed, underFs type is none, key:" << param.key << ",ret:" << ret << ".");
                return ret;
            }
            UfsHelper::ObjStat stat;
            auto underFsRet = UfsHelper::Instance()->Stat(param.key, stat);
            LVOS_TP_START(SDK_CLIENT_GET_CEPH_STAT_OK, &underFsRet, BIO_OK);
            LVOS_TP_END;
            if (UNLIKELY(underFsRet != BIO_OK)) {
                BIO_TRACE_END(SDK_TRACE_GET_TO_UNDERFS, underFsRet);
                return ret;
            }
            LVOS_TP_START(SDK_CLIENT_GET_CEPH_STAT_OK, &stat.size, NO_1024);
            LVOS_TP_END;
            if (UNLIKELY(stat.size <= param.offset)) {
                BIO_TRACE_END(SDK_TRACE_GET_TO_UNDERFS, BIO_INVALID_PARAM);
                return BIO_INVALID_PARAM;
            }
            if (param.length + param.offset > stat.size) {
                length = stat.size - param.offset;
            } else {
                length = param.length;
            }
            underFsRet = UfsHelper::Instance()->Get(param.key, param.value, length, param.offset);
            BIO_TRACE_END(SDK_TRACE_GET_TO_UNDERFS, underFsRet);
            return underFsRet == BIO_OK ? BIO_OK : ret;
        }
        return ret;
    }

    inline BResult DeleteKey(const char *key, const ObjLocation &location)
    {
        return mMirror->DeleteKey(key, location);
    }

    inline BResult Load(LoadPara &para, const ObjLocation &location, Bio::LoadCallback callback, void *context)
    {
        return mMirror->Load(para, location, std::move(callback), context);
    }

    inline BResult ListAll(const char *prefix, std::unordered_map<std::string, ObjStat> &objs)
    {
        return mMirror->ListAll(prefix, objs);
    }

    inline BResult Stat(const char *key, const ObjLocation &location, ObjStat &stat)
    {
        return mMirror->StatObject(key, location, stat);
    }

    inline BResult AddDisk(const char *diskPath)
    {
        return mMirror->AddDisk(diskPath);
    }

    inline BResult NotifyUpdate(bool &flag)
    {
        return mMirror->NotifyUpdate(flag);
    }

    inline BResult CheckUpdateReady()
    {
        return mMirror->CheckUpdateReady();
    }

    inline BResult AllocSpace(MirrorClient::MirrorPut &param, CacheSpaceDesc &spaceInfo)
    {
        return mMirror->AllocSpace(param, spaceInfo);
    }

    inline BResult GetCacheHitRatio(std::unordered_map<uint16_t, CacheHitDesc> &nodeDesc)
    {
        return mMirror->GetCacheHitRatioImpl(nodeDesc);
    }

    inline BResult QueryCacheResource(std::vector<CacheResourcesDesc> &nodeDesc)
    {
        return mMirror->QueryCacheResourceImpl(nodeDesc);
    }

    inline CacheDescriptor Query(const uint64_t tenantId)
    {
        mLock.LockRead();
        auto it = mCacheMap.find(tenantId);
        if (LIKELY(it != mCacheMap.end())) {
            mLock.UnLock();
            return { it->second->mTenantId, it->second->mAffinity, it->second->mStrategy };
        }
        mLock.UnLock();
        return { UINT64_MAX, AFFINITY_BUTT, STRATEGY_BUTT };
    }

    inline BResult Insert(const std::shared_ptr<Bio> &instance)
    {
        mLock.LockWrite();
        if (UNLIKELY(mCacheMap.size() > DEFAULT_MAX_CACHE_SIZE)) {
            mLock.UnLock();
            return BIO_INNER_ERR;
        }
        auto it = mCacheMap.find(instance->mTenantId);
        if (UNLIKELY(it != mCacheMap.end())) {
            if (it->second->mAffinity != instance->mAffinity || it->second->mStrategy != instance->mStrategy) {
                mLock.UnLock();
                return BIO_INNER_ERR;
            }
            mLock.UnLock();
            return BIO_OK;
        }
        mCacheMap[instance->mTenantId] = instance;
        mLock.UnLock();
        return BIO_OK;
    }

    inline void Delete(const uint64_t tenantId)
    {
        mLock.LockWrite();
        auto it = mCacheMap.find(tenantId);
        if (UNLIKELY(it == mCacheMap.end())) {
            mLock.UnLock();
            return;
        }
        mCacheMap.erase(it);
        mLock.UnLock();
    }

    inline std::vector<CacheDescriptor> List()
    {
        mLock.LockRead();
        std::vector<CacheDescriptor> vec;
        for (auto &cache : mCacheMap) {
            vec.push_back({ cache.second->mTenantId, cache.second->mAffinity, cache.second->mStrategy });
        }
        mLock.UnLock();
        return vec;
    }

    inline MirrorClientPtr GetMirror() const
    {
        return mMirror;
    }

    inline net::BioClientNetPtr GetNetEngine() const
    {
        return mNetEngine;
    }

    inline void* LoadFunction(const char *name, void *handler)
    {
        void *ptr = nullptr;
        ptr = dlsym(handler, name);
        return ptr;
    }

    BResult BioClientLoggerInit(WorkerMode mode, LogType logType, std::string logFilePath);
    void BioClientLoggerExit(WorkerMode mode);
    BResult BioClientAgentInit(WorkerMode mode);
    void BioClientAgentExit();
    BResult BioClientNetPreInit(WorkerMode mode, const NetOptions netConf);
    BResult BioClientNetPostInit(const NetOptions netConf);
    void BioClientNetExit();
    BResult BioClientMirrorInit(WorkerMode mode);
    void BioClientMirrorExit();
    BResult BioInterceptorServerInit(WorkerMode mode);
    BResult BioClientStartWork();
    BResult BioClientStartPrometheus();
    void BioClientExitPrometheus();
    void BioClientUpdateHandle();
    void BioClientUpdateView();

    DEFINE_REF_COUNT_FUNCTIONS;
protected:
    BResult BioDiagnoseSdkInit();
    BResult BioClientDiagnoseInit(WorkerMode mode);
    BResult BioClientTracePointInit(WorkerMode mode);

private:
    WorkerMode mMode;
    bool mStarted = false;
    std::mutex mStartLock;
    std::unordered_map<uint64_t, std::shared_ptr<Bio>> mCacheMap;
    ReadWriteLock mLock;
    MirrorClientPtr mMirror = nullptr;
    net::BioClientNetPtr mNetEngine = nullptr;
    std::atomic<bool> mIsUpdating;
    ExecutorServicePtr mHeartService = nullptr;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif