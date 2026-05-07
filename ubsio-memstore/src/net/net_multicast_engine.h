/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef MMSCORE_NET_MULTICAST_ENGINE_H
#define MMSCORE_NET_MULTICAST_ENGINE_H

#include "net_common.h"
#include "mms_message.h"
#include "cm.h"
#include "hcom/multicast_publisher_service.h"
#include "hcom/multicast_publisher.h"
#include "hcom/multicast_subscriber_service.h"
#include "hcom/multicast_subscriber.h"
#include "net_engine.h"
#include "mms_config_instance.h"
#include "multicast_connector.h"

#include <cstdint>
#include <mutex>

using namespace ock::hcom;

namespace ock {
namespace mms {

constexpr uint8_t MMAP_AREA_IOCTX_INDEX = 0;

class NetMulticastEngine;
using NetMulticastEnginePtr = Ref<NetMulticastEngine>;
class NetMulticastEngine {
public:
    NetMulticastEngine() = default;
    ~NetMulticastEngine() = default;

    bool NewSubscriptionCallBack(ock::hcom::SubscriptionInfoPtr &info);
    void PublisherSubscriberEpBroken(const ock::hcom::UBSHcomNetEndpointPtr &ep);
    BResult CreatePublisherService(const std::string &oobIp, uint16_t oobPort, const std::string &ipMask);
    BResult StartPublisherService();
    BResult InitPublisher(const std::string &oobIp, uint16_t oobPort, const std::string &ipMask);

    void SubscriberBrokenCallBack(const UBSHcomNetEndpointPtr &ep);
    BResult CreateSubscriberService(const std::string &ipMask);
    BResult StartSubscriberService();
    BResult InitSubscriberService(const std::string &ipMask);
    BResult CreateSubscriber(uint16_t peerNodeId, const ::std::string &ip, uint16_t port);
    bool IsSubscriberExist(const std::string &ip);

    bool CheckConnectDone(uint32_t onlineNodesNum);
    void WaitForConnectDone();

    inline BResult RegisterSubscriberBrokenHandler(const EpSubscriberBrokenHandler &handle)
    {
        if (handle == nullptr) {
            return MMS_INVALID_PARAM;
        }

        std::lock_guard<std::mutex> guard(mMutex);
        if (UNLIKELY(mHandlerBroken != nullptr)) {
            NET_LOG_ERROR("Failed to register channel broken handler");
            return MMS_ERR;
        }

        mHandlerBroken = handle;
        return MMS_OK;
    }

    BResult InitMemoryRegister();
    BResult InitMulticast(int16_t timeoutSec, const std::string &oobIp, uint16_t oobPort, const std::string &ipMask,
                          MulticastNetMemList &memList);
    void Stop();

    BResult SyncConnect(SubscriptionInfo &info);
    BResult AsyncConnect(SubscriptionInfo &info, MulticastAsyncHandler &handler);

    std::string GetMulticastInfoStr();

    // 校验本次需要发送的节点是否都订阅了本节点
    bool RemoteSendCheck(const std::unordered_set<std::string> &remoteIps);

    PrivateKeyCallback CreatePrivateKeyCallback(const MmsConfig::NetConfig &options);

    void SetDriverTlsCallback(ock::hcom::SubscriberService *driver, const MmsConfig::NetConfig &options);

    void SetDriverTlsCallback(ock::hcom::PublisherService *driver, const MmsConfig::NetConfig &options);

    BResult PrepareTlsDecrypter(const std::string &decrypterLibPath);

    inline void RegisterDecryptHandler(const DecryptFunc &h)
    {
        mDecryptHandler = h;
    }

    template <typename TReq>
    void MulticastAsyncCall(const std::unordered_set<std::string> &remoteIps, TReq &req, Callback &callback)
    {
        if (!RemoteSendCheck(remoteIps)) {
            NET_LOG_WARN("Remote nodes are not fully ready.");
            callback.cb(callback.cbCtx, nullptr, 0, MMS_NET_RETRY);
            return;
        }

        ock::hcom::UBSHcomNetTransOpInfo opInfo;
        opInfo.timeout = mTimeout;
        ock::hcom::MultiRequest multiReq(static_cast<void *>(&req), sizeof(req),
                                         mMemList.mr[MMAP_AREA_IOCTX_INDEX]->GetLKey());

        auto *netCallback = ock::hcom::NewMultiCastCallback(
            [this, &remoteIps, callback](PublisherContext &context) {
                const std::vector<ock::hcom::SubscriberRspInfo> &infos = context.GetSubscriberRspInfo();
                for (auto &item : infos) {
                    bool isNeedNode = (remoteIps.find(item.GetSubInfos()->GetIp()) != remoteIps.end());
                    if (!isNeedNode) {
                        continue;
                    }
                    if (item.GetStatus() != ock::hcom::SubscriberRspStatus::SUCCESS) {
                        callback.cb(callback.cbCtx, nullptr, 0, MMS_INNER_RETRY);  // 一个失败就全部失败
                        return;
                    }

                    int32_t opRet = *(static_cast<int32_t *>(item.GetMultiResponse().data));
                    if (opRet != MMS_OK) {  // 请求对应的处理失败了
                        callback.cb(callback.cbCtx, nullptr, 0, opRet);
                        return;
                    }
                }
                callback.cb(callback.cbCtx, nullptr, 0, MMS_OK);
            },
            std::placeholders::_1);

        auto ret = mPublisher->Call(opInfo, multiReq, netCallback);
        if (ret != MMS_OK) {
            NET_LOG_ERROR("Multicast async call failed, ret:" << ret << ", error:" << UBSHcomNetErrStr(ret) << ".");
            callback.cb(callback.cbCtx, nullptr, 0, MMS_NET_RETRY);
        }
    }

    void MulticastAsyncCallBuff(const std::unordered_set<std::string> &remoteIps, void *req, uint32_t reqLen,
                                Callback callback);

    void Reply(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    int16_t mTimeout = -1;
    NetEnginePtr mNetEngine = nullptr;
    bool mStarted = false;
    std::mutex mMutex;

    ock::hcom::PublisherService *mPublisherService = nullptr;
    std::string mPublisherServiceName{};
    NetRef<ock::hcom::Publisher> mPublisher = nullptr;
    std::string mIpMask{};
    uint16_t mPublisherWorkGroupNum = 0;

    ock::hcom::SubscriberService *mSubscriberService = nullptr;
    std::string mSubscriberServiceName{};

    // mSubScribers用来记录本节点订阅了哪些节点的publisher
    std::unordered_map<std::string, ock::hcom::SubscriberPtr> mSubScribers{};  // key:publisher's ip, value:Subscriber
    ReadWriteLock mSubScribersLock;

    // mSubScribersRemote用来记录订阅了本节点publisher的所有subscriber
    std::unordered_set<std::string> mSubScribersRemote{}; // ip
    ReadWriteLock mSubScribersRemoteLock;

    MultiNetConnectorPtr mConnector = nullptr;
    EpSubscriberBrokenHandler mHandlerBroken = nullptr;
    MulticastNetMemList mMemList;
    DecryptFunc mDecryptHandler;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}

#endif // MMSCORE_NET_MULTICAST_ENGINE_H

