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

#ifndef NET_ENGINE_H
#define NET_ENGINE_H

#include <cstdint>
#include <arpa/inet.h>
#include "hcom/hcom.h"
#include "hcom/hcom_service.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "bio_monotonic.h"
#include "bio_tls_util.h"
#include "net_common.h"
#include "net_channel_mgr.h"
#include "net_connector.h"
#include "net_block_pool.h"
#include "net_executor_pool.h"

#ifdef DEBUG_UT
#include "../../test/llt/unit-tests/net/net_stub.h"
#endif

namespace ock {
namespace bio {
constexpr size_t KEYPASS_MAX_LEN = 10000;

class NetEngine {
public:
    NetEngine() = default;
    ~NetEngine() = default;

    BResult Initialize(int16_t timeoutSec, uint32_t coreThreadNum, uint32_t queueSize, NetLogFunc func);
    BResult Start(const NetOptions &opt);
    void Stop();

    inline BResult RegisterMemoryRegion(uint8_t *addr, uint64_t size, MemoryRegion &mr)
    {
        if (UNLIKELY(mRpcService == nullptr)) {
            NET_LOG_ERROR("Net service not ready.");
            return BIO_NOT_READY;
        }
        return mRpcService->RegisterMemoryRegion(reinterpret_cast<uintptr_t>(addr), size, mr);
    }

    void Show(uint32_t &executorNum, NetOptions &option)
    {
        executorNum = reqExecutorNum;
        option = mOptions;
    }

    inline BResult RegisterMemoryRegion(uint64_t size, MemoryRegion &mr)
    {
        if (UNLIKELY(mRpcService == nullptr)) {
            NET_LOG_ERROR("Net service not ready.");
            return BIO_NOT_READY;
        }
        return mRpcService->RegisterMemoryRegion(size, mr);
    }

    inline void DestroyMemoryRegion(MemoryRegion &mr)
    {
        if (UNLIKELY(mRpcService == nullptr)) {
            NET_LOG_ERROR("Net service not ready.");
            return;
        }
        mRpcService->DestroyMemoryRegion(mr);
    }

    static inline NetMrInfo MemoryRegionInfo(MemoryRegion &mr)
    {
        return NetMrInfo(mr.GetAddress(), mr.GetSize(), mr.GetHcomMrs()[0]->GetLKey());
    }

    inline void SetDataPageKb(uint32_t dataPageKb)
    {
        mDataPageBytes = dataPageKb * KB_UNIT;
    }

    inline uint32_t GetDataPage() const
    {
        return mDataPageBytes;
    }

    inline BResult AllocLocalMrBatch(uint32_t count, std::vector<uintptr_t> &address, uint64_t &outKey)
    {
        if (UNLIKELY(mMrBlockPool == nullptr)) {
            NET_LOG_ERROR("Net block pool not ready.");
            return BIO_NOT_READY;
        }
        outKey = mLocalMr.GetHcomMrs()[0]->GetLKey();
        return mMrBlockPool->AllocMany(count, address);
    }

    inline uint64_t GetUsedBlockSize()
    {
        return mUsedBlock.load() * mDataPageBytes;
    }

    inline BResult AllocLocalMrSingle(uintptr_t &address, uint64_t &outKey)
    {
        if (UNLIKELY(mMrBlockPool == nullptr)) {
            NET_LOG_ERROR("Net block pool not ready.");
            return BIO_NOT_READY;
        }
        outKey = mLocalMr.GetHcomMrs()[0]->GetLKey();
        auto ret = mMrBlockPool->AllocOne(address);
        if (ret == BIO_OK) {
            mUsedBlock += NO_1;
        }
        return ret;
    }

    inline BResult GetLocalMrKey(uint64_t &outKey)
    {
        if (UNLIKELY(mMrBlockPool == nullptr)) {
            NET_LOG_ERROR("Net block pool not ready.");
            return BIO_NOT_READY;
        }
        outKey = mLocalMr.GetHcomMrs()[0]->GetLKey();
        return BIO_OK;
    }

    inline void FreeLocalMrBatch(std::vector<uintptr_t> &address)
    {
        if (UNLIKELY(mMrBlockPool == nullptr)) {
            NET_LOG_ERROR("Net block pool not ready.");
            return;
        }
        mMrBlockPool->ReleaseMany(address);
    }

    inline void FreeLocalMrSingle(uintptr_t address)
    {
        if (UNLIKELY(mMrBlockPool == nullptr || address == 0)) {
            return;
        }
        mMrBlockPool->ReleaseOne(address);
        mUsedBlock -= NO_1;
    }

    int QueryShmInfo(int32_t &fd, uint64_t &offset, uint64_t &length, uint64_t &mKey)
    {
        fd = mShmFd;
        offset = mShareOffset;
        length = mOptions.memorySize;
        mKey = 0;
        if (mOptions.memorySize > 0) {
            // 配置内存小于等于0，未初始化内存
            if (mLocalMr.GetHcomMrs().empty()) {
                return BIO_ERR;
            }
            mKey = mLocalMr.GetHcomMrs()[0]->GetLKey();
        }
        return BIO_OK;
    }

    void SetShmInfo(int32_t fd, uint8_t *addr, uint64_t off, uint64_t size)
    {
        mShmFd = fd;
        mShareOffset = off;
        mShmSize = size;
        mShareAddress = addr;
    }

    void UpdateTimeOut(int16_t timeoutSec)
    {
        mTimeout = timeoutSec;
    }

    BResult UpdateChannelTimeOut(const BioNodeId &targetNodeId)
    {
        ChannelPtr ch{nullptr};

        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_WARN("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_ERR;
        }
        ch->SetChannelTimeOut(mTimeout, mTimeout);

        ret = GetDataChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_WARN("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_ERR;
        }
        ch->SetChannelTimeOut(mTimeout, mTimeout);
        return BIO_OK;
    }

    uint8_t *GetShmAddress(uint64_t offset, uint64_t len)
    {
        if (UNLIKELY(offset < mShareOffset || offset >= mShareOffset + mShmSize || len >= mShareOffset + mShmSize ||
                     offset > mShareOffset + mShmSize - len)) {
            NET_LOG_ERROR("Shm info, offset:" << mShareOffset << ", size:" << mShmSize << ".");
            return nullptr;
        }
        return (mShareAddress + (offset - mShareOffset));
    }

    uint64_t GetAddressOffset(uint64_t addr)
    {
        auto shmAddr = reinterpret_cast<uintptr_t>(mShareAddress);
        if (UNLIKELY(addr < shmAddr || addr >= shmAddr + mShmSize)) {
            return UINT64_MAX;
        }
        return ((addr - shmAddr) + mShareOffset);
    }

    BResult CheckConnect(const BioNodeId targetNodeId)
    {
        ChannelPtr ch;
        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK)) {
            return BIO_NET_RETRY;
        }
        ret = GetDataChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK)) {
            return BIO_NET_RETRY;
        }
        return BIO_OK;
    }

    BResult SyncConnect(ConnectInfo &info)
    {
        if (UNLIKELY(mConnector == nullptr)) {
            NET_LOG_ERROR("Net Connector net ready.");
            return BIO_NOT_READY;
        }
        return mConnector->SyncConnect(info);
    }

    BResult AsyncConnect(ConnectInfo &info, AsyncConnHandler handler, uintptr_t ctx)
    {
        if (UNLIKELY(mConnector == nullptr)) {
            NET_LOG_ERROR("Net Connector net ready.");
            return BIO_NOT_READY;
        }
        return mConnector->AsyncConnect(info, handler, ctx);
    }

    BResult SendFds(ChannelPtr ch, int32_t fds[], uint32_t count)
    {
#ifndef DEBUG_UT
        // 内存设置0，未初始化fd,fds[0] == -1直接返回即可
        if (count == NO_1 && fds[0] == -1) {
            return BIO_OK;
        }
        auto ret = ch->SendFds(fds, count);
#else
        auto ret = NetStub::SendFds(fds, count);
#endif
        return NetResult(ret);
    }

    BResult ReceiveFds(const BioNodeId &targetNodeId, int32_t fds[], uint32_t count)
    {
        ChannelPtr ch{nullptr};
        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }
        int32_t timeoutSec = -1;
#ifndef DEBUG_UT
        return NetResult(ch->ReceiveFds(fds, count, timeoutSec));
#else
        return NetStub::ReceiveFds(fds, count, timeoutSec);
#endif
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, TResp &resp)
    {
        BIO_TRACE_START(NET_TRACE_SYNC_CALL_V1);
        bool isValidOp = true;
        BIO_TP_START(SYNCCALL_OPCODE_FAIL, &isValidOp, false);
        isValidOp = (opCode < MAX_NEW_REQ_HANDLER);
        BIO_TP_END;
        if (UNLIKELY(!isValidOp)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            BIO_TRACE_END(NET_TRACE_SYNC_CALL_V1, BIO_INVALID_PARAM);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{nullptr};
        BResult ret = BIO_INNER_ERR;
        BIO_TP_START(SYNCCALL_CHANNEL_FAIL, &ret, BIO_ERR);
        ret = GetCtrlChanel(targetNodeId, ch);
        BIO_TP_END;
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_WARN("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            BIO_TRACE_END(NET_TRACE_SYNC_CALL_V1, BIO_NET_RETRY);
            return BIO_NET_RETRY;
        }

        ret = SyncCall(opCode, req, resp, ch);
        BIO_TRACE_END(NET_TRACE_SYNC_CALL_V1, ret);
        return ret;
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, TResp **resp, uint64_t &respLen)
    {
        BIO_TRACE_START(NET_TRACE_SYNC_CALL_V2);
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            BIO_TRACE_END(NET_TRACE_SYNC_CALL_V2, BIO_INVALID_PARAM);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{nullptr};
        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            BIO_TRACE_END(NET_TRACE_SYNC_CALL_V2, BIO_NET_RETRY);
            return BIO_NET_RETRY;
        }

        ret = SyncCall(opCode, req, resp, respLen, ch);
        BIO_TRACE_END(NET_TRACE_SYNC_CALL_V2, ret);
        return ret;
    }

    template <typename TResp>
    BResult SyncCallBuff(const BioNodeId &targetNodeId, uint16_t opCode, void *req, uint32_t reqLen, TResp &resp)
    {
        BIO_TRACE_START(NET_TRACE_SYNC_CALL_BUFF);
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            BIO_TRACE_END(NET_TRACE_SYNC_CALL_BUFF, BIO_INVALID_PARAM);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{nullptr};
        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            BIO_TRACE_END(NET_TRACE_SYNC_CALL_BUFF, BIO_NET_RETRY);
            return BIO_NET_RETRY;
        }

        ret = SyncCallBuffInner(opCode, req, reqLen, resp, ch);
        BIO_TRACE_END(NET_TRACE_SYNC_CALL_BUFF, ret);
        return ret;
    }

    template <typename TReq>
    BResult AsyncCallWithoutResponse(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{nullptr};
        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }

        return AsyncCallWithoutResponse(opCode, req, ch);
    }

    template <typename TReq>
    void AsyncCall(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req, Callback callback)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_INVALID_PARAM);
            return;
        }

        ChannelPtr ch{nullptr};
        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_NET_RETRY);
            return;
        }

        AsyncCall(opCode, req, ch, callback);
    }

    void AsyncCallBuff(const BioNodeId &targetNodeId, uint16_t opCode, void *req, uint32_t reqLen, Callback callback)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_INVALID_PARAM);
            return;
        }

        ChannelPtr ch{nullptr};
        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            callback.cb(callback.cbCtx, nullptr, 0, BIO_NET_RETRY);
            return;
        }

        AsyncCallBuffInner(opCode, req, reqLen, ch, callback);
    }

    BResult SyncRead(const BioNodeId &targetNodeId, uint32_t pid, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{nullptr};
        auto ret = GetDataChanel(targetNodeId, pid, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }
        BIO_TP_START(SERVER_NET_RDMA_READ_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Get(req, nullptr);
#else
        ret = NetStub::Get(req);
#endif
        BIO_TP_END;
        return NetResult(ret);
    }

    BResult SyncRead(const BioNodeId &targetNodeId, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{nullptr};
        auto ret = GetDataChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }
        BIO_TRACE_START(NET_TRACE_SYNC_READ_V1);
        BIO_TP_START(SERVER_NET_RDMA_READ_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Get(req, nullptr);
#else
        ret = NetStub::Get(req);
#endif
        BIO_TP_END;
        BIO_TRACE_END(NET_TRACE_SYNC_READ_V1, ret);
        return NetResult(ret);
    }

    BResult SyncRead(ChannelPtr ch, const NetRequest &req)
    {
        using namespace ock::hcom;
        BIO_TRACE_START(NET_TRACE_SYNC_READ_V2);
        int ret;
        BIO_TP_START(SERVER_NET_RDMA_READ_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Get(req, nullptr);
#else
        ret = NetStub::Get(req);
#endif
        BIO_TP_END;
        BIO_TRACE_END(NET_TRACE_SYNC_READ_V2, ret);
        return NetResult(ret);
    }

    BResult SyncWrite(const BioNodeId &targetNodeId, uint32_t pid, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{nullptr};
        auto ret = GetDataChanel(targetNodeId, pid, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", pid:" << pid
                                                                              << ", result " << ret);
            return BIO_NET_RETRY;
        }
        BIO_TP_START(SERVER_NET_RDMA_WRITE_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Put(req, nullptr);
#else
        ret = NetStub::Put(req);
#endif
        BIO_TP_END;
        return NetResult(ret);
    }

    BResult SyncWrite(const BioNodeId &targetNodeId, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{nullptr};
        auto ret = GetDataChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", pid:0, result "
                                                                              << ret);
            return BIO_NET_RETRY;
        }
        BIO_TRACE_START(NET_TRACE_SYNC_WRITE_V1);
        BIO_TP_START(SERVER_NET_RDMA_WRITE_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Put(req, nullptr);
#else
        ret = NetStub::Put(req);
#endif
        BIO_TP_END;
        BIO_TRACE_END(NET_TRACE_SYNC_WRITE_V1, ret);
        return NetResult(ret);
    }

    BResult SyncWrite(ChannelPtr ch, const NetRequest &req)
    {
        using namespace ock::hcom;
        BIO_TRACE_START(NET_TRACE_SYNC_WRITE_V2);
        int ret;
        BIO_TP_START(SERVER_NET_RDMA_WRITE_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Put(req, nullptr);
#else
        ret = NetStub::Put(req);
#endif
        BIO_TP_END;
        BIO_TRACE_END(NET_TRACE_SYNC_WRITE_V2, ret);
        return NetResult(ret);
    }

    void ReplyDone(int32_t ret, uint64_t ts)
    {
        BIO_TRACE_ASYNC_END(NET_TRACE_REPLY_ASYNC, ret, ts);
    }

    void Reply(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize)
    {
        using namespace ock::hcom;
        int32_t result = BIO_ERR;

#ifndef DEBUG_UT
        BIO_TRACE_ASYNC_BEGIN(NET_TRACE_REPLY_ASYNC);
        uint64_t ts = Monotonic::TimeNs();
        NetCallback *callback = UBSHcomNewCallback(
            [this, ts](UBSHcomServiceContext &context) { ReplyDone(context.Result(), ts); }, std::placeholders::_1);

        BIO_TRACE_START(NET_TRACE_REPLY_SYNC);
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
        BIO_TRACE_END(NET_TRACE_REPLY_SYNC, result);
#else
        result = NetStub::Reply(retCode, resp, respSize);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Reply Send failed, ret:" << result << ".");
        }
    }

    BResult RegisterNewRequestHandler(uint32_t opCode, const NewRequestHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        if (UNLIKELY(mHandlers[opCode] != nullptr)) {
            NET_LOG_ERROR("Handler for opCode " << opCode << " already registered");
            return BIO_ALREADY_DONE;
        }

        mHandlers[opCode] = h;
        return BIO_OK;
    }

    BResult RegisterChannelBrokenHandler(const ChannelBrokenHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(mHandlerBroken != nullptr)) {
            NET_LOG_ERROR("Failed to register channel broken handler");
            return BIO_ERR;
        }

        mHandlerBroken = h;
        return BIO_OK;
    }

    inline void SetLocalNodeId(const uint16_t &nodeId)
    {
        mLocalNodeId = nodeId;
    }

    NetChannelMgrPtr &GetCtrlChannelMgr()
    {
        return mCtrlChannelMgr;
    }

    NetChannelMgrPtr &GetDataChannelMgr()
    {
        return mDataChannelMgr;
    }

    ServiceProtocol GetNetProtocol()
    {
        return mOptions.protocol;
    }

    uint16_t GeConnectCount()
    {
        return mOptions.connCount;
    }

    inline BResult GetCtrlChanel(const BioNodeId &targetNodeId, ChannelPtr &ch)
    {
        BIO_TRACE_START(NET_TRACE_GET_CTRL_CHANNEL);
        BResult result = mCtrlChannelMgr->GetChannel(targetNodeId, ch);
        BIO_TRACE_END(NET_TRACE_GET_CTRL_CHANNEL, result);
        return result;
    }

    inline BResult GetDataChanel(const BioNodeId &targetNodeId, uint32_t pid, ChannelPtr &ch)
    {
        return mDataChannelMgr->GetChannel(targetNodeId, pid, ch);
    }

    inline BResult IsChannelExist(const BioNodeId &targetNodeId, uint32_t pid)
    {
        ChannelPtr ch = nullptr;
        return mDataChannelMgr->GetChannel(targetNodeId, pid, ch) == BIO_OK;
    }

    inline BResult GetDataChanel(const BioNodeId &targetNodeId, ChannelPtr &ch)
    {
        BIO_TRACE_START(NET_TRACE_GET_DATA_CHANNEL);
        BResult result = mDataChannelMgr->GetChannel(targetNodeId, ch);
        BIO_TRACE_END(NET_TRACE_GET_DATA_CHANNEL, result);
        return result;
    }

    inline bool IsValidAddress(uint64_t begin, uint64_t end)
    {
        return (begin >= mLocalMr.GetAddress()) && (end <= (mLocalMr.GetAddress() + mOptions.memorySize));
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

    void FillConnectOption(ConnectInfo &info, bool isCtrl, std::string &prefix, ock::hcom::UBSHcomConnectOptions &op);
    BResult ConnectToPeer(ConnectMode mode, ConnectInfo &info, bool isCtrlPanel, ChannelPtr &ch);

    BResult InitCommMemAllocator();
    BResult InitShmMemAllocator();
    BResult InitMemoryAllocator();

    int32_t NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload);
    void ChannelBroken(const ChannelPtr &ch);
    int32_t RequestReceived(ServiceContext &ctx);
    int32_t RequestIPCReceived(ServiceContext &ctx);
    int RequestPosted(const ServiceContext &ctx);
    int OneSideDone(const ServiceContext &ctx);

    DEFINE_REF_COUNT_FUNCTIONS

private:
    BResult AssignIpcServiceOptions(const NetOptions &opt, bool isOobSvr);
    BResult StartIpcService(const NetOptions &opt);
    BResult AssignRpcServiceOptions(const NetOptions &opt, bool isOobSvr);
    BResult StartRpcService(const NetOptions &opt);
    void SetDriverTlsCallback(const NetOptions &options, ock::hcom::UBSHcomTlsOptions &tlsOpt);

    BResult PrepareTlsDecrypter(const NetOptions &config);

    void StopInner();

    BResult CreateShmFdWithName(int32_t &shmFd, uint64_t size, std::string &name);

    inline void RegisterDecryptHandler(const DecryptFunc &h)
    {
        mDecryptHandler = h;
    }

    static inline BResult NetResult(hcom::SerResult ret)
    {
        using namespace ock::hcom;
        switch (ret) {
            case SER_OK:
                return BIO_OK;
            case SER_NEW_OBJECT_FAILED:
            case SER_NOT_ESTABLISHED:
            case SER_TIMEOUT:
                return BIO_NET_RETRY;
            default:
                return BIO_NET_RETRY; // hcom错误码太多, SDK端IO遇到网络异常时需要进行重试处理
        }
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(uint16_t opCode, TReq &req, TResp &resp, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        UBSHcomRequest reqMsg(static_cast<void *>(&req), sizeof(TReq), opCode);
        UBSHcomResponse respMsg(static_cast<void *>(&resp), sizeof(TResp));
#ifndef DEBUG_UT
        auto result = ch->Call(reqMsg, respMsg);
#else
        auto result = NetStub::Call(reqMsg, respMsg);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            return NetResult(result);
        }

        if (UNLIKELY(respMsg.errorCode != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", error code " << respMsg.errorCode);
            return respMsg.errorCode;
        }

        return BIO_OK;
    }

    template <typename TResp>
    BResult SyncCallBuffInner(uint16_t opCode, void *req, uint32_t reqLen, TResp &resp, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        UBSHcomRequest reqMsg(req, reqLen, opCode);
        UBSHcomResponse respMsg(static_cast<void *>(&resp), sizeof(TResp));
#ifndef DEBUG_UT
        auto result = ch->Call(reqMsg, respMsg);
#else
        auto result = NetStub::Call(reqMsg, respMsg);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            return NetResult(result);
        }

        if (UNLIKELY(respMsg.errorCode != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", error code " << respMsg.errorCode);
            return respMsg.errorCode;
        }

        return BIO_OK;
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(uint16_t opCode, TReq &req, TResp **resp, uint64_t &respLen, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        BResult result = BIO_INNER_ERR;
        UBSHcomRequest reqMsg(static_cast<void *>(&req), sizeof(TReq), opCode);
        UBSHcomResponse respMsg{};

        BIO_TP_START(SYNCCALL_FAIL, &result, BIO_ERR);
#ifndef DEBUG_UT
        result = ch->Call(reqMsg, respMsg);
#else
        result = NetStub::Call(reqMsg, respMsg);
#endif
        BIO_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer unfixed-length resp with op " << opCode << ", result "
                                                                             << UBSHcomNetErrStr(result));
            return NetResult(result);
        }

        if (UNLIKELY(respMsg.errorCode != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer unfixed-length resp with op " << opCode << ", error code "
                                                                             << respMsg.errorCode);
            return respMsg.errorCode;
        }

        *resp = reinterpret_cast<TResp *>(respMsg.address);
        respLen = respMsg.size;
        return BIO_OK;
    }

    template <typename TReq>
    BResult AsyncCallWithoutResponse(uint16_t opCode, TReq &req, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        int32_t result = BIO_ERR;
        UBSHcomRequest reqMsg(static_cast<void *>(&req), sizeof(TReq), opCode);
        UBSHcomResponse respMsg{};

#ifndef DEBUG_UT
        auto *netCallback = UBSHcomNewCallback([](UBSHcomServiceContext &context) { return; }, std::placeholders::_1);
        result = ch->Call(reqMsg, respMsg, netCallback);
#else
        CbFunc cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
            free(resp);
        };
        Callback cb = Callback(cbFunc, nullptr);
        result = NetStub::AsyncCall(reqMsg, respMsg, cb);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            return NetResult(result);
        }
        return BIO_OK;
    }

    inline void AsyncCallDone(int32_t result, uint64_t ts)
    {
        BIO_TRACE_ASYNC_END(NET_TRACE_ASYNC_CALL, result, ts);
    }

    template <typename TReq>
    void AsyncCall(uint16_t opCode, TReq &req, ChannelPtr &ch, Callback callback)
    {
        using namespace ock::hcom;
        int32_t result = BIO_ERR;
        UBSHcomRequest reqMsg(static_cast<void *>(&req), sizeof(TReq), opCode);
        UBSHcomResponse respMsg{};
        uint64_t ts = Monotonic::TimeNs();

        BIO_TRACE_ASYNC_BEGIN(NET_TRACE_ASYNC_CALL);
        BIO_TP_START(SERVER_NET_ASYNC_CALL_FAIL, &result, BIO_NET_RETRY);
#ifndef DEBUG_UT
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
#else
        result = NetStub::AsyncCall(reqMsg, respMsg, callback);
#endif
        BIO_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            BIO_TRACE_ASYNC_END(NET_TRACE_ASYNC_CALL, result, ts);
            callback.cb(callback.cbCtx, nullptr, 0, NetResult(result));
        }
    }

    inline void AsyncCallBuffDone(int32_t result, uint64_t ts)
    {
        BIO_TRACE_ASYNC_END(NET_TRACE_ASYNC_CALL_BUFF, result, ts);
    }

    void AsyncCallBuffInner(uint16_t opCode, void *req, uint32_t reqLen, ChannelPtr &ch, Callback callback)
    {
        using namespace ock::hcom;
        int32_t result = BIO_ERR;
        UBSHcomRequest reqMsg(req, reqLen, opCode);
        UBSHcomResponse respMsg{};
        uint64_t ts = Monotonic::TimeNs();

        BIO_TRACE_ASYNC_BEGIN(NET_TRACE_ASYNC_CALL_BUFF);
#ifndef DEBUG_UT
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
#else
        result = NetStub::AsyncCall(reqMsg, respMsg, callback);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << UBSHcomNetErrStr(result));
            BIO_TRACE_ASYNC_END(NET_TRACE_ASYNC_CALL_BUFF, result, ts);
            callback.cb(callback.cbCtx, nullptr, 0, NetResult(result));
        }
    }

private:
    static constexpr uint32_t MAX_NEW_REQ_HANDLER = 256L;

private:
    bool mStarted = false;
    int16_t mTimeout = -1;
    uint32_t mDataPageBytes = NO_128 * NO_1024;
    NetChannelMgrPtr mCtrlChannelMgr = nullptr;
    NetChannelMgrPtr mDataChannelMgr = nullptr;
    MemoryRegion mLocalMr;
    NetBlockPoolPtr mMrBlockPool = nullptr;
    std::atomic<uint64_t> mUsedBlock{0};
    NewRequestHandler mHandlers[MAX_NEW_REQ_HANDLER]{};
    NetConnectorPtr mConnector = nullptr;
    DEFINE_REF_COUNT_VARIABLE
    uint16_t mLocalNodeId = UINT16_MAX;
    ChannelBrokenHandler mHandlerBroken = nullptr;
    ock::hcom::UBSHcomService *mRpcService = nullptr;
    ock::hcom::UBSHcomService *mIpcService = nullptr;
    std::mutex mMutex;
    NetOptions mOptions;
    NetExecutorPoolPtr mRequestExecutor = nullptr;
    uint32_t reqExecutorNum;
    int32_t mShmFd = -1;
    uint64_t mShareOffset = 0;
    uint64_t mShmSize = 0;
    uint8_t *mShareAddress = nullptr;
    DecryptFunc mDecryptHandler;
};

using NetEnginePtr = Ref<NetEngine>;
} // namespace bio
} // namespace ock
#endif // NET_ENGINE_H
