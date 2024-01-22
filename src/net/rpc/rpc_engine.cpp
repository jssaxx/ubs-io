/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include <arpa/inet.h>
#include <ifaddrs.h>
#include "rpc_engine.h"
#include "bio_log.h"
#include "bio_ip_util.h"
#include "net_executor_pool.h"

namespace ock {
namespace bio {
constexpr uint16_t WKR_GRP_INDEX_CTRL_CLIENT = 2L;
constexpr uint16_t WKR_GRP_INDEX_DATA_CLIENT = 3L;
constexpr uint16_t WKR_GRP_INDEX_CTRL_SERVER = 0L;
constexpr uint16_t WKR_GRP_INDEX_DATA_SERVER = 1L;

BResult RpcEngine::ValidateOptions()
{
    using namespace hcom;
    /* do later */

    /* validate all options */
    if (mOptions.protocol != NetServiceProtocol::TCP && mOptions.protocol != NetServiceProtocol::RDMA) {
        LOG_ERROR("Invalid protocol " << NetDriverProtocolToString(mOptions.protocol) <<
            " is set, only TCP and RDMA are supported");
        return BIO_INVALID_PARAM;
    }

    return BIO_OK;
}

BResult RpcEngine::Initialize()
{
    mName = "Bio-" + mOptions.name;

    mCtrlPanelTimeout = mOptions.timeoutCtrlSec;
    mDataPanelTimeout = mOptions.timeoutDataSec;

    /* create channel managers */
    mPeerCtrlChannels = MakeRef<RpcChannelMgr>(mOptions.name + "-ctrl");
    mPeerDataChannels = MakeRef<RpcChannelMgr>(mOptions.name + "-data");

    mAcceptedCtrlChannels = MakeRef<RpcChannelMgr>(mOptions.name + "-ctrl-accepted");
    mAcceptedDataChannels = MakeRef<RpcChannelMgr>(mOptions.name + "-data-accepted");
    if (mPeerCtrlChannels == nullptr || mPeerDataChannels == nullptr || mAcceptedCtrlChannels == nullptr ||
        mAcceptedDataChannels == nullptr) {
        LOG_ERROR("Failed to start Net Engine " << mOptions.name <<
            " as failed to create channel manager, probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    /* create and start connector */
    mAsyncConnector = MakeRef<RpcConnector>(this);
    if (mAsyncConnector == nullptr) {
        LOG_ERROR("Failed to start Net Engine " << mOptions.name <<
            " as failed to create connector, probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mRequestExecutor = MakeRef<NetExecutorPool>("BioSvr");
    if (mRequestExecutor == nullptr) {
        LOG_ERROR("Failed to start server event executor , probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    return BIO_OK;
}

BResult RpcEngine::Start(const BioNetOptions &opt)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        LOG_WARN("RpcEngine has been already started");
        return BIO_OK;
    }

    mOptions = opt;
    auto result = ValidateOptions();
    ASSERT_RETURN(result == BIO_OK, result);

    result = Initialize();
    if (result != BIO_OK) {
        StopInner();
        return result;
    }

    result = mAsyncConnector->Start(mOptions);
    if (result != BIO_OK) {
        StopInner();
        return result;
    }

    /* create hcom service */
    result = CreateService();
    if (result != BIO_OK) {
        StopInner();
        return result;
    }

    result = InitLocalMrAllocator();
    if (result != BIO_OK) {
        StopInner();
        return result;
    }

    result = mRequestExecutor->Start(mOptions.handleRequestThreadNum, mOptions.handleRequestQueueSize);
    if (result != BIO_OK) {
        StopInner();
        return result;
    }

    mStarted = true;
    LOG_INFO("Started Net Engine " << mOptions.name);
    return BIO_OK;
}

void RpcEngine::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    StopInner();
    mStarted = false;
}

void RpcEngine::StopInner()
{
    if (mRequestExecutor != nullptr) {
        mRequestExecutor->Stop();
        mRequestExecutor = nullptr;
    }

    if (mAsyncConnector != nullptr) {
        mAsyncConnector->Stop();
        mAsyncConnector = nullptr;
    }

    mPeerCtrlChannels = nullptr;
    mPeerDataChannels = nullptr;

    mAcceptedCtrlChannels = nullptr;
    mAcceptedDataChannels = nullptr;

    if (mMrBlockPool != nullptr) {
        mMrBlockPool->Stop();
        mMrBlockPool = nullptr;
    }

    if (mLocalMr != nullptr) {
        mService->DestroyMemoryRegion(mLocalMr);
        mLocalMr = nullptr;
    }

    /* stop service */
    if (mService != nullptr) {
        mService->Stop();
        mService = nullptr;
    }
}

BResult RpcEngine::InitLocalMrAllocator()
{
    using namespace ock::hcom;
    auto result = RegisterMemoryRegion(mOptions.localMrSize, mLocalMr);
    if (result != BIO_OK) {
        LOG_ERROR("Failed to register mr by size " << mOptions.localMrSize);
        return result;
    }

    mMrBlockPool = MakeRef<BioNetBlockPool>();
    ASSERT_RETURN(mMrBlockPool != nullptr, BIO_ALLOC_FAIL);

    result = mMrBlockPool->Start(mLocalMr->GetAddress(), mDataPageBytes, mOptions.localMrSize / mDataPageBytes);
    if (result != BIO_OK) {
        LOG_ERROR("Failed to start block pool " << mOptions.localMrSize);
        return result;
    }
    LOG_INFO("Succeed to start block pool " << mOptions.localMrSize);
    return BIO_OK;
}

std::string RpcEngine::GenerateWorkersSetting()
{
    std::ostringstream oss;
    /*
     * server side two groups: ctrl panel and data panel
     * client side two groups: ctrl panel and data panel
     * so, 4 groups in total
     *
     * server side groups */
    oss << std::to_string(mOptions.controlPanelHandlerCount) << "," << std::to_string(mOptions.dataPanelHandlerCount);
    /* client side groups */
    oss << "," << std::to_string(mOptions.controlPanelHandlerCount) << "," <<
        std::to_string(mOptions.dataPanelHandlerCount);
    return oss.str();
}

static void RpcEngineLog(int level, const char *msg)
{
    ASSERT_RET_VOID(msg != nullptr);
    Logger::gInstance->Log(level + 1U, msg);
}

BResult RpcEngine::CreateService()
{
    using namespace hcom;

    /* get cm listen conf */
    std::string ipMask = mOptions.ipMask;
    auto port = mOptions.port;

    LOG_INFO("Starting net service for drc_fs with ipMask " << ipMask << ", port " << port);
    if (mService != nullptr) {
        LOG_ERROR("Net service for drc_fs already created");
        return BIO_ERR;
    }

    // TODO: /* filter ip */
    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(ipMask, goodIps) || goodIps.empty()) {
        LOG_ERROR("Failed to find ip with ip mask " << ipMask);
        return BIO_ERR;
    }

    /* initialize out log of hcom before create hcom service */
    auto serviceLog = NetOutLogger::Instance();
    serviceLog->SetExternalLogFunction(RpcEngineLog);

    /* create service */
    mService = NetService::Instance(mOptions.protocol, mName, true);
    if (mService == nullptr) {
        LOG_ERROR("Failed to create service as already created");
        return BIO_ERR;
    }

    NetServiceOptions options{};
    options.mode =
        mOptions.isBusyLoop ? NetDriverWorkingMode::NET_BUSY_POLLING : NetDriverWorkingMode::NET_EVENT_POLLING;
    options.qpSendQueueSize = 64;
    options.pollingBatchSize = NO_16;
    options.mrSendReceiveSegSize = NO_4096 + NO_1024; /* 1024 for header */
    options.mrSendReceiveSegCount = NO_1024;
    options.prePostReceiveSizePerQP = 32;
    options.heartBeatIdleTime = 5;
    options.heartBeatProbeTimes = 1;
    options.heartBeatProbeInterval = 1;

    options.SetNetDeviceIpMask(ipMask);
    options.SetWorkerGroups(GenerateWorkersSetting());

    options.oobType = NET_OOB_TCP;
    NetServiceOobListenerOptions listenOpt;
    listenOpt.Set(goodIps.at(0), port);
    mService->AddListener(listenOpt);

    mService->RegisterNewChannelHandler(
        std::bind(&RpcEngine::NewChannel, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    mService->RegisterChannelBrokenHandler(std::bind(&RpcEngine::ChannelBroken, this, std::placeholders::_1),
        ock::hcom::BROKEN_ALL);
    mService->RegisterOpReceiveHandler(0, std::bind(&RpcEngine::RequestReceived, this, std::placeholders::_1));
    mService->RegisterOpSentHandler(0, std::bind(&RpcEngine::RequestPosted, this, std::placeholders::_1));
    mService->RegisterOpOneSideHandler(0, std::bind(&RpcEngine::OneSideDone, this, std::placeholders::_1));

    int32_t result = 0;
    if ((result = mService->Start(options)) != 0) {
        LOG_ERROR("Failed to start service result " << NetErrStr(result));
        return BIO_ERR;
    }

    LOG_INFO("Net server for file system started");

    return BIO_OK;
}

int32_t RpcEngine::NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload)
{
    LOG_INFO("New drc_fs peer connected from " << ipPort << " payload " << payload);

    /* call to upper server */
    NewChannelResp resp;
    if (mHandleNewChannel != nullptr) {
        mHandleNewChannel(newChannel, ipPort, resp);
    }

    if (resp.result != 0) {
        LOG_WARN("Fs peer connection from " << ipPort << " has been refused");
        return BIO_ERR;
    }

    BioNetConnPayload netPayload;
    bool isCtrl = true;
    if (netPayload.FromPayloadStr(payload, isCtrl) != BIO_OK) {
        LOG_ERROR("Failed to from payload");
        return BIO_ERR;
    }

    BioNodeId nodeId(netPayload.srcNodeId);
    if (isCtrl) {
        mAcceptedCtrlChannels->AddChannel(nodeId, const_cast<ChannelPtr &>(newChannel));
    } else {
        mAcceptedDataChannels->AddChannel(nodeId, const_cast<ChannelPtr &>(newChannel));
    }

    BioNetChannelUpCtx ctx(nodeId, isCtrl, true);
    newChannel->UpCtx(ctx.whole);

    return BIO_OK;
}

void RpcEngine::ChannelBroken(const ChannelPtr &ch)
{
    BioNetChannelUpCtx ctx(ch->UpCtx());
    ChannelPtr tmpCh = nullptr;
    BioNodeId nodeId(ctx.peerId);
    LOG_WARN("Net Engine channel " << ch->Id() << " broken, vnode id " << nodeId << ", " <<
        (ctx.IsCtrlPanel() ? "ctrl" : "data") << " plane");
    if (ctx.AcceptedChannel()) {
        if (ctx.IsCtrlPanel()) {
            mAcceptedCtrlChannels->RemoveChannel(nodeId, tmpCh);
        } else {
            mAcceptedDataChannels->RemoveChannel(nodeId, tmpCh);
        }
    } else {
        if (ctx.IsCtrlPanel()) {
            mPeerCtrlChannels->RemoveChannel(nodeId, tmpCh);
        } else {
            mPeerDataChannels->RemoveChannel(nodeId, tmpCh);
        }

        if (mHandlerBroken != nullptr && mPeerCtrlChannels->GetChannel(nodeId, tmpCh) == BIO_NOT_EXISTS &&
            mPeerDataChannels->GetChannel(nodeId, tmpCh) == BIO_NOT_EXISTS) {
            mHandlerBroken(ctx.peerId);
        }
    }
}

int32_t RpcEngine::RequestReceived(ServiceContext &ctx)
{
    ASSERT_RETURN(mRequestExecutor != nullptr, BIO_NOT_READY);

    if (UNLIKELY(ctx.OpCode() >= MAX_NEW_REQ_HANDLER)) {
        LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode());
        return BIO_ERR;
    }

    auto &handler = mHandlers[ctx.OpCode()];
    if (UNLIKELY(handler == nullptr)) {
        LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode() << " as no handler registered");
        return BIO_ERR;
    }

    return mRequestExecutor->AddTask(handler, ctx);
}

int32_t RpcEngine::RequestPosted(const ServiceContext &ctx)
{
    return BIO_OK;
}

int32_t RpcEngine::OneSideDone(const ServiceContext &ctx)
{
    LOG_WARN("Not reachable path");
    return BIO_OK;
}

BResult RpcEngine::ConnectToPeer(ConnectInfo &info, bool isCtrlPanel, ChannelPtr &ch)
{
    using namespace hcom;
    ASSERT_RETURN(mService != nullptr, BIO_ERR);

    /* get connecting options ready */
    NetServiceConnectOptions options{};
    std::string prefix;
    if (isCtrlPanel) {
        options.epSize = mOptions.controlPanelConnCount;
        options.clientGrpNo = WKR_GRP_INDEX_CTRL_CLIENT;
        options.serverGrpNo = WKR_GRP_INDEX_CTRL_SERVER;
        prefix = CONN_PAYLOAD_PREFIX_CTRL;
    } else {
        options.epSize = mOptions.dataPanelConnCount;
        options.clientGrpNo = WKR_GRP_INDEX_DATA_CLIENT;
        options.serverGrpNo = WKR_GRP_INDEX_DATA_SERVER;
        prefix = CONN_PAYLOAD_PREFIX_DATA;
    }

    BioNetConnPayload payload(mMyNodeId, info.peerId);
    LOG_INFO("Connect to peer, src nodeId:" << mMyNodeId << ", dst nodeId:" << info.peerId << ".");
    int32_t result = 0;
    for (uint16_t i = 0; i < info.retryTimes; ++i) {
        if ((result = mService->Connect(info.ip, info.port, payload.ToPayloadStr(prefix), ch, options)) == 0) {
            LOG_INFO("Connected to " << info.ip << ":" << info.port << " successfully in net engine " <<
                (isCtrlPanel ? " for ctrl panel " : " for data panel"));
            break;
        }
    }

    if (result != 0) {
        LOG_INFO("Failed to connect to " << info.ip << ":" << info.port << " in net engine " <<
            (isCtrlPanel ? " for ctrl panel " : " for data panel, result ") << NetErrStr(result));
        return result;
    }

    BioNetChannelUpCtx ctx(info.peerId, isCtrlPanel, false);
    ch->UpCtx(ctx.whole);
    ch->SetOneSideTimeout(mOptions.timeoutDataSec);
    ch->SetTwoSideTimeout(mOptions.timeoutCtrlSec);

    return BIO_OK;
}
}
}