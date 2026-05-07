/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <map>
#include <functional>
#include <utility>

#include "hcom/hcom_service.h"
#include "hcom/hcom_service_context.h"
#include "mms_err.h"
#include "mms_types.h"
#include "mms_ref.h"
#include "mms_def.h"
#include "mms_str_util.h"
#include "net_log.h"

namespace ock {
namespace mms {
struct NewChannelResp {
    int32_t result = 0; /* if 0, means a good client, otherwise reject */
};

using ChannelPtr = ock::hcom::UBSHcomChannelPtr;
using ServiceContext = ock::hcom::UBSHcomServiceContext;
using NewRequestHandler = std::function<int32_t(ServiceContext &)>;
using NewChannelHandler = std::function<int32_t(const ChannelPtr &, const std::string &ipPort, NewChannelResp &)>;
using ChannelBrokenHandler = std::function<void(uint32_t nodeId, uint32_t procId)>;
using ServiceProtocol = ock::hcom::UBSHcomServiceProtocol;
using MemoryRegion = ock::hcom::UBSHcomRegMemoryRegion;
using NetRequest = ock::hcom::UBSHcomOneSideRequest;
using NetCallback = ock::hcom::Callback;
using EpSubscriberBrokenHandler = std::function<void(uint16_t nodeId)>;

using CbFunc = std::function<void(void *ctx, void *resp, uint32_t len, int32_t result)>;
struct Callback {
    CbFunc cb;
    void *cbCtx;
    Callback() : cb([](void *ctx, void *resp, uint32_t len, int32_t result) {}), cbCtx(nullptr) {}
    Callback(CbFunc func, void *ctx) : cb(std::move(func)), cbCtx(ctx) {}
};

enum ConnectMode {
    CONNECT_IPC = 0,
    CONNECT_RPC = 1,
};

constexpr uint32_t INVALID_NID = 1024;

union NetNode {
    struct {
        uint32_t nid;
        uint32_t pid;
    };
    uint64_t whole = 0;

    NetNode() = default;
    NetNode(uint32_t inNid, uint32_t inPid) : nid(inNid), pid(inPid) {}
    explicit NetNode(uint64_t p) : whole(p) {}
    NetNode(const NetNode &inNid) : nid(inNid.nid), pid(inNid.pid) {}
    NetNode &operator = (const NetNode &inNid)
    {
        whole = inNid.whole;
        return *this;
    }
};

struct ConnectInfo {
    NetNode srcId;
    NetNode peerId;
    std::string ip;
    uint16_t port;
    uint16_t retryTimes;
    bool isSelfPoll;

    ConnectInfo() = default;
    ConnectInfo(uint32_t srcId, uint32_t srcPid, uint32_t nid, std::string ip, uint16_t port, uint16_t times)
        : srcId(srcId, srcPid), peerId(nid, 0), ip(std::move(ip)), port(port), retryTimes(times), isSelfPoll(false)
    {}
    ConnectInfo(uint32_t srcId, uint32_t srcPid, uint32_t nid, bool selfPool = false)
        : srcId(srcId, srcPid), peerId(nid, 0), port(0), retryTimes(NO_3), isSelfPoll(selfPool)
    {}
};

struct SubscriptionInfo {
    uint16_t peerNodeId;
    std::string ip{};
    uint16_t port = 0;
    uint16_t retryTimes = NO_3;
    SubscriptionInfo() = default;
    SubscriptionInfo(uint16_t peerNodeId, std::string ip, uint16_t port, uint16_t retryTimes)
        : peerNodeId(peerNodeId),
          ip(std::move(ip)),
          port(port),
          retryTimes(retryTimes)
    {
    }
};

using AsyncConnHandler = std::function<void(uintptr_t userCtx, int32_t ret, ConnectInfo &info)>;
using MulticastAsyncHandler = std::function<void(int32_t ret, SubscriptionInfo &info)>;

enum Role {
    NET_CLIENT = 0,
    NET_SERVER = 1,
    NET_BUTT = 2,
};

struct NetOptions {
    std::string ipMask;                                  /* ip mask */
    uint16_t port = 0;                                   /* listen port */
    ServiceProtocol protocol = ServiceProtocol::UNKNOWN; /* net protocol */
    uint16_t connCount = 1;                              /* connect count */
    bool isBusyPolling = false;                          /* busypoll or eventpoll */
    std::string workerGroups;                            /* worker groups */
    std::string workerGroupsCpuSet;                      /* worker groups cpuset */
    uint16_t workerGroupsNum;                            /* worker groups num */
    Role role = NET_BUTT;                                /* net service role */

    // tls config
    bool tlsEnable = true;
    std::string certificationPath{};
    std::string caCerPath{};
    std::string caCrlPath{};
    std::string privateKeyPath{};
    std::string privateKeyPasswordPath{};
    std::string decrypterLibPath{};
    std::string opensslLibDir{};

    NetOptions() = default;
    ~NetOptions() = default;
};

const std::string CONN_PAYLOAD_PREFIX = "mms-idx";
const std::string UDS_NAME = "MMS_SHM_UDS";
const std::string RPC_SERVICE_NAME_SERVER = "MMS_RPC_SERVER";
const std::string RPC_SERVICE_NAME_CLIENT = "MMS_RPC_CLIENT";
const std::string IPC_SERVICE_NAME_SERVER = "MMS_IPC_SERVER";
const std::string IPC_SERVICE_NAME_CLIENT = "MMS_IPC_CLIENT";

union NetConnPayload {
    NetNode srcNodeId;
    uint64_t whole = 0;

    NetConnPayload() : srcNodeId(0, 0) {}
    explicit NetConnPayload(const NetNode &sId) : srcNodeId(sId.nid, sId.pid) {}
    explicit NetConnPayload(uint64_t p) : whole(p) {}

    std::string ToPayloadStr(const std::string &prefix) const
    {
        return prefix + std::to_string(whole);
    }

    BResult FromPayloadStr(const std::string &payload, uint32_t &groupIndex)
    {
        std::vector<std::string> splitVec;
        StrUtil::Split(payload, "-", splitVec);

        if (splitVec.size() != NO_4) {
            return MMS_INVALID_PARAM;
        }

        long value;
        if (UNLIKELY(!StrUtil::StrToLong(splitVec[NO_2], value))) {
            return MMS_INVALID_PARAM;
        }
        if (value >= MAX_GROUPS_NUM) {
            return MMS_INVALID_PARAM;
        }
        groupIndex = static_cast<uint32_t>(value);

        if (UNLIKELY(!StrUtil::StrToLong(splitVec[NO_3], value))) {
            return MMS_INVALID_PARAM;
        }
        NetConnPayload pl(value);
        srcNodeId = pl.srcNodeId;
        return MMS_OK;
    }
};

union NetChannelUpCtx {
    struct {
        uint64_t peerId : 16;    /* peer node id */
        uint64_t procId : 32;    /* peer process id */
        uint64_t isAccepted : 1; /* accepted from other */
        uint64_t groupIndex : 2; /* group index, 0, 1, 2, 3 */
        uint64_t reserved : 13;  /* reserved */
    };
    uint64_t whole = 0;

    NetChannelUpCtx() = default;
    explicit NetChannelUpCtx(uint64_t w) : whole(w) {}
    NetChannelUpCtx(const NetNode &pId, uint32_t groupIndex, bool accepted)
    {
        peerId = static_cast<uint16_t>(pId.nid);
        procId = pId.pid;
        groupIndex = groupIndex;
        isAccepted = accepted ? 1 : 0;
    }

    inline uint32_t GetGroupIndex() const
    {
        return static_cast<uint32_t>(groupIndex);
    }
};

struct NetMemList {
    uint16_t num = 0;
    uintptr_t address[NO_8];
    uint64_t size[NO_8];
    MemoryRegion mr[NO_8];
};

struct MulticastNetMemList {
    uint16_t num = 0;
    uintptr_t address[NO_8];
    uint64_t size[NO_8];
    ock::hcom::UBSHcomNetMemoryRegionPtr mr[NO_8];
};

struct NetMrInfo {
    uintptr_t address = 0; /* buffer address */
    uint64_t size = 0;     /* size of buffer */
    uint32_t key = 0;      /* RDMA key */

    NetMrInfo() = default;
    NetMrInfo(uintptr_t addr, uint64_t s, uint32_t k) : address(addr), size(s), key(k) {}
};

class NetMulticastEngine;
using NetMulticastEnginePtr = Ref<NetMulticastEngine>;
class NetEngine;
using NetEnginePtr = Ref<NetEngine>;
}
}

#endif // NET_COMMON_H

