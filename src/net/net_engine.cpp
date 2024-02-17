/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <climits>

#include "bio_log.h"
#include "bio_ip_util.h"
#include "net_executor_pool.h"
#include "net_engine.h"

namespace ock {
namespace bio {
using namespace ock::hcom;

constexpr uint16_t WKR_GRP_INDEX_CTRL_SERVER = 0L;
constexpr uint16_t WKR_GRP_INDEX_DATA_SERVER = 1L;
constexpr uint16_t WKR_GRP_INDEX_CTRL_CLIENT = 2L;
constexpr uint16_t WKR_GRP_INDEX_DATA_CLIENT = 3L;

BResult NetEngine::InitializeBase()
{
    mName = "Bio-" + mOptions.name;

    mCtrlPanelTimeout = mOptions.timeoutCtrlSec;
    mDataPanelTimeout = mOptions.timeoutDataSec;

    // 1. create channel manager
    mCtrlChannels = MakeRef<NetChannelMgr>(mOptions.name + "-ctrl");
    mDataChannels = MakeRef<NetChannelMgr>(mOptions.name + "-data");
    if (mCtrlChannels == nullptr || mDataChannels == nullptr) {
        LOG_ERROR("Failed to start Net Engine " << mOptions.name << " as failed to create channel manager.");
        return BIO_ALLOC_FAIL;
    }

    // 2. create and start connector
    mConnector = MakeRef<NetConnector>(this);
    ChkTrue(mConnector != nullptr, BIO_ALLOC_FAIL, "Make net connector failed.");
    BResult ret = mConnector->Start(mOptions);
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to start net connector, name: " << mOptions.name << " ret:" << ret << ".");
        return ret;
    }

    // 3. create and start request executor
    mRequestExecutor = MakeRef<NetExecutorPool>("NetExecutor");
    ChkTrue(mRequestExecutor != nullptr, BIO_ALLOC_FAIL, "Make net request executor failed.");
    ret = mRequestExecutor->Start(mOptions.handleRequestThreadNum, mOptions.handleRequestQueueSize);
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to start request executor, name: " << mOptions.name << " ret:" << ret << ".");
    }
    return ret;
}

BResult NetEngine::Start(const NetOptions &opt)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        LOG_WARN("Net engine has been already started.");
        return BIO_OK;
    }
    mOptions = opt;

    BResult result = InitializeBase();
    if (result != BIO_OK) {
        StopInner();
        return result;
    }

    result = CreateNetService();
    if (result != BIO_OK) {
        StopInner();
        return result;
    }

    result = InitLocalMrAllocator();
    if (result != BIO_OK) {
        StopInner();
        return result;
    }

    mStarted = true;
    LOG_INFO("Started net engine name " << mOptions.name << "success.");
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

    mCtrlChannels = nullptr;
    mDataChannels = nullptr;

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
        LOG_ERROR("Failed to register mr by size " << mOptions.localMrSize);
        return result;
    }

    mMrBlockPool = MakeRef<NetBlockPool>();
    ChkTrue(mMrBlockPool != nullptr, BIO_ALLOC_FAIL, "Make block pool ptr failed.");
    result = mMrBlockPool->Start(mLocalMr->GetAddress(), mDataPageBytes, mOptions.localMrSize / mDataPageBytes);
    if (result != BIO_OK) {
        LOG_ERROR("Failed to start block pool " << mOptions.localMrSize << ".");
    } else {
        LOG_INFO("Succeed to start block pool " << mOptions.localMrSize << ".");
    }
    return result;
}

std::string NetEngine::GenerateWorkersSetting()
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
    ChkTrueEx(msg != nullptr, "Invalid message ptr");
    Logger::gInstance->Log(level + 1U, msg);
}

BResult NetEngine::CreateSocketPath(std::string &sockPath)
{
    std::string linkedPath = "/proc/" + std::to_string(getpid()) + "/exe";
    char realPath[PATH_MAX];
    auto size = readlink(linkedPath.c_str(), realPath, sizeof(realPath));
    if (size < 0) {
        LOG_ERROR("Get local proc path failed, linkedPath:" << linkedPath << ".");
        return BIO_ERR;
    }
    realPath[size] = '\0';
    std::string path{ realPath };
    sockPath = std::move(path.substr(0, path.find_last_of('/')));

    std::string::size_type position = sockPath.find_last_of('/');
    if (position == std::string::npos) {
        LOG_ERROR("Get service install path:" << sockPath.c_str() << " failed : invalid folder path.");
        return BIO_ERR;
    }

    sockPath = std::move(sockPath.substr(0, sockPath.find_last_of('/')));
    sockPath.append(SOCKET_PATH_SUFFIX);
    return BIO_OK;
}

void NetEngine::AssignIpcServiceOptions(bool isOobSvr, ock::hcom::NetServiceOptions &options)
{
    options.mode = NetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = MAX_MESSAGE_SIZE + MAX_MESSAGE_HEAD_SIZE;
    options.mrSendReceiveSegCount = NO_1024;
    options.heartBeatIdleTime = NO_5;
    options.heartBeatProbeInterval = NO_1;
    options.qpSendQueueSize = NO_64;
    options.pollingBatchSize = NO_16;
    options.prePostReceiveSizePerQP = NO_32;
    options.oobType = NET_OOB_UDS;
    options.SetWorkerGroups("NO_1");
    const static int DEFAULT_THREAD_PRIORITY = -20;
    options.workerThreadPriority = DEFAULT_THREAD_PRIORITY;
    if (isOobSvr) {
        NetServiceOobUDSListenerOptions listenOpt;
        listenOpt.Name(socketPath);
        listenOpt.isCheck = false;
        mIpcService->AddOobUdsOptions(listenOpt);
        options.SetWorkerGroups("NO_4");
        mIpcService->RegisterNewChannelHandler(std::bind(&NetEngine::NewChannel, this, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3));
    }
    mIpcService->RegisterChannelBrokenHandler(std::bind(&NetEngine::ChannelBroken, this, std::placeholders::_1),
        BROKEN_ALL);
    mIpcService->RegisterOpReceiveHandler(0, std::bind(&NetEngine::RequestReceived, this, std::placeholders::_1));
    mIpcService->RegisterOpSentHandler(0, std::bind(&NetEngine::RequestPosted, this, std::placeholders::_1));
    mIpcService->RegisterOpOneSideHandler(0, std::bind(&NetEngine::OneSideDone, this, std::placeholders::_1));
}

BResult NetEngine::CreateIpcService()
{
    ChkTrue(mIpcService == nullptr, BIO_OK, "Net ipc service has already created.");

    bool isOobSvr = mOptions.ipcRole != NET_CLIENT;
    mIpcService = NetService::Instance(mOptions.ipcProtocol, "ipcServer", isOobSvr);
    ChkTrue(mIpcService != nullptr, BIO_ERR,
        "Failed to create ipc service instance, protocol:" << mOptions.ipcProtocol << ".");

    if (CreateSocketPath(socketPath) != BIO_OK) {
        return BIO_ERR;
    }

    NetServiceOptions options{};
    AssignIpcServiceOptions(isOobSvr, options);

    auto result = mIpcService->Start(options);
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Failed to start ipc service, result:" << NetErrStr(result) << ".");
        return BIO_ERR;
    }

    LOG_INFO("Create ipc service success, name:" << socketPath << ".");
    return BIO_OK;
}

BResult NetEngine::AssignRpcServiceOptions(bool isOobSvr, NetServiceOptions &options)
{
    std::string ipMask = mOptions.ipMask;
    auto port = mOptions.port;
    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(ipMask, goodIps) || goodIps.empty()) {
        LOG_ERROR("Failed to find ip with ip mask " << ipMask);
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

BResult NetEngine::CreateRpcService()
{
    ChkTrue(mRpcService == nullptr, BIO_OK, "Net rpc service has already created.");

    bool isOobSvr = mOptions.rpcRole != NET_CLIENT;
    mRpcService = NetService::Instance(mOptions.rpcProtocol, mName, isOobSvr);
    ChkTrue(mRpcService != nullptr, BIO_ERR,
        "Failed to create rpc service instance, protocol:" << mOptions.rpcProtocol << ".");

    NetServiceOptions options{};
    auto result = AssignRpcServiceOptions(isOobSvr, options);
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Failed to assign rpc service options, result:" << NetErrStr(result) << ".");
        return BIO_ERR;
    }

    result = mRpcService->Start(options);
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Failed to start rpc service, result:" << NetErrStr(result) << ".");
        return BIO_ERR;
    }

    LOG_INFO("Create rpc service success.");
    return BIO_OK;
}

BResult NetEngine::CreateNetService()
{
    auto serviceLog = NetOutLogger::Instance();
    serviceLog->SetExternalLogFunction(RpcEngineLog);

    if (mOptions.ipcProtocol == ServiceProtocol::SHM || mOptions.ipcProtocol == ServiceProtocol::UDS) {
        int32_t result = CreateIpcService();
        if (result != BIO_OK) {
            return result;
        }
    }

    if (mOptions.rpcProtocol == ServiceProtocol::TCP || mOptions.rpcProtocol == ServiceProtocol::RDMA) {
        int32_t result = CreateRpcService();
        if (result != BIO_OK) {
            return result;
        }
    }

    return BIO_OK;
}

int32_t NetEngine::NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload)
{
    LOG_INFO("Receive new peer connected from " << ipPort << ", payload " << payload);

    NewChannelResp resp;
    if (mHandleNewChannel != nullptr) {
        mHandleNewChannel(newChannel, ipPort, resp);
    }
    if (resp.result != 0) {
        LOG_WARN("Peer connection from " << ipPort << " has been refused");
        return BIO_ERR;
    }

    NetConnPayload netPayload;
    bool isCtrl = true;
    if (netPayload.FromPayloadStr(payload, isCtrl) != BIO_OK) {
        LOG_ERROR("Failed to from payload");
        return BIO_ERR;
    }

    if (isCtrl) {
        mCtrlChannels->AddAcceptChannel(const_cast<ChannelPtr &>(newChannel), netPayload.srcNodeId, netPayload.srcPid);
    } else {
        mDataChannels->AddAcceptChannel(const_cast<ChannelPtr &>(newChannel), netPayload.srcNodeId, netPayload.srcPid);
    }
    NetChannelUpCtx ctx(netPayload.srcNodeId, isCtrl, true);
    newChannel->UpCtx(ctx.whole);

    return BIO_OK;
}

void NetEngine::ChannelBroken(const ChannelPtr &ch)
{
    NetChannelUpCtx ctx(ch->UpCtx());
    LOG_WARN("Net Engine channel " << ch->Id() << " broken, node id " << ctx.peerId << ", " <<
        (ctx.IsCtrlPanel() ? "ctrl" : "data") << " plane");

    if (ctx.AcceptedChannel()) {
        ChannelInfo chInfo;
        if (ctx.IsCtrlPanel()) {
            mCtrlChannels->RemoveAcceptChannel(ch->Id(), chInfo);
        } else {
            mDataChannels->RemoveAcceptChannel(ch->Id(), chInfo);
        }
        if (mHandlerBroken != nullptr) {
            mHandlerBroken(chInfo.nodeId, chInfo.pid);
        }
    } else {
        ChannelPtr tmpCh = nullptr;
        if (ctx.IsCtrlPanel()) {
            mCtrlChannels->RemoveChannel(ctx.peerId, tmpCh);
        } else {
            mDataChannels->RemoveChannel(ctx.peerId, tmpCh);
        }
    }
}

int32_t NetEngine::RequestReceived(ServiceContext &ctx)
{
    ChkTrue(mRequestExecutor != nullptr, BIO_NOT_READY, "Net request executor not ready.");

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

int32_t NetEngine::RequestPosted(const ServiceContext &ctx)
{
    return BIO_OK;
}

int32_t NetEngine::OneSideDone(const ServiceContext &ctx)
{
    LOG_WARN("Not reachable path");
    return BIO_OK;
}

BResult NetEngine::ConnectToPeer(ConnectMode mode, ConnectInfo &info, bool isCtrlPanel, ChannelPtr &ch)
{
    NetService *netService = (mode == CONNECT_IPC) ? mIpcService : mRpcService;
    ChkTrue(netService != nullptr, BIO_ERR, "Net service not ready.");

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

    int32_t result = 0;
    NetConnPayload payload(mLocalNodeId, getpid(), info.peerId);
    for (uint16_t i = 0; i < info.retryTimes; ++i) {
        if (mode == CONNECT_IPC) {
            if ((result = netService->Connect(SOCKET_FULL_PATH, 0, payload.ToPayloadStr(prefix), ch, options)) == 0) {
                LOG_INFO("Ipc connect success, ip:" << info.ip << ", port:" << info.port << ", pid:" << getpid() <<
                    ", panel:" << (isCtrlPanel ? " ctrl " : "data") << ".");
                break;
            }
        } else {
            if ((result = netService->Connect(info.ip, info.port, payload.ToPayloadStr(prefix), ch, options)) == 0) {
                LOG_INFO("Rpc connect success, ip:" << info.ip << ", port:" << info.port << ", pid:" << getpid() <<
                    ", panel:" << (isCtrlPanel ? " ctrl " : "data") << ".");
                break;
            }
        }
    }
    if (result != 0) {
        LOG_ERROR("Failed to connect to " << info.ip << ":" << info.port << " in net engine " <<
            (isCtrlPanel ? " for ctrl panel " : " for data panel, result ") << NetErrStr(result));
        return result;
    }

    NetChannelUpCtx ctx(info.peerId, isCtrlPanel, false);
    ch->UpCtx(ctx.whole);
    ch->SetOneSideTimeout(mOptions.timeoutDataSec);
    ch->SetTwoSideTimeout(mOptions.timeoutCtrlSec);
    return BIO_OK;
}
}
}