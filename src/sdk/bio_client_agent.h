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
    using BioServerStopFuncPtr = void (*)();
    using GetBioServerNetEngineFuncPtr = uintptr_t (*)();
    using GetLocalNidFuncPtr = int32_t (*)(GetLocalNidResponse *);
    using GetNodeViewFuncPtr = int32_t (*)(QueryNodeViewRequest *, QueryNodeViewResponse *);
    using GetPtViewFuncPtr = int32_t (*)(QueryPtViewRequest *, QueryPtViewResponse *);
    using CreateFlowMasterFuncPtr = int32_t (*)(CreateFlowRequest *, CreateFlowResponse *);
    using CreateFlowSlaveFuncPtr = int32_t (*)(CreateFlowRequest *);
    using GetSliceFuncPtr = int32_t (*)(GetSliceRequest *, GetSliceResponse **);
    using PutFuncPtr = int32_t (*)(PutRequest *);
    using GetFuncPtr = int32_t (*)(GetRequest *, GetResponse *);
    using DeleteFuncPtr = int32_t (*)(DeleteRequest *);
    using StatFuncPtr = int32_t (*)(StatRequest *, StatResponse *);
    using ListFuncPtr = int32_t (*)(ListRequest *, ListResponse **);
    using LoadFuncPtr = int32_t (*)(LoadRequest *);
    using ReportHbPtr = int32_t (*)(uint64_t *, uint64_t *);

    BioClientAgent()
    {
        mLocalNid.groupId = 0;
        mLocalNid.nodeId = UINT16_MAX;
        localPid = static_cast<uint32_t>(getpid());
    }
    ~BioClientAgent() {}

    static BioClientAgentPtr &Instance()
    {
        static auto instance = MakeRef<BioClientAgent>();
        return instance;
    }

    BResult Initialize(WorkerMode mode);

    NetEnginePtr GetNetService()
    {
        uintptr_t netPtr = getNetEngineOp();
        NetEnginePtr netEngine = MakeRef<NetEngine>();
        netEngine = reinterpret_cast<NetEngine *>(netPtr);
        return netEngine;
    }

    BResult GetLocalNodeInfo(uint16_t &protocol, CmNodeId &localNid);

    BResult GetClusterNodeView(uint64_t &curNodeTimes, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView);

    BResult GetPtView(uint64_t &curPtTimes, std::map<uint16_t, CmPtInfo> &ptView);

    BResult CreateFlowLocal(pid_t procId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId);

    BResult PrepareResource(CmPtInfo &ptEntry, uint64_t flowId, uint64_t offset, uint64_t index, uint64_t length,
        GetSliceResponse **rsp);

    void PutLocal(PutRequest *req, NetEngine::Callback &callback);

    BResult GetLocal(GetRequest &req, char *value, uint64_t &realLen);

    void DeleteLocal(DeleteRequest &req, NetEngine::Callback &callback);

    BResult ListLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    
    BResult StatLocal(StatRequest &req, ObjStat &objInfo);

    BResult LoadLocal(LoadRequest &req);

    BResult ReportHb(uint64_t &curNodeTimes, uint64_t &curPtTimes);

private:
    BResult InitOperation();
    void *LoadFunction(const char *name);

    BResult SendGetLocalNodeInfoRequest(uint16_t &protocol, CmNodeId &localNid);

    BResult SendCreateFlowRequestLocal(CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId);

    BResult SendPrepareResourceLocal(CmPtInfo &ptEntry, uint64_t flowId, uint64_t offset, uint64_t index,
        uint64_t length, GetSliceResponse **rsp);

    void SendPutRequestLocal(PutRequest *req, NetEngine::Callback &callback);

    BResult SendGetRequestLocal(GetRequest &req, char *value, uint64_t &realLen);

    void SendDeleteRequestLocal(DeleteRequest &req, NetEngine::Callback &callback);

    BResult CallServerListIntf(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    BResult SendListRequestLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);

    BResult SendStatRequestLocal(StatRequest &req, ObjStat &objInfo);

    BResult SendLoadRequestLocal(LoadRequest &req);

    BResult SendHbRequest(uint64_t &curNodeTimes, uint64_t &curPtTimes);

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    WorkerMode mMode = CONVERGENCE;
    CmNodeId mLocalNid;
    uint32_t localPid;
    DEFINE_REF_COUNT_VARIABLE;

    void *handler = nullptr;
    BioServerStartFuncPtr startOp = nullptr;
    BioServerStopFuncPtr stopOp = nullptr;
    GetBioServerNetEngineFuncPtr getNetEngineOp = nullptr;
    GetLocalNidFuncPtr getLocalNidOp = nullptr;
    GetNodeViewFuncPtr getNodeViewOp = nullptr;
    GetPtViewFuncPtr getPtViewOp = nullptr;
    CreateFlowMasterFuncPtr createFlowMasterOp = nullptr;
    CreateFlowSlaveFuncPtr createFlowSlaveOp = nullptr;
    GetSliceFuncPtr getSliceOp = nullptr;
    PutFuncPtr putOp = nullptr;
    GetFuncPtr getOp = nullptr;
    DeleteFuncPtr deleteOp = nullptr;
    StatFuncPtr statOp = nullptr;
    ListFuncPtr listOp = nullptr;
    LoadFuncPtr loadOp = nullptr;
    ReportHbPtr hbOp = nullptr;
};
}
}
}
#endif