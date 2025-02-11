/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include "bio_client_log.h"
#include "bio_client_net.h"
#include "bio_client_agent.h"
#include "interceptor_server.h"
#include "bio_tracepoint_helper.h"
#include "expire_checker.h"
#include "bio_client.h"
#ifdef USE_CLI_TOOLS
#include <dlfcn.h>
#endif

using namespace ock::bio;

BResult BioClient::BioClientUnderfsInit(WorkerMode mode)
{
    if (mode == SEPARATES) {
        BioConfig::UnderFsConfig config;
        BResult ret = mNetEngine->GetUnderFsConfig(config);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to get underfs configs from server, ret:" << ret << ".");
            return ret;
        }

        UnderFs::InitUnderFsConfig(config);
        ret = UnderFs::Instance()->Init();
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to init underfs, ret:" << ret << ".");
            return ret;
        }
    }
    CLIENT_LOG_INFO("Initialize client underfs success.");
    return BIO_OK;
}

BResult BioClient::BioClientLoggerInit(WorkerMode mode, LogType logType, std::string logFilePath)
{
    auto logMode = static_cast<int32_t>(mode);
    auto defaultLogLevel = static_cast<int32_t>(BioClientLog::Level::LOG_LEVEL_INFO);
    auto type = static_cast<uint8_t>(logType);
    auto clientLog = BioClientLog::Instance();
    if (clientLog == nullptr) {
        return BIO_ALLOC_FAIL;
    }
    return clientLog->Initialize(logMode, defaultLogLevel, type, logFilePath);
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
    // 创建client net引擎实例, 并且执行前置初始化.
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

    // 根据配置文件中的日志等级重新设置Client端的日志打印等级.
    BioClientLog::Instance()->ResetLogLevel(mNetEngine->GetNegoLogLevel());
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
    //  创建Mirror实例.
    mMirror = MakeRef<MirrorClient>(mode);
    if (mMirror == nullptr) {
        CLIENT_LOG_ERROR("Create mirror client instance failed.");
        return BIO_ALLOC_FAIL;
    }

    // 初始化Mirror client
    mIsUpdating = false;
    UpdateView updateView = [this]() { BioClientUpdateView(); };
    bool enableCrc = (mode == CONVERGENCE) ? agent::BioClientAgent::Instance()->GetConfigCrcFlag() :
        mNetEngine->GetCrcFlag();
    auto ret = mMirror->Initialize(updateView, mNetEngine->GetNegoWorkScene(), mNetEngine->GetNegoWorkIoAlignSize(),
        mNetEngine->GetNegoWorkIoTimeOut(), enableCrc);
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

BResult BioClient::BioClientStartPrometheus()
{
    std::string listenAddress;
    if (mMode == CONVERGENCE) {
        listenAddress = agent::BioClientAgent::Instance()->GetPrometheusListenAddress();
    } else {
        listenAddress = mNetEngine->GetPrometheusListenAddress();
    }
    uint32_t timeOut = (mMode == CONVERGENCE) ? agent::BioClientAgent::Instance()->GetNegoWorkIoTimeOut() :
                       mNetEngine->GetNegoWorkIoTimeOut();
    uint32_t scrapeIntervalSec = (mMode == CONVERGENCE) ? agent::BioClientAgent::Instance()->
            GetPrometheusScrapeIntervalSec() : mNetEngine->GetPrometheusScrapeIntervalSec();
    auto prometheusManager = PrometheusManager::Instance(listenAddress, timeOut, scrapeIntervalSec);
    auto ret = prometheusManager->Start();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start prometheus, ret:" << ret << ".");
        return ret;
    }

    return BIO_OK;
}

void BioClient::BioClientExitPrometheus()
{
#ifndef DEBUG_UT
#ifdef USE_PROMETHEUS
    std::string listenAddress;
    if (mMode == CONVERGENCE) {
        listenAddress = agent::BioClientAgent::Instance()->GetPrometheusListenAddress();
    } else {
        listenAddress = mNetEngine->GetPrometheusListenAddress();
    }
    uint32_t timeOut = (mMode == CONVERGENCE) ? agent::BioClientAgent::Instance()->GetNegoWorkIoTimeOut() :
        mNetEngine->GetNegoWorkIoTimeOut();
    uint32_t scrapeIntervalSec = (mMode == CONVERGENCE) ? agent::BioClientAgent::Instance()->
        GetPrometheusScrapeIntervalSec() : mNetEngine->GetPrometheusScrapeIntervalSec();
    auto prometheusManager = PrometheusManager::Instance(listenAddress, timeOut, scrapeIntervalSec);
    prometheusManager->Stop();
#endif
#endif
}

#ifdef USE_CLI_TOOLS
using SdkDiagnose = int (*)();
BResult BioClient::BioDiagnoseSdkInit()
{
#ifdef DEBUG_UT
    const char *soFileName = "libsdk_diagnose.so";
    void *handler = dlopen(soFileName, RTLD_NOW);
#else
    std::string soFileName = std::string(PROJECT_PATH_PREFIX) + "/lib/libsdk_diagnose.so";
    char *canonicalPath = realpath(soFileName.c_str(), nullptr);
    if (canonicalPath == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library, not exist, " << soFileName << ".");
        return BIO_NOT_EXISTS;
    }

    void *handler = dlopen(canonicalPath, RTLD_NOW);
    free(canonicalPath);
    canonicalPath = nullptr;
#endif
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

    // 1. 初始化client端Logger.
    if (BioClientLoggerInit(mode, optConf.logType, optConf.logFilePath) != BIO_OK) {
        return BIO_ERR;
    }

    // 2. 初始化client端Agent, 融合部署场景会初始化bio server.
    if (BioClientAgentInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    // 3. 初始化client端的CLI和TP功能, 仅debug生效.
#ifdef USE_CLI_TOOLS
    if (this->BioClientDiagnoseInit(mode) != BIO_OK) {
        return BIO_ERR;
    }
    if (this->BioClientTracePointInit(mode) != BIO_OK) {
        return BIO_ERR;
    }
#endif

    // 4. 初始化Net第一步, 分离部署场景: 1)创建IPC服务; 2)与本地sever建立连接; 3)创建shm pool; 4)获取配置项.
    NetOptions netConf;
    netConf.FillNetTlsConfigs(optConf.enable, optConf.certificationPath, optConf.caCerPath, optConf.caCrlPath,
        optConf.privateKeyPath, optConf.privateKeyPassword, optConf.hseKfsMasterPath, optConf.hseKfsStandbyPath);
    if (optConf.enable) {
        bool checkCaPath = FileUtil::CanonicalPath(netConf.caCerPath)
                           && FileUtil::CanonicalPath((netConf.caCrlPath))
                           && FileUtil::CanonicalPath(netConf.certificationPath)
                           && FileUtil::CanonicalPath(netConf.hseKfsMasterPath)
                           && FileUtil::CanonicalPath(netConf.hseKfsStandbyPath);
        if (!checkCaPath) {
            CLIENT_LOG_ERROR("Check ca path failed .");
            return BIO_ERR;
        }
    }
    if (BioClientNetPreInit(mode, netConf) != BIO_OK) {
        return BIO_ERR;
    }

    // 5. 初始化Mirror client.
    if (BioClientMirrorInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    // 6. 初始化Net第二步, 分离部署场景: 1)创建RPC服务; 2)与远端所有server建立连接.
    if (BioClientNetPostInit(netConf) != BIO_OK) {
        return BIO_ERR;
    }

    // 7. 初始化interceptor server.
    if (BioInterceptorServerInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    // 8. 初始化sdk端underfs
    if (BioClientUnderfsInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    // 9. bio client开工, mirror client开工去创建亲和的Flow实例.
    if (BioClientStartWork() != BIO_OK) {
        return BIO_ERR;
    }

    if (mode == SEPARATES && optConf.enable) {
        auto expireChecker = ExpireChecker::Instance();
        if (expireChecker == nullptr) {
            LOG_INFO("expire checker alloc fail.");
            return BIO_ALLOC_FAIL;
        }
        auto ret = expireChecker->ExpireCheckerInit(netConf.caCerPath, netConf.certificationPath);
        if (ret != BIO_OK) {
            return ret;
        }
    }

#ifndef DEBUG_UT
#ifdef USE_PROMETHEUS
    if (BioClientStartPrometheus() != BIO_OK) {
        return BIO_ERR;
    }
#endif
#endif

    mStarted = true;
    CLIENT_LOG_INFO("Boostio client start success, cost time:" << (Monotonic::TimeSec() - startTime) << "s.");
    return BIO_OK;
}

void BioClient::Exit()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (!mStarted) {
        return;
    }
    uint64_t startTime = Monotonic::TimeSec();
    BioClientAgentExit();
    BioClientNetExit();
    BioClientMirrorExit();
    BioClientExitPrometheus();
    BioClientLoggerExit(mMode);
    mStarted = false;
    CLIENT_LOG_INFO("Boostio client exit success, cost time:" << (Monotonic::TimeSec() - startTime) << "s.");
}
