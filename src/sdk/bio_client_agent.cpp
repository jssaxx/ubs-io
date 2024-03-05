/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <iostream>
#include <dlfcn.h>
#include "bio_functions.h"
#include "bio_client_log.h"
#include "message_op.h"
#include "bio_client_net.h"
#include "bio_client_agent.h"

using namespace ock::bio;
using namespace ock::bio::agent;

BResult BioClientAgent::Initialize(WorkerMode mode)
{
    mMode = mode;
    if (mMode == CONVERGENCE) {
        const char *soFileName = "libbio_server.so";
        handler = dlopen(soFileName, RTLD_NOW);
        if (handler == nullptr) {
            CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
            return BIO_ERR;
        }
        if (InitOperation() != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to init operation.");
            return BIO_ERR;
        }
        // Start boostio server for converged deployment mode
        auto ret = startOp();
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to start bio server, ret:" << ret << ".");
            return BIO_ERR;
        }
    }
    return BIO_OK;
}

BResult BioClientAgent::InitOperation()
{
    if ((startOp = reinterpret_cast<BioServerStartFuncPtr>(LoadFunction("BioServerInit"))) == nullptr) {
        return BIO_ERR;
    }
    if ((stopOp = reinterpret_cast<BioServerStopFuncPtr>(LoadFunction("BioServerUninit"))) == nullptr) {
        return BIO_ERR;
    }
    if ((getNetEngineOp = reinterpret_cast<GetBioServerNetEngineFuncPtr>(LoadFunction("GetBioServerNet"))) == nullptr) {
        return BIO_ERR;
    }
    if ((getLocalNidOp = reinterpret_cast<GetLocalNidFuncPtr>(LoadFunction("GetLocalNid"))) == nullptr) {
        return BIO_ERR;
    }
    if ((getNodeViewOp = reinterpret_cast<GetNodeViewFuncPtr>(LoadFunction("GetNodeView"))) == nullptr) {
        return BIO_ERR;
    }
    if ((getPtViewOp = reinterpret_cast<GetPtViewFuncPtr>(LoadFunction("GetPtView"))) == nullptr) {
        return BIO_ERR;
    }
    if ((createFlowMasterOp = reinterpret_cast<CreateFlowMasterFuncPtr>(LoadFunction("CreateFlowMaster"))) == nullptr) {
        return BIO_ERR;
    }
    if ((createFlowSlaveOp = reinterpret_cast<CreateFlowSlaveFuncPtr>(LoadFunction("CreateFlowSlave"))) == nullptr) {
        return BIO_ERR;
    }
    if ((getSliceOp = reinterpret_cast<GetSliceFuncPtr>(LoadFunction("GetSlice"))) == nullptr) {
        return BIO_ERR;
    }
    if ((putOp = reinterpret_cast<PutFuncPtr>(LoadFunction("Put"))) == nullptr) {
        return BIO_ERR;
    }
    if ((getOp = reinterpret_cast<GetFuncPtr>(LoadFunction("Get"))) == nullptr) {
        return BIO_ERR;
    }
    if ((deleteOp = reinterpret_cast<DeleteFuncPtr>(LoadFunction("Delete"))) == nullptr) {
        return BIO_ERR;
    }
    if ((listOp = reinterpret_cast<ListFuncPtr>(LoadFunction("List"))) == nullptr) {
        return BIO_ERR;
    }
    if ((statOp = reinterpret_cast<StatFuncPtr>(LoadFunction("Stat"))) == nullptr) {
        return BIO_ERR;
    }
    if ((loadOp = reinterpret_cast<LoadFuncPtr>(LoadFunction("Load"))) == nullptr) {
        return BIO_ERR;
    }
    if ((hbOp = reinterpret_cast<ReportHbPtr>(LoadFunction("ReportHb"))) == nullptr) {
        return BIO_ERR;
    }
    return BIO_OK;
}

void *BioClientAgent::LoadFunction(const char *name)
{
    void *ptr = dlsym(handler, name);
    if (ptr == nullptr) {
        CLIENT_LOG_ERROR("Failed to load function " << name);
        return nullptr;
    }
    return ptr;
}

BResult BioClientAgent::SendGetLocalNodeInfoRequest(uint16_t &protocol, CmNodeId &localNid)
{
    GetLocalNidRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() } };
    GetLocalNidResponse rsp;
    auto ret = net::BioClientNet::Instance()->SendSync<GetLocalNidRequest, GetLocalNidResponse>(localPid,
        BIO_OP_SDK_GET_NODE_INFO, req, rsp);
    if (ret != BIO_OK) {
        return ret;
    }
    protocol = rsp.protocol;
    localNid = { rsp.groupId, rsp.nodeId };
    return BIO_OK;
}

BResult BioClientAgent::GetLocalNodeInfo(uint16_t &protocol, CmNodeId &localNid)
{
    BResult ret = BIO_OK;
    if (mMode == CONVERGENCE) {
        GetLocalNidResponse getLocalNidRsp{};
        ret = getLocalNidOp(&getLocalNidRsp);
        localNid = { getLocalNidRsp.groupId, getLocalNidRsp.nodeId };
        mLocalNid = localNid;
        protocol = getLocalNidRsp.protocol;
    } else {
        ret = SendGetLocalNodeInfoRequest(protocol, localNid);
        mLocalNid = localNid;
    }
    return ret;
}

BResult BioClientAgent::SendGetClusterNodeViewRequest(uint64_t &curNodeTimes,
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView)
{
    QueryNodeViewRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() } };
    QueryNodeViewResponse rsp;
    auto ret = net::BioClientNet::Instance()->SendSync<QueryNodeViewRequest, QueryNodeViewResponse>(localPid,
        BIO_OP_SDK_GET_NODE_VIEW, req, rsp);
    if (ret != BIO_OK) {
        return ret;
    }

    for (uint32_t i = 0; i < rsp.num; i++) {
        std::vector<CmDiskInfo> disks;
        for (uint32_t j = 0; j < rsp.desc[i].num; j++) {
            disks.push_back(
                { rsp.desc[i].diskDesc[j].diskId, static_cast<CmDiskStatus>(rsp.desc[i].diskDesc[j].diskStatus) });
        }
        nodeView.insert(std::make_pair(CmNodeId(rsp.desc[i].groupId, rsp.desc[i].nodeId),
            CmNodeInfo(CmNodeId(rsp.desc[i].groupId, rsp.desc[i].nodeId), rsp.desc[i].ip, rsp.desc[i].port,
            static_cast<CmNodeStatus>(rsp.desc[i].status), disks)));
    }
    curNodeTimes = rsp.curNodeTimes;
    return BIO_OK;
}

BResult BioClientAgent::GetClusterNodeView(uint64_t &curNodeTimes, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView)
{
    if (mMode == CONVERGENCE) {
        QueryNodeViewResponse queryNodeViewRsp{};
        auto ret = getNodeViewOp(&queryNodeViewRsp);
        for (uint32_t i = 0; i < queryNodeViewRsp.num; i++) {
            std::vector<CmDiskInfo> disks;
            for (uint32_t j = 0; j < queryNodeViewRsp.desc[i].num; j++) {
                disks.push_back({ queryNodeViewRsp.desc[i].diskDesc[j].diskId,
                    static_cast<CmDiskStatus>(queryNodeViewRsp.desc[i].diskDesc[j].diskStatus) });
            }
            nodeView.insert(std::make_pair(CmNodeId(queryNodeViewRsp.desc[i].groupId, queryNodeViewRsp.desc[i].nodeId),
                CmNodeInfo(CmNodeId(queryNodeViewRsp.desc[i].groupId, queryNodeViewRsp.desc[i].nodeId),
                queryNodeViewRsp.desc[i].ip, queryNodeViewRsp.desc[i].port,
                static_cast<CmNodeStatus>(queryNodeViewRsp.desc[i].status), disks)));
        }
        curNodeTimes = queryNodeViewRsp.curNodeTimes;
        return ret;
    } else {
        return SendGetClusterNodeViewRequest(curNodeTimes, nodeView);
    }
}

BResult BioClientAgent::SendGetPtViewRequest(uint64_t &curPtTimes, std::map<uint16_t, CmPtInfo> &ptView)
{
    QueryPtViewRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() } };
    QueryPtViewResponse rsp;
    auto ret = net::BioClientNet::Instance()->SendSync<QueryPtViewRequest, QueryPtViewResponse>(localPid,
        BIO_OP_SDK_QUERY_PT_VIEW, req, rsp);
    if (ret != BIO_OK) {
        return ret;
    }

    for (uint32_t i = 0; i < rsp.num; i++) {
        std::vector<CmPtCopy> copys;
        for (uint32_t j = 0; j < PT_COPY_MAX_SIZE; j++) {
            copys.push_back({ rsp.desc[i].copys[j].nodeId, rsp.desc[i].copys[j].diskId,
                static_cast<CmCopyState>(rsp.desc[i].copys[j].state) });
        }
        ptView.insert(std::make_pair(rsp.desc[i].ptId, CmPtInfo(rsp.desc[i].version, rsp.desc[i].ptId,
            static_cast<CmPtState>(rsp.desc[i].state), rsp.desc[i].masterNodeId, rsp.desc[i].masterDiskId, copys)));
    }
    curPtTimes = rsp.curPtTimes;
    return BIO_OK;
}

BResult BioClientAgent::GetPtView(uint64_t &curPtTimes, std::map<uint16_t, CmPtInfo> &ptView)
{
    if (mMode == CONVERGENCE) {
        QueryPtViewResponse queryPtViewRsp{};
        auto ret = getPtViewOp(&queryPtViewRsp);
        for (uint32_t i = 0; i < queryPtViewRsp.num; i++) {
            std::vector<CmPtCopy> copys;
            for (uint32_t j = 0; j < PT_COPY_MAX_SIZE; j++) {
                copys.push_back({ queryPtViewRsp.desc[i].copys[j].nodeId, queryPtViewRsp.desc[i].copys[j].diskId,
                    static_cast<CmCopyState>(queryPtViewRsp.desc[i].copys[j].state) });
            }
            ptView.insert(std::make_pair(queryPtViewRsp.desc[i].ptId, CmPtInfo(queryPtViewRsp.desc[i].version,
                queryPtViewRsp.desc[i].ptId, static_cast<CmPtState>(queryPtViewRsp.desc[i].state),
                queryPtViewRsp.desc[i].masterNodeId, queryPtViewRsp.desc[i].masterDiskId, copys)));
        }
        curPtTimes = queryPtViewRsp.curPtTimes;
        return ret;
    } else {
        return SendGetPtViewRequest(curPtTimes, ptView);
    }
}

BResult BioClientAgent::SendCreateFlowRequestLocal(CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId)
{
    BResult ret = BIO_OK;
    CreateFlowRequest req;
    if (opType == 0) {
        req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, opType, 0 };
    } else if (opType == 1) {
        req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, opType, flowId };
    }
    CreateFlowResponse rsp;
    do {
        ret = net::BioClientNet::Instance()->SendSync<CreateFlowRequest, CreateFlowResponse>(localPid,
            BIO_OP_SDK_CREATE_FLOW, req, rsp);
        if (UNLIKELY(ret == BIO_NOT_READY)) {
            CLIENT_LOG_WARN("Remote cache service not ready, need retry, ret:" << ret << ", nodeId:" <<
                mLocalNid.VNodeId() << ", ptId:" << ptId << ".");
            sleep(NO_3);
        }
    } while (ret == BIO_NOT_READY);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync create flow request failed, ret:" << ret << ", nodeId:" << mLocalNid.VNodeId() <<
            ", ptId:" << ptId << ".");
        return ret;
    }

    if (opType == 0) {
        flowId = rsp.flowId;
    } else if (opType == 1 && rsp.flowId != 0) {
        ret = BIO_ERR;
    }
    return ret;
}

BResult BioClientAgent::CreateFlowLocal(pid_t procId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
    uint64_t &flowId)
{
    if (mMode == CONVERGENCE) {
        if (opType == 0) {
            CreateFlowRequest req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), procId },
                                      opType,
                                      0 };
            CreateFlowResponse rsp;
            auto ret = createFlowMasterOp(&req, &rsp);
            flowId = rsp.flowId;
            return ret;
        } else {
            CreateFlowRequest req = { { MESSAGE_MAGIC, ptId, 0, 0, procId }, opType, flowId };
            return createFlowSlaveOp(&req);
        }
    } else {
        return SendCreateFlowRequestLocal(ptEntry, ptId, opType, flowId);
    }
}

BResult BioClientAgent::SendPrepareResourceLocal(CmPtInfo &ptEntry, uint64_t flowId, uint64_t offset, uint64_t index,
    uint64_t length, GetSliceResponse **rsp)
{
    GetSliceRequest req = { { MESSAGE_MAGIC, ptEntry.ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() },
                            flowId,
                            offset,
                            index,
                            length };
    uint64_t rspLen = 0;
    return net::BioClientNet::Instance()->SendSync<GetSliceRequest, GetSliceResponse>(localPid, BIO_OP_SDK_GET_SLICE,
        req, rsp, rspLen);
}

BResult BioClientAgent::PrepareResource(CmPtInfo &ptEntry, uint64_t flowId, uint64_t offset, uint64_t index,
    uint64_t length, GetSliceResponse **rsp)
{
    if (mMode == CONVERGENCE) {
        GetSliceRequest req = { { MESSAGE_MAGIC, ptEntry.ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() },
                                flowId,
                                offset,
                                index,
                                length };
        return getSliceOp(&req, rsp);
    } else {
        return SendPrepareResourceLocal(ptEntry, flowId, offset, index, length, rsp);
    }
}

void BioClientAgent::SendPutRequestLocal(PutRequest *req, NetEngine::Callback &callback)
{
    net::BioClientNet::Instance()->SendAsyncBuff(localPid, BIO_OP_SDK_PUT, static_cast<void *>(req),
        sizeof(PutRequest) + req->sliceLen, callback);
}

void BioClientAgent::PutLocal(PutRequest *req, uint32_t localIdx, NetEngine::Callback &callback)
{
    if (mMode == CONVERGENCE) {
        auto ret = putOp(req);
        callback.cb(callback.cbCtx, nullptr, 0, ret);
    } else {
        SendPutRequestLocal(req, callback);
    }
}

BResult BioClientAgent::SendGetRequestLocal(GetRequest &req, uint64_t &realLen)
{
    realLen = 0;
    uint64_t length;
    auto ret = net::BioClientNet::Instance()->SendSync<GetRequest, uint64_t>(localPid, BIO_OP_SDK_GET, req, length);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync get request failed, ret:" << ret << ", key:" << req.key << ", offset:" <<
            req.offset << ", length:" << req.length << ", dstNid:" << mLocalNid.VNodeId() << ".");
    } else {
        realLen = length;
    }
    return ret;
}

BResult BioClientAgent::GetLocal(GetRequest &req, uint64_t &realLen)
{
    if (mMode == CONVERGENCE) {
        GetResponse rsp;
        auto ret = getOp(&req, &rsp);
        realLen = rsp.realLen;
        return ret;
    } else {
        return SendGetRequestLocal(req, realLen);
    }
}

void BioClientAgent::SendDeleteRequestLocal(DeleteRequest &req, NetEngine::Callback &callback)
{
    net::BioClientNet::Instance()->SendAsync<DeleteRequest>(localPid, BIO_OP_SDK_DELETE, req, callback);
}

void BioClientAgent::DeleteLocal(DeleteRequest &req, NetEngine::Callback &callback)
{
    if (mMode == CONVERGENCE) {
        auto ret = deleteOp(&req);
        callback.cb(callback.cbCtx, nullptr, 0, ret);
    } else {
        return SendDeleteRequestLocal(req, callback);
    }
}

BResult BioClientAgent::SendListRequestLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    ListResponse *rsp = nullptr;
    uint64_t rspLen = 0;
    BResult ret = net::BioClientNet::Instance()->SendSync<ListRequest, ListResponse>(localPid,
        BIO_OP_SDK_LIST, req, &rsp, rspLen);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send sync list request failed, ret:" << ret << ", prefix:" << req.prefix << ".");
        return ret;
    }

    auto statInfo = static_cast<ObjStat *>(static_cast<void*>(rsp->statBuf));
    for (uint32_t i = 0; i < rsp->num; i++) {
        ObjStat stat;
        CopyKey(stat.key, statInfo[i].key, KEY_MAX_SIZE);
        stat.size = statInfo[i].size;
        stat.time = statInfo[i].time;
        objs.insert({ stat.key, stat });
    }
    delete[] rsp;
    return BIO_OK;
}

BResult BioClientAgent::SendStatRequestLocal(StatRequest &req, ObjStat &objInfo)
{
    return net::BioClientNet::Instance()->SendSync<StatRequest, ObjStat>(localPid, BIO_OP_SDK_STAT, req, objInfo);
}

BResult BioClientAgent::StatLocal(StatRequest &req, ObjStat &objInfo)
{
    if (mMode == CONVERGENCE) {
        StatResponse rsp{};
        auto ret = statOp(&req, &rsp);
        CopyKey(objInfo.key, req.key, KEY_MAX_SIZE);
        objInfo.size = rsp.size;
        objInfo.time = rsp.time;
        return ret;
    } else {
        return SendStatRequestLocal(req, objInfo);
    }
}

BResult BioClientAgent::ListLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    if (mMode == CONVERGENCE) {
        ListResponse *rsp = nullptr;
        auto ret = listOp(&req, &rsp);
        if (ret != BIO_OK) {
            return ret;
        }
        auto statBuff = static_cast<ObjStat *>(static_cast<void*>(rsp->statBuf));
        for (uint32_t i = 0; i < rsp->num; i++) {
            if (objs.size() >= 1000U) {
                break;
            }
            ObjStat stat;
            CopyKey(stat.key, statBuff[i].key, KEY_MAX_SIZE);
            stat.size = statBuff[i].size;
            stat.time = statBuff[i].time;
            objs.insert({ stat.key, stat });
        }
        delete[] rsp;
        return BIO_OK;
    } else {
        return SendListRequestLocal(req, objs);
    }
}

BResult BioClientAgent::SendLoadRequestLocal(LoadRequest &req)
{
    BResult result = BIO_OK;
    auto ret = net::BioClientNet::Instance()->SendSync<LoadRequest, BResult>(localPid, BIO_OP_SDK_LOAD, req, result);
    if (ret != BIO_OK) {
        return ret;
    }
    return result;
}

void BioClientAgent::LoadLocal(LoadRequest &req, const Bio::LoadCallback &callback, void *context)
{
    if (mMode == CONVERGENCE) {
        auto ret = loadOp(&req);
        callback(context, ((ret == BIO_OK) ? RET_CACHE_OK : RET_CACHE_ERROR));
        return;
    } else {
        auto ret = SendLoadRequestLocal(req);
        callback(context, ((ret == BIO_OK) ? RET_CACHE_OK : RET_CACHE_ERROR));
    }
}

BResult BioClientAgent::SendHbRequest(uint64_t &curNodeTimes, uint64_t &curPtTimes)
{
    HbRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() } };
    HbResponse rsp;
    auto ret = net::BioClientNet::Instance()->SendSync<HbRequest, HbResponse>(localPid,
        BIO_OP_SDK_REPORT_HB, req, rsp);
    if (ret != BIO_OK) {
        return ret;
    }

    curNodeTimes = rsp.curNodeTimes;
    curPtTimes = rsp.curPtTimes;
    return BIO_OK;
}

BResult BioClientAgent::ReportHb(uint64_t &curNodeTimes, uint64_t &curPtTimes)
{
    if (mMode == CONVERGENCE) {
        HbResponse rsp{};
        auto ret = hbOp(&rsp.curNodeTimes, &rsp.curPtTimes);
        curNodeTimes = rsp.curNodeTimes;
        curPtTimes = rsp.curPtTimes;
        return ret;
    } else {
        return SendHbRequest(curNodeTimes, curPtTimes);
    }
}