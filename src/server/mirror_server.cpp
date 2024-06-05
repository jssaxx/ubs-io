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
        LOG_WARN("Check message pt version failed, base:" << base << ", ptv:" << reqComm.ptv << ".");
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
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_RES,
        std::bind(&MirrorServer::HandleQueryResource, this, std::placeholders::_1));
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
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_NOTIFY_UPDATE,
        std::bind(&MirrorServer::HandleNotifyUpdate, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_CHECK_UPDATE_READY,
        std::bind(&MirrorServer::HandleCheckUpdateReady, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SERVER_CHECK_REMOTE_UPDATE_READY,
        std::bind(&MirrorServer::HandleCheckRemoteUpdateReady, this, std::placeholders::_1));
}

void MirrorServer::ReplyListResultLocal(ServiceContext &ctx, std::unordered_map<std::string, ObjStat> &objs)
{
    uintptr_t address = 0;
    if (objs.size() != 0) {
        uint32_t key;
        auto ret = BioServer::Instance()->GetNetEngine()->AllocLocalMrSingle(address, key);
        if (UNLIKELY(ret != BIO_OK)) {
            BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
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
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&rsp), sizeof(ListResponse));
}

void MirrorServer::ReplyListResultRemote(ServiceContext &ctx, ListRequest *req,
    std::unordered_map<std::string, ObjStat> &objs)
{
    uintptr_t lAddress = 0;
    if (objs.size() != 0) {
        uint32_t lKey;
        auto ret = BioServer::Instance()->GetNetEngine()->AllocLocalMrSingle(lAddress, lKey);
        if (UNLIKELY(ret != BIO_OK)) {
            BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
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
            BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
            return;
        }
    }

    ListResponse rsp;
    rsp.addr = 0;
    rsp.addrOffset = 0;
    rsp.num = objs.size();
    rsp.buffLen = 0;
    BioServer::Instance()->GetNetEngine()->FreeLocalMrSingle(lAddress);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&rsp), sizeof(ListResponse));
}

BResult MirrorServer::CreateFlow(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId, bool isDegrade)
{
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(MIRROR_FLOW_CREATE_WCACHE_FAIL, &ret, BIO_ERR);
    ret = Cache::Instance().CreateWCache(procId, ptId, ptv, flowId, isDegrade);
    LVOS_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create write cache failed, ret:" << ret << ", procId:" << procId << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = Cache::Instance().CreateRCache(ptId, ptv);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create read cache failed, ret:" << ret << ", ptId:" << ptId << ".");
    } else {
        LOG_INFO("Create read cache success, procId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId);
    }

    return ret;
}

BResult MirrorServer::CreateFlowMaster(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t &flowId, bool &isDegrade)
{
    auto ret = Cache::Instance().AllocateFlowId(procId, ptId, ptv, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Alloc flow id failed, ret:" << ret << ", procId:" << procId << ", ptId:" << ptId << ".");
        return ret;
    }

    isDegrade = BioServer::Instance()->GetServiceState(); // 升级过程中，创建降级Cache实例
    return CreateFlow(procId, ptId, ptv, flowId, isDegrade);
}

BResult MirrorServer::CreateFlowSlave(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId, bool isDegrade)
{
    auto ret = CreateFlow(procId, ptId, ptv, flowId, isDegrade);
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

void MirrorServer::QueryCacheResource(QueryResourceRequest &req, QueryResourceResponse &rsp)
{
    CacheResDescription desc = { 0 };
    Cache::Instance().GetCacheResources(desc, WRITE_CACHE);
    rsp.writeRes = desc.memCapacity;
    LOG_INFO("Query write memory resource:" << desc.memCapacity << ", disk resource:" << desc.diskCapacity << ".");

    Cache::Instance().GetCacheResources(desc, READ_CACHE);
    rsp.readRes = desc.memCapacity;
    LOG_INFO("Query read memory resource:" << desc.memCapacity << ", disk resource:" << desc.diskCapacity << ".");
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
        if (req.memFromServer) {
            ret = BioServer::Instance()->GetNetEngine()->SyncRead(req.comm.srcNid, wReq);
        } else {
            ret = BioServer::Instance()->GetNetEngine()->SyncRead(netCtx.Channel(), wReq);
        }
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("One side read failed, ret:" << ret << ", idx:" << idx << ", lKey:" << lMrVec[idx].key <<
                ", rKey:" << rMrVec[idx].key << ", size:" << lMrVec[idx].size << ".");
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
        if (req.memFromServer) {
            ret = BioServer::Instance()->GetNetEngine()->SyncRead(req.comm.srcNid, wReq);
        } else {
            ret = BioServer::Instance()->GetNetEngine()->SyncRead(netCtx.Channel(), wReq);
        }
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("One side read failed, ret:" << ret << ", idx:" << idx << ", lKey:" << lMrVec[idx].key <<
                ", rKey:" << rMrKey << ", size:" << lMrVec[idx].size << ".");
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

BResult MirrorServer::Put(PutRequest &req, const WCacheSlicePtr &sliceP, ServiceContext &netCtx, uint32_t &ioStratege)
{
    LOG_DEBUG("Mirror server put, key:" << req.key << ", srcNid:" << req.comm.srcNid << ", flowId:" <<
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
    CacheAttr attr(static_cast<RealIoStrategy>(req.ioStratege), req.tenantId,
        static_cast<AffinityStrategy>(req.affinity), static_cast<WriteStrategy>(req.strategy));
    BResult ret = Cache::Instance().Put(req.key, sliceP, reader, attr);
    BIO_TRACE_END(MIRROR_TRACE_PUT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Put to write cache failed, ret:" << ret << ", key:" << req.key << ".");
        return ret;
    }
    ioStratege = static_cast<uint32_t>(attr.ioStratege); // 将下一个IO的写策略带回SDK端
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
    uint32_t off = 0;
    BResult ret = BIO_OK;

    BIO_TRACE_START(MIRROR_TRACE_GET_WRITE_DATA);
    LVOS_TP_START(WCACHE_READ_CALLBACK_FAIL, &ret, BIO_ERR);
    for (uint32_t idx = 0; idx < lMrVec.size(); idx++) {
        NetRequest rReq(lMrVec[idx].address, rMrVec[0].address + off, lMrVec[idx].key, rMrVec[0].key, lMrVec[idx].size);
        uint32_t dstPid = req.isConvDeploy ? 0 : static_cast<uint32_t>(req.comm.pid); // 融合部署场景目的端PID填充0
        ret = BioServer::Instance()->GetNetEngine()->SyncWrite(req.comm.srcNid, dstPid, rReq);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Sync write failed, ret:" << ret << ", index:" << idx << ", lKey:" << lMrVec[idx].key <<
                ", rKey:" << rMrVec[0].key << ", size:" << lMrVec[idx].size << ".");
            break;
        }
        off += lMrVec[idx].size;
    }
    if (isAlloc) {
        for (auto mr : lMrVec) {
            BioServer::Instance()->MemFree(mr.address);
        }
    }
    LVOS_TP_END;
    BIO_TRACE_END(MIRROR_TRACE_GET_WRITE_DATA, ret);

    return ret;
}

BResult MirrorServer::Get(GetRequest &req, GetResponse &rsp, ServiceContext &netCtx)
{
    InitGetResponse(rsp);
    MrInfo mrInfo = { req.address, static_cast<uint32_t>(req.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr sliceP = MakeRef<RCacheSlice>(req.ptId, req.length, addrVec);

    LOG_DEBUG("Mirror server get, key:" << req.key << ", srcNid:" << req.comm.srcNid << ", offset:" << req.offset <<
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
        LOG_ERROR("Get key from cache failed, ret:" << ret << ", key:" << req.key << ", offset:" << req.offset << ".");
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
        LOG_DEBUG("Mirror server delete success, key:" << req.key << ", ptId:" << req.comm.ptId << ".");
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
        LOG_DEBUG("Mirror server List success, prefix:" << req.prefix << ", ptId:" << req.comm.ptId << ", num:" <<
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

BResult MirrorServer::NotifyUpdate(NotifyUpdateRequest &req)
{
    return BioServer::Instance()->GetCm()->ReportServiceState(req.flag);
}

BResult MirrorServer::CheckUpdateReady(CheckUpdateReadyRequest &req, CheckUpdateReadyResponse &rsp)
{
    auto rpcEngine = BioServer::Instance()->GetNetEngine();
    uint64_t curNodeTimes = 0;
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioServer::Instance()->GetNodeView(&curNodeTimes);
    uint16_t localNid = BioServer::Instance()->GetCm()->GetCmLocalNodeId().VNodeId();
    rsp.flag = true;

    for (auto &nodeEntry : nodeView) {
        uint16_t dstNid = nodeEntry.second.id.VNodeId();
        if (localNid == dstNid) {
            auto chkRet = Cache::Instance().ServiceUngradeFlush();
            if (chkRet != BIO_OK) {
                rsp.flag = false;
                LOG_WARN("Check local node update not ready, localNid:" << localNid << ", chkRet: " << chkRet << ".");
            }
        } else {
            CheckRemoteUpdateReadyRequest ckRemoteReq = { { MESSAGE_MAGIC, req.comm.ptId, req.comm.ptv, localNid,
                                                            getpid() } };
            CheckRemoteUpdateReadyResponse *ckRemoteRsp = nullptr;
            uint64_t ckRemoteRspLen = 0;
            auto ret = rpcEngine->SyncCall<CheckRemoteUpdateReadyRequest, CheckRemoteUpdateReadyResponse>(dstNid,
                BIO_OP_SERVER_CHECK_REMOTE_UPDATE_READY, ckRemoteReq, &ckRemoteRsp, ckRemoteRspLen);
            if (UNLIKELY(ret != BIO_OK || ckRemoteRsp == nullptr)) {
                LOG_ERROR("Send check remote node update request failed, ret:" << ret << ", dstNid:" << dstNid << ".");
                rsp.flag = false;
                break;
            }
            rsp.flag = ckRemoteRsp->flag;
            if (!rsp.flag) {
                LOG_WARN("Check remote node update not ready, dstNid:" << dstNid << ".");
            }
            free(ckRemoteRsp);
        }
    }

    return BIO_OK;
}

BResult MirrorServer::SyncData(SyncDataRequest &req)
{
    LOG_DEBUG("Master sync data, ptId:" << req.comm.ptId << ", version:" << req.comm.ptv);
    BIO_TRACE_START(MIRROR_TRACE_SYNC_DATA);
    BResult ret = Cache::Instance().Flush(req.comm.ptId, req.comm.ptv);
    BIO_TRACE_END(MIRROR_TRACE_SYNC_DATA, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_WARN("Sync data failed:" << ret << ", ptId:" << req.comm.ptId << ", version:" << req.comm.ptv);
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

int32_t MirrorServer::MirrorServerShmInit(ServiceContext &ctx, ShmInitRequest *req)
{
    ShmInitResponse rsp;
    auto config = BioConfig::Instance()->GetDaemonConfig();
    rsp.scene = config.scene;
    rsp.serverPid = getpid();
    BioServer::Instance()->GetNetEngine()->QueryShmInfo(rsp.memFd, rsp.offset, rsp.length, rsp.mKey);
    auto ret = BioServer::Instance()->GetNetEngine()->SendFds(ctx.Channel(), &rsp.memFd, NO_1);
    if (ret != BIO_OK) {
        LOG_ERROR("Send fds failed, ret:" << ret << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
    } else {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(ShmInitResponse));
    }
    return BIO_OK;
}

int32_t MirrorServer::HandleShmInit(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(ShmInitRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive shm init message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<ShmInitRequest *>(ctx.MessageData());
    return MirrorServerShmInit(ctx, req);
}

int32_t MirrorServer::MirrorServerQueryNodeInfo(ServiceContext &ctx, GetLocalNidRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    CmNodeId localNid = BioServer::Instance()->GetLocalNid();
    GetLocalNidResponse rsp = { localNid.GroupId(), localNid.VNodeId(), BioServer::Instance()->GetNetProtocol() };
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(GetLocalNidResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryNodeInfo(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetLocalNidRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<GetLocalNidRequest *>(ctx.MessageData());
    return MirrorServerQueryNodeInfo(ctx, req);
}

int32_t MirrorServer::MirrorServerQueryNodeInfoByPt(ServiceContext &ctx, FileLocationQueryReq *req)
{
    CmNodeInfo nodeInfo;
    CmNodeId id;
    id.nodeId = req->masterPtId;
    id.groupId = 0;
    auto ret = BioServer::Instance()->GetNodeInfo(id, nodeInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    FileLocationQueryRsp rsp;
    ret = memcpy_s(rsp.hostMaster, NODE_DESC_SIZE, nodeInfo.ip.c_str(), nodeInfo.ip.length());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.portMaster = nodeInfo.port;

    id.nodeId = req->slavePtId;
    id.groupId = 0;
    ret = BioServer::Instance()->GetNodeInfo(id, nodeInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    ret = memcpy_s(rsp.hostSlave, NODE_DESC_SIZE, nodeInfo.ip.c_str(), nodeInfo.ip.length());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.portSlave = nodeInfo.port;

    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(FileLocationQueryRsp));
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryNodeInfoByPt(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(FileLocationQueryReq)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<FileLocationQueryReq *>(ctx.MessageData());
    return MirrorServerQueryNodeInfoByPt(ctx, req);
}

int32_t MirrorServer::MirrorServerQueryRes(ServiceContext &ctx, QueryResourceRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    QueryResourceResponse rsp;
    QueryCacheResource(*req, rsp);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(QueryResourceResponse));
    return BIO_OK;
}

int32_t MirrorServer::MirrorServerQueryNodeView(ServiceContext &ctx, QueryNodeViewRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    QueryNodeViewResponse rsp;
    QueryNodeView(*req, rsp);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(QueryNodeViewResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryResource(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(QueryResourceRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query res quota message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<QueryResourceRequest *>(ctx.MessageData());
    return MirrorServerQueryRes(ctx, req);
}

int32_t MirrorServer::HandleQueryNodeView(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(QueryNodeViewRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<QueryNodeViewRequest *>(ctx.MessageData());
    return MirrorServerQueryNodeView(ctx, req);
}

int32_t MirrorServer::MirrorServerQueryPtView(ServiceContext &ctx, QueryPtViewRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    QueryPtViewResponse rsp;
    QueryPtView(*req, rsp);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(QueryPtViewResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryPtView(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(QueryPtViewRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<QueryPtViewRequest *>(ctx.MessageData());
    return MirrorServerQueryPtView(ctx, req);
}

BResult MirrorServer::GetFlowGlobEvictOffset(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset)
{
    auto ret = SendFlowGetEvictOffset(ptId, flowId, flowOffset);
    ChkTrue(ret == BIO_OK, ret, "Get local role fail:" << ret << ", ptId:" << ptId);
    LOG_DEBUG("Slave:get flow evict offset, ptId:" << ptId << ", flowId:" << flowId << ", flowOffset:" << flowOffset);
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
    LOG_DEBUG("Master get evict offset, ptId:" << req.comm.ptId << ", flowId:" << req.flowId << ", flowOffset:" <<
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

int32_t MirrorServer::MirrorServerPut(ServiceContext &ctx, PutRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    WCacheSlicePtr sliceP = nullptr;
    if (req->sliceLen == 0) {
        MrInfo mrInfo = { req->mrAddress, static_cast<uint32_t>(req->mrSize) };
        std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
        LVOS_TP_START(PUT_SLICE_ZERO_ALLOC_FAIL, &sliceP, nullptr);
        sliceP = MakeRef<WCacheSlice>(req->flowId, req->flowOffset, req->flowIndex, req->length, addrVec);
        LVOS_TP_END;
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
            return BIO_OK;
        }
    } else {
        LVOS_TP_START(PUT_SLICE_NORMAL_ALLOC_FAIL, &sliceP, nullptr);
        sliceP = MakeRef<WCacheSlice>();
        LVOS_TP_END;
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
            return BIO_OK;
        }
        sliceP->Deserialize(req->sliceBuf, req->sliceLen);
    }

    BIO_TRACE_START(MIRROR_TRACE_PUT_RECEIVE_REMOTE);
    uint32_t ioStratege = 0;
    BResult result = Put(*req, sliceP, ctx, ioStratege);
    BIO_TRACE_END(MIRROR_TRACE_PUT_RECEIVE_REMOTE, result);

    PutResponse rsp;
    BIO_TRACE_START(MIRROR_TRACE_PUT_REMOTE_GET_QUOTA);
    rsp.updateQuota = Cache::Instance().GetAdjustWriteQuota();
    rsp.ioStratege = ioStratege;
    BIO_TRACE_END(MIRROR_TRACE_PUT_REMOTE_GET_QUOTA, BIO_OK);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, result, &rsp, sizeof(PutResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandlePut(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() < sizeof(PutRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive put message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NET_RETRY, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<PutRequest *>(ctx.MessageData());
    return MirrorServerPut(ctx, req);
}

int32_t MirrorServer::MirrorServerGet(ServiceContext &ctx, GetRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    GetResponse rsp;
    BIO_TRACE_START(MIRROR_TRACE_GET_HDL);
    BResult result = Get(*req, rsp, ctx);
    BIO_TRACE_END(MIRROR_TRACE_GET_HDL, result);
    if (result != BIO_OK) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, result, nullptr, 0);
        return BIO_OK;
    }

    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&rsp), sizeof(GetResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleGet(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive get message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<GetRequest *>(ctx.MessageData());
    return MirrorServerGet(ctx, req);
}

int32_t MirrorServer::MirrorServerDelete(ServiceContext &ctx, DeleteRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    BIO_TRACE_START(MIRROR_TRACE_DEL_HDL);
    BResult result = Delete(*req);
    BIO_TRACE_END(MIRROR_TRACE_DEL_HDL, result);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&result), sizeof(BResult));
    return BIO_OK;
}

int32_t MirrorServer::HandleDelete(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(DeleteRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive delete message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<DeleteRequest *>(ctx.MessageData());
    return MirrorServerDelete(ctx, req);
}

int32_t MirrorServer::MirrorServerStat(ServiceContext &ctx, StatRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    ObjStat objInfo;
    BIO_TRACE_START(MIRROR_TRACE_STAT_HDL);
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(MIRROR_SERVER_HDL_STAT_FAIL, &ret, BIO_INNER_RETRY);
    ret = Stat(*req, objInfo);
    LVOS_TP_END;
    BIO_TRACE_END(MIRROR_TRACE_STAT_HDL, ret);
    if (ret != BIO_OK) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
        return BIO_OK;
    }

    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&objInfo), sizeof(ObjStat));
    return BIO_OK;
}

int32_t MirrorServer::HandleStat(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(StatRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive delete message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<StatRequest *>(ctx.MessageData());
    return MirrorServerStat(ctx, req);
}

int32_t MirrorServer::MirrorServerList(ServiceContext &ctx, ListRequest *req)
{
    std::unordered_map<std::string, ObjStat> objs;
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(MIRROR_SERVER_HDL_LIST_FAIL, &ret, BIO_INNER_RETRY);
    ret = List(*req, objs);
    LVOS_TP_END;
    if (ret != BIO_OK) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
        return BIO_OK;
    }

    if (static_cast<uint16_t>(req->comm.srcNid) == BioServer::Instance()->GetLocalNid().VNodeId()) {
        ReplyListResultLocal(ctx, objs);
    } else {
        ReplyListResultRemote(ctx, req, objs);
    }
    return BIO_OK;
}

int32_t MirrorServer::HandleList(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(ListRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive load message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<ListRequest *>(ctx.MessageData());
    return MirrorServerList(ctx, req);
}

int32_t MirrorServer::MirrorServerLoad(ServiceContext &ctx, LoadRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    BIO_TRACE_START(MIRROR_TRACE_LOAD_HDL);
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(MIRROR_SERVER_HDL_LOAD_FAIL, &ret, BIO_INNER_RETRY);
    ret = Load(*req);
    LVOS_TP_END;
    BIO_TRACE_END(MIRROR_TRACE_LOAD_HDL, ret);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleLoad(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(LoadRequest) || ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive load message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<LoadRequest *>(ctx.MessageData());
    return MirrorServerLoad(ctx, req);
}

int32_t MirrorServer::MirrorServerReportHb(ServiceContext &ctx)
{
    HbResponse rsp;
    auto ret = BioServer::Instance()->GetHbInfo(&rsp.curNodeTimes, &rsp.curPtTimes);
    if (ret != BIO_OK) {
        LOG_ERROR("Get hb info fail:" << ret);
        BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
        return ret;
    }

    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(HbResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleReportHb(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(HbRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive hb message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    return MirrorServerReportHb(ctx);
}

int32_t MirrorServer::MirrorServerCreateFlow(ServiceContext &ctx, CreateFlowRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    BResult result;
    uint64_t flowId = UINT64_MAX;
    BIO_TRACE_START(MIRROR_TRACE_CREATEFLOW_HDL);
    if (req->opType == 0) {
        result = CreateFlowMaster(req->comm.pid, req->comm.ptId, req->comm.ptv, flowId, req->isDegrade);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Master create flow failed, ret:" << result << ", ptId:" << req->comm.ptId << ".");
            flowId = UINT64_MAX;
        }
    } else if (req->opType == 1) {
        result = CreateFlowSlave(req->comm.pid, req->comm.ptId, req->comm.ptv, req->flowId, req->isDegrade);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Slave create flow failed, ret:" << result << ", ptId:" << req->comm.ptId << ".");
            flowId = UINT64_MAX;
        } else {
            flowId = 0;
        }
    } else {
        LOG_ERROR("Invalid op type, opType:" << req->opType << ", ptId:" << req->comm.ptId << ".");
    }
    BIO_TRACE_END(MIRROR_TRACE_CREATEFLOW_HDL, BIO_OK);

    CreateFlowResponse rsp{ flowId, req->isDegrade };
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&rsp), sizeof(CreateFlowResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleCreateFlow(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(CreateFlowRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive create flow message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<CreateFlowRequest *>(ctx.MessageData());
    return MirrorServerCreateFlow(ctx, req);
}

int32_t MirrorServer::MirrorServerDestroyFlow(ServiceContext &ctx, DestroyFlowRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_DESTROYFLOW_HDL);
    auto result = DestroyFlow(req->comm.pid, req->comm.ptId, req->comm.ptv, req->flowId);
    BIO_TRACE_END(MIRROR_TRACE_DESTROYFLOW_HDL, result);
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Destroy flow failed, ret:" << result << ", ptId:" << req->comm.ptId << ".");
    }

    BioServer::Instance()->GetNetEngine()->Reply(ctx, result, static_cast<void *>(&req->flowId), sizeof(uint64_t));
    return BIO_OK;
}

int32_t MirrorServer::HandleDestroyFlow(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(DestroyFlowRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive destroy flow message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<DestroyFlowRequest *>(ctx.MessageData());
    return MirrorServerDestroyFlow(ctx, req);
}

int32_t MirrorServer::MirrorServerGetSlice(ServiceContext &ctx, GetSliceRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    WCacheSlicePtr sliceP = nullptr;
    BResult ret = GetSlice(req->flowId, req->flowOffset, req->flowIndex, req->length, sliceP);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get slice failed:" << ret << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
        return BIO_OK;
    }
    uint32_t sliceLen = sliceP->GetSerializeLen();
    uint8_t *tmp = nullptr;
    LVOS_TP_START(GET_SLICE_ALLOC_FAIL, &tmp, nullptr);
    tmp = new (std::nothrow) uint8_t[sizeof(GetSliceResponse) + sliceLen];
    LVOS_TP_END;
    if (UNLIKELY(tmp == nullptr)) {
        LOG_ERROR("Alloc memory failed, len:" << sizeof(GetSliceResponse) + sliceLen << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        return BIO_OK;
    }
    auto rsp = static_cast<GetSliceResponse *>(static_cast<void *>(tmp));

    std::vector<FlowAddr> addrVec = sliceP->GetAddrs();
    if (addrVec.size() > SLICE_ADDR_MAX_SIZE) {
        LOG_ERROR("Slice addr num " << addrVec.size() << " exceed " << SLICE_ADDR_MAX_SIZE << ".");
        delete[] tmp;
        return static_cast<int32_t>(BIO_INNER_ERR);
    }
    rsp->updateQuota = Cache::Instance().GetAdjustWriteQuota();
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
    sliceP->Serialize(rsp->sliceBuf, rsp->sliceLen, outSliceLen);
    if (UNLIKELY(outSliceLen != sliceLen)) {
        LOG_ERROR("Serialize slice failed, outSliceLen:" << outSliceLen << ", sliceLen:" << sliceLen << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
    } else {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, rsp, sizeof(GetSliceResponse) + sliceLen);
    }
    delete[] tmp;
    return BIO_OK;
}

int32_t MirrorServer::HandleGetSlice(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetSliceRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive create flow message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<GetSliceRequest *>(ctx.MessageData());
    return MirrorServerGetSlice(ctx, req);
}

int32_t MirrorServer::MirrorServerSyncData(ServiceContext &ctx, SyncDataRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_SYNC_DATA_HDL);
    BResult ret = SyncData(*req);
    BIO_TRACE_END(MIRROR_TRACE_SYNC_DATA_HDL, ret);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&ret), sizeof(BResult));
    return BIO_OK;
}

int32_t MirrorServer::HandleSyncData(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(SyncDataRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<SyncDataRequest *>(ctx.MessageData());
    return MirrorServerSyncData(ctx, req);
}

int32_t MirrorServer::MirrorServerGetEvictOffset(ServiceContext &ctx, GetEvictRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    uint64_t flowOffset;
    BIO_TRACE_START(MIRROR_TRACE_GET_EVICT_OFFSET_HDL);
    BResult ret = GetEvictOffset(*req, flowOffset);
    BIO_TRACE_END(MIRROR_TRACE_GET_EVICT_OFFSET_HDL, ret);
    if (ret != BIO_OK) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
        return BIO_OK;
    }

    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&flowOffset), sizeof(uint64_t));
    return BIO_OK;
}

int32_t MirrorServer::HandleGetEvictOffset(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetEvictRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<GetEvictRequest *>(ctx.MessageData());
    return MirrorServerGetEvictOffset(ctx, req);
}

int32_t MirrorServer::MirrorServerFreeMem(ServiceContext &ctx, FreeMemRequest *req)
{
    for (uint32_t idx = 0; idx < req->num; idx++) {
        auto addr = BioServer::Instance()->GetNetEngine()->GetShmAddress(req->addr[idx]);
        BioServer::Instance()->GetNetEngine()->FreeLocalMrSingle(reinterpret_cast<uintptr_t>(addr));
    }
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, nullptr, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleFreeMem(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(FreeMemRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<FreeMemRequest *>(ctx.MessageData());
    return MirrorServerFreeMem(ctx, req);
}

int32_t MirrorServer::MirrorServerNotifyUpdate(ServiceContext &ctx, NotifyUpdateRequest *req)
{
    auto ret = NotifyUpdate(*req);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
    return BIO_OK;
}

int32_t MirrorServer::HandleNotifyUpdate(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(NotifyUpdateRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<NotifyUpdateRequest *>(ctx.MessageData());
    return MirrorServerNotifyUpdate(ctx, req);
}

int32_t MirrorServer::MirrorServerCheckUpdateReady(ServiceContext &ctx, CheckUpdateReadyRequest *req)
{
    CheckUpdateReadyResponse rsp;
    auto ret = CheckUpdateReady(*req, rsp);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(CheckUpdateReadyResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleCheckUpdateReady(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(CheckUpdateReadyRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<CheckUpdateReadyRequest *>(ctx.MessageData());
    return MirrorServerCheckUpdateReady(ctx, req);
}

int32_t MirrorServer::MirrorServerCheckRemoteUpdateReady(ServiceContext &ctx, CheckRemoteUpdateReadyRequest *req)
{
    CheckRemoteUpdateReadyResponse rsp;
    auto chkRet = Cache::Instance().ServiceUngradeFlush();
    if (chkRet != BIO_OK) {
        rsp.flag = false;
        LOG_WARN("Check remote update ready failed, chkRet: " << chkRet << ".");
    } else {
        rsp.flag = true;
    }
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(CheckRemoteUpdateReadyResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleCheckRemoteUpdateReady(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(CheckRemoteUpdateReadyRequest)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<CheckRemoteUpdateReadyRequest *>(ctx.MessageData());
    return MirrorServerCheckRemoteUpdateReady(ctx, req);
}