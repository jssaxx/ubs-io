/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <dlfcn.h>
#include "bio_client_log.h"
#include "bio_client_net.h"
#include "bio_client_agent.h"
#include "interceptor_server.h"
#include "bio_tracepoint_helper.h"
#include "expire_checker.h"
#include "bio_client.h"

using namespace ock::bio;

BResult BioClient::BioClientLoggerInit(WorkerMode mode, LogType logType, std::string logFilePath)
{
    auto logMode = static_cast<int32_t>(mode);
    auto defaultLogLevel = static_cast<int32_t>(BioClientLog::Level::LOG_LEVEL_INFO);
    auto type = static_cast<uint8_t>(logType);
    auto clientLog = BioClientLog::Instance();
    if (UNLIKELY(clientLog == nullptr)) {
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
    BIO_TP_START(SDK_BIO_AGENT_CREAT_FAIL, &agentPtr, nullptr);
    agentPtr = agent::BioClientAgent::Instance();
    BIO_TP_END;
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

BResult BioClient::BioClientNetPreInit(WorkerMode mode, NetOptions &netConf)
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

BResult BioClient::BioClientNetPostInit(NetOptions &netConf)
{
    CheckNodeOnline checkHandle = [this](uint16_t nodeId, std::string &ip, uint16_t &port) -> bool {
        return mMirror->CheckIsOnline(nodeId, ip, port);
    };
    mNetEngine->RegCheckNodeOnline(checkHandle);
    netConf.transDeviceId = mDeivceId;
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
    bool enableCrc = (mode == CONVERGENCE) ? agent::BioClientAgent::Instance()->GetConfigCrcFlag() :
        mNetEngine->GetCrcFlag();
    auto ret = mMirror->Initialize(updateView, mNetEngine->GetNegoWorkScene(), mNetEngine->GetNegoWorkIoAlignSize(),
        mNetEngine->GetNegoWorkIoTimeOut(), enableCrc);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
        return ret;
    }
    if (mode == SEPARATES) {
        mNetEngine->RegIpcRecoveredHandler([this]() { return mMirror->RecoverDataMessageMem(); });
    }
    return ret;
}

void BioClient::BioClientMirrorExit()
{
    mMirror->FreeIoStrategy();
}

BResult BioClient::BioInterceptorServerInit(WorkerMode mode)
{
    return (mode == CONVERGENCE) ? InterceptorServer::GetInstance().Initialize() : BIO_OK;
}

BResult BioClient::BioClientStartWork()
{
    return mMirror->Start();
}

BResult BioClient::BioClientStartPrometheus()
{
    bool enablePrometheus = (mMode == CONVERGENCE) ? agent::BioClientAgent::Instance()->GetConfigPrometheusToggle() :
        mNetEngine->GetPrometheusToggle();
    if (!enablePrometheus) {
        return BIO_OK;
    }
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
    auto ret = prometheusManager->Start();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start prometheus, ret:" << ret << ".");
        return ret;
    }

#endif
#endif
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

using SdkDiagnose = int (*)();
using CLIAgentInitFunc = int (*)(uint32_t, char *);
BResult BioClient::BioDiagnoseSdkInit()
{
#ifdef DEBUG_UT
    return BIO_OK;
#endif
    std::string soFileName = "/usr/lib64/boostio/test_tools/libsdk_diagnose.so";
    char *canonicalPath = realpath(soFileName.c_str(), nullptr);
    if (canonicalPath == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library, not exist, " << soFileName << ".");
        return BIO_NOT_EXISTS;
    }

    void *handler = dlopen(canonicalPath, RTLD_NOW);
    free(canonicalPath);
    canonicalPath = nullptr;

    if (handler == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
        return BIO_INNER_ERR;
    }

    auto ptr = LoadFunction("SdkDiagnoseInit", handler);
    if (ptr == nullptr) {
        LOG_ERROR("Failed to load function SdkDiagnoseInit.");
        dlclose(handler);
        return BIO_ERR;
    }

    SdkDiagnose sdkInitFunc = reinterpret_cast<SdkDiagnose>(ptr);
    BResult ret = sdkInitFunc();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize sdk diagnose, ret:" << ret << ".");
        dlclose(handler);
    }
    return ret;
}

BResult BioClient::BioClientDiagnoseInit(WorkerMode mode)
{
#ifdef OPEN_RELEASE
    bool enableCli = (mode == CONVERGENCE) ? agent::BioClientAgent::Instance()->GetConfigCliFlag() :
        mNetEngine->GetCliFlag();
    if (!enableCli) {
        return BIO_OK;
    }
#endif
    BResult ret = BIO_OK;
    if (mode == SEPARATES) {
        const char* soFileName = "libcli_agent.so";
        void *handler = dlopen(soFileName, RTLD_NOW);
        if (handler == nullptr) {
            CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen, error " << dlerror());
            return BIO_INNER_ERR;
        }

        auto ptr = LoadFunction("cli_agent_init", handler);
        if (ptr == nullptr) {
            LOG_ERROR("Failed to load function cli_agent_init.");
            dlclose(handler);
            return BIO_ERR;
        }

        auto cliAgentInitFunc = reinterpret_cast<CLIAgentInitFunc>(ptr);
        std::string diagName = "bio_sdk";
        ret = cliAgentInitFunc(getpid(), const_cast<char *>(diagName.c_str()));
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to Initialize cli, ret:" << ret << ".");
            dlclose(handler);
            return BIO_INNER_ERR;
        }
    }

    ret = BioDiagnoseSdkInit();
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

BResult BioClient::BioClientCertificateExpiration(const ClientOptionsConfig &optConf)
{
    if (mMode == SEPARATES && optConf.enable != 0) { // 证书过期检查.
        auto expireChecker = ExpireChecker::Instance();
        if (expireChecker == nullptr) {
            LOG_ERROR("expire checker alloc fail.");
            return BIO_ALLOC_FAIL;
        }

        auto ret = expireChecker->ExpireCheckerInit(optConf.caCerPath, optConf.certificationPath,
            optConf.opensslLibDir);
        if (ret != BIO_OK) {
            return ret;
        }
    }
    return BIO_OK;
}

BResult BioClient::BioClientTraceInit()
{
    auto flag = mNetEngine->GetHtraceFlag();
    if (flag && mMode == SEPARATES) {
        const std::string dumpDir = "/var/log/boostio/clienttrace/";
        auto ret = ock::htracer::HTracerInit(dumpDir);
        ock::htracer::HTracerSetEnable(true);
        ChkTrue(ret == BIO_OK, BIO_ERR, "Failed to init tracer, result:" << ret << ", dumpDir:" << dumpDir << ".");
        return ret;
    }
    return BIO_OK;
}

void BioClient::BioClientTraceExit()
{
    auto flag = mNetEngine->GetHtraceFlag();
    if (flag && mMode == SEPARATES) {
        ock::htracer::HTracerExit();
    }
}

BResult BioClient::FillNetOptions(const ClientOptionsConfig &optConf, NetOptions &netConf)
{
    netConf.FillNetTlsConfigs(optConf.enable, optConf.certificationPath, optConf.caCerPath, optConf.caCrlPath,
        optConf.privateKeyPath, optConf.privateKeyPassword, optConf.decrypterLibPath);
    if (optConf.enable) {
        bool checkCaPath = FileUtil::CanonicalPath(netConf.certificationPath)
                           && FileUtil::CanonicalPath(netConf.caCerPath)
                           && FileUtil::CanonicalPath(netConf.privateKeyPath);
        if (!checkCaPath) {
            CLIENT_LOG_ERROR("Check ca path failed.");
            return BIO_ERR;
        }

        if (!netConf.caCrlPath.empty()) {
            if (!FileUtil::CanonicalPath(netConf.caCrlPath)) {
                LOG_ERROR("Invalid crl path.");
                return BIO_ERR;
            }
        }

        if (!netConf.privateKeyPassword.empty()) {
            if (!FileUtil::CanonicalPath(netConf.privateKeyPassword)) {
                LOG_ERROR("Invalid crl path.");
                return BIO_ERR;
            }
        }

        if (!netConf.decrypterLibPath.empty()) {
            if (!FileUtil::CanonicalPath(netConf.decrypterLibPath)) {
                LOG_ERROR("Invalid crl path.");
                return BIO_ERR;
            }
        }
    }
    return BIO_OK;
}

BResult BioClient::Start(WorkerMode mode, const ClientOptionsConfig &optConf, int32_t devId)
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }
    mMode = mode;
    mDeivceId = devId;
    uint64_t startTime = Monotonic::TimeSec();

    // 1. 初始化client端Logger.
    if (BioClientLoggerInit(mode, optConf.logType, optConf.logFilePath) != BIO_OK) {
        return BIO_ERR;
    }

    // 2. 初始化client端Agent, 融合部署场景会初始化bio server.
    if (BioClientAgentInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    // 3. 初始化client端的TP功能.
    if (BioClientTracePointInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    // 4. 初始化Net第一步, 分离部署场景: 1)创建IPC服务; 2)与本地sever建立连接; 3)创建shm pool; 4)获取配置项.
    NetOptions netConf;
    if (FillNetOptions(optConf, netConf)!= BIO_OK) {
        return BIO_ERR;
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

    // 8. bio client开工, 1)创建SDK端数据消息内存池; 2)mirror client开工去创建亲和的Flow实例.
    if (BioClientStartWork() != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientDiagnoseInit(mode) != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientTraceInit() != BIO_OK) {
        return BIO_ERR;
    }

    if (BioClientCertificateExpiration(optConf) != BIO_OK) {
        return BIO_ERR;
    }

#ifdef USE_PROMETHEUS
    if (BioClientStartPrometheus() != BIO_OK) {
        return BIO_ERR;
    }
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
    BioClientAgentExit();
    BioClientNetExit();
    BioClientMirrorExit();
#ifdef USE_PROMETHEUS
    BioClientExitPrometheus();
#endif
    BioClientLoggerExit(mMode);
    BioClientTraceExit();
    mStarted = false;
}

BResult BioClient::AsyncGet(MirrorClient::MirrorGet &param, AsyncOpParam &opParam)
{
    return mMirror->AsyncGet(param, opParam);
}

BResult BioClient::RegisterMem(uint64_t *addresses, uint64_t *sizes, uint32_t count)
{
    std::vector<void*> addrsVec;
    std::vector<size_t> sizesVec;
    addrsVec.reserve(count);
    sizesVec.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        addrsVec.emplace_back(reinterpret_cast<void*>(addresses[i]));
        sizesVec.emplace_back(sizes[i]);
    }
    return net::BioClientNet::Instance()->RegisterMem(addrsVec, sizesVec);
}

