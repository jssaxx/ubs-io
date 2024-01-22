/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_NET_COMMON_H
#define BOOSTIO_NET_COMMON_H

#include <map>
#include <functional>

#include "bio_err.h"
#include "bio_types.h"
#include "bio_ref.h"
#include "bio_log.h"
#include "bio_def.h"
#include "bio_str_util.h"
#include "hcom/hcom_service.h"

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

struct ConnectInfo {
    BioNodeId peerId {};     // peer node id
    std::string ip;          // peer ip
    uint16_t port = 0;       // peer port
    uint16_t retryTimes = 3; // connect retry times

    ConnectInfo() = default;
    ConnectInfo(BioNodeId tmpId, std::string tmpIp, uint16_t tmpPort, uint16_t times)
        : peerId(tmpId), ip(tmpIp), port(tmpPort), retryTimes(times)
    {}
};

/**
 * @brief async connect to peer callback
 *
 * @param userCtx      [in] user context
 * @param ret          [in] connect result
 * @param info         [in] connect information, the user can retry using these parameters.
 */
using AsyncConnHandler = std::function<void(uintptr_t userCtx, int32_t ret, ConnectInfo &info)>;

struct BioNetOptions {
    /* keep hot used variable ahead */
    int16_t timeoutCtrlSec = 3;                      /* timeout of ctrl panel */
    int16_t timeoutDataSec = 1;                      /* timeout of data panel */
    std::string ipMask;                              /* ip mask */
    uint16_t port = 0;                               /* listen port */
    uint16_t controlPanelHandlerCount = 1;           /* control panel handler count */
    uint16_t controlPanelConnCount = 1;              /* control panel conn count */
    uint16_t dataPanelHandlerCount = 1;              /* data panel handler count */
    uint16_t dataPanelConnCount = 1;                 /* data panel conn count */
    bool isBusyLoop = false;                         /* busy for rdma only */
    uint64_t localMrSize = 128 * 1024 * 1024;        /* local cached MR */
    ServiceProtocol protocol = ServiceProtocol::TCP; /* protocol */
    std::string name;                                /* net service name */
    uint16_t handleRequestThreadNum = NO_128;        /* handle request thread number */
    uint16_t handleRequestQueueSize = NO_8192;       /* handle request queue size */
};

/*
 * Connection payload
 */
const std::string CONN_PAYLOAD_PREFIX_CTRL = "bio-ctrl-";
const std::string CONN_PAYLOAD_PREFIX_DATA = "bio-data-";
const uint32_t CONN_PAYLOAD_PREFIX_SIZE = CONN_PAYLOAD_PREFIX_CTRL.size();

union BioNetConnPayload {
    struct {
        uint32_t srcNodeId : 32; /* source node id */
        uint32_t tgtNodeId : 32; /* target node id */
    };
    uint64_t whole = 0;

    BioNetConnPayload() = default;
    explicit BioNetConnPayload(uint32_t sId, uint32_t tId) : srcNodeId(sId), tgtNodeId(tId) {}
    explicit BioNetConnPayload(uint64_t p) : whole(p) {}

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

        BioNetConnPayload pl(nodeIds);
        srcNodeId = pl.srcNodeId;
        tgtNodeId = pl.tgtNodeId;

        return BIO_OK;
    }
};

/*
 * Channel up context to fast determinate peerId/acceptedChannel/panelId etc
 */
union BioNetChannelUpCtx {
    struct {
        uint64_t peerId : 32;    /* peer node id */
        uint64_t isAccepted : 1; /* accepted from other */
        uint64_t panelId : 2;    /* panel id, 0 for ctrl, 1 for data */
        uint64_t reserved : 29;  /* reserved */
    };
    uint64_t whole = 0;

    BioNetChannelUpCtx() = default;
    explicit BioNetChannelUpCtx(uint64_t w) : whole(w) {}
    BioNetChannelUpCtx(const BioNodeId &pId, bool isCtrlPanel, bool accepted)
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

struct BioMrInfo {
    uintptr_t address = 0; /* buffer address */
    uint64_t size = 0;     /* size of buffer */
    uint32_t key = 0;      /* RDMA key */

    BioMrInfo() = default;
    BioMrInfo(uintptr_t addr, uint64_t s, uint32_t k) : address(addr), size(s), key(k) {}
};

/**
 * @brief Request info of one side operation
 */
using BioNetRequest = hcom::NetServiceRequest;

/*
 * Pre-defined engine
 */
class RpcEngine;
using RpcEnginePtr = Ref<RpcEngine>;
}
}

#endif // BOOSTIO_NET_COMMON_H
