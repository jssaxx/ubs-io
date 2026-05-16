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

#ifndef MMSCORE_NET_MULTICAST_ENGINE_H
#define MMSCORE_NET_MULTICAST_ENGINE_H

#include "net_common.h"
#include "mms_message.h"
#include "cm.h"
#include "hcom/multicast/multicast_publisher_service.h"
#include "hcom/multicast/multicast_publisher.h"
#include "hcom/multicast/multicast_subscriber_service.h"
#include "hcom/multicast/multicast_subscriber.h"
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

    bool CheckRemoteNodeStatus(const std::string &remoteIp);

    template <typename TReq>
    void MulticastAsyncCall(TReq &req, Callback &callback)
    {
        ock::hcom::UBSHcomNetTransOpInfo opInfo;
        opInfo.timeout = mTimeout;
        ock::hcom::MultiRequest multiReq(static_cast<void *>(&req), sizeof(req),
                                         mMemList.mr[MMAP_AREA_IOCTX_INDEX]->GetLKey());

        auto *netCallback = ock::hcom::NewMultiCastCallback(
            [this, callback](PublisherContext &context) {
                const std::vector<ock::hcom::SubscriberRspInfo> &infos = context.GetSubscriberRspInfo();
                for (auto &item : infos) {
                    if (item.GetStatus() != ock::hcom::SubscriberRspStatus::SUCCESS) {
                        std::string remoteIp = item.GetSubInfos()->GetIp();
                        if (!CheckRemoteNodeStatus(remoteIp)) {
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

        auto ret = mPublisher->Call(opInfo, multiReq, netCallback);
        if (ret != MMS_OK) {
            NET_LOG_ERROR("Multicast async call failed, ret:" << ret << ", error:" << UBSHcomNetErrStr(ret) << ".");
            callback.cb(callback.cbCtx, nullptr, 0, MMS_NET_RETRY);
        }
    }

    void MulticastAsyncCallBuff(void *req, uint32_t reqLen, Callback callback);

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
