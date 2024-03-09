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
using ChannelBrokenHandler = std::function<void(uint32_t nodeId, uint32_t procId)>;
using ServiceProtocol = ock::hcom::NetServiceProtocol;
using MemoryRegionPtr = ock::hcom::NetMemoryRegionPtr;
using MemoryAllocatorPtr = ock::hcom::NetMemoryAllocatorPtr;
using NetRequest = hcom::NetServiceRequest;

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
    NetNode(uint64_t p) : whole(p) {}
    NetNode(const NetNode& inNid) : nid(inNid.nid), pid(inNid.pid) {}
};

struct ConnectInfo {
    NetNode srcId;
    NetNode peerId;
    std::string ip;
    uint16_t port;
    uint16_t retryTimes;

    ConnectInfo() = default;
    ConnectInfo(uint32_t srcid, uint32_t srcPid, uint32_t nid, std::string ip, uint16_t port, uint16_t times)
        : srcId(srcid, srcPid), peerId(nid, 0), ip(std::move(ip)), port(port), retryTimes(times)
    {}
    ConnectInfo(uint32_t srcid, uint32_t srcPid, uint32_t nid)
        : srcId(srcid, srcPid), peerId(nid, 0), port(0), retryTimes(NO_3)
    {}
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
constexpr uint32_t MAX_MESSAGE_SIZE = (4 * 1024);
constexpr uint32_t MAX_MESSAGE_HEAD_SIZE = 1024;

union NetConnPayload {
    NetNode srcNodeId;
    uint64_t whole = 0;

    NetConnPayload() : srcNodeId(0, 0) {}
    NetConnPayload(NetNode sId) : srcNodeId(sId.nid, sId.pid) {}
    NetConnPayload(uint64_t p) : whole(p) {}

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
        return BIO_OK;
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
