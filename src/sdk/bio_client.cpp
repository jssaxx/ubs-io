/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <dlfcn.h>
#include "bio_client_log.h"
#include "bio_client_net.h"
#include "bio_client_agent.h"
#include "bio_client.h"
#ifdef USE_DEBUG_TOOLS
#include <dlfcn.h>
#include "bio_tracepoint_helper.h"
#endif
using namespace ock::bio;

BResult BioClient::BioClientLoggerInit(WorkerMode mode)
{
    auto logMode = static_cast<int32_t>(mode);
    auto defaultLogLevel = static_cast<int32_t>(BioClientLog::Level::LOG_LEVEL_INFO);
    return BioClientLog::Instance()->Initialize(logMode, defaultLogLevel);
}

BResult BioClient::BioClientAgentInit(WorkerMode mode)
{
    agent::BioClientAgentPtr agentPtr = nullptr;
    LVOS_TP_START(SDK_BIO_AGENT_CREAT_FAIL, &agentPtr, nullptr);
    agentPtr = agent::BioClientAgent::Instance();
    LVOS_TP_END;
    if (agentPtr == nullptr) {
        CLIENT_LOG_ERROR("Failed to create agent instance.");
        return BIO_ALLOC_FAIL;
    }

    auto ret = agentPtr->Initialize(mode);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize agent, ret:" << ret << ".");
    }
    return ret;
}

BResult BioClient::BioClientNetPreInit(WorkerMode mode)
{
    LVOS_TP_START(SDK_BIO_NET_PRE_CREAT_FAIL, &mNetEngine, nullptr);
    mNetEngine = net::BioClientNet::Instance();
    LVOS_TP_END;
    if (mNetEngine == nullptr) {
        CLIENT_LOG_ERROR("Failed to create net instance.");
        return BIO_ALLOC_FAIL;
    }
    auto ret = mNetEngine->StartPre(mode);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start net service, ret:" << ret << ".");
    }
    return ret;
}

BResult BioClient::BioClientNetPostInit()
{
    CheckNodeOnline checkHandle = [this](uint16_t nodeId, std::string &ip, uint16_t &port) -> bool {
        return mMirror->CheckIsOnline(nodeId, ip, port);
    };

    mNetEngine->RegCheckNodeOnline(checkHandle);

    return mNetEngine->StartPost(mMirror->GetLocalNodeInfo().VNodeId(), mMirror->GetNodeView(),
        mMirror->GetNetProtocol());
}

void BioClient::BioClientUpdateHandle()
{
    agent::BioClientAgentPtr agentPtr = agent::BioClientAgent::Instance();
    net::BioClientNetPtr netEngine = net::BioClientNet::Instance();

    auto ret = mMirror->RebuildNodeView();
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Failed to rebuild node view, ret:" << ret << ".");
        return;
    }
    ret = netEngine->Rebuild(mMirror->GetLocalNodeInfo().VNodeId(), mMirror->GetNodeView());
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Failed to rebuild net engine, ret" << ret << ".");
        return;
    }

    ret = mMirror->RebuildPtView();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to rebuild pt view, ret:" << ret << ".");
        return;
    }

    return;
}

void BioClient::BioClientUpdateView()
{
    bool expectval = false;
    if (!mIsUpdating.compare_exchange_weak(expectval, true)) {
        return;
    }
    BioClientUpdateHandle();
    mIsUpdating.store(false);
    return;
}

BResult BioClient::BioClientMirrorInit(WorkerMode mode)
{
    LVOS_TP_START(SDK_BIO_MIRROR_CREAT_FAIL, &mMirror, nullptr);
    mMirror = MakeRef<MirrorClient>(mode);
    LVOS_TP_END;
    if (mMirror == nullptr) {
        CLIENT_LOG_ERROR("Create mirror client instance failed.");
        return BIO_ALLOC_FAIL;
    }

    mIsUpdating = false;
    UpdateView updateView = [this]() { BioClientUpdateView(); };
    uint32_t scene = mNetEngine->GetWorkerScene();

    auto ret = mMirror->Initialize(updateView, scene);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
    }
    return ret;
}

BResult BioClient::BioClientStartWork()
{
    auto ret = mMirror->Start();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
        return ret;
    }
    return ret;
}

#ifdef USE_DEBUG_TOOLS
using SdkDiagnose = int (*)();
BResult BioClient::BioDiagnoseSdkInit()
{
    const char *soFileName = "libsdk_diagnose.so";
    void *handler = dlopen(soFileName, RTLD_NOW);
    if (handler == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
        return BIO_INNER_ERR;
    }
    SdkDiagnose sdkInitFunc = reinterpret_cast<SdkDiagnose>(dlsym(handler, "SdkDiagnoseInit"));
    BResult ret = sdkInitFunc();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize sdk diagnose, ret:" << ret << ".");
    }
    return ret;
}

BResult BioClient::BioClientDiagnoseInit(WorkerMode mode)
{
    BResult ret = BIO_OK;
    if (mode == SEPARATES) {
        std::string diagName = "bio_sdk";
        ret = CLI_AgentInit(getpid(), const_cast<char *>(diagName.c_str()));
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to Initialize cli, ret:" << ret << ".");
            return BIO_INNER_ERR;
        }
    }

    ret = this->BioDiagnoseSdkInit();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize sdk diagnose, ret:" << ret << ".");
    }
    return ret;
}

BResult BioClient::BioClientTracePointInit(WorkerMode mode)
{
#ifdef USE_DEBUG_TP_TOOLS
    BResult ret = BIO_OK;
    if (mode == SEPARATES) {
        ret =  tp::TracePointManager::Initialize();
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to Initialize tracepoint, ret:" << ret << ".");
            return BIO_INNER_ERR;
        }
    }
#endif
    return BIO_OK;
}
#endif

BResult BioClient::Start(WorkerMode mode)
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }
    mMode = mode;

    if (BioClientLoggerInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientAgentInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

#ifdef USE_DEBUG_TOOLS
    if (this->BioClientDiagnoseInit(mode) != BIO_OK) {
        return BIO_ERR;
    }
    if (this->BioClientTracePointInit(mode) != BIO_OK) {
        return BIO_ERR;
    }
#endif

    if (BioClientNetPreInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientMirrorInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientNetPostInit() != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientStartWork() != BIO_OK) {
        return BIO_ERR;
    }

    mStarted = true;
    CLIENT_LOG_INFO("Boostio client start success.");
    return BIO_OK;
}

void BioClient::Stop()
{
    mStarted = false;
}