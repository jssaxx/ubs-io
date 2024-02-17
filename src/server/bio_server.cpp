/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <unistd.h>
#include "htracer.h"
#include "bio_log.h"
#include "bdm_core.h"
#include "bio_config_instance.h"
#include "flow_manager.h"
#include "bio_server.h"

namespace ock {
namespace bio {
BResult BioServer::Start()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }

    // initialize logger
    LoggerOptions loggerOptions;
    loggerOptions.minLogLevel = SPDLOG_LEVEL_INFO;
    loggerOptions.path = "./bio.log";
    auto logger = Logger::Instance(loggerOptions);
    if (UNLIKELY(logger == nullptr)) {
        LOG_ERROR("Failed to create logger instance.");
        return BIO_ERR;
    }
    auto ret = logger->Init();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init logger, result:" << ret << ", log path:" << loggerOptions.path << ".");
        return BIO_ERR;
    }

    // load configuration
    ret = LoadConfig();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to load bio configuration, result:" << ret << ".");
        return BIO_ERR;
    }

    // init tracer
    const std::string dumpDir = "/var/log/boostio/trace/";
    ret = ock::htracer::HTracerInit(dumpDir);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to load bio tracer, result:" << ret << ".");
        logger->Exit();
        return BIO_ERR;
    }

    // init underfs
    ret = UnderFs::Instance()->Init();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init underfs, result:" << ret << ".");
        logger->Exit();
        return BIO_ERR;
    }

    // start bdm
    ret = StartDisk();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to start bdm service, result:" << ret << ".");
        logger->Exit();
        return BIO_ERR;
    }

    // start rpc server
    ret = StartNetService();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to start rpc server, ret:" << ret << ".");
        logger->Exit();
        StopDisk();
        return BIO_ERR;
    }

    // start cm
    ret = StartCm();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to start cm, result:" << ret << ".");
        logger->Exit();
        StopDisk();
        StopNetService();
        return BIO_ERR;
    }

    // start mirror server
    ret = StartMirrorServer();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to start mirror server, ret:" << ret << ".");
        logger->Exit();
        StopDisk();
        StopNetService();
        StopCm();
        return BIO_ERR;
    }

    // wait connection and pt view initialize finish
    while (!mStarted || mPtView.empty()) {
        sleep(1);
    }
    LOG_INFO("Boostio Server Started!");
    return BIO_OK;
}

void BioServer::Stop()
{
    StopDisk();
    StopNetService();
    StopCm();
    StopMirrorServer();
    mStarted = false;
    LOG_INFO("Boostio Server Stopped!");
}

BResult BioServer::LoadConfig()
{
    if (mConfig.Get() != nullptr) {
        return BIO_OK;
    }

    mConfig = BioConfig::Instance();
    if (UNLIKELY(mConfig == nullptr)) {
        LOG_ERROR("Create bio configuration instance failed.");
        return BIO_ERR;
    }
    auto result = mConfig->Initialize(".");
    if (UNLIKELY(result != 0)) {
        LOG_ERROR("Failed to initialize configuration, result: " << result << ".");
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult BioServer::StartDisk()
{
    int ret = BdmInit();
    if (ret != BDM_CODE_OK) {
        return BIO_ERR;
    }

    auto &daemonConfig = mConfig->GetDaemonConfig();
    DiskDevices diskList;
    diskList.num = 0;
    for (uint32_t i = 0; i < daemonConfig.diskList.size(); i++) {
        strcpy(diskList.list[diskList.num].path, daemonConfig.diskList[i].c_str());
        diskList.num++;
    }

    ret = BdmStart(&diskList, daemonConfig.diskCap, daemonConfig.segment);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return BIO_ERR;
    }

    DiskAllocator diskAllocator;
    diskAllocator.alloc = [](uint32_t bdmId, uint64_t flowId, uint64_t flowOffset, uint64_t len, uint64_t *chunkId) {
        int ret = BdmAlloc(bdmId, flowId, flowOffset, len, chunkId);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
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

void BioServer::StopDisk()
{
    LOG_INFO("Stopped disk manager.");
}

BResult BioServer::StartNetService()
{
    mNetEngine = MakeRef<NetEngine>();
    ChkTrue(mNetEngine != nullptr, BIO_ALLOC_FAIL, "Make net engine failed.");

    // bio server create rpc and ipc service
    auto &netConfig = mConfig->GetNetConfig();
    NetOptions netOptions;
    netOptions.ipMask = netConfig.dataIpMask;
    netOptions.port = netConfig.dataPort;
    netOptions.isBusyLoop = netConfig.isBusyLoop;
    netOptions.rpcRole = NET_SERVER;
    netOptions.rpcProtocol = static_cast<ServiceProtocol>(netConfig.protocol);
    netOptions.localMrSize = mConfig->GetDaemonConfig().memCap;
    netOptions.name = "BIO-" + std::to_string(netOptions.port);
    netOptions.dataPanelConnCount = netConfig.dataWorkersCnt;
    netOptions.dataPanelHandlerCount = netConfig.dataWorkersCnt;
    netOptions.handleRequestThreadNum = netConfig.handleRequestThreadNum;
    netOptions.handleRequestQueueSize = netConfig.handleRequestQueueSize;

    mNetEngine->SetDataPageKb(mConfig->GetDaemonConfig().segment / NO_1024);
    auto result = mNetEngine->Start(netOptions);
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Start rpc engine failed, ret:" << result << ".");
        return BIO_ERR;
    }

    MemAllocator memAllocator;
    memAllocator.alloc = [this](uint64_t size, uint64_t *addr) { return this->MemAlloc(size, addr); };
    memAllocator.free = [this](uint64_t addr) { this->MemFree(addr); };
    FlowManager::RegisterMemAllocator(memAllocator);

    LOG_INFO("Net Server Started.");
    return BIO_OK;
}

void BioServer::StopNetService()
{
    if (mNetEngine != nullptr) {
        mNetEngine->Stop();
        mNetEngine = nullptr;
    }
    LOG_INFO("Rpc service stopped.");
}

BResult BioServer::StartCm()
{
    // prepare config.
    CmOptions cmOptions;
    cmOptions.role = ROLE_TOGETHER;
    std::stringstream ss;
    ss << mConfig->GetCmConfig().zkHostPort;
    cmOptions.zkIpMask = mConfig->GetCmConfig().zkHostIp + ":" + ss.str();
    cmOptions.groups.groupId = static_cast<uint16_t>(mConfig->GetCmConfig().groupId);
    cmOptions.groups.replicaNum = 2;
    cmOptions.groups.initialNodeNum = static_cast<uint16_t>(mConfig->GetCmConfig().initialNodeNum);
    cmOptions.groups.maxNodeNum = static_cast<uint16_t>(mConfig->GetCmConfig().nodeNum);
    cmOptions.groups.maxPtNum = static_cast<uint16_t>(mConfig->GetCmConfig().ptNum);

    mCm = Cm::Instance();
    if (UNLIKELY(mCm == nullptr)) {
        LOG_ERROR("cm instance is nullptr.");
        return BIO_ERR;
    }

    // initial.
    auto result = mCm->Initialize(cmOptions);
    ChkTrueNot(result == BIO_OK, result);

    // register node.
    auto &daemonConfig = mConfig->GetDaemonConfig();
    CmNodeInfo nodeInfo;
    nodeInfo.ip = mConfig->GetNetConfig().dataIp;
    nodeInfo.port = mConfig->GetNetConfig().dataPort;
    CmDiskInfo diskInfo;
    for (uint32_t index = 0; index < daemonConfig.diskList.size(); index++) {
        diskInfo.diskId = index;
        diskInfo.diskStatus = CM_DISK_NORMAL;
        nodeInfo.disks.push_back(diskInfo);
    }
    result = mCm->RegisterNode(nodeInfo);
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Failed to register node, result: " << result);
        return result;
    }

    // register listener to cmm.
    mCm->RegisterNodeHandler(std::bind(&BioServer::HandleCmNodeEvent, this, std::placeholders::_1));
    mCm->RegisterPtHandler(std::bind(&BioServer::HandleCmPtEvent, this, std::placeholders::_1));

    result = mCm->Start();
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Failed to start cm client, result:" << result);
        return result;
    }
    LOG_INFO("Started Cluster manager client successfully");
    return BIO_OK;
}

void BioServer::StopCm()
{
    LOG_INFO("Stopped cluster manager.");
}

BResult BioServer::StartMirrorServer()
{
    mMirror = MirrorServer::Instance();
    if (UNLIKELY(mMirror == nullptr)) {
        LOG_ERROR("Mirror server instance is nullptr.");
        return BIO_ERR;
    }

    BResult ret = mMirror->Initialize(mConfig->GetCmConfig().deployType);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init mirror server, ret:" << ret << ".");
        return BIO_ERR;
    }

    auto flowManager = FlowManager::Instance();
    ret = flowManager->Init();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init flow manager, ret:" << ret << ".");
        return BIO_ERR;
    }

    return BIO_OK;
}

void BioServer::StopMirrorServer()
{
    LOG_INFO("Stopped mirror server.");
}

BResult BioServer::HandleCmNodeEvent(const std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeInfos)
{
    for (auto it = nodeInfos.begin(); it != nodeInfos.end(); ++it) {
        LOG_INFO("Node:" << it->first.ToString() << ", " << it->second.ToString());
    }

    // The current cluster startup must be all node started successfully
    if (static_cast<uint32_t>(mConfig->GetCmConfig().nodeNum) == nodeInfos.size()) {
        mNodeView = nodeInfos;
        mLocalNid = mCm->GetCmLocalNodeId();
        mNetEngine->SetLocalNodeId(mLocalNid.VNodeId());
        Connection();
        mStarted = true;
    }
    return BIO_OK;
}

BResult BioServer::HandleCmPtEvent(const std::map<uint16_t, CmPtInfo> &ptInfos)
{
    mPtView = ptInfos;
    for (auto it = mPtView.begin(); it != mPtView.end(); ++it) {
        LOG_INFO("Pt:" << it->second.ptId << ", " << it->second.ToString());
    }
    return BIO_OK;
}

void BioServer::Connection()
{
    uint32_t failCnt = 0;
    for (auto it = mNodeView.begin(); it != mNodeView.end(); ++it) {
        if (it->second.id.VNodeId() == mLocalNid.VNodeId()) {
            continue;
        }
        LOG_INFO("Connect to node:" << it->second.id.VNodeId() << ", ip:" << it->second.ip << ", port:" << it->second.port << ".");
        ConnectInfo info(it->second.id.VNodeId(), it->second.ip, it->second.port, 1);
        BResult ret = mNetEngine->SyncConnect(info);
        if (ret != BIO_OK) {
            LOG_ERROR("Connect to " << it->first.ToString() << " failed, ret: " << ret << ".");
            failCnt++;
        }
    }
    LOG_INFO("Connection finish, cluster node num:" << mNodeView.size() << ", failed num: " << failCnt << ".");
}
}
}