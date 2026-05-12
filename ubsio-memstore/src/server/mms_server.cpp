/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <unistd.h>
#include <utility>
#include "expire_checker.h"
#include "mms_log.h"
#include "mms_trace.h"
#include "mms_functions.h"
#include "mms_monotonic.h"
#include "mms_server.h"

#ifdef USE_CLI_TOOLS
#include "cli.h"
#include <dlfcn.h>
#endif

namespace ock {
namespace mms {
static void Log(int level, const char *msg)
{
    if (Logger::gInstance != nullptr) {
        Logger::gInstance->Log(level, msg);
    }
}

MmsServer::MmsServer() noexcept
{
    std::vector<ModuleDesc> modules = {
#ifdef USE_CLI_TOOLS
        { "Diagnose", std::bind(&MmsServer::MmsServerDiagnoseInit, this), nullptr, nullptr, nullptr },
#endif
        { "Mem", std::bind(&MmsServer::MmsMemInit, this), nullptr, nullptr, std::bind(&MmsServer::MmsMemExit, this) },
        { "Net", std::bind(&MmsServer::MmsNetInit, this), nullptr, nullptr, std::bind(&MmsServer::MmsNetExit, this) },
        { "Cache", std::bind(&MmsServer::MmsCacheInit, this), nullptr, nullptr,
            std::bind(&MmsServer::MmsCacheExit, this) },
        { "CrbScheduler", std::bind(&MmsServer::MmsCrbSchedulerInit, this), nullptr, nullptr,
          std::bind(&MmsServer::MmsCrbSchedulerExit, this) },
        { "MmsKvServer", std::bind(&MmsServer::MmsKvServerInit, this), nullptr, nullptr,
            std::bind(&MmsServer::MmsKvServerExit, this) },
        { "CM", std::bind(&MmsServer::MmsCmInit, this), nullptr, nullptr, std::bind(&MmsServer::MmsCmExit, this) },
    };
    mService = MakeRef<MmsServiceProc>(modules);
}

BResult MmsServer::Start(ServiceCallback service)
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return MMS_OK;
    }

    if (UNLIKELY(service == nullptr)) {
        return MMS_INVALID_PARAM;
    }

    mServiceCallback = service;

    // 1. Initialize infrastructure
    std::string logPath{};
#ifdef DEBUG_UT
    logPath = "./mms_server.log";
#else
    logPath = "/var/log/mms/mms_server.log";
#endif
    if (MmsLoggerInit(logPath) != MMS_OK || MmsConfigInit() != MMS_OK) {
        return MMS_INNER_ERR;
    }

    auto &basicConfig = mConfig->GetBasicConfig();
    MMS_LOG_RESET_LEVEL(basicConfig.logLevel);

    mTaskService = ExecutorService::Create(NO_1, NO_1024);
    if (UNLIKELY(mTaskService == nullptr)) {
        LOG_ERROR("Failed to start executor for pt migrate task.");
        return MMS_ERR;
    }
    mTaskService->SetThreadName("pt-migrate");
    bool taskRet = mTaskService->Start();
    ChkTrue(taskRet, MMS_ERR, "Start pt migrate task failed, ret:" << taskRet << ".");

    // 2. Initialize mms service
    ChkTrue(mService != nullptr, MMS_ERR, "Mms service not created.");
    BResult ret = mService->Process();
    if (ret != MMS_OK) {
        return ret;
    }

    // 3. wait start finish
    while (!mStarted) {
        sleep(5U);
    }

    if (mConfig->GetNetConfig().tlsEnable) {
        auto expireChecker = ExpireChecker::Instance();
        if (expireChecker == nullptr) {
            LOG_INFO("expire checker alloc fail.");
            return MMS_ALLOC_FAIL;
        }
        ret = expireChecker->ExpireCheckerInit(mConfig->GetNetConfig().caCerPath,
            mConfig->GetNetConfig().certificationPath, mConfig->GetNetConfig().opensslLibDir);
        if (ret != MMS_OK) {
            return ret;
        }
    }

    LOG_INFO("Mms server start success, MY PID:" << getpid() << ".");
    return MMS_OK;
}

void MmsServer::Exit()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (!mStarted) {
        return;
    }
    mService->Exit();
    LOG_INFO("Mms server exit success.");
    MmsLoggerExit();
    mStarted = false;
}

BResult MmsServer::MmsConfigInit()
{
    if (mConfig != nullptr) {
        return MMS_OK;
    }

    mConfig = MmsConfig::Instance();
    if (mConfig == nullptr) {
        LOG_ERROR("Create mms configuration instance failed.");
        return MMS_ERR;
    }

    BResult result = MMS_INNER_ERR;
    result = mConfig->Initialize(std::string(PROJECT_PATH_PREFIX));
    if (result != MMS_OK) {
        LOG_ERROR("Failed to initialize configuration, result: " << result << ".");
        return MMS_ERR;
    }

    UpdateCrcSwitch(mConfig->GetBasicConfig().crcSwitch);
    tracemark::TraceMark::Init();
    tracemark::TraceMark::SetEnable(mConfig->GetBasicConfig().traceSwitch);

    return MMS_OK;
}

BResult MmsServer::MmsLoggerInit(std::string pathName)
{
    LoggerOptions loggerOptions;
    loggerOptions.logType = LOG_TYPE_FILE;
    loggerOptions.minLogLevel = SPDLOG_LEVEL_INFO;
    loggerOptions.path = std::move(pathName);
    Logger *logger = Logger::Instance(loggerOptions);
    if (logger == nullptr) {
        std::cout << "Failed to create logger instance." << std::endl;
        return MMS_ERR;
    }

    BResult ret = MMS_INNER_ERR;
    ret = logger->Init();
    if (ret != MMS_OK) {
        std::cout << "Failed to init logger, result:" << ret << ", log path:" << loggerOptions.path << "." << std::endl;
        return MMS_ERR;
    }
    return MMS_OK;
}

void MmsServer::MmsLoggerExit()
{
    Logger::Destroy();
}

BResult MmsServer::InitMemMgr()
{
    auto &memConfig = mConfig->GetMemConfig();
    MemMgrOptions options;
    uint16_t index;
    for (index = 0; index < memConfig.numaNum; index++) {
        options.numaId[index] = memConfig.numaId[index];
        options.numaSize[index] = memConfig.numaSize[index];
        options.areaSize[MMAP_AREA_IOCTX][index] = META_SHM_IOCTX_SIZE;
        options.areaSize[MMAP_AREA_BUCKET][index] = options.numaSize[index] / IO_SIZE_1G * IO_SIZE_32M;
        ValueIndexMemCfg cfg{};
        cfg.totalMemSize = options.numaSize[index] - META_SHM_IOCTX_SIZE - options.areaSize[MMAP_AREA_BUCKET][index];
        cfg.indexNodeSize = INDEX_VALUE_SIZE + sizeof(BlockHeader);
        cfg.minBlockSize = memConfig.minBlockSize + sizeof(BlockHeader) + sizeof(DataHeader);
        cfg.maxBlockSize = memConfig.maxBlockSize + sizeof(BlockHeader) + sizeof(DataHeader);
        cfg.minBlockMemRatio = static_cast<double>(memConfig.blockRate.first) / NO_10;
        cfg.maxBlockMemRatio = static_cast<double>(memConfig.blockRate.second) / NO_10;
        cfg.Calculate();
        options.areaSize[MMAP_AREA_INDEX][index] = cfg.indexMemSize;
        options.areaSize[MMAP_AREA_VALUE][index] = cfg.valueMemSize;
    }

    options.numaNum = memConfig.numaNum;
    options.areaFd[MMAP_AREA_IOCTX] = -1;
    options.areaFd[MMAP_AREA_BUCKET] = -1;
    options.areaFd[MMAP_AREA_INDEX] = -1;
    options.areaFd[MMAP_AREA_VALUE] = -1;
    options.areaMode[MMAP_AREA_IOCTX] = MMAP_MODE_RW;
    options.areaMode[MMAP_AREA_BUCKET] = MMAP_MODE_RW;
    options.areaMode[MMAP_AREA_INDEX] = MMAP_MODE_RW;
    options.areaMode[MMAP_AREA_VALUE] = MMAP_MODE_RW;

    auto ret = mMemMgr->Initialize(options, Log, true);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Mem mgr initialize failed, result:" << ret << ".");
        return ret;
    }

    mMemMgr->ResetLogLevel(mConfig->GetBasicConfig().logLevel);
    return MMS_OK;
}

BResult MmsServer::InitIndexMemAllocator()
{
    MemAllocOptions allocOptions{};
    BResult ret = mMemMgr->GetAreaMemDesc(MMAP_AREA_INDEX, allocOptions.numaId, allocOptions.numaSize,
                                          allocOptions.numaAddress, allocOptions.numaNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Mem mgr get index mem failed, result:" << ret << ".");
        return ret;
    }

    allocOptions.blockNum = NO_1;
    allocOptions.blockRate[NO_0] = NO_10;
    allocOptions.blockSize[NO_0] = INDEX_VALUE_SIZE;

    mIndexMemAllocator = MmsMemAllocator::Instance(MMAP_AREA_INDEX);
    if (UNLIKELY(mIndexMemAllocator == nullptr)) {
        LOG_ERROR("Memory alloc failed.");
        return MMS_ALLOC_FAIL;
    }

    ret = mIndexMemAllocator->Initialize(allocOptions);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Mem allocator initialize failed, result:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

BResult MmsServer::InitValueMemAllocator()
{
    auto &memConfig = mConfig->GetMemConfig();
    MemAllocOptions allocOptions{};
    BResult ret = mMemMgr->GetAreaMemDesc(MMAP_AREA_VALUE, allocOptions.numaId, allocOptions.numaSize,
                                          allocOptions.numaAddress, allocOptions.numaNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Mem mgr get data mem failed, result:" << ret << ".");
        return ret;
    }

    allocOptions.blockNum = NO_2;
    allocOptions.blockRate[NO_0] = static_cast<uint32_t>(memConfig.blockRate.first);
    allocOptions.blockRate[NO_1] = static_cast<uint32_t>(memConfig.blockRate.second);
    allocOptions.blockSize[NO_0] = memConfig.minBlockSize + sizeof(DataHeader);
    allocOptions.blockSize[NO_1] = memConfig.maxBlockSize + sizeof(DataHeader);

    mMemAllocator = MmsMemAllocator::Instance(MMAP_AREA_VALUE);
    if (UNLIKELY(mMemAllocator == nullptr)) {
        LOG_ERROR("Memory alloc failed.");
        return MMS_ALLOC_FAIL;
    }

    ret = mMemAllocator->Initialize(allocOptions);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Mem allocator initialize failed, result:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

BResult MmsServer::InitIOCtxMemAllocator()
{
    MemAllocOptions allocOptions{};
    BResult ret = mMemMgr->GetAreaMemDesc(MMAP_AREA_IOCTX, allocOptions.numaId, allocOptions.numaSize,
                                          allocOptions.numaAddress, allocOptions.numaNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Mem mgr get ioctx mem failed, result:" << ret << ".");
        return ret;
    }

    allocOptions.blockNum = NO_1;
    allocOptions.blockRate[NO_0] = NO_10;
    allocOptions.blockSize[NO_0] = mConfig->GetNetConfig().msgMaxBuffSize;

    mIoCtxMemAllocator = MmsMemAllocator::Instance(MMAP_AREA_IOCTX);
    if (UNLIKELY(mIoCtxMemAllocator == nullptr)) {
        LOG_ERROR("Memory alloc failed.");
        return MMS_ALLOC_FAIL;
    }

    ret = mIoCtxMemAllocator->Initialize(allocOptions);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Mem allocator initialize failed, result:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

BResult MmsServer::MmsMemInit()
{
    mMemMgr = MmsMemMgr::Instance();
    CheckTrue(mMemMgr != nullptr, MMS_ERR, "Mem mgr instance is nullptr.");

    BResult ret = InitMemMgr();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Init memory manage failed, ret:" << ret << ".");
        return ret;
    }

    ret = InitIndexMemAllocator();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Init index memory allocator failed, ret:" << ret << ".");
        return ret;
    }

    ret = InitValueMemAllocator();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Init value memory allocator failed, ret:" << ret << ".");
        return ret;
    }

    if (mConfig->GetBasicConfig().isSeparateMode) {
        LOG_INFO("Separate mode, no needed to init ioctx allocator.");
        return MMS_OK;
    }

    ret = InitIOCtxMemAllocator();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Init ioctx memory allocator failed, ret:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

void MmsServer::MmsMemExit()
{
}

BResult MmsServer::MmsMulticastNetInit()
{
    mMulticastEngine = MakeRef<NetMulticastEngine>();
    if (UNLIKELY(mMulticastEngine == nullptr)) {
        LOG_ERROR("Make net multicast engine failed.");
        return MMS_ALLOC_FAIL;
    }

    int16_t timeoutSec = mConfig->GetCmConfig().registeredTimeoutSec;  // 同zk心跳超时
    auto &netConfig = mConfig->GetNetConfig();
    MulticastNetMemList memList;
    BResult ret = MmsMemMgr::Instance()->GetAreaMemDesc(memList.address, memList.size, memList.num);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Mem mgr get k/v mem failed, result:" << ret << ".");
        return ret;
    }

    ret = mMulticastEngine->InitMulticast(timeoutSec, netConfig.dataIp, netConfig.multicastPort, netConfig.dataIpMask,
                                          memList);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Net multicast engine initialize failed, result:" << ret << ".");
        return ret;
    }

    EpSubscriberBrokenHandler epSubscriberBroken = [this](uint16_t peerNodeId) -> void {
        ReCreateSubscriber(peerNodeId);
    };

    ret = mMulticastEngine->RegisterSubscriberBrokenHandler(epSubscriberBroken);
    if (ret != MMS_OK) {
        LOG_ERROR("Regist subscriber broken handler failed, ret:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

void MmsServer::FillNetOptions(NetOptions &netOptions)
{
    auto &netConfig = mConfig->GetNetConfig();
    netOptions.ipMask = netConfig.dataIpMask;
    netOptions.port = netConfig.dataPort;
    netOptions.protocol = static_cast<ServiceProtocol>(netConfig.protocol);
    netOptions.connCount = netConfig.rpcConnCount;
    netOptions.isBusyPolling = netConfig.isRpcBusyPolling;
    netOptions.workerGroups = netConfig.rpcWorkerGroups;
    netOptions.workerGroupsCpuSet = netConfig.rpcWorkerGroupsCpuSet;
    netOptions.workerGroupsNum = netConfig.rpcWorkerGroupsNum;
    netOptions.role = NET_SERVER;

    // tls
    netOptions.tlsEnable = netConfig.tlsEnable;
    netOptions.certificationPath = netConfig.certificationPath;
    netOptions.caCerPath = netConfig.caCerPath;
    netOptions.caCrlPath = netConfig.caCrlPath;
    netOptions.privateKeyPath = netConfig.privateKeyPath;
    netOptions.privateKeyPasswordPath = netConfig.privateKeyPasswordPath;
    netOptions.decrypterLibPath = netConfig.decrypterLibPath;
    netOptions.opensslLibDir = netConfig.opensslLibDir;
}

BResult MmsServer::MmsUnicastNet()
{
    mNetEngine = MakeRef<NetEngine>();
    ChkTrue(mNetEngine != nullptr, MMS_ALLOC_FAIL, "Make net engine failed.");

    int16_t timeoutSec = mConfig->GetCmConfig().registeredTimeoutSec; // 同zk心跳超时
    auto &netConfig = mConfig->GetNetConfig();
    NetMemList memList;
    auto ret = MmsMemMgr::Instance()->GetAreaMemDesc(memList.address, memList.size, memList.num);
    ChkTrue(ret == MMS_OK, ret, "Mem mgr get k/v mem failed, result:" << ret << ".");
    ret = mNetEngine->Initialize(timeoutSec, netConfig.handleRequestThreadNum,
                                 netConfig.handleRequestQueueSize, Log, memList);
    ChkTrue(ret == MMS_OK, ret, "Net engine initialize failed, result:" << ret << ".");

    mNetEngine->ResetLogLevel(mConfig->GetBasicConfig().logLevel);

    auto channelBroken = [this](uint32_t nodeId, uint32_t pid) -> void {
        if (pid == 0) {
            NetReConnect(nodeId);
        }
    };
    ret = mNetEngine->RegisterChannelBrokenHandler(channelBroken);
    if (ret != MMS_OK) {
        LOG_ERROR("Net engine regist channel broken handler failed,, ret " << ret);
        return ret;
    }

    NetOptions netOptions;
    FillNetOptions(netOptions);
    ret = mNetEngine->Start(netOptions);
    ChkTrue(ret == MMS_OK, ret, "Start rpc service failed, result:" << ret << ".");

    uint16_t numaId[MAX_NUMAS_NUM];
    uint16_t numaNum;
    ret = MmsMemMgr::Instance()->GetNumaMemDesc(numaId, numaNum);
    ChkTrue(ret == MMS_OK, ret, "Mem mgr get k/v mem failed, result:" << ret << ".");
    NumaGroupIndex::Instance()->SetNumaInfo(numaId, numaNum);
    NumaGroupIndex::Instance()->SetGroupInfo(netOptions.workerGroupsNum);
    if (!mConfig->GetBasicConfig().isSeparateMode) {
        LOG_INFO("Converge mode, no needed to init ipc services.");
        return MMS_OK;
    }

    netOptions.protocol = ServiceProtocol::SHM;
    netOptions.isBusyPolling = netConfig.isIpcBusyPolling;
    netOptions.workerGroups = netConfig.ipcWorkerGroups;
    netOptions.workerGroupsCpuSet = netConfig.ipcWorkerGroupsCpuSet;
    netOptions.workerGroupsNum = netConfig.ipcWorkerGroupsNum;
    netOptions.role = NET_SERVER;
    ret = mNetEngine->Start(netOptions);
    ChkTrue(ret == MMS_OK, ret, "Startipc service failed, result:" << ret << ".");

    return MMS_OK;
}

BResult MmsServer::MmsNetInit()
{
    BResult ret = MMS_INNER_ERR;

    ret = MmsUnicastNet();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Init unicast net failed, ret:" << ret << ".");
        return ret;
    }

    if (!mConfig->GetBasicConfig().multicastSwitch) {
        return MMS_OK;
    }

    ret = MmsMulticastNetInit();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Init multicast net failed, ret:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

void MmsServer::MmsNetExit()
{
    if (mNetEngine != nullptr) {
        mNetEngine->Stop();
        mNetEngine = nullptr;
    }

    if (mMulticastEngine != nullptr) {
        mMulticastEngine->Stop();
        mMulticastEngine = nullptr;
    }
}

BResult MmsServer::MmsCacheInit()
{
    mCache = Cache::Instance();
    CheckTrue(mCache != nullptr, MMS_ERR, "Cache instance is nullptr.");

    uint64_t addr;
    uint64_t size;
    auto ret = mMemMgr->GetAreaMemDesc(MMAP_AREA_BUCKET, addr, size);
    ChkTrue(ret == MMS_OK, ret, "Mem mgr get k/v mem failed, result:" << ret << ".");

    ret = mCache->Init(addr, size, Log, true,
                       {mConfig->GetMemConfig().minBlockSize, mConfig->GetMemConfig().maxBlockSize});
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Failed to init cache instance, ret:" << ret << ".");
        return ret;
    }

    mCache->ResetLogLevel(mConfig->GetBasicConfig().logLevel);
    return MMS_OK;
}

void MmsServer::MmsCacheExit()
{
}

BResult MmsServer::MmsCmInit()
{
    mCm = Cm::Instance();
    ChkTrue(mCm != nullptr, MMS_ERR, "Cm instance is nullptr.");

    CmOptions options;
    options.role = ROLE_TOGETHER;
    options.zkIpMask = mConfig->GetCmConfig().zkHost;
    options.groups.groupId = 0;
    options.groups.replicaNum = static_cast<uint16_t>(mConfig->GetCmConfig().nodeNum);
    options.groups.initialNodeNum = static_cast<uint16_t>(mConfig->GetCmConfig().nodeNum);
    options.groups.maxNodeNum = static_cast<uint16_t>(mConfig->GetCmConfig().nodeNum);
    options.groups.maxPtNum = static_cast<uint16_t>(mConfig->GetCmConfig().nodeNum);
    options.nodeId = static_cast<uint32_t>(mConfig->GetCmConfig().nodeId);
    options.hbTempTimeout = static_cast<uint32_t>(mConfig->GetCmConfig().registeredTimeoutSec);

    auto result = mCm->Initialize(options);
    ChkTrue(result == MMS_OK, MMS_ERR, "Cm Initialize failed, result:" << result << ".");

    CmNodeInfo nodeInfo;
    nodeInfo.ip = mConfig->GetNetConfig().dataIp;
    nodeInfo.port = mConfig->GetNetConfig().dataPort;
    nodeInfo.multiPort = mConfig->GetNetConfig().multicastPort;
    uint16_t index;
    for (index = 0; index < mConfig->GetMemConfig().numaNum; index++) {
        nodeInfo.numas.push_back(mConfig->GetMemConfig().numaId[index]);
    }

    result = mCm->RegisterNode(nodeInfo);
    ChkTrue(result == MMS_OK, MMS_ERR, "Failed to register node, result: " << result << ".");

    // register listener to cm
    mCm->RegisterNodeHandler(std::bind(&MmsServer::HandleNodeEvent, this, std::placeholders::_1));
    mCm->RegisterPtMigrateHandler(std::bind(&MmsServer::HandlePtMigrateEvent, this, std::placeholders::_1));
    mCm->RegisterPtHandler(std::bind(&MmsServer::HandleCmPtEvent, this, std::placeholders::_1, std::placeholders::_2));

    result = mCm->Start();
    ChkTrue(result == MMS_OK, MMS_ERR, "Failed to start cm client, result: " << result << ".");
    return MMS_OK;
}

void MmsServer::MmsCmExit()
{
    mCm->Stop();
}

BResult MmsServer::MmsKvServerInit()
{
    mKvServer = MmsKvServer::Instance();
    ChkTrue(mKvServer != nullptr, MMS_ERR, "Mms kv server instance is nullptr.");

    BResult ret = mKvServer->Initialize();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Failed to init kv server, ret:" << ret << ".");
        return MMS_ERR;
    }

    return MMS_OK;
}

void MmsServer::MmsKvServerExit()
{
    return;
}

BResult MmsServer::MmsCrbSchedulerInit()
{
    mCrbSchedulerPtr = CrbScheduler::Instance();
    if (UNLIKELY(mCrbSchedulerPtr == nullptr)) {
        LOG_ERROR("crb scheduler is nullptr.");
        return MMS_ALLOC_FAIL;
    }

    BResult ret = mCrbSchedulerPtr->Init();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("crb scheduler is init failed, ret:" << ret << ".");
        return ret;
    }

    mCrbSchedulerPtr->SetCrcSwitch(mConfig->GetBasicConfig().crcSwitch);
    return MMS_OK;
}

void MmsServer::MmsCrbSchedulerExit()
{
    mCrbSchedulerPtr->Exit();
}

#ifdef USE_CLI_TOOLS
using ServerDiagnose = int (*)();
BResult MmsServer::MmsServerDiagnoseInit()
{
    uint32_t procPid = 800U;
    std::string diagName = "mms_s";
    int32_t ret = MMS_INNER_ERR;
    ret = cli_agent_init(procPid, const_cast<char *>(diagName.c_str()));
    if (ret != MMS_OK) {
        LOG_ERROR("init mms server diagnose fail.");
        return MMS_ERR;
    }

    const char *soFileName = "libserver_diagnose.so";
    void *handler = nullptr;
    handler = dlopen(soFileName, RTLD_NOW);
    if (handler == nullptr) {
        LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
        return MMS_ERR;
    }

    ServerDiagnose serverInitFunc = reinterpret_cast<ServerDiagnose>(dlsym(handler, "ServerDiagnoseInit"));
    ret = serverInitFunc();
    if (ret != MMS_OK) {
        LOG_ERROR("Failed to Initialize server diagnose, ret:" << ret << ".");
    }
    return MMS_OK;
}
#endif
BResult MmsServer::HandleNodeEvent(const std::map<uint16_t, CmNodeInfo> &nodeInfos)
{
    for (auto it = nodeInfos.begin(); it != nodeInfos.end(); ++it) {
        LOG_INFO("Recv " << it->second.ToString());
    }

    if (!mStarted) {
        mStarted = true;
    }

    if (mConfig->GetBasicConfig().multicastSwitch) {
        CreateSubscribers(nodeInfos);
    }

    NetConnect(nodeInfos);
    mCurNodeTimes++;
    LOG_DEBUG("Handle node event times:" << mCurNodeTimes);
    return MMS_OK;
}

BResult MmsServer::HandlePtMigrateEvent(uint16_t ptId)
{
    auto ret = mTaskService->Execute([this, ptId]() { RunPtMigrateTask(ptId); });
    if (ret == false) {
        LOG_ERROR("Execute Pt migrate task failed.");
        return MMS_ERR;
    }

    return MMS_OK;
}

BResult MmsServer::HandleCmPtEvent(const std::map<uint16_t, CmPtInfo> &ptInfos, bool serviceable)
{
    for (auto it = ptInfos.begin(); it != ptInfos.end(); ++it) {
        LOG_INFO("Recv " << it->second.ToString());
    }

    mCurPtTimes++;
    UpdateLocalPtVersion(mCm->GetLocalPtVersion());
    mCrbSchedulerPtr->UpdateLocalCopys();
    mCrbSchedulerPtr->CrbBrokenHandle(mCm->GetNodeView());
    NotifyServiceable(serviceable);

    LOG_DEBUG("Handle pt event times:" << mCurPtTimes);
    return MMS_OK;
}

void MmsServer::RunPtMigrateTask(uint16_t ptId)
{
    if (mKvServer != nullptr) {
        mKvServer->NotifyPtMigrate(ptId);
    }
    return;
}

void MmsServer::CreateSubscribers(const std::map<uint16_t, CmNodeInfo> &nodeInfos)
{
    uint16_t localNid = mCm->GetLocalNid();

    uint32_t failCnt = 0;
    for (auto it = nodeInfos.begin(); it != nodeInfos.end(); ++it) {
        if (it->second.id == localNid) {
            continue;
        }
        if (it->second.status != CM_NODE_NORMAL) {
            continue;
        }
        LOG_INFO("Subscribe to node:" << it->second.id << ", ip:" << it->second.ip << ", port:" << it->second.multiPort
                                      << ".");
        SubscriptionInfo info(it->second.id, it->second.ip, it->second.multiPort, NO_1);
        MulticastAsyncHandler handler = [this](int32_t ret, SubscriptionInfo &info) -> void {
            if (ret != MMS_OK) {
                sleep(NO_1);
                ReCreateSubscriber(info.peerNodeId);
            }
        };
        BResult ret = mMulticastEngine->AsyncConnect(info, handler);
        if (ret != MMS_OK) {
            LOG_ERROR("Subscribe to " << it->first << " failed, ret: " << ret << ".");
            failCnt++;
        }
    }

    LOG_INFO("Subscription finish, cluster node num:" << nodeInfos.size() << ", failed num: " << failCnt << ".");
}

void MmsServer::ReCreateSubscriber(uint16_t peerNodeId)
{
    std::string ip;
    uint16_t multiPort;
    if (!mCm->CheckIsOnlineMulti(peerNodeId, ip, multiPort)) {
        LOG_WARN("Target peer id:" << peerNodeId << " is offline.");
        return;
    }

    LOG_INFO("Resubscribe to node:" << peerNodeId << ", ip:" << ip << ", port:" << multiPort << ".");
    SubscriptionInfo info(peerNodeId, ip, multiPort, NO_3);
    MulticastAsyncHandler handler = [this](int32_t ret, SubscriptionInfo &info) -> void {
        if (ret != MMS_OK) {
            sleep(NO_1);
            ReCreateSubscriber(info.peerNodeId);
        }
    };

    BResult ret = mMulticastEngine->AsyncConnect(info, handler);
    if (ret != MMS_OK) {
        LOG_ERROR("Subscribe to node " << peerNodeId << " failed, ret: " << ret << ", ip:" << ip
                                       << ", port:" << multiPort << ".");
    }
}

void MmsServer::NetConnect(const std::map<uint16_t, CmNodeInfo> &nodeInfos)
{
    uint16_t localNid = mCm->GetLocalNid();

    uint32_t failCnt = 0;
    for (auto it = nodeInfos.begin(); it != nodeInfos.end(); ++it) {
        if (it->second.id == localNid) {
            continue;
        }
        if (it->second.status != CM_NODE_NORMAL) {
            continue;
        }
        LOG_INFO("Connect to node:" << it->second.id << ", ip:" << it->second.ip << ", port:" <<
            it->second.port << ".");
        ConnectInfo info(localNid, 0, it->second.id, it->second.ip, it->second.port, NO_1);
        auto handler = [this](uintptr_t userCtx, int32_t ret, ConnectInfo &info) -> void {
            if (ret != MMS_OK) {
                sleep(NO_1);
                NetReConnect(info.peerId.nid);
            }
        };
        BResult ret = mNetEngine->AsyncConnect(info, handler, 0);
        if (ret != MMS_OK) {
            LOG_ERROR("Connect to " << it->first << " failed, ret: " << ret << ".");
            failCnt++;
        }
    }
    LOG_INFO("Connection finish, cluster node num:" << nodeInfos.size() << ", failed num: " << failCnt << ".");
}

void MmsServer::NetReConnect(uint32_t peerId)
{
    uint16_t localNid = mCm->GetLocalNid();

    std::string ip;
    uint16_t port;
    if (!mCm->CheckIsOnline(peerId, ip, port)) {
        LOG_WARN("Target peer id:" << peerId << " is offline.");
        return;
    }

    LOG_INFO("ReConnect to remote node:" << peerId << ", ip:" << ip << ", port:" << port << ".");
    ConnectInfo info(localNid, 0, peerId, ip, port, NO_1);
    auto handler = [this](uintptr_t userCtx, int32_t ret, ConnectInfo &info) -> void {
        if (ret != MMS_OK) {
            sleep(NO_1);
            NetReConnect(info.peerId.nid);
        }
    };
    BResult result = mNetEngine->AsyncConnect(info, handler, 0);
    if (result != MMS_OK) {
        LOG_ERROR("Connect to " << info.peerId.nid << " failed, ret: " << result << ".");
    }
    return;
}

void MmsServer::NotifyServiceable(bool serviceable)
{
    if (mCrbSchedulerPtr != nullptr) {
        mCrbSchedulerPtr->NotifyServiceable(serviceable);
    }

    if (mKvServer != nullptr) {
        mKvServer->NotifyServiceable(serviceable);
    }

    if (mIsFirst) {
        mIsFirst = false;
        if (mConfig->GetBasicConfig().multicastSwitch) {
            mMulticastEngine->WaitForConnectDone(); // 进程启动后等组播建链完了再通知可服务
        }
        mServiceable = serviceable;
        mServiceCallback(serviceable);
        return;
    } else if (serviceable != mServiceable) {
        mServiceable = serviceable;
        mServiceCallback(serviceable);
        return;
    } else {
        return;
    }
}
}
}
