/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <functional>
#include <string>
#include <vector>
#include "bio_client_log.h"
#include "bio_trace.h"
#include "message_op.h"
#include "bio_client.h"
#include "bio_client_agent.h"
#include "bio_client_net.h"
#include "mirror_client.h"

using namespace ock::bio;

static void CopyKey(char *dstKey, const char *srcKey)
{
    auto ret = memcpy_s(dstKey, KEY_MAX_SIZE, srcKey, sizeof(srcKey));
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("Copy Key failed, key:" << srcKey << ", len:" << sizeof(srcKey) << ".");
    }
}

BResult MirrorClient::SendCreateFlowRequestRemote(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
    uint64_t &flowId)
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
        ret = net::BioClientNet::Instance()->SendSync<CreateFlowRequest, CreateFlowResponse>(
            static_cast<BioNodeId>(nodeId), BIO_OP_SDK_CREATE_FLOW, req, rsp);
        if (UNLIKELY(ret == BIO_NOT_READY)) {
            CLIENT_LOG_WARN("Remote cache service not ready, need retry, ret:" << ret << ", nodeId:" << nodeId <<
                ", ptId:" << ptId << ".");
            sleep(NO_3);
        }
    } while (ret == BIO_NOT_READY);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync create flow request failed, ret:" << ret << ", nodeId:" << nodeId << ", ptId:" <<
            ptId << ".");
        return ret;
    }

    if (opType == 0) {
        flowId = rsp.flowId;
    } else if (opType == 1 && rsp.flowId != 0) {
        ret = BIO_ERR;
    }
    return ret;
}

BResult MirrorClient::CreateFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
    uint64_t &flowId)
{
    if (LIKELY(nodeId == mLocalNid.VNodeId())) {
        return agent::BioClientAgent::Instance()->CreateFlowLocal(getpid(), ptEntry, ptId, opType, flowId);
    } else {
        return SendCreateFlowRequestRemote(nodeId, ptEntry, ptId, opType, flowId);
    }
}

BResult MirrorClient::CreateFlow(uint16_t ptId)
{
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret:" << ret << ", ptId:" << ptId << ".");
        return BIO_CHECK_PT_FAIL;
    }

    uint64_t flowId = UINT64_MAX;
    ret = CreateFlowImpl(ptEntry.masterNodeId, ptEntry, ptId, 0, flowId);
    if (UNLIKELY(ret != BIO_OK || flowId == UINT64_MAX)) {
        CLIENT_LOG_ERROR("Create master flow failed, ret:" << ret << ", ptId:" << ptId << ", masterNid:" <<
            ptEntry.masterNodeId << ".");
        return ret;
    }

    for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
        if (ptEntry.copys[idx].nodeId == ptEntry.masterNodeId) {
            continue;
        }
        ret = CreateFlowImpl(ptEntry.copys[idx].nodeId, ptEntry, ptId, 1, flowId);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Create slave flow failed, ret:" << ret << ", ptId:" << ptId << ", slaveNid:" <<
                ptEntry.copys[idx].nodeId << ".");
            DestroyFlow(ptId);
            return ret;
        }
    }

    Insert(ptId, flowId);
    CLIENT_LOG_DEBUG("Create flow instance success, ptId:" << ptId << ", flowId:" << flowId << ".");
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
            CLIENT_LOG_ERROR("Create affinity flow instance failed, ret:" << ret << ", ptId:" << ptId << ".");
        }
    }
    return BIO_OK;
}

BResult MirrorClient::LoadOriginView()
{
    auto ret = agent::BioClientAgent::Instance()->GetClusterNodeView(mNodeView);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get cluster node view failed, ret:" << ret << ".");
        return ret;
    }
    ret = agent::BioClientAgent::Instance()->GetPtView(mPtView);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get pt view failed, ret:" << ret << ".");
        return ret;
    }
    ret = agent::BioClientAgent::Instance()->GetLocalNodeInfo(mNetProtocol, mLocalNid);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get local node info failed, ret:" << ret << ".");
        return ret;
    }
    return BIO_OK;
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
        CLIENT_LOG_ERROR("Invalid affinity type or pt view is empty, objectId:" << objectId << ", affinity:" <<
            affinity << ".");
    }
    if (UNLIKELY(ptId == UINT16_MAX)) {
        CLIENT_LOG_ERROR("Selecting pt failed, objectId:" << objectId << ", affinity:" << affinity << ".");
    }
    return ptId;
}

BResult MirrorClient::GetPtEntry(uint16_t ptId, CmPtInfo &ptEntry)
{
    auto iter = mPtView.find(ptId);
    if (UNLIKELY(iter == mPtView.end())) {
        CLIENT_LOG_ERROR("Invalid pt id:" << ptId << ".");
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

BResult MirrorClient::Initialize()
{
    return LoadOriginView();
}

BResult MirrorClient::Start()
{
    return LoadAffinityFlow();
}

BResult MirrorClient::Put(MirrorPut &param)
{
    BIO_TRACE_START(SDK_TRACE_PUT_GET_PT);
    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    BIO_TRACE_END(SDK_TRACE_PUT_GET_PT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return BIO_CHECK_PT_FAIL;
    }

    uint64_t flowId = UINT64_MAX;
    uint64_t offset = UINT64_MAX;
    uint64_t flowIndex = UINT64_MAX;
    BIO_TRACE_START(SDK_TRACE_PUT_ALLOC_OFF);
    ret = AllocPutOffset(ptId, param.length, flowId, offset, flowIndex);
    BIO_TRACE_END(SDK_TRACE_PUT_ALLOC_OFF, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc put offset failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return ret;
    }
    CLIENT_LOG_DEBUG("Put location info, ptId:" << ptId << ", flowId:" << flowId << ", offset:" << offset << ", key:" <<
        param.key << ".");

    BIO_TRACE_START(SDK_TRACE_PUT_SEND);
    ret = SendPutRequest(ptEntry, param, flowId, offset, flowIndex);
    BIO_TRACE_END(SDK_TRACE_PUT_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send put request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
    }
    return ret;
}

BResult MirrorClient::Get(MirrorGet &param, uint64_t &realLen)
{
    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return ret;
    }

    GetRequest req;
    req.comm = { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.key, param.key);
    req.ptId = ptId;
    req.offset = param.offset;
    req.length = param.length;
    BIO_TRACE_START(SDK_TRACE_GET_SEND);
    ret = SendGetRequest(ptEntry, req, param.value, realLen);
    BIO_TRACE_END(SDK_TRACE_GET_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send get request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
    }
    return ret;
}

BResult MirrorClient::DeleteKey(const char *key, const Bio::ObjLocation &location)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return BIO_CHECK_PT_FAIL;
    }

    DeleteRequest req{};
    req.comm = { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.key, key);
    ret = SendDeleteRequest(ptEntry, req);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send delete request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
    }
    return ret;
}

BResult MirrorClient::Load(const char *key, uint64_t offset, uint64_t length, const Bio::ObjLocation &location,
    const Bio::LoadCallback &callback, void *context)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return ret;
    }

    LoadRequest req{};
    req.comm = { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.key, key);
    req.offset = offset;
    req.length = length;
    ret = SendLoadRequest(ptEntry, req, callback, context);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send stat request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
    }
    return ret;
}

Bio::ObjStat MirrorClient::StatObject(const char *key, const Bio::ObjLocation &location)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return { 0, 0 };
    }

    Bio::ObjStat info = { 0, 0 };
    StatRequest req{};
    req.comm = { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.key, key);
    ret = SendStatRequest(ptEntry, req, info);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send stat request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
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
            CLIENT_LOG_ERROR("Create flow instance failed, ret: " << ret << ", ptId:" << ptId << ".");
            return ret;
        }
        flowInst = Query(ptId);
        if (UNLIKELY(flowInst == nullptr)) {
            CLIENT_LOG_ERROR("Query flow failed, ptId:" << ptId << ".");
            return BIO_INNER_ERR;
        }
    }

    flowId = flowInst->FlowId();
    offset = flowInst->AllocOffset(len, index);
    return BIO_OK;
}

void MirrorClient::ConstructPutReq(PutRequest *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId,
    uint64_t offset, uint64_t index, GetSliceResponse *rsp) const
{
    req->comm = { MESSAGE_MAGIC, ptEntry.ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    req->tenantId = param.attr.mTenantId;
    req->affinity = param.attr.affinity;
    req->strategy = param.attr.strategy;
    CopyKey(req->key, param.key);
    req->length = param.length;
    req->flowId = flowId;
    req->offset = offset;
    req->index = index;
    req->mrKey = net::BioClientNet::Instance()->GetLocalMrKey();
    req->sliceLen = rsp->sliceLen;
    memcpy_s(req->sliceBuf, rsp->sliceLen, rsp->sliceBuf, rsp->sliceLen);
}

BResult MirrorClient::DataCopy(const char *from, SliceAddrDesc *addr, uint32_t addrNum)
{
    uint64_t offset = 0;
    for (uint32_t i = 0; i < addrNum; i++) {
        auto ret = memcpy_s(reinterpret_cast<void *>(addr[i].chunkId + addr[i].chunkOffset), addr[i].chunkLen,
            reinterpret_cast<void *>(const_cast<char *>(from + offset)), addr[i].chunkLen);
        ChkTrue(ret == BIO_OK, ret,
            "Failed to copy data from addr:" << from + offset << " to addr:" << addr[i].chunkId + addr[i].chunkOffset <<
            " by length:" << addr[i].chunkLen);
        offset += addr[i].chunkLen;
    }
    return BIO_OK;
}

BResult MirrorClient::Prepare(CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t offset, uint64_t index,
    PutRequest *&req)
{
    GetSliceResponse *rsp = nullptr;
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_GET_SLICE);
    auto ret = agent::BioClientAgent::Instance()->PrepareResource(ptEntry, flowId, offset, index, param.length, &rsp);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE_GET_SLICE, BIO_OK);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Prepare resource failed, ret:" << ret << ", key:" << param.key << ", flowId:" << flowId <<
            ", flowOffset:" << offset << ", length:" << param.length << ".");
        return ret;
    }

    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_COPY_DATA);
    ret = DataCopy(param.value, rsp->addr, rsp->addrNum);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE_COPY_DATA, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Copy data failed, ret:" << ret << ", key:" << param.key << ", flowId:" << flowId <<
            ", flowOffset:" << offset << ", length:" << param.length << ".");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        return ret;
    }

    auto *tmp = new (std::nothrow) uint8_t[sizeof(PutRequest) + rsp->sliceLen];
    if (UNLIKELY(tmp == nullptr)) {
        CLIENT_LOG_ERROR("Alloc put request memory failed, len:" << sizeof(PutRequest) + rsp->sliceLen << ".");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        return BIO_INNER_ERR;
    }
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_SLICE_SERIALIZATION);
    req = static_cast<PutRequest *>(static_cast<void *>(tmp));
    ConstructPutReq(req, ptEntry, param, flowId, offset, index, rsp);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE_SLICE_SERIALIZATION, ret);
    CLIENT_LOG_DEBUG("Put request, key:" << req->key << ", length:" << req->length << ", flowId:" << req->flowId <<
        ", offset:" << req->offset << ", index:" << req->index << ", sliceLen:" << req->sliceLen);
    delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
    return BIO_OK;
}

void MirrorClient::PutRemote(PutRequest *req, CmPtInfo &ptEntry, std::vector<uint32_t> &index,
    NetEngine::Callback &callback)
{
    for (uint32_t i = 0; i < index.size(); i++) {
        uint16_t dstNid = ptEntry.copys[index[i]].nodeId;
        net::BioClientNet::Instance()->SendAsyncBuff(static_cast<BioNodeId>(dstNid), BIO_OP_SDK_PUT,
            static_cast<void *>(req), sizeof(PutRequest) + req->sliceLen, callback);
    }
}

void MirrorClient::PutLocal(PutRequest *req, uint32_t localIdx, NetEngine::Callback &callback) const
{
    if (UNLIKELY(localIdx == UINT32_MAX)) {
        return;
    }
    agent::BioClientAgent::Instance()->PutLocal(req, localIdx, callback);
}

BResult MirrorClient::SendPutRequest(CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t offset,
    uint64_t index)
{
    uint32_t quota = ptEntry.copys.size();
    ClientCallbackCtx cbCtx;
    cbCtx.result = BIO_OK;
    cbCtx.quota = quota;
    sem_init(&cbCtx.sem, 0, 0);
    cbCtx.resp = nullptr;
    cbCtx.respLen = 0;
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    NetEngine::Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE);
    PutRequest *req = nullptr;
    BResult ret = Prepare(ptEntry, param, flowId, offset, index, req);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Prepare put local failed, ret:" << ret << ", key:" << param.key << ", length:" <<
            param.length << ", flowId:" << flowId << ", flowOffset:" << offset << ".");
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
        CLIENT_LOG_ERROR("Send put request failed, ret:" << cbCtx.result << ", key:" << param.key << ", flowId:" <<
            flowId << ", offset:" << offset << ", length:" << param.length << ".");
    }
    delete[] static_cast<uint8_t *>(static_cast<void *>(req));
    return cbCtx.result;
}

BResult MirrorClient::GetMaster(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen)
{
    if (masterNid == mLocalNid.VNodeId()) {
        BIO_TRACE_START(SDK_TRACE_GET_LOCAL);
        req.isMr = 0;
        req.address = reinterpret_cast<uintptr_t>(value);
        req.size = req.length;
        BResult ret = agent::BioClientAgent::Instance()->GetLocal(req, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_LOCAL, ret);
        return ret;
    }

    NetMrInfo mrInfo;
    BResult ret = net::BioClientNet::Instance()->Alloc(req.length, mrInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc rdma page failed, ret:" << ret << ", length:" << req.length << ", page size:" <<
            net::BioClientNet::Instance()->GetDataPage() << ".");
        return BIO_ALLOC_FAIL;
    }
    req.isMr = 1;
    req.address = mrInfo.address;
    req.size = mrInfo.size;
    req.mrKey = mrInfo.key;
    CLIENT_LOG_DEBUG("Alloc rdma page success, length:" << req.length << ", offset:" << req.offset << ", address:" <<
        mrInfo.address << ", size:" << mrInfo.size << ", key:" << mrInfo.key << ", dstNid:" << masterNid << ".");

    BIO_TRACE_START(SDK_TRACE_GET_REMOTE);
    uint64_t length;
    ret = net::BioClientNet::Instance()->SendSync<GetRequest, uint64_t>(static_cast<BioNodeId>(masterNid),
        BIO_OP_SDK_GET, req, length);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync get request failed, ret:" << ret << ", key:" << req.key << ", offset:" <<
            req.offset << ", length:" << req.length << ", dstNid:" << masterNid << ".");
    } else {
        realLen = length;
    }
    BIO_TRACE_END(SDK_TRACE_GET_REMOTE, ret);
    if (LIKELY(ret == BIO_OK)) {
        BIO_TRACE_START(SDK_TRACE_GET_COPY2U);
        Copy(reinterpret_cast<void *>(mrInfo.address), value, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_COPY2U, BIO_OK);
    }

    net::BioClientNet::Instance()->Free(mrInfo.address);
    return ret;
}

BResult MirrorClient::SendGetRequest(CmPtInfo &ptEntry, GetRequest &req, char *value, uint64_t &realLen)
{
    return GetMaster(req, ptEntry.masterNodeId, value, realLen);
}

void MirrorClient::DeleteRemote(DeleteRequest &req, CmPtInfo &ptEntry, uint32_t index, NetEngine::Callback &callback)
{
    uint16_t dstNid = ptEntry.copys[index].nodeId;
    net::BioClientNet::Instance()->SendAsync<DeleteRequest>(static_cast<BioNodeId>(dstNid), BIO_OP_SDK_DELETE, req,
        callback);
}

void MirrorClient::DeleteLocal(DeleteRequest &req, NetEngine::Callback &callback) const
{
    agent::BioClientAgent::Instance()->DeleteLocal(req, callback);
}

BResult MirrorClient::SendDeleteRequest(CmPtInfo &ptEntry, DeleteRequest &req)
{
    uint32_t quota = ptEntry.copys.size();
    ClientCallbackCtx cbCtx;
    cbCtx.result = BIO_OK;
    cbCtx.quota = quota;
    sem_init(&cbCtx.sem, 0, 0);
    cbCtx.resp = nullptr;
    cbCtx.respLen = 0;
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    NetEngine::Callback callback(cbFunc, static_cast<void *>(&cbCtx));

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
        CLIENT_LOG_ERROR("Send delete request failed, ret:" << cbCtx.result << ", key:" << req.key << ".");
    }
    return cbCtx.result;
}

BResult MirrorClient::StatRemote(uint16_t dstNid, StatRequest &req, Bio::ObjStat &objInfo)
{
    return net::BioClientNet::Instance()->SendSync<StatRequest, Bio::ObjStat>(static_cast<BioNodeId>(dstNid),
        BIO_OP_SDK_STAT, req, objInfo);
}

BResult MirrorClient::StatLocal(StatRequest &req, Bio::ObjStat &objInfo) const
{
    return agent::BioClientAgent::Instance()->StatLocal(req, objInfo);
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
    if (masterNid == mLocalNid.VNodeId()) {
        agent::BioClientAgent::Instance()->LoadLocal(req, callback, context);
        return BIO_OK;
    }

    uint16_t ptId = req.comm.ptId;
    auto cbFunc = [ptId, callback, context](void *ctx, void *resp, uint32_t len, int32_t result) {
        if (UNLIKELY(result != BIO_OK)) {
            CLIENT_LOG_ERROR("Load master return failed, ret:" << result << ".");
        }
        if (callback != nullptr) {
            callback(context, ((result == BIO_OK) ? RET_CACHE_OK : RET_CACHE_ERROR));
        }
    };
    NetEngine::Callback cb(cbFunc, nullptr);
    net::BioClientNet::Instance()->SendAsyncBuff(static_cast<BioNodeId>(masterNid), BIO_OP_SDK_LOAD,
        static_cast<void *>(&req), sizeof(LoadRequest), cb);
    return BIO_OK;
}

inline BResult MirrorClient::SendLoadRequest(CmPtInfo &ptEntry, LoadRequest &req, const Bio::LoadCallback &callback,
    void *context)
{
    return LoadMaster(req, ptEntry.masterNodeId, callback, context);
}