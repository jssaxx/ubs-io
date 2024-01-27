/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <functional>
#include <string>
#include <vector>
#include "bio_log.h"
#include "bio_trace.h"
#include "message_op.h"
#include "bio_client.h"
#include "bio_server.h"
#include "mirror_client.h"

using namespace ock::bio;

BResult MirrorClient::SendCreateFlowRequest(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId)
{
    CreateFlowRequest req{};
    if (opType == 0) {
        req = { RequestComm(ptId, ptEntry.version, mLocalNid.VNodeId()), opType, ptId, 0 };
    } else if (opType == 1) {
        req = { RequestComm(ptId, ptEntry.version, mLocalNid.VNodeId()), opType, ptId, flowId };
    }

    BResult ret = BIO_OK;
    uint64_t retFlowId = UINT64_MAX;
    do {
        ret = BioClient::Instance()->SendSync<CreateFlowRequest, uint64_t>(static_cast<BioNodeId>(nodeId),
            BIO_OP_SDK_CREATE_FLOW, req, retFlowId, false);
        if (UNLIKELY(ret == BIO_NOT_READY)) {
            LOG_WARN("Remote cache service not ready, need retry, ret:" << ret << ", nodeId:" << nodeId << ", ptId:" << ptId << ".");
            sleep(NO_3);
        }
    } while (ret == BIO_NOT_READY);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send sync create flow request failed, ret:" << ret << ", nodeId:" << nodeId << ", ptId:" << ptId << ".");
        return ret;
    }

    if (opType == 0) {
        flowId = retFlowId;
    } else if (opType == 1 && retFlowId != 0) {
        ret = BIO_ERR;
    }
    return ret;
}

BResult MirrorClient::CreateFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId)
{
    if (nodeId == mLocalNid.VNodeId()) {
        if (mDeployType == 1) {
            if (opType == 0) {
                return MirrorServer::Instance()->CreateFlowMaster(getpid(), ptId, flowId);
            } else {
                return MirrorServer::Instance()->CreateFlowSlave(getpid(), ptId, flowId);
            }
        } else {
            LOG_WARN("Not support get remote flow.");
            return BIO_ERR;
        }
    } else {
        return SendCreateFlowRequest(nodeId, ptEntry, ptId, opType, flowId);
    }
}

BResult MirrorClient::CreateFlow(uint16_t ptId)
{
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get pt entry failed, ret:" << ret << ", ptId:" << ptId << ".");
        return BIO_CHECK_PT_FAIL;
    }

    uint64_t flowId = UINT64_MAX;
    ret = CreateFlowImpl(ptEntry.masterNodeId, ptEntry, ptId, 0, flowId);
    if (UNLIKELY(ret != BIO_OK || flowId == UINT64_MAX)) {
        LOG_ERROR("Create master flow failed, ret:" << ret << ", ptId:" << ptId << ", masterNid:" <<
            ptEntry.masterNodeId << ".");
        return ret;
    }

    for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
        if (ptEntry.copys[idx].nodeId == ptEntry.masterNodeId) {
            continue;
        }
        ret = CreateFlowImpl(ptEntry.copys[idx].nodeId, ptEntry, ptId, 1, flowId);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Create slave flow failed, ret:" << ret << ", ptId:" << ptId << ", slaveNid:" <<
                ptEntry.copys[idx].nodeId << ".");
            // TODO::rollback created flow instance
            return ret;
        }
    }

    Insert(ptId, flowId);
    LOG_DEBUG("Create flow instance success, ptId:" << ptId << ", flowId:" << flowId << ".");
    return BIO_OK;
}

BResult MirrorClient::DestroyFlow(uint16_t ptId)
{
    return BIO_OK;
}

BResult MirrorClient::LoadAffinityFlow()
{
    std::vector<uint16_t> ptVec = ListLocalAffinityPt();
    for (uint16_t &ptId : ptVec) {
        BResult ret = CreateFlow(ptId);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Create affinity flow instance failed, ret:" << ret << ", ptId:" << ptId << ".");
        }
    }
    return BIO_OK;
}

BResult MirrorClient::LoadOriginView()
{
    if (mDeployType == 1) {
        mLocalNid = BioServer::Instance()->GetLocalNid();
        mNodeView = BioServer::Instance()->GetNodeView();
        mPtView = BioServer::Instance()->GetPtView();
        if (UNLIKELY(mPtView.empty())) {
            LOG_ERROR("Load pt view failed.");
            return BIO_ERR;
        }
        return BIO_OK;
    } else {
        LOG_WARN("Not support get remote view.");
        return BIO_ERR;
    }
}

std::vector<uint16_t> MirrorClient::ListLocalAffinityPt()
{
    std::vector<uint16_t> ans;
    for (auto &item : mPtView) {
        if (mLocalNid.VNodeId() == item.second.masterNodeId) {
            ans.emplace_back(item.first);
        }
    }
    return ans;
}

uint16_t MirrorClient::SelectingPt(uint64_t objectId, AffinityStrategy affinity)
{
    size_t v = std::hash<std::uint64_t>{}(objectId);
    uint16_t ptId = UINT16_MAX;
    if (LIKELY(affinity == LOCAL_AFFINITY)) {
        std::vector<uint16_t> ptVec = ListLocalAffinityPt();
        if (!ptVec.empty()) {
            ptId = ptVec[v % ptVec.size()];
        }
    } else if (affinity == GLOBAL_BALANCE && !mPtView.empty()) {
        ptId = mPtView[v % mPtView.size()].ptId;
    } else {
        LOG_ERROR("Invalid affinity type or pt view is empty, objectId:" << objectId << ", affinity:" << affinity << ".");
    }
    if (UNLIKELY(ptId == UINT16_MAX)) {
        LOG_ERROR("Selecting pt failed, objectId:" << objectId << ", affinity:" << affinity << ".");
    }
    return ptId;
}

BResult MirrorClient::GetPtEntry(uint16_t ptId, CmPtInfo &ptEntry)
{
    auto iter = mPtView.find(ptId);
    if (UNLIKELY(iter == mPtView.end())) {
        LOG_ERROR("Invalid pt id:" << ptId << ".");
        return BIO_CHECK_PT_FAIL;
    }
    BResult ret = BIO_OK;
    if (UNLIKELY(iter->second.state != CM_PT_NORMAL)) {
        ret = BIO_CHECK_PT_FAIL;
    } else {
        ptEntry = iter->second;
    }
    return ret;
}

uint16_t MirrorClient::ParseLocation(Bio::ObjLocation location)
{
    return static_cast<uint16_t>(location.location[0]);
}

BResult MirrorClient::Initialize()
{
    BResult ret = LoadOriginView();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to load origin view, ret:" << ret << ".");
        return ret;
    }

    ret = LoadAffinityFlow();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to load affinity flow instance, ret:" << ret << ".");
        return ret;
    }

    return BIO_OK;
}

BResult MirrorClient::Put(MirrorPut &param)
{
    BIO_TRACE_START(SDK_TRACE_PUT_GET_PT);
    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    BIO_TRACE_END(SDK_TRACE_PUT_GET_PT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return BIO_CHECK_PT_FAIL;
    }

    uint64_t flowId = UINT64_MAX;
    uint64_t offset = UINT64_MAX;
    uint64_t flowIndex = UINT64_MAX;
    BIO_TRACE_START(SDK_TRACE_PUT_ALLOC_OFF);
    ret = AllocPutOffset(ptId, param.length, flowId, offset, flowIndex);
    BIO_TRACE_END(SDK_TRACE_PUT_ALLOC_OFF, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Alloc put offset failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return ret;
    }
    LOG_INFO("Put location info, ptId:" << ptId << ", flowId:" << flowId << ", offset:" << offset << ", key:" << param.key << ".");

    BIO_TRACE_START(SDK_TRACE_PUT_SEND);
    ret = SendPutRequest(ptEntry, param, flowId, offset, flowIndex);
    BIO_TRACE_END(SDK_TRACE_PUT_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send put request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
    }
    return ret;
}

BResult MirrorClient::Get(MirrorGet &param, uint64_t &realLen)
{
    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return ret;
    }

    BIO_TRACE_START(SDK_TRACE_GET_SEND);
    GetRequest req = { RequestComm(ptId, ptEntry.version, mLocalNid.VNodeId()), param.key, ptId, param.offset, param.length, BioMrInfo() };
    ret = SendGetRequest(ptEntry, req, param.value, realLen);
    BIO_TRACE_END(SDK_TRACE_GET_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send get request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
    }
    return ret;
}

BResult MirrorClient::DeleteKey(const char *key, const Bio::ObjLocation &location)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return BIO_CHECK_PT_FAIL;
    }

    DeleteRequest req = { RequestComm(ptId, ptEntry.version, mLocalNid.VNodeId()), key, ptId };
    ret = SendDeleteRequest(ptEntry, req);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send delete request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
    }
    return ret;
}

BResult MirrorClient::Load(const char *key, uint64_t offset, uint64_t length, const Bio::ObjLocation &location, const Bio::LoadCallback &callback,
    void *context)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return ret;
    }

    LoadRequest req = { RequestComm(ptId, ptEntry.version, mLocalNid.VNodeId()), key, ptId, offset, length };
    ret = SendLoadRequest(ptEntry, req, callback, context);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send stat request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
    }
    return ret;
}

Bio::ObjStat MirrorClient::StatObject(const char *key, const Bio::ObjLocation &location)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return { 0, 0 };
    }

    Bio::ObjStat info = { 0, 0 };
    StatRequest req = { RequestComm(ptId, ptEntry.version, mLocalNid.VNodeId()), key, ptId };
    ret = SendStatRequest(ptEntry, req, info);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send stat request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return { 0, 0 };
    }

    return info;
}

BResult MirrorClient::AllocPutOffset(uint16_t ptId, uint64_t len, uint64_t &flowId, uint64_t &offset, uint64_t &index)
{
    FlowInstance *flowInst = Query(ptId);
    if (UNLIKELY(flowInst == nullptr)) {
        BResult ret = CreateFlow(ptId);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Create flow instance failed, ret: " << ret << ", ptId:" << ptId << ".");
            return ret;
        }
        flowInst = Query(ptId);
        if (UNLIKELY(flowInst == nullptr)) {
            LOG_ERROR("Query flow failed, ptId:" << ptId << ".");
            return BIO_INNER_ERR;
        }
    }

    flowId = flowInst->FlowId();
    offset = flowInst->AllocOffset(len, index);
    return BIO_OK;
}

void MirrorClient::ConstructPutReq(uint8_t *tmp, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t offset, uint64_t index,
    WCacheSlicePtr &slice) const
{
    auto *req = static_cast<PutRequest *>(static_cast<void *>(tmp));
    req->Fill(RequestComm(ptEntry.ptId, ptEntry.version, mLocalNid.VNodeId()), param.attr, param.key, param.length,
        flowId, offset, index, BioClient::Instance()->GetLocalMrKey(), slice->GetSerializeLen());
    uint32_t len = 0;
    char *sliceBuf = static_cast<char *>(static_cast<void *>(tmp)) + sizeof(PutRequest);
    slice->Serialize(sliceBuf, len);
}

BResult MirrorClient::Prepare(CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t offset, uint64_t index,
    PutRequest *&req)
{
    if (mDeployType == 1) {
        WCacheSlicePtr sliceP = nullptr;
        BResult ret = MirrorServer::Instance()->GetSlice(flowId, offset, index, param.length, sliceP);
        if (UNLIKELY(ret != BIO_OK || sliceP == nullptr)) {
            LOG_ERROR("Get slice failed, ret:" << ret << ", flowId:" << flowId << ", flowOffset:" << offset <<
                ", length:" << param.length << ".");
            return ret;
        }
        ret = mSliceOp.Copy(param.value, sliceP.Get());
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Copy data failed, ret:" << ret << ", key:" << param.key << ", flowId:" << flowId <<
                ", flowOffset:" << offset << ", length:" << param.length << ".");
            return ret;
        }
        LOG_INFO("Get slice success, key:" << param.key << ", flowId:" << sliceP->GetFlowId() << ", offsetInFlow:" <<
            sliceP->GetOffsetInFlow() << ", indexInFlow:" << sliceP->GetIndexInFlow() << ", slice: " <<
            sliceP->ToString() << ".");

        uint32_t sliceLen = sliceP->GetSerializeLen();
        auto *tmp = new (std::nothrow) uint8_t[sizeof(PutRequest) + sliceLen];
        if (UNLIKELY(tmp == nullptr)) {
            LOG_ERROR("Alloc put request memory failed, len:" << sizeof(PutRequest) + sliceLen << ".");
            return BIO_INNER_ERR;
        }
        ConstructPutReq(tmp, ptEntry, param, flowId, offset, index, sliceP);
        req = static_cast<PutRequest *>(static_cast<void *>(tmp));
        return BIO_OK;
    } else {
        LOG_WARN("Not support prepare put remote.");
        return BIO_INNER_ERR;
    }
}

void MirrorClient::PutRemote(PutRequest *req, CmPtInfo &ptEntry, std::vector<uint32_t> &index,
    RpcEngine::Callback &callback)
{
    for (uint32_t i = 0; i < index.size(); i++) {
        uint16_t dstNid = ptEntry.copys[index[i]].nodeId;
        BioClient::Instance()->SendAsyncBuff(static_cast<BioNodeId>(dstNid), BIO_OP_SDK_PUT, static_cast<void *>(req),
            sizeof(PutRequest) + req->sliceLen, callback, false);
    }
}

void MirrorClient::PutLocal(PutRequest *req, uint32_t localIdx, RpcEngine::Callback &callback) const
{
    if (localIdx == UINT32_MAX) {
        return;
    }
    if (mDeployType == 1) {
        WCacheSlicePtr sliceP = MakeRef<WCacheSlice>();
        char *sliceBuf = static_cast<char *>(static_cast<void *>(req)) + sizeof(PutRequest);
        sliceP->Deserialize(sliceBuf, req->sliceLen);
        BResult ret = MirrorServer::Instance()->Put(*req, sliceP);
        callback.cb(callback.cbCtx, nullptr, 0, ret);
    } else {
        LOG_WARN("Not support put local by ipc.");
        callback.cb(callback.cbCtx, nullptr, 0, BIO_INNER_ERR);
    }
}

BResult MirrorClient::SendPutRequest(CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t offset, uint64_t index)
{
    uint32_t quota = ptEntry.copys.size();
    ClientCallbackCtx cbCtx = { BIO_OK, quota };
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        }
        if ((--cbCtx->quota) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    RpcEngine::Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE);
    PutRequest *req = nullptr;
    BResult ret = Prepare(ptEntry, param, flowId, offset, index, req);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Prepare put local failed, ret:" << ret << ", key:" << param.key << ", length:" << param.length <<
            ", flowId:" << flowId << ", flowOffset:" << offset << ".");
        return ret;
    }

    uint32_t localIdx = UINT32_MAX;
    std::vector<uint32_t> remoteIdx;
    for (uint32_t idx = 0; idx < quota; idx++) {
        if (ptEntry.copys[idx].nodeId == mLocalNid.VNodeId()) {
            localIdx = idx;
        } else {
            remoteIdx.emplace_back(idx);
        }
    }
    PutRemote(req, ptEntry, remoteIdx, callback);
    PutLocal(req, localIdx, callback);

    sem_wait(&cbCtx.sem);
    sem_destroy(&cbCtx.sem);
    if (UNLIKELY(cbCtx.result != BIO_OK)) {
        LOG_ERROR("Send put request failed, ret:" << cbCtx.result << ", key:" << param.key << ", flowId:" << flowId <<
            ", offset:" << offset << ", length:" << param.length << ".");
    }
    delete[] static_cast<uint8_t *>(static_cast<void *>(req));
    return cbCtx.result;
}

BResult MirrorClient::GetMaster(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen)
{
    LOG_INFO("Get value from master, key:" << req.key << ", offset:" << req.offset << ", length:" << req.length <<
        ", masterNid:" << masterNid << ", localNid:" << mLocalNid.VNodeId() << ".");

    if (masterNid == mLocalNid.VNodeId()) {
        BIO_TRACE_START(SDK_TRACE_GET_LOCAL);
        req.SetMrInfo(value, req.length);
        BResult ret = MirrorServer::Instance()->Get(req, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_LOCAL, ret);
        return ret;
    }

    BioMrInfo mrInfo;
    BResult ret = BioClient::Instance()->Alloc(req.length, mrInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Alloc rdma page failed, ret:" << ret << ", length:" << req.length << ", page size:" <<
            BioClient::Instance()->GetDataPage() << ".");
        return BIO_ALLOC_FAIL;
    }
    req.SetMrInfo(mrInfo);
    LOG_INFO("Alloc rdma page success, length:" << req.length << ", offset:" << req.offset << ", address:" <<
        mrInfo.address << ", size:" << mrInfo.size << ", key:" << mrInfo.key << ", dstNid:" << masterNid << ".");

    BIO_TRACE_START(SDK_TRACE_GET_REMOTE);
    uint64_t length;
    ret = BioClient::Instance()->SendSync<GetRequest, uint64_t>(static_cast<BioNodeId>(masterNid), BIO_OP_SDK_GET,
        req, length, false);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send sync get request failed, ret:" << ret << ", key:" << req.key << ", offset:" << req.offset <<
            ", length:" << req.length << ", dstNid:" << masterNid << ".");
    } else {
        realLen = length;
    }
    BIO_TRACE_END(SDK_TRACE_GET_REMOTE, ret);
    if (LIKELY(ret == BIO_OK)) {
        BIO_TRACE_START(SDK_TRACE_GET_COPY2U);
        Copy(reinterpret_cast<void *>(mrInfo.address), value, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_COPY2U, BIO_OK);
    }

    BioClient::Instance()->Free(mrInfo.address);
    return ret;
}

BResult MirrorClient::SendGetRequest(CmPtInfo &ptEntry, GetRequest &req, char *value, uint64_t &realLen)
{
    return GetMaster(req, ptEntry.masterNodeId, value, realLen);
}

void MirrorClient::DeleteRemote(DeleteRequest &req, CmPtInfo &ptEntry, uint32_t index, RpcEngine::Callback &callback)
{
    uint16_t dstNid = ptEntry.copys[index].nodeId;
    BioClient::Instance()->SendAsync<DeleteRequest>(static_cast<BioNodeId>(dstNid), BIO_OP_SDK_DELETE, req, callback,
        false);
}

void MirrorClient::DeleteLocal(DeleteRequest &req, RpcEngine::Callback &callback) const
{
    if (mDeployType == 1) {
        BResult ret = MirrorServer::Instance()->Delete(req);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Delete local failed, ret:" << ret << ", key:" << req.key << ".");
        }
        callback.cb(callback.cbCtx, nullptr, 0, ret);
    } else {
        LOG_WARN("Not support delete local by ipc.");
        callback.cb(callback.cbCtx, nullptr, 0, BIO_INNER_ERR);
    }
}

BResult MirrorClient::SendDeleteRequest(CmPtInfo &ptEntry, DeleteRequest &req)
{
    uint32_t quota = ptEntry.copys.size();
    ClientCallbackCtx cbCtx = { BIO_OK, quota };
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        }
        if ((--cbCtx->quota) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    RpcEngine::Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    for (uint32_t idx = 0; idx < quota; idx++) {
        if (ptEntry.copys[idx].nodeId == mLocalNid.VNodeId()) {
            DeleteLocal(req, callback);
        } else {
            DeleteRemote(req, ptEntry, idx, callback);
        }
    }

    sem_wait(&cbCtx.sem);
    sem_destroy(&cbCtx.sem);
    if (UNLIKELY(cbCtx.result != BIO_OK)) {
        LOG_ERROR("Send delete request failed, ret:" << cbCtx.result << ", key:" << req.key << ".");
    }
    return cbCtx.result;
}

BResult MirrorClient::StatRemote(uint16_t dstNid, StatRequest &req, Bio::ObjStat &objInfo)
{
    BResult ret = BioClient::Instance()->SendSync<StatRequest, Bio::ObjStat>(static_cast<BioNodeId>(dstNid),
        BIO_OP_SDK_STAT, req, objInfo, false);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send sync stat request failed, ret:" << ret << ", key:" << req.key << ", dstNid:" << dstNid << ".");
        return ret;
    }
    return ret;
}

BResult MirrorClient::StatLocal(StatRequest &req, Bio::ObjStat &objInfo) const
{
    if (mDeployType == 1) {
        return MirrorServer::Instance()->Stat(req, objInfo);
    } else {
        LOG_WARN("Not support stat local by ipc.");
        return BIO_INNER_ERR;
    }
}

BResult MirrorClient::SendStatRequest(CmPtInfo &ptEntry, StatRequest &req, Bio::ObjStat &objInfo)
{
    uint16_t dstNid = ptEntry.masterNodeId;
    if (dstNid == mLocalNid.VNodeId()) {
        return StatLocal(req, objInfo);
    } else {
        return StatRemote(dstNid, req, objInfo);
    }
}

BResult MirrorClient::LoadMaster(LoadRequest &req, uint16_t masterNid, const Bio::LoadCallback &callback, void *context)
{
    uint16_t ptId = req.comm.ptId;
    auto cbFunc = [ptId, callback, context](void *ctx, void *resp, uint32_t len, int32_t result) {
        if (UNLIKELY(result != BIO_OK)) {
            LOG_ERROR("Load master return failed, ret:" << result << ".");
        }
        if (callback != nullptr) {
            callback(context, ((result == BIO_OK) ? RET_CACHE_OK : RET_CACHE_ERROR));
        }
    };
    RpcEngine::Callback cb(cbFunc, nullptr);

    BioClient::Instance()->SendAsyncBuff(static_cast<BioNodeId>(masterNid), BIO_OP_SDK_LOAD, static_cast<void *>(&req),
        sizeof(LoadRequest), cb, false);
    return BIO_OK;
}

BResult MirrorClient::SendLoadRequest(CmPtInfo &ptEntry, LoadRequest &req, const Bio::LoadCallback &callback, void *context)
{
    return LoadMaster(req, ptEntry.masterNodeId, callback, context);
}