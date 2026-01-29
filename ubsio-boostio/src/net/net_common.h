/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <map>
#include <functional>
#include <utility>

#include "hcom/hcom_service.h"
#include "bio_err.h"
#include "bio_types.h"
#include "bio_ref.h"
#include "bio_def.h"
#include "bio_str_util.h"
#include "net_log.h"

namespace ock {
namespace bio {
struct NewChannelResp {
    int32_t result = 0; /* if 0, means a good client, otherwise reject */
};

using ChannelPtr = ock::hcom::NetChannelPtr;
using ServiceContext = ock::hcom::NetServiceContext;
using NewRequestHandler = std::function<int32_t(ServiceContext &)>;
using NewChannelHandler = std::function<int32_t(const ChannelPtr &, const std::string &ipPort, NewChannelResp &)>;
using ChannelBrokenHandler = std::function<void(uint32_t nodeId, uint32_t procId)>;
using ServiceProtocol = ock::hcom::NetServiceProtocol;
using MemoryRegionPtr = ock::hcom::UBSHcomNetMemoryRegionPtr;
using MemoryAllocatorPtr = ock::hcom::UBSHcomNetMemoryAllocatorPtr;
using NetRequest = hcom::NetServiceRequest;

using CbFunc = std::function<void(void *ctx, void *resp, uint32_t len, int32_t result)>;
struct Callback {
    CbFunc cb;
    void *cbCtx;
    Callback() : cb([](void *ctx, void *resp, uint32_t len, int32_t result) {}), cbCtx(nullptr) {}
    Callback(CbFunc func, void *ctx) : cb(std::move(func)), cbCtx(ctx) {}
};

enum class ConnectMode {
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
    ConnectInfo(uint32_t srcId, uint32_t srcPid, uint32_t nid)
        : srcId(srcId, srcPid), peerId(nid, 0), port(0), retryTimes(NO_3), isSelfPoll(false)
    {}
};

using AsyncConnHandler = std::function<void(uintptr_t userCtx, int32_t ret, ConnectInfo &info)>;

enum class Role {
    NET_CLIENT = 0,
    NET_SERVER = 1,
    NET_BUTT = 2,
};

struct NetOptions {
    // Net base configs
    std::string ipMask;                                  /* ip mask */
    uint16_t port = 0;                                   /* listen port */
    uint16_t handlerCount = 1;                           /* handler count */
    uint16_t connCount = 1;                              /* connect count */
    bool isBusyLoop = false;                             /* busy for rdma only */
    uint64_t memorySize = 128 * 1024;                    /* local cached memory size */
    bool regShmMem = false;                              /* register the memory to shared */
    Role role = Role::NET_BUTT;                                /* net service role */
    ServiceProtocol protocol = ServiceProtocol::UNKNOWN; /* net protocol */
    // Net TLS configs
    bool enableTls = false;                              /* tls switch */
    std::string certificationPath{};                     /* certification path */
    std::string caCerPath{};                             /* caCer path */
    std::string caCrlPath{};                             /* caCer path */
    std::string privateKeyPath{};                        /* private key path */
    std::string privateKeyPassword{};                    /* private key password */
    std::string hseKfsMasterPath{};                      /* hseceasy kfs master path */
    std::string hseKfsStandbyPath{};                     /* hseceasy kfs standby path  */

    NetOptions() = default;
    ~NetOptions() = default;

    void FillNetBaseConfigs(uint16_t hdlCnt, uint16_t connCnt, Role netRole, ServiceProtocol netProtocol)
    {
        handlerCount = hdlCnt;
        connCount = connCnt;
        role = netRole;
        protocol = netProtocol;
    }

    void FillNetTlsConfigs(bool enable, std::string certPath, std::string tlsCaCerPath, std::string tlsCaCrlPath,
        std::string priKeyPath, std::string priKeyPw, std::string hseMstPath, std::string hseStandbyPath)
    {
        enableTls = enable;
        certificationPath = certPath;
        caCerPath = tlsCaCerPath;
        caCrlPath = tlsCaCrlPath;
        privateKeyPath = priKeyPath;
        privateKeyPassword = priKeyPw;
        hseKfsMasterPath = hseMstPath;
        hseKfsStandbyPath = hseStandbyPath;
    }
};

const std::string CONN_PAYLOAD_PREFIX_CTRL = "bio-ctrl-";
const std::string CONN_PAYLOAD_PREFIX_DATA = "bio-data-";
const uint32_t CONN_PAYLOAD_PREFIX_SIZE = CONN_PAYLOAD_PREFIX_DATA.size();
const std::string UDS_NAME = "BIO_SHM_UDS";
constexpr uint32_t MAX_MESSAGE_SIZE = (4 * 1024);
constexpr uint32_t MAX_MESSAGE_HEAD_SIZE = 1024;

union NetConnPayload {
    NetNode srcNodeId;
    uint64_t whole = 0;

    NetConnPayload() : srcNodeId(0, 0) {}
    explicit NetConnPayload(NetNode sId) : srcNodeId(sId.nid, sId.pid) {}
    explicit NetConnPayload(uint64_t p) : whole(p) {}

    std::string ToPayloadStr(const std::string &prefix) const
    {
        return prefix + std::to_string(whole);
    }

    BResult FromPayloadStr(const std::string &payload, bool &isCtrl)
    {
        if (StrUtil::StartWith(payload, CONN_PAYLOAD_PREFIX_CTRL)) {
            isCtrl = true;
        } else if (StrUtil::StartWith(payload, CONN_PAYLOAD_PREFIX_DATA)) {
            isCtrl = false;
        } else {
            return BIO_INVALID_PARAM;
        }

        auto nodeIdStr = payload.substr(CONN_PAYLOAD_PREFIX_SIZE, payload.length() - CONN_PAYLOAD_PREFIX_SIZE);
        long nodeIds = 0;
        if (UNLIKELY(!StrUtil::StrToLong(nodeIdStr, nodeIds))) {
            return BIO_INVALID_PARAM;
        }
        NetConnPayload pl(nodeIds);
        srcNodeId = pl.srcNodeId;
        return BIO_OK;
    }
};

union NetChannelUpCtx {
    struct {
        uint64_t peerId : 16;    /* peer node id */
        uint64_t procId : 32;    /* peer process id */
        uint64_t isAccepted : 1; /* accepted from other */
        uint64_t panelId : 2;    /* panel id, 0 for ctrl, 1 for data */
        uint64_t reserved : 13;  /* reserved */
    };
    uint64_t whole = 0;

    NetChannelUpCtx() = default;
    explicit NetChannelUpCtx(uint64_t w) : whole(w) {}
    NetChannelUpCtx(const NetNode &pId, bool isCtrlPanel, bool accepted)
    {
        peerId = static_cast<uint16_t>(pId.nid);
        procId = pId.pid;
        panelId = isCtrlPanel ? 0 : 1;
        isAccepted = accepted ? 1 : 0;
    }

    inline bool IsCtrlPanel() const
    {
        return panelId == NO_U64_0;
    }
};

struct NetMrInfo {
    uintptr_t address = 0; /* buffer address */
    uint64_t size = 0;     /* size of buffer */
    uint64_t key = 0;      /* RDMA key */

    NetMrInfo() = default;
    NetMrInfo(uintptr_t addr, uint64_t s, uint64_t k) : address(addr), size(s), key(k) {}
};

class NetEngine;
using NetEnginePtr = Ref<NetEngine>;
}
}

#endif // NET_COMMON_H
