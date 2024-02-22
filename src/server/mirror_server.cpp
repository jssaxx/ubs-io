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
    auto netEngine = BioServer::Instance()->GetRpcEngine();
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
    netEngine->RegisterNewRequestHandler(BIO_OP_SERVER_SYNC_DATA,
        std::bind(&MirrorServer::HandleSyncData, this, std::placeholders::_1));
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

BResult MirrorServer::CreateFlow(uint64_t procId, uint16_t ptId, uint64_t flowId)
{
    auto ret = Cache::Instance().CreateWCache(procId, ptId, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create write cache failed, ret:" << ret << ", procId:" << procId << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = Cache::Instance().CreateRCache(ptId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Create read cache failed, ret:" << ret << ", ptId:" << ptId << ".");
    } else {
        LOG_INFO("Create read cache success, procId:" << procId << ", ptId:" << ptId << ", flowId:" << flowId);
    }

    return ret;
}

BResult MirrorServer::CreateFlowMaster(uint64_t procId, uint16_t ptId, uint64_t &flowId)
{
    auto ret = Cache::Instance().AllocateFlowId(procId, ptId, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Alloc flow id failed, ret:" << ret << ", procId:" << procId << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = CreateFlow(procId, ptId, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    return ret;
}

BResult MirrorServer::CreateFlowSlave(uint64_t procId, uint16_t ptId, uint64_t flowId)
{
    auto ret = CreateFlow(procId, ptId, flowId);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    
    return ret;
}

BResult MirrorServer::GetSlice(uint64_t flowId, uint64_t flowOffset, uint64_t flowIndex, uint64_t length, WCacheSlicePtr &slice)
{
    SliceKey sliceKey(flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    return Cache::Instance().GetWCacheSlice(sliceKey, slice);
}

BResult MirrorServer::Put(PutRequest &req, const WCacheSlicePtr &sliceP)
{
    std::string key = req.Key();
    uint32_t dstNid = req.comm.srcNid;
    uint32_t localNid = BioServer::Instance()->GetLocalNid().VNodeId();
    uint32_t rMrKey = req.mrKey;
    std::vector<FlowAddr> rFlowAddr = sliceP->GetAddrs();
    LOG_INFO("Mirror server put, key:" << key << ", srcNid:" << dstNid << ", flowId:" << sliceP->GetFlowId() <<
        ", offsetInFlow:" << sliceP->GetOffsetInFlow() << ", indexInFlow:" << sliceP->GetIndexInFlow() <<
        ", slice: " << sliceP->ToString() << ", rFlowSize:" << rFlowAddr.size() << ".");

    auto reader = [dstNid, localNid, rMrKey](const SlicePtr &from, const SlicePtr &to) -> BResult {
        if (dstNid == localNid) {
            return BIO_OK;
        }

        BIO_TRACE_START(MIRROR_TRACE_PUT_READ_DATA);
        std::vector<FlowAddr> rFlowAddr = from->GetAddrs();
        std::vector<NetMrInfo> rMrVec;
        for (auto & addr : rFlowAddr) {
            MrInfo mr{};
            addr.ToMrInfo(mr);
            NetMrInfo bioMr(mr.address, mr.size, rMrKey);
            rMrVec.emplace_back(bioMr);
        }
        std::vector<FlowAddr> lFlowAddr = to->GetAddrs();
        std::vector<NetMrInfo> lMrVec;
        for (auto & addr : lFlowAddr) {
            MrInfo mr{};
            addr.ToMrInfo(mr);
            NetMrInfo bioMr(mr.address, mr.size, BioServer::Instance()->GetLocalMrKey());
            lMrVec.emplace_back(bioMr);
        }

        for (uint32_t idx = 0; idx < rMrVec.size(); idx++) {
            NetRequest readReq(lMrVec[idx].address, rMrVec[idx].address, lMrVec[idx].key, rMrVec[idx].key, lMrVec[idx].size);
            BResult ret = BioServer::Instance()->GetRpcEngine()->SyncRead(static_cast<BioNodeId>(dstNid), readReq);
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
    BResult ret = Cache::Instance().Put(const_cast<char *>(key.c_str()), sliceP, reader, req.attr);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Put to write cache failed, ret:" << ret << ", key:" << req.key << ".");
    }
    BIO_TRACE_END(MIRROR_TRACE_PUT, ret);
    return ret;
}

BResult MirrorServer::Get(GetRequest &req, uint64_t &realLen)
{
    std::string key = req.Key();
    uint32_t dstNid = req.comm.srcNid;
    uint32_t localNid = BioServer::Instance()->GetLocalNid().VNodeId();
    uint32_t rKey = req.mr.key;

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
            NetRequest writeReq(lMrVec[idx].address, rMrVec[0].address + off, lMrVec[idx].key, rMrVec[idx].key, lMrVec[idx].size);
            ret = BioServer::Instance()->GetRpcEngine()->SyncWrite(static_cast<BioNodeId>(dstNid), writeReq);
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

    MrInfo mrInfo = { req.mr.address, static_cast<uint32_t>(req.mr.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr sliceP = MakeRef<RCacheSlice>(req.ptId, req.length, addrVec);

    std::vector<FlowAddr> rFlowAddr = sliceP->GetAddrs();
    LOG_INFO("Mirror server get, key:" << key << ", srcNid:" << dstNid << ", offset:" << req.offset << ", length:" <<
        req.length << ", mr address:" << req.mr.address << ", mr size:" << req.mr.size << ", mr key:" << req.mr.key <<
        ", slice: " << sliceP->ToString() << ", rFlowSize:" << rFlowAddr.size() << ".");
    BIO_TRACE_START(MIRROR_TRACE_GET);
    BResult ret = Cache::Instance().Get(const_cast<char*>(key.c_str()), req.offset, sliceP, writer, realLen);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get from write cache failed, ret:" << ret << ", key:" << key << ".");
    }
    BIO_TRACE_END(MIRROR_TRACE_GET, ret);
    return ret;
}

BResult MirrorServer::Delete(DeleteRequest &req)
{
    std::string key = req.Key();
    BIO_TRACE_START(MIRROR_TRACE_DEL);
    BResult ret = Cache::Instance().Delete(req.ptId, const_cast<char*>(key.c_str()));
    BIO_TRACE_END(MIRROR_TRACE_DEL, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Delete key failed, ret:" << ret << ", key:" << key << ", ptId:" << req.ptId << ".");
    } else {
        LOG_INFO("Mirror server delete success, key:" << key << ", ptId:" << req.ptId << ".");
    }
    return ret;
}

BResult MirrorServer::Stat(StatRequest &req, Bio::ObjStat &objInfo)
{
    std::string key = req.Key();
    CacheObjStat objStat{};
    BIO_TRACE_START(MIRROR_TRACE_STAT);
    BResult ret = Cache::Instance().Stat(req.ptId, const_cast<char*>(key.c_str()), objStat);
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
    std::string key = req.Key();

    LOG_INFO("Mirror server load, key:" << key << ", ptId:" << req.ptId << ".");
    BIO_TRACE_START(MIRROR_TRACE_LOAD);
    uint64_t realLen = 0;
    BResult ret = Cache::Instance().Load(req.ptId, const_cast<char*>(key.c_str()), req.offset, req.length, realLen);
    BIO_TRACE_END(MIRROR_TRACE_LOAD, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Load key failed, ret:" << ret << ", key:" << key << ".");
    }
    return ret;
}

BResult MirrorServer::SyncData(SyncDataRequest &req)
{
    LOG_INFO("Mirror server sync data, ptId:" << req.comm.ptId << ", version:" << req.comm.ptv);
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
    NetEnginePtr rpcEngine = BioServer::Instance()->GetRpcEngine();

    uint16_t localNodeId = Cm::Instance()->GetCmLocalNodeId().nodeId;
    BResult rspRet;
    SyncDataRequest req = { RequestComm(ptId, version, localNodeId) };
    auto ret = rpcEngine->SyncCall<SyncDataRequest, BResult>(static_cast<BioNodeId>(masterNodeId),
        BIO_OP_SERVER_SYNC_DATA, req, rspRet, false);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send sync sync data failed:" << ret << ", ptId:" << ptId <<
            ", version:" << version);
        return ret;
    }
    return rspRet;
}

BResult MirrorServer::Initialize(int32_t type)
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }

    mDeployType = type;
    RegisterOpcode();

    BResult ret = Cache::Instance().Init();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init cache instance, ret:" << ret << ".");
        return ret;
    }

    mStarted = true;
    return BIO_OK;
}

int32_t MirrorServer::HandleQueryPtView(ServiceContext &ctx)
{
    if (UNLIKELY(!Ready())) {
        Reply(ctx, BIO_NOT_READY, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(QueryViewRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive query pt message len:" << ctx.MessageDataLen() << " or message data invalid.");
        Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_QUERYPTVIEW_HDL);
    auto *req = static_cast<PutRequest *>(ctx.MessageData());
    if (UNLIKELY(!CheckAll(req->comm))) {
        Reply(ctx, BIO_CHECK_PT_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_QUERYPTVIEW_HDL, BIO_CHECK_PT_FAIL);
        return BIO_CHECK_PT_FAIL;
    }

    // TODO: response view info
    Reply(ctx, BIO_INNER_ERR, nullptr, 0);
    BIO_TRACE_END(MIRROR_TRACE_QUERYPTVIEW_HDL, 0);
    return BIO_OK;
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

    WCacheSlicePtr sliceP = MakeRef<WCacheSlice>();
    char *sliceBuf = static_cast<char *>(static_cast<void *>(req)) + sizeof(PutRequest);
    sliceP->Deserialize(sliceBuf, req->sliceLen);
    LOG_INFO("Flow index:" << sliceP->GetIndexInFlow());
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
        result = CreateFlowMaster(req->comm.pid, req->ptId, flowId);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Master create flow failed, ret:" << result << ", ptId:" << req->ptId << ".");
            flowId = UINT64_MAX;
        }
    } else if (req->opType == 1) {
        result = CreateFlowSlave(req->comm.pid, req->ptId, req->flowId);
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Slave create flow failed, ret:" << result << ", ptId:" << req->ptId << ".");
            flowId = UINT64_MAX;
        } else {
            flowId = 0;
        }
    } else {
        LOG_ERROR("Invalid op type, opType:" << req->opType << ", ptId:" << req->ptId << ".");
    }

    Reply(ctx, BIO_OK, static_cast<void *>(&flowId), sizeof(uint64_t));
    BIO_TRACE_END(MIRROR_TRACE_CREATEFLOW_HDL, 0);
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