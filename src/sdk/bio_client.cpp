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
    agent::BioClientAgentPtr agentPtr = agent::BioClientAgent::Instance();
    if (agentPtr == nullptr) {
        CLIENT_LOG_ERROR("Failed to create agent instance.");
        return BIO_ALLOC_FAIL;
    }

    BResult ret = BIO_ERR;
    LVOS_TP_START(SDK_AGENT_INIT_FAIL, &ret, BIO_ERR);
    ret = agentPtr->Initialize(mode);
    LVOS_TP_END;
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize agent, ret:" << ret << ".");
    }
    return ret;
}

BResult BioClient::BioClientNetPreInit(WorkerMode mode)
{
    mNetEngine = net::BioClientNet::Instance();
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

BResult BioClient::BioClientMirrorInit(WorkerMode mode)
{
    if ((mMirror = MakeRef<MirrorClient>(mode)) == nullptr) {
        CLIENT_LOG_ERROR("Create mirror client instance failed.");
        return BIO_ALLOC_FAIL;
    }
    auto ret = mMirror->Initialize();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
    }
    return ret;
}

void BioClient::Heartbeat()
{
    constexpr uint16_t HEART_INTERAL = 2;
    agent::BioClientAgentPtr agentPtr = agent::BioClientAgent::Instance();
    net::BioClientNetPtr netEngine = net::BioClientNet::Instance();
    uint64_t oldNodeTimes = 0;
    uint64_t oldPtTimes = 0;

    mMirror->GetCurViewTimes(oldNodeTimes, oldPtTimes);

    while (mRunning) {
        sleep(HEART_INTERAL);
        uint64_t curNodeTimes = 0;
        uint64_t curPtTimes = 0;
        BResult ret = agentPtr->ReportHb(curNodeTimes, curPtTimes);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Report hb fail:" << ret << ".");
            continue;
        }
        if (oldNodeTimes != curNodeTimes) {
            CLIENT_LOG_INFO("oldNodeTimes:" << oldNodeTimes << ", curNodeTimes:" << curNodeTimes);
            uint64_t realNodeTimes;
            ret = mMirror->RebuildNodeView(realNodeTimes);
            if (UNLIKELY(ret != BIO_OK)) {
                CLIENT_LOG_ERROR("Failed to rebuild node view, ret:" << ret << ".");
                continue;
            }
            ret = netEngine->Rebuild(mMirror->GetLocalNodeInfo().VNodeId(), mMirror->GetNodeView());
            if (UNLIKELY(ret != BIO_OK)) {
                CLIENT_LOG_ERROR("Failed to rebuild net engine, ret" << ret << ".");
                continue;
            }
            oldNodeTimes = realNodeTimes;
        }
        if (oldPtTimes != curPtTimes) {
            CLIENT_LOG_INFO("oldPtTimes:" << oldPtTimes << ", curPtTimes:" << curPtTimes);
            uint64_t realPtTimes;
            ret = mMirror->RebuildPtView(realPtTimes);
            if (ret != BIO_OK) {
                CLIENT_LOG_ERROR("Failed to rebuild pt view, ret:" << ret << ".");
                continue;
            }
            oldPtTimes = realPtTimes;
        }
    }
}

BResult BioClient::BioClientRecover()
{
    constexpr uint16_t HEARTBEAT_THREAD_NUM = 1;
    constexpr uint32_t HEARTBEAT_QUEUE_SIZE = 128;

    mHeartService = ExecutorService::Create(HEARTBEAT_THREAD_NUM, HEARTBEAT_QUEUE_SIZE);
    if (UNLIKELY(mHeartService == nullptr)) {
        CLIENT_LOG_ERROR("Failed to create heartbeat execution service.");
        return BIO_ALLOC_FAIL;
    }

    mHeartService->SetThreadName("sdk-heartbeat");
    if (!mHeartService->Start()) {
        CLIENT_LOG_ERROR("Failed to start heartbeat execution service.");
        return BIO_INNER_ERR;
    }

    if (!mHeartService->Execute([this]() { Heartbeat(); })) {
        CLIENT_LOG_ERROR("Failed to execute heartbeat.");
        return BIO_INNER_ERR;
    }

    return BIO_OK;
}

BResult BioClient::BioClientStartWork()
{
    auto ret = mMirror->Start();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
        return ret;
    }
    ret = BioClientRecover();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start heartbeat, ret:" << ret << ".");
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
    BResult ret = BIO_OK;
    if (mode == SEPARATES) {
        ret =  tp::TracePointManager::Initialize();
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to Initialize tracepoint, ret:" << ret << ".");
            return BIO_INNER_ERR;
        }
    }
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
    mRunning = false;
    mStarted = false;
}