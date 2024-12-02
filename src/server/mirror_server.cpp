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
#include "bio_crc_util.h"
#include "cache_overload_ctrl.h"
#include "mirror_server.h"

using namespace ock::bio;
using namespace ock::hcom;

bool MirrorServer::CheckMagic(RequestComm &reqComm)
{
    if (UNLIKELY(reqComm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        return false;
    }
    return true;
}

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

void MirrorServer::RegisterOpcodeStep2(NetEnginePtr &netEngine)
{
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_CREATE_FLOW,
        std::bind(&MirrorServer::HandleCreateFlow, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_DESTROY_FLOW,
        std::bind(&MirrorServer::HandleDestroyFlow, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_SLICE,
        std::bind(&MirrorServer::HandleGetSlice, this, std::placeholders::_1));
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
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_UFS_CONFIG,
        std::bind(&MirrorServer::HandleGetUnderFsConfig, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SERVER_NEGOTIATE_EVICT,
        std::bind(&MirrorServer::HandleEvictNegotiateRequest, this, std::placeholders::_1));
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
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_GET_QUOTA_INFO,
        std::bind(&MirrorServer::HandleQueryQuota, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_ALLOC_QUOTA,
        std::bind(&MirrorServer::HandleAllocQuota, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_SDK_FREE_QUOTA,
        std::bind(&MirrorServer::HandleFreeQuota, this, std::placeholders::_1));
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

    RegisterOpcodeStep2(netEngine);
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
    uint64_t base = BioServer::Instance()->GetPtEntry(ptId).version;
    if (UNLIKELY(ptv != base)) {
        LOG_WARN("Check message pt version failed, base:" << base << ", ptv:" << ptv << ".");
        return BIO_CHECK_PT_FAIL;
    }
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
    uint64_t base = BioServer::Instance()->GetPtEntry(ptId).version;
    if (UNLIKELY(ptv != base)) {
        LOG_WARN("Check message pt version failed, base:" << base << ", ptv:" << ptv << ".");
        return BIO_CHECK_PT_FAIL;
    }
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
    BResult ret;
    LVOS_TP_START(MIRROR_SERVER_GET_SLICE_FAIL, &ret, BIO_INNER_RETRY);
    ret = Cache::Instance().GetWCacheSlice(sliceKey, slice);
    LVOS_TP_END;
    return ret;
}

void MirrorServer::QueryCacheQuota(QueryQuotaRequest &req, QueryQuotaResponse &rsp)
{
    static uint64_t defaultPreloadSize = NO_128 * NO_1024 * NO_1024; // 128M
    uint64_t totalQuota = CacheOverloadCtrl::Instance().GetAvailableQuota();
    rsp.preloadSize = std::min<uint64_t>(defaultPreloadSize, ROUND_UP((totalQuota / NO_10), NO_4096));
    rsp.enable = mBioConfig->GetDaemonConfig().enableQos;
    LOG_INFO("Query quota info success, write cache quota:" << totalQuota << ", scene:" << req.scene <<
        ", preload size:" << (rsp.preloadSize / NO_1024 / NO_1024) << "M, enable:" << rsp.enable << ".");
}

BResult MirrorServer::AllocCacheQuota(AllocQuotaRequest &req, AllocQuotaResponse &rsp)
{
    if (UNLIKELY(!CheckAll(req.comm))) {
        rsp.exceptQuota = 0;
        return BIO_CHECK_PT_FAIL;
    }

    auto bioServer = BioServer::Instance();
    if (bioServer == nullptr) {
        LOG_ERROR("Bio server instance get fail");
        return BIO_ALLOC_FAIL;
    }
    auto rpcEngine = bioServer->GetNetEngine();
    if (rpcEngine == nullptr) {
        LOG_ERROR("Net engine get fail");
        return BIO_ALLOC_FAIL;
    }
    if (req.nid != bioServer->GetLocalNid().VNodeId() && !rpcEngine->IsChannelExist(req.nid, req.cid)) {
        LOG_ERROR("Req nodeid " << req.nid << " or ptid " << req.cid << " is incorrect.");
        return BIO_INNER_RETRY;
    }

    QuotaHolder holder = { req.nid, req.cid };
    BIO_TRACE_START(MIRROR_TRACE_QOS_ALLOC);
    BResult ret = CacheOverloadCtrl::Instance().AllocQuota(holder, req.allocQuota, rsp.exceptQuota);
    BIO_TRACE_END(MIRROR_TRACE_QOS_ALLOC, ret);
    if (ret != BIO_OK) {
        LOG_ERROR("Alloc quota failed, ret:" << ret << ", holder:" << req.nid << "-" << req.cid << ", size:" <<
            req.allocQuota << ".");
    }
    return ret;
}

BResult MirrorServer::FreeCacheQuota(FreeQuotaRequest &req)
{
    if (UNLIKELY(!CheckAll(req.comm))) {
        return BIO_CHECK_PT_FAIL;
    }
    QuotaHolder holder = { req.nid, req.cid };
    std::string innerKey = "RollbackCacheQuota";
    BIO_TRACE_START(MIRROR_TRACE_QOS_ROLLBACK);
    CacheOverloadCtrl::Instance().ReleaseQuota(innerKey.c_str(), holder, req.quota, 1);
    CacheOverloadCtrl::Instance().FreeQuota(req.quota, 1);
    BIO_TRACE_END(MIRROR_TRACE_QOS_ROLLBACK, BIO_OK);
    LOG_INFO("Rollback cache quota success, holder: " << req.nid << "-" << req.cid << ", size:" << req.quota << ".");
    return BIO_OK;
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
        rsp.copyNum = ptEntry.second.copys.size();
        index++;
    }
    rsp.num = index;
    rsp.flag = (index == 0) ? 0 : 1;
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

BResult MirrorServer::Put(PutRequest &req, const WCacheSlicePtr &sliceP, ServiceContext &netCtx, uint32_t &ioStrategy)
{
    if (UNLIKELY(!CheckAll(req.comm))) {
        return BIO_CHECK_PT_FAIL;
    }

    LOG_DEBUG("Mirror server put, key:" << req.key << ", srcNid:" << req.comm.srcNid << ", flowId:" <<
        sliceP->GetFlowId() << ", offsetInFlow:" << sliceP->GetOffsetInFlow() << ", indexInFlow:" <<
        sliceP->GetIndexInFlow() << ", slice: " << sliceP->ToString() << ", rFlowSize:" << sliceP->GetAddrs().size() <<
        ".");

    auto reader = [&req, &netCtx, this](const SlicePtr &from, const SlicePtr &to) -> BResult {
        if (req.comm.srcNid == BioServer::Instance()->GetLocalNid().VNodeId()) {
            return BIO_OK; // read local
        } else {
            return ReaderRemote(from, to, req, netCtx);
        }
    };

    CacheAttr attr(static_cast<RealIoStrategy>(req.ioStrategy), req.tenantId,
        static_cast<AffinityStrategy>(req.affinity), static_cast<WriteStrategy>(req.strategy));
    BIO_TRACE_START(MIRROR_TRACE_PUT);
    BResult ret = Cache::Instance().Put(req.key, sliceP, reader, attr);
    BIO_TRACE_END(MIRROR_TRACE_PUT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Put to write cache failed, ret:" << ret << ", key:" << req.key << ".");
        return ret;
    } else {
        QuotaHolder holder = { static_cast<uint32_t>(req.quotaNid), req.quotaCid };
        BIO_TRACE_START(MIRROR_TRACE_QOS_RELEASE);
        CacheOverloadCtrl::Instance().ReleaseQuota(req.key, holder, req.length, 0);
        BIO_TRACE_END(MIRROR_TRACE_QOS_RELEASE, BIO_OK);
    }
    ioStrategy = static_cast<uint32_t>(attr.ioStrategy); // 修改sdk端IO写策略
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

BResult MirrorServer::WriterLocalSameProcess(const SlicePtr &from, GetResponse &rsp)
{
    std::vector<NetMrInfo> lMrVec;
    for (auto addr : from->GetAddrs()) {
        MrInfo mr{};
        addr.ToMrInfo(mr);
        lMrVec.emplace_back(NetMrInfo(mr.address, mr.size, 0));
    }

    ChkTrue(lMrVec.size() <= SLICE_ADDR_SIZE, BIO_INNER_ERR, "Local mr size exceed 4, size:" << lMrVec.size() << ".");
    rsp.isAlloc = false;
    rsp.num = lMrVec.size();
    for (uint32_t idx = 0; idx < lMrVec.size(); idx++) {
        rsp.address[idx] = lMrVec[idx].address;
        rsp.addrOffset[idx] = 0;
        rsp.addrLen[idx] = lMrVec[idx].size;
    }
    return BIO_OK;
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
    return BIO_OK;
}

BResult MirrorServer::WriterLocalDiffProcess(bool &isAlloc, std::vector<NetMrInfo> &lMrVec, GetResponse &rsp,
    GetRequest &req)
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
    if (isAlloc == true) {
        BioServer::Instance()->GetNetEngine()->InsertGetHolder(req.comm.srcNid, req.comm.pid, lMrVec);
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
    if (UNLIKELY(!CheckAll(req.comm))) {
        return BIO_CHECK_PT_FAIL;
    }
    InitGetResponse(rsp);
    MrInfo mrInfo = { req.address, static_cast<uint32_t>(req.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr sliceP = MakeRef<RCacheSlice>(req.ptId, req.length, addrVec);
    if (UNLIKELY(sliceP == nullptr)) {
        LOG_ERROR("Make rcache slice failed.");
        return BIO_ALLOC_FAIL;
    }

    LOG_DEBUG("Mirror server get, key:" << req.key << ", srcNid:" << req.comm.srcNid << ", offset:" << req.offset <<
        ", length:" << req.length << ", mr size:" << req.size << ", mr key:" <<
        req.mrKey << ", slice: " << sliceP->ToString() << ", rFlowSize:" << sliceP->GetAddrs().size() << ".");

    auto writer = [&req, &rsp, &netCtx, this](const SlicePtr &from, const SlicePtr &to) -> BResult {
        if ((req.comm.srcNid == BioServer::Instance()->GetLocalNid().VNodeId()) && (req.comm.pid == getpid())) {
            return WriterLocalSameProcess(from, rsp);
        }

        bool isAlloc = false;
        std::vector<NetMrInfo> rMrVec;
        std::vector<NetMrInfo> lMrVec;
        BResult ret = WriterParseMrInfo(from, to, rMrVec, lMrVec, req.mrKey, isAlloc);
        if (ret != BIO_OK) {
            return ret;
        }

        if ((req.comm.srcNid == BioServer::Instance()->GetLocalNid().VNodeId()) && (req.comm.pid != getpid())) {
            return WriterLocalDiffProcess(isAlloc, lMrVec, rsp, req);
        }

        return WriterRemote(isAlloc, lMrVec, rMrVec, netCtx, req);
    };

    BIO_TRACE_START(MIRROR_TRACE_GET);
    BResult ret = Cache::Instance().Get(req.key, req.offset, sliceP, writer, rsp.realLen);
    BIO_TRACE_END(MIRROR_TRACE_GET, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get key from cache failed, ret:" << ret << ", key:" << req.key << ", offset:" << req.offset << ".");
    } else {
        if (mBioConfig->GetDaemonConfig().enableCrc) {
            rsp.dataCrc = sliceP->GetDataCrc();
        }
    }
    return ret;
}

BResult MirrorServer::Delete(DeleteRequest &req)
{
    if (UNLIKELY(!CheckAll(req.comm))) {
        return BIO_CHECK_PT_FAIL;
    }
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
    if (UNLIKELY(!CheckAll(req.comm))) {
        return BIO_CHECK_PT_FAIL;
    }
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
    if (UNLIKELY(!CheckAll(req.comm))) {
        return BIO_CHECK_PT_FAIL;
    }
    BIO_TRACE_START(MIRROR_TRACE_LOAD);
    uint64_t realLen = 0;
    BResult ret = Cache::Instance().Load(req.comm.ptId, req.key, req.offset, req.length, realLen);
    BIO_TRACE_END(MIRROR_TRACE_LOAD, ret);
    return ret == BIO_LOAD_ALLOC_FAIL ? BIO_ALLOC_FAIL : ret;
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
    mBioConfig = BioConfig::Instance();
    if (mBioConfig == nullptr) {
        LOG_ERROR("Mirror server init bio config failed");
        return BIO_NOT_READY;
    }
    mStarted = true;
    return BIO_OK;
}

int32_t MirrorServer::MirrorServerShmInit(ServiceContext &ctx)
{
    ShmInitResponse rsp;
    auto config = mBioConfig->GetDaemonConfig();
    rsp.serverPid = getpid();
    rsp.scene = config.workScene;
    rsp.alignSize = config.workIoAlignSize;
    rsp.ioTimeOut = config.workIoTimeOut;
    rsp.netTimeOut = config.workNetTimeOut;
    rsp.logLevel = config.logLevel;
    rsp.enableCrc = config.enableCrc;
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
    LVOS_TP_START(SERVER_NO_PROCESS_SHM_INIT_SKIP, 0);
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(ShmInitRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive shm init message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }
    LVOS_TP_END;
    return MirrorServerShmInit(ctx);
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
    rsp.hostMaster[nodeInfo.ip.length()] = '\0';
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

int32_t MirrorServer::MirrorServerQueryQuota(ServiceContext &ctx, QueryQuotaRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }

    QueryQuotaResponse rsp;
    QueryCacheQuota(*req, rsp);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(QueryQuotaResponse));
    return BIO_OK;
}

int32_t MirrorServer::MirrorServerAllocQuota(ServiceContext &ctx, AllocQuotaRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }
    if (req->allocQuota > NO_1024 * IO_SIZE_1M) {
        LOG_ERROR("Invalid allocQuota :" << req->allocQuota << ".");
        return BIO_INVALID_PARAM;
    }
    AllocQuotaResponse rsp;
    auto ret = AllocCacheQuota(*req, rsp);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, &rsp, sizeof(AllocQuotaResponse));
    return BIO_OK;
}

int32_t MirrorServer::MirrorServerFreeQuota(ServiceContext &ctx, FreeQuotaRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }
    if (req->quota > NO_1024 * IO_SIZE_1M) {
        LOG_ERROR("Invalid free Quota :" << req->quota << ".");
        return BIO_INVALID_PARAM;
    }
    auto ret = FreeCacheQuota(*req);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, &ret, sizeof(BResult));
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

int32_t MirrorServer::HandleQueryQuota(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(QueryQuotaRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query quota message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<QueryQuotaRequest *>(ctx.MessageData());
    return MirrorServerQueryQuota(ctx, req);
}

int32_t MirrorServer::HandleAllocQuota(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(AllocQuotaRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive alloc quota message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<AllocQuotaRequest *>(ctx.MessageData());
    return MirrorServerAllocQuota(ctx, req);
}

int32_t MirrorServer::HandleFreeQuota(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(FreeQuotaRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive free quota message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<FreeQuotaRequest *>(ctx.MessageData());
    return MirrorServerFreeQuota(ctx, req);
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

bool MirrorServer::CheckQueryPtViewReq(QueryPtViewRequest *req)
{
    uint64_t curPtTimes = 0;
    std::map<uint16_t, CmPtInfo> ptView = BioServer::Instance()->GetPtView(&curPtTimes);
    if (req->bar > ptView.size()) {
        LOG_ERROR("bar:" << req->bar << ", pt view size:" << ptView.size());
        return false;
    }
    return true;
}

int32_t MirrorServer::MirrorServerQueryPtView(ServiceContext &ctx, QueryPtViewRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_CHECK_PT_FAIL;
    }
    if (!CheckQueryPtViewReq(req)) {
        LOG_ERROR("Check req failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_ERR;
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
    LOG_TRACE("Slave:get flow evict offset, ptId:" << ptId << ", flowId:" << flowId << ", flowOffset:" << flowOffset);
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

bool MirrorServer::CheckPutReq(PutRequest *req)
{
    if (req->affinity > AFFINITY_BUTT || req->affinity < LOCAL_AFFINITY) {
        return false;
    }
    if (req->strategy > STRATEGY_BUTT || req->strategy < WRITE_BACK) {
        return false;
    }
    std::string key(req->key);
    if (key.find("..") != std::string::npos) {
        return false;
    }
    if (req->length == 0 || req->length > BIO_IO_MAX_LEN) {
        return false;
    }

    if (req->memFromServer != true) {
        if (req->sliceLen != 0) {
            return false;
        }
        if (req->mrSize == 0 || req->mrSize > BIO_IO_MAX_LEN) {
            return false;
        }
        if (req->mrAddress == 0) {
            return false;
        }
    } else {
        if (req->sliceLen == 0) {
            return false;
        }
        if (req->mrAddress != 0 && req->mrSize != 0) {
            return false;
        }
    }
    if (req->ioStrategy > WRITE_UNDERFS_BACK) {
        return false;
    }
    return true;
}

bool MirrorServer::IsValidSliceAddress(WCacheSlicePtr &sliceP)
{
    auto addrs = sliceP->GetAddrs();
    for (auto addr : addrs) {
        auto beginAddr = addr.chunkId + addr.chunkOffset;
        auto endAddr = beginAddr + addr.chunkLen;
        if (!BioServer::Instance()->GetNetEngine()->IsValidAddress(beginAddr, endAddr)) {
            return false;
        }
    }
    return true;
}

int32_t MirrorServer::MirrorServerPut(ServiceContext &ctx, PutRequest *req)
{
    if (!CheckPutReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
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
        sliceP->SetDataCrc(req->dataCrc);
    } else {
        LVOS_TP_START(MIRROR_SERVER_PUT_PASS_MESSAGE_CHECK, 0);
        if (UNLIKELY(ctx.MessageDataLen() < sizeof(PutRequest) + req->sliceLen)) {
            LOG_ERROR("Invalid param message data length: " << ctx.MessageDataLen() <<
                ", param data length:" << sizeof(PutRequest) + req->sliceLen);
            BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
            return BIO_OK;
        }
        LVOS_TP_END;
        LVOS_TP_START(PUT_SLICE_NORMAL_ALLOC_FAIL, &sliceP, nullptr);
        sliceP = MakeRef<WCacheSlice>();
        LVOS_TP_END;
        if (UNLIKELY(sliceP == nullptr)) {
            LOG_ERROR("Make wcache slice failed.");
            BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
            return BIO_OK;
        }
        auto ret = sliceP->Deserialize(req->sliceBuf, req->sliceLen);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Deserialize slice failed, ret:" << ret << ".");
            BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
            return BIO_OK;
        }

        LVOS_TP_START(MIRROR_SERVER_PUT_SLICE_IS_LOCAL_NID, &(req->comm.srcNid),
            (BioServer::Instance()->GetLocalNid().VNodeId()));
        LVOS_TP_END;
        if (static_cast<uint16_t>(req->comm.srcNid) == BioServer::Instance()->GetLocalNid().VNodeId()) {
            if (!IsValidSliceAddress(sliceP)) {
                LOG_ERROR("Invalid slice address.");
                BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
                return BIO_OK;
            }
        }

        sliceP->SetDataCrc(req->dataCrc);
    }

    BIO_TRACE_START(MIRROR_TRACE_PUT_RECEIVE_REMOTE);
    BResult result;
    uint32_t ioStrategy = 0;
    LVOS_TP_START(MIRROR_SERVER_HDL_PUT_FAIL, &result, BIO_INNER_RETRY);
    result = Put(*req, sliceP, ctx, ioStrategy);
    LVOS_TP_END;
    BIO_TRACE_END(MIRROR_TRACE_PUT_RECEIVE_REMOTE, result);

    PutResponse rsp;
    rsp.ioStrategy = ioStrategy;
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

bool MirrorServer::CheckGetReq(GetRequest *req)
{
    std::string key(req->key);
    if (key.find("..") != std::string::npos) {
        return false;
    }
    if (req->offset > BIO_IO_MAX_LEN || req->length == 0 || req->length > BIO_IO_MAX_LEN) {
        return false;
    }

    if (req->offset + req->length > BIO_IO_MAX_LEN) {
        return false;
    }

    if (req->size == 0 || req->size > BIO_IO_MAX_LEN) {
        return false;
    }
    return true;
}

int32_t MirrorServer::MirrorServerGet(ServiceContext &ctx, GetRequest *req)
{
    if (!CheckGetReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }
    GetResponse rsp;
    BResult result;
    LVOS_TP_START(MIRROR_SERVER_HDL_GET_FAIL, &result, BIO_INNER_RETRY);
    result = Get(*req, rsp, ctx);
    LVOS_TP_END;
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

bool MirrorServer::CheckDeleteReq(DeleteRequest *req)
{
    std::string key(req->key);
    if (key.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

int32_t MirrorServer::MirrorServerDelete(ServiceContext &ctx, DeleteRequest *req)
{
    bool checkflag = CheckDeleteReq(req);
    if (!checkflag) {
        BResult result = BIO_INVALID_PARAM;
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, static_cast<void *>(&result),
            sizeof(BResult));
    }
    BResult result;
    LVOS_TP_START(MIRROR_SERVER_HDL_DELETE_FAIL, &result, BIO_INNER_RETRY);
    result = Delete(*req);
    LVOS_TP_END;
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

bool MirrorServer::CheckStatReq(StatRequest *req)
{
    std::string key(req->key);
    if (key.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

int32_t MirrorServer::MirrorServerStat(ServiceContext &ctx, StatRequest *req)
{
    if (!CheckStatReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }
    ObjStat objInfo;
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(MIRROR_SERVER_HDL_STAT_FAIL, &ret, BIO_INNER_RETRY);
    ret = Stat(*req, objInfo);
    LVOS_TP_END;
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

bool MirrorServer::CheckListReq(ListRequest *req)
{
    if (req->size !=sizeof(ObjStat) * 1000U && req->size != 0) {
        return false;
    }
    return true;
}

int32_t MirrorServer::MirrorServerList(ServiceContext &ctx, ListRequest *req)
{
    if (!CheckListReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ERR, nullptr, 0);
        return BIO_OK;
    }
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

bool MirrorServer::CheckLoadReq(LoadRequest *req)
{
    std::string key(req->key);
    if (key.find("..") != std::string::npos) {
        return false;
    }
    if (req->offset != 0) {
        return false;
    }
    if (req->length == 0 || req->length > BIO_IO_MAX_LEN) {
        return false;
    }
    return true;
}

int32_t MirrorServer::MirrorServerLoad(ServiceContext &ctx, LoadRequest *req)
{
    if (!CheckLoadReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(MIRROR_SERVER_HDL_LOAD_FAIL, &ret, BIO_INNER_RETRY);
    ret = Load(*req);
    LVOS_TP_END;
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

int32_t MirrorServer::MirrorServerCreateFlow(ServiceContext &ctx, CreateFlowRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }
    if (flowNum.load() > NO_32 * NO_1000) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }
    BResult result;
    uint64_t flowId = UINT64_MAX;
    BIO_TRACE_START(MIRROR_TRACE_CREATE_FLOW);
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
    BIO_TRACE_END(MIRROR_TRACE_CREATE_FLOW, BIO_OK);
    flowNum++;
    CreateFlowResponse rsp{ flowId, req->isDegrade };
    BioServer::Instance()->GetNetEngine()->Reply(ctx, result, static_cast<void *>(&rsp), sizeof(CreateFlowResponse));
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

    BIO_TRACE_START(MIRROR_TRACE_DESTROY_FLOW);
    auto result = DestroyFlow(req->comm.pid, req->comm.ptId, req->comm.ptv, req->flowId);
    BIO_TRACE_END(MIRROR_TRACE_DESTROY_FLOW, result);
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
    uint64_t sliceLen = sliceP->GetSerializeLen();
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
    rsp->addrNum = addrVec.size();
    for (uint32_t i = 0; i < addrVec.size(); i++) {
        rsp->addr[i].chunkId = addrVec[i].chunkId;
        rsp->addr[i].chunkOffset = addrVec[i].chunkOffset;
        rsp->addr[i].chunkLen = addrVec[i].chunkLen;
        rsp->addrOffset[i] =
            BioServer::Instance()->GetNetEngine()->GetAddressOffset(addrVec[i].chunkId + addrVec[i].chunkOffset);
    }
    rsp->sliceLen = sliceLen;
    uint64_t outSliceLen = 0;
    ret = sliceP->Serialize(rsp->sliceBuf, rsp->sliceLen, outSliceLen);
    if (ret != BIO_OK) {
        LOG_ERROR("Serialize slice failed, ret " << ret);
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }
    if (UNLIKELY(outSliceLen != sliceLen)) {
        LOG_ERROR("Serialize slice failed, outSliceLen:" << outSliceLen << ", sliceLen:" << sliceLen << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
    } else {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, rsp, sizeof(GetSliceResponse) + sliceLen);
    }
    delete[] tmp;
    return BIO_OK;
}

bool MirrorServer::CheckGetSliceReq(GetSliceRequest *req)
{
    if (req->length > IO_SIZE_64M) {
        LOG_ERROR("get slice length too long, length:" << req->length);
        return false;
    }
    return true;
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
    if (!CheckGetSliceReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }
    return MirrorServerGetSlice(ctx, req);
}

int32_t MirrorServer::MirrorServerSyncData(ServiceContext &ctx, SyncDataRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

    BResult ret = SyncData(*req);
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
    if (req->num > SLICE_ADDR_SIZE) {
        LOG_ERROR("Invalid param num: " << req->num << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    for (uint32_t idx = 0; idx < req->num; idx++) {
        auto addr = BioServer::Instance()->GetNetEngine()->GetShmAddress(req->addr[idx], 0);
        BioServer::Instance()->GetNetEngine()->FreeLocalMrSingle(reinterpret_cast<uintptr_t>(addr));
    }
    BioServer::Instance()->GetNetEngine()->RemoveGetHolder(req->comm.srcNid, req->comm.pid, false);

    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, nullptr, 0);
    return BIO_OK;
}

bool MirrorServer::CheckFreeMemReq(FreeMemRequest *req)
{
    LVOS_TP_START(MIRRIR_SERVER_CHECK_FREE_MEM_REQ_PASS_CHECK, 0);
    bool ckRet = CheckAll(req->comm);
    if (!ckRet) {
        return ckRet;
    }
    LVOS_TP_END;

    if (req->num > SLICE_ADDR_SIZE) {
        LOG_ERROR("Invalid param num: " << req->num << ".");
        return false;
    }
    return true;
}

int32_t MirrorServer::HandleFreeMem(ServiceContext &ctx)
{
#ifndef DEBUG_UT
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
#else
    FreeMemRequest *req = new FreeMemRequest();
    req->comm.magic = NO_1;
#endif

    if (!CheckFreeMemReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }
    return MirrorServerFreeMem(ctx, req);
}

int32_t MirrorServer::MirrorServerNotifyUpdate(ServiceContext &ctx, NotifyUpdateRequest *req)
{
    auto ret = NotifyUpdate(*req);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
    return BIO_OK;
}

bool MirrorServer::CheckNotifyUpdateReq(NotifyUpdateRequest *req)
{
    return CheckMagic(req->comm);
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
    if (!CheckNotifyUpdateReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }
    return MirrorServerNotifyUpdate(ctx, req);
}

int32_t MirrorServer::MirrorServerCheckUpdateReady(ServiceContext &ctx, CheckUpdateReadyRequest *req)
{
    CheckUpdateReadyResponse rsp;
    auto ret = CheckUpdateReady(*req, rsp);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(CheckUpdateReadyResponse));
    return BIO_OK;
}

bool MirrorServer::CheckUpdateReadyReq(CheckUpdateReadyRequest *req)
{
    return CheckMagic(req->comm);
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
    if (!CheckUpdateReadyReq(req)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }
    return MirrorServerCheckUpdateReady(ctx, req);
}

int32_t MirrorServer::MirrorServerCheckRemoteUpdateReady(ServiceContext &ctx, CheckRemoteUpdateReadyRequest *req)
{
    if (UNLIKELY(!CheckAll(req->comm))) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        return BIO_OK;
    }

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

int32_t MirrorServer::MirrorServerGetUnderFsConfig(ServiceContext &ctx, GetUnderFsConfigRequest *req)
{
    if (UNLIKELY(req->comm.magic != MESSAGE_MAGIC)) {
        LOG_ERROR("Check message magic failed.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    GetUnderFsConfigResponse rsp;

    std::shared_ptr<UnderFsConfig> underFsConfig = UnderFsConfig::Instance();
    if (!underFsConfig) {
        LOG_ERROR("Mirror server get underfs config failed.");
        return BIO_ALLOC_FAIL;
    }
    BioConfig::UnderFsConfig config = underFsConfig->GetUnderFsConfig();

    int32_t ret = memcpy_s(rsp.underFsType, KEY_MAX_SIZE, config.underFsType.c_str(), config.underFsType.size());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.underFsType[config.underFsType.size()] = '\0';

    ret = memcpy_s(rsp.hdfsConfig.nameNode, KEY_MAX_SIZE, config.hdfsConfig.nameNode.c_str(),
        config.hdfsConfig.nameNode.size());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.hdfsConfig.nameNode[config.hdfsConfig.nameNode.size()] = '\0';

    ret = memcpy_s(rsp.hdfsConfig.workingPath, KEY_MAX_SIZE, config.hdfsConfig.workingPath.c_str(),
        config.hdfsConfig.workingPath.size());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.hdfsConfig.workingPath[config.hdfsConfig.workingPath.size()] = '\0';

    ret = memcpy_s(rsp.cephConfig.user, KEY_MAX_SIZE, config.cephConfig.user.c_str(), config.cephConfig.user.size());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.cephConfig.user[config.cephConfig.user.size()] = '\0';

    ret = memcpy_s(rsp.cephConfig.cfgPath, KEY_MAX_SIZE, config.cephConfig.cfgPath.c_str(),
        config.cephConfig.cfgPath.size());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.cephConfig.cfgPath[config.cephConfig.cfgPath.size()] = '\0';

    ret = memcpy_s(rsp.cephConfig.cluster, KEY_MAX_SIZE, config.cephConfig.cluster.c_str(),
        config.cephConfig.cluster.size());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.cephConfig.cluster[config.cephConfig.cluster.size()] = '\0';

    ret = memcpy_s(rsp.cephConfig.pool, KEY_MAX_SIZE, config.cephConfig.pools.at(0).c_str(),
        config.cephConfig.pools.at(0).size());
    ChkTrue(ret == BIO_OK, ret, "Memory copy failed.");
    rsp.cephConfig.pool[config.cephConfig.pools.at(0).size()] = '\0';

    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(GetUnderFsConfigResponse));
    return BIO_OK;
}

int32_t MirrorServer::HandleGetUnderFsConfig(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetUnderFsConfigRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive sync data message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<GetUnderFsConfigRequest *>(ctx.MessageData());
    return MirrorServerGetUnderFsConfig(ctx, req);
}

int32_t MirrorServer::HandleEvictNegotiateRequest(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(EvictNegotiateRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive consult evict message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto req = static_cast<EvictNegotiateRequest *>(ctx.MessageData());
    return MirrorServerEvictNegotiate(ctx, req);
}

int32_t MirrorServer::MirrorServerEvictNegotiate(ServiceContext &ctx, EvictNegotiateRequest *req)
{
    if (req->count > MAX_EVICT_CONSULT_SIZE) {
        LOG_ERROR("Invalid param count: " << req->count << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }
    EvictNegotiateResponse rsp;
    std::vector<bool> result(req->count);
    auto ret = Cache::Instance().EvictNegotiate(req->flowId, req->data, result, req->count);
    if (ret == BIO_OK) {
        for (uint32_t idx = 0; idx < result.size(); idx++) {
            rsp.negoResult[idx] = result[idx];
        }
    }
    BioServer::Instance()->GetNetEngine()->Reply(ctx, ret, &rsp, sizeof(EvictNegotiateResponse));
    return BIO_OK;
}
