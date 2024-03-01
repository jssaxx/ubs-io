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
using ChannelBrokenHandler = std::function<void(uint32_t nodeId)>;
using ServiceProtocol = ock::hcom::NetServiceProtocol;
using MemoryRegionPtr = ock::hcom::NetMemoryRegionPtr;
using MemoryAllocatorPtr = ock::hcom::NetMemoryAllocatorPtr;
using NetRequest = hcom::NetServiceRequest;

enum ConnectMode {
    CONNECT_IPC = 0,
    CONNECT_RPC = 1,
};

struct ConnectInfo {
    uint32_t peerId;
    std::string ip;
    uint16_t port;
    uint16_t retryTimes;

    ConnectInfo() = default;
    ConnectInfo(uint32_t nid, std::string ip, uint16_t port, uint16_t times)
        : peerId(nid), ip(std::move(ip)), port(port), retryTimes(times)
    {}
    ConnectInfo(uint32_t nid) : peerId(nid), port(0), retryTimes(NO_3) {}
};

using AsyncConnHandler = std::function<void(uintptr_t userCtx, int32_t ret, ConnectInfo &info)>;

enum Role {
    NET_CLIENT = 0,
    NET_SERVER = 1,
    NET_BUTT = 2,
};

struct NetOptions {
    std::string ipMask;                                  /* ip mask */
    uint16_t port = 0;                                   /* listen port */
    uint16_t handlerCount = 1;                           /* handler count */
    uint16_t connCount = 1;                              /* connect count */
    bool isBusyLoop = false;                             /* busy for rdma only */
    uint64_t memorySize = 128 * 1024;                    /* local cached memory size */
    bool regShmMem = false;                              /* register the memory to shared */
    Role role = NET_BUTT;                                /* net service role */
    ServiceProtocol protocol = ServiceProtocol::UNKNOWN; /* net protocol */
};

const std::string CONN_PAYLOAD_PREFIX_DATA = "bio-data-";
const uint32_t CONN_PAYLOAD_PREFIX_SIZE = CONN_PAYLOAD_PREFIX_DATA.size();
const std::string UDS_NAME = "BIO_SHM_UDS";
constexpr uint32_t IPC_MAX_MESSAGE_SIZE = (33 * 1024);
constexpr uint32_t MAX_MESSAGE_SIZE = 4096;
constexpr uint32_t MAX_MESSAGE_HEAD_SIZE = 1024;

union NetConnPayload {
    struct {
        uint32_t srcNodeId;
        uint32_t tgtNodeId;
    };
    uint64_t whole = 0;

    NetConnPayload() = default;
    explicit NetConnPayload(uint16_t sId, uint16_t tId) : srcNodeId(sId), tgtNodeId(tId) {}
    explicit NetConnPayload(uint64_t p) : whole(p) {}

    std::string ToPayloadStr(const std::string &prefix) const
    {
        return prefix + std::to_string(whole);
    }

    BResult FromPayloadStr(const std::string &payload)
    {
        auto nodeIdStr = payload.substr(CONN_PAYLOAD_PREFIX_SIZE, payload.length() - CONN_PAYLOAD_PREFIX_SIZE);
        long nodeIds = 0;
        if (UNLIKELY(!StrUtil::StrToLong(nodeIdStr, nodeIds))) {
            return BIO_INVALID_PARAM;
        }
        NetConnPayload pl(nodeIds);
        srcNodeId = pl.srcNodeId;
        tgtNodeId = pl.tgtNodeId;
        return BIO_OK;
    }
};

union NetChannelUpCtx {
    struct {
        uint64_t peerId : 32;    /* peer node id */
        uint64_t isAccepted : 1; /* accepted from other */
        uint64_t reserved : 31;  /* reserved */
    };
    uint64_t whole = 0;

    NetChannelUpCtx() = default;
    explicit NetChannelUpCtx(uint64_t w) : whole(w) {}
    NetChannelUpCtx(const BioNodeId &pId, bool accepted)
    {
        peerId = pId;
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
