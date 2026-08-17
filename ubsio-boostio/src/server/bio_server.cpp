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

#include <algorithm>
#include <dlfcn.h>
#include <limits>
#include <unistd.h>
#include <mutex>
#include <utility>
#include "bdm_core.h"
#include "bdm_disk.h"
#include "bio_config_instance.h"
#include "bio_crc_util.h"
#include "bio_file_util.h"
#include "bio_functions.h"
#include "bio_log.h"
#include "bio_monotonic.h"
#include "bio_server_c.h"
#include "cache.h"
#include "cache_overload_ctrl.h"
#include "cm_c.h"
#include "expire_checker.h"
#include "flow_manager.h"
#include "htracer.h"
#include "interceptor_server.h"
#include "standalone_device_id_gather.h"
#include "standalone_view.h"
#include "bio_server.h"

namespace ock {
namespace bio {
namespace {
std::mutex gMetaEventCallbackLock;
UbsioMetaEventCallbackC gMetaEventCallback = nullptr;
void *gMetaEventCallbackContext = nullptr;
bool gMetaEventCallbackConfigured = false;
bool gMetaEventCallbackApplicable = false;

UbsIoMetaEventCallback BuildMetaEventCallback(UbsioMetaEventCallbackC callback, void *context)
{
    if (callback == nullptr) {
        return nullptr;
    }

    return [callback, context](const std::vector<UbsIoMetaEvent> &events) {
        std::vector<UbsioMetaEventC> cEvents;
        cEvents.reserve(events.size());
        for (const auto &event : events) {
            cEvents.push_back({ static_cast<int32_t>(event.type), event.key.c_str(),
                static_cast<uint32_t>(event.key.size()) });
        }
        callback(context, cEvents.data(), static_cast<uint32_t>(cEvents.size()));
    };
}

void ApplyMetaEventCallbackLocked()
{
    if (!gMetaEventCallbackConfigured || !gMetaEventCallbackApplicable) {
        return;
    }
    Cache::Instance().RegUbsIoMetaEventCallback(
        BuildMetaEventCallback(gMetaEventCallback, gMetaEventCallbackContext));
}
}

static void Log(int level, const char *msg)
{
    if (Logger::gInstance != nullptr) {
        int32_t ret = Logger::gInstance->Log(level, msg);
        if (ret < 0) {
            LOG_ERROR("Logger inner error!!!");
        }
    }
}

std::vector<ModuleDesc> BioServer::BuildClusterModules()
{
    std::vector<ModuleDesc> modules;
#ifdef USE_DEBUG_TP_TOOLS
    modules.emplace_back("Tracepoint", std::bind(&BioServer::BioServerTracePointInit, this), nullptr, nullptr,
        nullptr);
#endif
    modules.emplace_back("Diagnose", std::bind(&BioServer::BioServerDiagnoseInit, this), nullptr, nullptr, nullptr);
    modules.emplace_back("Tracer", std::bind(&BioServer::BioTraceInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioTraceExit, this));
    modules.emplace_back("UnderFs", std::bind(&BioServer::BioUnderFsInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioUnderFsExit, this));
    modules.emplace_back("Bdm", std::bind(&BioServer::BioBdmInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioBdmExit, this));
    modules.emplace_back("Net", std::bind(&BioServer::BioNetInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioNetExit, this));
    modules.emplace_back("Flow", std::bind(&BioServer::BioFlowInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioFlowExit, this));
    modules.emplace_back("Cache", std::bind(&BioServer::BioCacheInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioCacheExit, this));
    modules.emplace_back("MirrorServer", std::bind(&BioServer::BioMirrorServerInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioMirrorServerExit, this));
    modules.emplace_back("CM", std::bind(&BioServer::BioCmInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioCmExit, this));
    return modules;
}

std::vector<ModuleDesc> BioServer::BuildStandaloneModules()
{
    std::vector<ModuleDesc> modules;
#ifdef USE_DEBUG_TP_TOOLS
    modules.emplace_back("Tracepoint", std::bind(&BioServer::BioServerTracePointInit, this), nullptr, nullptr,
        nullptr);
#endif
    modules.emplace_back("Diagnose", std::bind(&BioServer::BioServerDiagnoseInit, this), nullptr, nullptr, nullptr);
    modules.emplace_back("Tracer", std::bind(&BioServer::BioTraceInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioTraceExit, this));
    modules.emplace_back("UnderFs", std::bind(&BioServer::BioUnderFsInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioUnderFsExit, this));
    modules.emplace_back("StandaloneDeviceIdGather", std::bind(&BioServer::BioStandaloneDeviceIdGatherInit, this),
        nullptr, nullptr, nullptr);
    modules.emplace_back("Bdm", std::bind(&BioServer::BioBdmInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioBdmExit, this));
    modules.emplace_back("StandaloneMem", std::bind(&BioServer::BioStandaloneMemInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioStandaloneMemExit, this));
    modules.emplace_back("Flow", std::bind(&BioServer::BioFlowInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioFlowExit, this));
    modules.emplace_back("StandaloneView", std::bind(&BioServer::BioStandaloneViewInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioStandaloneViewExit, this));
    modules.emplace_back("Cache", std::bind(&BioServer::BioCacheInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioCacheExit, this));
    modules.emplace_back("MirrorServer", std::bind(&BioServer::BioMirrorServerInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioMirrorServerExit, this));
    return modules;
}

BioServer::BioServer() noexcept
{
    mService = nullptr;
}

BResult BioServer::InitializeRuntime()
{
    if (BioConfigInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }

    const auto &daemonConfig = mConfig->GetDaemonConfig();
    std::string logPath = FileUtil::JoinPath(daemonConfig.logPath, "bio" + std::to_string(getpid()) + ".log");
    if (BioLoggerInit(logPath) != BIO_OK) {
        return BIO_INNER_ERR;
    }

    mConfig->DumpToLog();
    BIO_LOG_RESET_LEVEL(daemonConfig.logLevel);
    return BIO_OK;
}

BResult BioServer::ProcessService(std::vector<ModuleDesc> modules)
{
    mService = MakeRef<BioServiceProc>(std::move(modules));
    ChkTrue(mService != nullptr, BIO_ERR, "Boostio service not created.");
    auto ret = mService->Process();
    BIO_TP_START(SERVICE_START_FAIL, &ret, BIO_ERR);
    BIO_TP_END;
    if (ret != BIO_OK) {
        return ret;
    }
    return BIO_OK;
}

void BioServer::WaitStartReady()
{
    while (!mStarted || mPtView.empty()) {
        sleep(5U);
    }
}

BResult BioServer::StartExpireChecker()
{
    if (!mConfig->GetNetConfig().enableTls) {
        return BIO_OK;
    }
    auto expireChecker = ExpireChecker::Instance();
    if (expireChecker == nullptr) {
        LOG_INFO("expire checker alloc fail.");
        return BIO_ALLOC_FAIL;
    }
    return expireChecker->ExpireCheckerInit(mConfig->GetNetConfig().tlsCaCertPath,
        mConfig->GetNetConfig().tlsServerCertPath, mConfig->GetNetConfig().opensslLibDir);
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
    auto ret = InitializeRuntime();
    ChkTrue(ret == BIO_OK, ret, "Initialize runtime failed, result:" << ret << ".");

    mStandaloneMode = false;

    // 2. Initialize boostio service
    ret = ProcessService(BuildClusterModules());
    ChkTrue(ret == BIO_OK, ret, "Process cluster service failed, result:" << ret << ".");

    // 3. wait start finish
    WaitStartReady();

    ret = StartExpireChecker();
    ChkTrue(ret == BIO_OK, ret, "Start expire checker failed, result:" << ret << ".");
    LOG_INFO("Boostio server start success.");
    return BIO_OK;
}

BResult BioServer::StartStandalone()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    BIO_TP_START(NO_PROCESS_SERVER_START, 0);
    if (mStarted) {
        return BIO_OK;
    }
    BIO_TP_END;

    auto ret = InitializeRuntime();
    ChkTrue(ret == BIO_OK, ret, "Initialize runtime failed, result:" << ret << ".");

    mStandaloneMode = true;
    ret = ProcessService(BuildStandaloneModules());
    ChkTrue(ret == BIO_OK, ret, "Process standalone service failed, result:" << ret << ".");

    mStarted = true;

    LOG_INFO("Boostio standalone server start success.");
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

    if (mServerDiagnoseHandle != nullptr) {
        dlclose(mServerDiagnoseHandle);
        mServerDiagnoseHandle = nullptr;
    }
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
        BIO_LOG_STD_ERR("Create bio configuration instance failed.");
        return BIO_ERR;
    }

    BResult result = BIO_INNER_ERR;
    BIO_TP_START(CONFIG_INIT_FAIL, &result, -1);
    result = mConfig->Initialize();
    BIO_TP_END;
    if (result != BIO_OK) {
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
        BIO_LOG_STD_ERR("Failed to create logger instance.");
        return BIO_ERR;
    }

    BResult ret = BIO_INNER_ERR;
    BIO_TP_START(LOG_INIT_FAIL, &ret, BIO_ERR);
    ret = logger->Init();
    BIO_TP_END;
    if (ret != BIO_OK) {
        BIO_LOG_STD_ERR("Failed to init logger, result:" << ret << ", log path:" << loggerOptions.path << ".");
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
    const std::string dumpDir = FileUtil::JoinPath(mConfig->GetDaemonConfig().logPath, "trace");
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
    UfsHelperPtr underFsPtr = UfsHelper::Instance();
    if (underFsPtr == nullptr) {
        LOG_ERROR("Create underfs instance fail.");
        return BIO_ERR;
    }
    return underFsPtr->Initialize(mConfig->GetUnderFsConfig());
}

void BioServer::BioUnderFsExit()
{
    UfsHelper::Instance()->Stop();
}

BResult BioServer::BioStandaloneDeviceIdGatherInit()
{
    auto &daemonConfig = mConfig->GetDaemonConfig();
    if (!daemonConfig.hasDiskCache) {
        return mConfig->SelectStandaloneDiskByDeviceInfo();
    }
    if (daemonConfig.standaloneDeviceCount == 0) {
        LOG_ERROR("Standalone mode with disk cache requires ubsio.standalone.device_count in range [1, " <<
            DEVICE_SIZE << "]. Legacy device_count=0 disk selection is not supported.");
        return BIO_INVALID_PARAM;
    }

    uint32_t logicDeviceId = mConfig->GetStandaloneDeviceId();
    uint32_t virtualDeviceIndex = 0;
    uint64_t gatherTimeoutMs =
        static_cast<uint64_t>(daemonConfig.standaloneDeviceIdGatherTimeoutSec) * NO_1000;
    auto ret = StandaloneDeviceIdGather::Gather(logicDeviceId, daemonConfig.standaloneDeviceCount,
        virtualDeviceIndex, gatherTimeoutMs);
    ChkTrue(ret == BIO_OK, ret, "Gather standalone logic device IDs failed, logicDeviceId:" << logicDeviceId <<
        ", deviceCount:" << daemonConfig.standaloneDeviceCount << ", timeoutSec:" <<
        daemonConfig.standaloneDeviceIdGatherTimeoutSec << ", result:" << ret << ".");

    LOG_INFO("Overwrite standalone device ID with virtual index, logicDeviceId:" << logicDeviceId <<
        ", virtualDeviceIndex:" << virtualDeviceIndex << ".");
    mConfig->SetStandaloneDeviceInfo(virtualDeviceIndex);
    ret = mConfig->SelectStandaloneDiskByDeviceInfo();
    ChkTrue(ret == BIO_OK, ret, "Select standalone disk failed, ret:" << ret << ".");
    return BIO_OK;
}

BResult BioServer::BioBdmInit()
{
    auto &daemonConfig = mConfig->GetDaemonConfig();
    if (!daemonConfig.hasDiskCache) {
        DiskAllocator diskAllocator;
        diskAllocator.alloc = [](uint32_t, uint64_t, uint64_t, uint64_t, uint64_t *) { return BIO_ERR; };
        diskAllocator.free = [](uint32_t, uint64_t, uint64_t) { return; };
        FlowManager::RegisterDiskAllocator(diskAllocator);
        LOG_INFO("Disk cache is disabled, skip BDM initialization.");
        return BIO_OK;
    }

    auto ret = BdmSetIoEngine(daemonConfig.bdmIoEngine.c_str());
    ChkTrue(ret == BDM_CODE_OK, BIO_ERR,
        "Failed to set BDM IO engine, engine:" << daemonConfig.bdmIoEngine << ", result:" << ret << ".");
    ret = BdmSetUringSqpollMode(daemonConfig.bdmIoUringSqpollMode.c_str());
    ChkTrue(ret == BDM_CODE_OK, BIO_ERR,
        "Failed to set BDM io_uring SQPOLL mode, mode:" << daemonConfig.bdmIoUringSqpollMode << ", result:" << ret
                                                        << ".");
    ret = BdmSetSyncWorkerNum(daemonConfig.bdmSyncWorkerNum);
    ChkTrue(ret == BDM_CODE_OK, BIO_ERR,
        "Failed to set BDM sync worker number, workerNum:" << daemonConfig.bdmSyncWorkerNum << ", result:" << ret
                                                           << ".");
    ret = BdmInit();
    ChkTrue(ret == BDM_CODE_OK, BIO_ERR, "Failed to init BDM, result:" << ret << ".");
    bool useVirtualRegions = mStandaloneMode && daemonConfig.standaloneDeviceCount != 0;
    if (!useVirtualRegions) {
        BdmSetDiskStartupInfo(mStandaloneMode ? 1U : 0U, mStandaloneMode ? mConfig->GetStandaloneDeviceId() : 0U);
    }
    DiskDevices diskList = {};
    if (daemonConfig.diskList.size() > DISK_DEV_NUM) {
        LOG_ERROR("BDM disk num limit:" << DISK_DEV_NUM << ", input:" << daemonConfig.diskList.size() << ".");
        return BIO_ERR;
    }
    for (auto diskPathStr : daemonConfig.diskList) {
        ret = strcpy_s(diskList.list[diskList.num].path, DISK_PATH_LEN, diskPathStr.c_str());
        if (ret != 0) {
            return BIO_ERR;
        }
        auto diskCap = FileUtil::GetDiskCapacity(diskPathStr);
        diskList.diskCaps[diskList.num] = static_cast<uint64_t>(diskCap);
        diskList.num++;
    }

    bool forceNewDisk = mStandaloneMode && daemonConfig.standaloneForceNewDisk;
    BdmDiskSetForceNew(forceNewDisk ? 1U : 0U);
    if (useVirtualRegions) {
        // StandaloneDeviceIdGather has converted deviceId from a logic device ID to its sorted virtual index.
        ret = BdmStartVirtual(&diskList, daemonConfig.segment, mConfig->GetStandaloneDeviceId(),
            daemonConfig.standaloneDeviceCount);
    } else {
        ret = BdmStart(&diskList, daemonConfig.segment);
    }
    ChkTrue(ret == BDM_CODE_OK, BIO_ERR, "Failed to start BDM, result:" << ret << ".");

    if (useVirtualRegions) {
        for (uint32_t diskId = 0; diskId < diskList.num; ++diskId) {
            if (BdmGetDiskStatus(diskId) != BDM_DISK_STATE_NORMAL) {
                LOG_WARN("Skip unavailable virtual BDM capacity update, diskId:" << diskId << ".");
                continue;
            }
            uint64_t totalCapacity = 0;
            uint64_t usedCapacity = 0;
            ret = BdmGetCapacity(diskId, &totalCapacity, &usedCapacity);
            ChkTrue(ret == BDM_CODE_OK, BIO_ERR,
                "Failed to get virtual BDM capacity, diskId:" << diskId << ", result:" << ret << ".");
            ret = mConfig->UpdateStandaloneDiskCapacity(diskId, static_cast<int64_t>(totalCapacity));
            ChkTrue(ret == BIO_OK, ret,
                "Failed to update virtual disk capacity, diskId:" << diskId << ", capacity:" << totalCapacity << ".");
        }
    }

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

BResult BioServer::BioAttachDisk(std::string diskPath)
{
    uint32_t diskId = DISK_ID_INVALID;
    uint64_t diskCapacity = 0;
    return BioAttachDisk(std::move(diskPath), diskId, diskCapacity);
}

BResult BioServer::BioAttachDisk(std::string diskPath, uint32_t &diskId, uint64_t &diskCapacity)
{
    auto &daemonConfig = mConfig->GetDaemonConfig();
    if (!daemonConfig.hasDiskCache) {
        LOG_ERROR("Add disk is not supported when disk cache is disabled.");
        return BIO_INVALID_PARAM;
    }

    int64_t physicalCapacity = FileUtil::GetDiskCapacity(diskPath);
    if (physicalCapacity <= 0) {
        LOG_ERROR("Get added disk capacity failed, diskPath:" << diskPath << ".");
        return BIO_INVALID_PARAM;
    }

    uint64_t virtualRegionCapacity = 0;
    auto ret = BdmAttachDisk(const_cast<char *>(diskPath.c_str()), daemonConfig.segment,
        static_cast<uint64_t>(physicalCapacity), &diskId, &virtualRegionCapacity);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        LOG_ERROR("Bdm attach failed, diskPath: " << diskPath << ".");
        return BIO_ERR;
    }
    uint64_t usedCapacity = 0;
    ret = BdmGetCapacity(diskId, &diskCapacity, &usedCapacity);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        diskCapacity = virtualRegionCapacity;
        LOG_WARN("Get added BDM capacity failed, use virtual region capacity, diskId:" << diskId << ".");
    }
    return BIO_OK;
}

BResult BioServer::BioDiskReset(uint16_t diskId)
{
    if (!mConfig->GetDaemonConfig().hasDiskCache) {
        LOG_ERROR("Reset disk is not supported when disk cache is disabled, diskId:" << diskId << ".");
        return BIO_INVALID_PARAM;
    }

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
    int32_t ret = BdmExit();
    if (ret != BDM_CODE_OK) {
        LOG_WARN("Bdm exit failed, result:" << ret << ".");
    }
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

BResult BioServer::BioStandaloneMemInit()
{
    if (mStandaloneMemPool != nullptr) {
        return BIO_OK;
    }

    mStandaloneMemPool = MakeRef<StandaloneMemoryPool>();
    ChkTrue(mStandaloneMemPool != nullptr, BIO_ALLOC_FAIL, "Make standalone memory pool failed.");

    const auto &daemonConfig = mConfig->GetDaemonConfig();
    auto ret = mStandaloneMemPool->Start(daemonConfig.segment, daemonConfig.memCap);
    ChkTrue(ret == BIO_OK, ret, "Start standalone memory pool failed, result:" << ret << ".");

    MemAllocator memAllocator;
    memAllocator.alloc = [this](uint64_t size, uint64_t *addr) { return this->MemAlloc(size, addr); };
    memAllocator.free = [this](uint64_t addr) { this->MemFree(addr); };
    FlowManager::RegisterMemAllocator(memAllocator);
    return BIO_OK;
}

void BioServer::BioStandaloneMemExit()
{
    if (mStandaloneMemPool != nullptr) {
        mStandaloneMemPool->Stop();
        mStandaloneMemPool = nullptr;
    }
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
    netOptions.netSegmentSize = netConfig.netSegmentSize;
    netOptions.ipMask = netConfig.dataIpMask;
    netOptions.port = netConfig.dataPort;
    netOptions.isBusyLoop = netConfig.isRpcBusyLoop;
    netOptions.role = Role::NET_SERVER;
    netOptions.protocol = static_cast<ServiceProtocol>(netConfig.protocol);
    netOptions.isCreateMemPool = true;
    netOptions.memorySize = mConfig->GetDaemonConfig().memCap;
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
    if (!daemonConfig.hasDiskCache) {
        diskInfo.diskId = 0;
        diskInfo.diskStatus = CM_DISK_NORMAL;
        nodeInfo.disks.push_back(diskInfo);
    } else {
        for (uint32_t index = 0; index < daemonConfig.diskList.size(); index++) {
            diskInfo.diskId = index;
            if (BdmGetDiskStatus(index) == BDM_DISK_STATE_NORMAL) {
                diskInfo.diskStatus = CM_DISK_NORMAL;
            } else {
                diskInfo.diskStatus = CM_DISK_FAULT;
            }
            nodeInfo.disks.push_back(diskInfo);
        }
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

BResult BioServer::BioStandaloneViewInit()
{
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    CmNodeId localNid;
    BResult ret = StandaloneView::Build(mConfig, localNid, nodeView, ptView);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Build standalone view failed, ret:" << ret << ".");
        return ret;
    }

    {
        std::lock_guard<std::mutex> lock(mNodeViewMutex);
        // Client direct calls query this same NodeView through GetNodeView, so
        // localNid and view timestamps are updated under the normal locks.
        mLocalNid = localNid;
        mNodeView = std::move(nodeView);
        mCurNodeTimes = Monotonic::TimeUs();
    }
    {
        std::lock_guard<std::mutex> lock(mPtViewMutex);
        mPtView = std::move(ptView);
        mCurPtTimes = Monotonic::TimeUs();
    }
    LOG_INFO("Standalone view init success, localNid:" << mLocalNid.VNodeId() << ", ptNum:" << mPtView.size() <<
        ".");

    const auto &daemonConfig = mConfig->GetDaemonConfig();
    if (!daemonConfig.hasDiskCache) {
        return BIO_OK;
    }
    ret = mStandaloneView.Start(static_cast<uint32_t>(daemonConfig.diskList.size()),
        std::bind(&BioServer::HandleStandaloneDiskFault, this, std::placeholders::_1));
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Start standalone disk fault handler failed, ret:" << ret << ".");
        return ret;
    }
    return BIO_OK;
}

void BioServer::BioStandaloneViewExit()
{
    mStandaloneView.Stop();
}

BResult BioServer::HandleStandaloneDiskFault(uint16_t diskId)
{
    std::lock_guard<std::mutex> updateLock(mStandaloneViewUpdateMutex);
    // The fault worker collects pending disk ids before it reaches this lock, so
    // the fault can already be obsolete: a rolled back add disk untracks the id,
    // and a retry can re-add the same id as a healthy disk. Only a still pending
    // fault may be failed over.
    if (!mStandaloneView.IsDiskFaultPending(diskId)) {
        LOG_INFO("Skip obsolete standalone disk fault, diskId:" << diskId << ".");
        return BIO_OK;
    }

    std::unique_lock<std::mutex> nodeLock(mNodeViewMutex, std::defer_lock);
    std::unique_lock<std::mutex> ptLock(mPtViewMutex, std::defer_lock);
    std::lock(nodeLock, ptLock);

    StandaloneView::NodeView nextNodeView = mNodeView;
    StandaloneView::PtView nextPtView = mPtView;
    std::vector<std::pair<uint16_t, uint64_t>> faultPtCleanups;
    BResult ret = mStandaloneView.FailoverDisk(diskId, mConfig->GetDaemonConfig().diskCaps, mLocalNid,
        nextNodeView, nextPtView, faultPtCleanups);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    uint64_t viewTime = Monotonic::TimeUs();
    uint64_t lastViewTime = std::max(mCurNodeTimes, mCurPtTimes);
    if (viewTime <= lastViewTime) {
        viewTime = lastViewTime + 1;
    }
    mNodeView.swap(nextNodeView);
    mPtView.swap(nextPtView);
    mCurNodeTimes = viewTime;
    mCurPtTimes = viewTime;

    nodeLock.unlock();
    ptLock.unlock();
    LOG_INFO("Publish standalone disk fault view, diskId:" << diskId << ", viewTime:" << viewTime <<
        ", faultPtCount:" << faultPtCleanups.size() << ".");

    if (!mCacheInited) {
        // StandaloneView starts before Cache, so an early startup disk fault has
        // no WCache flows or index entries to clean up.
        return BIO_OK;
    }

    // This fault worker is the single owner of disk-level flow cleanup. Other
    // request paths, such as DestroyFlow, must only acknowledge flows that
    // have already been taken over here and must not start a cleanup again.
    BResult cleanupRet = WCacheManager::Instance()->CleanupFaultedDiskFlows(diskId);
    if (UNLIKELY(cleanupRet != BIO_OK)) {
        LOG_ERROR("Standalone faulted disk flow cleanup failed, diskId:" << diskId << ", ret:" << cleanupRet << ".");
    }
    return cleanupRet;
}

BResult BioServer::AddStandaloneDisk(std::string &diskPath)
{
    std::lock_guard<std::mutex> updateLock(mStandaloneViewUpdateMutex);
    const auto &daemonConfig = mConfig->GetDaemonConfig();

    // Keep the startup-time physical capacity snapshot in sync so a later
    // rejoin can detect a replaced disk with a different size.
    int64_t physicalCapacity = FileUtil::GetDiskCapacity(diskPath);
    if (UNLIKELY(physicalCapacity <= 0)) {
        LOG_ERROR("Get added disk capacity failed, diskPath: " << diskPath << ".");
        return BIO_INVALID_PARAM;
    }

    BResult ret = mConfig->LockDiskConfig();
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    bool configChanged = true;
    ret = mConfig->CreateDiskConfBak(diskPath);
    if (ret == BIO_EXISTS) {
        configChanged = false;
        ret = BIO_OK;
    }
    if (UNLIKELY(ret != BIO_OK)) {
        mConfig->UnlockDiskConfig();
        return ret;
    }

    uint32_t diskId = DISK_ID_INVALID;
    uint64_t diskCapacity = 0;
    bool diskTracked = false;
    // Replacing the config file is the commit point of add disk: every step
    // before it has to be undone as one unit, and no step after it is allowed to
    // fail. BDM has no detach primitive, so a rolled back attach stays attached
    // until the process restarts. A still healthy leftover is reused by the next
    // attach of the same path, but a leftover that BDM has already faulted is
    // invisible to that lookup, so adding that disk again needs a restart.
    auto rollbackPrepared = [this, &configChanged, &diskId, &diskTracked]() {
        if (diskTracked) {
            mStandaloneView.UntrackDisk(static_cast<uint16_t>(diskId));
            diskTracked = false;
        }
        if (configChanged) {
            mConfig->DiscardDiskConfBak();
        }
        mConfig->UnlockDiskConfig();
    };

    ret = BioAttachDisk(diskPath, diskId, diskCapacity);
    if (UNLIKELY(ret != BIO_OK || diskCapacity > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))) {
        rollbackPrepared();
        return ret == BIO_OK ? BIO_ERR : ret;
    }

    // Track the new disk before any view work so a fault reported by BDM
    // immediately after attach cannot be dropped for being out of range.
    ret = mStandaloneView.TrackDisk(static_cast<uint16_t>(diskId));
    if (UNLIKELY(ret != BIO_OK)) {
        rollbackPrepared();
        return ret;
    }
    diskTracked = true;
    if (BdmGetDiskStatus(diskId) != BDM_DISK_STATE_NORMAL) {
        LOG_ERROR("New standalone disk is not normal, diskId:" << diskId << ", diskPath:" << diskPath << ".");
        rollbackPrepared();
        return BIO_ERR;
    }

    StandaloneView::NodeView currentNodeView;
    StandaloneView::PtView currentPtView;
    {
        std::unique_lock<std::mutex> nodeLock(mNodeViewMutex, std::defer_lock);
        std::unique_lock<std::mutex> ptLock(mPtViewMutex, std::defer_lock);
        std::lock(nodeLock, ptLock);
        currentNodeView = mNodeView;
        currentPtView = mPtView;
    }
    StandaloneView::NodeView nextNodeView = currentNodeView;
    StandaloneView::PtView nextPtView = currentPtView;
    std::vector<CmPtInfo> changedPts;
    ret = mStandaloneView.AddDisk(static_cast<uint16_t>(diskId), static_cast<int64_t>(diskCapacity),
        daemonConfig.diskCaps, mLocalNid, nextNodeView, nextPtView, changedPts);
    if (UNLIKELY(ret != BIO_OK)) {
        rollbackPrepared();
        return ret;
    }

    // BDM reports faults asynchronously, and the fault worker serializes behind
    // mStandaloneViewUpdateMutex, so a fault raised while the next view was
    // being rebuilt only becomes visible here. Publishing such a disk as
    // CM_DISK_NORMAL would advertise capacity that BDM already stopped serving.
    if (UNLIKELY(BdmGetDiskStatus(diskId) != BDM_DISK_STATE_NORMAL ||
        mStandaloneView.IsDiskFault(static_cast<uint16_t>(diskId)))) {
        LOG_ERROR("New standalone disk faulted before view publish, diskId:" << diskId << ", diskPath:" <<
            diskPath << ".");
        rollbackPrepared();
        return BIO_ERR;
    }

    if (configChanged) {
        ret = mConfig->CommitDiskConfBak();
        if (UNLIKELY(ret != BIO_OK)) {
            // The commit point was not reached, so the disk must not stay
            // tracked. Leaving it in mDiskStates keeps one more entry than the
            // config disk list, and the next retry then fails forever on the
            // contiguous id check in TrackDisk.
            rollbackPrepared();
            return ret;
        }
    }
    mConfig->UnlockDiskConfig();
    // The config file is committed, so no step below may fail or roll back. The
    // in-memory disk capacity must be appended before the views are published,
    // because a later fault of this disk indexes diskCaps by the published disk
    // id when it rebuilds the pt distribution.
    mConfig->AppendDaemonDisk(diskPath, static_cast<int64_t>(diskCapacity), physicalCapacity);

    auto markReadOnly = [&changedPts]() {
        uint32_t markedWCacheCount = 0;
        for (const auto &pt : changedPts) {
            markedWCacheCount += WCacheManager::Instance()->MarkPtFlowsReadOnly(pt.ptId, pt.version,
                pt.masterDiskId);
        }
        return markedWCacheCount;
    };
    uint32_t markedWCacheCount = markReadOnly();
    uint64_t viewTime = Monotonic::TimeUs();
    {
        std::unique_lock<std::mutex> nodeLock(mNodeViewMutex, std::defer_lock);
        std::unique_lock<std::mutex> ptLock(mPtViewMutex, std::defer_lock);
        std::lock(nodeLock, ptLock);
        uint64_t lastViewTime = std::max(mCurNodeTimes, mCurPtTimes);
        if (viewTime <= lastViewTime) {
            viewTime = lastViewTime + 1;
        }
        mNodeView.swap(nextNodeView);
        mPtView.swap(nextPtView);
        mCurNodeTimes = viewTime;
        mCurPtTimes = viewTime;
    }
    markedWCacheCount += markReadOnly();
    LOG_INFO("Publish standalone add disk view, diskId:" << diskId << ", capacity:" << diskCapacity <<
        ", markedWCacheCount:" << markedWCacheCount << ", viewTime:" << viewTime << ".");
    return BIO_OK;
}

BResult BioServer::AddStandaloneOldDisk(const std::string &diskPath, uint16_t diskId)
{
    std::lock_guard<std::mutex> updateLock(mStandaloneViewUpdateMutex);
    const auto &daemonConfig = mConfig->GetDaemonConfig();
    if (diskId >= daemonConfig.diskList.size() || diskId >= daemonConfig.diskPhysicalCaps.size()) {
        LOG_ERROR("Invalid standalone rejoin disk, diskId:" << diskId << ", diskNum:" << daemonConfig.diskList.size() <<
            ", physicalCapNum:" << daemonConfig.diskPhysicalCaps.size() << ".");
        return BIO_INVALID_PARAM;
    }

    // A disk whose fault worker is still pending must finish failover first;
    // otherwise a stale fault task could re-fault the recovered disk.
    BResult ret = mStandaloneView.CheckDiskRecoverable(diskId);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    StandaloneView::NodeView currentNodeView;
    StandaloneView::PtView currentPtView;
    {
        std::unique_lock<std::mutex> nodeLock(mNodeViewMutex, std::defer_lock);
        std::unique_lock<std::mutex> ptLock(mPtViewMutex, std::defer_lock);
        std::lock(nodeLock, ptLock);
        currentNodeView = mNodeView;
        currentPtView = mPtView;
    }

    bool viewDiskNormal = false;
    auto nodeIter = currentNodeView.find(mLocalNid);
    if (nodeIter != currentNodeView.end()) {
        for (const auto &disk : nodeIter->second.disks) {
            if (disk.diskId == diskId) {
                viewDiskNormal = disk.diskStatus == CM_DISK_NORMAL;
                break;
            }
        }
    }
    if (viewDiskNormal) {
        // Keep the original no-op behavior for an already-normal disk. If BDM
        // disagrees with the view the state is inconsistent, so do not report
        // success.
        return BdmGetDiskStatus(diskId) == BDM_DISK_STATE_NORMAL ? BIO_OK : BIO_ERR;
    }

    // Reject a replaced disk whose physical capacity differs from the startup
    // snapshot. BDM's cached region is sized by the startup capacity, so a
    // different size cannot be adopted online.
    std::string capacityProbePath = diskPath;
    int64_t physicalCapacity = FileUtil::GetDiskCapacity(capacityProbePath);
    if (UNLIKELY(physicalCapacity <= 0)) {
        LOG_ERROR("Get rejoin disk capacity failed, diskPath:" << diskPath << ".");
        return BIO_INVALID_PARAM;
    }
    if (physicalCapacity != daemonConfig.diskPhysicalCaps[diskId]) {
        LOG_ERROR("Standalone rejoin disk capacity mismatch, diskId:" << diskId << ", diskPath:" << diskPath <<
            ", expectedCapacity:" << daemonConfig.diskPhysicalCaps[diskId] << ", actualCapacity:" <<
            physicalCapacity << ". Replace it with a disk of the same capacity or restart to adopt a new layout.");
        return BIO_ERR;
    }

    StandaloneView::NodeView nextNodeView = currentNodeView;
    StandaloneView::PtView nextPtView = currentPtView;
    std::vector<CmPtInfo> changedPts;
    ret = mStandaloneView.RejoinDisk(diskId, daemonConfig.diskCaps, mLocalNid, nextNodeView, nextPtView, changedPts);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    if (BdmGetDiskStatus(diskId) != BDM_DISK_STATE_NORMAL) {
        ret = BioDiskReset(diskId);
        if (UNLIKELY(ret != BIO_OK)) {
            // BioDiskReset marks the slot used before rebuilding the
            // allocator; restore the fault state so a failed reset does not
            // look like a healthy empty disk on retry.
            BdmSetDiskUsedStatus(diskId, false);
            return ret;
        }
    }

    ret = mStandaloneView.MarkDiskRecovered(diskId);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    auto markReadOnly = [&changedPts]() {
        uint32_t markedWCacheCount = 0;
        for (const auto &pt : changedPts) {
            markedWCacheCount += WCacheManager::Instance()->MarkPtFlowsReadOnly(pt.ptId, pt.version,
                pt.masterDiskId);
        }
        return markedWCacheCount;
    };
    uint32_t markedWCacheCount = markReadOnly();
    uint64_t viewTime = Monotonic::TimeUs();
    {
        std::unique_lock<std::mutex> nodeLock(mNodeViewMutex, std::defer_lock);
        std::unique_lock<std::mutex> ptLock(mPtViewMutex, std::defer_lock);
        std::lock(nodeLock, ptLock);
        uint64_t lastViewTime = std::max(mCurNodeTimes, mCurPtTimes);
        if (viewTime <= lastViewTime) {
            viewTime = lastViewTime + 1;
        }
        mNodeView.swap(nextNodeView);
        mPtView.swap(nextPtView);
        mCurNodeTimes = viewTime;
        mCurPtTimes = viewTime;
    }
    markedWCacheCount += markReadOnly();
    LOG_INFO("Publish standalone rejoin disk view, diskId:" << diskId << ", capacity:" <<
        daemonConfig.diskCaps[diskId] << ", markedWCacheCount:" << markedWCacheCount << ", viewTime:" << viewTime <<
        ".");
    return BIO_OK;
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

    if (!mStandaloneMode) {
        mMirrorCrb = MirrorServerCrb::Instance();
        ChkTrue(mMirrorCrb != nullptr, BIO_ERR, "Mirror server crb instance is nullptr.");
        ret = mMirrorCrb->Init();
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Failed to init mirror server crb, ret:" << ret << ".");
            return BIO_ERR;
        }
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
    {
        std::lock_guard<std::mutex> lock(gMetaEventCallbackLock);
        gMetaEventCallbackApplicable = true;
        ApplyMetaEventCallbackLocked();
    }

    GetLocDiskId getLocDiskId = [this](uint16_t ptId, uint16_t &diskId) -> BResult {
        // CM owns this mapping in cluster mode. Standalone reads it from the
        // synthetic PtView generated at startup.
        if (mStandaloneMode) {
            return GetLocalDiskIdFromView(ptId, diskId);
        }
        return Cm::Instance()->GetLocalDiskId(ptId, diskId);
    };
    Cache::Instance().RegGetLocDiskId(getLocDiskId);

    GetLocDiskStatus getLocDiskStatus = [this](uint16_t ptId, uint16_t diskId, bool &isNormal) -> void {
        if (mStandaloneMode) {
            GetLocalDiskStatusFromView(ptId, diskId, isNormal);
            return;
        }
        Cm::Instance()->GetLocalDiskStatus(ptId, diskId, isNormal);
    };
    Cache::Instance().RegGetLocDiskStatus(getLocDiskStatus);

    CheckServiceState checkService = [this]() -> bool {
        return GetServiceState();
    };
    Cache::Instance().RegCheckServiceState(checkService);

    CheckDegrade checkDegrade = [this](uint16_t ptId, bool &isDegrade) -> BResult {
        if (mStandaloneMode) {
            return CheckPtDegradeFromView(ptId, isDegrade);
        }
        return Cm::Instance()->CheckPtDegrade(ptId, isDegrade);
    };
    Cache::Instance().RegCheckDegrade(checkDegrade);

    GetGlobEvictOffset evictOffset = [this](uint16_t ptId, uint64_t flowId, uint64_t &flowOffset) -> BResult {
        if (mStandaloneMode) {
            return Cache::Instance().GetEvictOffset(flowId, flowOffset);
        }
        return MirrorServer::Instance()->GetFlowGlobEvictOffset(ptId, flowId, flowOffset);
    };
    Cache::Instance().RegGetGlobEvictOffset(evictOffset);

    CheckLocRole checkLocRole = [this](uint16_t ptId, bool &isMaster) -> BResult {
        if (mStandaloneMode) {
            return CheckLocalRoleFromView(ptId, isMaster);
        }
        return Cm::Instance()->CheckLocalRole(ptId, isMaster);
    };
    Cache::Instance().RegCheckLocRole(checkLocRole);

    if (!mStandaloneMode) {
        // Channel-broken callbacks only make sense when NetEngine exists. In
        // standalone the client and server share one process lifetime.
        auto mirror = mMirror;
        auto channelBroken = [this, mirror](uint32_t nodeId, uint32_t pid) -> void {
            if (pid != 0) {
                Cache::Instance().HandleProcBroken(pid);
            } else {
                ReConnect(nodeId);
            }
            nodeId = (nodeId == 1024) ? mLocalNid.VNodeId() : nodeId;
            QuotaHolder holder = { nodeId, static_cast<uint64_t>(pid) };
            CacheOverloadCtrl::Instance().RecycleQuota(holder);
            if (mirror.Get() != nullptr) {
                mirror->RecycleDataMsgMem(pid);
            }
        };
        ret = mNetEngine->RegisterChannelBrokenHandler(channelBroken);
        if (ret != BIO_OK) {
            LOG_ERROR("Net engine register channel broken handler failed, ret " << ret);
            return ret;
        }
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
    {
        std::lock_guard<std::mutex> lock(gMetaEventCallbackLock);
        gMetaEventCallbackApplicable = false;
    }
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

    auto ptr = LoadFunction("cli_agent_init", handler);
    if (ptr == nullptr) {
        LOG_ERROR("Failed to load function cli_agent_init.");
        dlclose(handler);
        return BIO_ERR;
    }

    auto cliAgentInitFunc = reinterpret_cast<CLIAgentInitFunc>(ptr);
    uint32_t procPid = getpid();
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
    void *handler = nullptr;
    const char *soFileName = "libserver_diagnose.so";
    handler = dlopen(soFileName, RTLD_NOW);
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
    } else {
        mServerDiagnoseHandle = handler;
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

BResult BioServer::GetLocalDiskIdFromView(uint16_t ptId, uint16_t &diskId)
{
    std::lock_guard<std::mutex> lock(mPtViewMutex);
    auto iter = mPtView.find(ptId);
    if (iter == mPtView.end()) {
        return BIO_ERR;
    }

    for (const auto &copy : iter->second.copys) {
        if (copy.nodeId == mLocalNid.VNodeId()) {
            diskId = copy.diskId;
            return BIO_OK;
        }
    }
    return BIO_ERR;
}

void BioServer::GetLocalDiskStatusFromView(uint16_t ptId, uint16_t diskId, bool &isNormal)
{
    isNormal = false;
    {
        std::lock_guard<std::mutex> lock(mNodeViewMutex);
        auto nodeIter = mNodeView.find(mLocalNid);
        if (nodeIter == mNodeView.end()) {
            return;
        }
        bool diskNormal = false;
        for (const auto &disk : nodeIter->second.disks) {
            if (disk.diskId == diskId) {
                diskNormal = disk.diskStatus == CM_DISK_NORMAL;
                break;
            }
        }
        if (!diskNormal) {
            return;
        }
    }

    std::lock_guard<std::mutex> lock(mPtViewMutex);
    auto ptIter = mPtView.find(ptId);
    if (ptIter == mPtView.end()) {
        return;
    }
    for (const auto &copy : ptIter->second.copys) {
        if (copy.nodeId == mLocalNid.VNodeId() && copy.diskId == diskId && copy.state == CM_COPY_RUNNING) {
            isNormal = true;
            return;
        }
    }
}

BResult BioServer::CheckPtDegradeFromView(uint16_t ptId, bool &isDegrade)
{
    isDegrade = false;
    std::lock_guard<std::mutex> lock(mPtViewMutex);
    auto iter = mPtView.find(ptId);
    if (iter == mPtView.end()) {
        return BIO_ERR;
    }
    isDegrade = iter->second.state != CM_PT_NORMAL;
    return BIO_OK;
}

BResult BioServer::CheckLocalRoleFromView(uint16_t ptId, bool &isMaster)
{
    isMaster = false;
    std::lock_guard<std::mutex> lock(mPtViewMutex);
    auto iter = mPtView.find(ptId);
    if (iter == mPtView.end()) {
        return BIO_ERR;
    }
    isMaster = iter->second.masterNodeId == mLocalNid.VNodeId();
    return BIO_OK;
}

BResult BioServer::HandleCmNodeEvent(const std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeInfos)
{
    uint64_t nodeSize = nodeInfos.size();
    BIO_TP_START(LARGE_NODE_LIST, &nodeSize, 65535UL);
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

int32_t BioServerStandaloneInit()
{
    auto bioServer = BioServer::Instance();
    if (UNLIKELY(bioServer == nullptr)) {
        LOG_ERROR("Make bio server instance failed.");
        return BIO_ALLOC_FAIL;
    }
    return bioServer->StartStandalone();
}

void SetStandaloneDeviceInfo(uint32_t deviceId)
{
    auto config = BioConfig::Instance();
    if (UNLIKELY(config == nullptr)) {
        LOG_ERROR("Make bio config instance failed.");
        return;
    }
    config->SetStandaloneDeviceInfo(deviceId);
}

void BioServerExit(void)
{
    BioServer::Instance()->Exit();
}

uintptr_t GetBioServerNet()
{
    NetEnginePtr netEngine = BioServer::Instance()->GetNetEngine();
    if (netEngine == nullptr) {
        return 0;
    }
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

extern "C" int32_t UbsioRegisterMetaEventCallback(UbsioMetaEventCallbackC callback, void *context)
{
    std::lock_guard<std::mutex> lock(gMetaEventCallbackLock);
    gMetaEventCallback = callback;
    gMetaEventCallbackContext = context;
    gMetaEventCallbackConfigured = true;
    ApplyMetaEventCallbackLocked();
    return BIO_OK;
}

extern "C" int32_t UbsioScanKey(const UbsioKvKeyInfo **items, uint64_t *count)
{
    if (items == nullptr || count == nullptr) {
        return BIO_INVALID_PARAM;
    }
    *items = nullptr;
    *count = 0;

    std::unordered_map<std::string, uint64_t> diskItems;
    auto ret = Cache::Instance().ScanDiskKeys(diskItems);
    if (ret != BIO_OK || diskItems.empty()) {
        return ret;
    }

    for (const auto &item : diskItems) {
        if (item.first.size() >= UBSIO_KV_MAX_KEY_SIZE) {
            LOG_ERROR("Invalid key length when scanning disk cache, key length:" << item.first.size() << ".");
            return BIO_INNER_ERR;
        }
    }

    auto *snapshot = static_cast<UbsioKvKeyInfo *>(std::calloc(diskItems.size(), sizeof(UbsioKvKeyInfo)));
    if (snapshot == nullptr) {
        return BIO_ALLOC_FAIL;
    }

    uint64_t index = 0;
    for (const auto &item : diskItems) {
        CopyKey(snapshot[index].key, item.first.c_str(), UBSIO_KV_MAX_KEY_SIZE);
        snapshot[index].keyLen = static_cast<uint32_t>(item.first.size());
        snapshot[index].valueLen = item.second;
        ++index;
    }

    *items = snapshot;
    *count = index;
    return BIO_OK;
}

uint32_t GetNegoWorkIoTimeOut()
{
    return BioServer::Instance()->GetNegoWorkIoTimeOut();
}

uint32_t GetPrometheusScrapeIntervalSec()
{
    return BioServer::Instance()->GetPrometheusScrapeIntervalSec();
}

int32_t GetRuntimeConfig(StandaloneRuntimeConfigResponse *rsp)
{
    if (rsp == nullptr) {
        return BIO_INVALID_PARAM;
    }
    auto config = BioServer::Instance()->GetConfig()->GetDaemonConfig();
    auto netConfig = BioServer::Instance()->GetConfig()->GetNetConfig();
    rsp->serverPid = getpid();
    rsp->netSegmentSize = netConfig.netSegmentSize;
    rsp->scene = config.workScene;
    rsp->alignSize = config.workIoAlignSize;
    rsp->ioTimeOut = config.workIoTimeOut;
    rsp->netTimeOut = config.workNetTimeOut;
    rsp->logLevel = config.logLevel;
    rsp->enableHtrace = config.enableTrace;
    rsp->enableCrc = config.enableCrc;
    rsp->enableCli = config.enableCli;
    rsp->enablePrometheus = config.enablePrometheus;
    rsp->scrapeIntervalSec = config.scrapeIntervalSec;
    rsp->sdkPoolSize = config.sdkPoolSize;
    rsp->dataMsgBlockSize = config.segment;
    auto ret = strcpy_s(rsp->listenAddress, sizeof(rsp->listenAddress), config.listenAddress.c_str());
    return ret == 0 ? BIO_OK : BIO_ERR;
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
        req->comm.ptv, *rsp);
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
    return BioServer::Instance()->GetMirrorServer()->AddDisk(*req);
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
    int32_t ret = BioServer::Instance()->GetMirrorServer()->GetConvergence(*req, *rsp);
    return ret;
}

int32_t BatchGet(BatchGetRequest *req, BatchGetResponse *rsp)
{
    *rsp = {};
    int32_t ret = BioServer::Instance()->GetMirrorServer()->BatchGetConvergence(*req, *rsp);
    return ret;
}

int32_t BatchExist(BatchExistRequest *req, BatchExistResponse *rsp)
{
    return BioServer::Instance()->GetMirrorServer()->BatchExistConvergence(*req, *rsp);
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

int32_t CalcCacheResourceLocal(CacheResourceRequest *req, CacheResourceResponse *rsp)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->CalcCacheResourceLocal(req, rsp));
}

int32_t GetTracePointsLocal(GetTracePointsResponse *rsp)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->GetTracePointsLocal(rsp));
}
