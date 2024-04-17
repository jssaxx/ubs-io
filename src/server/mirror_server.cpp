/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include "bio_log.h"
#include "bio_config_instance.h"
#include "bio_client.h"
#include "bio_server.h"
#include "bio_trace.h"
#include "bio_functions.h"
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
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_SHM_INIT,
        std::bind(&MirrorServer::HandleShmInit, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_NODE_INFO,
        std::bind(&MirrorServer::HandleQueryNodeInfo, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_NODE_INFO_BY_PT,
        std::bind(&MirrorServer::HandleQueryNodeInfoByPt, this, std::placeholders::_1));
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
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_LIST,
        std::bind(&MirrorServer::HandleList, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_LOAD,
        std::bind(&MirrorServer::HandleLoad, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_CREATE_FLOW,
        std::bind(&MirrorServer::HandleCreateFlow, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_DESTROY_FLOW,
        std::bind(&MirrorServer::HandleDestroyFlow, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_SLICE,
        std::bind(&MirrorServer::HandleGetSlice, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_REPORT_HB,
        std::bind(&MirrorServer::HandleReportHb, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SERVER_SYNC_DATA,
        std::bind(&MirrorServer::HandleSyncData, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SERVER_GET_EVICT_OFFSET,
        std::bind(&MirrorServer::HandleGetEvictOffset, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_FREE_MEM,
        std::bind(&MirrorServer::HandleFreeMem, this, std::placeholders::_1));
}

void MirrorServer::ReplyDone(uint32_t opcode, int32_t ret, uint64_t ts)
{
    if (opcode == BIO_OP_SDK_PUT) {
        BIO_TRACE_ASYNC_END(MIRROR_TRACE_PUT_REPLY, ret, ts);
    }
    BIO_TRACE_ASYNC_END(NET_TRACE_REPLY_ASYNC, ret, ts);
}

void MirrorServer::Reply(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize, uint32_t opcode)
{
    uint64_t ts = Monotonic::TimeNs();
    NetServiceOpInfo opInfo{};
    opInfo.errorCode = static_cast<int16_t>(retCode);
    NetCallback *callback = NewCallback([this, ts, opcode](NetServiceContext &context) {
        ReplyDone(opcode, context.Result(), ts);
        }, std::placeholders::_1);

    if (opcode == BIO_OP_SDK_PUT) {
        BIO_TRACE_ASYNC_BEGIN(MIRROR_TRACE_PUT_REPLY);
    }

    int32_t ret;
    BIO_TRACE_ASYNC_BEGIN(NET_TRACE_REPLY_ASYNC);
    BIO_TRACE_START(NET_TRACE_REPLY_SYNC);
    if (resp != nullptr) {
        ret = ctx.ReplySend(opInfo, { resp, respSize }, callback);
    } else {
        ret = ctx.ReplySend(opInfo, { &retCode, sizeof(retCode) }, callback);
    }
    BIO_TRACE_END(NET_TRACE_REPLY_SYNC, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Reply Send failed, ret:" << ret << ".");
    }
}

void MirrorServer::ReplyListResultLocal(ServiceContext &ctx, std::unordered_map<std::string, ObjStat> &objs)
{
    uintptr_t address = 0;
    if (objs.size() != 0) {
        uint32_t key;
        auto ret = BioServer::Instance()->GetNetEngine()->AllocLocalMrSingle(address, key);
        if (UNLIKELY(ret != BIO_OK)) {
            Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
            return;
        }

        auto addrP = reinterpret_cast<ObjStat *>(address);
        uint32_t index = 0;
        for (auto &obj : objs) {
            CopyKey(addrP[index].key, obj.second.key, KEY_MAX_SIZE);
            addrP[index].size = obj.second.size;
            addrP[index].time = obj.second.time;
            index++;
        }
    }

    ListResponse rsp;
    rsp.addr = address;
    rsp.addrOffset =
        (address != 0) ? BioServer::Instance()->GetNetEngine()->GetAddressOffset(static_cast<uint64_t>(address)) : 0;
    rsp.num = objs.size();
    rsp.buffLen = 0;
    Reply(ctx, BIO_OK, static_cast<void *>(&rsp), sizeof(ListResponse));
}

void MirrorServer::ReplyListResultRemote(ServiceContext &ctx, ListRequest *req,
    std::unordered_map<std::string, ObjStat> &objs)
{
    uintptr_t lAddress = 0;
    if (objs.size() != 0) {
        uint32_t lKey;
        auto ret = BioServer::Instance()->GetNetEngine()->AllocLocalMrSingle(lAddress, lKey);
        if (UNLIKELY(ret != BIO_OK)) {
            Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
            return;
        }

        auto addrP = reinterpret_cast<ObjStat *>(lAddress);
        uint32_t index = 0;
        for (auto &obj : objs) {
            CopyKey(addrP[index].key, obj.second.key, KEY_MAX_SIZE);
            addrP[index].size = obj.second.size;
            addrP[index].time = obj.second.time;
            index++;
        }

        NetRequest writeReq(lAddress, req->address, lKey, req->mrKey, (sizeof(ObjStat) * objs.size()));
        ret = BioServer::Instance()->GetNetEngine()->SyncWrite(ctx.Channel(), writeReq);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Sync write failed, ret:" << ret << ".");
            Reply(ctx, BIO_INNER_ERR, nullptr, 0);
            return;
        }
    }

    ListResponse rsp;
    rsp.addr = 0;
    rsp.addrOffset = 0;
    rsp.num = objs.size();
    rsp.buffLen = 0;
    BioServer::Instance()->GetNetEngine()->FreeLocalMrSingle(lAddress);
    Reply(ctx, BIO_OK, static_cast<void *>(&rsp), sizeof(ListResponse));
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

    ret = Cache::Instance().CreateRCache(ptId, ptv, diskId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create read cache failed, ret:" << ret << ", ptId:" << ptId << ".");
    } else {
        LOG_INFO("Create read cache success, procId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId);
    }

    return ret;
}

BResult MirrorServer::CreateFlowMaster(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t &flowId)
{
    auto ret = Cache::Instance().AllocateFlowId(procId, ptId, ptv, flowId);
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

BResult MirrorServer::DestroyFlow(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId)
{
    auto ret = Cache::Instance().DestroyWCache(procId, ptId, ptv, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Destroy write cache failed, ret:" << ret << ", procId:" << procId << ", ptId:" << ptId << ".");
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

void MirrorServer::QueryNodeView(QueryNodeViewRequest &req, QueryNodeViewResponse &rsp)
{
    uint32_t bar = req.bar;
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioServer::Instance()->GetNodeView(&rsp.curNodeTimes);
    uint32_t index = 0;
    for (auto &nodeEntry : nodeView) {
        if ((bar--) != 0) { // Skip the obtained node
            continue;
        }
        if (index == CLUSTER_NODE_SIZE) {
            break;
        }
        rsp.desc[index].groupId = nodeEntry.second.id.GroupId();
        rsp.desc[index].nodeId = nodeEntry.second.id.VNodeId();
        strncpy_s(rsp.desc[index].ip, IP_MAX_SIZE, nodeEntry.second.ip.c_str(), nodeEntry.second.ip.size());
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
    rsp.flag = (index == 0) ? 0 : 1;
}

void MirrorServer::QueryPtView(QueryPtViewRequest &req, QueryPtViewResponse &rsp)
{
    uint32_t bar = req.bar;
    std::map<uint16_t, CmPtInfo> ptView = BioServer::Instance()->GetPtView(&rsp.curPtTimes);
    uint32_t index = 0;
    for (auto &ptEntry : ptView) {
        if (bar != 0) { // Skip the obtained pt
            bar--;
            continue;
        }
        if (index == PT_SIZE) {
            break;
        }
        rsp.desc[index].version = ptEntry.second.version;
        rsp.desc[index].ptId = ptEntry.second.ptId;
        rsp.desc[index].state = static_cast<uint16_t>(ptEntry.second.state);
        rsp.desc[index].masterNodeId = ptEntry.second.masterNodeId;
        rsp.desc[index].masterDiskId = ptEntry.second.masterDiskId;
        for (uint32_t j = 0; j < ptEntry.second.copys.size(); j++) {
            rsp.desc[index].copys[j].nodeId = ptEntry.second.copys[j].nodeId;
            rsp.desc[index].copys[j].diskId = ptEntry.second.copys[j].diskId;
            rsp.desc[index].copys[j].state = static_cast<uint16_t>(ptEntry.second.copys[j].state);
        }
        index++;
    }
    rsp.num = index;
    rsp.flag = (index == 0) ? 0 : 1;
}

BResult MirrorServer::ReaderLocal(const SlicePtr &from, const SlicePtr &to)
{
    return BIO_OK;
}

BResult MirrorServer::ReaderRemoteEquals(PutRequest &req, std::vector<NetMrInfo> &lMrVec,
    std::vector<NetMrInfo> &rMrVec, ServiceContext &netCtx)
{
    BResult ret = BIO_OK;
    for (uint32_t idx = 0; idx < lMrVec.size(); idx++) {
        NetRequest wReq(lMrVec[idx].address, rMrVec[idx].address, lMrVec[idx].key, rMrVec[idx].key, lMrVec[idx].size);
        LOG_INFO("Sync read start, lMrAddr:" << lMrVec[idx].address << ", rMrAddr:" << rMrVec[idx].address <<
            ", lKey:" << lMrVec[idx].key << ", rKey:" << rMrVec[idx].key << ", size:" << lMrVec[idx].size << ".");
        if (req.memFromServer) {
            ret = BioServer::Instance()->GetNetEngine()->SyncRead(req.comm.srcNid, wReq);
        } else {
            ret = BioServer::Instance()->GetNetEngine()->SyncRead(netCtx.Channel(), wReq);
        }
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("One side read failed, ret:" << ret << ", idx:" << idx << ", lAddr:" << lMrVec[idx].address <<
                ", lKey:" << lMrVec[idx].key << ", rAddr:" << rMrVec[idx].address << ", rKey:" << rMrVec[idx].key <<
                ", size:" << lMrVec[idx].size << ".");
            break;
        }
    }
    return ret;
}

BResult MirrorServer::ReaderRemoteNotEquals(PutRequest &req, std::vector<NetMrInfo> &lMrVec,
    std::vector<NetMrInfo> &rMrVec, ServiceContext &netCtx)
{
    ChkTrue(rMrVec.size() == 1, BIO_INNER_ERR, "Slice addr num not match, rAddrNum:" << rMrVec.size() << ".");
    BResult ret = BIO_OK;
    uintptr_t rMrAddr = rMrVec[0].address;
    uint32_t rMrKey = rMrVec[0].key;
    uint64_t off = 0;
    for (uint32_t idx = 0; idx < lMrVec.size(); idx++) {
        rMrAddr += off;
        NetRequest wReq(lMrVec[idx].address, rMrAddr, lMrVec[idx].key, rMrKey, lMrVec[idx].size);
        LOG_INFO("Sync read start, lMrAddr:" << lMrVec[idx].address << ", rMrAddr:" << rMrAddr << ", lKey:" <<
            lMrVec[idx].key << ", rKey:" << rMrKey << ", size:" << lMrVec[idx].size << ".");
        if (req.memFromServer) {
            ret = BioServer::Instance()->GetNetEngine()->SyncRead(req.comm.srcNid, wReq);
        } else {
            ret = BioServer::Instance()->GetNetEngine()->SyncRead(netCtx.Channel(), wReq);
        }
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("One side read failed, ret:" << ret << ", idx:" << idx << ", lAddr:" << lMrVec[idx].address <<
                ", lKey:" << lMrVec[idx].key << ", rAddr:" << rMrAddr << ", rKey:" << rMrKey << ", size:" <<
                lMrVec[idx].size << ".");
            break;
        }
        off += lMrVec[idx].size;
    }
    return ret;
}

BResult MirrorServer::ReaderRemote(const SlicePtr &from, const SlicePtr &to, PutRequest &req, ServiceContext &netCtx)
{
    // 1. parse remote mr info
    BIO_TRACE_START(MIRROR_TRACE_PUT_READ_DATA);
    std::vector<FlowAddr> rFlowAddr = from->GetAddrs();
    std::vector<NetMrInfo> rMrVec;
    for (auto &addr : rFlowAddr) {
        MrInfo mr;
        addr.ToMrInfo(mr);
        rMrVec.emplace_back(NetMrInfo(mr.address, mr.size, req.mrKey));
    }

    // 2. parse local mr info
    std::vector<FlowAddr> lFlowAddr = to->GetAddrs();
    std::vector<NetMrInfo> lMrVec;
    for (auto &addr : lFlowAddr) {
        MrInfo mr;
        addr.ToMrInfo(mr);
        lMrVec.emplace_back(NetMrInfo(mr.address, mr.size, BioServer::Instance()->GetLocalMrKey()));
    }

    // 3. one side read
    BResult ret = BIO_OK;
    if (lMrVec.size() > rMrVec.size()) {
        ret = ReaderRemoteNotEquals(req, lMrVec, rMrVec, netCtx);
    } else if (lMrVec.size() == rMrVec.size()) {
        ret = ReaderRemoteEquals(req, lMrVec, rMrVec, netCtx);
    } else {
        LOG_ERROR("Slice addr num not match, lAddrNum:" << lMrVec.size() << ", rAddrNum:" << rMrVec.size() << ".");
        ret = BIO_INNER_ERR;
    }
    BIO_TRACE_END(MIRROR_TRACE_PUT_READ_DATA, ret);
    return ret;
}

BResult MirrorServer::Put(PutRequest &req, const WCacheSlicePtr &sliceP, ServiceContext &netCtx)
{
    LOG_INFO("Mirror server put, key:" << req.key << ", srcNid:" << req.comm.srcNid << ", flowId:" <<
        sliceP->GetFlowId() << ", offsetInFlow:" << sliceP->GetOffsetInFlow() << ", indexInFlow:" <<
        sliceP->GetIndexInFlow() << ", slice: " << sliceP->ToString() << ", rFlowSize:" << sliceP->GetAddrs().size() <<
        ".");

    auto reader = [&req, &netCtx, this](const SlicePtr &from, const SlicePtr &to) -> BResult {
        if (req.comm.srcNid == BioServer::Instance()->GetLocalNid().VNodeId()) {
            return ReaderLocal(from, to);
        } else {
            return ReaderRemote(from, to, req, netCtx);
        }
    };

    BIO_TRACE_START(MIRROR_TRACE_PUT);
    CacheAttr attr(req.copyFree, req.tenantId, static_cast<AffinityStrategy>(req.affinity),
        static_cast<WriteStrategy>(req.strategy));
    BResult ret = Cache::Instance().Put(req.key, sliceP, reader, attr);
    BIO_TRACE_END(MIRROR_TRACE_PUT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Put to write cache failed, ret:" << ret << ", key:" << req.key << ".");
    }
    return ret;
}

void MirrorServer::InitGetResponse(GetResponse &rsp)
{
    rsp.isAlloc = false;
    rsp.num = 0;
    for (uint32_t idx = 0; idx < SLICE_ADDR_SIZE; idx++) {
        rsp.address[idx] = 0;
        rsp.addrOffset[idx] = 0;
        rsp.addrLen[idx] = 0;
    }
    rsp.realLen = 0;
}

BResult MirrorServer::WriterLocalSameProcess(const SlicePtr &from, const SlicePtr &to, uint32_t rKey)
{
    std::vector<NetMrInfo> rMrVec;
    for (auto addr : to->GetAddrs()) {
        MrInfo mr{};
        addr.ToMrInfo(mr);
        rMrVec.emplace_back(NetMrInfo(mr.address, mr.size, rKey));
    }
    ChkTrue(rMrVec.size() == 1, BIO_INNER_ERR, "Remote addr size not equal to 1, size:" << rMrVec.size() << ".");

    BIO_TRACE_START(MIRROR_TRACE_GET_COPY_DATA);
    BResult ret = mSliceOp.Copy(from, reinterpret_cast<char *>(rMrVec[0].address));
    BIO_TRACE_END(MIRROR_TRACE_GET_COPY_DATA, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Slice copy failed, ret:" << ret << ".");
    }
    return ret;
}

BResult MirrorServer::WriterParseMrInfo(const SlicePtr &from, const SlicePtr &to, std::vector<NetMrInfo> &rMrVec,
    std::vector<NetMrInfo> &lMrVec, uint32_t rKey, bool &isAlloc)
{
    // 1. parse remote mr info
    uint32_t totalLen = 0;
    for (auto addr : to->GetAddrs()) {
        MrInfo mr;
        addr.ToMrInfo(mr);
        rMrVec.emplace_back(NetMrInfo(mr.address, mr.size, rKey));
        totalLen += mr.size;
    }

    // 2. parse local mr info
    if (from->GetFlowType() == FLOW_MEMORY) {
        for (auto addr : from->GetAddrs()) {
            MrInfo mr;
            addr.ToMrInfo(mr);
            lMrVec.emplace_back(NetMrInfo(mr.address, mr.size, BioServer::Instance()->GetLocalMrKey()));
        }
    } else {
        NetMrInfo bioMr;
        BResult ret = BioServer::Instance()->MemAlloc(totalLen, bioMr);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Alloc rdma memory failed, ret:" << ret << ", length:" << totalLen << ".");
            return ret;
        }
        ret = mSliceOp.Copy(from, reinterpret_cast<char *>(bioMr.address));
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Slice copy failed, ret:" << ret << ".");
            BioServer::Instance()->MemFree(bioMr.address);
            return ret;
        }
        isAlloc = true;
        lMrVec.emplace_back(bioMr);
    }
    return BIO_OK;
}

BResult MirrorServer::WriterLocalDiffProcess(bool &isAlloc, std::vector<NetMrInfo> &lMrVec, GetResponse &rsp)
{
    ChkTrue(lMrVec.size() <= SLICE_ADDR_SIZE, BIO_INNER_ERR, "Local mr size exceed 4, size:" << lMrVec.size() << ".");
    rsp.isAlloc = isAlloc;
    rsp.num = lMrVec.size();
    for (uint32_t idx = 0; idx < lMrVec.size(); idx++) {
        rsp.address[idx] = isAlloc ? lMrVec[idx].address : 0;
        rsp.addrOffset[idx] =
            BioServer::Instance()->GetNetEngine()->GetAddressOffset(static_cast<uint64_t>(lMrVec[idx].address));
        rsp.addrLen[idx] = lMrVec[idx].size;
    }
    return BIO_OK;
}

BResult MirrorServer::WriterRemote(bool isAlloc, std::vector<NetMrInfo> &lMrVec, std::vector<NetMrInfo> &rMrVec,
    ServiceContext &netCtx, GetRequest &req)
{
    ChkTrue(rMrVec.size() == 1, BIO_INNER_ERR, "Remote addr size not equal to 1, size:" << rMrVec.size() << ".");
    BIO_TRACE_START(MIRROR_TRACE_GET_WRITE_DATA);
    uint32_t off = 0;
    BResult ret = BIO_OK;
    for (uint32_t idx = 0; idx < lMrVec.size(); idx++) {
        NetRequest rReq(lMrVec[idx].address, rMrVec[0].address + off, lMrVec[idx].key, rMrVec[0].key, lMrVec[idx].size);
        uint32_t dstPid = req.isConvDeploy ? 0 : req.comm.pid; // 融合部署场景目的端PID填充0
        ret = BioServer::Instance()->GetNetEngine()->SyncWrite(req.comm.srcNid, dstPid, rReq);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Sync write failed, ret:" << ret << ", index:" << idx << ", lAddr:" << lMrVec[idx].address <<
                ", lKey:" << lMrVec[idx].key << ", rAddr:" << rMrVec[0].address + off << ", rKey:" << rMrVec[0].key <<
                ", size:" << lMrVec[idx].size << ".");
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
}

BResult MirrorServer::Get(GetRequest &req, GetResponse &rsp, ServiceContext &netCtx)
{
    InitGetResponse(rsp);
    MrInfo mrInfo = { req.address, static_cast<uint32_t>(req.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr sliceP = MakeRef<RCacheSlice>(req.ptId, req.length, addrVec);

    LOG_INFO("Mirror server get, key:" << req.key << ", srcNid:" << req.comm.srcNid << ", offset:" << req.offset <<
        ", length:" << req.length << ", mr address:" << req.address << ", mr size:" << req.size << ", mr key:" <<
        req.mrKey << ", slice: " << sliceP->ToString() << ", rFlowSize:" << sliceP->GetAddrs().size() << ".");

    auto writer = [&req, &rsp, &netCtx, this](const SlicePtr &from, const SlicePtr &to) -> BResult {
        if ((req.comm.srcNid == BioServer::Instance()->GetLocalNid().VNodeId()) && (req.comm.pid == getpid())) {
            return WriterLocalSameProcess(from, to, req.mrKey);
        }

        bool isAlloc = false;
        std::vector<NetMrInfo> rMrVec;
        std::vector<NetMrInfo> lMrVec;
        BResult ret = WriterParseMrInfo(from, to, rMrVec, lMrVec, req.mrKey, isAlloc);
        if (ret != BIO_OK) {
            return ret;
        }

        if ((req.comm.srcNid == BioServer::Instance()->GetLocalNid().VNodeId()) && (req.comm.pid != getpid())) {
            return WriterLocalDiffProcess(isAlloc, lMrVec, rsp);
        }

        return WriterRemote(isAlloc, lMrVec, rMrVec, netCtx, req);
    };

    BIO_TRACE_START(MIRROR_TRACE_GET);
    BResult ret = Cache::Instance().Get(req.key, req.offset, sliceP, writer, rsp.realLen);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get from write cache failed, ret:" << ret << ", key:" << req.key << ".");
    }
    BIO_TRACE_END(MIRROR_TRACE_GET, ret);
    return ret;
}

BResult MirrorServer::Delete(DeleteRequest &req)
{
    BIO_TRACE_START(MIRROR_TRACE_DEL);
    BResult ret = Cache::Instance().Delete(req.comm.ptId, req.key);
    BIO_TRACE_END(MIRROR_TRACE_DEL, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Delete key failed, ret:" << ret << ", key:" << req.key << ", ptId:" << req.comm.ptId << ".");
    } else {
        LOG_INFO("Mirror server delete success, key:" << req.key << ", ptId:" << req.comm.ptId << ".");
    }
    return ret;
}

BResult MirrorServer::List(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    std::unordered_map<std::string, CacheObjStat> cacheStatInfo;
    BResult ret = Cache::Instance().List(req.prefix, req.comm.ptId, req.isListUnderFs, cacheStatInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("List key failed, ret:" << ret << ", prefix:" << req.prefix << ".");
    } else {
        for (auto &info : cacheStatInfo) {
            if (objs.size() >= 1000U) {
                return BIO_OK;
            }
            ObjStat stat;
            CopyKey(stat.key, info.first.c_str(), MAX_KEY_SIZE);
            stat.size = info.second.size;
            stat.time = info.second.time;
            objs.insert({ info.first, stat });
        }
        LOG_INFO("Mirror server List success, prefix:" << req.prefix << ", ptId:" << req.comm.ptId << ", num:" <<
            objs.size() << ".");
    }
    return ret;
}

BResult MirrorServer::Stat(StatRequest &req, ObjStat &objInfo)
{
    std::string key(req.key);
    CacheObjStat objStat;
    BIO_TRACE_START(MIRROR_TRACE_STAT);
    BResult ret = Cache::Instance().Stat(req.comm.ptId, const_cast<char *>(key.c_str()), objStat);
    BIO_TRACE_END(MIRROR_TRACE_STAT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Stat key failed, ret:" << ret << ", key:" << key << ".");
    } else {
        CopyKey(objInfo.key, req.key, KEY_MAX_SIZE);
        objInfo.size = objStat.size;
        objInfo.time = objStat.time;
    }
    return ret;
}

BResult MirrorServer::Load(LoadRequest &req)
{
    BIO_TRACE_START(MIRROR_TRACE_LOAD);
    uint64_t realLen = 0;
    BResult ret = Cache::Instance().Load(req.comm.ptId, req.key, req.offset, req.length, realLen);
    BIO_TRACE_END(MIRROR_TRACE_LOAD, ret);
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

int32_t MirrorServer::HandleShmInit(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(ShmInitRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive shm init message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<ShmInitRequest *>(ctx.MessageData());
    ShmInitResponse rsp{};
    rsp.serverPid = getpid();
    BioServer::Instance()->GetNetEngine()->QueryShmInfo(rsp.memFd, rsp.offset, rsp.length, rsp.mKey);
    auto ret = BioServer::Instance()->GetNetEngine()->SendFds(ctx.Channel(), &rsp.memFd, NO_1);
    if (ret != BIO_OK) {
        LOG_ERROR("Send fds failed, ret:" << ret << ".");
        Reply(ctx, BIO_ERR, nullptr, 0);
    } else {
        Reply(ctx, BIO_OK, &rsp, sizeof(ShmInitResponse));
    }
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryNodeInfoByPt(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(FileLocationQueryReq)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<FileLocationQueryReq *>(ctx.MessageData());
    CmNodeInfo nodeInfo;
    CmNodeId id;
    id.nodeId = req->masterPtId;
    id.groupId = 0;
    auto ret = BioServer::Instance()->GetNodeInfo(id, nodeInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    FileLocationQueryRsp rsp;
    memcpy_s(rsp.hostMaster, NO_16, nodeInfo.ip.c_str(), nodeInfo.ip.length());
    rsp.portMaster = nodeInfo.port;

    id.nodeId = req->slavePtId;
    id.groupId = 0;
    ret = BioServer::Instance()->GetNodeInfo(id, nodeInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    memcpy_s(rsp.hostSlave, NO_16, nodeInfo.ip.c_str(), nodeInfo.ip.length());
    rsp.portSlave = nodeInfo.port;

    Reply(ctx, BIO_OK, &rsp, sizeof(FileLocationQueryRsp));
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

    QueryNodeViewResponse rsp;
    QueryNodeView(*req, rsp);
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

    QueryPtViewResponse rsp;
    QueryPtView(*req, rsp);
    Reply(ctx, BIO_OK, &rsp, sizeof(QueryPtViewResponse));
    return BIO_OK;
}

BResult MirrorServer::GetFlowGlobEvictOffset(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset)
{
    auto ret = SendFlowGetEvictOffset(ptId, flowId, flowOffset);
    ChkTrue(ret == BIO_OK, ret, "Get local role fail:" << ret << ", ptId:" << ptId);
    LOG_INFO("Slave:get flow evict offset, ptId:" << ptId << ", flowId:" << flowId << ", flowOffset:" << flowOffset);
    return BIO_OK;
}

BResult MirrorServer::GetEvictOffset(GetEvictRequest &req, uint64_t &flowOffset)
{
    BIO_TRACE_START(MIRROR_TRACE_GET_EVICT_OFFSET);
    BResult ret = Cache::Instance().GetEvictOffset(req.flowId, flowOffset);
    BIO_TRACE_END(MIRROR_TRACE_GET_EVICT_OFFSET, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_WARN("Get evict offset failed:" << ret << ", ptId:" << req.comm.ptId << ", flowId:" << req.flowId);
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
        Reply(ctx, BIO_NET_RETRY, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<PutRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    WCacheSlicePtr sliceP = nullptr;
    if (req->sliceLen == 0) {
        MrInfo mrInfo = { req->mrAddress, static_cast<uint32_t>(req->mrSize) };
        std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
        sliceP = MakeRef<WCacheSlice>(req->flowId, req->flowOffset, req->flowIndex, req->length, addrVec);
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
            return BIO_OK;
        }
    } else {
        sliceP = MakeRef<WCacheSlice>();
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
            return BIO_OK;
        }
        sliceP->Deserialize(req->sliceBuf, req->sliceLen);
    }

    BIO_TRACE_START(MIRROR_TRACE_PUT_RECEIVE_REMOTE);
    BResult result = Put(*req, sliceP, ctx);
    BIO_TRACE_END(MIRROR_TRACE_PUT_RECEIVE_REMOTE, result);
    Reply(ctx, result, nullptr, 0, BIO_OP_SDK_PUT);
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
    auto req = static_cast<GetRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_GET_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    GetResponse rsp;
    BResult result = Get(*req, rsp, ctx);
    if (result != BIO_OK) {
        Reply(ctx, result, nullptr, 0);
        return BIO_OK;
    }

    Reply(ctx, BIO_OK, static_cast<void *>(&rsp), sizeof(GetResponse));
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

    auto req = static_cast<DeleteRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    BIO_TRACE_START(MIRROR_TRACE_DEL_HDL);
    BResult result = Delete(*req);
    BIO_TRACE_END(MIRROR_TRACE_DEL_HDL, result);
    Reply(ctx, BIO_OK, static_cast<void *>(&result), sizeof(BResult));
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

    ObjStat objInfo;
    BResult ret = Stat(*req, objInfo);
    if (ret != BIO_OK) {
        Reply(ctx, ret, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_STAT_HDL, ret);
        return BIO_OK;
    }

    Reply(ctx, BIO_OK, static_cast<void *>(&objInfo), sizeof(ObjStat));
    BIO_TRACE_END(MIRROR_TRACE_STAT_HDL, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleList(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(ListRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive load message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<ListRequest *>(ctx.MessageData());
    std::unordered_map<std::string, ObjStat> objs;
    BResult ret = List(*req, objs);
    if (ret != BIO_OK) {
        Reply(ctx, ret, nullptr, 0);
        return BIO_OK;
    }

    if (static_cast<uint16_t>(req->comm.srcNid) == BioServer::Instance()->GetLocalNid().VNodeId()) {
        ReplyListResultLocal(ctx, objs);
    } else {
        ReplyListResultRemote(ctx, req, objs);
    }
    return BIO_OK;
}

int32_t MirrorServer::HandleLoad(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(LoadRequest) || ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive load message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<LoadRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    BIO_TRACE_START(MIRROR_TRACE_LOAD_HDL);
    BResult ret = Load(*req);
    BIO_TRACE_END(MIRROR_TRACE_LOAD_HDL, ret);
    Reply(ctx, ret, nullptr, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleReportHb(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(HbRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive hb message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    HbResponse rsp;
    auto ret = BioServer::Instance()->GetHbInfo(&rsp.curNodeTimes, &rsp.curPtTimes);
    if (ret != BIO_OK) {
        LOG_ERROR("Get hb info fail:" << ret);
        Reply(ctx, ret, nullptr, 0);
        return ret;
    }

    Reply(ctx, BIO_OK, &rsp, sizeof(HbResponse));
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

int32_t MirrorServer::HandleDestroyFlow(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(DestroyFlowRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive destroy flow message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_DESTROYFLOW_HDL);
    auto *req = static_cast<DestroyFlowRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_DESTROYFLOW_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    auto result = DestroyFlow(req->comm.pid, req->comm.ptId, req->comm.ptv, req->flowId);
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Destroy flow failed, ret:" << result << ", ptId:" << req->comm.ptId << ".");
    }

    Reply(ctx, result, static_cast<void *>(&req->flowId), sizeof(uint64_t));
    BIO_TRACE_END(MIRROR_TRACE_DESTROYFLOW_HDL, 0);
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
        return BIO_OK;
    }

    WCacheSlicePtr sliceP = nullptr;
    BResult ret = GetSlice(req->flowId, req->flowOffset, req->flowIndex, req->length, sliceP);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get slice failed:" << ret << ".");
        Reply(ctx, ret, nullptr, 0);
        return BIO_OK;
    }
    uint32_t sliceLen = sliceP->GetSerializeLen();
    auto *tmp = new (std::nothrow) uint8_t[sizeof(GetSliceResponse) + sliceLen];
    if (UNLIKELY(tmp == nullptr)) {
        LOG_ERROR("Alloc memory failed, len:" << sizeof(GetSliceResponse) + sliceLen << ".");
        Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        return BIO_OK;
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
        rsp->addrOffset[i] =
            BioServer::Instance()->GetNetEngine()->GetAddressOffset(addrVec[i].chunkId + addrVec[i].chunkOffset);
    }
    rsp->sliceLen = sliceLen;
    uint32_t outSliceLen = 0;
    sliceP->Serialize(rsp->sliceBuf, outSliceLen);
    if (UNLIKELY(outSliceLen != sliceLen)) {
        LOG_ERROR("Serialize slice failed, outSliceLen:" << outSliceLen << ", sliceLen:" << sliceLen << ".");
        Reply(ctx, BIO_INNER_ERR, nullptr, 0);
    } else {
        Reply(ctx, BIO_OK, rsp, sizeof(GetSliceResponse) + sliceLen);
    }
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
    auto req = static_cast<GetEvictRequest *>(ctx.MessageData());
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

int32_t MirrorServer::HandleFreeMem(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(FreeMemRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<FreeMemRequest *>(ctx.MessageData());
    for (uint32_t idx = 0; idx < req->num; idx++) {
        auto addr = BioServer::Instance()->GetNetEngine()->GetShmAddress(req->addr[idx]);
        BioServer::Instance()->GetNetEngine()->FreeLocalMrSingle(reinterpret_cast<uintptr_t>(addr));
    }
    Reply(ctx, BIO_OK, nullptr, 0);
    return BIO_OK;
}