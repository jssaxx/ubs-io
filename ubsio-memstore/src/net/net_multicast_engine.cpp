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

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <thread>
#include "securec.h"
#include "mms_server.h"
#include "mms_trace.h"
#include "net_multicast_engine.h"

namespace ock {
namespace mms {
using namespace ock::hcom;

static constexpr uint8_t HCOM_TLS_HEADER_COST = 128;
static constexpr uint32_t MULTICAST_IO_CONTEXT_COUNT = 65536;
constexpr uint16_t MULTICAST_CONNECT_TIME_OUT = 60; // 组播建链超时时间(s)
constexpr uint16_t CONNECT_DONE_RETRY_INTERAL = 1;

struct MulticastSyncCallCtx {
    std::mutex mutex;
    std::condition_variable cond;
    BResult result = MMS_INNER_ERR;
    bool done = false;
    void *resp = nullptr;
    uint32_t respLen = 0;
};

static bool SplitPublisherWorkerGroup(const std::pair<uint32_t, uint32_t> &cpuSet, uint16_t groupNum,
                                      std::vector<std::pair<uint32_t, uint32_t>> &workerGroups)
{
    uint32_t cpuCount = cpuSet.second - cpuSet.first + NO_1;
    if (cpuCount < groupNum) {
        return false;
    }
    uint32_t cpuId = cpuSet.first;
    uint32_t baseWorkerNum = cpuCount / groupNum;
    uint32_t remainder = cpuCount % groupNum;
    for (uint16_t groupIdx = NO_0; groupIdx < groupNum; ++groupIdx) {
        uint32_t workerNum = baseWorkerNum + static_cast<uint32_t>(groupIdx < remainder);
        workerGroups.emplace_back(cpuId, cpuId + workerNum - NO_1);
        cpuId += workerNum;
    }
    return true;
}

static bool BuildPublisherWorkerGroups(const std::vector<std::pair<uint32_t, uint32_t>> &cpuSets,
                                       uint16_t groupNum,
                                       std::vector<std::pair<uint32_t, uint32_t>> &workerGroups)
{
    if (cpuSets.size() == static_cast<size_t>(groupNum)) {
        workerGroups = cpuSets;
        return true;
    }
    if (cpuSets.size() != static_cast<size_t>(NO_1) || groupNum == NO_0) {
        return false;
    }
    return SplitPublisherWorkerGroup(cpuSets.front(), groupNum, workerGroups);
}

static uint16_t GetPublisherGroupIndex(uint16_t localNodeId, uint16_t peerNodeId)
{
    return localNodeId < peerNodeId ? localNodeId : static_cast<uint16_t>(localNodeId - NO_1);
}

static void AddPublisherWorkerGroups(ock::hcom::PublisherService *publisherService,
                                     const std::vector<std::pair<uint32_t, uint32_t>> &workerGroups)
{
    for (uint16_t groupIdx = NO_1; groupIdx < static_cast<uint16_t>(workerGroups.size()); ++groupIdx) {
        const auto &cpuSet = workerGroups[groupIdx];
        uint16_t workerNum = static_cast<uint16_t>(cpuSet.second - cpuSet.first + NO_1);
        publisherService->AddWorkerGroup(groupIdx, workerNum, cpuSet, NO_0);
    }
}

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
    mSubScribersRemote[info->GetIp()]++;
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
    if (info.Get() == nullptr) {
        NET_LOG_WARN("Subscription info is null, ep id:" << ep->Id() << ".");
        return;
    }
    mPublisher->DelSubscription(info);

    mSubScribersRemoteLock.LockWrite();
    auto it = mSubScribersRemote.find(info->GetIp());
    if (it != mSubScribersRemote.end()) {
        if (it->second <= NO_1) {
            mSubScribersRemote.erase(it);
        } else {
            it->second--;
        }
    }
    mSubScribersRemoteLock.UnLock();
}

BResult NetMulticastEngine::CreatePublisherService(const std::string &oobIp, uint16_t oobPort,
                                                   const std::string &ipMask)
{
    auto &opt = MmsConfig::Instance()->GetNetConfig();
    const auto &cpuSets = opt.publisherWorkerCpuSets;
    if (cpuSets.empty()) {
        NET_LOG_ERROR("Publisher worker cpu set is empty.");
        return MMS_INVALID_PARAM;
    }

    uint16_t nodeNum = static_cast<uint16_t>(MmsConfig::Instance()->GetCmConfig().nodeNum);
    uint16_t requiredGroupNum = static_cast<uint16_t>(nodeNum - NO_1);
    std::vector<std::pair<uint32_t, uint32_t>> workerGroups;
    if (UNLIKELY(!BuildPublisherWorkerGroups(cpuSets, requiredGroupNum, workerGroups))) {
        NET_LOG_ERROR("Invalid publisher worker cpu set, range count:" << cpuSets.size()
                                                                       << ", required group count:"
                                                                       << requiredGroupNum << ".");
        return MMS_INVALID_PARAM;
    }

    const auto &firstGroup = workerGroups.front();
    MulticastServiceOptions options;
    options.maxSendRecvDataSize = opt.msgMaxBuffSize + HCOM_TLS_HEADER_COST;
    options.multicastIoContextCount = MULTICAST_IO_CONTEXT_COUNT;
    options.enableTls = opt.tlsEnable;
    options.workerGroupCpuIdsRange = firstGroup;
    options.workerGroupThreadCount = static_cast<uint16_t>(firstGroup.second - firstGroup.first + NO_1);
    options.workerGroupMode = ock::hcom::NET_BUSY_POLLING;
    options.qpRecvQueueSize = NO_4096;
    options.qpSendQueueSize = NO_4096;
    options.qpPrePostSize = NO_2048;
    options.qpBatchRePostSize = NO_64;
    options.maxSubscriberNum = (MAX_NODES_NUM - 1) * opt.subscriberConnectCount;
    if (opt.multicastProtocol == "rdma") {
        options.protocol = UBSHcomNetDriverProtocol::RDMA;
    } else {
        options.protocol = UBSHcomNetDriverProtocol::TCP;
    }

    mPublisherWorkGroupNum = static_cast<uint16_t>(workerGroups.size());
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

    AddPublisherWorkerGroups(mPublisherService, workerGroups);

    std::string url = "tcp://" + oobIp + ":" + std::to_string(oobPort);
    mPublisherService->GetConfig().SetDeviceIpMask({ipMask});
    mPublisherService->GetConfig().SetPeriodicThreadNum(NO_4);
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
        auto it = mSubScribers.find(peerIp);
        if (it != mSubScribers.end()) {
            auto &subscribers = it->second;
            subscribers.erase(std::remove_if(subscribers.begin(), subscribers.end(),
                [&ep](const ock::hcom::SubscriberPtr &subscriber) {
                    return subscriber.Get() == nullptr || subscriber->GetEp()->Id() == ep->Id();
                }), subscribers.end());
            if (subscribers.empty()) {
                mSubScribers.erase(it);
            }
        }
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
    options.publisherWrkGroupNo = static_cast<uint8_t>(Cm::Instance()->GetLocalNid() % mPublisherWorkGroupNum);
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
    auto connectCount = MmsConfig::Instance()->GetNetConfig().subscriberConnectCount;
    uint16_t localNodeId = Cm::Instance()->GetLocalNid();
    uint16_t nodeNum = static_cast<uint16_t>(MmsConfig::Instance()->GetCmConfig().nodeNum);
    if (UNLIKELY(localNodeId >= nodeNum || peerNodeId >= nodeNum || localNodeId == peerNodeId)) {
        NET_LOG_ERROR("Invalid node for subscriber, local node:" << localNodeId << ", peer node:" << peerNodeId
                                                                 << ", node count:" << nodeNum << ".");
        return MMS_INVALID_PARAM;
    }
    uint16_t publisherGroupIdx = GetPublisherGroupIndex(localNodeId, peerNodeId);
    auto it = mSubScribers.find(ip);
    if (it != mSubScribers.end() && it->second.size() >= connectCount) {
        NET_LOG_INFO("Subscriber to " << url << " is exist, count:" << it->second.size() << ", skip.");
        return MMS_OK;
    }

    ock::hcom::NetRef<ock::hcom::Subscriber> subscriber = nullptr;
    if ((mSubscriberService->CreateSubscriber(url, publisherGroupIdx, subscriber) != ock::hcom::SER_OK) ||
        (subscriber == nullptr)) {
        NET_LOG_ERROR("Subscription failed, url:" << url << ".");
        return MMS_NET_RETRY;
    }

    subscriber->GetEp()->UpCtx(static_cast<uint64_t>(peerNodeId));  // 记录publisher的nodeId
    auto &subscribers = mSubScribers[ip];
    subscribers.emplace_back(subscriber);
    NET_LOG_INFO("Subscribed to node success, url:" << url << ", epId:" << subscriber->GetEp()->Id()
                                                    << ", count:" << subscribers.size() << "/" << connectCount
                                                    << ", publisherGroupIdx:" << publisherGroupIdx << ".");
    return MMS_OK;
}

bool NetMulticastEngine::IsSubscriberExist(const std::string &ip)
{
    ReadLocker<ReadWriteLock> lock(&mSubScribersLock);
    auto it = mSubScribers.find(ip);
    if (it != mSubScribers.end() &&
        it->second.size() >= MmsConfig::Instance()->GetNetConfig().subscriberConnectCount) {
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
        for (auto &subscriber : item.second) {
            if (subscriber.Get() == nullptr) {
                continue;
            }
            oss << "publisher " << count++ << ":"
                << " ip:" << subscriber->GetIp() << ", port:" << subscriber->GetPort()
                << ", epId:" << subscriber->GetEp()->Id() << std::endl;
        }
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

static BResult CopyMulticastSyncResponse(const ock::hcom::MultiResponse &multiResp, void *resp, uint32_t respLen)
{
    if (UNLIKELY(multiResp.data == nullptr || multiResp.size < respLen)) {
        NET_LOG_ERROR("Invalid multicast response, response size:" << multiResp.size << ", expected size:" << respLen
                                                                   << ".");
        return MMS_INNER_ERR;
    }

    int32_t ret = memcpy_s(resp, respLen, multiResp.data, respLen);
    if (UNLIKELY(ret != EOK)) {
        NET_LOG_ERROR("Copy multicast response failed, ret:" << ret << ", response size:" << multiResp.size
                                                             << ", expected size:" << respLen << ".");
        return MMS_INNER_ERR;
    }
    return MMS_OK;
}

static BResult CopyMulticastDynamicResponse(const ock::hcom::MultiResponse &multiResp, MulticastSyncResponse &resp)
{
    if (UNLIKELY(multiResp.data == nullptr || multiResp.size == 0)) {
        NET_LOG_ERROR("Invalid multicast response, response size:" << multiResp.size << ".");
        return MMS_INNER_ERR;
    }

    void *data = malloc(multiResp.size);
    if (UNLIKELY(data == nullptr)) {
        NET_LOG_ERROR("Alloc multicast response failed, response size:" << multiResp.size << ".");
        return MMS_ALLOC_FAIL;
    }

    int32_t ret = memcpy_s(data, multiResp.size, multiResp.data, multiResp.size);
    if (UNLIKELY(ret != EOK)) {
        NET_LOG_ERROR("Copy multicast response failed, ret:" << ret << ", response size:" << multiResp.size << ".");
        free(data);
        return MMS_INNER_ERR;
    }

    resp.data = data;
    resp.len = multiResp.size;
    return MMS_OK;
}

static BResult GetMulticastRemoteIp(uint16_t dstNode, std::string &remoteIp)
{
    uint16_t remotePort = 0;
    if (UNLIKELY(!Cm::Instance()->CheckIsOnlineMulti(dstNode, remoteIp, remotePort))) {
        NET_LOG_WARN("Remote multicast node is offline, node id:" << dstNode << ".");
        return MMS_NET_RETRY;
    }
    return MMS_OK;
}

static void FinishMulticastSyncCall(MulticastSyncCallCtx &syncCtx, BResult result)
{
    std::lock_guard<std::mutex> lock(syncCtx.mutex);
    syncCtx.result = result;
    syncCtx.done = true;
    syncCtx.cond.notify_one();
}

static BResult GetSingleMulticastResponse(NetMulticastEngine *engine, const ock::hcom::SubscriberRspInfo &item)
{
    if (UNLIKELY(item.GetStatus() != ock::hcom::SubscriberRspStatus::SUCCESS)) {
        std::string remoteIp = item.GetSubInfos()->GetIp();
        if (!engine->CheckRemoteNodeStatus(remoteIp)) {
            NET_LOG_WARN("Remote node not running, remote node:" << remoteIp << ", status:"
                                                                 << static_cast<int>(item.GetStatus()) << ".");
        } else {
            NET_LOG_ERROR("Request failed, remote node:" << remoteIp << ", status:"
                                                         << static_cast<int>(item.GetStatus()) << ".");
        }
        return MMS_NET_RETRY;
    }

    const auto &multiResp = item.GetMultiResponse();
    if (UNLIKELY(multiResp.data == nullptr || multiResp.size < sizeof(int32_t))) {
        NET_LOG_ERROR("Invalid multicast response, response size:" << multiResp.size << ".");
        return MMS_INNER_ERR;
    }
    int32_t opRet = *(static_cast<int32_t *>(multiResp.data));
    if (UNLIKELY(opRet != MMS_OK)) {
        NET_LOG_ERROR("Request failed, remote node:" << item.GetSubInfos()->GetIp() << ", ret:" << opRet << ".");
    }
    return opRet;
}

static void HandleMulticastAsyncResponse(NetMulticastEngine *engine, const Callback &callback,
                                         PublisherContext &context, uint64_t timeStart)
{
    const std::vector<ock::hcom::SubscriberRspInfo> &infos = context.GetSubscriberRspInfo();
    MMS_TRACE_ASYNC_END(NET_HCOM_MULTICAST_SEND, MMS_OK, timeStart);
    if (UNLIKELY(infos.empty())) {
        callback.cb(callback.cbCtx, nullptr, 0, MMS_NET_RETRY);
        return;
    }

    BResult ret = GetSingleMulticastResponse(engine, infos.front());
    callback.cb(callback.cbCtx, nullptr, 0, ret);
}

static void HandleMulticastDynamicResponse(MulticastSyncCallCtx &syncCtx, MulticastSyncResponse &resp,
                                           PublisherContext &context)
{
    const std::vector<ock::hcom::SubscriberRspInfo> &infos = context.GetSubscriberRspInfo();
    if (UNLIKELY(infos.empty())) {
        FinishMulticastSyncCall(syncCtx, MMS_NET_RETRY);
        return;
    }

    const auto &item = infos.front();
    if (UNLIKELY(item.GetStatus() != ock::hcom::SubscriberRspStatus::SUCCESS)) {
        std::string remoteIp = item.GetSubInfos()->GetIp();
        NET_LOG_ERROR("Request failed, remote node:" << remoteIp << ", status:"
                                                     << static_cast<int>(item.GetStatus()) << ".");
        FinishMulticastSyncCall(syncCtx, MMS_NET_RETRY);
        return;
    }
    FinishMulticastSyncCall(syncCtx, CopyMulticastDynamicResponse(item.GetMultiResponse(), resp));
}

BResult NetMulticastEngine::MulticastSyncCallBuff(uint16_t dstNode, uint16_t opCode, void *req, uint32_t reqLen,
                                                  void *resp, uint32_t respLen)
{
    if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER || req == nullptr || reqLen == 0 || resp == nullptr || respLen == 0)) {
        NET_LOG_ERROR("Invalid multicast sync call param, opcode:" << opCode << ", req len:" << reqLen
                                                                   << ", resp len:" << respLen << ".");
        return MMS_INVALID_PARAM;
    }
    if (UNLIKELY(mPublisher == nullptr || mMemList.mr[MMAP_AREA_IOCTX_INDEX] == nullptr)) {
        NET_LOG_ERROR("Multicast sync call is not ready.");
        return MMS_NOT_READY;
    }

    std::string remoteIp;
    auto result = GetMulticastRemoteIp(dstNode, remoteIp);
    if (UNLIKELY(result != MMS_OK)) {
        return result;
    }

    ock::hcom::UBSHcomNetTransOpInfo opInfo;
    opInfo.timeout = mTimeout;
    ock::hcom::MultiRequest multiReq(req, reqLen, mMemList.mr[MMAP_AREA_IOCTX_INDEX]->GetLKey());
    MulticastSyncCallCtx syncCtx;
    syncCtx.resp = resp;
    syncCtx.respLen = respLen;

    auto *netCallback = ock::hcom::NewMultiCastCallback(
        [this, &syncCtx](PublisherContext &context) {
            const std::vector<ock::hcom::SubscriberRspInfo> &infos = context.GetSubscriberRspInfo();
            if (UNLIKELY(infos.empty())) {
                FinishMulticastSyncCall(syncCtx, MMS_NET_RETRY);
                return;
            }

            const auto &item = infos.front();
            if (UNLIKELY(item.GetStatus() != ock::hcom::SubscriberRspStatus::SUCCESS)) {
                std::string remoteIp = item.GetSubInfos()->GetIp();
                if (!CheckRemoteNodeStatus(remoteIp)) {
                    NET_LOG_WARN("Remote node not running, remote node:" << remoteIp << ", status:"
                                                                         << static_cast<int>(item.GetStatus()) << ".");
                } else {
                    NET_LOG_ERROR("Request failed, remote node:" << remoteIp << ", status:"
                                                                 << static_cast<int>(item.GetStatus()) << ".");
                }
                FinishMulticastSyncCall(syncCtx, MMS_NET_RETRY);
                return;
            }

            FinishMulticastSyncCall(syncCtx,
                                    CopyMulticastSyncResponse(item.GetMultiResponse(), syncCtx.resp, syncCtx.respLen));
        },
        std::placeholders::_1);
    if (UNLIKELY(netCallback == nullptr)) {
        NET_LOG_ERROR("Create multicast sync callback failed.");
        return MMS_ALLOC_FAIL;
    }

    auto ret = mPublisher->Call(opInfo, remoteIp, multiReq, netCallback);
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Multicast sync call failed, ret:" << ret << ", error:" << UBSHcomNetErrStr(ret)
                                                         << ", remote ip:" << remoteIp << ".");
        return MMS_NET_RETRY;
    }

    std::unique_lock<std::mutex> lock(syncCtx.mutex);
    syncCtx.cond.wait(lock, [&syncCtx]() { return syncCtx.done; });
    return syncCtx.result;
}

BResult NetMulticastEngine::MulticastSyncCallBuff(uint16_t dstNode, uint16_t opCode, void *req, uint32_t reqLen,
                                                  MulticastSyncResponse &resp)
{
    if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER || req == nullptr || reqLen == 0)) {
        NET_LOG_ERROR("Invalid multicast sync call param, opcode:" << opCode << ", req len:" << reqLen << ".");
        return MMS_INVALID_PARAM;
    }
    if (UNLIKELY(mPublisher == nullptr || mMemList.mr[MMAP_AREA_IOCTX_INDEX] == nullptr)) {
        NET_LOG_ERROR("Multicast sync call is not ready.");
        return MMS_NOT_READY;
    }

    std::string remoteIp;
    auto result = GetMulticastRemoteIp(dstNode, remoteIp);
    if (UNLIKELY(result != MMS_OK)) {
        return result;
    }

    ock::hcom::UBSHcomNetTransOpInfo opInfo;
    opInfo.timeout = mTimeout;
    ock::hcom::MultiRequest multiReq(req, reqLen, mMemList.mr[MMAP_AREA_IOCTX_INDEX]->GetLKey());
    MulticastSyncCallCtx syncCtx;
    resp.data = nullptr;
    resp.len = 0;

    auto *netCallback = ock::hcom::NewMultiCastCallback(
        [&syncCtx, &resp](PublisherContext &context) {
            HandleMulticastDynamicResponse(syncCtx, resp, context);
        },
        std::placeholders::_1);
    if (UNLIKELY(netCallback == nullptr)) {
        NET_LOG_ERROR("Create multicast sync callback failed.");
        return MMS_ALLOC_FAIL;
    }

    auto ret = mPublisher->Call(opInfo, remoteIp, multiReq, netCallback);
    if (UNLIKELY(ret != MMS_OK)) {
        NET_LOG_ERROR("Multicast sync call failed, ret:" << ret << ", error:" << UBSHcomNetErrStr(ret)
                                                         << ", remote ip:" << remoteIp << ".");
        return MMS_NET_RETRY;
    }

    std::unique_lock<std::mutex> lock(syncCtx.mutex);
    syncCtx.cond.wait(lock, [&syncCtx]() { return syncCtx.done; });
    return syncCtx.result;
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

void NetMulticastEngine::MulticastAsyncCallBuff(uint16_t dstNode, void *req, uint32_t reqLen, Callback callback)
{
    if (UNLIKELY(req == nullptr || reqLen == 0)) {
        NET_LOG_ERROR("Invalid multicast async call param, node id:" << dstNode << ", req len:" << reqLen << ".");
        callback.cb(callback.cbCtx, nullptr, 0, MMS_INVALID_PARAM);
        return;
    }
    if (UNLIKELY(mPublisher == nullptr || mMemList.mr[MMAP_AREA_IOCTX_INDEX] == nullptr)) {
        NET_LOG_ERROR("Multicast async call is not ready.");
        callback.cb(callback.cbCtx, nullptr, 0, MMS_NOT_READY);
        return;
    }

    std::string remoteIp;
    auto result = GetMulticastRemoteIp(dstNode, remoteIp);
    if (UNLIKELY(result != MMS_OK)) {
        callback.cb(callback.cbCtx, nullptr, 0, result);
        return;
    }

    ock::hcom::UBSHcomNetTransOpInfo opInfo;
    opInfo.timeout = mTimeout;
    ock::hcom::MultiRequest multiReq(req, reqLen, mMemList.mr[MMAP_AREA_IOCTX_INDEX]->GetLKey());
    uint64_t timeStart = Monotonic::TimeNs();
    auto *netCallback = ock::hcom::NewMultiCastCallback(
        [this, callback, timeStart](PublisherContext &context) {
            HandleMulticastAsyncResponse(this, callback, context, timeStart);
        },
        std::placeholders::_1);
    if (UNLIKELY(netCallback == nullptr)) {
        NET_LOG_ERROR("Create multicast async callback failed.");
        callback.cb(callback.cbCtx, nullptr, 0, MMS_ALLOC_FAIL);
        return;
    }

    MMS_TRACE_ASYNC_BEGIN(NET_HCOM_MULTICAST_SEND);
    auto ret = mPublisher->Call(opInfo, remoteIp, multiReq, netCallback);
    if (ret != MMS_OK) {
        NET_LOG_ERROR("Multicast async call failed, ret:" << ret << ", error:" << UBSHcomNetErrStr(ret)
                                                          << ", remote ip:" << remoteIp << ".");
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
