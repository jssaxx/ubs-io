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

#include <thread>
#include <chrono>
#include "mms_server.h"
#include "mms_trace.h"
#include "net_multicast_engine.h"

namespace ock {
namespace mms {
using namespace ock::hcom;

static constexpr uint8_t HCOM_TLS_HEADER_COST = 128;
constexpr uint16_t MULTICAST_CONNECT_TIME_OUT = 60; // 组播建链超时时间(s)
constexpr uint16_t CONNECT_DONE_RETRY_INTERAL = 1;

bool NetMulticastEngine::NewSubscriptionCallBack(ock::hcom::SubscriptionInfoPtr &info)
{
    if (mPublisherService == nullptr || mPublisher == nullptr) {
        NET_LOG_ERROR("mPublisherService or mPublisher is nullptr");
        return MMS_NOT_READY;
    }

    if (!mPublisher->AddSubscription(info)) {
        NET_LOG_ERROR("addSubscription failed.");
        return MMS_INNER_ERR;
    }

    mSubScribersRemoteLock.LockWrite();
    mSubScribersRemote.emplace(info->GetIp());
    mSubScribersRemoteLock.UnLock();

    NET_LOG_INFO("Add a subscriber success, id:" << info->GetId() << ", ip:" << info->GetIp()
                                                 << ", port:" << info->GetPort() << ", name:" << info->GetName()
                                                 << ", epId:" << info->GetId() << ".");
    return MMS_OK;
}

void NetMulticastEngine::PublisherSubscriberEpBroken(const ock::hcom::UBSHcomNetEndpointPtr &ep)
{
    NET_LOG_WARN("publisher ep broken id " << ep->Id());
    auto info = mPublisher->GetSubscribeByEpId(ep->Id());
    mPublisher->DelSubscription(info);

    mSubScribersRemoteLock.LockWrite();
    mSubScribersRemote.erase(info->GetIp());
    mSubScribersRemoteLock.UnLock();
}

BResult NetMulticastEngine::CreatePublisherService(const std::string &oobIp, uint16_t oobPort,
                                                   const std::string &ipMask)
{
    auto &opt = MmsConfig::Instance()->GetNetConfig();
    std::pair<long, long> cpuSet = opt.publisherWorkerCpuSet;
    uint32_t cpuStart = static_cast<uint32_t>(cpuSet.first);
    MulticastServiceOptions options;
    options.maxSendRecvDataSize = opt.msgMaxBuffSize + HCOM_TLS_HEADER_COST;
    options.enableTls = opt.tlsEnable;
    options.workerGroupCpuIdsRange = std::make_pair(cpuStart, cpuStart);
    options.workerGroupThreadCount = NO_1;
    options.workerGroupMode = ock::hcom::NET_BUSY_POLLING;
    options.qpRecvQueueSize = NO_4096;
    options.qpSendQueueSize = NO_4096;
    options.qpPrePostSize = NO_2048;
    options.qpBatchRePostSize = NO_64;
    options.maxSubscriberNum = MAX_NODES_NUM - 1;
    if (opt.multicastProtocol == "rdma") {
        options.protocol = UBSHcomNetDriverProtocol::RDMA;
    } else {
        options.protocol = UBSHcomNetDriverProtocol::TCP;
    }

    mPublisherWorkGroupNum= static_cast<uint16_t>(cpuSet.second - cpuSet.first + NO_1);
    mPublisherServiceName = "Publisher";
    mPublisherService = ock::hcom::PublisherService::Create(mPublisherServiceName, options);
    if (mPublisherService == nullptr) {
        NET_LOG_ERROR("Failed to create publisher service, ip:" << oobIp << ", port:" << oobPort << ".");
        return MMS_INNER_ERR;
    }

    if (options.enableTls) {
        auto result = PrepareTlsDecrypter(opt.decrypterLibPath);
        if (result != MMS_OK) {
            NET_LOG_ERROR("Failed to prepare tls decrypter, result:" << result << ".");
            DestroyPublisherService();
            return result;
        }
        SetDriverTlsCallback(mPublisherService, opt);
    }

    for (int groupIdx = NO_1; groupIdx < mPublisherWorkGroupNum; ++groupIdx) {
        // AddWorkerGroup(groupIdx, threadCount, cpuSet, priority)
        mPublisherService->AddWorkerGroup(groupIdx, NO_1, std::make_pair(cpuStart + groupIdx, cpuStart + groupIdx), 0);
    }

    std::string url = "tcp://" + oobIp + ":" + std::to_string(oobPort);
    mPublisherService->GetConfig().SetDeviceIpMask({ipMask});
    mPublisherService->Bind(url, std::bind(&NetMulticastEngine::NewSubscriptionCallBack, this, std::placeholders::_1));
    mPublisherService->RegisterBrokenHandler(
        std::bind(&NetMulticastEngine::PublisherSubscriberEpBroken, this, std::placeholders::_1));

    NET_LOG_INFO("PublisherService Created, ip:" << oobIp << ", port:" << oobPort << ".");
    return MMS_OK;
}

BResult NetMulticastEngine::StartPublisherService()
{
    if (mPublisherService == nullptr) {
        NET_LOG_ERROR("mPublisherService is nullptr.");
        return MMS_NOT_READY;
    }

    if (mPublisherService->Start() != 0) {
        NET_LOG_ERROR("Failed to start NetService.");
        return MMS_INNER_ERR;
    }

    NET_LOG_INFO("PublisherService Started!");
    return MMS_OK;
}

BResult NetMulticastEngine::InitPublisher(const std::string &oobIp, uint16_t oobPort, const std::string &ipMask)
{
    BResult ret = MMS_INNER_ERR;
    ret = CreatePublisherService(oobIp, oobPort, ipMask);
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Create publisher service failed, ret:" << ret << ", ip:" << oobIp << ", port:" << oobPort
                                                              << ".");
        return ret;
    }

    ret = StartPublisherService();
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Start publisher service failed, ret:" << ret << ", ip:" << oobIp << ", port:" << oobPort << ".");
        return ret;
    }

    if (UNLIKELY(mPublisherService->CreatePublisher(mPublisher) != ock::hcom::SER_OK || mPublisher == nullptr)) {
        NET_LOG_ERROR("Create publisher failed.");
        return MMS_INNER_ERR;
    }

    NET_LOG_INFO("Init publisher success, ip" << oobIp << ", port:" << oobPort << ", ipMask:" << ipMask << ".");
    return MMS_OK;
}

std::string GetIpFromIpPort(const std::string &ipPort)
{
    size_t pos = ipPort.rfind(':');
    if (pos == std::string::npos) {
        return ipPort;
    }
    return ipPort.substr(0, pos);
}

void NetMulticastEngine::SubscriberBrokenCallBack(const ock::hcom::UBSHcomNetEndpointPtr &ep)
{
    uint16_t peerNodeId = static_cast<uint16_t>(ep->UpCtx());
    NET_LOG_WARN("Subscriber ep broken id " << ep->Id() << ", peer nodeId:" << peerNodeId
                                            << ", peer ip and port:" << ep->PeerIpAndPort() << ".");
    std::string peerIp = GetIpFromIpPort(ep->PeerIpAndPort());
    {
        WriteLocker<ReadWriteLock> lock(&mSubScribersLock);
        mSubScribers.erase(peerIp);
    }

    if (mHandlerBroken) {
        mHandlerBroken(peerNodeId);
    }
}

void NetMulticastEngine::DestroyPublisherService()
{
    if (mPublisherService != nullptr) {
        mPublisherService->Stop();
        mPublisherService = nullptr;
    }

    auto ret = ock::hcom::PublisherService::Destroy(mPublisherServiceName);
    if (ret != ock::hcom::SER_OK) {
        NET_LOG_WARN("Destroy publisher service failed, service does not exist or service empty.");
    }
}

void NetMulticastEngine::DestroySubscriberService()
{
    if (mSubscriberService != nullptr) {
        mSubscriberService->Stop();
        mSubscriberService = nullptr;
    }

    auto ret = ock::hcom::SubscriberService::Destroy(mSubscriberServiceName);
    if (ret != ock::hcom::SER_OK) {
        NET_LOG_WARN("Destroy subscriber service failed, service does not exist or service empty.");
    }
}

BResult NetMulticastEngine::CreateSubscriberService(const std::string &ipMask)
{
    auto &opt = MmsConfig::Instance()->GetNetConfig();
    std::pair<long, long> cpuSet = opt.subscriberWorkerCpuSet;
    MulticastServiceOptions options;
    options.enableTls = opt.tlsEnable;
    options.maxSendRecvDataSize = opt.msgMaxBuffSize + HCOM_TLS_HEADER_COST;
    options.workerGroupMode = ock::hcom::NET_BUSY_POLLING;
    options.workerGroupCpuIdsRange = std::make_pair(static_cast<uint32_t>(cpuSet.first),
                                                    static_cast<uint32_t>(cpuSet.second));
    options.workerGroupThreadCount = static_cast<uint16_t>(cpuSet.second - cpuSet.first + NO_1);
    options.qpRecvQueueSize = NO_4096;
    options.qpSendQueueSize = NO_4096;
    options.qpPrePostSize = NO_2048;
    options.qpBatchRePostSize = NO_2;
    options.publisherWrkGroupNo = Cm::Instance()->GetLocalNid() % mPublisherWorkGroupNum;
    if (opt.multicastProtocol == "rdma") {
        options.protocol = UBSHcomNetDriverProtocol::RDMA;
    } else {
        options.protocol = UBSHcomNetDriverProtocol::TCP;
    }

    mSubscriberServiceName = "Subscriber";
    mSubscriberService = ock::hcom::SubscriberService::Create(mSubscriberServiceName, options);
    if (mSubscriberService == nullptr) {
        NET_LOG_ERROR("Failed to create subscriber service.");
        return MMS_INNER_ERR;
    }

    if (options.enableTls) {
        auto result = PrepareTlsDecrypter(opt.decrypterLibPath);
        if (result != MMS_OK) {
            NET_LOG_ERROR("Failed to prepare tls decrypter, result:" << result << ".");
            DestroySubscriberService();
            return result;
        }
        SetDriverTlsCallback(mSubscriberService, opt);
    }

    mSubscriberService->GetConfig().SetDeviceIpMask({ipMask});
    mSubscriberService->RegisterRecvHandler(
        std::bind(&NetEngine::RequestInnerReceived, mNetEngine.Get(), std::placeholders::_1));
    mSubscriberService->RegisterBrokenHandler(
        std::bind(&NetMulticastEngine::SubscriberBrokenCallBack, this, std::placeholders::_1));

    NET_LOG_INFO("Create subscriber service success, group number:"
                 << static_cast<uint16_t>(options.publisherWrkGroupNo) << ".");
    return MMS_OK;
}

BResult NetMulticastEngine::StartSubscriberService()
{
    if (mSubscriberService == nullptr) {
        NET_LOG_WARN("Subscriber service not ready.");
        return MMS_NOT_READY;
    }
    if (mSubscriberService->Start() != MMS_OK) {
        NET_LOG_ERROR("Failed to start subscriber service.");
        return MMS_INNER_ERR;
    }

    NET_LOG_INFO("Start subscriber service success!");
    return MMS_OK;
}

BResult NetMulticastEngine::InitSubscriberService(const std::string &ipMask)
{
    BResult ret = CreateSubscriberService(ipMask);
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Create subscriber service failed, ret:" << ret << ".");
        return ret;
    }

    ret = StartSubscriberService();
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Start subscriber service failed, ret:" << ret << ".");
        DestroySubscriberService();
        return ret;
    }

    NET_LOG_INFO("Init subscriber service success.");
    return MMS_OK;
}

BResult NetMulticastEngine::CreateSubscriber(uint16_t peerNodeId, const ::std::string &ip, uint16_t port)
{
    WriteLocker<ReadWriteLock> lock(&mSubScribersLock);
    if (mSubscriberService == nullptr) {
        BResult ret = InitSubscriberService(mIpMask);
        if (UNLIKELY(ret != MMS_OK)) {
            NET_LOG_ERROR("Init subscriber service failed, ret:" << ret << ".");
            return ret;
        }
    }

    if (mSubscriberService == nullptr) {
        NET_LOG_WARN("Subscriber service not ready.");
        return MMS_NOT_READY;
    }

    std::string url = "tcp://" + ip + ":" + std::to_string(port);
    if (mSubScribers.find(ip) != mSubScribers.end()) {
        NET_LOG_INFO("Subscriber to " << url << " is exist, skip."
                                      << ".");
        return MMS_OK;
    };

    ock::hcom::NetRef<ock::hcom::Subscriber> subscriber = nullptr;
    if ((mSubscriberService->CreateSubscriber(url, subscriber) != ock::hcom::SER_OK) || (subscriber == nullptr)) {
        NET_LOG_ERROR("Subscription failed, url:" << url << ".");
        return MMS_NET_RETRY;
    }

    subscriber->GetEp()->UpCtx(static_cast<uint64_t>(peerNodeId));  // 记录publisher的nodeId
    mSubScribers.emplace(ip, subscriber);
    NET_LOG_INFO("subscribed to node success, url:" << url << ", epId:" << subscriber->GetEp()->Id() << ".");
    return MMS_OK;
}

bool NetMulticastEngine::IsSubscriberExist(const std::string &ip)
{
    ReadLocker<ReadWriteLock> lock(&mSubScribersLock);
    if (mSubScribers.find(ip) != mSubScribers.end()) {
        return true;
    }

    return false;
}

bool NetMulticastEngine::CheckConnectDone(uint32_t onlineNodesNum)
{
    mSubScribersLock.LockRead();
    if (mSubScribers.size() != (onlineNodesNum - NO_1)) {
        mSubScribersLock.UnLock();
        return false;
    }
    mSubScribersLock.UnLock();

    mSubScribersRemoteLock.LockRead();
    if (mSubScribersRemote.size() != (onlineNodesNum - NO_1)) {
        mSubScribersRemoteLock.UnLock();
        return false;
    }
    mSubScribersRemoteLock.UnLock();

    return true;
}

void NetMulticastEngine::WaitForConnectDone()
{
    uint32_t onlineNodesNum = Cm::Instance()->GetOnlineNodesNum();
    uint64_t startTime = Monotonic::TimeSec();
    bool connectDone = false;
    while (true) {
        connectDone = CheckConnectDone(onlineNodesNum);
        if (connectDone) {
            LOG_INFO("Multicast connect done, online node num:" << onlineNodesNum << ".");
            break;
        }

        LOG_INFO("Multicast connect not done, sleep 1s...");
        std::this_thread::sleep_for(std::chrono::seconds(CONNECT_DONE_RETRY_INTERAL));
        uint64_t costTime = Monotonic::TimeSec() - startTime;
        if (costTime >= MULTICAST_CONNECT_TIME_OUT) {
            LOG_WARN("Multicast connect timeout after " << MULTICAST_CONNECT_TIME_OUT << "s.");
            return;
        }
    }
}

BResult NetMulticastEngine::InitMemoryRegister()
{
    auto ret = mPublisherService->RegisterMemoryRegion(mMemList.address[MMAP_AREA_IOCTX_INDEX],
                                                       mMemList.size[MMAP_AREA_IOCTX_INDEX],
                                                       mMemList.mr[MMAP_AREA_IOCTX_INDEX]);
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Register memory failed, ret:" << ret << ", size:" << mMemList.size[MMAP_AREA_IOCTX_INDEX]
                                                     << ".");
        return MMS_INNER_ERR;
    }

    NET_LOG_INFO("Register ioctx memory success, size:" << mMemList.size[MMAP_AREA_IOCTX_INDEX] << ", key:"
                                                        << mMemList.mr[MMAP_AREA_IOCTX_INDEX]->GetLKey() << ".");
    return MMS_OK;
}

BResult NetMulticastEngine::InitMulticast(int16_t timeoutSec, const std::string &oobIp, uint16_t oobPort,
                                          const std::string &ipMask, MulticastNetMemList &memList)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        NET_LOG_WARN("Net engine has been already initialized.");
        return MMS_OK;
    }

    mNetEngine = MmsServer::Instance()->GetNetEngine();
    mTimeout = timeoutSec;
    mMemList = memList;
    mIpMask = ipMask;
    BResult ret = MMS_INNER_ERR;

    // 1、init publisher
    ret = InitPublisher(oobIp, oobPort, ipMask);
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Init publisher failed, ret:" << ret << ", ip:" << oobIp << ", port:" << oobPort << ".");
        return ret;
    }

    // 2、register ioctx memory region
    ret = InitMemoryRegister();
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Register memory failed, ret:" << ret << ".");
        return ret;
    }

    // 3、init multicast connector
    mConnector = MakeRef<MultiNetConnector>(this);
    if (mConnector == nullptr) {
        NET_LOG_ERROR("Make multicast net connector failed.");
        return MMS_ALLOC_FAIL;
    }

    ret = mConnector->Start();
    if (ret != MMS_OK) {
        NET_LOG_ERROR("Failed to start multicast net connector, ret:" << ret << ".");
        return ret;
    }

    mStarted = true;
    NET_LOG_INFO("Init multicast success.");
    return MMS_OK;
}

void NetMulticastEngine::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    if (mConnector != nullptr) {
        mConnector->Stop();
        mConnector = nullptr;
    }

    DestroyPublisherService();
    DestroySubscriberService();

    NET_LOG_INFO("Stop multicast net done.");
}

BResult NetMulticastEngine::SyncConnect(SubscriptionInfo &info)
{
    if (UNLIKELY(mConnector == nullptr)) {
        NET_LOG_ERROR("Multicast net Connector net ready.");
        return MMS_NOT_READY;
    }
    return mConnector->SyncConnect(info);
}

BResult NetMulticastEngine::AsyncConnect(SubscriptionInfo &info, MulticastAsyncHandler &handler)
{
    if (UNLIKELY(mConnector == nullptr)) {
        NET_LOG_ERROR("Multicast net Connector net ready.");
        return MMS_NOT_READY;
    }
    return mConnector->AsyncConnect(info, handler);
}

std::string NetMulticastEngine::GetMulticastInfoStr()
{
    std::ostringstream oss;
    std::vector<SubscriptionInfoPtr> subscribers = mPublisher->GetAllSubscriberInfo();
    uint16_t count = NO_1;
    oss << "[The local publisher's subscribers:]" << std::endl;
    for (auto &item : subscribers) {
        oss << "subscriber " << count++ << ":"
            << " ip:" << item->GetIp() << ", port:" << item->GetPort() << ", epId:" << item->GetId() << std::endl;
    }

    count = NO_1;
    oss << "[The publishers that this node has subscribed:]" << std::endl;
    ReadLocker<ReadWriteLock> lock(&mSubScribersLock);
    for (auto &item : mSubScribers) {
        oss << "publisher " << count++ << ":"
            << " ip:" << item.second->GetIp() << ", port:" << item.second->GetPort()
            << ", epId:" << item.second->GetEp()->Id() << std::endl;
    }

    return oss.str();
}

bool NetMulticastEngine::RemoteSendCheck(const std::unordered_set<std::string> &remoteIps)
{
    if (remoteIps.empty()) {
        return false;
    }

    ReadLocker<ReadWriteLock> lock(&mSubScribersRemoteLock);
    for (auto &ip : remoteIps) {
        if (mSubScribersRemote.find(ip) == mSubScribersRemote.end()) {
            return false;
        }
    }

    return true;
}

bool NetMulticastEngine::CheckRemoteNodeStatus(const std::string &remoteIp)
{
    CmPtInfo ptInfo = Cm::Instance()->GetLocalPtInfo();
    auto nodeInfos = Cm::Instance()->GetNodeView();

    for (const auto &copy : ptInfo.copys) {
        auto it = nodeInfos.find(copy.nodeId);
        if (it == nodeInfos.end() || it->second.ip != remoteIp) {
            continue;
        }
        return copy.state == CM_COPY_RUNNING;
    }

    return false;
}

void NetMulticastEngine::MulticastAsyncCallBuff(void *req, uint32_t reqLen, Callback callback)
{
    ock::hcom::UBSHcomNetTransOpInfo opInfo;
    opInfo.timeout = mTimeout;
    ock::hcom::MultiRequest multiReq(req, reqLen, mMemList.mr[MMAP_AREA_IOCTX_INDEX]->GetLKey());

    uint64_t timeStart = Monotonic::TimeNs();
    auto *netCallback = ock::hcom::NewMultiCastCallback(
        [this, callback, timeStart](PublisherContext  &context) {
            const std::vector<ock::hcom::SubscriberRspInfo> &infos = context.GetSubscriberRspInfo();
            MMS_TRACE_ASYNC_END(NET_HCOM_MULTICAST_SEND, MMS_OK, timeStart);
            for (auto &item : infos) {
                if (item.GetStatus() != ock::hcom::SubscriberRspStatus::SUCCESS) {
                    std::string remoteIp = item.GetSubInfos()->GetIp();
                    if (!CheckRemoteNodeStatus(remoteIp)) {
                        NET_LOG_WARN("remote node not running, skip, remote node:" << remoteIp << ", status:"
                                                                     << static_cast<int>(item.GetStatus()) << ".");
                        continue;
                    }

                    callback.cb(callback.cbCtx, nullptr, 0, MMS_INNER_RETRY);  // 一个失败就全部失败
                    NET_LOG_ERROR("Request failed, remote node:" << remoteIp << ", status:"
                                                                 << static_cast<int>(item.GetStatus()) << ".");
                    return;
                }

                int32_t opRet = *(static_cast<int32_t *>(item.GetMultiResponse().data));
                if (opRet != MMS_OK) {  // 请求对应的处理失败了
                    callback.cb(callback.cbCtx, nullptr, 0, opRet);
                    NET_LOG_ERROR("Request failed, remote node:" << item.GetSubInfos()->GetIp()
                                                                 << ", ret:" << opRet << ".");
                    return;
                }
            }
            callback.cb(callback.cbCtx, nullptr, 0, MMS_OK);
        },
        std::placeholders::_1);

    MMS_TRACE_ASYNC_BEGIN(NET_HCOM_MULTICAST_SEND);
    auto ret = mPublisher->Call(opInfo, multiReq, netCallback);
    if (ret != MMS_OK) {
        NET_LOG_ERROR("Multicast async call failed, ret:" << ret << ", error:" << UBSHcomNetErrStr(ret) << ".");
        callback.cb(callback.cbCtx, nullptr, 0, MMS_NET_RETRY);
    }
}

void NetMulticastEngine::Reply(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize)
{
    int32_t result = MMS_ERR;
    auto subCtx = dynamic_cast<ock::hcom::SubscriberContext *>(&ctx);
    if (subCtx == nullptr) {
        NET_LOG_ERROR("subCtx is nullptr.");
        return;
    }

    if (resp != nullptr) {
        result = subCtx->Reply({resp, respSize});
    } else {
        result = subCtx->Reply({&retCode, sizeof(retCode)});
    }
    if (UNLIKELY(result != MMS_OK)) {
        NET_LOG_ERROR("Reply Send failed, ret:" << result << ".");
    }
}

BResult NetMulticastEngine::PrepareTlsDecrypter(const std::string &decrypterLibPath)
{
    const auto decrypter = TlsUtil::LoadDecryptFunction(decrypterLibPath.c_str());
    if (decrypter == nullptr) {
        LOG_ERROR("Failed to load customized decrypt function.");
        return MMS_INVALID_PARAM;
    }

    RegisterDecryptHandler(decrypter);
    return MMS_OK;
}

PrivateKeyCallback NetMulticastEngine::CreatePrivateKeyCallback(const MmsConfig::NetConfig &options)
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

void NetMulticastEngine::SetDriverTlsCallback(ock::hcom::SubscriberService *driver, const MmsConfig::NetConfig &options)
{
    driver->RegisterTLSCertificationCallback([&options](const std::string &name, std::string &path) {
        path = options.certificationPath;
        NET_LOG_INFO("Get client cert success.");
        return true;
    });

    driver->RegisterTLSCaCallback([&options](const std::string &name, std::string &capath, std::string &crlPath,
                                             UBSHcomPeerCertVerifyType &verifyPeerCert,
                                             UBSHcomTLSCertVerifyCallback &cb) {
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

    driver->RegisterTLSPrivateKeyCallback([this, &options](const std::string &name, std::string &path, void *&pwd,
                                                           int &len, UBSHcomTLSEraseKeypass &erase) {
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
    });
}

void NetMulticastEngine::SetDriverTlsCallback(ock::hcom::PublisherService *driver, const MmsConfig::NetConfig &options)
{
    driver->RegisterTLSCertificationCallback([&options](const std::string &name, std::string &path) {
        path = options.certificationPath;
        NET_LOG_INFO("Get client cert success.");
        return true;
    });

    driver->RegisterTLSCaCallback([&options](const std::string &name, std::string &capath, std::string &crlPath,
                                             UBSHcomPeerCertVerifyType &verifyPeerCert,
                                             UBSHcomTLSCertVerifyCallback &cb) {
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

    driver->RegisterTLSPrivateKeyCallback(CreatePrivateKeyCallback(options));
}
}
}
