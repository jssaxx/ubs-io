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

#ifndef BIO_CLIENT_NET_H
#define BIO_CLIENT_NET_H

#include <functional>
#include "cm.h"
#include "net_engine.h"
#include "net_common.h"
#include "bio_ref.h"
#include "bio_tracepoint_helper.h"
#include "bio.h"
#include "bio_config_instance.h"
#include "message.h"
#include "net_trans_engine.h"

namespace ock {
namespace bio {
using CheckNodeOnline = std::function<bool(uint16_t nodeId, std::string &ip, uint16_t &port)>;
using IpcRecoveredHandler = std::function<BResult()>;
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
    BResult StartPre(WorkerMode mode, NetOptions &netConf);

    // Establish an RPC connection with the other bio server
    BResult StartPost(uint16_t localNid, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView, uint16_t protocol,
        const NetOptions netConf);

    BResult GetUnderFsConfig(BioConfig::UnderFsConfig &config);

    void Exit();

    BResult ShmInit();

    bool CheckShmInitResp(ShmInitResponse rsp);

    void RegCheckNodeOnline(CheckNodeOnline checkOnLine)
    {
        mCheckOnLine = checkOnLine;
    }

    inline uint32_t GetNegoWorkScene() const
    {
        return mWorkScene;
    }

    void RegIpcRecoveredHandler(const IpcRecoveredHandler &handler)
    {
        mIpcRecoveredHandler = handler;
    }

    inline uint32_t GetNegoWorkIoAlignSize() const
    {
        return mWorkIoAlignSize;
    }

    inline uint32_t GetNegoWorkIoTimeOut() const
    {
        return mWorkIoTimeOut;
    }

    inline uint32_t GetNegoWorkNetTimeOut() const
    {
        return mWorkNetTimeOut;
    }

    inline int32_t GetNegoLogLevel() const
    {
        return mLogLevel;
    }

    inline std::string GetPrometheusListenAddress() const
    {
        return mPrometheusListenAddress;
    }

    inline uint32_t GetPrometheusScrapeIntervalSec() const
    {
        return mPrometheusScrapeIntervalSec;
    }

    inline bool GetPrometheusToggle() const
    {
        return mEnablePrometheus;
    }

    inline bool GetCrcFlag() const
    {
        return mEnableCrc;
    }

    inline bool GetHtraceFlag() const
    {
        return mEnableHtrace;
    }

    inline bool GetCliFlag() const
    {
        return mEnableCli;
    }

    inline NetEnginePtr GetNetEngine() const
    {
        return mNetEngine;
    }

    inline uint32_t GetDataPage() const
    {
        return mNetEngine->GetDataPage();
    }

    inline BResult RegisterMemoryRegion(uint8_t *addr, uint64_t size, MemoryRegion &mr)
    {
        return mNetEngine->RegisterMemoryRegion(addr, size, mr);
    }

    inline BResult RegisterMemoryRegion(uint64_t size, MemoryRegion &mr)
    {
        return mNetEngine->RegisterMemoryRegion(size, mr);
    }

    inline BResult Alloc(uint64_t size, NetMrInfo &mr)
    {
        if (UNLIKELY(mNetEngine->GetDataPage() < size)) {
            return BIO_ALLOC_FAIL;
        }
        uintptr_t address = 0;
        uint64_t key = UINT64_MAX;
        BResult ret = mNetEngine->AllocLocalMrSingle(address, key);
        mr = NetMrInfo(address, size, key);
        return ret;
    }

    inline void Free(uintptr_t address)
    {
        mNetEngine->FreeLocalMrSingle(address);
    }

    uint8_t *GetShmAddress(uint64_t offset, uint32_t len)
    {
        return mNetEngine->GetShmAddress(offset, len);
    }

    BResult ReceiveFds(const BioNodeId &targetNodeId, int32_t fds[], uint32_t count)
    {
        return mNetEngine->ReceiveFds(targetNodeId, fds, count);
    }

    template <typename TReq, typename TResp>
    inline BResult SendSyncBuff(const BioNodeId target, uint16_t opcode, void *req, uint32_t reqLen, TResp &rsp)
    {
        BResult ret = BIO_INNER_ERR;
        BIO_TP_START(SDK_BIO_MIRROR_SEND_SYNC_FAIL, &ret, BIO_INNER_RETRY);
        ret = mNetEngine->SyncCallBuff(target, opcode, req, reqLen, rsp);
        BIO_TP_END;
        return ret;
    }

    template <typename TResp>
    inline BResult SendSyncBuff(const BioNodeId target, uint16_t opcode, void *req, uint32_t reqLen,
                                TResp **rsp, uint64_t &respLen)
    {
        BResult ret = BIO_INNER_ERR;
        BIO_TP_START(SDK_BIO_MIRROR_SEND_SYNC_FAIL, &ret, BIO_INNER_RETRY);
        ret = mNetEngine->SyncCallBuff(target, opcode, req, reqLen, rsp, respLen);
        BIO_TP_END;
        return ret;
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp &rsp)
    {
        BResult ret = BIO_INNER_ERR;
        BIO_TP_START(SDK_BIO_MIRROR_SEND_SYNC_FAIL, &ret, BIO_INNER_RETRY);
        ret = mNetEngine->SyncCall(target, opcode, req, rsp);
        BIO_TP_END;
        return ret;
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp **rsp, uint64_t &respLen)
    {
        return mNetEngine->SyncCall(target, opcode, req, rsp, respLen);
    }

    template <typename TReq>
    inline void SendAsync(const BioNodeId target, uint16_t opcode, TReq &req, Callback &cb)
    {
        mNetEngine->AsyncCall(target, opcode, req, cb);
    }

    template <typename TReq> BResult SendAsync(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req)
    {
        return mNetEngine->AsyncCallWithoutResponse(targetNodeId, opCode, req);
    }

    inline void SendAsyncBuff(const BioNodeId target, uint16_t opcode, void *req, uint32_t reqLen,
        Callback &cb)
    {
        mNetEngine->AsyncCallBuff(target, opcode, req, reqLen, cb);
    }

    inline uint64_t GetLocalMrKey()
    {
        uint64_t key = 0;
        if (mMode == CONVERGENCE) {
            mNetEngine->GetLocalMrKey(key);
        } else {
            key = mShmKey;
        }
        return key;
    }

    BResult Rebuild(uint16_t localNid, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView);

    bool CheckGetUnderFsConfigResp(GetUnderFsConfigResponse &rsp);

    BResult RegisterMem(std::vector<void*>& addresses, std::vector<size_t>& sizes);

    DEFINE_REF_COUNT_FUNCTIONS

private:
    BResult CheckShmFd();
    BResult CorrectFd();
    BResult ShmInitInner();
    BResult StartIpcService(const NetOptions netConf);
    BResult StartRpcService(std::string ipMask, uint16_t port, ServiceProtocol protocol, uint16_t workerNum,
        const NetOptions netConf);
    BResult RecoverIpcService();
    BResult SetChannelBrokenHandler();
    void RecoverIpc();
    void RecoverRpc(uint32_t peerId);
    void StopInner();

private:
    WorkerMode mMode;
    uint32_t mWorkScene = 0;
    uint32_t mWorkIoAlignSize = 1;
    uint32_t mWorkIoTimeOut = 60;
    uint32_t mWorkNetTimeOut = 16;
    int32_t mLogLevel = 1;
    bool mEnableHtrace = { false };
    bool mEnableCrc = { false };
    bool mEnableCli = { false };
    NetEnginePtr mNetEngine = nullptr;
    NetTransEnginePtr mTransEngine = nullptr;
    int32_t mShmFd = -1;
    int32_t mServerPid = 0;
    uint32_t mNetSegmentSize = 256;
    uint64_t mShmOffset = 0;
    uint64_t mShmLength = 0;
    uint64_t mShmKey = 0;
    uint8_t *mShmAddr = nullptr;
    CheckNodeOnline mCheckOnLine = nullptr;
    IpcRecoveredHandler mIpcRecoveredHandler = nullptr;
    uint16_t mLocalNid;
    bool mEnablePrometheus = { false };
    std::string mPrometheusListenAddress = "127.0.0.1:7204";
    uint32_t mPrometheusScrapeIntervalSec = 15;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
}
#endif
