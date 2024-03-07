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
#include "bio_execution.h"
#include "mirror_client.h"
#include "bio_client_net.h"

#ifdef USE_DEBUG_TOOLS
#include "cli.h"
#include "sdk_diagnose.h"
#include "htracer_diagnose.h"
#endif

namespace ock {
namespace bio {
class BioClient;
using BioClientPtr = Ref<BioClient>;
class BioClient {
public:
    static constexpr uint32_t defaultMaxCacheSize = 1024;

    static BioClientPtr &Instance()
    {
        static auto instance = MakeRef<BioClient>();
        return instance;
    }

    BResult Start(WorkerMode mode);
    void Stop();

    inline bool Ready() const
    {
        return mStarted;
    }

    inline BResult CalculateLocation(const uint64_t objectId, AffinityStrategy affinity, ObjLocation &location)
    {
        uint16_t ptId = mMirror->SelectingPt(objectId, affinity);
        if (UNLIKELY(ptId == UINT16_MAX)) {
            return BIO_CHECK_PT_FAIL;
        }
        location.location[0] = (static_cast<uint64_t>(ptId) & 0x000000000000FFFF);
        return BIO_OK;
    }

    inline BResult ParseLocation(ObjLocation &location)
    {
        return mMirror->ParseLocation(location);
    }

    inline BResult Put(MirrorClient::MirrorPut &param)
    {
        return mMirror->Put(param);
    }

    inline BResult Get(MirrorClient::MirrorGet &param, uint64_t &length)
    {
        return mMirror->Get(param, length);
    }

    inline BResult DeleteKey(const char *key, const ObjLocation &location)
    {
        return mMirror->DeleteKey(key, location);
    }

    inline BResult Load(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
        Bio::LoadCallback callback, void *context)
    {
        return mMirror->Load(key, offset, length, location, std::move(callback), context);
    }

    inline BResult ListAll(const char *prefix, std::unordered_map<std::string, ObjStat> &objs)
    {
        return mMirror->ListAll(prefix, objs);
    }

    inline BResult Stat(const char *key, const ObjLocation &location, ObjStat &stat)
    {
        return mMirror->StatObject(key, location, stat);
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
        if (UNLIKELY(mCacheMap.size() > defaultMaxCacheSize)) {
            mLock.UnLock();
            return BIO_ERR;
        }
        auto it = mCacheMap.find(instance->mTenantId);
        if (UNLIKELY(it != mCacheMap.end())) {
            if (it->second->mAffinity != instance->mAffinity ||
                it->second->mStrategy != instance->mStrategy) {
                mLock.UnLock();
                return BIO_ERR;
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

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult BioClientLoggerInit(WorkerMode mode);
    BResult BioClientAgentInit(WorkerMode mode);
    BResult BioClientNetPreInit(WorkerMode mode);
    BResult BioClientNetPostInit();
    BResult BioClientMirrorInit(WorkerMode mode);
    BResult BioClientStartWork();
    void Heartbeat();
    BResult BioClientRecover();

#ifdef USE_DEBUG_TOOLS
protected:
    BResult BioDiagnoseSdkInit();
    BResult BioDiagnoseHtracerInit();
    BResult BioClientDiagnoseInit(WorkerMode mode);
#endif

private:
    WorkerMode mMode;
    bool mStarted = false;
    std::mutex mStartLock;
    std::unordered_map<uint64_t, std::shared_ptr<Bio>> mCacheMap;
    ReadWriteLock mLock;
    MirrorClientPtr mMirror = nullptr;
    net::BioClientNetPtr mNetEngine = nullptr;
    bool mRunning = true;
    ExecutorServicePtr mHeartService = nullptr;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif