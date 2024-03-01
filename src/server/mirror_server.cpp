/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include "bio_log.h"
#include "bio_config_instance.h"
#include "bio_client.h"
#include "bio_server.h"
#include "bio_trace.h"
#include "message_op.h"
#include "mirror_server.h"

using namespace ock::bio;
using namespace ock::hcom;

bool MirrorServer::CheckAll(RequestComm &reqComm)
{
    if (UNLIKELY(reqComm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        return false;
    }
    uint64_t base = BioServer::Instance()->GetPtEntry(reqComm.ptId).version;
    if (UNLIKELY(reqComm.ptv != base)) {
        LOG_ERROR("Check message pt version failed, base:" << base << ", ptv:" << reqComm.ptv << ".");
        return false;
    }
    return true;
}

void MirrorServer::RegisterOpcode()
{
    auto netEngine = BioServer::Instance()->GetNetEngine();
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_NODE_INFO,
        std::bind(&MirrorServer::HandleQueryNodeInfo, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_NODE_VIEW,
        std::bind(&MirrorServer::HandleQueryNodeView, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_QUERY_PT_VIEW,
        std::bind(&MirrorServer::HandleQueryPtView, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_PUT,
        std::bind(&MirrorServer::HandlePut, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET,
        std::bind(&MirrorServer::HandleGet, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_DELETE,
        std::bind(&MirrorServer::HandleDelete, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_STAT,
        std::bind(&MirrorServer::HandleStat, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_LOAD,
        std::bind(&MirrorServer::HandleLoad, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_CREATE_FLOW,
        std::bind(&MirrorServer::HandleCreateFlow, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_SLICE,
        std::bind(&MirrorServer::HandleGetSlice, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SERVER_SYNC_DATA,
        std::bind(&MirrorServer::HandleSyncData, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SERVER_GET_EVICT_OFFSET,
        std::bind(&MirrorServer::HandleGetEvictOffset, this, std::placeholders::_1));
}

void MirrorServer::Reply(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize)
{
    NetServiceOpInfo opInfo{};
    opInfo.errorCode = static_cast<int16_t>(retCode);
    NetCallback *callback = NewCallback([](NetServiceContext &context) {}, std::placeholders::_1);
    int32_t ret;
    if (resp != nullptr) {
        ret = ctx.ReplySend(opInfo, { resp, respSize }, callback);
    } else {
        ret = ctx.ReplySend(opInfo, { &retCode, sizeof(retCode) }, callback);
    }
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Reply Send failed, ret:" << ret << ".");
    }
}

BResult MirrorServer::CreateFlow(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId)
{
    uint16_t diskId;
    auto ret = Cm::Instance()->GetLocalDiskId(ptId, diskId);
    ChkTrue(ret == BIO_OK, ret, "Get local disk fail:" << ret << ", ptId:" << ptId);

    ret = Cache::Instance().CreateWCache(procId, ptId, ptv, diskId, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create write cache failed, ret:" << ret << ", procId:" << procId << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = Cache::Instance().CreateRCache(ptId, diskId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create read cache failed, ret:" << ret << ", ptId:" << ptId << ".");
    } else {
        LOG_INFO("Create read cache success, procId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId);
    }

    return ret;
}

BResult MirrorServer::CreateFlowMaster(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t &flowId)
{
    auto ret = Cache::Instance().AllocateFlowId(procId, ptId, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Alloc flow id failed, ret:" << ret << ", procId:" << procId << ", ptId:" << ptId << ".");
        return ret;
    }
    ret = CreateFlow(procId, ptId, ptv, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    return ret;
}

BResult MirrorServer::CreateFlowSlave(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId)
{
    auto ret = CreateFlow(procId, ptId, ptv, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    return ret;
}

BResult MirrorServer::GetSlice(uint64_t flowId, uint64_t flowOffset, uint64_t flowIndex, uint64_t length,
    WCacheSlicePtr &slice)
{
    SliceKey sliceKey(flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    return Cache::Instance().GetWCacheSlice(sliceKey, slice);
}

BResult MirrorServer::Put(PutRequest &req, const WCacheSlicePtr &sliceP)
{
    std::string key(req.key);
    uint32_t dstNid = req.comm.srcNid;
    uint32_t localNid = BioServer::Instance()->GetLocalNid().VNodeId();
    uint32_t rMrKey = req.mrKey;
    std::vector<FlowAddr> rFlowAddr = sliceP->GetAddrs();
    LOG_INFO("Mirror server put, key:" << key << ", srcNid:" << dstNid << ", flowId:" << sliceP->GetFlowId() <<
        ", offsetInFlow:" << sliceP->GetOffsetInFlow() << ", indexInFlow:" << sliceP->GetIndexInFlow() << ", slice: " <<
        sliceP->ToString() << ", rFlowSize:" << rFlowAddr.size() << ".");

    auto reader = [dstNid, localNid, rMrKey](const SlicePtr &from, const SlicePtr &to) -> BResult {
        if (dstNid == localNid) {
            return BIO_OK;
        }

        BIO_TRACE_START(MIRROR_TRACE_PUT_READ_DATA);
        std::vector<FlowAddr> rFlowAddr = from->GetAddrs();
        std::vector<NetMrInfo> rMrVec;
        for (auto &addr : rFlowAddr) {
            MrInfo mr{};
            addr.ToMrInfo(mr);
            NetMrInfo bioMr(mr.address, mr.size, rMrKey);
            rMrVec.emplace_back(bioMr);
        }
        std::vector<FlowAddr> lFlowAddr = to->GetAddrs();
        std::vector<NetMrInfo> lMrVec;
        for (auto &addr : lFlowAddr) {
            MrInfo mr{};
            addr.ToMrInfo(mr);
            NetMrInfo bioMr(mr.address, mr.size, BioServer::Instance()->GetLocalMrKey());
            lMrVec.emplace_back(bioMr);
        }

        for (uint32_t idx = 0; idx < rMrVec.size(); idx++) {
            NetRequest readReq(lMrVec[idx].address, rMrVec[idx].address, lMrVec[idx].key, rMrVec[idx].key,
                lMrVec[idx].size);
            BResult ret = BioServer::Instance()->GetNetEngine()->SyncRead(static_cast<BioNodeId>(dstNid), readReq);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("Sync read failed, ret:" << ret << ", dstNid:" << dstNid << ".");
                BIO_TRACE_END(MIRROR_TRACE_PUT_READ_DATA, ret);
                return ret;
            }
        }
        BIO_TRACE_END(MIRROR_TRACE_PUT_READ_DATA, BIO_OK);
        return BIO_OK;
    };

    BIO_TRACE_START(MIRROR_TRACE_PUT);
    CacheAttr attr(req.tenantId, static_cast<AffinityStrategy>(req.affinity), static_cast<WriteStrategy>(req.strategy));
    BResult ret = Cache::Instance().Put(const_cast<char *>(key.c_str()), sliceP, reader, attr);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Put to write cache failed, ret:" << ret << ", key:" << req.key << ".");
    }
    BIO_TRACE_END(MIRROR_TRACE_PUT, ret);
    return ret;
}

BResult MirrorServer::Get(GetRequest &req, uint64_t &realLen)
{
    std::string key(req.key);
    uint32_t dstNid = req.comm.srcNid;
    uint32_t localNid = BioServer::Instance()->GetLocalNid().VNodeId();
    uint32_t rKey = req.mrKey;

    auto writer = [dstNid, localNid, rKey, key, this](const SlicePtr &from, const SlicePtr &to) -> BResult {
        BResult ret = BIO_ERR;
        if (dstNid == localNid) {
            std::vector<NetMrInfo> rMrVec;
            for (auto addr : to->GetAddrs()) {
                MrInfo mr{};
                addr.ToMrInfo(mr);
                rMrVec.emplace_back(mr.address, mr.size, rKey);
            }
            if (rMrVec.size() != 1) {
                LOG_ERROR("Remote slice num failed, num:" << rMrVec.size() << ".");
                return BIO_INNER_ERR;
            }
            BIO_TRACE_START(MIRROR_TRACE_GET_COPY_DATA);
            char *rAddr = reinterpret_cast<char *>(rMrVec[0].address);
            ret = mSliceOp.Copy(from, rAddr);
            BIO_TRACE_END(MIRROR_TRACE_GET_COPY_DATA, ret);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("Slice copy failed, ret:" << ret << ".");
            }
            return ret;
        }

        BIO_TRACE_START(MIRROR_TRACE_GET_WRITE_DATA);
        uint32_t totalLen = 0;
        std::vector<NetMrInfo> rMrVec;
        for (auto addr : to->GetAddrs()) {
            MrInfo mr{};
            addr.ToMrInfo(mr);
            rMrVec.emplace_back(mr.address, mr.size, rKey);
            totalLen += mr.size;
        }

        bool isAlloc = false;
        std::vector<NetMrInfo> lMrVec;
        if (from->GetFlowType() == FLOW_MEMORY) {
            for (auto addr : from->GetAddrs()) {
                MrInfo mr{};
                addr.ToMrInfo(mr);
                lMrVec.emplace_back(mr.address, mr.size, BioServer::Instance()->GetLocalMrKey());
            }
        } else {
            NetMrInfo bioMr;
            ret = BioServer::Instance()->MemAlloc(totalLen, bioMr);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("Alloc rdma memory failed, ret:" << ret << ", length:" << totalLen << ".");
                BIO_TRACE_END(MIRROR_TRACE_GET_WRITE_DATA, ret);
                return ret;
            }
            isAlloc = true;
            lMrVec.emplace_back(bioMr);
            ret = mSliceOp.Copy(from, reinterpret_cast<char *>(bioMr.address));
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("Slice copy failed, ret:" << ret << ", key:" << key.c_str());
                BioServer::Instance()->MemFree(bioMr.address);
                BIO_TRACE_END(MIRROR_TRACE_GET_WRITE_DATA, ret);
                return ret;
            }
        }

        for (uint32_t idx = 0; idx < lMrVec.size(); idx++) {
            uint32_t off = 0;
            NetRequest writeReq(lMrVec[idx].address, rMrVec[0].address + off, lMrVec[idx].key, rMrVec[idx].key,
                lMrVec[idx].size);
            ret = BioServer::Instance()->GetNetEngine()->SyncWrite(static_cast<BioNodeId>(dstNid), writeReq);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("Sync write failed, ret:" << ret << ", dstNid:" << dstNid << ".");
                break;
            }
            off += lMrVec[idx].size;
        }
        if (isAlloc) {
            for (auto mr : lMrVec) {
                BioServer::Instance()->MemFree(mr.address);
            }
        }
        BIO_TRACE_END(MIRROR_TRACE_GET_WRITE_DATA, ret);
        return ret;
    };

    MrInfo mrInfo = { req.address, static_cast<uint32_t>(req.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr sliceP = MakeRef<RCacheSlice>(req.ptId, req.length, addrVec);

    std::vector<FlowAddr> rFlowAddr = sliceP->GetAddrs();
    LOG_INFO("Mirror server get, key:" << key << ", srcNid:" << dstNid << ", offset:" << req.offset << ", length:" <<
        req.length << ", mr address:" << req.address << ", mr size:" << req.size << ", mr key:" << req.mrKey <<
        ", slice: " << sliceP->ToString() << ", rFlowSize:" << rFlowAddr.size() << ".");
    BIO_TRACE_START(MIRROR_TRACE_GET);
    BResult ret = Cache::Instance().Get(const_cast<char *>(key.c_str()), req.offset, sliceP, writer, realLen);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get from write cache failed, ret:" << ret << ", key:" << key << ".");
    }
    BIO_TRACE_END(MIRROR_TRACE_GET, ret);
    return ret;
}

BResult MirrorServer::Delete(DeleteRequest &req)
{
    std::string key(req.key);
    BIO_TRACE_START(MIRROR_TRACE_DEL);
    BResult ret = Cache::Instance().Delete(req.comm.ptId, const_cast<char *>(key.c_str()));
    BIO_TRACE_END(MIRROR_TRACE_DEL, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Delete key failed, ret:" << ret << ", key:" << key << ", ptId:" << req.comm.ptId << ".");
    } else {
        LOG_INFO("Mirror server delete success, key:" << key << ", ptId:" << req.comm.ptId << ".");
    }
    return ret;
}

BResult MirrorServer::Stat(StatRequest &req, Bio::ObjStat &objInfo)
{
    std::string key(req.key);
    CacheObjStat objStat{};
    BIO_TRACE_START(MIRROR_TRACE_STAT);
    BResult ret = Cache::Instance().Stat(req.comm.ptId, const_cast<char *>(key.c_str()), objStat);
    BIO_TRACE_END(MIRROR_TRACE_STAT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Stat key failed, ret:" << ret << ", key:" << key << ".");
    } else {
        objInfo.size = objStat.size;
        objInfo.time = objStat.time;
    }
    return ret;
}

BResult MirrorServer::Load(LoadRequest &req)
{
    std::string key(req.key);
    LOG_INFO("Mirror server load, key:" << key << ", ptId:" << req.comm.ptId << ".");
    BIO_TRACE_START(MIRROR_TRACE_LOAD);
    uint64_t realLen = 0;
    BResult ret =
        Cache::Instance().Load(req.comm.ptId, const_cast<char *>(key.c_str()), req.offset, req.length, realLen);
    BIO_TRACE_END(MIRROR_TRACE_LOAD, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Load key failed, ret:" << ret << ", key:" << key << ".");
    }
    return ret;
}

BResult MirrorServer::SyncData(SyncDataRequest &req)
{
    LOG_INFO("Master sync data, ptId:" << req.comm.ptId << ", version:" << req.comm.ptv);
    BIO_TRACE_START(MIRROR_TRACE_SYNC_DATA);
    BResult ret = Cache::Instance().Flush(req.comm.ptId, req.comm.ptv);
    BIO_TRACE_END(MIRROR_TRACE_SYNC_DATA, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Sync data failed:" << ret << ", ptId:" << req.comm.ptId << ", version:" << req.comm.ptv);
    }
    return ret;
}

BResult MirrorServer::SendSyncData(uint16_t ptId, uint16_t masterNodeId, uint64_t version)
{
    NetEnginePtr rpcEngine = BioServer::Instance()->GetNetEngine();
    uint16_t localNodeId = Cm::Instance()->GetCmLocalNodeId().nodeId;
    BResult rspRet;
    SyncDataRequest req = { { MESSAGE_MAGIC, ptId, version, localNodeId, getpid() } };
    auto ret = rpcEngine->SyncCall<SyncDataRequest, BResult>(static_cast<BioNodeId>(masterNodeId),
        BIO_OP_SERVER_SYNC_DATA, req, rspRet);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send sync sync data failed:" << ret << ", ptId:" << ptId << ", version:" << version);
        return ret;
    }
    return rspRet;
}

BResult MirrorServer::Initialize()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }
    RegisterOpcode();

    mStarted = true;
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryNodeInfo(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetLocalNidRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<GetLocalNidRequest *>(ctx.MessageData());
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    CmNodeId localNid = BioServer::Instance()->GetLocalNid();
    GetLocalNidResponse rsp = { localNid.GroupId(), localNid.VNodeId(), BioServer::Instance()->GetNetProtocol() };
    Reply(ctx, BIO_OK, &rsp, sizeof(GetLocalNidResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryNodeView(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(QueryNodeViewRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<QueryNodeViewRequest *>(ctx.MessageData());
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioServer::Instance()->GetNodeView();
    uint32_t size = nodeView.size();
    if (UNLIKELY(size > CLUSTER_NODE_MAX_SIZE)) {
        LOG_ERROR("Cluster node num  " << size << " exceeds 256.");
        return BIO_ERR;
    }

    QueryNodeViewResponse rsp;
    uint32_t index = 0;
    for (auto &nodeEntry : nodeView) {
        rsp.desc[index].groupId = nodeEntry.second.id.GroupId();
        rsp.desc[index].nodeId = nodeEntry.second.id.VNodeId();
        memcpy_s(rsp.desc[index].ip, IP_MAX_SIZE, nodeEntry.second.ip.c_str(), nodeEntry.second.ip.size());
        rsp.desc[index].port = nodeEntry.second.port;
        rsp.desc[index].status = static_cast<uint16_t>(nodeEntry.second.status);
        rsp.desc[index].num = nodeEntry.second.disks.size();
        for (uint32_t j = 0; j < nodeEntry.second.disks.size(); j++) {
            rsp.desc[index].diskDesc[j].diskId = nodeEntry.second.disks[j].diskId;
            rsp.desc[index].diskDesc[j].diskStatus = static_cast<uint16_t>(nodeEntry.second.disks[j].diskStatus);
        }
        index++;
    }
    rsp.num = index;

    Reply(ctx, BIO_OK, &rsp, sizeof(QueryNodeViewResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryPtView(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(QueryPtViewRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<QueryPtViewRequest *>(ctx.MessageData());
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    std::map<uint16_t, CmPtInfo> ptView = BioServer::Instance()->GetPtView();
    uint32_t size = ptView.size();
    if (UNLIKELY(size > PT_MAX_SIZE)) {
        LOG_ERROR("Pt view num  " << size << " exceeds 8192.");
        return -1;
    }

    QueryPtViewResponse rsp;
    uint32_t index = 0;
    for (auto &ptEntry : ptView) {
        rsp.desc[index].version = ptEntry.second.version;
        rsp.desc[index].ptId = ptEntry.second.ptId;
        rsp.desc[index].state = static_cast<uint16_t>(ptEntry.second.state);
        rsp.desc[index].masterNodeId = ptEntry.second.masterNodeId;
        rsp.desc[index].masterDiskId = ptEntry.second.masterDiskId;
        for (uint32_t j = 0; j < ptEntry.second.copys.size(); j++) {
            rsp.desc[index].copys[j].nodeId = ptEntry.second.copys[j].diskId;
            rsp.desc[index].copys[j].diskId = ptEntry.second.copys[j].diskId;
            rsp.desc[index].copys[j].state = static_cast<uint16_t>(ptEntry.second.copys[j].state);
        }
        index++;
    }
    rsp.num = index;

    Reply(ctx, BIO_OK, &rsp, sizeof(QueryPtViewResponse));
    return BIO_OK;
}

BResult MirrorServer::GetFlowGlobEvictOffset(uint16_t ptId, uint64_t flowId, bool &isMaster, uint64_t &flowOffset)
{
    auto ret = Cm::Instance()->CheckLocalRole(ptId, isMaster);
    ChkTrue(ret == BIO_OK, ret, "Get local role fail:" << ret << ", ptId:" << ptId);

    if (isMaster) {
        ret = Cache::Instance().GetEvictOffset(flowId, flowOffset);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Get evict offset failed:" << ret << ", ptId:" << ptId << ", flowId:" << flowId);
            return ret;
        }
        flowOffset += WRITE_CACHE_EVICT_STEP_SIZE;
        LOG_INFO("Master:get flow evict offset, ptId:" << ptId << ", flowId:" << flowId << ", flowOffset:" <<
            flowOffset);
        return BIO_OK;
    } else {
        ret = SendFlowGetEvictOffset(ptId, flowId, flowOffset);
        ChkTrue(ret == BIO_OK, ret, "Get local role fail:" << ret << ", ptId:" << ptId);
        LOG_INFO("Slave:get flow evict offset, ptId:" << ptId << ", flowId:" << flowId << ", flowOffset:" <<
            flowOffset);
        return BIO_OK;
    }
}

BResult MirrorServer::GetEvictOffset(GetEvictRequest &req, uint64_t &flowOffset)
{
    BIO_TRACE_START(MIRROR_TRACE_GET_EVICT_OFFSET);
    BResult ret = Cache::Instance().GetEvictOffset(req.flowId, flowOffset);
    BIO_TRACE_END(MIRROR_TRACE_GET_EVICT_OFFSET, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get evict offset failed:" << ret << ", ptId:" << req.comm.ptId << ", flowId:" << req.flowId);
        return ret;
    }
    LOG_INFO("Master get evict offset, ptId:" << req.comm.ptId << ", flowId:" << req.flowId << ", flowOffset:" <<
        flowOffset);
    return BIO_OK;
}

BResult MirrorServer::SendFlowGetEvictOffset(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset)
{
    NetEnginePtr rpcEngine = BioServer::Instance()->GetNetEngine();
    CmPtInfo cache;
    auto ret = Cm::Instance()->GetPtInfo(ptId, cache);
    if (ret != BIO_OK) {
        LOG_ERROR("Get ptInfo fail:" << ret << ", ptId:" << ptId);
        return ret;
    }

    uint16_t localNodeId = Cm::Instance()->GetCmLocalNodeId().nodeId;
    GetEvictRequest req = { { MESSAGE_MAGIC, ptId, cache.version, localNodeId, getpid() }, flowId };
    ret = rpcEngine->SyncCall<GetEvictRequest, uint64_t>(static_cast<BioNodeId>(cache.masterNodeId),
        BIO_OP_SERVER_GET_EVICT_OFFSET, req, flowOffset);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send get evict offset failed:" << ret << ", ptId:" << ptId << ", version:" << cache.version);
        return ret;
    }
    return ret;
}

int32_t MirrorServer::HandlePut(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() < sizeof(PutRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive put message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_PUT_HDL);
    auto *req = static_cast<PutRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_PUT_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }
    LOG_DEBUG("Put request, key:" << req->key << ", length:" << req->length << ", flowId:" << req->flowId <<
        ", offset:" << req->offset << ", index:" << req->index << ", sliceLen:" << req->sliceLen);

    WCacheSlicePtr sliceP = MakeRef<WCacheSlice>();
    char *sliceBuf = static_cast<char *>(static_cast<void *>(req)) + sizeof(PutRequest);
    sliceP->Deserialize(sliceBuf, req->sliceLen);
    BResult result = Put(*req, sliceP);

    Reply(ctx, BIO_OK, static_cast<void *>(&result), sizeof(BResult));
    BIO_TRACE_END(MIRROR_TRACE_PUT_HDL, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleGet(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive get message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_GET_HDL);
    auto *req = static_cast<GetRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_GET_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    uint64_t realLen;
    BResult result = Get(*req, realLen);
    if (result != BIO_OK) {
        Reply(ctx, result, nullptr, 0);
        return BIO_OK;
    }

    Reply(ctx, BIO_OK, static_cast<void *>(&realLen), sizeof(realLen));
    BIO_TRACE_END(MIRROR_TRACE_GET_HDL, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleDelete(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(DeleteRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive delete message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_DEL_HDL);
    auto *req = static_cast<DeleteRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_DEL_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    BResult result = Delete(*req);

    Reply(ctx, BIO_OK, static_cast<void *>(&result), sizeof(BResult));
    BIO_TRACE_END(MIRROR_TRACE_DEL_HDL, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleStat(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(StatRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive delete message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_STAT_HDL);
    auto *req = static_cast<StatRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_STAT_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    Bio::ObjStat objInfo = {};
    BResult ret = Stat(*req, objInfo);
    if (ret != BIO_OK) {
        Reply(ctx, ret, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_STAT_HDL, ret);
        return BIO_OK;
    }

    Reply(ctx, BIO_OK, static_cast<void *>(&objInfo), sizeof(Bio::ObjStat));
    BIO_TRACE_END(MIRROR_TRACE_STAT_HDL, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleLoad(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(LoadRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive load message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_LOAD_HDL);
    auto *req = static_cast<LoadRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_LOAD_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    BResult ret = Load(*req);
    Reply(ctx, BIO_OK, static_cast<void *>(&ret), sizeof(BResult));
    BIO_TRACE_END(MIRROR_TRACE_LOAD_HDL, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleCreateFlow(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(CreateFlowRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive create flow message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_CREATEFLOW_HDL);
    auto *req = static_cast<CreateFlowRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_CREATEFLOW_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    BResult result;
    uint64_t flowId = UINT64_MAX;
    if (req->opType == 0) {
        result = CreateFlowMaster(req->comm.pid, req->comm.ptId, req->comm.ptv, flowId);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Master create flow failed, ret:" << result << ", ptId:" << req->comm.ptId << ".");
            flowId = UINT64_MAX;
        }
    } else if (req->opType == 1) {
        result = CreateFlowSlave(req->comm.pid, req->comm.ptId, req->comm.ptv, req->flowId);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Slave create flow failed, ret:" << result << ", ptId:" << req->comm.ptId << ".");
            flowId = UINT64_MAX;
        } else {
            flowId = 0;
        }
    } else {
        LOG_ERROR("Invalid op type, opType:" << req->opType << ", ptId:" << req->comm.ptId << ".");
    }

    Reply(ctx, BIO_OK, static_cast<void *>(&flowId), sizeof(uint64_t));
    BIO_TRACE_END(MIRROR_TRACE_CREATEFLOW_HDL, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleGetSlice(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetSliceRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive create flow message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<GetSliceRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    WCacheSlicePtr sliceP = nullptr;
    BResult ret = GetSlice(req->flowId, req->flowOffset, req->flowIndex, req->length, sliceP);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get slice failed:" << ret << ".");
        return static_cast<int32_t>(ret);
    }
    uint32_t sliceLen = sliceP->GetSerializeLen();
    auto *tmp = new (std::nothrow) uint8_t[sizeof(GetSliceResponse) + sliceLen];
    if (UNLIKELY(tmp == nullptr)) {
        LOG_ERROR("Alloc memory failed, len:" << sizeof(GetSliceResponse) + sliceLen << ".");
        return static_cast<int32_t>(BIO_INNER_ERR);
    }
    GetSliceResponse *rsp = static_cast<GetSliceResponse *>(static_cast<void *>(tmp));

    std::vector<FlowAddr> addrVec = sliceP->GetAddrs();
    if (addrVec.size() > SLICE_ADDR_MAX_SIZE) {
        LOG_ERROR("Slice addr num " << addrVec.size() << " exceed 32.");
        return static_cast<int32_t>(BIO_INNER_ERR);
    }
    rsp->addrNum = addrVec.size();
    for (uint32_t i = 0; i < addrVec.size(); i++) {
        rsp->addr[i].chunkId = addrVec[i].chunkId;
        rsp->addr[i].chunkOffset = addrVec[i].chunkOffset;
        rsp->addr[i].chunkLen = addrVec[i].chunkLen;
    }
    char *sliceBuf = static_cast<char *>(static_cast<void *>(tmp)) + sizeof(GetSliceResponse);
    sliceP->Serialize(sliceBuf, sliceLen);

    Reply(ctx, BIO_OK, rsp, sizeof(GetSliceResponse) + sliceLen);
    delete[] tmp;
    return BIO_OK;
}

int32_t MirrorServer::HandleSyncData(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(SyncDataRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_SYNC_DATA_HDL);
    auto *req = static_cast<SyncDataRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_SYNC_DATA_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    BResult ret = SyncData(*req);
    Reply(ctx, BIO_OK, static_cast<void *>(&ret), sizeof(BResult));
    BIO_TRACE_END(MIRROR_TRACE_SYNC_DATA_HDL, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleGetEvictOffset(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetEvictRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_GET_EVICT_OFFSET_HDL);
    auto *req = static_cast<GetEvictRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_GET_EVICT_OFFSET_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    uint64_t flowOffset;
    BResult ret = GetEvictOffset(*req, flowOffset);
    if (ret != BIO_OK) {
        Reply(ctx, ret, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_GET_EVICT_OFFSET_HDL, ret);
        return BIO_OK;
    }

    Reply(ctx, BIO_OK, static_cast<void *>(&flowOffset), sizeof(uint64_t));
    BIO_TRACE_END(MIRROR_TRACE_GET_EVICT_OFFSET_HDL, 0);
    return BIO_OK;
}