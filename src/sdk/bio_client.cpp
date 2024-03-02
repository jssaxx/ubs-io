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

BResult BioClient::BioLoggerInit(BioService::WorkerMode mode)
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

BResult BioClient::BioClientDiagnoseInit(BioService::WorkerMode mode)
{
    BResult ret = BIO_OK;
    if (mode == BioService::SEPARATES) {
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