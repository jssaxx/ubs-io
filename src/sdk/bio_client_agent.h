/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BIO_CLIENT_AGENT_H
#define BIO_CLIENT_AGENT_H

#include "cm.h"
#include "net_engine.h"
#include "message.h"
#include "cache_slice.h"
#include "bio_err.h"
#include "bio_tracepoint_helper.h"
#include "bio_ref.h"
#include "bio.h"

namespace ock {
namespace bio {
namespace agent {
class BioClientAgent;
using BioClientAgentPtr = Ref<BioClientAgent>;
class BioClientAgent {
public:
    using BioServerStartFuncPtr = int32_t (*)();
    using BioServerExitFuncPtr = void (*)();
    using GetBioServerCrcFlagFuncPtr = bool (*)();
    using GetBioServerCliFlagFuncPtr = bool (*)();
    using GetBioServerPromethuesToggleFuncPtr = bool (*)();
    using GetBioServerListenAddressFuncPtr = const char *(*)();
    using GetBioServertimeOutFuncPtr = uint32_t (*)();
    using GetBioServerScrapeIntervalSecFuncPtr = uint32_t (*)();
    using GetBioServerNetEngineFuncPtr = uintptr_t (*)();
    using GetLocalNidFuncPtr = int32_t (*)(GetLocalNidResponse *);
    using GetQuotaInfoFuncPtr = int32_t (*)(QueryQuotaRequest *, QueryQuotaResponse *);
    using AllocQuotaFuncPtr = int32_t (*)(AllocQuotaRequest *, AllocQuotaResponse *);
    using FreeQuotaFuncPtr = int32_t (*)(FreeQuotaRequest *);
    using GetNodeViewFuncPtr = int32_t (*)(QueryNodeViewRequest *, QueryNodeViewResponse *);
    using GetPtViewFuncPtr = int32_t (*)(QueryPtViewRequest *, QueryPtViewResponse *);
    using CreateFlowMasterFuncPtr = int32_t (*)(CreateFlowRequest *, CreateFlowResponse *);
    using CreateFlowSlaveFuncPtr = int32_t (*)(CreateFlowRequest *);
    using DestroyFlowFuncPtr = int32_t (*)(DestroyFlowRequest *);
    using NotifyUpdateFuncPtr = int32_t (*)(NotifyUpdateRequest *);
    using CheckUpdateReadyFuncPtr = int32_t (*)(CheckUpdateReadyRequest *, CheckUpdateReadyResponse *);
    using GetSliceFuncPtr = int32_t (*)(GetSliceRequest *, GetSliceResponse **);
    using PutFuncPtr = int32_t (*)(PutRequest *, PutResponse *);
    using GetFuncPtr = int32_t (*)(GetRequest *, GetResponse *);
    using DeleteFuncPtr = int32_t (*)(DeleteRequest *);
    using AddDiskFuncPtr = int32_t (*)(AddDiskRequest *, AddDiskResponse *);
    using StatFuncPtr = int32_t (*)(StatRequest *, StatResponse *);
    using ListFuncPtr = int32_t (*)(ListRequest *, ListResponse **);
    using LoadFuncPtr = int32_t (*)(LoadRequest *);
    using GetCacheHitLocalFuncPtr = int32_t (*)(CacheHitResponse *);
    using CalcCacheResourceLocalFuncPtr = int32_t (*)(CacheResourceResponse *);
    using GetTracePointsLocalFuncPtr = int32_t (*)(GetTracePointsResponse *);

    BioClientAgent() : mLocalNid(CmNodeId(0, UINT16_MAX)), localPid(static_cast<uint32_t>(getpid())) {}
    ~BioClientAgent() = default;

    static BioClientAgentPtr &Instance()
    {
        static auto instance = MakeRef<BioClientAgent>();
        return instance;
    }

    BResult Initialize(WorkerMode mode);
    void Exit();

    NetEnginePtr GetNetService()
    {
        uintptr_t netPtr = getNetEngineOp();
        NetEnginePtr netEngine = MakeRef<NetEngine>();
        netEngine = reinterpret_cast<NetEngine *>(netPtr);
        return netEngine;
    }

    bool GetConfigCrcFlag();

    bool GetConfigCliFlag();

    bool GetConfigPrometheusToggle();

    const char *GetPrometheusListenAddress();

    uint32_t GetNegoWorkIoTimeOut();

    uint32_t GetPrometheusScrapeIntervalSec();

    BResult GetLocalNodeInfo(uint16_t &protocol, CmNodeId &localNid);

    BResult GetLocalQuotaInfo(uint32_t scene, bool &enable, uint64_t &preloadSize);

    BResult AllocQuota(AllocQuotaRequest &req, uint64_t &expectPreloadSize);

    BResult FreeQuota(FreeQuotaRequest &req);

    BResult GetClusterNodeView(uint64_t &curNodeTimes, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView);

    BResult GetPtView(uint64_t &curPtTimes, std::map<uint16_t, CmPtInfo> &ptView);

    BResult CreateFlowLocal(pid_t procId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
        uint64_t &flowId, bool &isDegrade);

    BResult DestroyFlowLocal(pid_t procId, CmPtInfo &ptEntry, uint16_t ptId, uint64_t flowId);

    BResult PrepareResource(CmPtInfo &ptEntry, uint64_t flowId, uint64_t offset, uint64_t index, uint64_t length,
        GetSliceResponse **rsp);

    void PutLocal(PutRequest *req, Callback &callback);

    BResult GetLocal(GetRequest &req, char *value, uint64_t &realLen);

    void DeleteLocal(DeleteRequest &req, Callback &callback);

    BResult AddDisk(AddDiskRequest &req, AddDiskResponse &rsp);

    BResult ListLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);

    BResult StatLocal(StatRequest &req, ObjStat &objInfo);

    BResult NotifyUpdate(bool &flag);

    BResult SendNotifyUpdateRequestLocal(NotifyUpdateRequest &req);

    BResult CheckUpdateReady();

    BResult SendCheckUpdateReadyRequestLocal(CheckUpdateReadyRequest &req, CheckUpdateReadyResponse &rsp);

    BResult LoadLocal(LoadRequest &req);

    BResult GetCacheHitLocal(CacheHitRequest &req, std::unordered_map<uint16_t, CacheHitDesc> &nodeDesc);

    BResult CalcCacheResourceLocal(CacheResourceRequest &req, std::vector<CacheResourcesDesc> &nodeDesc);

    BResult GetTracePointsLocal(GetTracePointsRequest &req, GetTracePointsResponse &rsp);

    BResult GetTracePointsLocal(GetTracePointsRequest &req,
                                std::map<uint16_t, TraceDatabase> &nodesTracePoints);

    BResult SendGetNodeInfoRequest(uint16_t masterPtId, uint16_t slavePtId, FileLocationQueryRsp &rsp);

    BResult SendPrepareResourceLocal(CmPtInfo &ptEntry, uint64_t flowId, uint64_t offset, uint64_t index,
                                     uint64_t length, GetSliceResponse **rsp);

    bool CheckGetSliceRsp(GetSliceResponse **rsp);

private:
    BResult InitUpgradeOperation();
    BResult InitOperation();
    void *LoadFunction(const char *name);

    BResult SendGetLocalNodeInfoRequest(uint16_t &protocol, CmNodeId &localNid);

    BResult SendCreateFlowRequestLocal(CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
        uint64_t &flowId, bool &isDegrade);

    BResult SendDestroyFlowRequestLocal(CmPtInfo &ptEntry, uint16_t ptId, uint64_t flowId);

    void SendPutRequestLocal(PutRequest *req, Callback &callback);

    BResult SendGetRequestLocal(GetRequest &req, char *value, uint64_t &realLen);

    void SendDeleteRequestLocal(DeleteRequest &req, Callback &callback);

    BResult SendAddDiskRequest(AddDiskRequest &req, AddDiskResponse &rsp);

    BResult CallServerListIntf(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    BResult SendListRequestLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);

    BResult SendStatRequestLocal(StatRequest &req, ObjStat &objInfo);

    BResult SendLoadRequestLocal(LoadRequest &req);

    BResult SendCacheHitRequestLocal(CacheHitRequest &req, CacheHitResponse &rsp);

    BResult SendCacheResourceRequestLocal(CacheResourceRequest &req, CacheResourceResponse &rsp);

    BResult SendGetLocalTracePointsRequest(GetTracePointsRequest &req, GetTracePointsResponse &rsp);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    WorkerMode mMode = CONVERGENCE;
    CmNodeId mLocalNid;
    uint32_t localPid;
    DEFINE_REF_COUNT_VARIABLE;

    void *handler = nullptr;
    BioServerStartFuncPtr startOp = nullptr;
    BioServerExitFuncPtr exitOp = nullptr;
    GetBioServerCrcFlagFuncPtr getCrcFlag = nullptr;
    GetBioServerCliFlagFuncPtr getCliFlag = nullptr;
    GetBioServerPromethuesToggleFuncPtr getPrometheusToggle = nullptr;
    GetBioServerListenAddressFuncPtr getListenAddress = nullptr;
    GetBioServertimeOutFuncPtr getTimeOut = nullptr;
    GetBioServerScrapeIntervalSecFuncPtr getScrapeIntervalSec = nullptr;
    GetBioServerNetEngineFuncPtr getNetEngineOp = nullptr;
    GetLocalNidFuncPtr getLocalNidOp = nullptr;
    GetQuotaInfoFuncPtr getQuotaInfoOp = nullptr;
    AllocQuotaFuncPtr allocQuotaOp = nullptr;
    FreeQuotaFuncPtr freeQuotaOp = nullptr;
    GetNodeViewFuncPtr getNodeViewOp = nullptr;
    GetPtViewFuncPtr getPtViewOp = nullptr;
    CreateFlowMasterFuncPtr createFlowMasterOp = nullptr;
    CreateFlowSlaveFuncPtr createFlowSlaveOp = nullptr;
    DestroyFlowFuncPtr destroyFlowOp = nullptr;
    NotifyUpdateFuncPtr notifyUpdateOp = nullptr;
    CheckUpdateReadyFuncPtr checkUpdateReadyOp = nullptr;
    GetSliceFuncPtr getSliceOp = nullptr;
    PutFuncPtr putOp = nullptr;
    GetFuncPtr getOp = nullptr;
    DeleteFuncPtr deleteOp = nullptr;
    AddDiskFuncPtr addDiskOp = nullptr;
    StatFuncPtr statOp = nullptr;
    ListFuncPtr listOp = nullptr;
    LoadFuncPtr loadOp = nullptr;
    GetCacheHitLocalFuncPtr cacheHitOp = nullptr;
    CalcCacheResourceLocalFuncPtr cacheResourceOp = nullptr;
    GetTracePointsLocalFuncPtr getTracePointsOp = nullptr;
};
}
}
}
#endif