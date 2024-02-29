/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include "bio_client_log.h"
#include "bio_client_agent.h"
#include "bio_client_net.h"

#include <utility>

using namespace ock::bio;
using namespace ock::bio::agent;
using namespace ock::bio::net;

static void Log(int level, const char *msg)
{
    if (level == 0) {
        CLIENT_LOG_DEBUG(msg);
    } else if (level == 1U) {
        CLIENT_LOG_INFO(msg);
    } else if (level == 2U) {
        CLIENT_LOG_WARN(msg);
    } else if (level == 3U) {
        CLIENT_LOG_ERROR(msg);
    }
}

BResult BioClientNet::StartPre(BioService::WorkerMode mode)
{
    mMode = mode;
    BResult ret = BIO_OK;
    if (mode == BioService::CONVERGENCE) {
        mNetEngine = BioClientAgent::Instance()->GetNetService();
        if (mNetEngine == nullptr) {
            ret = BIO_ERR;
        }
    } else {
        ret = StartIpcService();
    }
    return ret;
}

BResult BioClientNet::StartPost(uint16_t localNid, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView,
    uint16_t protocol)
{
    if (mMode == BioService::CONVERGENCE) {
        return BIO_OK;
    }
    mNetEngine->SetLocalNodeId(localNid);

    std::string ipMask;
    uint16_t port = UINT16_MAX;
    for (auto &node : nodeView) {
        if (node.second.id.VNodeId() == localNid) {
            ipMask = node.second.ip;
            port = node.second.port;
            break;
        }
    }
    if (port == UINT16_MAX) {
        CLIENT_LOG_WARN("Not found local node info.");
        return BIO_OK;
    }

    auto ret = StartRpcService((ipMask + "/24"), port, static_cast<ServiceProtocol>(protocol), NO_4);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Bio client start rpc service failed, result:" << ret << ".");
        return ret;
    }

    for (auto &node : nodeView) {
        if (node.second.id.VNodeId() == localNid) {
            continue;
        }
        ConnectInfo info(node.second.id.VNodeId(), node.second.ip, node.second.port, NO_3);
        LOG_INFO("Connect to remote node:" << info.peerId << ", ip:" << info.ip << ", port:" << info.port << ".");
        ret = mNetEngine->SyncConnect(info);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Connect to local bio server failed, result:" << ret << ".");
            return ret;
        }
    }
    return BIO_OK;
}

void BioClientNet::Stop()
{
    if (mMode == BioService::SEPARATES) {
        StopInner();
    }
}

BResult BioClientNet::StartIpcService()
{
    mNetEngine = MakeRef<NetEngine>();
    if (mNetEngine == nullptr) {
        CLIENT_LOG_ERROR("Make net engine failed.");
        return BIO_ALLOC_FAIL;
    }

    // 1. Initialize net engine
    int16_t timeoutSec = NO_5 * NO_60; // 5min
    uint32_t coreThreadNum = NO_128;
    uint32_t queueSize = NO_1024;
    auto ret = mNetEngine->Initialize(timeoutSec, coreThreadNum, queueSize, Log);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Net engine initialize failed, result:" << ret << ".");
        return ret;
    }

    // 2. start ipc service
    NetOptions netOptions;
    netOptions.isBusyLoop = false;
    netOptions.role = NET_CLIENT;
    netOptions.protocol = ServiceProtocol::SHM;
    netOptions.connCount = NO_4;
    netOptions.handlerCount = NO_4;
    ret = mNetEngine->Start(netOptions);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Start ipc service failed, result:" << ret << ".");
        return ret;
    }

    // 3. connection to local bio server
    ConnectInfo info(static_cast<uint32_t>(getpid()));
    CLIENT_LOG_INFO("Connect to local bio server, pid:" << info.peerId << ", ip:" << info.ip << ", port:" <<
        info.port << ".");
    ret = mNetEngine->SyncConnect(info);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Connect to local bio server failed, result:" << ret << ".");
    }
    return ret;
}

BResult BioClientNet::StartRpcService(std::string ipMask, uint16_t port, ServiceProtocol protocol, uint16_t workerNum)
{
    NetOptions netOptions;
    netOptions.ipMask = std::move(ipMask);
    netOptions.port = port;
    netOptions.role = NET_CLIENT;
    netOptions.isBusyLoop = false;
    netOptions.protocol = protocol;
    netOptions.connCount = workerNum;
    netOptions.handlerCount = workerNum;
    return mNetEngine->Start(netOptions);
}

void BioClientNet::StopInner()
{
    mNetEngine->Stop();
}