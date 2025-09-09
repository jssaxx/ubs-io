/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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
#include "bio_cryptor_helper.h"
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
class NetEngine {
public:
    NetEngine() = default;
    ~NetEngine() = default;

    BResult Initialize(int16_t timeoutSec, uint32_t coreThreadNum, uint32_t queueSize, NetLogFunc func);
    BResult Start(const NetOptions &opt);
    void Stop();

    inline BResult RegisterMemoryRegion(uint8_t *addr, uint64_t size, MemoryRegionPtr &mr)
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

    inline BResult RegisterMemoryRegion(uint64_t size, MemoryRegionPtr &mr)
    {
        if (UNLIKELY(mRpcService == nullptr)) {
            NET_LOG_ERROR("Net service not ready.");
            return BIO_NOT_READY;
        }
        return mRpcService->RegisterMemoryRegion(size, mr);
    }

    inline void DestroyMemoryRegion(MemoryRegionPtr &mr)
    {
        if (UNLIKELY(mRpcService == nullptr)) {
            NET_LOG_ERROR("Net service not ready.");
            return;
        }
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
        if (UNLIKELY(mMrBlockPool == nullptr)) {
            NET_LOG_ERROR("Net block pool not ready.");
            return BIO_NOT_READY;
        }
        outKey = mLocalMr->GetLKey();
        return mMrBlockPool->AllocMany(count, address);
    }

    inline uint64_t GetUsedBlockSize()
    {
        return mUsedBlock * mDataPageBytes;
    }

    void setDriverTlsCallback(ock::hcom::NetService *driver, const NetOptions &options);

    inline BResult AllocLocalMrSingle(uintptr_t &address, uint32_t &outKey)
    {
        if (UNLIKELY(mMrBlockPool == nullptr)) {
            NET_LOG_ERROR("Net block pool not ready.");
            return BIO_NOT_READY;
        }
        outKey = mLocalMr->GetLKey();
        auto ret = mMrBlockPool->AllocOne(address);
        if (ret == BIO_OK) {
            mUsedBlock += NO_1;
        }
        return ret;
    }

    inline BResult GetLocalMrKey(uint32_t &outKey)
    {
        if (UNLIKELY(mMrBlockPool == nullptr)) {
            NET_LOG_ERROR("Net block pool not ready.");
            return BIO_NOT_READY;
        }
        outKey = mLocalMr->GetLKey();
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

    void QueryShmInfo(int32_t &fd, uint64_t &offset, uint64_t &length, uint32_t &mKey)
    {
        fd = mShmFd;
        offset = mShareOffset;
        length = mOptions.memorySize;
        mKey = 0;
        if (mOptions.memorySize > 0) {
            // 配置内存小于等于0，未初始化内存
            mKey = mLocalMr->GetLKey();
        }
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
        ChannelPtr ch{ nullptr };

        auto ret = GetCtrlChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_WARN("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_ERR;
        }
        ch->SetOneSideTimeout(mTimeout);
        ch->SetTwoSideTimeout(mTimeout);
        ret = GetDataChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_WARN("Failed to get channel by target node id " << targetNodeId << ", result " << ret);
            return BIO_ERR;
        }
        ch->SetOneSideTimeout(mTimeout);
        ch->SetTwoSideTimeout(mTimeout);
        return BIO_OK;
    }

    void GetTlsOptions(NetOptions options)
    {
        options.enableTls = mOptions.enableTls;
        options.certificationPath = mOptions.certificationPath;   /* certification path */
        options.caCerPath = mOptions.caCerPath;                   /* caCert path */
        options.caCrlPath = mOptions.caCrlPath;                   /* caCrl path */
        options.privateKeyPath = mOptions.privateKeyPath;         /* private key path */
        options.privateKeyPassword = mOptions.privateKeyPassword; /* private key password */
        options.hseKfsMasterPath = mOptions.hseKfsMasterPath;     /* hseceasy kfs master path */
        options.hseKfsStandbyPath = mOptions.hseKfsStandbyPath;   /* hseceasy kfs standby path */
    }

    void SetTlsOptions(NetOptions options)
    {
        mOptions.enableTls = options.enableTls;
        mOptions.certificationPath = options.certificationPath;   /* certification path */
        mOptions.caCerPath = options.caCerPath;                   /* caCert path */
        mOptions.caCrlPath = options.caCrlPath;                   /* caCrl path */
        mOptions.privateKeyPath = options.privateKeyPath;         /* private key path */
        mOptions.privateKeyPassword = options.privateKeyPassword; /* private key password */
        mOptions.hseKfsMasterPath = options.hseKfsMasterPath;     /* hseceasy kfs master path */
        mOptions.hseKfsStandbyPath = options.hseKfsStandbyPath;   /* hseceasy kfs standby path */
    }

    uint8_t *GetShmAddress(uint64_t offset, uint64_t len)
    {
        if (UNLIKELY(offset < mShareOffset ||
                    offset >= mShareOffset + mShmSize ||
                    offset + len > mShareOffset + mShmSize)) {
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
        ChannelPtr ch{ nullptr };
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
        LVOS_TP_START(SYNCCALL_OPCODE_FAIL, &isValidOp, false);
        isValidOp = (opCode < MAX_NEW_REQ_HANDLER);
        LVOS_TP_END;
        if (UNLIKELY(!isValidOp)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            BIO_TRACE_END(NET_TRACE_SYNC_CALL_V1, BIO_INVALID_PARAM);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
        BResult ret = BIO_INNER_ERR;
        LVOS_TP_START(SYNCCALL_CHANNEL_FAIL, &ret, BIO_ERR);
        ret = GetCtrlChanel(targetNodeId, ch);
        LVOS_TP_END;
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

        ChannelPtr ch{ nullptr };
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

        ChannelPtr ch{ nullptr };
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

    template <typename TReq> BResult AsyncCallWithoutResponse(const BioNodeId &targetNodeId, uint16_t opCode, TReq &req)
    {
        if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
            NET_LOG_ERROR("Invalid opCode " << opCode << " which should be less than " << MAX_NEW_REQ_HANDLER);
            return BIO_INVALID_PARAM;
        }

        ChannelPtr ch{ nullptr };
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

        ChannelPtr ch{ nullptr };
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

        ChannelPtr ch{ nullptr };
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
        ChannelPtr ch{ nullptr };
        auto ret = GetDataChanel(targetNodeId, pid, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }
        LVOS_TP_START(SERVER_NET_RDMA_READ_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Read(req, nullptr);
#else
        ret = NetStub::SyncRead(req);
#endif
        LVOS_TP_END;
        return NetResult(ret);
    }

    BResult SyncRead(const BioNodeId &targetNodeId, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetDataChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", result " << ret);
            return BIO_NET_RETRY;
        }
        BIO_TRACE_START(NET_TRACE_SYNC_READ_V1);
        LVOS_TP_START(SERVER_NET_RDMA_READ_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Read(req, nullptr);
#else
        ret = NetStub::SyncRead(req);
#endif
        LVOS_TP_END;
        BIO_TRACE_END(NET_TRACE_SYNC_READ_V1, ret);
        return NetResult(ret);
    }

    BResult SyncRead(ChannelPtr ch, const NetRequest &req)
    {
        using namespace ock::hcom;
        BIO_TRACE_START(NET_TRACE_SYNC_READ_V2);
        int ret;
        LVOS_TP_START(SERVER_NET_RDMA_READ_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Read(req, nullptr);
#else
        ret = NetStub::SyncRead(req);
#endif
        LVOS_TP_END;
        BIO_TRACE_END(NET_TRACE_SYNC_READ_V2, ret);
        return NetResult(ret);
    }

    BResult SyncWrite(const BioNodeId &targetNodeId, uint32_t pid, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetDataChanel(targetNodeId, pid, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId << ", pid:" <<
                pid << ", result " << ret);
            return BIO_NET_RETRY;
        }
        LVOS_TP_START(SERVER_NET_RDMA_WRITE_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Write(req, nullptr);
#else
        ret = NetStub::SyncWrite(req);
#endif
        LVOS_TP_END;
        return NetResult(ret);
    }

    BResult SyncWrite(const BioNodeId &targetNodeId, const NetRequest &req)
    {
        using namespace ock::hcom;
        ChannelPtr ch{ nullptr };
        auto ret = GetDataChanel(targetNodeId, ch);
        if (UNLIKELY(ret != BIO_OK || ch == nullptr)) {
            NET_LOG_ERROR("Failed to get channel for read by target node id " << targetNodeId <<
                ", pid:0, result " << ret);
            return BIO_NET_RETRY;
        }
        BIO_TRACE_START(NET_TRACE_SYNC_WRITE_V1);
        LVOS_TP_START(SERVER_NET_RDMA_WRITE_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Write(req, nullptr);
#else
        ret = NetStub::SyncWrite(req);
#endif
        LVOS_TP_END;
        BIO_TRACE_END(NET_TRACE_SYNC_WRITE_V1, ret);
        return NetResult(ret);
    }

    BResult SyncWrite(ChannelPtr ch, const NetRequest &req)
    {
        using namespace ock::hcom;
        BIO_TRACE_START(NET_TRACE_SYNC_WRITE_V2);
        int ret;
        LVOS_TP_START(SERVER_NET_RDMA_WRITE_FAIL, &ret, BIO_NET_RETRY);
#ifndef DEBUG_UT
        ret = ch->Write(req, nullptr);
#else
        ret = NetStub::SyncWrite(req);
#endif
        LVOS_TP_END;
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
        NetCallback *callback = NewCallback([this, ts](NetServiceContext &context) {
            ReplyDone(context.Result(), ts);
            }, std::placeholders::_1);

        BIO_TRACE_START(NET_TRACE_REPLY_SYNC);
        NetServiceOpInfo opInfo{};
        opInfo.errorCode = static_cast<int16_t>(retCode);
        if (resp != nullptr) {
            result = ctx.ReplySend(opInfo, { resp, respSize }, callback);
        } else {
            result = ctx.ReplySend(opInfo, { &retCode, sizeof(retCode) }, callback);
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

    BResult RegisterNewChannelHandler(const NewChannelHandler &h)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(mHandleNewChannel != nullptr)) {
            NET_LOG_ERROR("Failed to register new channel handler");
            return BIO_ERR;
        }

        mHandleNewChannel = h;
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
    };

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
        return (begin >= mLocalMr->GetAddress()) && (end <= (mLocalMr->GetAddress() + mOptions.memorySize));
    }

    void FillConnectOption(ConnectInfo &info, bool isCtrl, std::string &prefix,
        ock::hcom::NetServiceConnectOptions &op);
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
    void AssignIpcServiceOptions(const NetOptions &opt, bool isOobSvr, ock::hcom::NetServiceOptions &options);
    BResult StartIpcService(const NetOptions &opt);
    BResult AssignRpcServiceOptions(const NetOptions &opt, bool isOobSvr, ock::hcom::NetServiceOptions &options);
    BResult StartRpcService(const NetOptions &opt);

    BResult PrepareHseCryptor(std::string kfsMaster, std::string kfsStandby);

    std::string GenerateWorkersSetting(const NetOptions& opt);

    void StopInner();

    BResult CreateShmFdWithName(int32_t &shmFd, uint64_t size, std::string &name);

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
                return BIO_NET_RETRY; // hcom错误码太多，SDK端IO遇到网络IO异常时需要进行重试处理
        }
    }

    template <typename TReq, typename TResp> BResult SyncCall(uint16_t opCode, TReq &req, TResp &resp, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = mTimeout;
        NetServiceOpInfo rspOpInfo{};
        NetServiceMessage respMsg(&resp, sizeof(TResp));
#ifndef DEBUG_UT
        auto result = ch->SyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, rspOpInfo, respMsg);
#else
        auto result = NetStub::SyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, rspOpInfo, respMsg);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", result " << NetErrStr(result));
            return NetResult(result);
        }

        if (NN_UNLIKELY(rspOpInfo.errorCode != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", error code " << rspOpInfo.errorCode);
            return rspOpInfo.errorCode;
        }

        return BIO_OK;
    }

    template <typename TResp>
    BResult SyncCallBuffInner(uint16_t opCode, void *req, uint32_t reqLen, TResp &resp, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = mTimeout;
        NetServiceOpInfo rspOpInfo{};
        NetServiceMessage respMsg(&resp, sizeof(TResp));
#ifndef DEBUG_UT
        auto result = ch->SyncCall(reqOpInfo, { static_cast<void *>(req), reqLen }, rspOpInfo, respMsg);
#else
        auto result = NetStub::SyncCall(reqOpInfo, { static_cast<void *>(req), reqLen }, rspOpInfo, respMsg);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", result " << NetErrStr(result));
            return NetResult(result);
        }

        if (NN_UNLIKELY(rspOpInfo.errorCode != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer resp with op " << opCode << ", error code " << rspOpInfo.errorCode);
            return rspOpInfo.errorCode;
        }

        return BIO_OK;
    }

    template <typename TReq, typename TResp>
    BResult SyncCall(uint16_t opCode, TReq &req, TResp **resp, uint64_t &respLen, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        BResult result = BIO_INNER_ERR;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = mTimeout;
        NetServiceOpInfo rspOpInfo{};
        NetServiceMessage respMsg{};

        LVOS_TP_START(SYNCCALL_FAIL, &result, BIO_ERR);
#ifndef DEBUG_UT
        result = ch->SyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, rspOpInfo, respMsg);
#else
        result = NetStub::SyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, rspOpInfo, respMsg);
#endif
        LVOS_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer unfixed-length resp with op " << opCode << ", result " <<
                NetErrStr(result));
            return NetResult(result);
        }

        if (NN_UNLIKELY(rspOpInfo.errorCode != BIO_OK)) {
            NET_LOG_ERROR("Failed to call peer unfixed-length resp with op " << opCode << ", error code " <<
                rspOpInfo.errorCode);
            return rspOpInfo.errorCode;
        }

        *resp = reinterpret_cast<TResp *>(respMsg.data);
        respLen = respMsg.size;
        return BIO_OK;
    }

    template <typename TReq> BResult AsyncCallWithoutResponse(uint16_t opCode, TReq &req, ChannelPtr &ch)
    {
        using namespace ock::hcom;
        int32_t result = BIO_ERR;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = mTimeout;

#ifndef DEBUG_UT
        auto *netCallback = NewCallback([](NetServiceContext &context) { return; }, std::placeholders::_1);
        result = ch->AsyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, netCallback);
#else
        CbFunc cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
            free(resp);
        };
        Callback cb = Callback(cbFunc, nullptr);
        result = NetStub::AsyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, cb);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << NetErrStr(result));
            return NetResult(result);
        }
        return BIO_OK;
    }

    inline void AsyncCallDone(int32_t result, uint64_t ts)
    {
        BIO_TRACE_ASYNC_END(NET_TRACE_ASYNC_CALL, result, ts);
    }

    template <typename TReq> void AsyncCall(uint16_t opCode, TReq &req, ChannelPtr &ch, Callback callback)
    {
        using namespace ock::hcom;
        int32_t result = BIO_ERR;
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = mTimeout;
        uint64_t ts = Monotonic::TimeNs();

        BIO_TRACE_ASYNC_BEGIN(NET_TRACE_ASYNC_CALL);
        LVOS_TP_START(SERVER_NET_ASYNC_CALL_FAIL, &result, BIO_NET_RETRY);
#ifndef DEBUG_UT
        auto *netCallback = NewCallback(
            [this, ts, callback](NetServiceContext &context) {
                if (context.Result() != SER_OK) {
                    AsyncCallDone(context.Result(), ts);
                    callback.cb(callback.cbCtx,
                                nullptr,
                                0,
                                NetResult(context.Result()));
                } else {
                    NetServiceOpInfo rspOpInfo = context.OpInfo();
                    AsyncCallDone(rspOpInfo.errorCode, ts);
                    callback.cb(callback.cbCtx,
                                context.MessageData(),
                                context.MessageDataLen(),
                                rspOpInfo.errorCode);
                }
            }, std::placeholders::_1);
        result = ch->AsyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, netCallback);
#else
        result = NetStub::AsyncCall(reqOpInfo, { static_cast<void *>(&req), sizeof(TReq) }, callback);
#endif
        LVOS_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << NetErrStr(result));
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
        NetServiceOpInfo reqOpInfo(opCode);
        reqOpInfo.timeout = mTimeout;
        uint64_t ts = Monotonic::TimeNs();

        BIO_TRACE_ASYNC_BEGIN(NET_TRACE_ASYNC_CALL_BUFF);
#ifndef DEBUG_UT
        auto *netCallback = NewCallback(
            [this, ts, callback](NetServiceContext &context) {
                if (context.Result() != SER_OK) {
                    AsyncCallBuffDone(context.Result(), ts);
                    callback.cb(callback.cbCtx,
                                nullptr,
                                0,
                                NetResult(context.Result()));
                } else {
                    NetServiceOpInfo rspOpInfo = context.OpInfo();
                    AsyncCallBuffDone(rspOpInfo.errorCode, ts);
                    callback.cb(callback.cbCtx,
                                context.MessageData(),
                                context.MessageDataLen(),
                                rspOpInfo.errorCode);
                }
        }, std::placeholders::_1);
        result = ch->AsyncCall(reqOpInfo, { req, reqLen }, netCallback);
#else
        result = NetStub::AsyncCall(reqOpInfo, { req, reqLen }, callback);
#endif
        if (UNLIKELY(result != BIO_OK)) {
            NET_LOG_ERROR("Failed async call with op " << opCode << ", result " << NetErrStr(result));
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
    MemoryRegionPtr mLocalMr = nullptr;
    NetBlockPoolPtr mMrBlockPool = nullptr;
    std::atomic<uint64_t> mUsedBlock;
    NewRequestHandler mHandlers[MAX_NEW_REQ_HANDLER]{};
    NetConnectorPtr mConnector = nullptr;
    DEFINE_REF_COUNT_VARIABLE
    uint16_t mLocalNodeId = UINT16_MAX;
    NewChannelHandler mHandleNewChannel = nullptr;
    ChannelBrokenHandler mHandlerBroken = nullptr;
    ock::hcom::NetService *mRpcService = nullptr;
    ock::hcom::NetService *mIpcService = nullptr;
    BioCryptorHelper *mbioCryptorHelper = nullptr;
    std::mutex mMutex;
    NetOptions mOptions;
    NetExecutorPoolPtr mRequestExecutor = nullptr;
    uint32_t reqExecutorNum;
    int32_t mShmFd = -1;
    uint64_t mShareOffset = 0;
    uint64_t mShmSize = 0;
    uint8_t *mShareAddress = nullptr;
    friend class NetConnectTask;
};

using NetEnginePtr = Ref<NetEngine>;
}
}
#endif // NET_ENGINE_H
