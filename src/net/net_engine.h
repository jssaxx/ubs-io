/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef NET_ENGINE_H
#define NET_ENGINE_H

#include <cstdint>
#include <arpa/inet.h>
#include "hcom/hcom.h"
#include "hcom/hcom_service.h"
#include "net_common.h"
#include "net_channel_mgr.h"
#include "net_connector.h"
#include "net_block_pool.h"
#include "net_executor_pool.h"

namespace ock {
namespace bio {
class NetEngine {
public:
    NetEngine() = default;
    ~NetEngine()
    {
        Stop();
    }

    BResult Start(const NetOptions &opt);
    void Stop();

    inline BResult RegisterMemoryRegion(uint64_t size, MemoryRegionPtr &mr)
    {
        ChkTrue(mRpcService != nullptr, BIO_ERR, "Net service not ready.");
        return mRpcService->RegisterMemoryRegion(size, mr);
    }

    inline void DestroyMemoryRegion(MemoryRegionPtr &mr)
    {
        ChkTrueEx(mRpcService != nullptr, "Net service not ready.");
        mRpcService->DestroyMemoryRegion(mr);
    }

    static inline NetMrInfo MemoryRegionInfo(MemoryRegionPtr &mr)
    {
        return NetMrInfo(mr->GetAddress(), mr->Size(), mr->GetLKey());
    }

    inline void SetDataPageKb(uint32_t dataPageKb)
    {
        mDataPageBytes = dataPageKb * KB_UNIT;
    }

    inline uint32_t GetDataPage() const
    {
        return mDataPageBytes;
    }

    inline BResult AllocLocalMrBatch(uint32_t count, std::vector<uintptr_t> &address, uint32_t &outKey)
    {
        ChkTrue(mMrBlockPool != nullptr, BIO_NOT_READY, "Net block pool not ready.");
        outKey = mLocalMr->GetLKey();
        return mMrBlockPool->AllocMany(count, address);
    }

    inline BResult AllocLocalMrSingle(uintptr_t &address, uint32_t &outKey)
    {
        ChkTrue(mMrBlockPool != nullptr, BIO_NOT_READY, "Net block pool not ready.");
        outKey = mLocalMr->GetLKey();
        return mMrBlockPool->AllocOne(address);
    }

    inline BResult GetLocalMrKey(uint32_t &outKey)
    {
        ChkTrue(mMrBlockPool != nullptr, BIO_NOT_READY, "Net block pool not ready.");
        outKey = mLocalMr->GetLKey();
        return BIO_OK;
    }

    inline void FreeLocalMrBatch(std::vector<uintptr_t> &address)
    {
        ChkTrueEx(mMrBlockPool != nullptr, "Net block pool not ready.");
        mMrBlockPool->ReleaseMany(address);
    }

    inline void FreeLocalMrSingle(uintptr_t address)
    {
        ChkTrueEx(mMrBlockPool != nullptr, "Net block pool not ready.");
        mMrBlockPool->ReleaseOne(address);
    }

    BResult SyncConnect(ConnectInfo &info)
    {
        ChkTrue(mConnector != nullptr, BIO_NOT_INITIALIZED, "Net Connector net ready.");
        return mConnector->SyncConnect(info);
    }

    BResult AsyncConnect(ConnectInfo &info, AsyncConnHandler handler, uintptr_t ctx)
    {
        ChkTrue(mConnector != nullptr, BIO_NOT_INITIALIZED, "Net Connector net ready.");
        return mConnector->AsyncConnect(info, handler, ctx);
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, TResp &resp, bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }

        return SyncCall(opCode, req, resp, ch, isCtrlPanel);
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, TResp **resp, uint64_t &respLen,
        bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }

        return SyncCall(opCode, req, resp, respLen, ch, isCtrlPanel);
    }

    template <typename TReq>
    BResult AsyncCallWithoutResponse(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }

        return AsyncCallWithoutResponse(opCode, req, ch, isCtrlPanel);
    }

    using CbFunc = std::function<void(void *ctx, void *resp, uint32_t len, int32_t result)>;
    struct Callback {
        CbFunc cb;
        void *cbCtx;
        Callback() : cb([](void *ctx, void *resp, uint32_t len, int32_t result) {}), cbCtx(nullptr) {}
        Callback(CbFunc func, void *ctx) : cb(std::move(func)), cbCtx(ctx) {}
    };
    template <typename TReq>
    void AsyncCall(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, Callback callback, bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_INVALID_PARAM);
            return;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_NET_RETRY);
            return;
        }

        AsyncCall(opCode, req, ch, callback, isCtrlPanel);
    }

    void AsyncCallBuff(const BioNodeId &targetNodeId, uint16_t opCode, void *req, uint32_t reqLen, Callback callback,
        bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_INVALID_PARAM);
            return;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_NET_RETRY);
            return;
        }

        AsyncCallBuffInner(opCode, req, reqLen, ch, callback, isCtrlPanel);
    }

    BResult SyncRead(const BioNodeId &targetNodeId, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, false, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }
        return ch->Read(req, nullptr);
    }

    BResult SyncWrite(const BioNodeId &targetNodeId, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, false, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }
        return ch->Write(req, nullptr);
    }

    BResult RegisterNewRequestHandler(uint32_t opCode, const NewRequestHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        if (UNLIKELY(mHandlers[opCode] != nullptr)) {
            LOG_ERROR("Handler for opCode " << opCode << " already registered");
            return BIO_ALREADY_DONE;
        }

        mHandlers[opCode] = h;
        return BIO_OK;
    }

    BResult RegisterNewChannelHandler(const NewChannelHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(mHandleNewChannel != nullptr)) {
            LOG_ERROR("Failed to register new channel handler");
            return BIO_ERR;
        }

        mHandleNewChannel = h;
        return BIO_OK;
    }

    BResult RegisterChannelBrokenHandler(const ChannelBrokenHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(mHandlerBroken != nullptr)) {
            LOG_ERROR("Failed to register channel broken handler");
            return BIO_ERR;
        }

        mHandlerBroken = h;
        return BIO_OK;
    }

    inline bool IsStarted() const
    {
        return mStarted;
    }

    inline void SetLocalNodeId(const uint32_t &nodeId)
    {
        mLocalNodeId = nodeId;
    }

    NetChannelMgrPtr &GetPeerCtrlChannel()
    {
        return mCtrlChannels;
    };

    uint16_t GeCtrlChannelNum()
    {
        return mOptions.controlPanelConnCount;
    };

    NetChannelMgrPtr &GetPeerDataChannel()
    {
        return mDataChannels;
    }

    uint16_t GeDataChannelNum()
    {
        return mOptions.dataPanelConnCount;
    };

    BResult ConnectToPeer(ConnectMode mode, ConnectInfo &info, bool isCtrlPanel, ChannelPtr &ch);

    DEFINE_REF_COUNT_FUNCTIONS

private:
    BResult CreateSocketPath(std::string &sockPath);
    void AssignIpcServiceOptions(bool isOobSvr, ock::hcom::NetServiceOptions &options);
    BResult CreateIpcService();
    BResult AssignRpcServiceOptions(bool isOobSvr, ock::hcom::NetServiceOptions &options);
    BResult CreateRpcService();
    BResult CreateNetService();

    int32_t NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload);
    void ChannelBroken(const ChannelPtr &ch);
    int32_t RequestReceived(ServiceContext &ctx);
    int RequestPosted(const ServiceContext &ctx);
    int OneSideDone(const ServiceContext &ctx);

private:
    std::string GenerateWorkersSetting();

    BResult InitializeBase();
    void StopInner();

    BResult InitLocalMrAllocator();

    static inline BResult NetResult(hcom::SerResult ret)
    {
        using namespace ock::hcom;
        switch (ret) {
            case SER_NEW_OBJECT_FAILED:
            case SER_NOT_ESTABLISHED:
            case SER_TIMEOUT:
                return BIO_NET_RETRY;
            default:
                return BIO_NET_ERROR;
        }
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(uint16_t opCode, TReq &req, TResp &resp, ChannelPtr &ch, bool isCtrlPanel)
    {
        using namespace ock::hcom;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = isCtrlPanel ? mCtrlPanelTimeout : mDataPanelTimeout;
        NetServiceOpInfo rspOpInfo{};
        NetServiceMessage respMsg(&resp, sizeof(TResp));
        auto result = ch->SyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, rspOpInfo, respMsg);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed to call peer resp with op " << opCode << ", result " << NetErrStr(result));
            return NetResult(result);
        }

        if (NN_UNLIKELY(rspOpInfo.errorCode != BIO_OK)) {
            LOG_ERROR("Failed to call peer resp with op " << opCode << ", error code " << rspOpInfo.errorCode);
            return rspOpInfo.errorCode;
        }

        return BIO_OK;
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(uint16_t opCode, TReq &req, TResp **resp, uint64_t &respLen, ChannelPtr &ch, bool isCtrlPanel)
    {
        using namespace ock::hcom;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = isCtrlPanel ? mCtrlPanelTimeout : mDataPanelTimeout;
        NetServiceOpInfo rspOpInfo{};
        NetServiceMessage respMsg{};
        auto result = ch->SyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, rspOpInfo, respMsg);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed to call peer unfixed-length resp with op " << opCode << ", result " << NetErrStr(result));
            return NetResult(result);
        }

        if (NN_UNLIKELY(rspOpInfo.errorCode != BIO_OK)) {
            LOG_ERROR("Failed to call peer unfixed-length resp with op " << opCode << ", error code " <<
                rspOpInfo.errorCode);
            return rspOpInfo.errorCode;
        }

        *resp = reinterpret_cast<TResp *>(respMsg.data);
        respLen = respMsg.size;
        return BIO_OK;
    }

    template <typename TReq>
    BResult AsyncCallWithoutResponse(uint16_t opCode, TReq &req, ChannelPtr &ch, bool isCtrlPanel)
    {
        using namespace ock::hcom;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = isCtrlPanel ? mCtrlPanelTimeout : mDataPanelTimeout;
        auto *netCallback = NewCallback([](NetServiceContext &context) { return; }, std::placeholders::_1);
        auto result = ch->AsyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, netCallback);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed async call with op " << opCode << ", result " << NetErrStr(result));
            return NetResult(result);
        }
        return BIO_OK;
    }

    template <typename TReq>
    void AsyncCall(uint16_t opCode, TReq &req, ChannelPtr &ch, Callback callback, bool isCtrlPanel)
    {
        using namespace ock::hcom;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = isCtrlPanel ? mCtrlPanelTimeout : mDataPanelTimeout;
        auto *netCallback = NewCallback(
            [callback](NetServiceContext &context) {
                callback.cb(callback.cbCtx, context.MessageData(), context.MessageDataLen(), context.Result());
            },
            std::placeholders::_1);
        auto result = ch->AsyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, netCallback);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed async call with op " << opCode << ", result " << NetErrStr(result));
            callback.cb(callback.cbCtx, nullptr, 0, NetResult(result));
        }
    }

    void AsyncCallBuffInner(uint16_t opCode, void *req, uint32_t reqLen, ChannelPtr &ch, Callback callback,
        bool isCtrlPanel)
    {
        using namespace ock::hcom;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = isCtrlPanel ? mCtrlPanelTimeout : mDataPanelTimeout;
        auto *netCallback = NewCallback(
            [callback](NetServiceContext &context) {
                callback.cb(callback.cbCtx, context.MessageData(), context.MessageDataLen(), context.Result());
            },
            std::placeholders::_1);
        auto result = ch->AsyncCall(reqOpInfo, { req, reqLen }, netCallback);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed async call with op " << opCode << ", result " << NetErrStr(result));
            callback.cb(callback.cbCtx, nullptr, 0, NetResult(result));
        }
    }

    inline BResult GetChanel(const BioNodeId &targetNodeId, bool isCtrlPanel, ChannelPtr &ch)
    {
        if (UNLIKELY(isCtrlPanel)) {
            return mCtrlChannels->GetChannel(targetNodeId, ch);
        } else {
            return mDataChannels->GetChannel(targetNodeId, ch);
        }
    }

private:
    static constexpr uint32_t MAX_NEW_REQ_HANDLER = 256L;

private:
    bool mStarted = false;
    int16_t mCtrlPanelTimeout{ -1 };
    int16_t mDataPanelTimeout{ -1 };
    uint32_t mDataPageBytes{ NO_128 * NO_1024 };
    NetChannelMgrPtr mCtrlChannels{ nullptr };
    NetChannelMgrPtr mDataChannels{ nullptr };

    MemoryRegionPtr mLocalMr{ nullptr };
    NetBlockPoolPtr mMrBlockPool{ nullptr };
    NewRequestHandler mHandlers[MAX_NEW_REQ_HANDLER]{};
    NetConnectorPtr mConnector{ nullptr };

    DEFINE_REF_COUNT_VARIABLE

    uint32_t mLocalNodeId{ UINT32_MAX };
    NewChannelHandler mHandleNewChannel{ nullptr }; /* callback to upper layer */
    ChannelBrokenHandler mHandlerBroken{ nullptr }; /* callback to upper layer */

    ock::hcom::NetService *mRpcService = nullptr;
    std::string socketPath;
    ock::hcom::NetService *mIpcService = nullptr;
    std::mutex mMutex;

    NetOptions mOptions;
    std::string mName;
    NetExecutorPoolPtr mRequestExecutor{ nullptr };
    friend class NetConnectTask;
};

using NetEnginePtr = Ref<NetEngine>;
}
}
#endif // NET_ENGINE_H
