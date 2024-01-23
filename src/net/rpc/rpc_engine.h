/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef RPC_ENGINE_H
#define RPC_ENGINE_H

#include <arpa/inet.h>
#include "net_common.h"
#include "rpc_engine_channel_mgt.h"
#include "rpc_engine_connector.h"
#include "net_block_pool.h"
#include "hcom/hcom.h"
#include "hcom/hcom_service.h"
#include "net_executor_pool.h"
#include <cstdint>

namespace ock {
namespace bio {
class RpcEngine {
public:
    RpcEngine() = default;
    ~RpcEngine()
    {
        Stop();
    }

public:
    BResult Start(const BioNetOptions &opt);
    void Stop();

    /*
     * @brief Register a memory region to NIC, mainly used in RDMA case
     *
     * @param size         [in] size of the memory region to be registered to the NIC
     * @param mr           [out] memory region info
     *
     * @return 0 if successfully registered
     */
    inline BResult RegisterMemoryRegion(uint64_t size, MemoryRegionPtr &mr)
    {
        using namespace ock::hcom;
        ChkTrueNot(mService != nullptr, BIO_ERR);
        auto ret = mService->RegisterMemoryRegion(size, mr);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to register mr by result " << NetErrStr(ret));
            return ret;
        }

        return BIO_OK;
    }

    /* *
     * @brief Destroy the memory region
     *
     * @param mr           [in] the memory region to be destroyed
     */
    inline void DestroyMemoryRegion(MemoryRegionPtr &mr)
    {
        using namespace ock::hcom;
        ChkTrueExNot(mService != nullptr);
        mService->DestroyMemoryRegion(mr);
    }

    /*
     * @brief Translate the mr info
     *
     * @param mr           [in] the memory region to be translated
     * @return
     */
    static inline BioMrInfo MemoryRegionInfo(MemoryRegionPtr &mr)
    {
        BioMrInfo info(mr->GetAddress(), mr->Size(), mr->GetLKey());
        return info;
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
        ChkTrueNot(mMrBlockPool != nullptr, BIO_NOT_READY);

        outKey = mLocalMr->GetLKey();
        return mMrBlockPool->AllocMany(count, address);
    }

    inline BResult AllocLocalMrSingle(uintptr_t &address, uint32_t &outKey)
    {
        ChkTrueNot(mMrBlockPool != nullptr, BIO_NOT_READY);
        outKey = mLocalMr->GetLKey();
        return mMrBlockPool->AllocOne(address);
    }

    inline BResult GetLocalMrKey(uint32_t &outKey)
    {
        ChkTrueNot(mMrBlockPool != nullptr, BIO_NOT_READY);
        outKey = mLocalMr->GetLKey();
        return BIO_OK;
    }

    inline void FreeLocalMrBatch(std::vector<uintptr_t> &address)
    {
        ChkTrueExNot(mMrBlockPool != nullptr);
        mMrBlockPool->ReleaseMany(address);
    }

    inline void FreeLocalMrSingle(uintptr_t address)
    {
        ChkTrueExNot(mMrBlockPool != nullptr);
        mMrBlockPool->ReleaseOne(address);
    }

    /*
     * @brief Connect to peer in sync way
     *
     * @param info         [in] connect information include ip/port/node id
     *
     * @return 0 if successfully connected
     */
    BResult SyncConnect(ConnectInfo &info)
    {
        ChkTrueNot(mAsyncConnector.Get() != nullptr, BIO_NOT_INITIALIZED);
        return mAsyncConnector->SyncConnect(info);
    }

    /*
     * @brief Issue a connecting request, the connecting action is done by a background function,
     * after the connecting action is done (successfully or timeout), the callback is will be called
     *
     * @param info         [in] connect information include ip/port/node id
     * @param handler      [in] callback handler called after connecting action is done
     * @param ctx          [in] user context for callback handler, the context will be passed in callback handler
     *
     * @return 0 if successfully issued the connection action
     */
    BResult AsyncConnect(ConnectInfo &info, AsyncConnHandler handler, uintptr_t ctx)
    {
        ChkTrueNot(mAsyncConnector.Get() != nullptr, BIO_NOT_INITIALIZED);
        return mAsyncConnector->AsyncConnect(info, handler, ctx);
    }

    /* *
     * @brief Call to peer in sync mode for fixed-length resp
     *
     * @tparam TReq        [in] type of request
     * @tparam TResp       [in] type of response
     * @param targetNodeId [in] target peer node id
     * @param opCode       [in] opCode
     * @param req          [in] request data
     * @param resp         [in/out] response data
     * @param isCtrlPanel  [in] control panel
     *
     * @return 0 if successful
     */
    template <typename TReq, typename TResp>
    BResult SyncCall(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, TResp &resp, bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch { nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Failed to get channel by vnode id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY; // need retry depend on node state
        }

        auto result = SyncCall(opCode, req, resp, ch, isCtrlPanel);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed to call peer with op " << opCode << ", result " << result);
        }

        return result;
    }

    /* *
     * @brief Call to peer in sync mode for unfixed-length resp
     *
     * @tparam TReq        [in] type of request
     * @tparam TResp       [in] type of response
     * @param targetNodeId [in] target peer node id
     * @param opCode       [in] opCode
     * @param req          [in] request data
     * @param resp         [in/out] response data ptr
     * @param isCtrlPanel  [in] control panel.
     *
     * @return 0 if successful
     */
    template <typename TReq, typename TResp>
    BResult SyncCall(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, TResp **resp, uint64_t &respLen,
        bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch { nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Failed to call unfixed-length resp get channel by vnode id " << targetNodeId <<
                ", result " << ret);
            return BIO_ERR;
        }

        auto result = SyncCall(opCode, req, resp, respLen, ch, isCtrlPanel);
        if (result == BIO_NET_RETRY) {
            return BIO_ERR;
        }
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed to call peer unfixed-length resp with op " << opCode << ", result " << result);
        }

        return result;
    }

    template <typename TReq>
    BResult AsyncCallWithoutResponse(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch { nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Failed to call unfixed-length resp get channel by vnode id " << targetNodeId <<
                ", result " << ret);
            return BIO_NET_RETRY; // need retry depend on node state
        }

        auto result = AsyncCallWithoutResponse(opCode, req, ch, isCtrlPanel);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed to call peer unfixed-length resp with op " << opCode << ", result " << result);
        }

        return result;
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

        ChannelPtr ch { nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK || ch.Get() == nullptr)) {
            LOG_ERROR("Failed to get channel by node id " << targetNodeId << ", result " << ret);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_NET_RETRY);
            return;
        }

        AsyncCall(opCode, req, ch, callback, isCtrlPanel);
   }

    void AsyncCallBuff(const BioNodeId &targetNodeId, uint16_t opCode, void *req, uint32_t reqLen, Callback callback, bool isCtrlPanel)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_INVALID_PARAM);
            return;
        }

        ChannelPtr ch { nullptr };
        auto ret = GetChanel(targetNodeId, isCtrlPanel, ch);
        if (UNLIKELY(ret != BIO_OK || ch.Get() == nullptr)) {
            LOG_ERROR("Failed to get channel by node id " << targetNodeId << ", result " << ret);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_NET_RETRY);
            return;
        }

        AsyncCallBuffInner(opCode, req, reqLen, ch, callback, isCtrlPanel);
    }

    /*
     * @brief Read data from peer using one-sided call
     *
     * @param targetNodeId [in] target vnode id
     * @param req          [in] request info include peer address/key/size
     *
     * @return
     */
    BResult SyncRead(const BioNodeId &targetNodeId, const BioNetRequest &req)
    {
        ChannelPtr ch { nullptr };
        auto ret = GetChanel(targetNodeId, false, ch);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Failed to get channel for read by vnode id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY; // need retry depend on node state
        }

        using namespace hcom;
        ChkTrueNot(ch.Get() != nullptr, BIO_NOT_EXISTS);

        // NetServiceRequest innerRequest(req.lAddress, req.rAddress, req.lKey, req.rKey, req.size);
        auto result = ch->Read(req, nullptr);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed to read data from peer vnode " << targetNodeId << ", result " <<
                NetErrStr(result));
            return result;
        }

        return BIO_OK;
    }

    /* *
     * @brief Read multiple non-consecutive data buffer of peer to consecutive data buffer,
     * need to take good care of the size of the data buffers, i.e. the size of target
     * data buffer must bigger enough
     *
     * @param targetNodeId [in] target peer node id
     * @param req          [in] request array
     *
     * @return 0 if successfully read
     */
    BResult SyncReadSgl(const BioNodeId &targetNodeId, std::vector<BioNetRequest> &reqs)
    {
        /* NOTE: note need to check the param here as hcom check it already */

        ChannelPtr ch = nullptr;
        auto ret = GetChanel(targetNodeId, false, ch);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("get channel fail, vNodeId=" << targetNodeId << ", whole="
                << targetNodeId << ", ret" << ret);
            return BIO_NET_RETRY; // need retry depend on node state
        }

        using namespace hcom;
        ChkTrueNot(ch.Get() != nullptr, BIO_NOT_EXISTS);
        ChkTrue(reqs.size() <= NET_SGE_MAX_IOV, BIO_INVALID_PARAM,"Failed to syncReadSgl, reqs.size():"
            << reqs.size());

        NetServiceRequest request[reqs.size()];
        for (uint32_t i = 0; i < reqs.size(); i++) {
            request[i].lAddress = reqs[i].lAddress;
            request[i].rAddress = reqs[i].rAddress;
            request[i].lKey = reqs[i].lKey;
            request[i].rKey = reqs[i].rKey;
            request[i].size = reqs[i].size;
        }

        NetServiceSglRequest innerRequest(request, reqs.size());
        auto result = ch->ReadSgl(innerRequest, nullptr);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed to read sgl peer, result " << NetErrStr(result));
            return result;
        }

        return BIO_OK;
    }

    /*
     * @brief Write data from peer using one-sided call
     *
     * @param targetNodeId [in] target vnode id
     * @param req          [in] request info include peer address/key/size
     *
     * @return
     */
    BResult SyncWrite(const BioNodeId &targetNodeId, const BioNetRequest &req)
    {
        ChannelPtr ch { nullptr };
        auto ret = GetChanel(targetNodeId, false, ch);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Failed to get channel for read by vnode id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY; // need retry depend on node state
        }

        using namespace hcom;
        ChkTrueNot(ch.Get() != nullptr, BIO_NOT_EXISTS);

        // NetServiceRequest innerRequest(req.lAddress, req.rAddress, req.lKey, req.rKey, req.size);
        auto result = ch->Write(req, nullptr);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Failed to write data from peer vnode " << targetNodeId << ", result " <<
                                                              NetErrStr(result));
            return result;
        }

        return BIO_OK;
    }

    inline bool ChannelExist(const BioNodeId &targetNodeId)
    {
        ChannelPtr ch;
        auto ret = mPeerCtrlChannels->GetChannel(targetNodeId, ch);
        ret |= mPeerDataChannels->GetChannel(targetNodeId, ch);
        return ret == BIO_OK;
    }
    /* *
     * @brief Register a request handler of upper layer
     *
     * @param opCode       [in] opCode
     * @param h            [in] handler
     *
     * @return 0 if successfully, i.e. not registered and in the bound
     */
    BResult RegisterNewRequestHandler(uint32_t opCode, const NewRequestHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (opCode >= MAX_NEW_REQ_HANDLER) {
            LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        if (mHandlers[opCode] != nullptr) {
            LOG_ERROR("Handler for opCode " << opCode << " already registered");
            return BIO_ALREADY_DONE;
        }

        mHandlers[opCode] = h;

        return BIO_OK;
    }

    BResult RegisterNewChannelHandler(const NewChannelHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (mHandleNewChannel != nullptr) {
            LOG_ERROR("Failed to register new channel handler");
            return BIO_ERR;
        }

        mHandleNewChannel = h;

        return BIO_OK;
    }

    BResult RegisterChannelBrokenHandler(const ChannelBrokenHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (mHandlerBroken != nullptr) {
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

    inline void SetMyNodeId(const BioNodeId &nodeId)
    {
        mMyNodeId = nodeId;
    }

    DEFINE_REF_COUNT_FUNCTIONS

private:
    /* hcom handle functions */
    int32_t CreateService();
    int32_t NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload);
    void ChannelBroken(const ChannelPtr &ch);
    int32_t RequestReceived(ServiceContext &ctx);
    int RequestPosted(const ServiceContext &ctx);
    int OneSideDone(const ServiceContext &ctx);

private:
    BResult ValidateOptions();
    std::string GenerateWorkersSetting();
    BResult ConnectToPeer(ConnectInfo &info, bool isCtrlPanel, ChannelPtr &ch);

    BResult Initialize();
    void StopInner();

    BResult InitLocalMrAllocator();

    static inline BResult NetResult(hcom::SerResult ret)
    {
        using namespace hcom;
        switch (ret) {
            case SER_NEW_OBJECT_FAILED:
            case SER_NOT_ESTABLISHED:
            case SER_TIMEOUT:
                return BIO_NET_RETRY;
            default:
                return BIO_NET_ERROR;
        }
    }
    /* *
     * @brief Call to peer in sync mode for fixed-length resp
     *
     * @tparam TReq        [in] type of request
     * @tparam TResp       [in] type of response
     * @param opCode       [in] opCode
     * @param req          [in] request data
     * @param resp         [in/out] response data
     * @param ch           [in] target channel
     *
     * @return 0 if successful
     */
    template <typename TReq, typename TResp>
    BResult SyncCall(uint16_t opCode, TReq &req, TResp &resp, ChannelPtr &ch, bool isCtrlPanel)
    {
        using namespace hcom;
        ChkTrueNot(ch.Get() != nullptr, BIO_ERR);

        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = isCtrlPanel ? mCtrlPanelTimeout : mDataPanelTimeout;
        NetServiceOpInfo rspOpInfo {};

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

    /* *
     * @brief Call to peer in sync mode for unfixed-length resp
     *
     * @tparam TReq        [in] type of request
     * @tparam TResp       [in] type of response
     * @param opCode       [in] opCode
     * @param req          [in] request data
     * @param resp         [in/out] response data ptr
     * @param ch           [in] target channel
     *
     * @return 0 if successful
     */
    template <typename TReq, typename TResp>
    BResult SyncCall(uint16_t opCode, TReq &req, TResp **resp, uint64_t &respLen, ChannelPtr &ch, bool isCtrlPanel)
    {
        using namespace hcom;
        ChkTrueNot(ch.Get() != nullptr, BIO_ERR);

        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = isCtrlPanel ? mCtrlPanelTimeout : mDataPanelTimeout;
        NetServiceOpInfo rspOpInfo {};

        NetServiceMessage respMsg {};
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
        using namespace hcom;
        ChkTrueNot(ch.Get() != nullptr, BIO_ERR);

        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = isCtrlPanel ? mCtrlPanelTimeout : mDataPanelTimeout;
        auto *netCallback = NewCallback(
            [](NetServiceContext &context) {
                // do nothing now
            },
            std::placeholders::_1);
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
        using namespace hcom;
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

    void AsyncCallBuffInner(uint16_t opCode, void *req, uint32_t reqLen, ChannelPtr &ch, Callback callback, bool isCtrlPanel)
    {
        using namespace hcom;
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
        if (isCtrlPanel) {
            return mPeerCtrlChannels->GetChannel(targetNodeId, ch);
        } else {
            return mPeerDataChannels->GetChannel(targetNodeId, ch);
        }
    }

private:
    static constexpr uint32_t MAX_NEW_REQ_HANDLER = 256L;

private:
    /* hot used variables, try to keep them less than 64bytes */
    bool mStarted = false;                             /* started or not */
    int16_t mCtrlPanelTimeout = -1;                    /* timeout in seconds for ctrl panel */
    int16_t mDataPanelTimeout = -1;                    /* timeout in seconds for data panel */
    uint32_t mDataPageBytes = 128 * 1024;              /* fs data block size */
    RpcChannelMgrPtr mPeerCtrlChannels { nullptr }; /* peer channel for ctrl panel */
    RpcChannelMgrPtr mPeerDataChannels { nullptr }; /* peer channel for data panel */
    MemoryRegionPtr mLocalMr { nullptr };
    BioNetBlockPoolPtr mMrBlockPool { nullptr };
    NewRequestHandler mHandlers[MAX_NEW_REQ_HANDLER] {}; /* request handler */

    /* not-hot used variables */
    RpcConnectorPtr mAsyncConnector { nullptr };
    RpcChannelMgrPtr mAcceptedCtrlChannels { nullptr }; /* channels accepted from peers of ctrl panel */
    RpcChannelMgrPtr mAcceptedDataChannels { nullptr }; /* channels accepted from peers of data panel */

    DEFINE_REF_COUNT_VARIABLE

    BioNodeId mMyNodeId {};
    NewChannelHandler mHandleNewChannel { nullptr }; /* callback to upper layer */
    ChannelBrokenHandler mHandlerBroken { nullptr }; /* callback to upper layer */
    hcom::NetService *mService = nullptr;
    std::mutex mMutex;

    BioNetOptions mOptions;
    std::string mName;

    NetExecutorPoolPtr mRequestExecutor { nullptr };
    friend class RpcConnectTask;
};
using RpcEnginePtr = Ref<RpcEngine>;
}
}

#endif // RPC_ENGINE_H
