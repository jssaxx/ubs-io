/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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
#include "bio_log.h"
#include "bio_def.h"
#include "bio_str_util.h"

namespace ock {
namespace bio {
struct NewChannelResp {
    int32_t result = 0; /* if 0, means a good client, otherwise reject */
};

using ChannelPtr = ock::hcom::NetChannelPtr;
using ServiceContext = ock::hcom::NetServiceContext;
using NewRequestHandler = std::function<int32_t(ServiceContext &)>;
using NewChannelHandler = std::function<int32_t(const ChannelPtr &, const std::string &ipPort, NewChannelResp &)>;
using ChannelBrokenHandler = std::function<void(uint32_t nodeId, pid_t pid)>;
using ServiceProtocol = ock::hcom::NetServiceProtocol;
using MemoryRegionPtr = ock::hcom::NetMemoryRegionPtr;
using MemoryAllocatorPtr = ock::hcom::NetMemoryAllocatorPtr;
using NetRequest = hcom::NetServiceRequest;

enum ConnectMode {
    CONNECT_IPC = 0,
    CONNECT_RPC = 1,
};

struct ConnectInfo {
    uint32_t peerId{};       // peer node id
    std::string ip;          // peer ip
    uint16_t port = 0;       // peer port
    uint16_t retryTimes = 3; // connect retry times

    ConnectInfo() = default;
    ConnectInfo(uint32_t tmpId, std::string tmpIp, uint16_t tmpPort, uint16_t times)
        : peerId(tmpId), ip(std::move(tmpIp)), port(tmpPort), retryTimes(times) {}
};

using AsyncConnHandler = std::function<void(uintptr_t userCtx, int32_t ret, ConnectInfo &info)>;

enum Role {
    NET_CLIENT = 0,
    NET_SERVER = 1,
    NET_BUTT = 2,
};

struct NetOptions {
    int16_t timeoutCtrlSec = 3;                             /* timeout of ctrl panel */
    int16_t timeoutDataSec = 1;                             /* timeout of data panel */
    std::string ipMask;                                     /* ip mask */
    uint16_t port = 0;                                      /* listen port */
    uint16_t controlPanelHandlerCount = 1;                  /* control panel handler count */
    uint16_t controlPanelConnCount = 1;                     /* control panel conn count */
    uint16_t dataPanelHandlerCount = 1;                     /* data panel handler count */
    uint16_t dataPanelConnCount = 1;                        /* data panel conn count */
    bool isBusyLoop = false;                                /* busy for rdma only */
    uint64_t localMrSize = 128 * 1024 * 1024;               /* local cached MR */
    Role rpcRole = NET_SERVER;                              /* rpc service role */
    ServiceProtocol rpcProtocol = ServiceProtocol::TCP;     /* rpc protocol */
    Role ipcRole = NET_BUTT;                              /* ipc service role */
    ServiceProtocol ipcProtocol = ServiceProtocol::UNKNOWN; /* ipc protocol */
    std::string name;                                       /* net service name */
    uint16_t handleRequestThreadNum = NO_128;               /* handle request thread number */
    uint16_t handleRequestQueueSize = NO_8192;              /* handle request queue size */
};

const std::string CONN_PAYLOAD_PREFIX_CTRL = "bio-ctrl-";
const std::string CONN_PAYLOAD_PREFIX_DATA = "bio-data-";
const uint32_t CONN_PAYLOAD_PREFIX_SIZE = CONN_PAYLOAD_PREFIX_CTRL.size();
const std::string SOCKET_FULL_PATH = "/usr/local/bioServer/uds/bio_123.s";
const std::string SOCKET_PATH_SUFFIX = "/uds/bio_123.s";
constexpr uint32_t MAX_MESSAGE_SIZE = 4096;
constexpr uint32_t MAX_MESSAGE_HEAD_SIZE = 1024;

union NetConnPayload {
    struct {
        uint32_t srcNodeId;
        pid_t srcPid;
        uint32_t tgtNodeId;
    };
    uint64_t whole = 0;

    NetConnPayload() = default;
    explicit NetConnPayload(uint32_t sId, pid_t pid, uint32_t tId) : srcNodeId(sId), srcPid(pid), tgtNodeId(tId) {}
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
        srcPid = pl.srcPid;
        tgtNodeId = pl.tgtNodeId;
        return BIO_OK;
    }
};

union NetChannelUpCtx {
    struct {
        uint64_t peerId : 32;    /* peer node id */
        uint64_t isAccepted : 1; /* accepted from other */
        uint64_t panelId : 2;    /* panel id, 0 for ctrl, 1 for data */
        uint64_t reserved : 29;  /* reserved */
    };
    uint64_t whole = 0;

    NetChannelUpCtx() = default;
    explicit NetChannelUpCtx(uint64_t w) : whole(w) {}
    NetChannelUpCtx(const BioNodeId &pId, bool isCtrlPanel, bool accepted)
    {
        peerId = pId;
        panelId = isCtrlPanel ? 0 : 1;
        isAccepted = accepted ? 1 : 0;
    }

    inline BioNodeId PeerId() const
    {
        return BioNodeId(peerId);
    }

    inline bool AcceptedChannel() const
    {
        return isAccepted;
    }

    inline bool IsCtrlPanel() const
    {
        return panelId == NO_U64_0;
    }

    inline bool IsDataPanel() const
    {
        return panelId == NO_U64_1;
    }
};

struct NetMrInfo {
    uintptr_t address = 0; /* buffer address */
    uint64_t size = 0;     /* size of buffer */
    uint32_t key = 0;      /* RDMA key */

    NetMrInfo() = default;
    NetMrInfo(uintptr_t addr, uint64_t s, uint32_t k) : address(addr), size(s), key(k) {}
};

class NetEngine;
using NetEnginePtr = Ref<NetEngine>;
}
}

#endif // NET_COMMON_H
