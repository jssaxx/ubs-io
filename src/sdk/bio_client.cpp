/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include "bio_client_log.h"
#include "bio_client_net.h"
#include "bio_client_agent.h"
#include "bio_client.h"

using namespace ock::bio;

BResult BioClient::BioLoggerInit(BioService::WorkerMode mode)
{
    auto defaultLogLevel = static_cast<int32_t>(BioClientLog::Level::LOG_LEVEL_INFO);
    auto logMode = static_cast<int32_t>(mode);
    return BioClientLog::Instance()->Initialize(logMode, defaultLogLevel);
}

BResult BioClient::Start(BioService::WorkerMode mode)
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }
    mMode = mode;

    if (BioLoggerInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    agent::BioClientAgentPtr agentPtr = agent::BioClientAgent::Instance();
    if (agentPtr == nullptr) {
        CLIENT_LOG_ERROR("Failed to create agent instance.");
        return BIO_ERR;
    }
    BResult ret = agentPtr->Initialize(mode);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize agent, ret:" << ret << ".");
        return ret;
    }

    net::BioClientNetPtr netEngine = net::BioClientNet::Instance();
    if (netEngine == nullptr) {
        CLIENT_LOG_ERROR("Failed to create net instance.");
        return BIO_ERR;
    }
    ret = netEngine->StartPre(mode);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start net service, ret:" << ret << ".");
        return ret;
    }

    if ((mMirror = MakeRef<MirrorClient>(mode)) == nullptr) {
        CLIENT_LOG_ERROR("Create mirror client instance failed.");
        return BIO_ERR;
    }
    ret = mMirror->Initialize();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
        return ret;
    }

    ret =
        netEngine->StartPost(mMirror->GetLocalNodeInfo().VNodeId(), mMirror->GetNodeView(), mMirror->GetNetProtocol());
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start net service, ret" << ret << ".");
        return ret;
    }

    ret = mMirror->Start();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
        return ret;
    }

    mStarted = true;
    CLIENT_LOG_INFO("Boostio client start success.");
    return BIO_OK;
}

void BioClient::Stop()
{
    mStarted = false;
}