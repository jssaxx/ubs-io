/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

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
#include "bio_config_instance.h"
#include "mirror_client.h"

namespace ock {
namespace bio {
class BioClient;
using BioClientPtr = Ref<BioClient>;
class BioClient {
public:
    // A single client supports a maximum of 1024 cache instances
    static constexpr uint32_t defaultMaxCacheSize = 1024;

    static BioClientPtr &Instance()
    {
        static auto instance = MakeRef<BioClient>();
        return instance;
    }

    BResult Start();
    void Stop();

    inline bool Ready() const
    {
        return mStarted;
    }

    inline BResult CalculateLocation(const uint64_t objectId, AffinityStrategy affinity, Bio::ObjLocation &location)
    {
        uint16_t ptId = mMirror->SelectingPt(objectId, affinity);
        if (UNLIKELY(ptId == UINT16_MAX)) {
            return BIO_CHECK_PT_FAIL;
        }
        location.location[0] = (static_cast<uint64_t>(ptId) & 0x000000000000FFFF);
        return BIO_OK;
    }

    inline BResult ParseLocation(Bio::ObjLocation &location)
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

    inline BResult DeleteKey(const char *key, const Bio::ObjLocation &location)
    {
        return mMirror->DeleteKey(key, location);
    }

    inline BResult Load(const char *key, uint64_t offset, uint64_t length, const Bio::ObjLocation &location, Bio::LoadCallback callback, void *context)
    {
        return mMirror->Load(key, offset, length, location, std::move(callback), context);
    }

    inline BResult ListAll(const char *prefix, std::vector<std::pair<char *, Bio::ObjStat>>& objs)
    {
        // TODO::Not supported listAll operation
        return BIO_INNER_ERR;
    }

    inline Bio::ObjStat Stat(const char *key, const Bio::ObjLocation &location)
    {
        return mMirror->StatObject(key, location);
    }

    inline uint32_t GetDataPage() const
    {
        return mRpcService->GetDataPage();
    }

    inline BResult Alloc(uint64_t size, NetMrInfo &mr)
    {
        if (UNLIKELY(mRpcService->GetDataPage() < size)) {
            return BIO_ALLOC_FAIL;
        }
        uintptr_t address = 0;
        uint32_t key = UINT32_MAX;
        BResult ret = mRpcService->AllocLocalMrSingle(address, key);
        mr = NetMrInfo(address, size, key);
        return ret;
    }

    inline void Free(uintptr_t address)
    {
        mRpcService->FreeLocalMrSingle(address);
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp &rsp, bool plane)
    {
        return mRpcService->SyncCall(target, opcode, req, rsp, plane);
    }

    template <typename TReq>
    inline void SendAsync(const BioNodeId target, uint16_t opcode, TReq &req, NetEngine::Callback &cb, bool plane)
    {
        mRpcService->AsyncCall(target, opcode, req, cb, plane);
    }

    inline void SendAsyncBuff(const BioNodeId target, uint16_t opcode, void *req, uint32_t reqLen, NetEngine::Callback &cb, bool plane)
    {
        mRpcService->AsyncCallBuff(target, opcode, req, reqLen, cb, plane);
    }

    inline uint32_t GetLocalMrKey()
    {
        uint32_t key = 0;
        mRpcService->GetLocalMrKey(key);
        return key;
    }

    inline std::shared_ptr<Bio> Query(const uint64_t tenantId)
    {
        mLock.LockRead();
        auto it = mCacheMap.find(tenantId);
        if (LIKELY(it != mCacheMap.end())) {
            mLock.UnLock();
            return it->second;
        }
        mLock.UnLock();
        return nullptr;
    }

    inline BResult Insert(const std::shared_ptr<Bio> &instance)
    {
        mLock.LockWrite();
        if (UNLIKELY(mCacheMap.size() > defaultMaxCacheSize)) {
            mLock.UnLock();
            return BIO_ERR;
        }
        auto it = mCacheMap.find(instance->GetTenantId());
        if (UNLIKELY(it != mCacheMap.end())) {
            if (it->second->GetAffinityPolicy() != instance->GetAffinityPolicy() ||
                it->second->GetWriteStrategy() != instance->GetWriteStrategy()) {
                mLock.UnLock();
                return BIO_ERR;
            }
            mLock.UnLock();
            return BIO_OK;
        }
        mCacheMap[instance->GetTenantId()] = instance;
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

    inline std::unordered_map<uint64_t, std::shared_ptr<Bio>> List()
    {
        return mCacheMap;
    }

    DEFINE_REF_COUNT_FUNCTIONS

    inline MirrorClientPtr GetMirror()
    {
        return mMirror;
    }

private:
    BResult StartRpcService();

private:
    bool mStarted{ false };
    std::mutex mStartLock;
    std::unordered_map<uint64_t, std::shared_ptr<Bio>> mCacheMap;
    ReadWriteLock mLock;
    BioConfigPtr mConfig{ nullptr };
    NetEnginePtr mRpcService{ nullptr };
    MirrorClientPtr mMirror{ nullptr };
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif