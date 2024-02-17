/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BIO_SERVER_H
#define BIO_SERVER_H

#include <mutex>
#include "bio_err.h"
#include "bio_ref.h"
#include "bio_config_instance.h"
#include "net_engine.h"
#include "cm.h"
#include "mirror_server.h"

namespace ock {
namespace bio {
class BioServer;
using BioServerPtr = Ref<BioServer>;
class BioServer {
public:
    BResult Start();
    void Stop();

    static BioServerPtr &Instance()
    {
        static auto instance = MakeRef<BioServer>();
        return instance;
    }

    inline NetEnginePtr GetRpcEngine()
    {
        return mNetEngine;
    }

    inline CmPtr GetCm()
    {
        return mCm;
    }

    inline CmNodeId GetLocalNid()
    {
        return mLocalNid;
    }

    inline std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> GetNodeView()
    {
        return mNodeView;
    }

    inline std::map<uint16_t, CmPtInfo> GetPtView()
    {
        return mPtView;
    }

    inline CmPtInfo GetPtEntry(uint64_t ptId)
    {
        return mPtView[ptId];
    }

    inline uint32_t GetLocalMrKey()
    {
        uint32_t key = 0;
        mNetEngine->GetLocalMrKey(key);
        return key;
    }

    inline BResult MemAlloc(uint64_t size, NetMrInfo &mr)
    {
        auto ret = mNetEngine->AllocLocalMrSingle(mr.address, mr.key);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
        mr.size = size;
        return BIO_OK;
    }

    inline BResult MemAlloc(uint64_t size, uint64_t *addr)
    {
        uintptr_t address;
        uint32_t outKey;
        auto ret = mNetEngine->AllocLocalMrSingle(address, outKey);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
        *addr = address;
        return BIO_OK;
    }

    inline void MemFree(uint64_t addr)
    {
        mNetEngine->FreeLocalMrSingle(addr);
    }

    DEFINE_REF_COUNT_FUNCTIONS;

protected:
    BResult LoadConfig();

    BResult StartDisk();
    void StopDisk();

    BResult StartNetService();
    void StopNetService();

    BResult StartCm();
    void StopCm();
    BResult HandleCmNodeEvent(const std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeInfos);
    BResult HandleCmPtEvent(const std::map<uint16_t, CmPtInfo> &ptInfos);

    BResult StartMirrorServer();
    void StopMirrorServer();

    void Connection();

private:
    bool mStarted = false;
    std::mutex mStartLock;
    BioConfigPtr mConfig = nullptr;
    NetEnginePtr mNetEngine = nullptr;
    CmPtr mCm = nullptr;
    MirrorServerPtr mMirror = nullptr;
    CmNodeId mLocalNid;
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> mNodeView;
    std::map<uint16_t, CmPtInfo> mPtView;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}

#endif // BIO_SERVER_H