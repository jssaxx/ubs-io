/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <unistd.h>
#include <utility>
#include "htracer.h"
#include "bio_log.h"
#include "bdm_core.h"
#include "bio_functions.h"
#include "bio_config_instance.h"
#include "bio_monotonic.h"
#include "flow_manager.h"
#include "bio_server_c.h"
#include "interceptor_server.h"
#include "bio_server.h"
#ifdef USE_DEBUG_TOOLS
#include <dlfcn.h>
#endif

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
#ifdef USE_DEBUG_TOOLS
        { "Diagnose", std::bind(&BioServer::BioServerDiagnoseInit, this), nullptr, nullptr, nullptr },
#endif
        { "Tracer", std::bind(&BioServer::BioTraceInit, this), nullptr, nullptr,
        std::bind(&BioServer::BioTraceExit, this) },
        { "UnderFs", std::bind(&BioServer::BioUnderFsInit, this), nullptr, nullptr, nullptr },
        { "Bdm", std::bind(&BioServer::BioBdmInit, this), nullptr, nullptr, std::bind(&BioServer::BioBdmExit, this) },
        { "Net", std::bind(&BioServer::BioNetInit, this), nullptr, nullptr, std::bind(&BioServer::BioNetExit, this) },
        { "Flow", std::bind(&BioServer::BioFlowInit, this), nullptr, nullptr, nullptr },
        { "Cache", std::bind(&BioServer::BioCacheInit, this), nullptr, nullptr, nullptr },
        { "InterceptorServer", std::bind(&BioServer::BioIntercepterServerInit, this), nullptr, nullptr, nullptr},
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
    std::string logPath = "./bio.log";
    if (BioLoggerInit(logPath) != BIO_OK || BioConfigInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }
    auto &daemonConfig = mConfig->GetDaemonConfig();
    BIO_LOG_RESET_LEVEL(daemonConfig.logLevel);

    // 2. Initialize boostio service
    ChkTrue(mService != nullptr, BIO_ERR, "Boostio service not created.");
    auto ret = mService->Process();
    if (ret != BIO_OK) {
        return ret;
    }

    // 3. wait start finish
    while (!mStarted || mPtView.empty()) {
        sleep(5U);
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

BResult BioServer::BioLoggerInit(std::string pathName)
{
    LoggerOptions loggerOptions;
    loggerOptions.minLogLevel = SPDLOG_LEVEL_INFO;
    loggerOptions.path = std::move(pathName);
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

void BioServer::BioTraceExit()
{
    ock::htracer::HTracerExit();
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
    for (auto diskPathStr : daemonConfig.diskList) {
        strcpy(diskList.list[diskList.num].path, diskPathStr.c_str());
        diskList.diskCaps[diskList.num] = FileUtil::GetDiskCapacity(diskPathStr);
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

    int16_t timeoutSec = mConfig->GetCmConfig().registeredTimeoutSec; // 同zk心跳超时
    auto &netConfig = mConfig->GetNetConfig();
    auto ret =
        mNetEngine->Initialize(timeoutSec, netConfig.handleRequestThreadNum, netConfig.handleRequestQueueSize, Log);
    ChkTrue(ret == BIO_OK, ret, "Net engine initialize failed, result:" << ret << ".");

    NetOptions netOptions;
    netOptions.ipMask = netConfig.dataIpMask;
    netOptions.port = netConfig.dataPort;
    netOptions.isBusyLoop = netConfig.isRpcBusyLoop;
    netOptions.role = NET_SERVER;
    netOptions.protocol = static_cast<ServiceProtocol>(netConfig.protocol);
    netOptions.memorySize = mConfig->GetDaemonConfig().memCap;
    netOptions.regShmMem = true;
    netOptions.handlerCount = netConfig.rpcDataWorkersCnt;
    netOptions.connCount = netConfig.rpcDataWorkersCnt;
    ret = StartRpcService(netOptions);
    ChkTrue(ret == BIO_OK, ret, "Start rpc service failed, result:" << ret << ".");

    netOptions.isBusyLoop = netConfig.isIpcBusyLoop;
    netOptions.role = NET_SERVER;
    netOptions.protocol = ServiceProtocol::SHM;
    netOptions.handlerCount = netConfig.ipcDataWorkersCnt;
    netOptions.connCount = netConfig.ipcDataWorkersCnt;
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

    GetLocDiskStatus getLocDiskStatus = [](uint16_t ptId, uint16_t diskId, bool &isNormal) -> void {
        Cm::Instance()->GetLocalDiskStatus(ptId, diskId, isNormal);
    };
    Cache::Instance().RegGetLocDiskStatus(getLocDiskStatus);

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
    };
    mNetEngine->RegisterChannelBrokenHandler(channelBroken);

    ret = Cache::Instance().Recover();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to recover cache instance, ret:" << ret << ".");
        return ret;
    }

    return BIO_OK;
}

BResult BioServer::BioIntercepterServerInit()
{
    auto ret = InterceptorServer::GetInstance().StartServer();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init interceptor server, ret:" << ret << ".");
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult BioServer::BioFlowInit()
{
    auto flowManager = FlowManager::Instance();
    ChkTrue(flowManager != nullptr, BIO_ERR, "Flow manager instance is nullptr.");
    return flowManager->Init();
}

#ifdef USE_DEBUG_TOOLS
BResult BioServer::BioServerDiagnoseInit()
{
    uint32_t procPid = 456U;
    std::string diagName = "bio_server";
    auto ret = CLI_AgentInit(procPid, const_cast<char *>(diagName.c_str()));
    if (ret != BIO_OK) {
        LOG_ERROR("init bio server diagnose fail.");
        return BIO_ERR;
    }
    ret = this->BioServerDiagnoseInitInner();
    if (ret != BIO_OK) {
        LOG_ERROR("inner init bio server diagnose fail.");
        return BIO_ERR;
    }
    return BIO_OK;
}

using ServerDiagnose = int (*)();
BResult BioServer::BioServerDiagnoseInitInner()
{
    const char *soFileName = "libserver_diagnose.so";
    void *handler = dlopen(soFileName, RTLD_NOW);
    if (handler == nullptr) {
        LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
        return BIO_ERR;
    }
    ServerDiagnose serverInitFunc = reinterpret_cast<ServerDiagnose>(dlsym(handler, "ServerDiagnoseInit"));
    BResult ret = serverInitFunc();
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to Initialize server diagnose, ret:" << ret << ".");
    }
    return ret;
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
    for (auto it = nodeInfos.begin(); it != nodeInfos.end(); ++it) {
        LOG_INFO("Node:" << it->first.ToString() << ", " << it->second.ToString());
    }

    if (!mStarted) {
        mLocalNid = mCm->GetCmLocalNodeId();
        mNetEngine->SetLocalNodeId(mLocalNid.VNodeId());
        mStarted = true;
    }

    mNodeView = nodeInfos;
    Connection();

    mCurNodeTimes = Monotonic::TimeUs();
    LOG_INFO("Cur node times:" << mCurNodeTimes);
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
    mCurPtTimes = Monotonic::TimeUs();
    LOG_INFO("Cur pt times:" << mCurPtTimes);
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
    WCacheSlicePtr sliceP = nullptr;
    if (req->sliceLen == 0) {
        MrInfo mrInfo = { req->mrAddress, static_cast<uint32_t>(req->mrSize) };
        std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
        sliceP = MakeRef<WCacheSlice>(req->flowId, req->flowOffset, req->flowIndex, req->length, addrVec);
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            return BIO_ALLOC_FAIL;
        }
    } else {
        sliceP = MakeRef<WCacheSlice>();
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            return BIO_ALLOC_FAIL;
        }
        sliceP->Deserialize(req->sliceBuf, req->sliceLen);
    }
    ServiceContext netCtx;
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->Put(*req, sliceP, netCtx));
}

int32_t Get(GetRequest *req, GetResponse *rsp)
{
    ServiceContext netCtx;
    return BioServer::Instance()->GetMirrorServer()->Get(*req, *rsp, netCtx);
}

int32_t Delete(DeleteRequest *req)
{
    return static_cast<int32_t>(BioServer::Instance()->GetMirrorServer()->Delete(*req));
}

int32_t List(ListRequest *req, ListResponse **rsp)
{
    std::unordered_map<std::string, ObjStat> objs;
    BResult ret = BioServer::Instance()->GetMirrorServer()->List(*req, objs);
    if (ret != BIO_OK) {
        return ret;
    }

    char *tmp = new (std::nothrow) char[sizeof(ListResponse) + sizeof(ObjStat) * objs.size()];
    if (UNLIKELY(tmp == nullptr)) {
        LOG_ERROR("Alloc memory failed, len:" << (sizeof(ListResponse) + sizeof(ObjStat) * objs.size()) << ".");
        return BIO_ALLOC_FAIL;
    }
    *rsp = static_cast<ListResponse *>(static_cast<void *>(tmp));
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

int32_t ReportHb(uint64_t *curNodeTimes, uint64_t *curPtTimes)
{
    return static_cast<int32_t>(BioServer::Instance()->GetHbInfo(curNodeTimes, curPtTimes));
}