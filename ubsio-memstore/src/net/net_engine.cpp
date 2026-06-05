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
#include "mms_file_util.h"
#include "mms_ip_util.h"
#include "mms_trace.h"
#include "net_log.h"
#include "net_executor_pool.h"
#include "net_engine.h"

namespace ock {
namespace mms {
using namespace ock::hcom;

static void HcomLog(int level, const char *msg)
{
    NET_BASE_LOG(level, msg);
}

BResult NetEngine::Initialize(int16_t timeoutSec, uint32_t coreThreadNum, uint32_t queueSize, NetLogFunc func,
    NetMemList &memList)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        NET_LOG_WARN("Net engine has been already initialized.");
        return MMS_OK;
    }

    mMemList = memList;

    NetLog::Instance()->SetLogFuncFunc(func);
    mTimeout = timeoutSec;

    mChannelMgr = MakeRef<NetChannelMgr>();
    if (mChannelMgr == nullptr) {
        NET_LOG_ERROR("Make ctrl channel manager failed.");
        return MMS_ALLOC_FAIL;
    }
    mChannelMgr->Initialize();

    mConnector = MakeRef<NetConnector>(this);
    if (mConnector == nullptr) {
        NET_LOG_ERROR("Make net connector failed.");
        return MMS_ALLOC_FAIL;
    }
    BResult ret = mConnector->Start();
    if (ret != MMS_OK) {
        NET_LOG_ERROR("Failed to start net connector, ret:" << ret << ".");
        return ret;
    }

    mReqExecutorNum = coreThreadNum;
    if (mReqExecutorNum != 0) {
        mRequestExecutor = MakeRef<NetExecutorPool>("NetExecutor");
        if (mRequestExecutor == nullptr) {
            NET_LOG_ERROR("Make net request executor failed.");
            return MMS_ALLOC_FAIL;
        }

        ret = mRequestExecutor->Start(mReqExecutorNum, queueSize);
        if (ret != MMS_OK) {
            NET_LOG_ERROR("Failed to start request executor, ret:" << ret << ".");
            return ret;
        }
    }

    UBSHcomNetOutLogger::Instance()->SetExternalLogFunction(HcomLog);
    mStarted = true;
    return MMS_OK;
}

BResult NetEngine::Start(const NetOptions &opt)
{
    int32_t result = MMS_INNER_ERR;
    if (opt.protocol == ServiceProtocol::SHM || opt.protocol == ServiceProtocol::UDS) {
        result = StartIpcService(opt);
        if (result != MMS_OK) {
            return result;
        }
    }
    if (opt.protocol == ServiceProtocol::TCP || opt.protocol == ServiceProtocol::RDMA) {
        result = StartRpcService(opt);
        if (result != MMS_OK) {
            return result;
        }
    }
    return result;
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

    mChannelMgr->UnInitialize();
    mChannelMgr = nullptr;

    if (mRpcService != nullptr) {
        uint16_t index;
        for (index = 0; index < mMemList.num; index++) {
            if (!mMemList.mr[index].GetHcomMrs().empty()) {
                mRpcService->DestroyMemoryRegion(mMemList.mr[index]);
            }
        }
        bool isOobSvr = mRpcOptions.role != NET_CLIENT;
        std::string rpcName = isOobSvr ? RPC_SERVICE_NAME_SERVER : RPC_SERVICE_NAME_CLIENT;
        mRpcService->Destroy(rpcName);
        mRpcService = nullptr;
    }

    if (mIpcService != nullptr) {
        bool isOobSvr = mIpcOptions.role != NET_CLIENT;
        std::string ipcName = isOobSvr ? IPC_SERVICE_NAME_SERVER : IPC_SERVICE_NAME_CLIENT;
        mIpcService->Destroy(ipcName);
        mIpcService = nullptr;
    }
}

BResult NetEngine::InitMemoryRegister(void)
{
    uint16_t index;
    for (index = 0; index < mMemList.num; index++) {
        auto ret = mRpcService->RegisterMemoryRegion(mMemList.address[index], mMemList.size[index],
            mMemList.mr[index]);
        if (ret != MMS_OK) {
            NET_LOG_ERROR("Failed to register memory region, address:" << mMemList.address[index] <<
                ", size:" << mMemList.size[index] << ".");
            return ret;
        }
        LOG_INFO("Register memory success, address:" << mMemList.address[index] <<
            ", size:" << mMemList.size[index] << ", key:" << mMemList.mr[index].GetHcomMrs()[0]->GetLKey() << ".");
    }
    return MMS_OK;
}

PrivateKeyCallback NetEngine::CreatePrivateKeyCallback(const NetOptions &options)
{
    return [this, &options](const std::string &name, std::string &path, void *&pwd, int &len,
                            UBSHcomTLSEraseKeypass &erase) {
        std::vector<char> encryptedKeyPass(KEYPASS_MAX_LEN, 0);
        std::ifstream fileStream(options.privateKeyPasswordPath);
        if (!fileStream.is_open()) {
            LOG_ERROR("Failed to open keyPassFile: " << options.privateKeyPasswordPath);
            return false;
        }

        if (!fileStream.getline(encryptedKeyPass.data(), KEYPASS_MAX_LEN)) {
            LOG_ERROR("Failed to read keyPassFile");
            return false;
        }

        size_t actualLen = strlen(encryptedKeyPass.data());
        std::vector<char> plainTextBuffer(KEYPASS_MAX_LEN, 0);
        size_t plainTextLen = KEYPASS_MAX_LEN;
        auto ret = mDecryptHandler(encryptedKeyPass.data(), actualLen, plainTextBuffer.data(), &plainTextLen);
        if (ret != 0) {
            std::fill(plainTextBuffer.begin(), plainTextBuffer.end(), 0);
            LOG_ERROR("Decrypt failed with error: " << ret);
            return false;
        }

        path = options.privateKeyPath;
        pwd = malloc(plainTextLen);
        len = static_cast<int>(plainTextLen);
        if (!pwd) {
            std::fill(plainTextBuffer.begin(), plainTextBuffer.end(), 0);
            LOG_ERROR("Memory allocation failed.");
            return false;
        }

        ret = memcpy_s(pwd, plainTextLen, plainTextBuffer.data(), plainTextLen);
        if (ret != 0) {
            std::fill(plainTextBuffer.begin(), plainTextBuffer.end(), 0);
            free(pwd);
            pwd = nullptr;
            LOG_ERROR("Memory copy failed.");
            return false;
        }

        erase = [](void *pass, int len) {
            if (pass && len > 0) {
                (void)memset_s(pass, len, 0, len);
                free(pass);
                pass = nullptr;
            }
        };

        std::fill(plainTextBuffer.begin(), plainTextBuffer.end(), 0);
        return true;
    };
}

void NetEngine::SetDriverTlsCallback(const NetOptions &options, ock::hcom::UBSHcomTlsOptions &tlsOpt)
{
    auto tlsCertificationCallback = ([&options](const std::string &name, std::string &path) {
        path = options.certificationPath;
        NET_LOG_INFO("Get client cert success.");
        return true;
    });
    tlsOpt.cfCb = tlsCertificationCallback;

    auto tlsCaCallback = ([&options](const std::string &name, std::string &capath, std::string &crlPath,
                                     UBSHcomPeerCertVerifyType &verifyPeerCert, UBSHcomTLSCertVerifyCallback &cb) {
        capath = options.caCerPath;
        if (!options.caCrlPath.empty()) {
            crlPath = options.caCrlPath;
            NET_LOG_INFO("Get cacrl cert path success.");
        }
        NET_LOG_INFO("Get CA cert success.");
        verifyPeerCert = UBSHcomPeerCertVerifyType::VERIFY_BY_DEFAULT;
        cb = [](void *, const char *) {
            return 0;
        };
        return true;
    });
    tlsOpt.caCb = tlsCaCallback;
    tlsOpt.pkCb = CreatePrivateKeyCallback(options);
}

BResult NetEngine::AssignIpcServiceOptions(const NetOptions &opt, bool isOobSvr)
{
    UBSHcomHeartBeatOptions hbOpt;
    hbOpt.heartBeatIdleSec = NO_5;
    hbOpt.heartBeatProbeIntervalSec = 1;
    mIpcService->SetHeartBeatOptions(hbOpt);
    mIpcService->SetMaxSendRecvDataCount(NO_1024);
    mIpcService->SetSendQueueSize(NO_128);
    mIpcService->SetCompletionQueueDepth(NO_8192);
    mIpcService->SetPollingBatchSize(NO_16);
    mIpcService->SetQueuePrePostSize(NO_64);
    mIpcService->SetEventPollingTimeOutUs(NO_1000);
    mIpcService->SetMaxConnectionCount(NO_4096 * NO_1024);
    if (isOobSvr) {
        const std::string listenerUrl = "uds://" + UDS_NAME;
        auto ret = mIpcService->Bind(listenerUrl, std::bind(&NetEngine::NewChannel, this, std::placeholders::_1,
                                                            std::placeholders::_2, std::placeholders::_3));
        if (UNLIKELY(ret != MMS_OK)) {
            NET_LOG_ERROR("Net tls callback has created.");
            return ret;
        }
    }

    UBSHcomTlsOptions tlsOpt;
    tlsOpt.enableTls = opt.tlsEnable;
    if (opt.tlsEnable) {
        SetDriverTlsCallback(mIpcOptions, tlsOpt);
        NET_LOG_INFO("Net tls callback has created.");
    }
    mIpcService->SetTlsOptions(tlsOpt);
    mIpcService->RegisterChannelBrokenHandler(std::bind(&NetEngine::ChannelBroken, this, std::placeholders::_1),
                                              UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    mIpcService->RegisterRecvHandler(std::bind(&NetEngine::RequestInnerReceived, this, std::placeholders::_1));
    mIpcService->RegisterSendHandler(std::bind(&NetEngine::RequestPosted, this, std::placeholders::_1));
    mIpcService->RegisterOneSideHandler(std::bind(&NetEngine::OneSideDone, this, std::placeholders::_1));
    return MMS_OK;
}

BResult NetEngine::StartIpcService(const NetOptions &opt)
{
    int result = MMS_ERR;
    if (mIpcService != nullptr) {
        NET_LOG_INFO("Net ipc service has already created.");
        return MMS_OK;
    }

    mIpcOptions = opt;
    bool isOobSvr = opt.role != NET_CLIENT;
    std::string name = isOobSvr ? IPC_SERVICE_NAME_SERVER : IPC_SERVICE_NAME_CLIENT;

    std::vector<std::string> groupsVec;
    StrUtil::Split(opt.workerGroups, ",", groupsVec);
    std::vector<std::string> groupsCpuSetVec;
    StrUtil::Split(opt.workerGroupsCpuSet, ",", groupsCpuSetVec);
    if (groupsVec.size() != groupsCpuSetVec.size()) {
        NET_LOG_ERROR("Worker group size not equal group cpu set size. Worker group size: " << groupsVec.size() <<
            ", group cpu set size: " << groupsCpuSetVec.size() << ".");
        return MMS_INVALID_PARAM;
    }

    UBSHcomServiceOptions options;
    options.workerGroupMode = opt.isBusyPolling ? UBSHcomNetDriverWorkingMode::NET_BUSY_POLLING :
                              UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.maxSendRecvDataSize = (NO_64 * NO_1024);
    options.workerGroupId = 0;
    options.workerThreadPriority = 0;
    if (isOobSvr) {
        StrUtil::StrToUint16(groupsVec[0], options.workerGroupThreadCount);
        StrUtil::StrToUint32Pair(groupsCpuSetVec[0], options.workerGroupCpuIdsRange);
    } else {
        options.workerGroupThreadCount = 0;
        std::pair<uint32_t, uint32_t> workerGroupCpuIdsRangeOther = {UINT32_MAX, UINT32_MAX};
        options.workerGroupCpuIdsRange = workerGroupCpuIdsRangeOther;
    }

    mIpcService = UBSHcomService::Create(opt.protocol, name, options);
    if (mIpcService == nullptr) {
        NET_LOG_ERROR("Failed to create ipc service instance, protocol:" << opt.protocol << ".");
        return MMS_ERR;
    }

    if (opt.tlsEnable) {
        result = PrepareTlsDecrypter(opt);
        if (result != MMS_OK) {
            NET_LOG_ERROR("Failed to prepare tls decrypter, result:" << result << ".");
            return result;
        }
    }

    if (isOobSvr && options.workerGroupThreadCount != 0) {
        for (size_t i = 1; i < groupsVec.size(); i++) {
            StrUtil::StrToUint16(groupsVec[i], options.workerGroupThreadCount);
            StrUtil::StrToUint32Pair(groupsCpuSetVec[i], options.workerGroupCpuIdsRange);
            mIpcService->AddWorkerGroup(i, options.workerGroupThreadCount, options.workerGroupCpuIdsRange);
        }
    }

    result = AssignIpcServiceOptions(opt, isOobSvr);
    if (result != MMS_OK) {
        NET_LOG_ERROR("Failed to assign ipc service options, result:" << UBSHcomNetErrStr(result) << ".");
        return result;
    }

    result = mIpcService->Start();
    if (result != MMS_OK) {
        NET_LOG_ERROR("Failed to start ipc service, result:" << UBSHcomNetErrStr(result) << ".");
        return MMS_ERR;
    }
    NET_LOG_INFO("Mms server Start ipc service success, protocol:" << opt.protocol << ".");
    return MMS_OK;
}

BResult NetEngine::AssignRpcServiceOptions(const NetOptions &opt, bool isOobSvr)
{
    std::string ipMask = opt.ipMask;
    auto port = opt.port;
    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(ipMask, goodIps) || goodIps.empty()) {
        NET_LOG_ERROR("Failed to find ip with ip mask " << ipMask);
        return MMS_ERR;
    }

    UBSHcomHeartBeatOptions hbOpt;
    hbOpt.heartBeatIdleSec = NO_1;
    hbOpt.heartBeatProbeTimes = NO_1;
    hbOpt.heartBeatProbeIntervalSec = NO_1;
    mRpcService->SetHeartBeatOptions(hbOpt);
    mRpcService->SetMaxSendRecvDataCount(NO_1024);
    mRpcService->SetSendQueueSize(NO_4096);
    mRpcService->SetRecvQueueSize(NO_2048);
    mRpcService->SetCompletionQueueDepth(NO_2048);
    mRpcService->SetPollingBatchSize(NO_1024);
    mRpcService->SetQueuePrePostSize(NO_1024);
    mRpcService->SetTcpUserTimeOutSec(NO_3);
    mRpcService->SetDeviceIpMask({ipMask});

    if (isOobSvr) {
        const std::string listenerUrl = "tcp://" + goodIps.at(0) + ":" + std::to_string(port);
        auto ret = mRpcService->Bind(listenerUrl, std::bind(&NetEngine::NewChannel, this, std::placeholders::_1,
                                                            std::placeholders::_2, std::placeholders::_3));
        if (UNLIKELY(ret != MMS_OK)) {
            NET_LOG_ERROR("Net tls callback has created.");
            return ret;
        }
    }

    UBSHcomTlsOptions tlsOpt;
    tlsOpt.enableTls = opt.tlsEnable;
    if (opt.tlsEnable) {
        SetDriverTlsCallback(mRpcOptions, tlsOpt);
        NET_LOG_INFO("Net tls callback has created.");
    }
    mRpcService->SetTlsOptions(tlsOpt);
    mRpcService->RegisterChannelBrokenHandler(std::bind(&NetEngine::ChannelBroken, this, std::placeholders::_1),
                                              UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    mRpcService->RegisterRecvHandler(std::bind(&NetEngine::RequestInnerReceived, this, std::placeholders::_1));
    mRpcService->RegisterSendHandler(std::bind(&NetEngine::RequestPosted, this, std::placeholders::_1));
    mRpcService->RegisterOneSideHandler(std::bind(&NetEngine::OneSideDone, this, std::placeholders::_1));
    return MMS_OK;
}

BResult NetEngine::StartRpcService(const NetOptions &opt)
{
    int result = MMS_ERR;
    if (mRpcService != nullptr) {
        NET_LOG_INFO("Net rpc service has already created.");
        return MMS_OK;
    }

    mRpcOptions = opt;
    mUseHlcRpc = opt.protocol == ServiceProtocol::TCP;
    bool isOobSvr = opt.role != NET_CLIENT;
    std::string name = isOobSvr ? RPC_SERVICE_NAME_SERVER : RPC_SERVICE_NAME_CLIENT;
    std::vector<std::string> groupsVec;
    StrUtil::Split(opt.workerGroups, ",", groupsVec);
    std::vector<std::string> groupsCpuSetVec;
    StrUtil::Split(opt.workerGroupsCpuSet, ",", groupsCpuSetVec);
    if (groupsVec.size() != groupsCpuSetVec.size()) {
        NET_LOG_ERROR("Worker group size not equal group cpu set size. Worker group size: " << groupsVec.size() <<
            ", group cpu set size: " << groupsCpuSetVec.size() << ".");
        return MMS_INVALID_PARAM;
    }

    UBSHcomServiceOptions options;
    options.workerGroupMode = opt.isBusyPolling ? UBSHcomNetDriverWorkingMode::NET_BUSY_POLLING :
                              UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.maxSendRecvDataSize = isOobSvr ? (NO_256 * NO_1024) : (NO_16 * NO_1024);
    options.workerGroupId = 0;
    options.workerThreadPriority = 0;
    StrUtil::StrToUint16(groupsVec[0], options.workerGroupThreadCount);
    StrUtil::StrToUint32Pair(groupsCpuSetVec[0], options.workerGroupCpuIdsRange);
    mRpcService = UBSHcomService::Create(opt.protocol, name, options);
    if (mRpcService == nullptr) {
        NET_LOG_ERROR("Failed to create rpc service instance, protocol:" << opt.protocol << ".");
        return MMS_ERR;
    }

    if (opt.protocol == ServiceProtocol::TCP) {
        mRpcService->SetTcpEpollMode(true);
    }

    if (opt.tlsEnable) {
        result = PrepareTlsDecrypter(opt);
        if (result != MMS_OK) {
            NET_LOG_ERROR("Failed to prepare tls decrypter, result:" << result << ".");
            return result;
        }
    }

    for (size_t i = 1; i < groupsVec.size(); i++) {
        StrUtil::StrToUint16(groupsVec[i], options.workerGroupThreadCount);
        StrUtil::StrToUint32Pair(groupsCpuSetVec[i], options.workerGroupCpuIdsRange);
        mRpcService->AddWorkerGroup(i, options.workerGroupThreadCount, options.workerGroupCpuIdsRange);
    }

    result = AssignRpcServiceOptions(opt, isOobSvr);
    if (result != MMS_OK) {
        NET_LOG_ERROR("Failed to assign rpc service options, result:" << UBSHcomNetErrStr(result) << ".");
        return MMS_ERR;
    }

    result = mRpcService->Start();
    if (result != MMS_OK) {
        NET_LOG_ERROR("Failed to start rpc service, result:" << UBSHcomNetErrStr(result) << ".");
        return MMS_ERR;
    }

    result = InitMemoryRegister();
    if (result != MMS_OK) {
        NET_LOG_ERROR("Failed to init mr allocator, result:" << UBSHcomNetErrStr(result) << ".");
        return MMS_ERR;
    }

    NET_LOG_INFO("Mms server start rpc service success, protocol:" << opt.protocol << ".");
    return MMS_OK;
}

int32_t NetEngine::NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload)
{
    NewChannelResp resp;
    if (mHandleNewChannel != nullptr) {
        mHandleNewChannel(newChannel, ipPort, resp);
    }
    if (resp.result != 0) {
        NET_LOG_WARN("Peer connection from " << ipPort << " has been refused");
        return MMS_ERR;
    }

    NetConnPayload netPayload;
    uint32_t groupIndex = 0;
    if (netPayload.FromPayloadStr(payload, groupIndex) != MMS_OK) {
        NET_LOG_ERROR("Failed to parse payload:" << payload << ".");
        return MMS_ERR;
    }

    NetChannelUpCtx ctx(netPayload.srcNodeId, groupIndex, true);
    newChannel->SetUpCtx(ctx.whole);
    newChannel->SetChannelTimeOut(mTimeout, mTimeout);

    if (netPayload.srcNodeId.pid == 0) {
        NET_LOG_INFO("Receive new channel, not needed add, channel " << newChannel->GetId() << ", peer connected nid "
            << netPayload.srcNodeId.nid << " pid " << netPayload.srcNodeId.pid << ", ip " << ipPort << ", payload " <<
            payload << ".");
        return MMS_OK;
    }

    mChannelMgr->AddChannel(netPayload.srcNodeId, const_cast<ChannelPtr &>(newChannel), groupIndex);
    NET_LOG_INFO("Receive new channel " << newChannel->GetId() << ", nodeId:" << netPayload.srcNodeId.nid << ", pid:" <<
        netPayload.srcNodeId.pid << ", ip:" << ipPort << ", payload:" << payload << ".");
    return MMS_OK;
}

void NetEngine::ChannelBroken(const ChannelPtr &ch)
{
    if (ch == nullptr) {
        NET_LOG_ERROR("Invalid input parameter, ch is nullptr.");
        return;
    }

    NetChannelUpCtx ctx(ch->GetUpCtx());
    NetNode dstNid(static_cast<uint32_t>(ctx.peerId), ctx.procId);
    NET_LOG_WARN("Receive broken channel " << ch->GetId() << ", nodeId:" << dstNid.nid << ", pid:" << dstNid.pid <<
        ", groupIndex:" << ctx.GetGroupIndex() << ".");

    mChannelMgr->RemoveChannel(dstNid, ch, ctx.GetGroupIndex());
    if (mHandlerBroken != nullptr) {
        mHandlerBroken(dstNid.nid, dstNid.pid);
    }
}

int32_t NetEngine::RequestReceived(ServiceContext &ctx)
{
    if (UNLIKELY(mRequestExecutor == nullptr)) {
        NET_LOG_ERROR("Net request executor not ready.");
        return MMS_NOT_READY;
    }
    if (UNLIKELY(ctx.OpCode() >= MAX_NEW_REQ_HANDLER)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode());
        return MMS_ERR;
    }
    auto &handler = mHandlers[ctx.OpCode()];
    if (UNLIKELY(handler == nullptr)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode() <<
            " as no handler registered");
        return MMS_ERR;
    }
    auto ret = mRequestExecutor->AddTask(handler, ctx);
    return ret;
}

int32_t NetEngine::RequestInnerReceived(ServiceContext &ctx)
{
    ReqHead *head = (ReqHead *)ctx.MessageData();
    auto &handler = mHandlers[head->opcode];
    if (UNLIKELY(handler == nullptr)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << head->opcode << ".");
        return MMS_ERR;
    }

    MMS_TRACE_START(NET_TRACE_IPC_HDL);
    auto ret = handler(ctx);
    MMS_TRACE_END(NET_TRACE_IPC_HDL, ret);
    return ret;
}

int32_t NetEngine::RequestPosted(const ServiceContext &ctx)
{
    return MMS_OK;
}

int32_t NetEngine::OneSideDone(const ServiceContext &ctx)
{
    return MMS_OK;
}

void NetEngine::FillConnectOption(ConnectMode mode, ConnectInfo &info, uint32_t groupIndex, std::string &prefix,
                                  ock::hcom::UBSHcomConnectOptions &op)
{
    if (mode == CONNECT_IPC) {
        op.linkCount = mIpcOptions.connCount;
    } else {
        op.linkCount = mRpcOptions.connCount;
        op.clientGroupId = static_cast<uint8_t>(groupIndex);
    }

    op.serverGroupId = static_cast<uint8_t>(groupIndex);
    prefix = CONN_PAYLOAD_PREFIX + "-" + std::to_string(groupIndex) + "-";
    if (info.isSelfPoll) {
        op.mode = UBSHcomClientPollingMode::SELF_POLL_BUSY;
    }

    NetConnPayload payload(info.srcId);
    op.payload = payload.ToPayloadStr(prefix);
}

BResult NetEngine::ConnectToPeer(ConnectMode mode, ConnectInfo &info, uint32_t groupIndex, ChannelPtr &ch)
{
    UBSHcomService *netService = (mode == CONNECT_IPC) ? mIpcService : mRpcService;
    if (netService == nullptr) {
        NET_LOG_ERROR("Net service not ready.");
        return MMS_ERR;
    }

    UBSHcomConnectOptions options;
    std::string prefix;
    FillConnectOption(mode, info, groupIndex, prefix, options);
    int32_t result = 0;
    for (uint16_t i = 0; i < info.retryTimes; ++i) {
        NetConnPayload payload(info.srcId);
        if (mode == CONNECT_IPC) {
            const std::string serverUrl = "uds://" + UDS_NAME;
            result = netService->Connect(serverUrl, ch, options);
        } else {
            const std::string serverUrl = "tcp://" + info.ip + ":" + std::to_string(info.port);
            result = netService->Connect(serverUrl, ch, options);
        }
        if (result == 0) {
            NET_LOG_INFO("Connect to peer success, ip " << info.ip << ", port " << info.port << ", dstNid " <<
                info.peerId.nid << ", payload " << payload.ToPayloadStr(prefix) << ".");
            break;
        }
    }
    if (result != 0) {
        NET_LOG_ERROR("Connect to peer failed, ret:" << UBSHcomNetErrStr(result) << ", ip " << info.ip << ", port " <<
            info.port << ", nid " << info.peerId.nid << ", pid " << info.peerId.pid << ".");
        return result;
    }

    NetChannelUpCtx ctx(info.peerId, groupIndex, false);
    ch->SetUpCtx(ctx.whole);
    ch->SetChannelTimeOut(mTimeout, mTimeout);
    return MMS_OK;
}

BResult NetEngine::PrepareTlsDecrypter(const NetOptions &config)
{
    const auto decrypter = TlsUtil::LoadDecryptFunction(config.decrypterLibPath.c_str());
    if (decrypter == nullptr) {
        LOG_ERROR("Failed to load customized decrypt function.");
        return MMS_INVALID_PARAM;
    }

    RegisterDecryptHandler(decrypter);
    return MMS_OK;
}

}
}
