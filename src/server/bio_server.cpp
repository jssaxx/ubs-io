/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <unistd.h>
#include "htracer.h"
#include "bio_log.h"
#include "bdm_core.h"
#include "bio_config_instance.h"
#include "flow_manager.h"
#include "bio_server_c.h"
#include "bio_server.h"

namespace ock {
namespace bio {
static void Log(int level, const char *msg)
{
    if (Logger::gInstance != nullptr) {
        Logger::gInstance->Log(level + 1U, msg);
    }
}

BioServer::BioServer() noexcept
{
    std::vector<ModuleDesc> modules = {
        { "Tracer", std::bind(&BioServer::BioTraceInit, this), nullptr, nullptr, nullptr },
        { "UnderFs", std::bind(&BioServer::BioUnderFsInit, this), nullptr, nullptr, nullptr },
        { "Bdm", std::bind(&BioServer::BioBdmInit, this), nullptr, nullptr, std::bind(&BioServer::BioBdmExit, this) },
        { "Net", std::bind(&BioServer::BioNetInit, this), nullptr, nullptr, std::bind(&BioServer::BioNetExit, this) },
        { "Flow", std::bind(&BioServer::BioFlowInit, this), nullptr, nullptr, nullptr },
        { "Cache", std::bind(&BioServer::BioCacheInit, this), nullptr, nullptr, nullptr },
        { "MirrorServer", std::bind(&BioServer::BioMirrorServerInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioMirrorServerExit, this) },
        { "CM", std::bind(&BioServer::BioCmInit, this), nullptr, nullptr, std::bind(&BioServer::BioCmExit, this) },
    };
    mService = MakeRef<BioServiceProc>(modules);
}

BResult BioServer::Start()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }

    // 1. Initialize infrastructure
    if (BioLoggerInit() != BIO_OK || BioConfigInit() != BIO_OK) {
        return BIO_ERR;
    }
    auto &daemonConfig = mConfig->GetDaemonConfig();
    BIO_LOG_RESET_LEVEL(daemonConfig.logLevel);

    // 2. Initialize boostio service
    ChkTrue(mService != nullptr, BIO_ERR, "Boostio service not created.");
    auto ret = mService->Process();
    if (ret != BIO_OK) {
        return BIO_ERR;
    }

    // 3. wait start finish
    while (!mStarted || mPtView.empty()) {
        sleep(1);
    }

    LOG_INFO("Boostio Server Started.");
    return BIO_OK;
}

void BioServer::Stop()
{
    return;
}

BResult BioServer::BioConfigInit()
{
    if (mConfig != nullptr) {
        return BIO_OK;
    }
    mConfig = BioConfig::Instance();
    if (mConfig == nullptr) {
        LOG_ERROR("Create bio configuration instance failed.");
        return BIO_ERR;
    }
    auto result = mConfig->Initialize(".");
    if (result != BIO_OK) {
        LOG_ERROR("Failed to initialize configuration, result: " << result << ".");
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult BioServer::BioLoggerInit()
{
    LoggerOptions loggerOptions;
    loggerOptions.minLogLevel = SPDLOG_LEVEL_INFO;
    loggerOptions.path = "./bio.log";
    auto logger = Logger::Instance(loggerOptions);
    if (logger == nullptr) {
        std::cout << "Failed to create logger instance." << std::endl;
        return BIO_ERR;
    }
    auto ret = logger->Init();
    if (ret != BIO_OK) {
        std::cout << "Failed to init logger, result:" << ret << ", log path:" << loggerOptions.path << "." << std::endl;
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult BioServer::BioTraceInit()
{
    const std::string dumpDir = "/var/log/boostio/trace/";
    auto ret = ock::htracer::HTracerInit(dumpDir);
    ChkTrue(ret == BIO_OK, BIO_ERR, "Failed to init tracer, result:" << ret << ", dumpDir:" << dumpDir << ".");
    return ret;
}

BResult BioServer::BioUnderFsInit()
{
    return UnderFs::Instance()->Init();
}

BResult BioServer::BioBdmInit()
{
    auto ret = BdmInit();
    ChkTrue(ret == BDM_CODE_OK, BIO_ERR, "Failed to init BDM, result:" << ret << ".");

    auto &daemonConfig = mConfig->GetDaemonConfig();
    DiskDevices diskList;
    diskList.num = 0;
    for (uint32_t i = 0; i < daemonConfig.diskList.size(); i++) {
        strcpy(diskList.list[diskList.num].path, daemonConfig.diskList[i].c_str());
        diskList.num++;
    }

    ret = BdmStart(&diskList, daemonConfig.diskCap, daemonConfig.segment);
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
    mNetEngine = MakeRef<NetEngine>();
    ChkTrue(mNetEngine != nullptr, BIO_ALLOC_FAIL, "Make net engine failed.");

    // 1. Initialize net engine
    int16_t timeoutSec = NO_5 * NO_60; // 5min
    auto &netConfig = mConfig->GetNetConfig();
    auto ret =
        mNetEngine->Initialize(timeoutSec, netConfig.handleRequestThreadNum, netConfig.handleRequestQueueSize, Log);
    ChkTrue(ret == BIO_OK, ret, "Net engine initialize failed, result:" << ret << ".");

    // 2. start rpc service
    NetOptions netOptions;
    netOptions.ipMask = netConfig.dataIpMask;
    netOptions.port = netConfig.dataPort;
    netOptions.isBusyLoop = netConfig.isBusyLoop;
    netOptions.role = NET_SERVER;
    netOptions.protocol = static_cast<ServiceProtocol>(netConfig.protocol);
    netOptions.localMrSize = mConfig->GetDaemonConfig().memCap;
    netOptions.handlerCount = netConfig.dataWorkersCnt;
    netOptions.connCount = netConfig.dataWorkersCnt;
    ret = StartRpcService(netOptions);
    ChkTrue(ret == BIO_OK, ret, "Start rpc service failed, result:" << ret << ".");

    // 3. start ipc service
    netOptions.isBusyLoop = false;
    netOptions.role = NET_SERVER;
    netOptions.protocol = ServiceProtocol::SHM;
    ret = StartIpcService(netOptions);
    ChkTrue(ret == BIO_OK, ret, "Start ipc service failed, result:" << ret << ".");
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
    cmOptions.role = ROLE_TOGETHER;
    cmOptions.zkIpMask = mConfig->GetCmConfig().zkHost;
    cmOptions.groups.groupId = static_cast<uint16_t>(mConfig->GetCmConfig().groupId);
    cmOptions.groups.replicaNum = 2;
    cmOptions.groups.initialNodeNum = static_cast<uint16_t>(mConfig->GetCmConfig().initialNodeNum);
    cmOptions.groups.maxNodeNum = static_cast<uint16_t>(mConfig->GetCmConfig().nodeNum);
    cmOptions.groups.maxPtNum = static_cast<uint16_t>(mConfig->GetCmConfig().ptNum);
    cmOptions.hbTempTimeout = static_cast<uint32_t>(mConfig->GetCmConfig().registeredTimeoutSec);
    cmOptions.hbPermFaultTime = static_cast<uint32_t>(mConfig->GetCmConfig().registeredPermTimeoutSec);
    mCm = Cm::Instance();
    ChkTrue(mCm != nullptr, BIO_ERR, "cm instance is nullptr.");

    auto result = mCm->Initialize(cmOptions);
    ChkTrue(result == BIO_OK, BIO_ERR, "Cm Initialize failed, result:" << result << ".");

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
    return;
}

BResult BioServer::BioMirrorServerInit()
{
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

    return BIO_OK;
}

void BioServer::BioMirrorServerExit()
{
    return;
}

BResult BioServer::BioCacheInit()
{
    BResult ret = Cache::Instance().Init();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init cache instance, ret:" << ret << ".");
        return ret;
    }

    GetLocDiskId getLocDiskId = [](uint16_t ptId, uint16_t &diskId) -> BResult {
        return Cm::Instance()->GetLocalDiskId(ptId, diskId);
    };
    Cache::Instance().RegGetLocDiskId(getLocDiskId);

    CheckDegrade checkDegrade = [](uint16_t ptId, bool &isDegrade) -> BResult {
        return Cm::Instance()->CheckPtDegrade(ptId, isDegrade);
    };
    Cache::Instance().RegCheckDegrade(checkDegrade);

    GetGlobEvictOffset evictOffset = [](uint16_t ptId, uint64_t flowId, bool &isMaster,
        uint64_t &flowOffset) -> BResult {
        return MirrorServer::Instance()->GetFlowGlobEvictOffset(ptId, flowId, isMaster, flowOffset);
    };
    Cache::Instance().RegGetGlobEvictOffset(evictOffset);

    CacheMalloc memMalloc = [this](uint64_t size, uint64_t *addr) { return this->MemAlloc(size, addr); };
    Cache::Instance().RegCacheMalloc(memMalloc);

    CacheFree memFree = [this](uint64_t addr) { this->MemFree(addr); };
    Cache::Instance().RegCacheFree(memFree);

    ret = Cache::Instance().Recover();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to recover cache instance, ret:" << ret << ".");
        return ret;
    }

    return BIO_OK;
}

BResult BioServer::BioFlowInit()
{
    auto flowManager = FlowManager::Instance();
    ChkTrue(flowManager != nullptr, BIO_ERR, "Flow manager instance is nullptr.");
    return flowManager->Init();
}

void BioServer::Connection()
{
    uint32_t failCnt = 0;
    for (auto it = mNodeView.begin(); it != mNodeView.end(); ++it) {
        if (it->second.id.VNodeId() == mLocalNid.VNodeId()) {
            continue;
        }
        LOG_INFO("Connect to node:" << it->second.id.VNodeId() << ", ip:" << it->second.ip << ", port:" <<
            it->second.port << ".");
        ConnectInfo info(it->second.id.VNodeId(), it->second.ip, it->second.port, 1);
        BResult ret = mNetEngine->SyncConnect(info);
        if (ret != BIO_OK) {
            LOG_ERROR("Connect to " << it->first.ToString() << " failed, ret: " << ret << ".");
            failCnt++;
        }
    }
    LOG_INFO("Connection finish, cluster node num:" << mNodeView.size() << ", failed num: " << failCnt << ".");
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
        LOG_INFO("Recv ptId:" << it->second.ptId << ", " << it->second.ToString());
    }

    auto ret = mMirrorCrb->NotifyPtChangeEvent(ptInfos);
    if (ret != BIO_OK) {
        LOG_ERROR("Handle ptevent fail, ret:" << ret);
        return ret;
    }
    return BIO_OK;
}
}
}

using namespace ock::bio;

int32_t BioServerInit()
{
    return static_cast<int32_t>(BioServer::Instance()->Start());
}

void BioServerUninit()
{
    BioServer::Instance()->Stop();
}

uintptr_t GetBioServerNet()
{
    NetEnginePtr netEngine = BioServer::Instance()->GetNetEngine();
    return reinterpret_cast<uintptr_t>(netEngine.Get());
}

int32_t GetLocalNid(GetLocalNidResponse *rsp)
{
    CmNodeId localNid = BioServer::Instance()->GetLocalNid();
    rsp->groupId = localNid.GroupId();
    rsp->nodeId = localNid.VNodeId();
    rsp->protocol = BioServer::Instance()->GetNetProtocol();
    return 0;
}

int32_t GetNodeView(QueryNodeViewResponse *rsp)
{
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioServer::Instance()->GetNodeView();
    uint32_t size = nodeView.size();
    if (UNLIKELY(size > CLUSTER_NODE_MAX_SIZE)) {
        LOG_ERROR("Cluster node num  " << size << " exceeds 256.");
        return -1;
    }
    uint32_t index = 0;
    for (auto &nodeEntry : nodeView) {
        rsp->desc[index].groupId = nodeEntry.second.id.GroupId();
        rsp->desc[index].nodeId = nodeEntry.second.id.VNodeId();
        memcpy_s(rsp->desc[index].ip, IP_MAX_SIZE, nodeEntry.second.ip.c_str(), nodeEntry.second.ip.size());
        rsp->desc[index].port = nodeEntry.second.port;
        rsp->desc[index].status = static_cast<uint16_t>(nodeEntry.second.status);
        rsp->desc[index].num = nodeEntry.second.disks.size();
        for (uint32_t j = 0; j < nodeEntry.second.disks.size(); j++) {
            rsp->desc[index].diskDesc[j].diskId = nodeEntry.second.disks[j].diskId;
            rsp->desc[index].diskDesc[j].diskStatus = static_cast<uint16_t>(nodeEntry.second.disks[j].diskStatus);
        }
        index++;
    }
    rsp->num = index;
    return 0;
}

int32_t GetPtView(QueryPtViewResponse *rsp)
{
    std::map<uint16_t, CmPtInfo> ptView = BioServer::Instance()->GetPtView();
    uint32_t size = ptView.size();
    if (UNLIKELY(size > PT_MAX_SIZE)) {
        LOG_ERROR("Pt view num  " << size << " exceeds 8192.");
        return -1;
    }
    uint32_t index = 0;
    for (auto &ptEntry : ptView) {
        rsp->desc[index].version = ptEntry.second.version;
        rsp->desc[index].ptId = ptEntry.second.ptId;
        rsp->desc[index].state = static_cast<uint16_t>(ptEntry.second.state);
        rsp->desc[index].masterNodeId = ptEntry.second.masterNodeId;
        rsp->desc[index].masterDiskId = ptEntry.second.masterDiskId;
        for (uint32_t j = 0; j < ptEntry.second.copys.size(); j++) {
            rsp->desc[index].copys[j].nodeId = ptEntry.second.copys[j].nodeId;
            rsp->desc[index].copys[j].diskId = ptEntry.second.copys[j].diskId;
            rsp->desc[index].copys[j].state = static_cast<uint16_t>(ptEntry.second.copys[j].state);
        }
        index++;
    }
    rsp->num = index;
    return 0;
}

int32_t CreateFlowMaster(CreateFlowRequest *req, CreateFlowResponse *rsp)
{
    uint64_t flowId;
    BResult ret = BioServer::Instance()->GetMirrorServer()->CreateFlowMaster(req->comm.pid, req->comm.ptId,
        req->comm.ptv, flowId);
    rsp->flowId = flowId;
    return static_cast<int32_t>(ret);
}

int32_t CreateFlowSlave(CreateFlowRequest *req)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->CreateFlowSlave(req->comm.pid, req->comm.ptId,
        req->comm.ptv, req->flowId));
}

int32_t GetSlice(GetSliceRequest *req, GetSliceResponse **rsp)
{
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

    uint32_t sliceLen = sliceP->GetSerializeLen();
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
    uint32_t outSliceLen = 0;
    sliceP->Serialize((*rsp)->sliceBuf, outSliceLen);
    if (UNLIKELY(outSliceLen != sliceLen)) {
        LOG_ERROR("Serialize slice failed, outSliceLen:" << outSliceLen << ", sliceLen:" << sliceLen << ".");
        return static_cast<int32_t>(BIO_INNER_ERR);
    }

    return BIO_OK;
}

int32_t Put(PutRequest *req)
{
    LOG_DEBUG("Put request, key:" << req->key << ", length:" << req->length << ", flowId:" << req->flowId <<
        ", offset:" << req->offset << ", index:" << req->index << ", sliceLen:" << req->sliceLen);
    WCacheSlicePtr sliceP = MakeRef<WCacheSlice>();
    sliceP->Deserialize(req->sliceBuf, req->sliceLen);
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->Put(*req, sliceP));
}

int32_t Get(GetRequest *req, GetResponse *rsp)
{
    uint64_t realLen = 0;
    BResult ret = BioServer::Instance()->GetMirrorServer()->Get(*req, realLen);
    rsp->realLen = realLen;
    return static_cast<int32_t>(ret);
}

int32_t Delete(DeleteRequest *req)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->Delete(*req));
}

int32_t Stat(StatRequest *req, StatResponse *rsp)
{
    Bio::ObjStat objInfo{};
    BResult ret = BioServer::Instance()->GetMirrorServer()->Stat(*req, objInfo);
    rsp->size = objInfo.size;
    rsp->time = objInfo.time;
    return static_cast<int32_t>(ret);
}

int32_t Load(LoadRequest *req)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->Load(*req));
}