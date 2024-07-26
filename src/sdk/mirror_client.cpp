/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <functional>
#include <string>
#include <vector>
#include <dlfcn.h>
#include "bio_tracepoint_helper.h"
#include "bio_client_log.h"
#include "bio_trace.h"
#include "message_op.h"
#include "bio_functions.h"
#include "bio_client.h"
#include "bio_client_agent.h"
#include "bio_client_net.h"
#include "bio_monotonic.h"
#include "bio_crc_util.h"
#include "mirror_client.h"

using namespace ock::bio;

static const uint32_t IO_EXTRATEGE_TIME = 3; // IO策略3s过期

BResult MirrorClient::SendCreateFlowRequestRemote(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
    uint64_t &flowId, bool &isDegrade)
{
    BResult ret = BIO_OK;
    CreateFlowRequest req;
    if (opType == 0) {
        req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, opType, 0, false };
    } else if (opType == 1) {
        req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, opType, flowId, isDegrade };
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
        isDegrade = rsp.isDegrade;
    } else if (opType == 1 && rsp.flowId != 0) {
        ret = BIO_ERR;
    }
    return ret;
}

BResult MirrorClient::SendDestroyFlowRequestRemote(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId,
    uint64_t flowId)
{
    DestroyFlowRequest req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() },
        flowId };
    DestroyFlowResponse rsp;
    BResult ret = net::BioClientNet::Instance()->SendSync<DestroyFlowRequest, DestroyFlowResponse>(
        static_cast<BioNodeId>(nodeId), BIO_OP_SDK_DESTROY_FLOW, req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync destroy flow request failed, ret:" << ret << ", nodeId:" << nodeId <<
            ", ptId:" << ptId << ", flowId:" << flowId << ".");
        return ret;
    }

    return BIO_OK;
}

BResult MirrorClient::CreateFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
    uint64_t &flowId, bool &isDegrade)
{
    if (LIKELY(nodeId == mLocalNid.VNodeId())) {
        return agent::BioClientAgent::Instance()->CreateFlowLocal(getpid(), ptEntry, ptId, opType,
            flowId, isDegrade);
    } else {
        return SendCreateFlowRequestRemote(nodeId, ptEntry, ptId, opType, flowId, isDegrade);
    }
}

BResult MirrorClient::DestroyFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint64_t flowId)
{
    if (LIKELY(nodeId == mLocalNid.VNodeId())) {
        return agent::BioClientAgent::Instance()->DestroyFlowLocal(getpid(), ptEntry, ptId, flowId);
    } else {
        return SendDestroyFlowRequestRemote(nodeId, ptEntry, ptId, flowId);
    }
}

BResult MirrorClient::CreateFlow(uint16_t ptId)
{
    BResult ret = Insert(ptId); // 多并发IO情况下，防止重入
    if (ret != BIO_OK) {
        CLIENT_LOG_WARN("Insert failed, ret:" << ret << ", ptId:" << ptId << ".");
        return BIO_INNER_RETRY; // 延迟重试
    }

    CmPtInfo ptEntry;
    ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret:" << ret << ", ptId:" << ptId << ".");
        Delete(ptId, 0);
        return ret;
    }

    uint64_t flowId = UINT64_MAX;
    bool isDegrade = false;
    ret = CreateFlowImpl(ptEntry.masterNodeId, ptEntry, ptId, 0, flowId, isDegrade);
    if (UNLIKELY(ret != BIO_OK || flowId == UINT64_MAX)) {
        CLIENT_LOG_ERROR("Create master flow failed, ret:" << ret << ", ptId:" << ptId << ", masterNid:" <<
            ptEntry.masterNodeId << ".");
        Delete(ptId, 0);
        return ret;
    }

    for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
        if (ptEntry.copys[idx].nodeId == ptEntry.masterNodeId) {
            continue;
        }
        if (ptEntry.copys[idx].state != CM_COPY_RUNNING && ptEntry.copys[idx].state != CM_COPY_RECOVERY) {
            continue;
        }
        ret = CreateFlowImpl(ptEntry.copys[idx].nodeId, ptEntry, ptId, 1, flowId, isDegrade);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Create slave flow failed, ret:" << ret << ", ptId:" << ptId << ", slaveNid:" <<
                ptEntry.copys[idx].nodeId << ".");
            Delete(ptId, 0);
            return ret;
        }
    }

    Update(ptId, ptEntry.version, flowId, isDegrade);

    CLIENT_LOG_INFO("Create flow instance success, ptId:" << ptId << ", ptv:" << ptEntry.version << ", flowId:" <<
        flowId << ", isDegrade:" << isDegrade << ".");
    return BIO_OK;
}

BResult MirrorClient::DestroyFlow(uint16_t ptId, uint64_t flowId)
{
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret:" << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
        if (ptEntry.copys[idx].state != CM_COPY_RUNNING && ptEntry.copys[idx].state != CM_COPY_RECOVERY) {
            continue;
        }
        ret = DestroyFlowImpl(ptEntry.copys[idx].nodeId, ptEntry, ptId, flowId);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Destroy flow failed, ret:" << ret << ", ptId:" << ptId << ", nid:" <<
                ptEntry.copys[idx].nodeId << ", flowId:" << flowId << ".");
            continue;
        }
    }

    return BIO_OK;
}

BResult MirrorClient::LoadAffinityFlow()
{
    std::vector<uint16_t> ptVec = ListLocalAffinityPt();
    for (uint16_t &ptId : ptVec) {
        BResult ret = CreateFlow(ptId);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Create affinity flow instance failed, ret:" << ret << ", ptId:" << ptId << ".");
        } else {
            CLIENT_LOG_INFO("Load affinity flow success, ptId:" << ptId << ".");
        }
    }
    return BIO_OK;
}

BResult MirrorClient::InitializeBioQos()
{
    // 1. query local node write and read resource
    uint64_t writeRes = 0;
    uint64_t readRes = 0;
    auto ret = agent::BioClientAgent::Instance()->GetLocalResourceInfo(writeRes, readRes);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get local resource quota failed, ret:" << ret << ".");
        return ret;
    }
    CLIENT_LOG_INFO("InitializeBioQos writeRes : " << writeRes << " ,readRes : " << readRes <<".");
    // 分离部署server端可能没有资源配置， 先设置max，后续io通信时会同步新的阈值
    if (writeRes == 0 && readRes == 0) {
        writeRes = UINT64_MAX;
        readRes = UINT64_MAX;
    }

    // 2. fill write and read concurrency
    uint64_t writeConcur = (mScene == SCENE_BIGDATA) ? NO_32 : NO_128;
    uint64_t readConcur = (mScene == SCENE_BIGDATA) ? NO_32 : NO_1024;

    // 3. start bio qos
    mBioQos = BioQos::Instance();
    return mBioQos->Initialize(writeRes, readRes, writeConcur, readConcur);
}

BResult MirrorClient::LoadOriginView()
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret;

    do {
        isRetry = false;
        ret = LoadOriginViewImpl();
        if (LIKELY(ret == BIO_OK)) {
            return BIO_OK;
        }
        if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY || ret == BIO_CHECK_PT_FAIL) {
            CLIENT_LOG_INFO("Delay retry, times:" << ++retryCnt << ", ret:" << ret << ".");
            retryTime = Monotonic::TimeSec() - startTime;
            if (retryTime < BIO_INIT_TIMEOUT_TIME) {
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ret;
}

BResult MirrorClient::GetFileLocation(uint16_t masterPtId, uint16_t slavePtId,
    FileLocationQueryRsp &fileLocationQueryRsp)
{
    return agent::BioClientAgent::Instance()->SendGetNodeInfoRequest(masterPtId, slavePtId, fileLocationQueryRsp);
}

BResult MirrorClient::LoadOriginViewImpl()
{
    auto ret = agent::BioClientAgent::Instance()->GetClusterNodeView(mCurNodeTimes, mNodeView);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get cluster node view failed, ret:" << ret << ".");
        return ret;
    }
    CLIENT_LOG_INFO("Current node times:" << mCurNodeTimes);

    ret = agent::BioClientAgent::Instance()->GetPtView(mCurPtTimes, mPtView);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get pt view failed, ret:" << ret << ".");
        return ret;
    }
    CLIENT_LOG_INFO("Current pt times:" << mCurPtTimes);

    ret = agent::BioClientAgent::Instance()->GetLocalNodeInfo(mNetProtocol, mLocalNid);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get local node info failed, ret:" << ret << ".");
        return ret;
    }

    for (auto &item : mPtView) {
        IoStratege *ioStratege = new (std::nothrow) IoStratege();
        if (UNLIKELY(ioStratege == nullptr)) {
            CLIENT_LOG_ERROR("Alloc ioStratege failed.");
            return BIO_ALLOC_FAIL;
        }
        ioStratege->expired = Monotonic::TimeSec() + IO_EXTRATEGE_TIME;
        ioStratege->stratege = 0;
        mIoStratege[item.first] = ioStratege;
    }

    if (mNodeView.empty() || mPtView.empty()) {
        return BIO_INNER_RETRY;
    }
    CLIENT_LOG_INFO("Load origin view success, localNid:" << mLocalNid.VNodeId() << ", protocol:" << mNetProtocol);
    return BIO_OK;
}

std::vector<uint16_t> MirrorClient::ListLocalAffinityPt()
{
    std::vector<uint16_t> ans;
    mLock.LockRead();
    for (auto &item : mPtView) {
        if (mLocalNid.VNodeId() == item.second.masterNodeId) {
            ans.emplace_back(item.first);
        }
    }
    mLock.UnLock();
    return ans;
}

std::vector<uint64_t> MirrorClient::ShowPtHit()
{
    std::vector<uint64_t> res;
    for (uint32_t idx = 0; idx < mPtView.size(); idx++) {
        res.emplace_back(mPtHit[idx].load());
    }
    return res;
}

void MirrorClient::StatisticPtHit(uint16_t ptId)
{
    mPtHit[ptId]++;
}

uint16_t MirrorClient::SelectingPt(uint64_t objectId, AffinityStrategy affinity)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    uint16_t ptId = UINT16_MAX;

    do {
        isRetry = false;
        ptId = SelectingPtImpl(objectId, affinity);
        if (ptId == UINT16_MAX) {
            uint64_t retryTime = Monotonic::TimeSec() - startTime;
            CLIENT_LOG_INFO("Delay retry, costs:" << retryTime << ", times:" << ++retryCnt);
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ptId;
}

uint16_t MirrorClient::SelectingPtImpl(uint64_t objectId, AffinityStrategy affinity)
{
    size_t v = std::hash<std::uint64_t>{}(objectId);
    uint16_t ptId = UINT16_MAX;
    if (LIKELY(affinity == LOCAL_AFFINITY)) {
        std::vector<uint16_t> ptVec = ListLocalAffinityPt();
        if (!ptVec.empty()) {
            ptId = ptVec[v % ptVec.size()];
        }
    } else if (affinity == GLOBAL_BALANCE) {
        mLock.LockRead();
        size_t ptNum = mPtView.size();
        if (ptNum != 0) {
            ptId = mPtView[v % ptNum].ptId;
        }
        mLock.UnLock();
    } else {
        CLIENT_LOG_ERROR("Invalid affinity type or pt view is empty, objectId:" << objectId << ", affinity:" <<
            affinity << ".");
    }
    LVOS_TP_START(SDK_MIRROR_SET_PT_ID_FAIL, &ptId, UINT16_MAX);
    LVOS_TP_END;
    if (UNLIKELY(ptId == UINT16_MAX)) {
        CLIENT_LOG_ERROR("Selecting pt failed, objectId:" << objectId << ", affinity:" << affinity << ".");
    } else {
        StatisticPtHit(ptId);
    }
    return ptId;
}

BResult MirrorClient::GetPtEntry(uint16_t ptId, CmPtInfo &ptEntry)
{
    mLock.LockRead();
    auto iter = mPtView.find(ptId);
    bool isFind = false;
    LVOS_TP_START(SDK_MIRROR_PT_VIEW_FIND_FAIL, &isFind, false);
    isFind = (iter != mPtView.end());
    LVOS_TP_END;
    if (UNLIKELY(!isFind)) {
        mLock.UnLock();
        CLIENT_LOG_ERROR("Invalid pt id:" << ptId << ".");
        return BIO_INVALID_PARAM;
    }

    bool isPtNormal = false;
    LVOS_TP_START(SDK_MIRROR_CHECK_PT_FAIL, &isPtNormal, false);
    isPtNormal = (iter->second.state != CM_PT_FAULT);
    LVOS_TP_END;
    if (UNLIKELY(!isPtNormal)) {
        mLock.UnLock();
        CLIENT_LOG_ERROR("Pt stat is fault, pt id:" << ptId << ", state:" << iter->second.state << ".");
        return BIO_CHECK_PT_FAIL;
    }

    ptEntry = iter->second;
    mLock.UnLock();
    return BIO_OK;
}

uint32_t MirrorClient::CalcPtQuota(CmPtInfo &ptEntry)
{
    uint32_t quota = 0;
    for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
        if (ptEntry.copys[idx].state != CM_COPY_RUNNING && ptEntry.copys[idx].state != CM_COPY_RECOVERY) {
            continue;
        }
        quota++;
    }
    return quota;
}

BResult MirrorClient::Initialize(UpdateView updateView, uint32_t scene, uint32_t alignSize,
    uint32_t timeOut, bool enableCrc)
{
    mEnableCrc = enableCrc;
    if (mEnableCrc) {
        CLIENT_LOG_INFO("Crc verify start-up.");
    }
    mUpdateView = updateView;
    mScene = static_cast<WorkerScene>(scene);
    mAlignSize = alignSize;
    mTimeOut = timeOut;
    BIO_TRACE_START(SDK_TRACE_INIT_LOAD_VIEW);
    BResult ret = LoadOriginView();
    BIO_TRACE_END(SDK_TRACE_INIT_LOAD_VIEW, ret);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Sdk load origin view failed, ret:" << ret << ".");
        return ret;
    }

    ret = InitializeBioQos();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Sdk init qos failed, ret:" << ret << ".");
        return ret;
    }

    CLIENT_LOG_INFO("Mirror client initialize, scene:" << mScene << ", alignSize:" << mAlignSize <<
        ", timeOut:" << mTimeOut << ".");
    return BIO_OK;
}

BResult MirrorClient::Start()
{
    uint32_t ptSize = mPtView.size();
    LVOS_TP_START(SDK_BIO_START_WORK_ALLOC_FAIL, &mPtHit, nullptr);
    mPtHit = new (std::nothrow) std::atomic<uint64_t>[ptSize];
    LVOS_TP_END;
    if (mPtHit == nullptr) {
        return BIO_ALLOC_FAIL;
    }
    for (uint32_t idx = 0; idx < ptSize; idx++) {
        mPtHit[idx].store(0);
    }
    return LoadAffinityFlow();
}

BResult MirrorClient::Put(MirrorPut &param)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret = BIO_OK;

    bool isSelf = false;
    char *value = param.value;
    if ((mScene == SCENE_BIGDATA) && (param.length % mAlignSize != 0)) {
        uint64_t length = (((param.length) + (mAlignSize)-1) & ~((mAlignSize)-1));
        BIO_TRACE_START(SDK_TRACE_PUT_ALIGN_IO);
        CLIENT_LOG_DEBUG("Not align io, key:" << param.key << ", length:" << param.length << ".");
        isSelf = true;
        if ((param.value = static_cast<char *>(malloc(length))) == nullptr) {
            BIO_TRACE_END(SDK_TRACE_PUT_ALIGN_IO, BIO_ALLOC_FAIL);
            return BIO_ALLOC_FAIL;
        }
        ret = memcpy_s(param.value, length, value, param.length);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Memory copy failed.");
        }
        param.length = length;
        BIO_TRACE_END(SDK_TRACE_PUT_ALIGN_IO, BIO_OK);
    }

    BIO_TRACE_START(SDK_TRACE_PUT_APPLY_QOS);
    mBioQos->Apply(QOS_CONCURRENCY | QOS_QUOTA, QUOTA_WRITE, param.length);
    BIO_TRACE_END(SDK_TRACE_PUT_APPLY_QOS, BIO_OK);
    do {
        isRetry = false;
        uint64_t updateWriteQuota = 0;
        ret = PutImpl(param, updateWriteQuota);
        if (LIKELY(ret == BIO_OK)) {
            BIO_TRACE_START(SDK_TRACE_PUT_RELEASE_QOS);
            mBioQos->Release(QOS_CONCURRENCY | QOS_QUOTA, QUOTA_WRITE, param.length, updateWriteQuota);
            BIO_TRACE_END(SDK_TRACE_PUT_RELEASE_QOS, BIO_OK);
            if (isSelf) {
                free(param.value);
                param.value = value;
            }
            return BIO_OK;
        }
        if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY ||
            ret == BIO_CHECK_PT_FAIL || ret == BIO_DISK_IOERR) {
            uint64_t retryTime = Monotonic::TimeSec() - startTime;
            CLIENT_LOG_INFO("Delay retry, key:" << param.key << ", costs:" << retryTime << ", times:" << ++retryCnt);
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);
    BIO_TRACE_START(SDK_TRACE_PUT_RELEASE_QOS);
    mBioQos->Release(QOS_CONCURRENCY | QOS_QUOTA, QUOTA_WRITE, param.length);
    BIO_TRACE_END(SDK_TRACE_PUT_RELEASE_QOS, BIO_OK);
    if (isSelf) {
        free(param.value);
        param.value = value;
    }

    return ret;
}

BResult MirrorClient::PreparePutWithSpace(MirrorPut &param, CmPtInfo &ptEntry, CacheSpaceDesc &spaceInfo,
    PutRequest *&req)
{
    uint8_t *reqTmp = nullptr;
    LVOS_TP_START(SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL, 0);
    reqTmp = new(std::nothrow) uint8_t[sizeof(PutRequest) + spaceInfo.descriptorSize];
    LVOS_TP_END;
    if (UNLIKELY(reqTmp == nullptr)) {
        CLIENT_LOG_ERROR("Alloc put memory failed, len:" << sizeof(PutRequest) + spaceInfo.descriptorSize << ".");
        return BIO_INNER_ERR;
    }

    req = static_cast<PutRequest *>(static_cast<void *>(reqTmp));
    req->comm = { MESSAGE_MAGIC, ptEntry.ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    req->tenantId = param.attr.mTenantId;
    req->affinity = param.attr.affinity;
    req->strategy = param.attr.strategy;
    CopyKey(req->key, param.key, KEY_MAX_SIZE);
    req->length = param.length;
    req->mrKey = net::BioClientNet::Instance()->GetLocalMrKey();
    req->sliceLen = spaceInfo.descriptorSize;
    if (mIoStratege[ptEntry.ptId]->expired > Monotonic::TimeSec()) {
        req->ioStratege = mIoStratege[ptEntry.ptId]->stratege;
    } else {
        req->ioStratege = 0;
    }
    req->memFromServer = true;
    req->mrAddress = 0ULL;
    memcpy_s(req->sliceBuf, spaceInfo.descriptorSize, spaceInfo.descriptorInfo, spaceInfo.descriptorSize);

    CLIENT_LOG_DEBUG("Put copy free request key:" << req->key << ", length:" << ", index:" << req->flowIndex <<
        ", sliceLen:" << req->sliceLen << ", mrkey:" << req->mrKey << ".");
    return BIO_OK;
}

BResult MirrorClient::PutImpl(MirrorPut &param, CacheSpaceDesc &spaceInfo, uint64_t &updateQuota)
{
    uint16_t ptId = ParseLocation(spaceInfo.loc);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return BIO_CHECK_PT_FAIL;
    }

    PutRequest *req = nullptr;
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE);
    ret = PreparePutWithSpace(param, ptEntry, spaceInfo, req);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Prepare put with space failed, ret:" << ret << ", key:" << param.key << ", length:" <<
            param.length << ", loc0:" << spaceInfo.loc.location[0] << ", loc1:" << spaceInfo.loc.location[1] << ".");
        return ret;
    }

    BIO_TRACE_START(SDK_TRACE_PUT_SEND);
    ret = SendPutRequestImpl(ptEntry, param, req, updateQuota);
    BIO_TRACE_END(SDK_TRACE_PUT_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send put request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
    }

    delete[] static_cast<uint8_t *>(static_cast<void *>(req));
    return ret;
}

BResult MirrorClient::Put(MirrorPut &param, CacheSpaceDesc &spaceInfo)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret = BIO_OK;

    BIO_TRACE_START(SDK_TRACE_PUT_CPYFREE_APPLY_QOS);
    mBioQos->Apply(QOS_CONCURRENCY, QUOTA_WRITE);
    BIO_TRACE_END(SDK_TRACE_PUT_CPYFREE_APPLY_QOS, BIO_OK);
    do {
        isRetry = false;
        uint64_t updateWriteQuota = 0;
        ret = PutImpl(param, spaceInfo, updateWriteQuota);
        if (LIKELY(ret == BIO_OK)) {
            BIO_TRACE_START(SDK_TRACE_PUT_CPYFREE_RELEASE_QOS);
            mBioQos->Release(QOS_CONCURRENCY, QUOTA_WRITE);
            BIO_TRACE_END(SDK_TRACE_PUT_CPYFREE_RELEASE_QOS, BIO_OK);
            return BIO_OK;
        }
        if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY ||
            ret == BIO_CHECK_PT_FAIL || ret == BIO_DISK_IOERR) {
            uint64_t retryTime = Monotonic::TimeSec() - startTime;
            CLIENT_LOG_INFO("Delay retry, key:" << param.key << ", costs:" << retryTime << ", times:" << ++retryCnt);
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);
    BIO_TRACE_START(SDK_TRACE_PUT_CPYFREE_RELEASE_QOS);
    mBioQos->Release(QOS_CONCURRENCY, QUOTA_WRITE);
    BIO_TRACE_END(SDK_TRACE_PUT_CPYFREE_RELEASE_QOS, BIO_OK);

    return ret;
}

BResult MirrorClient::PutImpl(MirrorPut &param, uint64_t &updateQuota)
{
    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    BIO_TRACE_START(SDK_TRACE_PUT_GET_PT);
    BResult ret = GetPtEntry(ptId, ptEntry);
    BIO_TRACE_END(SDK_TRACE_PUT_GET_PT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return ret;
    }

    BIO_TRACE_START(SDK_TRACE_PUT_ALLOC_OFF);
    ret = AllocPutOffset(ptId, ptEntry.version, param.length, param.flowId, param.flowOffset, param.flowIndex);
    BIO_TRACE_END(SDK_TRACE_PUT_ALLOC_OFF, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc put offset failed, ret:" << ret << ", ptId:" << ptId <<
            ", flowId:" << param.flowId << ", key:" << param.key << ".");
        ret = (ret == BIO_NOT_EXISTS) ? BIO_INNER_RETRY : ret;
        return ret;
    }

    ret = SendPutRequest(ptEntry, param, updateQuota);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send put request failed, ret:" << ret << ", ptId:" << ptId <<
            ", flowId:" << param.flowId << ", key:" << param.key << ".");
        Delete(ptId, param.flowId);
        ret = (ret == BIO_NOT_EXISTS) ? BIO_INNER_RETRY : ret;
    }

    return ret;
}

BResult MirrorClient::Get(MirrorGet &param, uint64_t &realLen)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret = BIO_OK;

    BIO_TRACE_START(SDK_TRACE_GET_APPLY_QOS);
    mBioQos->Apply(QOS_CONCURRENCY, QUOTA_READ);
    BIO_TRACE_END(SDK_TRACE_GET_APPLY_QOS, BIO_OK);
    do {
        isRetry = false;
        ret = GetImpl(param, realLen);
        uint64_t endTime = Monotonic::TimeSec();
        if ((endTime > startTime) && (endTime - startTime > NO_30)) {
            CLIENT_LOG_ERROR("Too long, key:" << param.key << ", cost:" << (endTime - startTime) <<
                ", ret:" << ret << ".");
        }
        if (LIKELY(ret == BIO_OK)) {
            BIO_TRACE_START(SDK_TRACE_GET_RELEASE_QOS);
            mBioQos->Release(QOS_CONCURRENCY, QUOTA_READ);
            BIO_TRACE_END(SDK_TRACE_GET_RELEASE_QOS, BIO_OK);
            return BIO_OK;
        }
        if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY ||
            ret == BIO_CHECK_PT_FAIL || ret == BIO_DISK_IOERR) {
            uint64_t retryTime = Monotonic::TimeSec() - startTime;
            CLIENT_LOG_INFO("Delay retry, key:" << param.key << ", costs:" << retryTime << ", times:" << ++retryCnt);
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);
    mBioQos->Release(QOS_CONCURRENCY, QUOTA_READ);

    return ret;
}

BResult MirrorClient::GetImpl(MirrorGet &param, uint64_t &realLen)
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
    CopyKey(req.key, param.key, KEY_MAX_SIZE);
    req.ptId = ptId;
    req.offset = param.offset;
    req.length = param.length;
    req.isConvDeploy = (mMode == WorkerMode::CONVERGENCE);
    req.enableCrc = mEnableCrc;
    BIO_TRACE_START(SDK_TRACE_GET_SEND);
    ret = SendGetRequest(ptEntry, req, param.value, realLen);
    BIO_TRACE_END(SDK_TRACE_GET_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send get request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
    }
    return ret;
}

BResult MirrorClient::DeleteKey(const char *key, const ObjLocation &location)
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret;

    do {
        isRetry = false;
        ret = DeleteKeyImpl(key, location);
        if (LIKELY(ret == BIO_OK)) {
            return BIO_OK;
        }
        if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY ||
            ret == BIO_CHECK_PT_FAIL || ret == BIO_DISK_IOERR) {
            retryTime = Monotonic::TimeSec() - startTime;
            CLIENT_LOG_INFO("Delay retry, key:" << key << ", costs:" << retryTime << ", times:" << ++retryCnt);
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ret;
}

BResult MirrorClient::DeleteKeyImpl(const char *key, const ObjLocation &location)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return ret;
    }

    DeleteRequest req{};
    req.comm = { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.key, key, KEY_MAX_SIZE);
    ret = SendDeleteRequest(ptEntry, req);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send delete request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
    }
    return ret;
}

BResult MirrorClient::Load(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
    const Bio::LoadCallback &callback, void *context)
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret;

    do {
        isRetry = false;
        ret = LoadImpl(key, offset, length, location, callback, context);
        if (LIKELY(ret == BIO_OK)) {
            return BIO_OK;
        }
        if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY ||
            ret == BIO_CHECK_PT_FAIL || ret == BIO_DISK_IOERR) {
            retryTime = Monotonic::TimeSec() - startTime;
            CLIENT_LOG_INFO("Delay retry, key:" << key << ", costs:" << retryTime << ", times:" << ++retryCnt);
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ret;
}

BResult MirrorClient::LoadImpl(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
    const Bio::LoadCallback &callback, void *context)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return ret;
    }

    LoadRequest req;
    req.comm = { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.key, key, KEY_MAX_SIZE);
    req.offset = offset;
    req.length = length;
    ret = SendLoadRequest(ptEntry, req, callback, context);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send stat request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
    }
    return ret;
}

BResult MirrorClient::ListAll(const char *prefix, std::unordered_map<std::string, ObjStat> &objs)
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret;

    do {
        isRetry = false;
        ret = ListAllImpl(prefix, objs);
        if (LIKELY(ret == BIO_OK)) {
            return BIO_OK;
        }
        if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY ||
            ret == BIO_CHECK_PT_FAIL || ret == BIO_DISK_IOERR) {
            retryTime = Monotonic::TimeSec() - startTime;
            CLIENT_LOG_INFO("Delay retry, costs:" << retryTime << ", times:" << ++retryCnt);
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ret;
}

BResult MirrorClient::ListAllImpl(const char *prefix, std::unordered_map<std::string, ObjStat> &objs)
{
    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.prefix, prefix, KEY_MAX_SIZE);
    return SendListRequest(req, objs);
}

BResult MirrorClient::StatObject(const char *key, const ObjLocation &location, ObjStat &stat)
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret;

    do {
        isRetry = false;
        ret = StatObjectImpl(key, location, stat);
        if (LIKELY(ret == BIO_OK)) {
            return BIO_OK;
        }
        if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY ||
            ret == BIO_CHECK_PT_FAIL || ret == BIO_DISK_IOERR) {
            CLIENT_LOG_INFO("Delay retry, key:" << key << ", times:" << ++retryCnt);
            retryTime = Monotonic::TimeSec() - startTime;
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ret;
}

BResult MirrorClient::StatObjectImpl(const char *key, const ObjLocation &location, ObjStat &stat)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << key << ".");
        return ret;
    }

    StatRequest req{};
    req.comm = { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.key, key, KEY_MAX_SIZE);
    ret = SendStatRequest(ptEntry, req, stat);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send stat request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << key << ".");
    }
    return ret;
}

BResult MirrorClient::NotifyUpdate(bool &flag)
{
    auto ret = SendNotifyUpdateRequest(flag);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send update request failed, ret:" << ret << ".");
    }
    return ret;
}

BResult MirrorClient::CheckUpdateReady()
{
    auto ret = SendCheckUpdateReadyRequest();
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Check update request failed, ret:" << ret << ".");
    }
    return ret;
}

BResult MirrorClient::AllocSpaceImpl(MirrorClient::MirrorPut &param, CacheSpaceDesc &spaceInfo,
    uint64_t &adjustWriteQuota)
{
    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get py entry failed, ret: " << ret << ", ptId:" << ptId << ", location:" <<
            param.location.location[0] << ".");
        return ret;
    }

    BIO_TRACE_START(SDK_TRACE_PUT_ALLOC_OFF);
    ret = AllocPutOffset(ptId, ptEntry.version, param.length, param.flowId, param.flowOffset, param.flowIndex);
    BIO_TRACE_END(SDK_TRACE_PUT_ALLOC_OFF, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc put offset failed, ret:" << ret << ", ptId:" << ptId << ", ptv:" << ptEntry.version
            << ", length:" << param.length << ".");
        return ret;
    }

    GetSliceResponse *rsp = nullptr;
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_GET_SLICE);
    ret = agent::BioClientAgent::Instance()->PrepareResource(ptEntry, param.flowId, param.flowOffset,
        param.flowIndex, param.length, &rsp);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE_GET_SLICE, BIO_OK);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc put space failed, ret:" << ret << ", flowId:" << param.flowId << ", flowOffset:" <<
            param.flowOffset << ", length:" << param.length << ".");
        Delete(ptId, param.flowId);
        return ret;
    }

    spaceInfo.descriptorSize = rsp->sliceLen;
    ret = memcpy_s(spaceInfo.descriptorInfo, CACHE_SPACE_DEC_SIZE, rsp->sliceBuf, rsp->sliceLen);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Failed to data copy, sliceLen:" << rsp->sliceLen << ".");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        Delete(ptId, param.flowId);
        return BIO_INNER_ERR;
    }

    spaceInfo.addressNum = rsp->addrNum;
    for (uint32_t idx = 0; idx < spaceInfo.addressNum; idx++) {
        if (mMode == CONVERGENCE) {
            spaceInfo.address[idx].address = rsp->addr[idx].chunkId + rsp->addr[idx].chunkOffset;
        } else {
            uint8_t *realAddr = net::BioClientNet::Instance()->GetShmAddress(rsp->addrOffset[idx]);
            spaceInfo.address[idx].address = reinterpret_cast<uintptr_t>(realAddr);
        }
        spaceInfo.address[idx].size = rsp->addr[idx].chunkLen;
    }

    adjustWriteQuota = rsp->updateQuota;
    delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
    return BIO_OK;
}

BResult MirrorClient::AllocSpace(MirrorClient::MirrorPut &param, CacheSpaceDesc &spaceInfo)
{
    BIO_TRACE_START(SDK_TRACE_ALLOC_SPACE_APPLY_QOS);
    mBioQos->Apply(QOS_QUOTA, QUOTA_WRITE, param.length);
    BIO_TRACE_END(SDK_TRACE_ALLOC_SPACE_APPLY_QOS, BIO_OK);
    uint64_t updateWriteQuota = 0;
    BResult ret = AllocSpaceImpl(param, spaceInfo, updateWriteQuota);
    if (LIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc space failed, ret:" << ret << ", length:" << param.length << ".");
        BIO_TRACE_START(SDK_TRACE_ALLOC_SPACE_RELEASE_QOS);
        mBioQos->Release(QOS_QUOTA, QUOTA_WRITE, param.length);
        BIO_TRACE_END(SDK_TRACE_ALLOC_SPACE_RELEASE_QOS, BIO_OK);
        return ret;
    }

    BIO_TRACE_START(SDK_TRACE_ALLOC_SPACE_RELEASE_QOS);
    mBioQos->Release(QOS_QUOTA, QUOTA_WRITE, param.length, updateWriteQuota);
    BIO_TRACE_END(SDK_TRACE_ALLOC_SPACE_RELEASE_QOS, BIO_OK);
    return BIO_OK;
}

BResult MirrorClient::AllocPutOffset(uint16_t ptId, uint64_t ptv, uint64_t len, uint64_t &flowId, uint64_t &offset,
    uint64_t &index)
{
    BResult ret;

    FlowInstancePtr flowInst = Query(ptId);
    if (UNLIKELY(flowInst == nullptr)) {
        ret = CreateFlow(ptId);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_WARN("Create flow instance failed, ret: " << ret << ", ptId:" << ptId << ".");
            return ret;
        }
        flowInst = Query(ptId);
        if (UNLIKELY(flowInst == nullptr)) {
            CLIENT_LOG_WARN("Query flow failed, ptId:" << ptId << ".");
            return BIO_INNER_RETRY;
        }
    }

    bool isNormal = false;
    LVOS_TP_START(SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL, &isNormal, false);
    isNormal = flowInst->IsNormal();
    LVOS_TP_END;
    if (UNLIKELY(!isNormal)) {
        CLIENT_LOG_WARN("Check flow failed, ptId:" << ptId << ".");
        return BIO_INNER_RETRY;
    }

    if (UNLIKELY(flowInst->Version() != ptv)) {
        Delete(ptId, flowInst->FlowId()); // 销毁老版本实例
        BResult ret = CreateFlow(ptId);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Create flow instance failed, need retry, ret: " << ret << ", ptId:" << ptId << ".");
        }
        return BIO_INNER_RETRY;
    }

    flowId = flowInst->FlowId();
    offset = flowInst->AllocOffset(len, index);
    return BIO_OK;
}

void MirrorClient::ConstructPutReq(PutRequest *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId,
    uint64_t flowOffset, uint64_t flowIndex, GetSliceResponse *rsp)
{
    req->comm = { MESSAGE_MAGIC, ptEntry.ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    req->tenantId = param.attr.mTenantId;
    req->affinity = param.attr.affinity;
    req->strategy = param.attr.strategy;
    CopyKey(req->key, param.key, KEY_MAX_SIZE);
    req->length = param.length;
    req->flowId = flowId;
    req->flowOffset = flowOffset;
    req->flowIndex = flowIndex;
    req->mrAddress = 0;
    req->mrSize = 0;
    req->mrKey = net::BioClientNet::Instance()->GetLocalMrKey();
    req->sliceLen = rsp->sliceLen;
    if (mIoStratege[ptEntry.ptId]->expired > Monotonic::TimeSec()) {
        req->ioStratege = mIoStratege[ptEntry.ptId]->stratege;
    } else {
        req->ioStratege = 0;
    }
    memcpy_s(req->sliceBuf, req->sliceLen, rsp->sliceBuf, rsp->sliceLen);
}

void MirrorClient::ConstructPutReq(PutRequest *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId,
    uint64_t flowOffset, uint64_t flowIndex, NetMrInfo &mr)
{
    req->comm = { MESSAGE_MAGIC, ptEntry.ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    req->tenantId = param.attr.mTenantId;
    req->affinity = param.attr.affinity;
    req->strategy = param.attr.strategy;
    CopyKey(req->key, param.key, KEY_MAX_SIZE);
    req->length = param.length;
    req->flowId = flowId;
    req->flowOffset = flowOffset;
    req->flowIndex = flowIndex;
    req->mrAddress = mr.address;
    req->mrSize = mr.size;
    req->mrKey = mr.key;
    req->sliceLen = 0;
    if (mIoStratege[ptEntry.ptId]->expired > Monotonic::TimeSec()) {
        req->ioStratege = mIoStratege[ptEntry.ptId]->stratege;
    } else {
        req->ioStratege = 0;
    }
}

BResult MirrorClient::DataCopy(const char *from, SliceAddrDesc *addr, uint64_t *offset, uint32_t addrNum)
{
    uint64_t off = 0;
    for (uint32_t i = 0; i < addrNum; i++) {
        uint8_t *realAddr = nullptr;
        if (mMode == WorkerMode::CONVERGENCE) {
            realAddr = reinterpret_cast<uint8_t *>(addr[i].chunkId + addr[i].chunkOffset);
        } else {
            realAddr = net::BioClientNet::Instance()->GetShmAddress(offset[i]);
            if (UNLIKELY(realAddr == nullptr)) {
                CLIENT_LOG_ERROR("Get shm addr failed offset:" << offset[i] << ".");
                return BIO_INNER_ERR;
            }
        }
        auto ret = memcpy_s(realAddr, addr[i].chunkLen, (from + off), addr[i].chunkLen);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Failed to copy data, len:" << addr[i].chunkLen << ".");
            return BIO_INNER_ERR;
        }
        off += addr[i].chunkLen;
    }
    return BIO_OK;
}

bool MirrorClient::IsExistLocalCopy(CmPtInfo &ptEntry)
{
    for (uint32_t i = 0; i < ptEntry.copys.size(); i++) {
        if (ptEntry.copys[i].nodeId == mLocalNid.VNodeId() &&
            (ptEntry.copys[i].state == CM_COPY_RUNNING || ptEntry.copys[i].state == CM_COPY_RECOVERY)) {
            return true;
        }
    }
    return false;
}

BResult MirrorClient::PrepareFromServer(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req)
{
    GetSliceResponse *rsp = nullptr;
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_GET_SLICE);
    auto ret = agent::BioClientAgent::Instance()->PrepareResource(ptEntry, param.flowId, param.flowOffset,
        param.flowIndex, param.length, &rsp);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE_GET_SLICE, BIO_OK);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Prepare resource failed, ret:" << ret << ", key:" << param.key << ", flowId:" <<
            param.flowId << ", flowOffset:" << param.flowOffset << ", length:" << param.length << ".");
        return ret;
    }

    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_COPY_DATA);
    ret = DataCopy(param.value, rsp->addr, rsp->addrOffset, rsp->addrNum);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE_COPY_DATA, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Copy data failed, ret:" << ret << ", key:" << param.key << ", flowId:" << param.flowId <<
            ", flowOffset:" << param.flowOffset << ", length:" << param.length << ".");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        return ret;
    }
    auto tmp = new (std::nothrow) uint8_t[sizeof(PutRequest) + rsp->sliceLen];
    if (UNLIKELY(tmp == nullptr)) {
        CLIENT_LOG_ERROR("Alloc put request memory failed, len:" << sizeof(PutRequest) + rsp->sliceLen << ".");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        return BIO_ALLOC_FAIL;
    }

    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_SLICE_SERIALIZATION);
    req = static_cast<PutRequest *>(static_cast<void *>(tmp));
    ConstructPutReq(req, ptEntry, param, param.flowId, param.flowOffset, param.flowIndex, rsp);
    req->memFromServer = true;
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE_SLICE_SERIALIZATION, ret);
    delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
    return BIO_OK;
}

BResult MirrorClient::PrepareFromClient(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req)
{
    NetMrInfo mr;
    BResult ret = net::BioClientNet::Instance()->Alloc(param.length, mr);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc rdma memory failed, ret:" << ret << ", length:" << param.length << ".");
        return BIO_ALLOC_FAIL;
    }

    ret = memcpy_s(reinterpret_cast<char *>(mr.address), mr.size, param.value, param.length);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Copy data failed, ret:" << ret << ", key:" << param.key << ", flowId:" << param.flowId <<
            ", flowOffset:" << param.flowOffset << ", length:" << param.length << ".");
        net::BioClientNet::Instance()->Free(mr.address);
        return BIO_ALLOC_FAIL;
    }

    auto tmp = new uint8_t[sizeof(PutRequest)];
    req = static_cast<PutRequest *>(static_cast<void *>(tmp));
    ConstructPutReq(req, ptEntry, param, param.flowId, param.flowOffset, param.flowIndex, mr);
    req->memFromServer = false;
    return BIO_OK;
}

BResult MirrorClient::Prepare(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req)
{
    if (IsExistLocalCopy(ptEntry)) {
        return PrepareFromServer(ptEntry, param, req);
    } else {
        return PrepareFromClient(ptEntry, param, req);
    }
}

void MirrorClient::PutRemote(PutRequest *req, CmPtInfo &ptEntry, std::vector<uint32_t> &index,
    Callback &callback)
{
    for (uint32_t i = 0; i < index.size(); i++) {
        uint16_t dstNid = ptEntry.copys[index[i]].nodeId;
        BIO_TRACE_START(SDK_TRACE_PUT_REMOTE_SYNC);
        net::BioClientNet::Instance()->SendAsyncBuff(static_cast<BioNodeId>(dstNid), BIO_OP_SDK_PUT,
            static_cast<void *>(req), sizeof(PutRequest) + req->sliceLen, callback);
        BIO_TRACE_END(SDK_TRACE_PUT_REMOTE_SYNC, BIO_OK);
    }
}

void MirrorClient::PutLocal(PutRequest *req, uint32_t localIdx, Callback &callback) const
{
    if (UNLIKELY(localIdx == UINT32_MAX)) {
        return;
    }
    agent::BioClientAgent::Instance()->PutLocal(req, callback);
}

void MirrorClient::InitCallbackCtx(ClientCallbackCtx &cbCtx, uint32_t quota)
{
    cbCtx.result = BIO_OK;
    cbCtx.quota = quota;
    sem_init(&cbCtx.sem, 0, 0);
    cbCtx.resp = nullptr;
    cbCtx.respLen = 0;
}

BResult MirrorClient::SendPutRequestImpl(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *req, uint64_t &updateQuota)
{
    uint32_t quota = CalcPtQuota(ptEntry);
    ClientCallbackCtx cbCtx;
    InitCallbackCtx(cbCtx, quota);
    std::atomic<uint64_t> negoWriteQuota(UINT64_MAX);
    std::atomic<uint32_t> ioStratege(0);

    auto cbFunc = [&negoWriteQuota, &ioStratege](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        LVOS_TP_START(SDK_MIRROR_PUT_RECV_FAIL, &(cbCtx->result), BIO_INNER_RETRY);
        LVOS_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else {
            PutResponse *rsp = static_cast<PutResponse *>(resp);
            negoWriteQuota = (rsp->updateQuota < negoWriteQuota) ? rsp->updateQuota : negoWriteQuota.load();
            ioStratege = (rsp->ioStratege > ioStratege) ? rsp->ioStratege : ioStratege.load();
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    uint32_t localIdx = UINT32_MAX;
    std::vector<uint32_t> remoteIdx;
    for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
        if (ptEntry.copys[idx].state != CM_COPY_RUNNING && ptEntry.copys[idx].state != CM_COPY_RECOVERY) {
            continue;
        }
        if (ptEntry.copys[idx].nodeId == mLocalNid.VNodeId()) {
            localIdx = idx;
            continue;
        }
        remoteIdx.emplace_back(idx);
    }
    PutRemote(req, ptEntry, remoteIdx, callback);
    PutLocal(req, localIdx, callback);

    sem_wait(&cbCtx.sem);
    sem_destroy(&cbCtx.sem);
    net::BioClientNet::Instance()->Free(req->mrAddress);
    if (cbCtx.result == BIO_OK) {
        updateQuota = negoWriteQuota.load();
        mIoStratege[ptEntry.ptId]->expired = Monotonic::TimeSec() + IO_EXTRATEGE_TIME;
        mIoStratege[ptEntry.ptId]->stratege = ioStratege.load();
    }
    return cbCtx.result;
}

BResult MirrorClient::SendPutRequest(CmPtInfo &ptEntry, MirrorPut &param, uint64_t &updateQuota)
{
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE);
    PutRequest *req = nullptr;
    BResult ret = Prepare(ptEntry, param, req);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Prepare put resource failed, ret:" << ret << ", key:" << param.key << ", length:" <<
            param.length << ", flowId:" << param.flowId << ", flowOffset:" << param.flowOffset << ".");
        return ret;
    }
    if (mEnableCrc) {
        req->dataCrc = BioCrcUtil::Crc32(param.value, param.length);
    } else {
        req->dataCrc = 0;
    }

    BIO_TRACE_START(SDK_TRACE_PUT_SEND);
    ret = SendPutRequestImpl(ptEntry, param, req, updateQuota);
    BIO_TRACE_END(SDK_TRACE_PUT_SEND, ret);
    delete[] static_cast<uint8_t *>(static_cast<void *>(req));
    return ret;
}

BResult MirrorClient::GetMasterRemote(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen)
{
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

    GetResponse rsp;
    ret = net::BioClientNet::Instance()->SendSync<GetRequest, GetResponse>(static_cast<BioNodeId>(masterNid),
        BIO_OP_SDK_GET, req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync get request failed, ret:" << ret << ", key:" << req.key << ", offset:" <<
            req.offset << ", length:" << req.length << ", dstNid:" << masterNid << ".");
    } else {
        realLen = rsp.realLen;
        if (realLen > mrInfo.size) {
            CLIENT_LOG_ERROR("Read length greater than value size, realLen:" << realLen << ", size:" << mrInfo.size);
            return BIO_INNER_ERR;
        }
        BIO_TRACE_START(SDK_TRACE_GET_COPY2U);
        ret = memcpy_s(reinterpret_cast<void *>(mrInfo.address), mrInfo.size, value, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_COPY2U, ret);
        if (UNLIKELY(ret != 0)) {
            CLIENT_LOG_ERROR("Copy data to user failed, ret:" << ret << ".");
        }
        if (req.enableCrc) {
            uint32_t currentCrc = rsp.dataCrc != BioCrcUtil::Crc32(value, realLen);
            if (currentCrc != rsp.dataCrc) {
                CLIENT_LOG_ERROR("Client Get failed to verify the CRC, key:" << req.key << ", origin crc:" <<
                rsp.dataCrc << ", current crc:" << currentCrc);
                return BIO_CRC_ERR;
            }
        }
    }
    net::BioClientNet::Instance()->Free(mrInfo.address);
    return ret;
}

BResult MirrorClient::GetMaster(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen)
{
    CLIENT_LOG_TRACE("Get master start, masterNid:" << masterNid << ", localNid:" << mLocalNid.VNodeId() << ", key:" <<
        req.key << ", offset:" << req.offset << ", length:" << req.length << ".");
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(SDK_MIRROR_GET_RECV_FAIL, &ret, BIO_INNER_RETRY);
    if (masterNid == mLocalNid.VNodeId()) {
        BIO_TRACE_START(SDK_TRACE_GET_LOCAL);
        ret = agent::BioClientAgent::Instance()->GetLocal(req, value, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_LOCAL, ret);
    } else {
        BIO_TRACE_START(SDK_TRACE_GET_REMOTE);
        ret = GetMasterRemote(req, masterNid, value, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_REMOTE, ret);
    }
    LVOS_TP_END;
    return ret;
}

BResult MirrorClient::SendGetRequest(CmPtInfo &ptEntry, GetRequest &req, char *value, uint64_t &realLen)
{
    return GetMaster(req, ptEntry.masterNodeId, value, realLen);
}

void MirrorClient::DeleteRemote(DeleteRequest &req, CmPtInfo &ptEntry, uint32_t index, Callback &callback)
{
    uint16_t dstNid = ptEntry.copys[index].nodeId;
    net::BioClientNet::Instance()->SendAsync<DeleteRequest>(static_cast<BioNodeId>(dstNid), BIO_OP_SDK_DELETE, req,
        callback);
}

void MirrorClient::DeleteLocal(DeleteRequest &req, Callback &callback) const
{
    agent::BioClientAgent::Instance()->DeleteLocal(req, callback);
}

BResult MirrorClient::SendDeleteRequest(CmPtInfo &ptEntry, DeleteRequest &req)
{
    uint32_t quota = CalcPtQuota(ptEntry);
    ClientCallbackCtx cbCtx;
    InitCallbackCtx(cbCtx, quota);
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto cbCtx = (ClientCallbackCtx *)ctx;
        LVOS_TP_START(SDK_MIRROR_DELETE_RECV_FAIL, &result, BIO_INNER_RETRY);
        LVOS_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else {
            BResult res = *(static_cast<BResult *>(resp));
            if (res != BIO_OK) {
                cbCtx->result = res;
            }
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
        if (ptEntry.copys[idx].state != CM_COPY_RUNNING && ptEntry.copys[idx].state != CM_COPY_RECOVERY) {
            continue;
        }
        if (ptEntry.copys[idx].nodeId == mLocalNid.VNodeId()) {
            DeleteLocal(req, callback);
            continue;
        }
        DeleteRemote(req, ptEntry, idx, callback);
    }

    sem_wait(&cbCtx.sem);
    sem_destroy(&cbCtx.sem);
    return cbCtx.result;
}

BResult MirrorClient::StatRemote(uint16_t dstNid, StatRequest &req, ObjStat &objInfo)
{
    return net::BioClientNet::Instance()->SendSync<StatRequest, ObjStat>(static_cast<BioNodeId>(dstNid),
        BIO_OP_SDK_STAT, req, objInfo);
}

BResult MirrorClient::StatLocal(StatRequest &req, ObjStat &objInfo) const
{
    return agent::BioClientAgent::Instance()->StatLocal(req, objInfo);
}

BResult MirrorClient::SendStatRequest(CmPtInfo &ptEntry, StatRequest &req, ObjStat &objInfo)
{
    uint16_t dstNid = ptEntry.masterNodeId;
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(SDK_MIRROR_STAT_RECV_FAIL, &ret, BIO_INNER_RETRY);
    if (dstNid == mLocalNid.VNodeId()) {
        ret = StatLocal(req, objInfo);
    } else {
        ret = StatRemote(dstNid, req, objInfo);
    }
    LVOS_TP_END;
    return ret;
}

BResult MirrorClient::SendNotifyUpdateRequest(bool &flag)
{
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL, &ret, BIO_INNER_RETRY);
    ret = agent::BioClientAgent::Instance()->NotifyUpdate(flag);
    LVOS_TP_END;
    return ret;
}

BResult MirrorClient::SendCheckUpdateReadyRequest()
{
    BResult ret = BIO_INNER_ERR;
    LVOS_TP_START(SDK_MIRROR_CHECK_UPDATE_RECV_FAIL, &ret, BIO_INNER_RETRY);
    ret = agent::BioClientAgent::Instance()->CheckUpdateReady();
    LVOS_TP_END;
    return ret;
}

BResult MirrorClient::ListRemote(uint16_t nid, ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    uint64_t maxSize = sizeof(ObjStat) * 1000U;
    NetMrInfo mr;
    auto ret = net::BioClientNet::Instance()->Alloc(maxSize, mr);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc rdma memory failed.");
        return BIO_ALLOC_FAIL;
    }

    req.address = mr.address;
    req.size = maxSize;
    req.mrKey = mr.key;
    ListResponse rsp;
    ret = net::BioClientNet::Instance()->SendSync<ListRequest, ListResponse>(static_cast<BioNodeId>(nid),
        BIO_OP_SDK_LIST, req, rsp);
    if (ret != BIO_OK) {
        net::BioClientNet::Instance()->Free(mr.address);
        return ret;
    }

    if (rsp.num != 0) {
        auto statInfo = reinterpret_cast<ObjStat *>(mr.address);
        for (uint32_t i = 0; i < rsp.num; i++) {
            ObjStat stat;
            CopyKey(stat.key, statInfo[i].key, KEY_MAX_SIZE);
            stat.size = statInfo[i].size;
            stat.time = statInfo[i].time;
            objs.insert({ stat.key, stat });
        }
    }
    net::BioClientNet::Instance()->Free(mr.address);
    return BIO_OK;
}

BResult MirrorClient::ListLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    return agent::BioClientAgent::Instance()->ListLocal(req, objs);
}

BResult MirrorClient::SendListRequest(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    BResult result = BIO_INNER_ERR;
    uint32_t index = 0;
    for (auto &ptEntry : mPtView) {
        uint16_t dstNid = ptEntry.second.masterNodeId;
        req.isListUnderFs = (index == 0) ? true : false;
        req.comm.ptId = ptEntry.second.ptId;
        req.comm.ptv = ptEntry.second.version;
        LVOS_TP_START(SDK_MIRROR_LIST_RECV_FAIL, &result, BIO_INNER_RETRY);
        if (dstNid == mLocalNid.VNodeId()) {
            result = ListLocal(req, objs);
        } else {
            result = ListRemote(dstNid, req, objs);
        }
        LVOS_TP_END;
        if (result != BIO_OK) {
            CLIENT_LOG_ERROR("Send list request failed, ret:" << result << ", dstNid:" << dstNid << ", ptId:" <<
                ptEntry.second.ptId << ".");
            objs.clear();
            return result;
        }
        index++;
    }
    return BIO_OK;
}

BResult MirrorClient::LoadMaster(LoadRequest &req, uint16_t masterNid, const Bio::LoadCallback &callback, void *context)
{
    if (masterNid == mLocalNid.VNodeId()) {
        BResult ret = BIO_INNER_ERR;
        LVOS_TP_START(SDK_MIRROR_LOAD_RECV_FAIL, &ret, BIO_INNER_RETRY);
        ret = agent::BioClientAgent::Instance()->LoadLocal(req);
        LVOS_TP_END;
        callback(context, ret);
        return BIO_OK;
    }

    auto cbFunc = [&callback, context](void *ctx, void *resp, uint32_t len, int32_t result) {
        LVOS_TP_START(SDK_MIRROR_LOAD_RECV_FAIL, &result, BIO_INNER_RETRY);
        LVOS_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            callback(context, result);
            return;
        }
        BResult hdlRet = *(static_cast<BResult *>(resp));
        callback(context, hdlRet);
    };
    Callback cb(cbFunc, nullptr);
    net::BioClientNet::Instance()->SendAsync<LoadRequest>(static_cast<uint32_t>(masterNid), BIO_OP_SDK_LOAD, req, cb);
    return BIO_OK;
}

inline BResult MirrorClient::SendLoadRequest(CmPtInfo &ptEntry, LoadRequest &req, const Bio::LoadCallback &callback,
    void *context)
{
    return LoadMaster(req, ptEntry.masterNodeId, callback, context);
}

BResult MirrorClient::RebuildNodeView()
{
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView;
    auto ret = agent::BioClientAgent::Instance()->GetClusterNodeView(mCurNodeTimes, nodeView);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get cluster node view failed, ret:" << ret << ".");
        return ret;
    }

    if (!nodeView.empty()) {
        mLock.LockWrite();
        mNodeView.clear();
        mNodeView.swap(nodeView);
        mLock.UnLock();
    }
    CLIENT_LOG_INFO("Cur node times:" << mCurNodeTimes << ", nodeview size:" << mNodeView.size());
    return BIO_OK;
}

BResult MirrorClient::RebuildPtView()
{
    std::map<uint16_t, CmPtInfo> ptView;
    auto ret = agent::BioClientAgent::Instance()->GetPtView(mCurPtTimes, ptView);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get pt view failed, ret:" << ret << ".");
        return ret;
    }

    if (!ptView.empty()) {
        mLock.LockWrite();
        mPtView.clear();
        mPtView.swap(ptView);
        mLock.UnLock();
    }
    CLIENT_LOG_INFO("Cur pt times:" << mCurPtTimes << ", ptview size:" << mPtView.size());
    return BIO_OK;
}
