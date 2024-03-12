/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BIO_CLIENT_NET_H
#define BIO_CLIENT_NET_H

#include "cm.h"
#include "net_engine.h"
#include "net_common.h"
#include "bio_ref.h"
#include "bio.h"

namespace ock {
namespace bio {
namespace net {
class BioClientNet;
using BioClientNetPtr = Ref<BioClientNet>;
class BioClientNet {
public:
    static BioClientNetPtr &Instance()
    {
        static auto instance = MakeRef<BioClientNet>();
        return instance;
    }

    // Establish an IPC connection with the local bio server
    BResult StartPre(WorkerMode mode);
    // Establish an RPC connection with the other bio server
    BResult StartPost(uint16_t localNid, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView, uint16_t protocol);
    void Stop();

    inline uint32_t GetDataPage() const
    {
        return mNetEngine->GetDataPage();
    }

    inline BResult Alloc(uint64_t size, NetMrInfo &mr)
    {
        if (UNLIKELY(mNetEngine->GetDataPage() < size)) {
            return BIO_ALLOC_FAIL;
        }
        uintptr_t address = 0;
        uint32_t key = UINT32_MAX;
        BResult ret = mNetEngine->AllocLocalMrSingle(address, key);
        mr = NetMrInfo(address, size, key);
        return ret;
    }

    inline void Free(uintptr_t address)
    {
        mNetEngine->FreeLocalMrSingle(address);
    }

    uint8_t* GetShmAddress(uint64_t offset)
    {
        return mNetEngine->GetShmAddress(offset);
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp &rsp)
    {
        return mNetEngine->SyncCall(target, opcode, req, rsp);
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp **rsp, uint64_t &respLen)
    {
        return mNetEngine->SyncCall(target, opcode, req, rsp, respLen);
    }

    template <typename TReq>
    inline void SendAsync(const BioNodeId target, uint16_t opcode, TReq &req, NetEngine::Callback &cb)
    {
        mNetEngine->AsyncCall(target, opcode, req, cb);
    }

    template <typename TReq>
    BResult SendAsync(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req)
    {
        return mNetEngine->AsyncCallWithoutResponse(targetNodeId, opCode, req);
    }

    inline void SendAsyncBuff(const BioNodeId target, uint16_t opcode, void *req, uint32_t reqLen,
        NetEngine::Callback &cb)
    {
        mNetEngine->AsyncCallBuff(target, opcode, req, reqLen, cb);
    }

    inline uint32_t GetLocalMrKey()
    {
        uint32_t key = 0;
        if (mMode == CONVERGENCE) {
            mNetEngine->GetLocalMrKey(key);
        } else {
            key = mShmKey;
        }
        return key;
    }

    BResult Rebuild(uint16_t localNid, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView);

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    BResult CheckShmFd();
    BResult CorrectFd();
    BResult ShmInitInner();
    BResult ShmInit();
    BResult StartIpcService();
    BResult StartRpcService(std::string ipMask, uint16_t port, ServiceProtocol protocol, uint16_t workerNum);
    BResult RecoverIpcService();
    BResult ListenEvent();
    void Recover();
    void StopInner();

private:
    WorkerMode mMode;
    NetEnginePtr mNetEngine = nullptr;
    int32_t mShmFd = -1;
    uint32_t mServerPid = 0;
    uint64_t mShmOffset = 0;
    uint64_t mShmLength = 0;
    uint32_t mShmKey = 0;
    uint8_t *mShmAddr = nullptr;
    ExecutorServicePtr mEventService = nullptr;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
}
#endif