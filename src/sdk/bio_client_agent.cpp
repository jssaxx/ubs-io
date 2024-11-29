/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <iostream>
#include <dlfcn.h>
#include "bio_functions.h"
#include "bio_client_log.h"
#include "message_op.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "bio_client_net.h"
#include "bio_crc_util.h"
#include "bio_client_agent.h"

using namespace ock::bio;
using namespace ock::bio::agent;

BResult BioClientAgent::Initialize(WorkerMode mode)
{
    mMode = mode;
    if (mMode == CONVERGENCE) {
#ifdef DEBUG_UT
        const char *soFileName = "libbio_server.so";
        handler = dlopen(soFileName, RTLD_NOW);
#else
        std::string soFileName = std::string(PROJECT_PATH_PREFIX) + "/lib/libbio_server.so";
        char *canonicalPath = realpath(soFileName.c_str(), nullptr);
        if (canonicalPath == nullptr) {
            CLIENT_LOG_ERROR("Failed to open library, not exist, " << soFileName << ".");
            return BIO_NOT_EXISTS;
        }

        handler = dlopen(canonicalPath, RTLD_NOW);
        free(canonicalPath);
        canonicalPath = nullptr;
#endif
        if (handler == nullptr) {
            CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
            return BIO_INNER_ERR;
        }

        if (InitOperation() != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to init operation.");
            return BIO_INNER_ERR;
        }

        // Start boostio server for converged deployment mode
        int32_t ret = BIO_INNER_ERR;
        LVOS_TP_START(SDK_BIO_AGENT_START_OP_FAIL, &ret, BIO_INNER_ERR);
        ret = startOp();
        LVOS_TP_END;
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Failed to start bio server, ret:" << ret << ".");
            return BIO_INNER_ERR;
        }
    }
    return BIO_OK;
}

void BioClientAgent::Exit()
{
    if (mMode == CONVERGENCE) {
        exitOp();
    }
}

BResult BioClientAgent::InitUpgradeOperation()
{
    if ((notifyUpdateOp = reinterpret_cast<NotifyUpdateFuncPtr>(LoadFunction("NotifyUpdate"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((checkUpdateReadyOp = reinterpret_cast<CheckUpdateReadyFuncPtr>(LoadFunction("CheckUpdateReady"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    return BIO_OK;
}

BResult BioClientAgent::InitOperation()
{
    if ((startOp = reinterpret_cast<BioServerStartFuncPtr>(LoadFunction("BioServerInit"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((exitOp = reinterpret_cast<BioServerExitFuncPtr>(LoadFunction("BioServerExit"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getCrcFlag = reinterpret_cast<GetBioServerCrcFlagFuncPtr>(LoadFunction("GetCrcFlag"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getNetEngineOp = reinterpret_cast<GetBioServerNetEngineFuncPtr>(LoadFunction("GetBioServerNet"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getLocalNidOp = reinterpret_cast<GetLocalNidFuncPtr>(LoadFunction("GetLocalNid"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getQuotaInfoOp = reinterpret_cast<GetQuotaInfoFuncPtr>(LoadFunction("GetQuotaInfo"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((allocQuotaOp = reinterpret_cast<AllocQuotaFuncPtr>(LoadFunction("AllocQuota"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((freeQuotaOp = reinterpret_cast<FreeQuotaFuncPtr>(LoadFunction("FreeQuota"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getNodeViewOp = reinterpret_cast<GetNodeViewFuncPtr>(LoadFunction("GetNodeView"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getPtViewOp = reinterpret_cast<GetPtViewFuncPtr>(LoadFunction("GetPtView"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((createFlowMasterOp = reinterpret_cast<CreateFlowMasterFuncPtr>(LoadFunction("CreateFlowMaster"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((createFlowSlaveOp = reinterpret_cast<CreateFlowSlaveFuncPtr>(LoadFunction("CreateFlowSlave"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((destroyFlowOp = reinterpret_cast<DestroyFlowFuncPtr>(LoadFunction("DestroyFlow"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getSliceOp = reinterpret_cast<GetSliceFuncPtr>(LoadFunction("GetSlice"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((putOp = reinterpret_cast<PutFuncPtr>(LoadFunction("Put"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getOp = reinterpret_cast<GetFuncPtr>(LoadFunction("Get"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((deleteOp = reinterpret_cast<DeleteFuncPtr>(LoadFunction("Delete"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((listOp = reinterpret_cast<ListFuncPtr>(LoadFunction("List"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((statOp = reinterpret_cast<StatFuncPtr>(LoadFunction("Stat"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((loadOp = reinterpret_cast<LoadFuncPtr>(LoadFunction("Load"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((hbOp = reinterpret_cast<ReportHbPtr>(LoadFunction("ReportHb"))) == nullptr) {
        return BIO_INNER_ERR;
    }

    return InitUpgradeOperation();
}

void *BioClientAgent::LoadFunction(const char *name)
{
    void *ptr = nullptr;
    LVOS_TP_START(SDK_BIO_AGENT_LOAD_FUNC_FAIL, &ptr, nullptr);
    ptr = dlsym(handler, name);
    LVOS_TP_END;
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
    auto ret = net::BioClientNet::Instance()->SendSync<GetLocalNidRequest, GetLocalNidResponse>(INVALID_NID,
        BIO_OP_SDK_GET_NODE_INFO, req, rsp);
    if (ret != BIO_OK) {
        return ret;
    }
    protocol = rsp.protocol;
    localNid = { rsp.groupId, rsp.nodeId };
    return BIO_OK;
}

BResult BioClientAgent::SendGetNodeInfoRequest(uint16_t masterPtId, uint16_t slavePtId, FileLocationQueryRsp &rsp)
{
    FileLocationQueryReq req = { masterPtId, slavePtId };
    auto ret = net::BioClientNet::Instance()->SendSync<FileLocationQueryReq, FileLocationQueryRsp>(INVALID_NID,
        BIO_OP_SDK_GET_NODE_INFO_BY_PT, req, rsp);
    if (ret != BIO_OK) {
        return ret;
    }

    return BIO_OK;
}

bool BioClientAgent::GetConfigCrcFlag()
{
    return getCrcFlag();
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

BResult BioClientAgent::GetLocalQuotaInfo(uint32_t scene, bool &enable, uint64_t &preloadSize)
{
    BResult ret = BIO_OK;
    QueryQuotaRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() }, scene };
    QueryQuotaResponse rsp;
    LVOS_TP_START(NO_PROCESS_GET_LOCAL_QUOTA, 0);
    if (mMode == CONVERGENCE) {
        ret = getQuotaInfoOp(&req, &rsp);
    } else {
        ret = net::BioClientNet::Instance()->SendSync<QueryQuotaRequest, QueryQuotaResponse>(INVALID_NID,
            BIO_OP_SDK_GET_QUOTA_INFO, req, rsp);
    }
    LVOS_TP_END;
    if (ret == BIO_OK) {
        enable = rsp.enable;
        uint64_t preSize = rsp.preloadSize;
        LVOS_TP_START(GET_LOCAL_QUOTA_SET_PRE_LOAD_SIZE, &preSize, 135, 266, 304);
        LVOS_TP_END;
        if (preSize > NO_128 * IO_SIZE_1M) {
            CLIENT_LOG_ERROR("Too large size " << rsp.preloadSize << ".");
            return BIO_ERR;
        }
        preloadSize = rsp.preloadSize;
    }
    return ret;
}

BResult BioClientAgent::AllocQuota(AllocQuotaRequest &req, uint64_t &expectPreloadSize)
{
    BResult ret = BIO_INNER_ERR;
    AllocQuotaResponse rsp = { 0 };
    if (mMode == CONVERGENCE) {
        ret = allocQuotaOp(&req, &rsp);
    } else {
        ret = net::BioClientNet::Instance()->SendSync<AllocQuotaRequest, AllocQuotaResponse>(INVALID_NID,
            BIO_OP_SDK_ALLOC_QUOTA, req, rsp);
    }
    expectPreloadSize = std::min<uint64_t>(expectPreloadSize, rsp.exceptQuota);\
    if (expectPreloadSize > NO_1024 * IO_SIZE_1M) {
        return BIO_ERR;
    }
    return ret;
}

BResult BioClientAgent::FreeQuota(FreeQuotaRequest &req)
{
    BResult ret = BIO_INNER_ERR;
    if (mMode == CONVERGENCE) {
        ret = freeQuotaOp(&req);
    } else {
        BResult hdlRet = BIO_INNER_ERR;
        ret = net::BioClientNet::Instance()->SendSync<FreeQuotaRequest, BResult>(INVALID_NID, BIO_OP_SDK_FREE_QUOTA,
            req, hdlRet);
        if (ret == BIO_OK && hdlRet != BIO_OK) {
            ret = hdlRet;
        }
    }
    return ret;
}

BResult BioClientAgent::GetClusterNodeView(uint64_t &curNodeTimes,
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView)
{
    BResult ret = BIO_OK;
    int32_t flag = 0;
    uint32_t progressBar = 0;
    static uint32_t maxRetryCnt = NO_512;
    uint32_t retryCnt = 0;
    do {
        QueryNodeViewRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() }, progressBar };
        QueryNodeViewResponse rsp;
        if (mMode == CONVERGENCE) {
            ret = getNodeViewOp(&req, &rsp);
        } else {
            ret = net::BioClientNet::Instance()->SendSync<QueryNodeViewRequest, QueryNodeViewResponse>(INVALID_NID,
                BIO_OP_SDK_GET_NODE_VIEW, req, rsp);
        }
        if (ret != BIO_OK) {
            nodeView.clear();
            return ret;
        }

        LVOS_TP_START(SDK_BIO_AGENT_GET_CLUSTER_NODE_VIEW_NUM_INVALID, &rsp.num, (CLUSTER_NODE_SIZE + 1));
        LVOS_TP_END;
        if (rsp.num > CLUSTER_NODE_SIZE) {
            CLIENT_LOG_ERROR("rsp num: " << rsp.num << " is invalid.");
            nodeView.clear();
            return BIO_INVALID_PARAM;
        }

        for (uint32_t i = 0; i < rsp.num; i++) {
            std::vector<CmDiskInfo> disks;
            LVOS_TP_START(SDK_BIO_AGENT_GET_CLUSTER_NODE_VIEW_NODEID_INVALID, &rsp.desc[0].num, (DISK_MAX_SIZE + 1));
            LVOS_TP_END;
            if (rsp.desc[i].num > DISK_MAX_SIZE) {
                CLIENT_LOG_ERROR("rsp nodeid(" << rsp.desc[i].nodeId << ") num: " << rsp.desc[i].num << " is invalid.");
                nodeView.clear();
                return BIO_INVALID_PARAM;
            }
            for (uint32_t j = 0; j < rsp.desc[i].num; j++) {
                disks.push_back(
                    { rsp.desc[i].diskDesc[j].diskId, static_cast<CmDiskStatus>(rsp.desc[i].diskDesc[j].diskStatus) });
            }
            nodeView.insert(std::make_pair(CmNodeId(rsp.desc[i].groupId, rsp.desc[i].nodeId),
                CmNodeInfo(CmNodeId(rsp.desc[i].groupId, rsp.desc[i].nodeId), rsp.desc[i].ip, rsp.desc[i].port,
                static_cast<CmNodeStatus>(rsp.desc[i].status), disks)));
        }
        flag = rsp.flag;
        progressBar += rsp.num;
        curNodeTimes = rsp.curNodeTimes;
        if ((retryCnt++) > maxRetryCnt) { // 分片获取集群视图防止死循环, 在512次分片内必定可以获取完整的集群视图.
            break;
        }
    } while (flag == 1);

    return BIO_OK;
}

bool BioClientAgent::CheckGetPtViewRsp(QueryPtViewResponse rsp)
{
    if (rsp.num > PT_SIZE || rsp.copyNum > PT_COPY_MAX_SIZE) {
        CLIENT_LOG_ERROR("rsp num: " << rsp.num << " or copyNum: " << rsp.copyNum << " is invalid.");
        return false;
    }
    return true;
}

BResult BioClientAgent::GetPtView(uint64_t &curPtTimes, std::map<uint16_t, CmPtInfo> &ptView)
{
    BResult ret = BIO_OK;
    int32_t flag = 0;
    uint32_t progressBar = 0;
    static uint32_t maxRetryCnt = NO_1024;
    uint32_t retryCnt = 0;
    do {
        QueryPtViewRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() }, progressBar };
        QueryPtViewResponse rsp;
        if (mMode == CONVERGENCE) {
            ret = getPtViewOp(&req, &rsp);
        } else {
            ret = net::BioClientNet::Instance()->SendSync<QueryPtViewRequest, QueryPtViewResponse>(INVALID_NID,
                BIO_OP_SDK_QUERY_PT_VIEW, req, rsp);
        }
        if (ret != BIO_OK) {
            ptView.clear();
            return ret;
        }
        LVOS_TP_START(SDK_BIO_AGENT_GET_PT_VIEW_RSP_NUM_INVALID, &rsp.num, (PT_SIZE + 1));
        LVOS_TP_END;
        if (!CheckGetPtViewRsp(rsp)) {
            return BIO_INVALID_PARAM;
        }
        for (uint32_t i = 0; i < rsp.num; i++) {
            std::vector<CmPtCopy> copys;
            for (uint32_t j = 0; j < rsp.copyNum; j++) {
                copys.push_back({ rsp.desc[i].copys[j].nodeId, rsp.desc[i].copys[j].diskId,
                    static_cast<CmCopyState>(rsp.desc[i].copys[j].state) });
            }
            ptView.insert(std::make_pair(rsp.desc[i].ptId, CmPtInfo(rsp.desc[i].version, rsp.desc[i].ptId,
                static_cast<CmPtState>(rsp.desc[i].state), rsp.desc[i].masterNodeId, rsp.desc[i].masterDiskId, copys)));
        }
        flag = rsp.flag;
        progressBar += rsp.num;
        curPtTimes = rsp.curPtTimes;
        if ((retryCnt++) > maxRetryCnt) { // 分片获取分区视图防止死循环, 在1024次分片内必定可以获取完整的分区视图.
            break;
        }
    } while (flag == 1);

    return BIO_OK;
}

BResult BioClientAgent::SendCreateFlowRequestLocal(CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId,
    bool &isDegrade)
{
    BResult ret = BIO_OK;
    CreateFlowRequest req;
    if (opType == 0) {
        req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, opType, 0, false };
    } else if (opType == 1) {
        req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, opType, flowId, isDegrade };
    }
    CreateFlowResponse rsp;
    static uint32_t createFlowTimeout = NO_60;
    uint64_t startTime = Monotonic::TimeSec();
    do {
        ret = net::BioClientNet::Instance()->SendSync<CreateFlowRequest, CreateFlowResponse>(INVALID_NID,
            BIO_OP_SDK_CREATE_FLOW, req, rsp);
        uint64_t retryTime = Monotonic::TimeSec() - startTime;
        if (UNLIKELY(ret == BIO_NOT_READY && retryTime < createFlowTimeout)) {
            CLIENT_LOG_WARN("Remote cache service not ready, need retry, ret:" << ret << ", nodeId:" <<
                mLocalNid.VNodeId() << ", ptId:" << ptId << ".");
            sleep(NO_3);
        } else {
            break;
        }
    } while (ret == BIO_NOT_READY);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync create flow request failed, ret:" << ret << ", nodeId:" << mLocalNid.VNodeId() <<
            ", ptId:" << ptId << ".");
        return ret;
    }

    if (opType == 0) {
        flowId = rsp.flowId;
        isDegrade = rsp.isDegrade;
    } else if (opType == 1 && rsp.flowId != 0) {
        ret = BIO_ERR;
    }
    return ret;
}

BResult BioClientAgent::SendDestroyFlowRequestLocal(CmPtInfo &ptEntry, uint16_t ptId, uint64_t flowId)
{
    DestroyFlowRequest req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, flowId };
    DestroyFlowResponse rsp;
    BResult ret = net::BioClientNet::Instance()->SendSync<DestroyFlowRequest, DestroyFlowResponse>(INVALID_NID,
        BIO_OP_SDK_DESTROY_FLOW, req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync destroy flow request failed:" << ret << ", nodeId:" << mLocalNid.VNodeId() <<
            ", ptId:" << ptId << ", flowId:" << flowId << ".");
        return ret;
    }

    return BIO_OK;
}

BResult BioClientAgent::CreateFlowLocal(pid_t procId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
    uint64_t &flowId, bool &isDegrade)
{
    if (mMode == CONVERGENCE) {
        if (opType == 0) {
            CreateFlowRequest req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), procId },
                                      opType,
                                      0,
                                      false };
            CreateFlowResponse rsp;
            auto ret = createFlowMasterOp(&req, &rsp);
            flowId = rsp.flowId;
            isDegrade = rsp.isDegrade;
            return ret;
        } else {
            CreateFlowRequest req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), procId },
                                      opType,
                                      flowId,
                                      isDegrade };
            return createFlowSlaveOp(&req);
        }
    } else {
        return SendCreateFlowRequestLocal(ptEntry, ptId, opType, flowId, isDegrade);
    }
}

BResult BioClientAgent::DestroyFlowLocal(pid_t procId, CmPtInfo &ptEntry, uint16_t ptId, uint64_t flowId)
{
    if (mMode == CONVERGENCE) {
        DestroyFlowRequest req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, flowId };
        return destroyFlowOp(&req);
    } else {
        return SendDestroyFlowRequestLocal(ptEntry, ptId, flowId);
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
    auto ret = net::BioClientNet::Instance()->SendSync<GetSliceRequest, GetSliceResponse>(INVALID_NID,
        BIO_OP_SDK_GET_SLICE, req, rsp, rspLen);
    if (ret != BIO_OK) {
        return ret;
    } else if (rspLen < (*rsp)->sliceLen + sizeof(GetSliceResponse)) {
        return BIO_INVALID_PARAM;
    }

    return BIO_OK;
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

void BioClientAgent::SendPutRequestLocal(PutRequest *req, Callback &callback)
{
    net::BioClientNet::Instance()->SendAsyncBuff(INVALID_NID, BIO_OP_SDK_PUT, static_cast<void *>(req),
        sizeof(PutRequest) + req->sliceLen, callback);
}

void BioClientAgent::PutLocal(PutRequest *req, Callback &callback)
{
    if (mMode == CONVERGENCE) {
        PutResponse rsp;
        BIO_TRACE_START(SDK_TRACE_PUT_LOCAL_SYNC);
        auto ret = putOp(req, &rsp);
        BIO_TRACE_END(SDK_TRACE_PUT_LOCAL_SYNC, ret);
        callback.cb(callback.cbCtx, &rsp, sizeof(PutResponse), ret);
    } else {
        BIO_TRACE_START(SDK_TRACE_PUT_LOCAL_SYNC);
        SendPutRequestLocal(req, callback);
        BIO_TRACE_END(SDK_TRACE_PUT_LOCAL_SYNC, BIO_OK);
    }
}

bool BioClientAgent::CheckGetRsp(GetResponse rsp)
{
    if (rsp.num > SLICE_ADDR_SIZE) {
        return false;
    }
    for (uint32_t idx = 0; idx < rsp.num; idx++) {
        if (rsp.addrLen[idx] > BIO_IO_MAX_LEN) {
            return false;
        }
    }
    return true;
}

BResult BioClientAgent::SendGetRequestLocal(GetRequest &req, char *value, uint64_t &realLen)
{
    GetResponse rsp;
    auto ret = net::BioClientNet::Instance()->SendSync<GetRequest, GetResponse>(INVALID_NID, BIO_OP_SDK_GET, req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync get request failed, ret:" << ret << ", key:" << req.key << ", offset:" <<
            req.offset << ", length:" << req.length << ", dstNid:" << mLocalNid.VNodeId() << ".");
    } else {
        if (!CheckGetRsp(rsp)) {
            return BIO_INVALID_PARAM;
        }
        realLen = rsp.realLen;
        if (realLen > req.length) {
            return BIO_INNER_ERR;
        }
        uint64_t off = 0;
        uint64_t cpyLength = req.length;
        for (uint32_t idx = 0; idx < rsp.num; idx++) {
            uint8_t *addr = net::BioClientNet::Instance()->GetShmAddress(rsp.addrOffset[idx], rsp.addrLen[idx]);
			if (addr == nullptr) {
                CLIENT_LOG_ERROR("Send sync request get shm addr failed.");
                break;
            }
            ret =
                memcpy_s(static_cast<void *>(value + off), cpyLength, reinterpret_cast<void *>(addr), rsp.addrLen[idx]);
            if (UNLIKELY(ret != 0)) {
                CLIENT_LOG_ERROR("Memory copy data to user failed, ret:" << ret << ", idx:" << idx << ", len:" <<
                    rsp.addrLen[idx] << ".");
                break;
            }
            off += rsp.addrLen[idx];
            cpyLength -= rsp.addrLen[idx];
        }

        if (rsp.isAlloc) {
            FreeMemRequest freeReq = { req.comm, rsp.num, { 0 } };
            for (uint32_t idx = 0; idx < rsp.num; idx++) {
                freeReq.addr[idx] = rsp.addrOffset[idx];
            }
            auto freeRet =
                net::BioClientNet::Instance()->SendAsync<FreeMemRequest>(INVALID_NID, BIO_OP_SDK_FREE_MEM, freeReq);
            if (freeRet != BIO_OK) {
                CLIENT_LOG_ERROR("Send async free request failed, ret:" << ret << ".");
            }
        }
        if (req.enableCrc && ret == BIO_OK) {
            uint32_t currentCrc = BioCrcUtil::Crc32(value, rsp.realLen);
            if (rsp.dataCrc != currentCrc) {
                CLIENT_LOG_ERROR("Client get failed to verify the CRC, << key:" << req.key << ", origin crc:" <<
                    rsp.dataCrc << ", current crc:" << currentCrc);
                ret = BIO_CRC_ERR;
            }
        }
    }
    return ret;
}

BResult BioClientAgent::GetLocal(GetRequest &req, char *value, uint64_t &realLen)
{
    req.size = req.length;
    if (mMode == CONVERGENCE) {
        req.isMr = 0;
        req.address = reinterpret_cast<uintptr_t>(value);
        GetResponse rsp;
        auto ret = getOp(&req, &rsp);
        realLen = rsp.realLen;
        uint64_t off = 0;
        uint64_t cpyLength = req.length;
        for (uint32_t idx = 0; idx < rsp.num; idx++) {
            ret = memcpy_s(static_cast<void *>(value + off), cpyLength, reinterpret_cast<void *>(rsp.address[idx]),
                rsp.addrLen[idx]);
            if (UNLIKELY(ret != 0)) {
                CLIENT_LOG_ERROR("Memory copy data to user failed, ret:" << ret << ", idx:" << idx << ", len:" <<
                    rsp.addrLen[idx] << ".");
                break;
            }
            off += rsp.addrLen[idx];
            cpyLength -= rsp.addrLen[idx];
        }
        if (req.enableCrc && ret == BIO_OK) {
            uint32_t currentCrc = BioCrcUtil::Crc32(value, rsp.realLen);
            if (rsp.dataCrc != currentCrc) {
                CLIENT_LOG_ERROR("Client get failed to verify the CRC, key:" << req.key << ", origin crc:" <<
                    rsp.dataCrc << ", current crc:" << currentCrc << ".");
                ret = BIO_CRC_ERR;
            }
        }
        return ret;
    } else {
        return SendGetRequestLocal(req, value, realLen);
    }
}

void BioClientAgent::SendDeleteRequestLocal(DeleteRequest &req, Callback &callback)
{
    net::BioClientNet::Instance()->SendAsync<DeleteRequest>(INVALID_NID, BIO_OP_SDK_DELETE, req, callback);
}

void BioClientAgent::DeleteLocal(DeleteRequest &req, Callback &callback)
{
    if (mMode == CONVERGENCE) {
        auto ret = deleteOp(&req);
        callback.cb(callback.cbCtx, &ret, sizeof(ret), BIO_OK);
    } else {
        return SendDeleteRequestLocal(req, callback);
    }
}

BResult BioClientAgent::CallServerListIntf(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    req.address = 0;
    req.size = 0;
    req.mrKey = 0;
    ListResponse *rsp = nullptr;
    auto ret = listOp(&req, &rsp);
    if (ret != BIO_OK) {
        return ret;
    }

    if (rsp->num != 0) {
        auto statBuff = static_cast<ObjStat *>(static_cast<void *>(rsp->statBuf));
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
    }
    delete[] rsp;
    return BIO_OK;
}

BResult BioClientAgent::SendListRequestLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    req.address = 0;
    req.size = 0;
    req.mrKey = 0;
    ListResponse rsp;
    BResult ret =
        net::BioClientNet::Instance()->SendSync<ListRequest, ListResponse>(INVALID_NID, BIO_OP_SDK_LIST, req, rsp);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send sync list request failed, ret:" << ret << ", prefix:" << req.prefix << ".");
        return ret;
    }

    if (rsp.num != 0) {
        uint8_t *addr = net::BioClientNet::Instance()->GetShmAddress(rsp.addrOffset, (rsp.num * sizeof(ObjStat)));
		if (addr == nullptr) {
            CLIENT_LOG_ERROR("Send list request get shm addr failed.");
            return BIO_INNER_ERR;
        }
        auto statInfo = static_cast<ObjStat *>(static_cast<void *>(addr));
        for (uint32_t i = 0; i < rsp.num; i++) {
            if (objs.size() >= 1000U) {
                break;
            }
            ObjStat stat;
            if (strlen(statInfo[i].key) >= MAX_KEY_SIZE) {
                continue;
            }
            CopyKey(stat.key, statInfo[i].key, KEY_MAX_SIZE);
            stat.size = statInfo[i].size;
            stat.time = statInfo[i].time;
            objs.insert({ stat.key, stat });
        }

        FreeMemRequest freeReq = { req.comm, 1, { 0 } };
        freeReq.addr[0] = rsp.addr;
        ret = net::BioClientNet::Instance()->SendAsync<FreeMemRequest>(INVALID_NID, BIO_OP_SDK_FREE_MEM, freeReq);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Send async free request failed, ret:" << ret << ".");
        }
    }
    return BIO_OK;
}

BResult BioClientAgent::SendStatRequestLocal(StatRequest &req, ObjStat &objInfo)
{
    return net::BioClientNet::Instance()->SendSync<StatRequest, ObjStat>(INVALID_NID, BIO_OP_SDK_STAT, req, objInfo);
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

BResult BioClientAgent::SendNotifyUpdateRequestLocal(NotifyUpdateRequest &req)
{
    return net::BioClientNet::Instance()->SendAsync<NotifyUpdateRequest>(INVALID_NID, BIO_OP_SDK_NOTIFY_UPDATE, req);
}

BResult BioClientAgent::NotifyUpdate(bool &flag)
{
    NotifyUpdateRequest req = { { MESSAGE_MAGIC, 0, 0, mLocalNid.VNodeId(), getpid() }, flag };
    if (mMode == CONVERGENCE) {
        return notifyUpdateOp(&req);
    } else {
        return SendNotifyUpdateRequestLocal(req);
    }
}

BResult BioClientAgent::SendCheckUpdateReadyRequestLocal(CheckUpdateReadyRequest &req, CheckUpdateReadyResponse &rsp)
{
    return net::BioClientNet::Instance()->SendSync<CheckUpdateReadyRequest, CheckUpdateReadyResponse>(INVALID_NID,
        BIO_OP_SDK_CHECK_UPDATE_READY, req, rsp);
}

BResult BioClientAgent::CheckUpdateReady()
{
    BResult ret = BIO_INNER_ERR;
    CheckUpdateReadyRequest req = { { MESSAGE_MAGIC, 0, 0, mLocalNid.VNodeId(), getpid() } };
    CheckUpdateReadyResponse rsp;
    if (mMode == CONVERGENCE) {
        ret = checkUpdateReadyOp(&req, &rsp);
    } else {
        ret = SendCheckUpdateReadyRequestLocal(req, rsp);
    }
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send check update ready request failed, ret:" << ret << ".");
        return ret;
    }
    return rsp.flag ? BIO_OK : BIO_ERR;
}

BResult BioClientAgent::ListLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    if (mMode == CONVERGENCE) {
        return CallServerListIntf(req, objs);
    } else {
        return SendListRequestLocal(req, objs);
    }
}

BResult BioClientAgent::SendLoadRequestLocal(LoadRequest &req)
{
    BResult result = BIO_OK;
    auto ret = net::BioClientNet::Instance()->SendSync<LoadRequest, BResult>(INVALID_NID, BIO_OP_SDK_LOAD, req, result);
    if (ret != BIO_OK) {
        return ret;
    }
    return result;
}

BResult BioClientAgent::LoadLocal(LoadRequest &req)
{
    if (mMode == CONVERGENCE) {
        return loadOp(&req);
    } else {
        return SendLoadRequestLocal(req);
    }
}

BResult BioClientAgent::SendHbRequest(uint64_t &curNodeTimes, uint64_t &curPtTimes)
{
    HbRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() } };
    HbResponse rsp;
    auto ret =
        net::BioClientNet::Instance()->SendSync<HbRequest, HbResponse>(INVALID_NID, BIO_OP_SDK_REPORT_HB, req, rsp);
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