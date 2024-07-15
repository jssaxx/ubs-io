/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include "bio_client_log.h"
#include "bio_client_net.h"
#include "bio_client_agent.h"
#include "interceptor_server.h"
#include "bio_tracepoint_helper.h"
#include "bio_client.h"
#ifdef USE_CLI_TOOLS
#include <dlfcn.h>
#endif

using namespace ock::bio;

BResult BioClient::BioClientLoggerInit(WorkerMode mode, LogType logType, std::string logFilePath)
{
    auto logMode = static_cast<int32_t>(mode);
    auto defaultLogLevel = static_cast<int32_t>(BioClientLog::Level::LOG_LEVEL_INFO);
    auto type = static_cast<uint8_t>(logType);
    return BioClientLog::Instance()->Initialize(logMode, defaultLogLevel, type, logFilePath);
}

void BioClient::BioClientLoggerExit(WorkerMode mode)
{
    BioClientLog::Instance()->Exit(mode);
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

void BioClient::BioClientAgentExit()
{
    agent::BioClientAgent::Instance()->Exit();
}

BResult BioClient::BioClientNetPreInit(WorkerMode mode, const NetOptions netConf)
{
    mNetEngine = net::BioClientNet::Instance();
    if (mNetEngine == nullptr) {
        CLIENT_LOG_ERROR("Failed to create net instance.");
        return BIO_ALLOC_FAIL;
    }
    auto ret = mNetEngine->StartPre(mode, netConf);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start net service, ret:" << ret << ".");
        return ret;
    }

    int32_t logLevel = mNetEngine->GetNegoLogLevel();
    BioClientLog::Instance()->ResetLogLevel(logLevel);
    return BIO_OK;
}

BResult BioClient::BioClientNetPostInit(const NetOptions netConf)
{
    CheckNodeOnline checkHandle = [this](uint16_t nodeId, std::string &ip, uint16_t &port) -> bool {
        return mMirror->CheckIsOnline(nodeId, ip, port);
    };

    mNetEngine->RegCheckNodeOnline(checkHandle);

    return mNetEngine->StartPost(mMirror->GetLocalNodeInfo().VNodeId(), mMirror->GetNodeView(),
        mMirror->GetNetProtocol(), netConf);
}

void BioClient::BioClientNetExit()
{
    mNetEngine->Exit();
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
    mMirror = MakeRef<MirrorClient>(mode);
    if (mMirror == nullptr) {
        CLIENT_LOG_ERROR("Create mirror client instance failed.");
        return BIO_ALLOC_FAIL;
    }

    mIsUpdating = false;
    UpdateView updateView = [this]() { BioClientUpdateView(); };
    uint32_t scene = mNetEngine->GetNegoWorkScene();
    uint32_t alignSize = mNetEngine->GetNegoWorkIoAlignSize();
    uint32_t timeOut = mNetEngine->GetNegoWorkIoTimeOut();
    bool enableCrc = false;
    if (mode == CONVERGENCE) {
        enableCrc = agent::BioClientAgent::Instance()->GetConfigCrcFlag();
    } else {
        enableCrc = mNetEngine->GetCrcFlag();
    }

    auto ret = mMirror->Initialize(updateView, scene, alignSize, timeOut, enableCrc);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
    }
    return ret;
}

void BioClient::BioClientMirrorExit()
{
    return;
}

BResult BioClient::BioInterceptorServerInit(WorkerMode mode)
{
    return (mode == CONVERGENCE) ? InterceptorServer::GetInstance().Initialize() : BIO_OK;
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

#ifdef USE_CLI_TOOLS
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

BResult BioClient::Start(WorkerMode mode, const ClientOptionsConfig &optConf)
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }
    mMode = mode;

    uint64_t startTime = Monotonic::TimeSec();
    if (BioClientLoggerInit(mode, optConf.logType, optConf.logFilePath) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientAgentInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

#ifdef USE_CLI_TOOLS
    if (this->BioClientDiagnoseInit(mode) != BIO_OK) {
        return BIO_ERR;
    }
    if (this->BioClientTracePointInit(mode) != BIO_OK) {
        return BIO_ERR;
    }
#endif

    NetOptions netConf;
    netConf.enableTls = optConf.enable;                      /* tls switch */
    netConf.certificationPath = optConf.certificationPath;      /* certification path */
    netConf.caCerPath = optConf.caCerPath;                      /* caCer path */
    netConf.caCrlPath = optConf.caCrlPath;                      /* caCrl path */
    netConf.privateKeyPath = optConf.privateKeyPath;            /* private key path */
    netConf.privateKeyPassword = optConf.privateKeyPassword;    /* private key password */
    netConf.hseKfsMasterPath = optConf.hseKfsMasterPath;        /* hseceasy kfs master path */
    netConf.hseKfsStandbyPath = optConf.hseKfsStandbyPath;      /* hseceasy kfs standby path */

    if (BioClientNetPreInit(mode, netConf) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientMirrorInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientNetPostInit(netConf) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioInterceptorServerInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientStartWork() != BIO_OK) {
        return BIO_ERR;
    }

    mStarted = true;
    CLIENT_LOG_INFO("Boostio client start success, cost tine:" << (Monotonic::TimeSec() - startTime) << ".");
    return BIO_OK;
}

void BioClient::Exit()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (!mStarted) {
        return;
    }
    BioClientAgentExit();
    BioClientNetExit();
    BioClientMirrorExit();
    BioClientLoggerExit(mMode);
    mStarted = false;
}