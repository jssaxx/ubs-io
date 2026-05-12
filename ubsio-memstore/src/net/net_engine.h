/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef NET_ENGINE_H
#define NET_ENGINE_H

#include <cstdint>
#include <arpa/inet.h>
#include <unordered_map>
#include "hcom/hcom.h"
#include "hcom/hcom_service.h"
#include "mms_monotonic.h"
#include "mms_message.h"
#include "mms_tls_util.h"
#include "net_common.h"
#include "net_channel_mgr.h"
#include "net_connector.h"
#include "net_executor_pool.h"

namespace ock {
namespace mms {
constexpr size_t KEYPASS_MAX_LEN = 10000;

using PrivateKeyCallback =
    std::function<bool(const std::string &, std::string &, void *&, int &, ock::hcom::UBSHcomTLSEraseKeypass &)>;

class NetEngine;
using NetEnginePtr = Ref<NetEngine>;
class NetEngine {
public:
    NetEngine() = default;
    ~NetEngine() = default;

    BResult Initialize(int16_t timeoutSec, uint32_t coreThreadNum, uint32_t queueSize, NetLogFunc func,
        NetMemList &memList);
    BResult Start(const NetOptions &opt);
    void Stop();

    void Show(uint32_t &executorNum, NetOptions &rpcOption, NetOptions &ipcOption)
    {
        executorNum = mReqExecutorNum;
        rpcOption = mRpcOptions;
        ipcOption = mIpcOptions;
    }

    void ResetLogLevel(int32_t level)
    {
        NetLog::Instance()->SetMinLogLevel(level);
        if (level > 0) {
            // hcom日志等级比mms小1
            ock::hcom::UBSHcomNetOutLogger::Instance()->SetLogLevel(level - NO_1);
            return;
        }
        ock::hcom::UBSHcomNetOutLogger::Instance()->SetLogLevel(level);
    }

    void UpdateTimeOut(int16_t timeoutSec)
    {
        mTimeout = timeoutSec;
    }

    BResult UpdateChannelTimeOut(const MmsNodeId &targetNodeId)
    {
        uint16_t index;
        for (index = 0; index < mIpcOptions.workerGroupsNum; index++) {
            ChannelPtr ch{ nullptr };
            auto ret = GetChanel(targetNodeId, ch, index);
            if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
                NET_LOG_WARN("Failed to get channel by target node id " << targetNodeId << ", result " << ret <<
                    ", groupIndex:" << index);
                return MMS_ERR;
            }
            ch->SetChannelTimeOut(mTimeout, mTimeout);
        }
        return MMS_OK;
    }

    BResult CheckConnect(const MmsNodeId targetNodeId)
    {
        ChannelPtr ch;
        auto ret = GetChanel(targetNodeId, ch, 0);
        if (UNLIKELY(ret != MMS_OK)) {
            return MMS_NET_RETRY;
        }
        return MMS_OK;
    }

    BResult SyncConnect(ConnectInfo &info)
    {
        if (UNLIKELY(mConnector == nullptr)) {
            NET_LOG_ERROR("Net Connector net ready.");
            return MMS_NOT_READY;
        }
        return mConnector->SyncConnect(info);
    }

    BResult AsyncConnect(ConnectInfo &info, AsyncConnHandler handler, uintptr_t ctx)
    {
        if (UNLIKELY(mConnector == nullptr)) {
            NET_LOG_ERROR("Net Connector net ready.");
            return MMS_NOT_READY;
        }
        return mConnector->AsyncConnect(info, handler, ctx);
    }

    BResult SendFds(ChannelPtr ch, int32_t fds[], uint32_t count)
    {
        auto ret = ch->SendFds(fds, count);
        return NetResult(ret);
    }

    BResult ReceiveFds(const MmsNodeId &targetNodeId, int32_t fds[], uint32_t count)
    {
        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, ch, 0);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return MMS_NET_RETRY;
        }
        int32_t timeoutSec = -1;
        ret = NetResult(ch->ReceiveFds(fds, count, timeoutSec));
        if (ret != MMS_OK) {
            NET_LOG_ERROR("Failed to receive by target node id " << targetNodeId << ", result " << ret);
            return ret;
        }
        return MMS_OK;
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(const MmsNodeId &targetNodeId, uint32_t groupIndex, uint16_t opCode, TReq &req, TResp &resp)
    {
        bool isValidOp = true;
        isValidOp = (opCode < MAX_NEW_REQ_HANDLER);
        if (UNLIKELY(!isValidOp)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return MMS_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
        BResult ret = MMS_INNER_ERR;
        ret = GetChanel(targetNodeId, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_WARN("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return MMS_NET_RETRY;
        }

        ret = SyncCall(opCode, req, resp, ch);
        return ret;
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(const MmsNodeId &targetNodeId, uint32_t groupIndex, uint16_t opCode, TReq &req, TResp **resp,
                     uint64_t &respLen)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return MMS_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return MMS_NET_RETRY;
        }

        ret = SyncCall(opCode, req, resp, respLen, ch);
        return ret;
    }

    template <typename TResp>
    BResult SyncCall(const MmsNodeId &targetNodeId, uint32_t groupIndex, uint16_t opCode, void *req, uint32_t reqLen,
                     TResp &resp)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return MMS_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return MMS_NET_RETRY;
        }

        ret = SyncCall(opCode, req, reqLen, resp, ch);
        return ret;
    }

    template <typename TReq> BResult AsyncCallWithoutResponse(const MmsNodeId &targetNodeId, uint32_t groupIndex,
                                                              uint16_t opCode, TReq &req)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return MMS_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return MMS_NET_RETRY;
        }

        return AsyncCallWithoutResponse(opCode, req, ch);
    }

    template <typename TReq>
    void AsyncCall(const MmsNodeId &targetNodeId, uint32_t groupIndex, uint16_t opCode, TReq &req, Callback callback)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            callback.cb(callback.cbCtx, nullptr, 0, MMS_INVALID_PARAM);
            return;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            callback.cb(callback.cbCtx, nullptr, 0, MMS_NET_RETRY);
            return;
        }

        AsyncCall(opCode, req, ch, callback);
    }

    void AsyncCallBuff(const MmsNodeId &targetNodeId, uint32_t groupIndex, uint16_t opCode, void *req,
                       uint32_t reqLen, Callback callback)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            callback.cb(callback.cbCtx, nullptr, 0, MMS_INVALID_PARAM);
            return;
        }

        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            callback.cb(callback.cbCtx, nullptr, 0, MMS_NET_RETRY);
            return;
        }

        AsyncCallBuffInner(opCode, req, reqLen, ch, callback);
    }

    BResult SyncRead(const MmsNodeId &targetNodeId, uint32_t groupIndex, uint32_t pid, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, pid, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", result " << ret);
            return MMS_NET_RETRY;
        }
        ret = ch->Get(req, nullptr);
        return NetResult(ret);
    }

    BResult SyncRead(const MmsNodeId &targetNodeId, uint32_t groupIndex, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", result " << ret);
            return MMS_NET_RETRY;
        }
        ret = ch->Get(req, nullptr);
        return NetResult(ret);
    }

    BResult SyncRead(ChannelPtr ch, const NetRequest &req)
    {
        using namespace ock::hcom;
        int ret = MMS_OK;
        ret = ch->Get(req, nullptr);
        return NetResult(ret);
    }

    BResult SyncWrite(const MmsNodeId &targetNodeId, uint32_t groupIndex, uint32_t pid, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, pid, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", pid:" <<
                pid << ", result " << ret);
            return MMS_NET_RETRY;
        }
        ret = ch->Put(req, nullptr);
        return NetResult(ret);
    }

    BResult SyncWrite(const MmsNodeId &targetNodeId, uint32_t groupIndex, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetChanel(targetNodeId, ch, groupIndex);
        if (UNLIKELY(ret != MMS_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId <<
                ", pid:0, result " << ret);
            return MMS_NET_RETRY;
        }
        ret = ch->Put(req, nullptr);
        return NetResult(ret);
    }

    BResult SyncWrite(ChannelPtr ch, const NetRequest &req)
    {
        using namespace ock::hcom;
        int ret = MMS_OK;
        ret = ch->Put(req, nullptr);
        return NetResult(ret);
    }

    void Reply(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize)
    {
        using namespace ock::hcom;
        int32_t result = MMS_ERR;

        NetCallback *callback = UBSHcomNewCallback([this](UBSHcomServiceContext &context) {
            }, std::placeholders::_1);

        UBSHcomReplyContext replyCtx;
        replyCtx.errorCode = static_cast<int16_t>(retCode);
        replyCtx.rspCtx = ctx.RspCtx();
        UBSHcomRequest reqMsg;
        if (resp != nullptr) {
            reqMsg.address = resp;
            reqMsg.size = respSize;
            result = ctx.Channel()->Reply(replyCtx, reqMsg, callback);
        } else {
            reqMsg.address = &retCode;
            reqMsg.size = sizeof(retCode);
            result = ctx.Channel()->Reply(replyCtx, reqMsg, callback);
        }
        if (UNLIKELY(result != MMS_OK)) {
            LOG_ERROR("Reply Send failed, ret:" << result << ".");
        }
    }

    BResult RegisterNewRequestHandler(uint32_t opCode, const NewRequestHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return MMS_INVALID_PARAM;
        }

        if (UNLIKELY(mHandlers[opCode] != nullptr)) {
            NET_LOG_ERROR("Handler for opCode " << opCode << " already registered");
            return MMS_ALREADY_DONE;
        }

        mHandlers[opCode] = h;
        return MMS_OK;
    }

    BResult RegisterNewChannelHandler(const NewChannelHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(mHandleNewChannel != nullptr)) {
            NET_LOG_ERROR("Failed to register new channel handler");
            return MMS_ERR;
        }

        mHandleNewChannel = h;
        return MMS_OK;
    }

    BResult RegisterChannelBrokenHandler(const ChannelBrokenHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(mHandlerBroken != nullptr)) {
            NET_LOG_ERROR("Failed to register channel broken handler");
            return MMS_ERR;
        }

        mHandlerBroken = h;
        return MMS_OK;
    }

    NetChannelMgrPtr &GetChannelMgr()
    {
        return mChannelMgr;
    }

    ServiceProtocol GetNetProtocol(ConnectMode mode)
    {
        if (mode == CONNECT_IPC) {
            return mIpcOptions.protocol;
        } else {
            return mRpcOptions.protocol;
        }
    }

    uint16_t GetConnectCount(ConnectMode mode)
    {
        if (mode == CONNECT_IPC) {
            return mIpcOptions.connCount;
        } else {
            return mRpcOptions.connCount;
        }
    };

    uint16_t GetGroupNum(ConnectMode mode)
    {
        if (mode == CONNECT_IPC) {
            return mIpcOptions.workerGroupsNum;
        } else {
            return mRpcOptions.workerGroupsNum;
        }
    };

    inline BResult GetChanel(const MmsNodeId &targetNodeId, ChannelPtr &ch, uint32_t groupIndex)
    {
        BResult result = mChannelMgr->GetChannel(targetNodeId, ch, groupIndex);
        return result;
    }

    inline BResult GetChanel(const MmsNodeId &targetNodeId, uint32_t pid, ChannelPtr &ch, uint32_t groupIndex)
    {
        return mChannelMgr->GetChannel(targetNodeId, pid, ch, groupIndex);
    }

    inline NetRequest InitNetRequest(uintptr_t la, uintptr_t ra, uint64_t lk, uint64_t rk, uint32_t size)
    {
        NetRequest req;
        ock::hcom::UBSHcomMemoryKey lKey;
        lKey.keys[0] = lk;
        ock::hcom::UBSHcomMemoryKey rKey;
        rKey.keys[0] = rk;
        req.lKey = lKey;
        req.rKey = rKey;
        req.lAddress = la;
        req.rAddress = ra;
        req.size = size;
        return req;
    }

    void FillConnectOption(ConnectMode mode, ConnectInfo &info, uint32_t groupIndex, std::string &prefix,
                           ock::hcom::UBSHcomConnectOptions &op);
    BResult ConnectToPeer(ConnectMode mode, ConnectInfo &info, uint32_t groupIndex, ChannelPtr &ch);

    BResult InitMemoryRegister(void);

    int32_t NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload);
    void ChannelBroken(const ChannelPtr &ch);
    int32_t RequestReceived(ServiceContext &ctx);
    int32_t RequestInnerReceived(ServiceContext &ctx);
    int RequestPosted(const ServiceContext &ctx);
    int OneSideDone(const ServiceContext &ctx);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult AssignIpcServiceOptions(const NetOptions &opt, bool isOobSvr);
    BResult StartIpcService(const NetOptions &opt);
    BResult AssignRpcServiceOptions(const NetOptions &opt, bool isOobSvr);
    BResult StartRpcService(const NetOptions &opt);
    PrivateKeyCallback CreatePrivateKeyCallback(const NetOptions &options);
    void SetDriverTlsCallback(const NetOptions &options, ock::hcom::UBSHcomTlsOptions &tlsOpt);
    BResult PrepareTlsDecrypter(const NetOptions &config);

    void StopInner();

    inline void RegisterDecryptHandler(const DecryptFunc &h)
    {
        mDecryptHandler = h;
    }

    static inline BResult NetResult(hcom::SerResult ret)
    {
        using namespace ock::hcom;
        switch (ret) {
            case SER_OK:
                return MMS_OK;
            case SER_NEW_OBJECT_FAILED:
            case SER_NOT_ESTABLISHED:
            case SER_TIMEOUT:
                return MMS_NET_RETRY;
            default:
                return MMS_NET_RETRY; // hcom错误码太多，SDK端IO遇到网络IO异常时需要进行重试处理
        }
    }

    template <typename TReq, typename TResp> BResult SyncCall(uint16_t opCode, TReq &req, TResp &resp, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        UBSHcomRequest reqMsg(static_cast<void *>(&req), sizeof(TReq), opCode);
        UBSHcomResponse respMsg(static_cast<void *>(&resp), sizeof(TResp));
        auto result = ch->Call(reqMsg, respMsg);
        if (UNLIKELY(result != MMS_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            return NetResult(result);
        }

        if (NN_UNLIKELY(respMsg.errorCode != MMS_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", error code " << respMsg.errorCode);
            return respMsg.errorCode;
        }

        return MMS_OK;
    }

    template <typename TResp>
    BResult SyncCall(uint16_t opCode, void *req, uint32_t reqLen, TResp &resp, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        UBSHcomRequest reqMsg(req, reqLen, opCode);
        UBSHcomResponse respMsg(static_cast<void *>(&resp), sizeof(TResp));
        auto result = ch->Call(reqMsg, respMsg);
        if (UNLIKELY(result != MMS_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            return NetResult(result);
        }

        if (NN_UNLIKELY(respMsg.errorCode != MMS_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", error code " << respMsg.errorCode);
            return respMsg.errorCode;
        }

        return MMS_OK;
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(uint16_t opCode, TReq &req, TResp **resp, uint64_t &respLen, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        BResult result = MMS_INNER_ERR;
        UBSHcomRequest reqMsg(static_cast<void *>(&req), sizeof(TReq), opCode);
        UBSHcomResponse respMsg{};

        result = ch->Call(reqMsg, respMsg);
        if (UNLIKELY(result != MMS_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            return NetResult(result);
        }

        if (NN_UNLIKELY(respMsg.errorCode != MMS_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", error code " << respMsg.errorCode);
            return respMsg.errorCode;
        }

        *resp = reinterpret_cast<TResp *>(respMsg.address);
        respLen = respMsg.size;
        return MMS_OK;
    }

    template <typename TReq> BResult AsyncCallWithoutResponse(uint16_t opCode, TReq &req, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        int32_t result = MMS_ERR;
        UBSHcomRequest reqMsg(static_cast<void *>(&req), sizeof(TReq), opCode);
        UBSHcomResponse respMsg{};

        auto *netCallback = UBSHcomNewCallback([](UBSHcomServiceContext &context) { return; }, std::placeholders::_1);
        result = ch->Call(reqMsg, respMsg, netCallback);
        if (UNLIKELY(result != MMS_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            return NetResult(result);
        }
        return MMS_OK;
    }

    inline void AsyncCallDone(int32_t result, uint64_t ts)
    {
    }

    template <typename TReq>
    void AsyncCall(uint16_t opCode, TReq &req, ChannelPtr &ch, Callback callback)
    {
        using namespace ock::hcom;
        int32_t result = MMS_ERR;
        UBSHcomRequest reqMsg(static_cast<void *>(&req), sizeof(TReq), opCode);
        UBSHcomResponse respMsg{};
        uint64_t ts = Monotonic::TimeNs();

        auto *netCallback = UBSHcomNewCallback(
            [this, ts, callback](UBSHcomServiceContext &context) {
                if (context.Result() != SER_OK) {
                    AsyncCallDone(context.Result(), ts);
                    callback.cb(callback.cbCtx, nullptr, 0, NetResult(context.Result()));
                } else {
                    AsyncCallDone(context.ErrorCode(), ts);
                    callback.cb(callback.cbCtx, context.MessageData(), context.MessageDataLen(), context.ErrorCode());
                }
            },
            std::placeholders::_1);
        result = ch->Call(reqMsg, respMsg, netCallback);
        if (UNLIKELY(result != MMS_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            callback.cb(callback.cbCtx, nullptr, 0, NetResult(result));
        }
    }

    inline void AsyncCallBuffDone(int32_t result, uint64_t ts)
    {
    }

    void AsyncCallBuffInner(uint16_t opCode, void *req, uint32_t reqLen, ChannelPtr &ch, Callback callback)
    {
        using namespace ock::hcom;
        int32_t result = MMS_ERR;
        UBSHcomRequest reqMsg(req, reqLen, opCode);
        UBSHcomResponse respMsg{};
        uint64_t ts = Monotonic::TimeNs();

        auto *netCallback = UBSHcomNewCallback(
            [this, ts, callback](UBSHcomServiceContext &context) {
                if (context.Result() != SER_OK) {
                    AsyncCallBuffDone(context.Result(), ts);
                    callback.cb(callback.cbCtx, nullptr, 0, NetResult(context.Result()));
                } else {
                    AsyncCallBuffDone(context.ErrorCode(), ts);
                    callback.cb(callback.cbCtx, context.MessageData(), context.MessageDataLen(), context.ErrorCode());
                }
            },
            std::placeholders::_1);
        result = ch->Call(reqMsg, respMsg, netCallback);
        if (UNLIKELY(result != MMS_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            callback.cb(callback.cbCtx, nullptr, 0, NetResult(result));
        }
    }

private:
    static constexpr uint32_t MAX_NEW_REQ_HANDLER = 256L;

private:
    bool mStarted = false;
    int16_t mTimeout = -1;
    NetChannelMgrPtr mChannelMgr = nullptr;
    NewRequestHandler mHandlers[MAX_NEW_REQ_HANDLER]{};
    NetConnectorPtr mConnector = nullptr;
    NewChannelHandler mHandleNewChannel = nullptr;
    ChannelBrokenHandler mHandlerBroken = nullptr;
    ock::hcom::UBSHcomService *mRpcService = nullptr;
    ock::hcom::UBSHcomService *mIpcService = nullptr;
    NetOptions mRpcOptions;
    NetOptions mIpcOptions;
    std::mutex mMutex;
    NetExecutorPoolPtr mRequestExecutor = nullptr;
    uint32_t mReqExecutorNum;
    NetMemList mMemList;
    DecryptFunc mDecryptHandler;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // NET_ENGINE_H

