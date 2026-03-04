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
#include <unistd.h>
#include <utility>
#include "bdm_core.h"
#include "bio_config_instance.h"
#include "bio_crc_util.h"
#include "bio_functions.h"
#include "bio_log.h"
#include "bio_monotonic.h"
#include "bio_server_c.h"
#include "cache_overload_ctrl.h"
#include "cm_c.h"
#include "expire_checker.h"
#include "flow_manager.h"
#include "htracer.h"
#include "interceptor_server.h"
#include "bio_server.h"

namespace ock {
namespace bio {
static void Log(int level, const char *msg)
{
    if (Logger::gInstance != nullptr) {
        int32_t ret = Logger::gInstance->Log(level, msg);
        if (ret < 0) {
            LOG_ERROR("Logger inner error!!!");
        }
    }
}

BioServer::BioServer() noexcept
{
    std::vector<ModuleDesc> modules = {
#ifdef USE_DEBUG_TP_TOOLS
        { "Tracepoint", std::bind(&BioServer::BioServerTracePointInit, this), nullptr, nullptr, nullptr },
#endif
        { "Diagnose", std::bind(&BioServer::BioServerDiagnoseInit, this), nullptr, nullptr, nullptr },
        { "Tracer", std::bind(&BioServer::BioTraceInit, this), nullptr, nullptr,
            std::bind(&BioServer::BioTraceExit, this) },
        { "UnderFs", std::bind(&BioServer::BioUnderFsInit, this), nullptr, nullptr,
            std::bind(&BioServer::BioUnderFsExit, this) },
        { "Bdm", std::bind(&BioServer::BioBdmInit, this), nullptr, nullptr, std::bind(&BioServer::BioBdmExit, this) },
        { "Net", std::bind(&BioServer::BioNetInit, this), nullptr, nullptr, std::bind(&BioServer::BioNetExit, this) },
        { "Flow", std::bind(&BioServer::BioFlowInit, this), nullptr, nullptr,
            std::bind(&BioServer::BioFlowExit, this) },
        { "Cache", std::bind(&BioServer::BioCacheInit, this), nullptr, nullptr,
            std::bind(&BioServer::BioCacheExit, this) },
        { "MirrorServer", std::bind(&BioServer::BioMirrorServerInit, this), nullptr, nullptr,
            std::bind(&BioServer::BioMirrorServerExit, this) },
        { "CM", std::bind(&BioServer::BioCmInit, this), nullptr, nullptr,
            std::bind(&BioServer::BioCmExit, this) },
    };
    mService = MakeRef<BioServiceProc>(modules);
}

BResult BioServer::Start()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    BIO_TP_START(NO_PROCESS_SERVER_START, 0);
    if (mStarted) {
        return BIO_OK;
    }
    BIO_TP_END;

    // 1. Initialize infrastructure
    std::string path = "/var/log/boostio/";
#ifdef DEBUG_UT
    path = "./";
#endif
    std::string logPath = path + "bio.log";
    if (BioLoggerInit(logPath) != BIO_OK || BioConfigInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }

    UnderFs::InitUnderFsConfig(mConfig->GetUnderFsConfig());
    auto &daemonConfig = mConfig->GetDaemonConfig();
    BIO_LOG_RESET_LEVEL(daemonConfig.logLevel);

    // 2. Initialize boostio service
    ChkTrue(mService != nullptr, BIO_ERR, "Boostio service not created.");
    auto ret = mService->Process();
    BIO_TP_START(SERVICE_START_FAIL, &ret, BIO_ERR);
    BIO_TP_END;
    if (ret != BIO_OK) {
        return ret;
    }

    // 3. wait start finish
    while (!mStarted || mPtView.empty()) {
        sleep(5U);
    }

    if (mConfig->GetNetConfig().enableTls) {
        auto expireChecker = ExpireChecker::Instance();
        if (expireChecker == nullptr) {
            LOG_INFO("expire checker alloc fail.");
            return BIO_ALLOC_FAIL;
        }
        ret = expireChecker->ExpireCheckerInit(mConfig->GetNetConfig().tlsCaCertPath,
            mConfig->GetNetConfig().tlsServerCertPath, mConfig->GetNetConfig().opensslLibDir);
        if (ret != BIO_OK) {
            return ret;
        }
    }
    LOG_INFO("Boostio server start success.");
    return BIO_OK;
}

void BioServer::Exit()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (!mStarted) {
        return;
    }
    if (mService != nullptr) {
        mService->Exit();
    }
    LOG_INFO("Boostio server exit success.");
    BioLoggerExit();
    mStarted = false;
}

BResult BioServer::BioConfigInit()
{
    BIO_TP_START(NO_PROCESS_CONFIG, 0);
    if (mConfig != nullptr) {
        return BIO_OK;
    }
    BIO_TP_END;

    mConfig = BioConfig::Instance();
    if (mConfig == nullptr) {
        LOG_ERROR("Create bio configuration instance failed.");
        return BIO_ERR;
    }

    BResult result = BIO_INNER_ERR;
    BIO_TP_START(CONFIG_INIT_FAIL, &result, -1);
#ifdef DEBUG_UT
    const std::string confPath = "./";
#else
    const std::string confPath = "/etc/boostio/";
#endif
    result = mConfig->Initialize(confPath);
    BIO_TP_END;
    if (result != BIO_OK) {
        LOG_ERROR("Failed to initialize configuration, result: " << result << ".");
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult BioServer::BioLoggerInit(std::string pathName)
{
    LoggerOptions loggerOptions;
    loggerOptions.logType = FILE_TYPE;
    loggerOptions.minLogLevel = BIOLOG_LEVEL_INFO;
    loggerOptions.path = std::move(pathName);
    Logger *logger = Logger::Instance(loggerOptions);
    if (logger == nullptr) {
        std::cout << "Failed to create logger instance." << std::endl;
        return BIO_ERR;
    }

    BResult ret = BIO_INNER_ERR;
    BIO_TP_START(LOG_INIT_FAIL, &ret, BIO_ERR);
    ret = logger->Init();
    BIO_TP_END;
    if (ret != BIO_OK) {
        std::cout << "Failed to init logger, result:" << ret << ", log path:" << loggerOptions.path << "." << std::endl;
        return BIO_ERR;
    }
    return BIO_OK;
}

void BioServer::BioLoggerExit()
{
    Logger::Destroy();
}

BResult BioServer::BioTraceInit()
{
#ifdef DEBUG_UT
    const std::string dumpDir = "./";
#else
    const std::string dumpDir = "/var/log/boostio/trace/";
#endif
    auto ret = ock::htracer::HTracerInit(dumpDir);
    ock::htracer::HTracerSetEnable(BioConfig::Instance()->GetDaemonConfig().enableTrace);
    ChkTrue(ret == BIO_OK, BIO_ERR, "Failed to init tracer, result:" << ret << ", dumpDir:" << dumpDir << ".");
    return ret;
}

void BioServer::BioTraceExit()
{
    ock::htracer::HTracerExit();
}

BResult BioServer::BioUnderFsInit()
{
    UnderFsPtr underFsPtr = UnderFs::Instance();
    if (underFsPtr == nullptr) {
        LOG_ERROR("Create underfs instance fail.");
        return BIO_ERR;
    }
    return underFsPtr->Init();
}

void BioServer::BioUnderFsExit()
{
    UnderFs::Instance()->Stop();
}

BResult BioServer::BioBdmInit()
{
    auto &daemonConfig = mConfig->GetDaemonConfig();
    auto ret = BdmInit();
    ChkTrue(ret == BDM_CODE_OK, BIO_ERR, "Failed to init BDM, result:" << ret << ".");
    DiskDevices diskList;
    diskList.num = 0;
    for (auto diskPathStr : daemonConfig.diskList) {
        ret = strcpy_s(diskList.list[diskList.num].path, DISK_PATH_LEN, diskPathStr.c_str());
        if (ret != 0) {
            return BIO_ERR;
        }
        diskList.diskCaps[diskList.num] = static_cast<uint64_t>(FileUtil::GetDiskCapacity(diskPathStr));
        diskList.num++;
    }

    ret = BdmStart(&diskList, daemonConfig.segment);
    ChkTrue(ret == BDM_CODE_OK, BIO_ERR, "Failed to start BDM, result:" << ret << ".");

    DiskAllocator diskAllocator;
    diskAllocator.alloc = [](uint32_t bdmId, uint64_t flowId, uint64_t flowOffset, uint64_t len, uint64_t *chunkId) {
        int ret = BdmAlloc(bdmId, flowId, flowOffset, len, chunkId);
        if (ret != BDM_CODE_OK) {
            return BIO_ERR;
        }
        return BIO_OK;
    };
    diskAllocator.free = [](uint32_t bdmId, uint64_t len, uint64_t chunkId) {
        int ret = BdmFree(bdmId, len, chunkId);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            return;
        }
        return;
    };
    FlowManager::RegisterDiskAllocator(diskAllocator);
    return BIO_OK;
}

BResult BioServer::BioBdmUpdate(std::string diskPath)
{
    auto &daemonConfig = mConfig->GetDaemonConfig();
    auto diskCap = static_cast<uint64_t>(FileUtil::GetDiskCapacity(diskPath));

    auto ret = BdmUpdate(const_cast<char *>(diskPath.c_str()), daemonConfig.segment, diskCap);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        LOG_ERROR("Bdm Update fail, diskPath: " << diskPath << ".");
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult BioServer::BioDiskReset(uint16_t diskId)
{
    // 防重入
    if (BdmGetDiskStatus(diskId) == BDM_DISK_STATE_NORMAL) {
        LOG_ERROR("Bdm is already in used, diskId: " << diskId << ".");
        return BIO_ERR;
    }

    BdmSetDiskUsedStatus(diskId, true);
    return BdmResetDisk(diskId);
}

void BioServer::BioBdmExit()
{
    return;
}

BResult BioServer::StartRpcService(const NetOptions &opt)
{
    mNetEngine->SetDataPageKb(mConfig->GetDaemonConfig().segment / NO_1024);
    auto ret = mNetEngine->Start(opt);
    ChkTrue(ret == BIO_OK, BIO_ERR, "Start rpc engine failed, result:" << ret << ".");

    MemAllocator memAllocator;
    memAllocator.alloc = [this](uint64_t size, uint64_t *addr) { return this->MemAlloc(size, addr); };
    memAllocator.free = [this](uint64_t addr) { this->MemFree(addr); };
    FlowManager::RegisterMemAllocator(memAllocator);
    return BIO_OK;
}

BResult BioServer::StartIpcService(const NetOptions &opt)
{
    return mNetEngine->Start(opt);
}

BResult BioServer::BioNetInit()
{
    if (mNetEngineInited) {
        return BIO_OK;
    }
    mNetEngine = MakeRef<NetEngine>();
    ChkTrue(mNetEngine != nullptr, BIO_ALLOC_FAIL, "Make net engine failed.");

    int16_t timeoutSec = mConfig->GetCmConfig().registeredTimeoutSec; // 同zk心跳超时
    auto &netConfig = mConfig->GetNetConfig();
    auto ret =
        mNetEngine->Initialize(timeoutSec, netConfig.handleRequestThreadNum, netConfig.handleRequestQueueSize, Log);
    ChkTrue(ret == BIO_OK, ret, "Net engine initialize failed, result:" << ret << ".");

    NetOptions netOptions;
    netOptions.ipMask = netConfig.dataIpMask;
    netOptions.port = netConfig.dataPort;
    netOptions.isBusyLoop = netConfig.isRpcBusyLoop;
    netOptions.role = Role::NET_SERVER;
    netOptions.protocol = static_cast<ServiceProtocol>(netConfig.protocol);
    netOptions.memorySize = mConfig->GetDaemonConfig().memCap;
    netOptions.regShmMem = true;
    netOptions.handlerCount = netConfig.rpcDataWorkersCnt;
    netOptions.connCount = netConfig.rpcDataWorkersCnt;
    netOptions.enableTls = mConfig->GetNetConfig().enableTls;
    netOptions.certificationPath = mConfig->GetNetConfig().tlsServerCertPath;      /* certification path */
    netOptions.caCerPath = mConfig->GetNetConfig().tlsCaCertPath;                  /* caCert path */
    netOptions.caCrlPath = mConfig->GetNetConfig().tlsCaCrlPath;                   /* caCrl path */
    netOptions.privateKeyPath = mConfig->GetNetConfig().tlsServerKeyPath;          /* private key path */
    netOptions.privateKeyPassword = mConfig->GetNetConfig().tlsServerKeyPassPath;  /* private key password */
    netOptions.decrypterLibPath = mConfig->GetNetConfig().decrypterLibPath;        /* decrypter lib path */
    ret = StartRpcService(netOptions);
    ChkTrue(ret == BIO_OK, ret, "Start rpc service failed, result:" << ret << ".");

    netOptions.isBusyLoop = netConfig.isIpcBusyLoop;
    netOptions.role = Role::NET_SERVER;
    netOptions.protocol = ServiceProtocol::SHM;
    netOptions.handlerCount = netConfig.ipcDataWorkersCnt;
    netOptions.connCount = netConfig.ipcDataWorkersCnt;
    netOptions.enableTls = mConfig->GetNetConfig().enableTls;
    netOptions.certificationPath = mConfig->GetNetConfig().tlsServerCertPath;      /* certification path */
    netOptions.caCerPath = mConfig->GetNetConfig().tlsCaCertPath;                  /* caCert path */
    netOptions.caCrlPath = mConfig->GetNetConfig().tlsCaCrlPath;                   /* caCrl path */
    netOptions.privateKeyPath = mConfig->GetNetConfig().tlsServerKeyPath;          /* private key path */
    netOptions.privateKeyPassword = mConfig->GetNetConfig().tlsServerKeyPassPath;  /* private key password */
    netOptions.decrypterLibPath = mConfig->GetNetConfig().decrypterLibPath;        /* decrypter lib path */
    ret = StartIpcService(netOptions);
    ChkTrue(ret == BIO_OK, ret, "Start ipc service failed, result:" << ret << ".");
    mNetEngineInited = true;
    return BIO_OK;
}

void BioServer::BioNetExit()
{
    if (mNetEngine != nullptr) {
        mNetEngine->Stop();
        mNetEngine = nullptr;
    }
}

BResult BioServer::BioCmInit()
{
    CmOptions cmOptions;
    BResult result = BIO_OK;
    cmOptions.role = ROLE_TOGETHER;
    cmOptions.zkIpMask = mConfig->GetCmConfig().zkHost;
    cmOptions.groups.groupId = static_cast<uint16_t>(mConfig->GetCmConfig().groupId);
    cmOptions.groups.replicaNum = static_cast<uint16_t>(mConfig->GetCmConfig().copyNum);
    cmOptions.groups.initialNodeNum = static_cast<uint16_t>(mConfig->GetCmConfig().initialNodeNum);
    cmOptions.groups.maxNodeNum = static_cast<uint16_t>(mConfig->GetCmConfig().nodeNum);
    cmOptions.groups.maxPtNum = static_cast<uint16_t>(mConfig->GetCmConfig().ptNum);
    cmOptions.hbTempTimeout = static_cast<uint32_t>(mConfig->GetCmConfig().registeredTimeoutSec);
    cmOptions.hbPermFaultTime = static_cast<uint32_t>(mConfig->GetCmConfig().registeredPermTimeoutSec);
    mCm = Cm::Instance();
    ChkTrue(mCm != nullptr, BIO_ERR, "cm instance is nullptr.");

    result = mCm->Initialize(cmOptions);
    ChkTrue(result == BIO_OK, BIO_ERR, "Cm Initialize failed, result:" << result << ".");

    auto &daemonConfig = mConfig->GetDaemonConfig();
    CmNodeInfo nodeInfo;
    nodeInfo.ip = mConfig->GetNetConfig().dataIp;
    nodeInfo.port = mConfig->GetNetConfig().dataPort;
    CmDiskInfo diskInfo;
    for (uint32_t index = 0; index < daemonConfig.diskList.size(); index++) {
        diskInfo.diskId = index;
        if (BdmGetDiskStatus(index) == BDM_DISK_STATE_NORMAL) {
            diskInfo.diskStatus = CM_DISK_NORMAL;
        } else {
            diskInfo.diskStatus = CM_DISK_FAULT;
        }
        nodeInfo.disks.push_back(diskInfo);
    }
    result = mCm->RegisterNode(nodeInfo);
    ChkTrue(result == BIO_OK, BIO_ERR, "Failed to register node, result: " << result << ".");

    // register listener to cm
    mCm->RegisterNodeHandler(std::bind(&BioServer::HandleCmNodeEvent, this, std::placeholders::_1));
    mCm->RegisterPtHandler(std::bind(&BioServer::HandleCmPtEvent, this, std::placeholders::_1));

    result = mCm->Start();
    ChkTrue(result == BIO_OK, BIO_ERR, "Failed to start cm client, result: " << result << ".");
    return BIO_OK;
}

void BioServer::BioCmExit()
{
    mCm->Stop();
}

BResult BioServer::BioMirrorServerInit()
{
    BIO_TP_START(NO_PROCESS_MIRROR_SERVER_INIT, 0);
    if (mMirrorInited) {
        return BIO_OK;
    }
    BIO_TP_END;

    mMirror = MirrorServer::Instance();
    ChkTrue(mMirror != nullptr, BIO_ERR, "Mirror server instance is nullptr.");

    BResult ret = mMirror->Initialize();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init mirror server, ret:" << ret << ".");
        return BIO_ERR;
    }

    mMirrorCrb = MirrorServerCrb::Instance();
    ChkTrue(mMirrorCrb != nullptr, BIO_ERR, "Mirror server crb instance is nullptr.");
    ret = mMirrorCrb->Init();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init mirror server crb, ret:" << ret << ".");
        return BIO_ERR;
    }
    mMirrorInited = true;

    return BIO_OK;
}

void BioServer::BioMirrorServerExit()
{
    return;
}

BResult BioServer::BioCacheInit()
{
    BIO_TP_START(NO_PROCESS_CACHE_INIT, 0);
    if (mCacheInited) {
        return BIO_OK;
    }
    BIO_TP_END;

    BResult ret = BIO_OK;
    BIO_TP_START(NO_PROCESS_CACHE_PROCESS, 0);
    ret = Cache::Instance().Init();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init cache instance, ret:" << ret << ".");
        return ret;
    }

    GetLocDiskId getLocDiskId = [](uint16_t ptId, uint16_t &diskId) -> BResult {
        return Cm::Instance()->GetLocalDiskId(ptId, diskId);
    };
    Cache::Instance().RegGetLocDiskId(getLocDiskId);

    GetLocDiskStatus getLocDiskStatus = [](uint16_t ptId, uint16_t diskId, bool &isNormal) -> void {
        Cm::Instance()->GetLocalDiskStatus(ptId, diskId, isNormal);
    };
    Cache::Instance().RegGetLocDiskStatus(getLocDiskStatus);

    CheckServiceState checkService = []() -> bool {
        return Cm::Instance()->GetServiceState();
    };
    Cache::Instance().RegCheckServiceState(checkService);

    CheckDegrade checkDegrade = [](uint16_t ptId, bool &isDegrade) -> BResult {
        return Cm::Instance()->CheckPtDegrade(ptId, isDegrade);
    };
    Cache::Instance().RegCheckDegrade(checkDegrade);

    GetGlobEvictOffset evictOffset = [](uint16_t ptId, uint64_t flowId, uint64_t &flowOffset) -> BResult {
        return MirrorServer::Instance()->GetFlowGlobEvictOffset(ptId, flowId, flowOffset);
    };
    Cache::Instance().RegGetGlobEvictOffset(evictOffset);

    CheckLocRole checkLocRole = [](uint16_t ptId, bool &isMaster) -> BResult {
        return Cm::Instance()->CheckLocalRole(ptId, isMaster);
    };
    Cache::Instance().RegCheckLocRole(checkLocRole);

    auto channelBroken = [this](uint32_t nodeId, uint32_t pid) -> void {
        if (pid != 0) {
            Cache::Instance().HandleProcBroken(pid);
        } else {
            ReConnect(nodeId);
        }
        nodeId = (nodeId == 1024) ? mLocalNid.VNodeId() : nodeId;
        QuotaHolder holder = { nodeId, static_cast<uint64_t>(pid) };
        CacheOverloadCtrl::Instance().RecycleQuota(holder);
    };
    ret = mNetEngine->RegisterChannelBrokenHandler(channelBroken);
    if (ret != BIO_OK) {
        LOG_ERROR("Net engine regist channel broken handler failed,, ret " << ret);
        return ret;
    }
    BIO_TP_END;

    ret = Cache::Instance().Recover();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to recover cache instance, ret:" << ret << ".");
        return ret;
    }

    mCacheInited = true;
    return BIO_OK;
}

void BioServer::BioCacheExit()
{
    Cache::Instance().Exit();
}

BResult BioServer::BioFlowInit()
{
    GetCacheType getCacheType = [](uint64_t flowId) -> FlowCache {
        return Cache::Instance().GetFlowCache(flowId);
    };
    FlowManager::RegisterGetCacheType(getCacheType);

    auto flowManager = FlowManager::Instance();
    ChkTrue(flowManager != nullptr, BIO_ERR, "Flow manager instance is nullptr.");
    return flowManager->Init();
}

void BioServer::BioFlowExit()
{
    FlowManager::Instance()->Exit();
}

using CLIAgentInitFunc = int (*)(uint32_t, char *);
BResult BioServer::BioServerDiagnoseInit()
{
#ifdef DEBUG_UT
    return BIO_OK;
#endif

#ifdef OPEN_RELEASE
    if (!mConfig->GetDaemonConfig().enableCli) {
        LOG_DEBUG("not open cli, skip");
        return BIO_OK;
    }
#endif
    const char* soFileName = "libcli_agent.so";
    void *handler = dlopen(soFileName, RTLD_NOW);
    if (handler == nullptr) {
        LOG_ERROR("Failed to open library() " << soFileName << " dlopen, error " << dlerror());
        return BIO_INNER_ERR;
    }

    auto ptr = LoadFunction("CLI_AgentInit", handler);
    if (ptr == nullptr) {
        LOG_ERROR("Failed to load function CLI_AgentInit.");
        dlclose(handler);
        return BIO_ERR;
    }

    auto cliAgentInitFunc = reinterpret_cast<CLIAgentInitFunc>(ptr);
    uint32_t procPid = 456U;
    std::string diagName = "bio_server";

    BResult ret = BIO_OK;
    ret = cliAgentInitFunc(procPid, const_cast<char *>(diagName.c_str()));
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to Initialize cli, ret:" << ret << ".");
        dlclose(handler);
        return BIO_INNER_ERR;
    }

    ret = this->BioServerDiagnoseInitInner();
    if (ret != BIO_OK) {
        LOG_ERROR("inner init bio server diagnose fail.");
    }
    dlclose(handler);
    return ret;
}

using ServerDiagnose = int (*)();
BResult BioServer::BioServerDiagnoseInitInner()
{
#ifdef DEBUG_UT
    return BIO_OK;
#endif
    std::string soFileName = "/usr/lib64/boostio/test_tools/libserver_diagnose.so";
    char *canonicalPath = realpath(soFileName.c_str(), nullptr);
    if (canonicalPath == nullptr) {
        LOG_ERROR("Failed to open library, not exist, " << soFileName << ".");
        return BIO_NOT_EXISTS;
    }

    auto handler = dlopen(canonicalPath, RTLD_NOW);
    free(canonicalPath);
    canonicalPath = nullptr;
    if (handler == nullptr) {
        LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
        return BIO_ERR;
    }

    ServerDiagnose serverInitFunc = reinterpret_cast<ServerDiagnose>(dlsym(handler, "ServerDiagnoseInit"));
    if (serverInitFunc == nullptr) {
        dlclose(handler);
        return BIO_INNER_ERR;
    }

    BResult ret = BIO_INNER_ERR;
    ret = serverInitFunc();
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to Initialize server diagnose, ret:" << ret << ".");
        dlclose(handler);
    }
    return ret;
}

#ifdef USE_DEBUG_TP_TOOLS
BResult BioServer::BioServerTracePointInit()
{
#ifdef USE_DEBUG_TP_TOOLS
    auto ret = tp::TracePointManager::Initialize();
    if (ret != BIO_OK) {
        LOG_ERROR("Init bio server tracepoint fail.");
        return BIO_ERR;
    }
#endif
    return BIO_OK;
}
#endif

void BioServer::Connection()
{
    uint32_t failCnt = 0;
    for (auto it = mNodeView.begin(); it != mNodeView.end(); ++it) {
        if (it->second.id.VNodeId() == mLocalNid.VNodeId()) {
            continue;
        }
        if (it->second.status != CM_NODE_NORMAL) {
            continue;
        }
        LOG_INFO("Connect to node:" << it->second.id.VNodeId() << ", ip:" << it->second.ip << ", port:" <<
            it->second.port << ".");
        ConnectInfo info(mLocalNid.VNodeId(), 0, it->second.id.VNodeId(), it->second.ip, it->second.port, NO_1);
        auto handler = [this](uintptr_t userCtx, int32_t ret, ConnectInfo &info) -> void {
            if (ret != BIO_OK) {
                ReConnect(info.peerId.nid);
            }
        };
        BResult ret = mNetEngine->AsyncConnect(info, handler, 0);
        if (ret != BIO_OK) {
            LOG_ERROR("Connect to " << it->first.ToString() << " failed, ret: " << ret << ".");
            failCnt++;
        }
    }
    sleep(NO_1);
    LOG_INFO("Connection finish, cluster node num:" << mNodeView.size() << ", failed num: " << failCnt << ".");
}

void BioServer::ReConnect(uint32_t peerId)
{
    std::string ip;
    uint16_t port;
    if (!CheckIsOnline(peerId, ip, port)) {
        LOG_WARN("Target peer id:" << peerId << " is offline.");
        return;
    }
    LOG_INFO("ReConnect to remote node:" << peerId << ", ip:" << ip << ", port:" << port << ".");
    sleep(NO_2);
    ConnectInfo info(mLocalNid.VNodeId(), 0, peerId, ip, port, NO_1);
    auto handler = [this](uintptr_t userCtx, int32_t ret, ConnectInfo &info) -> void {
        if (ret != BIO_OK) {
            ReConnect(info.peerId.nid);
        }
    };
    BResult result = mNetEngine->AsyncConnect(info, handler, 0);
    if (result != BIO_OK) {
        LOG_ERROR("Connect to " << info.peerId.nid << " failed, ret: " << result << ".");
    }
    return;
}

BResult BioServer::HandleCmNodeEvent(const std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeInfos)
{
    uint64_t nodeSize = nodeInfos.size();
    BIO_TP_START(LARGE_NODE_LIST, &nodeSize, 65535);
    BIO_TP_END;
    if (nodeSize > NO_256) {
        LOG_ERROR("Invalid node size :" << nodeInfos.size() << ".");
        return BIO_ERR;
    }
    for (auto it = nodeInfos.begin(); it != nodeInfos.end(); ++it) {
        LOG_INFO("Node:" << it->first.ToString() << ", " << it->second.ToString());
    }

    if (!mStarted) {
        mLocalNid = mCm->GetCmLocalNodeId();
        mNetEngine->SetLocalNodeId(mLocalNid.VNodeId());
        mStarted = true;
    }

    std::lock_guard<std::mutex> lock(mNodeViewMutex);
    mNodeView = nodeInfos;
    Connection();

    mCurNodeTimes = Monotonic::TimeUs();
    LOG_INFO("Cur node times:" << mCurNodeTimes);
    return BIO_OK;
}

bool BioServer::CheckNeedCrb(const std::map<uint16_t, CmPtInfo> &ptInfos)
{
    for (const auto &ptInfo: ptInfos) {
        for (const auto &copy: ptInfo.second.copys) {
            if (copy.state == CM_COPY_RECOVERY) {
                return true;
            }
        }
    }
    return false;
}

BResult BioServer::HandleCmPtEvent(const std::map<uint16_t, CmPtInfo> &ptInfos)
{
    std::lock_guard<std::mutex> lock(mPtViewMutex);

    if (CheckNeedCrb(ptInfos)) {
        mCrbProcessing.store(true);
    }

    mPtView = ptInfos;
    for (auto it = mPtView.begin(); it != mPtView.end(); ++it) {
        LOG_INFO("Recv ptId:" << it->second.ptId << ", " << it->second.ToString());
    }

    auto pFunc = [this]() {
        this->mCrbProcessing.store(false);
    };
    auto ret = mMirrorCrb->NotifyPtChangeEvent(ptInfos, pFunc);
    if (ret != BIO_OK) {
        LOG_ERROR("Handle ptevent fail, ret:" << ret);
        return ret;
    }
    mCurPtTimes = Monotonic::TimeUs();
    LOG_INFO("Cur pt times:" << mCurPtTimes);
    return BIO_OK;
}
}
}

using namespace ock::bio;

int32_t BioServerInit()
{
    auto bioServer = BioServer::Instance();
    if (UNLIKELY(bioServer == nullptr)) {
        LOG_ERROR("Make bio server instance failed.");
        return BIO_ALLOC_FAIL;
    }
    return bioServer->Start();
}

void BioServerExit(void)
{
    BioServer::Instance()->Exit();
}

uintptr_t GetBioServerNet()
{
    NetEnginePtr netEngine = BioServer::Instance()->GetNetEngine();
    return reinterpret_cast<uintptr_t>(netEngine.Get());
}

bool GetCrcFlag()
{
    return BioServer::Instance()->GetCrcFlag();
}

bool GetCliFlag()
{
    return BioServer::Instance()->GetCliFlag();
}

bool GetPrometheusToggle()
{
    return BioServer::Instance()->GetPrometheusToggle();
}

const char *GetPrometheusListenAddress(void)
{
    static std::string listenAddress = BioServer::Instance()->GetPrometheusListenAddress();
    const char *listenAddressCStr = listenAddress.c_str();
    return listenAddressCStr;
}

uint32_t GetNegoWorkIoTimeOut()
{
    return BioServer::Instance()->GetNegoWorkIoTimeOut();
}

uint32_t GetPrometheusScrapeIntervalSec()
{
    return BioServer::Instance()->GetPrometheusScrapeIntervalSec();
}

int32_t GetLocalNid(GetLocalNidResponse *rsp)
{
    CmNodeId localNid = BioServer::Instance()->GetLocalNid();
    rsp->groupId = localNid.GroupId();
    rsp->nodeId = localNid.VNodeId();
    rsp->protocol = BioServer::Instance()->GetNetProtocol();
    return 0;
}

int32_t GetQuotaInfo(QueryQuotaRequest *req, QueryQuotaResponse *rsp)
{
    BioServer::Instance()->GetMirrorServer()->QueryCacheQuota(*req, *rsp);
    return BIO_OK;
}

int32_t AllocQuota(AllocQuotaRequest *req, AllocQuotaResponse *rsp)
{
    return BioServer::Instance()->GetMirrorServer()->AllocCacheQuota(*req, *rsp);
}

int32_t FreeQuota(FreeQuotaRequest *req)
{
    return BioServer::Instance()->GetMirrorServer()->FreeCacheQuota(*req);
}

int32_t GetNodeView(QueryNodeViewRequest *req, QueryNodeViewResponse *rsp)
{
    BioServer::Instance()->GetMirrorServer()->QueryNodeView(*req, *rsp);
    return BIO_OK;
}

int32_t GetPtView(QueryPtViewRequest *req, QueryPtViewResponse *rsp)
{
    BioServer::Instance()->GetMirrorServer()->QueryPtView(*req, *rsp);
    return BIO_OK;
}

int32_t CreateFlowMaster(CreateFlowRequest *req, CreateFlowResponse *rsp)
{
    BResult ret = BioServer::Instance()->GetMirrorServer()->CreateFlowMaster(req->comm.pid, req->comm.ptId,
        req->comm.ptv, rsp->flowId, rsp->isDegrade);
    return static_cast<int32_t>(ret);
}

int32_t CreateFlowSlave(CreateFlowRequest *req)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->CreateFlowSlave(req->comm.pid, req->comm.ptId,
        req->comm.ptv, req->flowId, req->isDegrade));
}

int32_t DestroyFlow(DestroyFlowRequest *req)
{
    return BioServer::Instance()->GetMirrorServer()->DestroyFlow(req->comm.pid, req->comm.ptId,
                                                                 req->comm.ptv, req->flowId);
}

int32_t GetSlice(GetSliceRequest *req, GetSliceResponse **rsp)
{
    uint64_t base = BioServer::Instance()->GetPtEntry(req->comm.ptId).version;
    if (UNLIKELY(req->comm.ptv != base)) {
        LOG_WARN("Check message pt version failed, base:" << base << ", ptv:" << req->comm.ptv << ".");
        return BIO_CHECK_PT_FAIL;
    }
    WCacheSlicePtr sliceP = nullptr;
    BResult ret = BioServer::Instance()->GetMirrorServer()->GetSlice(req->flowId, req->flowOffset, req->flowIndex,
        req->length, sliceP);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get slice failed:" << ret << ".");
        return static_cast<int32_t>(ret);
    }
    std::vector<FlowAddr> addrVec = sliceP->GetAddrs();
    if (addrVec.size() > SLICE_ADDR_MAX_SIZE) {
        LOG_ERROR("Slice addr num " << addrVec.size() << " exceed 32.");
        return static_cast<int32_t>(BIO_INNER_ERR);
    }

    uint64_t sliceLen = sliceP->GetSerializeLen();
    auto *tmp = new (std::nothrow) uint8_t[sizeof(GetSliceResponse) + sliceLen];
    if (UNLIKELY(tmp == nullptr)) {
        LOG_ERROR("Alloc memory failed, len:" << sizeof(GetSliceResponse) + sliceLen << ".");
        return static_cast<int32_t>(BIO_INNER_ERR);
    }
    *rsp = static_cast<GetSliceResponse *>(static_cast<void *>(tmp));
    (*rsp)->addrNum = addrVec.size();
    for (uint32_t i = 0; i < addrVec.size(); i++) {
        (*rsp)->addr[i].chunkId = addrVec[i].chunkId;
        (*rsp)->addr[i].chunkOffset = addrVec[i].chunkOffset;
        (*rsp)->addr[i].chunkLen = addrVec[i].chunkLen;
    }
    (*rsp)->sliceLen = sliceLen;
    uint64_t outSliceLen = 0;
    ret = sliceP->Serialize((*rsp)->sliceBuf, (*rsp)->sliceLen, outSliceLen);
    if (ret != BIO_OK) {
        LOG_ERROR("Serialize slice failed, ret " << ret);
        return static_cast<int32_t>(BIO_INNER_ERR);
    }
    if (UNLIKELY(outSliceLen != sliceLen)) {
        LOG_ERROR("Serialize slice failed, outSliceLen:" << outSliceLen << ", sliceLen:" << sliceLen << ".");
        return static_cast<int32_t>(BIO_INNER_ERR);
    }

    return BIO_OK;
}

inline static void StatisticPutIoSize(uint64_t length)
{
    if (length <= IO_SIZE_4K) {
        BIO_TRACE_START(MIRROR_TRACE_W_S_1_4K);
        BIO_TRACE_END(MIRROR_TRACE_W_S_1_4K, BIO_OK);
    } else if (length <= IO_SIZE_8K) {
        BIO_TRACE_START(MIRROR_TRACE_W_S_4_8K);
        BIO_TRACE_END(MIRROR_TRACE_W_S_4_8K, BIO_OK);
    } else if (length <= IO_SIZE_64K) {
        BIO_TRACE_START(MIRROR_TRACE_W_S_8_64K);
        BIO_TRACE_END(MIRROR_TRACE_W_S_8_64K, BIO_OK);
    } else if (length <= IO_SIZE_128K) {
        BIO_TRACE_START(MIRROR_TRACE_W_S_64_128K);
        BIO_TRACE_END(MIRROR_TRACE_W_S_64_128K, BIO_OK);
    } else if (length <= IO_SIZE_256K) {
        BIO_TRACE_START(MIRROR_TRACE_W_S_128_256K);
        BIO_TRACE_END(MIRROR_TRACE_W_S_128_256K, BIO_OK);
    } else if (length <= IO_SIZE_1M) {
        BIO_TRACE_START(MIRROR_TRACE_W_S_256K_1M);
        BIO_TRACE_END(MIRROR_TRACE_W_S_256K_1M, BIO_OK);
    } else if (length <= IO_SIZE_2M) {
        BIO_TRACE_START(MIRROR_TRACE_W_S_1_2M);
        BIO_TRACE_END(MIRROR_TRACE_W_S_1_2M, BIO_OK);
    } else if (length <= IO_SIZE_4M) {
        BIO_TRACE_START(MIRROR_TRACE_W_S_2_4M);
        BIO_TRACE_END(MIRROR_TRACE_W_S_2_4M, BIO_OK);
    } else {
        BIO_TRACE_START(MIRROR_TRACE_W_S_4M);
        BIO_TRACE_END(MIRROR_TRACE_W_S_4M, BIO_OK);
    }
}

int32_t Put(PutRequest *req, PutResponse *rsp)
{
    BResult ret = BIO_OK;
    WCacheSlicePtr sliceP = nullptr;
    if (req->sliceLen == 0) {
        MrInfo mrInfo = { req->mrAddress, static_cast<uint32_t>(req->mrSize) };
        std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
        BIO_TP_START(PUT_SLICELEN_ZERO_ALLOC_SLICE_FAIL, &sliceP, nullptr);
        sliceP = MakeRef<WCacheSlice>(req->flowId, req->flowOffset, req->flowIndex, req->length, addrVec);
        BIO_TP_END;
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            return BIO_ALLOC_FAIL;
        }
        sliceP->SetDataCrc(req->dataCrc);
    } else {
        BIO_TP_START(PUT_ALLOC_SLICE_FAIL, &sliceP, nullptr);
        sliceP = MakeRef<WCacheSlice>();
        BIO_TP_END;
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            return BIO_ALLOC_FAIL;
        }
        ret = sliceP->Deserialize(req->sliceBuf, req->sliceLen);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Deserialize slice failed, ret:" << ret << ".");
            return ret;
        }
        sliceP->SetDataCrc(req->dataCrc);
    }

    StatisticPutIoSize(req->length);
    ServiceContext netCtx;
    BIO_TRACE_START(MIRROR_TRACE_PUT_RECEIVE_LOCAL);
    uint32_t ioStrategy = 0;
    ret = BioServer::Instance()->GetMirrorServer()->Put(*req, sliceP, netCtx, ioStrategy);
    BIO_TRACE_END(MIRROR_TRACE_PUT_RECEIVE_LOCAL, ret);
    rsp->ioStrategy = ioStrategy;
    return ret;
}

int32_t AddDisk(AddDiskRequest *req, AddDiskResponse *rsp)
{
    BResult ret = BIO_ERR;
    ret = BioServer::Instance()->GetMirrorServer()->AddDisk(*req);
    return ret;
}

inline static void StatisticGetIoSize(uint64_t length)
{
    if (length <= IO_SIZE_4K) {
        BIO_TRACE_START(MIRROR_TRACE_R_S_1_4K);
        BIO_TRACE_END(MIRROR_TRACE_R_S_1_4K, BIO_OK);
    } else if (length <= IO_SIZE_8K) {
        BIO_TRACE_START(MIRROR_TRACE_R_S_4_8K);
        BIO_TRACE_END(MIRROR_TRACE_R_S_4_8K, BIO_OK);
    } else if (length <= IO_SIZE_64K) {
        BIO_TRACE_START(MIRROR_TRACE_R_S_8_64K);
        BIO_TRACE_END(MIRROR_TRACE_R_S_8_64K, BIO_OK);
    } else if (length <= IO_SIZE_128K) {
        BIO_TRACE_START(MIRROR_TRACE_R_S_64_128K);
        BIO_TRACE_END(MIRROR_TRACE_R_S_64_128K, BIO_OK);
    } else if (length <= IO_SIZE_256K) {
        BIO_TRACE_START(MIRROR_TRACE_R_S_128_256K);
        BIO_TRACE_END(MIRROR_TRACE_R_S_128_256K, BIO_OK);
    } else if (length <= IO_SIZE_1M) {
        BIO_TRACE_START(MIRROR_TRACE_R_S_256K_1M);
        BIO_TRACE_END(MIRROR_TRACE_R_S_256K_1M, BIO_OK);
    } else if (length <= IO_SIZE_2M) {
        BIO_TRACE_START(MIRROR_TRACE_R_S_1_2M);
        BIO_TRACE_END(MIRROR_TRACE_R_S_1_2M, BIO_OK);
    } else if (length <= IO_SIZE_4M) {
        BIO_TRACE_START(MIRROR_TRACE_R_S_2_4M);
        BIO_TRACE_END(MIRROR_TRACE_R_S_2_4M, BIO_OK);
    } else {
        BIO_TRACE_START(MIRROR_TRACE_R_S_4M);
        BIO_TRACE_END(MIRROR_TRACE_R_S_4M, BIO_OK);
    }
}

int32_t Get(GetRequest *req, GetResponse *rsp)
{
    StatisticGetIoSize(req->length);
    ServiceContext netCtx;
    return BioServer::Instance()->GetMirrorServer()->GetConvergence(*req, *rsp);
}

int32_t Delete(DeleteRequest *req)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->Delete(*req));
}

int32_t List(ListRequest *req, ListResponse **rsp)
{
    std::unordered_map<std::string, ObjStat> objs;
    BResult ret = BIO_INNER_ERR;
    BIO_TP_START(LIST_LIST_FAIL, &ret, BIO_ERR);
    ret = BioServer::Instance()->GetMirrorServer()->List(*req, objs);
    BIO_TP_END;
    if (ret != BIO_OK) {
        return ret;
    }

    char *tmp = nullptr;
    BIO_TP_START(LIST_MALLOC_RSP_FAIL, &tmp, nullptr);
    tmp = new (std::nothrow) char[sizeof(ListResponse) + sizeof(ObjStat) * objs.size()];
    BIO_TP_END;
    if (UNLIKELY(tmp == nullptr)) {
        LOG_ERROR("Alloc memory failed, len:" << (sizeof(ListResponse) + sizeof(ObjStat) * objs.size()) << ".");
        return BIO_ALLOC_FAIL;
    }
    *rsp = static_cast<ListResponse *>(static_cast<void *>(tmp));
    (*rsp)->addr = 0;
    (*rsp)->addrOffset = 0;
    (*rsp)->num = objs.size();
    (*rsp)->buffLen = sizeof(ObjStat) * objs.size();
    auto statBuf = static_cast<ObjStat *>(static_cast<void *>((*rsp)->statBuf));
    uint32_t index = 0;
    for (auto &obj : objs) {
        CopyKey(statBuf[index].key, obj.second.key, MAX_KEY_SIZE);
        statBuf[index].size = obj.second.size;
        statBuf[index].time = obj.second.time;
        index++;
    }
    return BIO_OK;
}

int32_t Stat(StatRequest *req, StatResponse *rsp)
{
    ObjStat objInfo{};
    BResult ret = BioServer::Instance()->GetMirrorServer()->Stat(*req, objInfo);
    rsp->size = objInfo.size;
    rsp->time = objInfo.time;
    return static_cast<int32_t>(ret);
}

int32_t Load(LoadRequest *req)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->Load(*req));
}

int32_t NotifyUpdate(NotifyUpdateRequest *req)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->NotifyUpdate(*req));
}

int32_t CheckUpdateReady(CheckUpdateReadyRequest *req, CheckUpdateReadyResponse *rsp)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->CheckUpdateReady(*req, *rsp));
}

int32_t GetCacheHitLocal(CacheHitResponse *rsp)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->GetCacheHitLocal(rsp));
}

int32_t CalcCacheResourceLocal(CacheResourceResponse *rsp)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->CalcCacheResourceLocal(rsp));
}

int32_t GetTracePointsLocal(GetTracePointsResponse *rsp)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->GetTracePointsLocal(rsp));
}