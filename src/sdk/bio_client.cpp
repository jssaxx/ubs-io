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
#endif
using namespace ock::bio;

BResult BioClient::BioLoggerInit(WorkerMode mode)
{
    auto defaultLogLevel = static_cast<int32_t>(BioClientLog::Level::LOG_LEVEL_INFO);
    auto logMode = static_cast<int32_t>(mode);
    return BioClientLog::Instance()->Initialize(logMode, defaultLogLevel);
}

#ifdef USE_DEBUG_TOOLS
#define SDK_DIAGNOSE_PID 123
using SdkDiagnose = int (*)();

BResult BioClient::BioDiagnoseSdkInit()
{
    const char *soFileName = "libsdk_diagnose.so";
    void *handler = dlopen(soFileName, RTLD_NOW);
    if (handler == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
        return BIO_ERR;
    }
    SdkDiagnose sdkInitFunc = reinterpret_cast<SdkDiagnose>(dlsym(handler, "SdkDiagnoseInit"));
    BResult ret = sdkInitFunc();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize sdk diagnose, ret:" << ret << ".");
        return BIO_ERR;
    }
    return ret;
}

BResult BioClient::BioDiagnoseHtracerInit()
{
    const char *soFileName = "libhtracer_diagnose.so";
    void *handler = dlopen(soFileName, RTLD_NOW);
    if (handler == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
        return BIO_ERR;
    }
    SdkDiagnose sdkInitFunc = reinterpret_cast<SdkDiagnose>(dlsym(handler, "HtracerDiagnoseInit"));
    BResult ret = sdkInitFunc();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize htracer diagnose, ret:" << ret << ".");
        return BIO_ERR;
    }
    return ret;
}

BResult BioClient::BioClientDiagnoseInit(WorkerMode mode)
{
    BResult ret = BIO_OK;
    if (mode == SEPARATES) {
        char rootName[] = "bio_sdk";
        ret =  CLI_AgentInit(SDK_DIAGNOSE_PID, rootName);
        if (ret != BIO_OK) {
            return BIO_ERR;
        }
    }

    ret = this->BioDiagnoseSdkInit();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize sdk diagnose, ret:" << ret << ".");
        return BIO_ERR;
    }

    ret = this->BioDiagnoseHtracerInit();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize htracer diagnose, ret:" << ret << ".");
        return BIO_ERR;
    }

    return ret;
}
#endif

BResult BioClient::Start(WorkerMode mode)
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

#ifdef USE_DEBUG_TOOLS
    ret = this->BioClientDiagnoseInit(mode);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize sdk diagnose.");
        return ret;
    }
#endif

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

BResult BioClient::Recover()
{
    constexpr uint16_t HEARTBEAT_THREAD_NUM = 1;
    constexpr uint32_t HEARTBEAT_QUEUE_SIZE = 128;

    mHeartService = ExecutorService::Create(HEARTBEAT_THREAD_NUM, HEARTBEAT_QUEUE_SIZE);
    if (UNLIKELY(mHeartService == nullptr)) {
        LOG_ERROR("Failed to start heartbeat execution service");
        return BIO_ALLOC_FAIL;
    }

    mHeartService->SetThreadName("sdk-heartbeat");
    auto result = mHeartService->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    result = mHeartService->Execute([this]() { Heartbeat(); });
    ChkTrueNot(result, BIO_INNER_ERR);
}

void BioClient::Heartbeat()
{
    constexpr uint16_t HEART_INTERAL = 2;
    agent::BioClientAgentPtr agentPtr = agent::BioClientAgent::Instance();
    net::BioClientNetPtr netEngine = net::BioClientNet::Instance();
    uint64_t oldNodeTimes = 0;
    uint64_t oldPtTimes = 0;

    while (mRunning) {
        sleep(HEART_INTERAL);
        uint64_t curNodeTimes = 0;
        uint64_t curPtTimes = 0;
        BResult ret = agentPtr->ReportHb(curNodeTimes, curPtTimes);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Report hb fail:" << ret << ".");
            continue;
        }

        if (oldNodeTimes != curNodeTimes) {
            uint64_t realNodeTimes;
            ret = mMirror->RebuildNodeView(realNodeTimes);
            if (ret != BIO_OK) {
                CLIENT_LOG_ERROR("Failed to rebuild nodeview, ret:" << ret << ".");
                continue;
            }
            ret = netEngine->Rebuild(mMirror->GetLocalNodeInfo().VNodeId(), mMirror->GetNodeView());
            if (ret != BIO_OK) {
                CLIENT_LOG_ERROR("Failed to rebuild channel, ret" << ret << ".");
                continue;
            }
            oldNodeTimes = realNodeTimes;
        }
        if (oldPtTimes != curPtTimes) {
            uint64_t realPtTimes;
            ret = mMirror->RebuildPtView(realPtTimes);
            if (ret != BIO_OK) {
                CLIENT_LOG_ERROR("Failed to rebuild ptview, ret:" << ret << ".");
                continue;
            }
            oldNodeTimes = realPtTimes;
        }
    }
}