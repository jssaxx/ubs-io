/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include <iostream>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <climits>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <sys/syscall.h>
#include <linux/version.h>

#include "securec.h"
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

    mCtrlChannelMgr = MakeRef<NetChannelMgr>();
    if (mCtrlChannelMgr == nullptr) {
        NET_LOG_ERROR("Make ctrl channel manager failed.");
        return BIO_ALLOC_FAIL;
    }

    mDataChannelMgr = MakeRef<NetChannelMgr>();
    if (mDataChannelMgr == nullptr) {
        NET_LOG_ERROR("Make data channel manager failed.");
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

    reqExecutorNum = coreThreadNum;
    mRequestExecutor = MakeRef<NetExecutorPool>("NetExecutor");
    if (mRequestExecutor == nullptr) {
        NET_LOG_ERROR("Make net request executor failed.");
        return BIO_ALLOC_FAIL;
    }

    ret = mRequestExecutor->Start(reqExecutorNum, queueSize);
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

    mCtrlChannelMgr = nullptr;
    mDataChannelMgr = nullptr;

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

BResult NetEngine::CreateShmFdWithName(int32_t &shmFd, uint64_t size, std::string &name)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
    auto fd = shm_open(name.c_str(), O_CREAT | O_RDWR | O_EXCL | O_CLOEXEC, 600);
#else
    auto fd = syscall(SYS_memfd_create, name.c_str(), 0);
#endif
    if (fd < 0) {
        NET_LOG_ERROR("create memory file " << name << ", failed, error:" << strerror(errno));
        return BIO_INNER_ERR;
    }

    auto ret = ftruncate(fd, static_cast<off_t>(size));
    if (ret < 0) {
        NET_LOG_ERROR("truncate file " << name << " with size" << size << " failed, error:" << strerror(errno));
        close(fd);
        return BIO_INNER_ERR;
    }

    shmFd = fd;
    return BIO_OK;
}

BResult NetEngine::InitCommMemAllocator()
{
    auto result = RegisterMemoryRegion(mOptions.memorySize, mLocalMr);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to register mr by size " << mOptions.memorySize);
        return result;
    }

    mMrBlockPool = MakeRef<NetBlockPool>();
    if (mMrBlockPool == nullptr) {
        NET_LOG_ERROR("Make block pool ptr failed.");
        return BIO_ALLOC_FAIL;
    }

    SetDataPageKb(NO_4 * NO_1024);
    result = mMrBlockPool->Start(mLocalMr->GetAddress(), mDataPageBytes, mOptions.memorySize / mDataPageBytes);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start block pool " << mOptions.memorySize << ".");
    } else {
        NET_LOG_INFO("Succeed to start comm memory pool success, size:" << mOptions.memorySize << ", key:" <<
            mLocalMr->GetLKey() << ".");
    }
    LOG_INFO("Regist mem: addr:" << mLocalMr->GetAddress() << ", size:" << mOptions.memorySize << ", Key:" <<
        mLocalMr->GetLKey());
    return result;
}

BResult NetEngine::InitShmMemAllocator()
{
    std::string shmName = "bio_shm";
    auto result = CreateShmFdWithName(mShmFd, mOptions.memorySize, shmName);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to create shm fd, size:" << mOptions.memorySize << ".");
        return result;
    }

    auto offset = static_cast<off_t>(mShareOffset);
    auto address = mmap(nullptr, mOptions.memorySize, PROT_READ | PROT_WRITE, MAP_SHARED, mShmFd, offset);
    if (address == MAP_FAILED) {
        NET_LOG_ERROR("Mmap bio_shm size " << mOptions.memorySize << " offset " << offset << " failed, error:" <<
            strerror(errno));
        close(mShmFd);
        mShmFd = -1;
        return BIO_ERR;
    }
    mShareAddress = static_cast<uint8_t *>(address);
    mShmSize = mOptions.memorySize;

    result = RegisterMemoryRegion(mShareAddress, mOptions.memorySize, mLocalMr);
    if (result != BIO_OK) {
        close(mShmFd);
        mShmFd = -1;
        return result;
    }

    LOG_INFO("Register mem: addr:" << (uintptr_t)(mShareAddress) << ", size:" << mOptions.memorySize << ", Key:" <<
        mLocalMr->GetLKey());

    mMrBlockPool = MakeRef<NetBlockPool>();
    if (mMrBlockPool == nullptr) {
        NET_LOG_ERROR("Make block pool ptr failed.");
        close(mShmFd);
        mShmFd = -1;
        return BIO_ALLOC_FAIL;
    }
    result = mMrBlockPool->Start(mLocalMr->GetAddress(), mDataPageBytes, mOptions.memorySize / mDataPageBytes);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start block pool " << mOptions.memorySize << ".");
        close(mShmFd);
        mShmFd = -1;
    } else {
        NET_LOG_INFO("Succeed to start shm memory pool success, size:" << mOptions.memorySize << " , shmOffset:" <<
            mShareOffset << ", shmFd:" << mShmFd << ", shmAddr:" << reinterpret_cast<uintptr_t>(mShareAddress) <<
            ", key:" << mLocalMr->GetLKey() << ".");
    }
    return result;
}

BResult NetEngine::InitMemoryAllocator()
{
    if (mOptions.regShmMem == true) {
        return InitShmMemAllocator();
    } else {
        return InitCommMemAllocator();
    }
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
    oss << std::to_string(mOptions.handlerCount) << "," << std::to_string(mOptions.handlerCount);
    /* client side groups */
    oss << "," << std::to_string(mOptions.handlerCount) << "," << std::to_string(mOptions.handlerCount);
    return oss.str();
}

void NetEngine::AssignIpcServiceOptions(const NetOptions &opt, bool isOobSvr, ock::hcom::NetServiceOptions &options)
{
    options.mode = opt.isBusyLoop ? NetDriverWorkingMode::NET_BUSY_POLLING : NetDriverWorkingMode::NET_EVENT_POLLING;
    options.SetWorkerGroups(GenerateWorkersSetting());
    options.mrSendReceiveSegSize = MAX_MESSAGE_SIZE + MAX_MESSAGE_HEAD_SIZE;
    options.mrSendReceiveSegCount = NO_1024;
    options.heartBeatIdleTime = NO_5;
    options.heartBeatProbeInterval = NO_1;
    options.qpSendQueueSize = NO_512;
    options.pollingBatchSize = NO_16;
    options.prePostReceiveSizePerQP = NO_64;
    options.oobType = NET_OOB_UDS;
    options.completionQueueDepth = NO_8192;
    options.eventPollingTimeout = NO_1000;
    const static int DEFAULT_THREAD_PRIORITY = 0;
    options.workerThreadPriority = DEFAULT_THREAD_PRIORITY;
    if (isOobSvr) {
        NetServiceOobUDSListenerOptions listenOpt;
        listenOpt.Name(UDS_NAME);
        listenOpt.perm = 0;
        mIpcService->AddOobUdsOptions(listenOpt);
        mIpcService->RegisterNewChannelHandler(std::bind(&NetEngine::NewChannel, this, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3));
    }
    mIpcService->RegisterChannelBrokenHandler(std::bind(&NetEngine::ChannelBroken, this, std::placeholders::_1),
        BROKEN_ALL);
    mIpcService->RegisterOpReceiveHandler(0, std::bind(&NetEngine::RequestIPCReceived, this, std::placeholders::_1));
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
    AssignIpcServiceOptions(opt, isOobSvr, options);
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

    result = InitMemoryAllocator();
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to init mr allocator, result:" << NetErrStr(result) << ".");
        return BIO_ERR;
    }

    NET_LOG_INFO("Bio server start rpc service success, protocol:" << opt.protocol << ".");
    return BIO_OK;
}

int32_t NetEngine::NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload)
{
    NewChannelResp resp;
    if (mHandleNewChannel != nullptr) {
        mHandleNewChannel(newChannel, ipPort, resp);
    }
    if (resp.result != 0) {
        NET_LOG_WARN("Peer connection from " << ipPort << " has been refused");
        return BIO_ERR;
    }

    NetConnPayload netPayload;
    bool isCtrl = true;
    if (netPayload.FromPayloadStr(payload, isCtrl) != BIO_OK) {
        NET_LOG_ERROR("Failed to parse payload:" << payload << ".");
        return BIO_ERR;
    }

    NetChannelUpCtx ctx(netPayload.srcNodeId, isCtrl, true);
    newChannel->UpCtx(ctx.whole);

    if (netPayload.srcNodeId.pid == 0) {
        NET_LOG_INFO("No needed add channel " << newChannel->Id() << ", peer connected nid " <<
            netPayload.srcNodeId.nid << " pid " << netPayload.srcNodeId.pid << ", ip " << ipPort << ", payload " <<
            payload);
        return BIO_OK;
    }

    if (isCtrl) {
        mCtrlChannelMgr->AddChannel(netPayload.srcNodeId, const_cast<ChannelPtr &>(newChannel));
    } else {
        mDataChannelMgr->AddChannel(netPayload.srcNodeId, const_cast<ChannelPtr &>(newChannel));
    }
    NET_LOG_INFO("Receive new channel " << newChannel->Id() << ", peer connected from:" << netPayload.srcNodeId.nid <<
        "-" << netPayload.srcNodeId.pid << ", ip:" << ipPort << ", payload " << payload << ".");
    return BIO_OK;
}

void NetEngine::ChannelBroken(const ChannelPtr &ch)
{
    NetChannelUpCtx ctx(ch->UpCtx());
    NET_LOG_WARN("Net Engine channel " << ch->Id() << " broken, node id " << ctx.peerId << ".");

    NetNode dstNid(static_cast<uint32_t>(ctx.peerId), ctx.procId);
    if (ctx.IsCtrlPanel()) {
        mCtrlChannelMgr->RemoveChannel(dstNid, ch);
    } else {
        mDataChannelMgr->RemoveChannel(dstNid, ch);
    }
    if (mHandlerBroken != nullptr) {
        mHandlerBroken(dstNid.nid, dstNid.pid);
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

int32_t NetEngine::RequestIPCReceived(ServiceContext &ctx)
{
    auto &handler = mHandlers[ctx.OpCode()];
    if (UNLIKELY(handler == nullptr)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode() << ".");
        return BIO_ERR;
    }
    return handler(ctx);
}

int32_t NetEngine::RequestPosted(const ServiceContext &ctx)
{
    return BIO_OK;
}

int32_t NetEngine::OneSideDone(const ServiceContext &ctx)
{
    return BIO_OK;
}

BResult NetEngine::ConnectToPeer(ConnectMode mode, ConnectInfo &info, bool isCtrlPanel, ChannelPtr &ch)
{
    NetService *netService = (mode == CONNECT_IPC) ? mIpcService : mRpcService;
    if (netService == nullptr) {
        NET_LOG_ERROR("Net service not ready.");
        return BIO_ERR;
    }

    NetServiceConnectOptions options{};
    std::string prefix;
    options.epSize = mOptions.connCount;
    if (isCtrlPanel) {
        options.clientGrpNo = WKR_GRP_INDEX_CTRL_CLIENT;
        options.serverGrpNo = WKR_GRP_INDEX_CTRL_SERVER;
        prefix = CONN_PAYLOAD_PREFIX_CTRL;
    } else {
        options.clientGrpNo = WKR_GRP_INDEX_DATA_CLIENT;
        options.serverGrpNo = WKR_GRP_INDEX_DATA_SERVER;
        prefix = CONN_PAYLOAD_PREFIX_DATA;
    }
    if (info.isSelfPoll) {
        options.flags = NET_EP_SELF_POLLING;
    }

    int32_t result = 0;
    for (uint16_t i = 0; i < info.retryTimes; ++i) {
        NetConnPayload payload(info.srcId);
        if (mode == CONNECT_IPC) {
            result = netService->Connect(UDS_NAME, 0, payload.ToPayloadStr(prefix), ch, options);
        } else {
            result = netService->Connect(info.ip, info.port, payload.ToPayloadStr(prefix), ch, options);
        }
        if (result == 0) {
            NET_LOG_INFO("Connect to peer success, ip " << info.ip << ", port " << info.port << ", nid " <<
                info.peerId.nid << ", payload " << payload.ToPayloadStr(prefix) << ".");
            break;
        }
    }
    if (result != 0) {
        NET_LOG_ERROR("Connect to peer failed, ret:" << NetErrStr(result) << ", ip " << info.ip << ", port " <<
            info.port << ", nid " << info.peerId.nid << ", pid " << info.peerId.pid << ".");
        return result;
    }

    NetChannelUpCtx ctx(info.peerId, isCtrlPanel, false);
    ch->UpCtx(ctx.whole);
    ch->SetOneSideTimeout(mTimeout);
    ch->SetTwoSideTimeout(mTimeout);
    return BIO_OK;
}
}
}