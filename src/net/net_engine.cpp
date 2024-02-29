/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include <iostream>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <climits>

#include "bio_ip_util.h"
#include "net_log.h"
#include "net_executor_pool.h"
#include "net_engine.h"

namespace ock {
namespace bio {
using namespace ock::hcom;

constexpr uint16_t WKR_GRP_INDEX_CTRL_SERVER = 0L;
constexpr uint16_t WKR_GRP_INDEX_DATA_SERVER = 1L;
constexpr uint16_t WKR_GRP_INDEX_CTRL_CLIENT = 2L;
constexpr uint16_t WKR_GRP_INDEX_DATA_CLIENT = 3L;

BResult NetEngine::Initialize(int16_t timeoutSec, uint32_t coreThreadNum, uint32_t queueSize, NetLogFunc func)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        NET_LOG_WARN("Net engine has been already initialized.");
        return BIO_OK;
    }

    NetLog::Instance()->SetLogFuncFunc(func);
    mTimeout = timeoutSec;

    mChannelMgr = MakeRef<NetChannelMgr>();
    if (mChannelMgr == nullptr) {
        NET_LOG_ERROR("Make channel manager failed.");
        return BIO_ALLOC_FAIL;
    }

    mConnector = MakeRef<NetConnector>(this);
    if (mConnector == nullptr) {
        NET_LOG_ERROR("Make net connector failed.");
        return BIO_ALLOC_FAIL;
    }
    BResult ret = mConnector->Start();
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to start net connector, ret:" << ret << ".");
        return ret;
    }

    mRequestExecutor = MakeRef<NetExecutorPool>("NetExecutor");
    if (mRequestExecutor == nullptr) {
        NET_LOG_ERROR("Make net request executor failed.");
        return BIO_ALLOC_FAIL;
    }
    ret = mRequestExecutor->Start(coreThreadNum, queueSize);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to start request executor, ret:" << ret << ".");
        return ret;
    }

    auto serviceLog = NetOutLogger::Instance();
    serviceLog->SetExternalLogFunction(func);
    mStarted = true;
    return BIO_OK;
}

BResult NetEngine::Start(const NetOptions &opt)
{
    if (opt.protocol == ServiceProtocol::SHM || opt.protocol == ServiceProtocol::UDS) {
        int32_t result = StartIpcService(opt);
        if (result != BIO_OK) {
            return result;
        }
    }
    if (opt.protocol == ServiceProtocol::TCP || opt.protocol == ServiceProtocol::RDMA) {
        int32_t result = StartRpcService(opt);
        if (result != BIO_OK) {
            return result;
        }
    }
    return BIO_OK;
}

void NetEngine::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    StopInner();
    mStarted = false;
}

void NetEngine::StopInner()
{
    if (mRequestExecutor != nullptr) {
        mRequestExecutor->Stop();
        mRequestExecutor = nullptr;
    }

    if (mConnector != nullptr) {
        mConnector->Stop();
        mConnector = nullptr;
    }

    mChannelMgr = nullptr;

    if (mMrBlockPool != nullptr) {
        mMrBlockPool->Stop();
        mMrBlockPool = nullptr;
    }

    if (mLocalMr != nullptr) {
        mRpcService->DestroyMemoryRegion(mLocalMr);
        mLocalMr = nullptr;
    }

    if (mRpcService != nullptr) {
        mRpcService->Stop();
        mRpcService = nullptr;
    }

    if (mIpcService != nullptr) {
        mIpcService->Stop();
        mIpcService = nullptr;
    }
}

BResult NetEngine::InitLocalMrAllocator()
{
    auto result = RegisterMemoryRegion(mOptions.localMrSize, mLocalMr);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to register mr by size " << mOptions.localMrSize);
        return result;
    }

    mMrBlockPool = MakeRef<NetBlockPool>();
    if (mMrBlockPool == nullptr) {
        NET_LOG_ERROR("Make block pool ptr failed.");
        return BIO_ALLOC_FAIL;
    }
    result = mMrBlockPool->Start(mLocalMr->GetAddress(), mDataPageBytes, mOptions.localMrSize / mDataPageBytes);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start block pool " << mOptions.localMrSize << ".");
    } else {
        NET_LOG_INFO("Succeed to start block pool " << mOptions.localMrSize << ".");
    }
    return result;
}

std::string NetEngine::GenerateWorkersSetting()
{
    std::ostringstream oss;
    oss << std::to_string(mOptions.handlerCount);
    return oss.str();
}

void NetEngine::AssignIpcServiceOptions(bool isOobSvr, ock::hcom::NetServiceOptions &options)
{
    options.mode = NetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = IPC_MAX_MESSAGE_SIZE + MAX_MESSAGE_HEAD_SIZE;
    options.mrSendReceiveSegCount = NO_512;
    options.heartBeatIdleTime = NO_5;
    options.heartBeatProbeInterval = NO_1;
    options.qpSendQueueSize = NO_64;
    options.pollingBatchSize = NO_16;
    options.prePostReceiveSizePerQP = NO_32;
    options.oobType = NET_OOB_UDS;
    options.SetWorkerGroups("1");
    const static int DEFAULT_THREAD_PRIORITY = -20;
    options.workerThreadPriority = DEFAULT_THREAD_PRIORITY;
    if (isOobSvr) {
        NetServiceOobUDSListenerOptions listenOpt;
        listenOpt.Name(UDS_NAME);
        listenOpt.perm = 0;
        mIpcService->AddOobUdsOptions(listenOpt);
        options.SetWorkerGroups("4");
        mIpcService->RegisterNewChannelHandler(std::bind(&NetEngine::NewChannel, this, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3));
    }
    mIpcService->RegisterChannelBrokenHandler(std::bind(&NetEngine::ChannelBroken, this, std::placeholders::_1),
        BROKEN_ALL);
    mIpcService->RegisterOpReceiveHandler(0, std::bind(&NetEngine::RequestReceived, this, std::placeholders::_1));
    mIpcService->RegisterOpSentHandler(0, std::bind(&NetEngine::RequestPosted, this, std::placeholders::_1));
    mIpcService->RegisterOpOneSideHandler(0, std::bind(&NetEngine::OneSideDone, this, std::placeholders::_1));
}

BResult NetEngine::StartIpcService(const NetOptions &opt)
{
    if (mIpcService != nullptr) {
        NET_LOG_INFO("Net ipc service has already created.");
        return BIO_OK;
    }
    bool isOobSvr = opt.role != NET_CLIENT;
    mIpcService = NetService::Instance(opt.protocol, "BIO_IPC", isOobSvr);
    if (mIpcService == nullptr) {
        NET_LOG_ERROR("Failed to create ipc service instance, protocol:" << opt.protocol << ".");
        return BIO_ERR;
    }

    NetServiceOptions options{};
    AssignIpcServiceOptions(isOobSvr, options);
    auto result = mIpcService->Start(options);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start ipc service, result:" << NetErrStr(result) << ".");
        return BIO_ERR;
    }

    NET_LOG_INFO("Bio server Start ipc service success, protocol:" << opt.protocol << ".");
    return BIO_OK;
}

BResult NetEngine::AssignRpcServiceOptions(bool isOobSvr, NetServiceOptions &options)
{
    std::string ipMask = mOptions.ipMask;
    auto port = mOptions.port;
    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(ipMask, goodIps) || goodIps.empty()) {
        NET_LOG_ERROR("Failed to find ip with ip mask " << ipMask);
        return BIO_ERR;
    }

    options.mode =
        mOptions.isBusyLoop ? NetDriverWorkingMode::NET_BUSY_POLLING : NetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = MAX_MESSAGE_SIZE + MAX_MESSAGE_HEAD_SIZE;
    options.mrSendReceiveSegCount = NO_1024;
    options.qpSendQueueSize = NO_4096;
    options.qpReceiveQueueSize = NO_2048;
    options.prePostReceiveSizePerQP = NO_1024;
    options.pollingBatchSize = NO_1024;
    options.maxPostSendCountPerQP = NO_1024;
    options.dontStartWorkers = false;
    options.completionQueueDepth = NO_2048;
    options.heartBeatIdleTime = NO_5;
    options.heartBeatProbeTimes = NO_1;
    options.heartBeatProbeInterval = NO_1;
    options.SetNetDeviceIpMask(ipMask);
    options.SetWorkerGroups(GenerateWorkersSetting());
    if (isOobSvr) {
        options.oobType = NET_OOB_TCP;
        NetServiceOobListenerOptions listenOpt;
        listenOpt.Set(goodIps.at(0), port);
        mRpcService->AddListener(listenOpt);
        mRpcService->RegisterNewChannelHandler(std::bind(&NetEngine::NewChannel, this, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3));
    }
    mRpcService->RegisterChannelBrokenHandler(std::bind(&NetEngine::ChannelBroken, this, std::placeholders::_1),
        BROKEN_ALL);
    mRpcService->RegisterOpReceiveHandler(0, std::bind(&NetEngine::RequestReceived, this, std::placeholders::_1));
    mRpcService->RegisterOpSentHandler(0, std::bind(&NetEngine::RequestPosted, this, std::placeholders::_1));
    mRpcService->RegisterOpOneSideHandler(0, std::bind(&NetEngine::OneSideDone, this, std::placeholders::_1));
    return BIO_OK;
}

BResult NetEngine::StartRpcService(const NetOptions &opt)
{
    if (mRpcService != nullptr) {
        NET_LOG_INFO("Net rpc service has already created.");
        return BIO_OK;
    }
    mOptions = opt;
    bool isOobSvr = opt.role != NET_CLIENT;
    mRpcService = NetService::Instance(opt.protocol, "BIO_RPC", isOobSvr);
    if (mRpcService == nullptr) {
        NET_LOG_ERROR("Failed to create rpc service instance, protocol:" << opt.protocol << ".");
        return BIO_ERR;
    }

    NetServiceOptions options{};
    auto result = AssignRpcServiceOptions(isOobSvr, options);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to assign rpc service options, result:" << NetErrStr(result) << ".");
        return BIO_ERR;
    }
    result = mRpcService->Start(options);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start rpc service, result:" << NetErrStr(result) << ".");
        return BIO_ERR;
    }

    result = InitLocalMrAllocator();
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to init mr allocator, result:" << NetErrStr(result) << ".");
        return BIO_ERR;
    }

    NET_LOG_INFO("Bio server start rpc service success, protocol:" << opt.protocol << ".");
    return BIO_OK;
}

int32_t NetEngine::NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload)
{
    NET_LOG_INFO("Receive new peer connected from " << ipPort << ", payload " << payload);

    NewChannelResp resp;
    if (mHandleNewChannel != nullptr) {
        mHandleNewChannel(newChannel, ipPort, resp);
    }
    if (resp.result != 0) {
        NET_LOG_WARN("Peer connection from " << ipPort << " has been refused");
        return BIO_ERR;
    }

    NetConnPayload netPayload;
    if (netPayload.FromPayloadStr(payload) != BIO_OK) {
        NET_LOG_ERROR("Failed to parse payload:" << payload << ".");
        return BIO_ERR;
    }

    mChannelMgr->AddChannelNode(netPayload.srcNodeId, const_cast<ChannelPtr &>(newChannel));
    NetChannelUpCtx ctx(netPayload.srcNodeId, true);
    newChannel->UpCtx(ctx.whole);
    return BIO_OK;
}

void NetEngine::ChannelBroken(const ChannelPtr &ch)
{
    NetChannelUpCtx ctx(ch->UpCtx());
    NET_LOG_WARN("Net Engine channel " << ch->Id() << " broken, node id " << ctx.peerId << ".");

    if (ctx.AcceptedChannel()) {
        ChannelNode chNode;
        mChannelMgr->RemoveChannelNode(ch->Id(), chNode);
        if (mHandlerBroken != nullptr) {
            mHandlerBroken(chNode.id);
        }
    } else {
        mChannelMgr->RemoveChannel(ctx.peerId, ch);
    }
}

int32_t NetEngine::RequestReceived(ServiceContext &ctx)
{
    if (UNLIKELY(mRequestExecutor == nullptr)) {
        NET_LOG_ERROR("Net request executor not ready.");
        return BIO_NOT_READY;
    }
    if (UNLIKELY(ctx.OpCode() >= MAX_NEW_REQ_HANDLER)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode());
        return BIO_ERR;
    }
    auto &handler = mHandlers[ctx.OpCode()];
    if (UNLIKELY(handler == nullptr)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode() <<
            " as no handler registered");
        return BIO_ERR;
    }
    return mRequestExecutor->AddTask(handler, ctx);
}

int32_t NetEngine::RequestPosted(const ServiceContext &ctx)
{
    return BIO_OK;
}

int32_t NetEngine::OneSideDone(const ServiceContext &ctx)
{
    return BIO_OK;
}

BResult NetEngine::ConnectToPeer(ConnectMode mode, ConnectInfo &info, ChannelPtr &ch)
{
    NetService *netService = (mode == CONNECT_IPC) ? mIpcService : mRpcService;
    if (netService == nullptr) {
        NET_LOG_ERROR("Net service not ready.");
        return BIO_ERR;
    }

    NetServiceConnectOptions options{};
    std::string prefix;
    options.epSize = mOptions.connCount;
    prefix = CONN_PAYLOAD_PREFIX_DATA;
    int32_t result = 0;
    for (uint16_t i = 0; i < info.retryTimes; ++i) {
        if (mode == CONNECT_IPC) {
            NetConnPayload payload(static_cast<uint32_t>(getpid()), info.peerId);
            result = netService->Connect(UDS_NAME, 0, payload.ToPayloadStr(prefix), ch, options);
        } else {
            NetConnPayload payload(mLocalNodeId, info.peerId);
            result = netService->Connect(info.ip, info.port, payload.ToPayloadStr(prefix), ch, options);
        }
        if (result == 0) {
            NET_LOG_INFO("Connect to peer success, ip:" << info.ip << ", port:" << info.port << ", pid:" << getpid() <<
                ".");
            break;
        }
    }
    if (result != 0) {
        NET_LOG_ERROR("Connect to peer failed, ret:" << NetErrStr(result) << ", ip:" << info.ip << ", port:" <<
            info.port << ", pid:" << getpid() << ".");
        return result;
    }

    NetChannelUpCtx ctx(info.peerId, false);
    ch->UpCtx(ctx.whole);
    ch->SetOneSideTimeout(mTimeout);
    ch->SetTwoSideTimeout(mTimeout);
    return BIO_OK;
}
}
}