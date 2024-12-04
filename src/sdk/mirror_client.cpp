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
    static uint32_t createFlowRTimeout = NO_60;
    uint64_t startTime = Monotonic::TimeSec();
    CreateFlowResponse rsp;
    do {
        ret = net::BioClientNet::Instance()->SendSync<CreateFlowRequest, CreateFlowResponse>(
            static_cast<BioNodeId>(nodeId), BIO_OP_SDK_CREATE_FLOW, req, rsp);
        uint64_t retryTime = Monotonic::TimeSec() - startTime;
        if (UNLIKELY(ret == BIO_NOT_READY && retryTime < createFlowRTimeout)) {
            CLIENT_LOG_WARN("Remote cache service not ready, need retry, ret:" << ret << ", nodeId:" << nodeId <<
                ", ptId:" << ptId << ".");
            sleep(NO_3);
        } else {
            break;
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

    ret = Update(ptId, ptEntry.version, flowId, isDegrade);
    if (ret != BIO_OK) {
        LOG_ERROR("Update flow info failed, ret " << ret);
        return ret;
    }

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
    mBioQos = BioQos::Instance();
    if (UNLIKELY(mBioQos == nullptr)) {
        CLIENT_LOG_ERROR("Boostio client qos instance failed.");
        return BIO_INNER_ERR;
    }
    return mBioQos->Initialize(static_cast<uint32_t>(mLocalNid.VNodeId()), mMode, mScene);
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

    auto tempPtView = mPtView;
    for (auto &item : tempPtView) {
        IoStrategy *ioStrategy = new (std::nothrow) IoStrategy();
        if (UNLIKELY(ioStrategy == nullptr)) {
            CLIENT_LOG_ERROR("Alloc io strategy failed.");
            return BIO_ALLOC_FAIL;
        }
        ioStrategy->expired = Monotonic::TimeSec() + IO_EXTRATEGE_TIME;
        ioStrategy->strategy = 0;
        mIoStrategy[item.first] = ioStrategy;
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
    auto tempPtView = mPtView;
    for (auto &item : tempPtView) {
        if (mLocalNid.VNodeId() == item.second.masterNodeId) {
            ans.emplace_back(item.first);
        }
    }
    mLock.UnLock();
    return ans;
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
            CLIENT_LOG_INFO("Select pt delay retry, cost time:" << retryTime << ", times:" << ++retryCnt <<".");
            if (retryTime < mTimeOut) {
                mUpdateView();
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);
    LVOS_TP_START(SDK_MIRROR_SELECT_PT_FAIL, &ptId, UINT16_MAX);
    LVOS_TP_END;
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
    LVOS_TP_START(SDK_MIRROR_SET_PT_ID_FAIL, &ptId, 0);
    LVOS_TP_END;
    if (UNLIKELY(ptId == UINT16_MAX)) {
        CLIENT_LOG_ERROR("Selecting pt failed, objectId:" << objectId << ", affinity:" << affinity << ".");
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
    mUpdateView = updateView;
    mScene = static_cast<WorkerScene>(scene);
    mAlignSize = alignSize;
    mTimeOut = timeOut;

    // 加载视图信息
    BIO_TRACE_START(SDK_TRACE_INIT_LOAD_VIEW);
    BResult ret = LoadOriginView();
    BIO_TRACE_END(SDK_TRACE_INIT_LOAD_VIEW, ret);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Sdk load origin view failed, ret:" << ret << ".");
        return ret;
    }

    // 初始化client端的QOS控制
    ret = InitializeBioQos();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Sdk init qos failed, ret:" << ret << ".");
        return ret;
    }

    CLIENT_LOG_INFO("Mirror client initialize, clcEnable: " << mEnableCrc << "， scene:" << mScene << ", alignSize:" <<
        mAlignSize << ", timeOut:" << mTimeOut << ".");
    return BIO_OK;
}

BResult MirrorClient::Start()
{
    return LoadAffinityFlow();
}

BResult MirrorClient::PutCheckPtState(CmPtInfo ptEntry)
{
    if (ptEntry.state == CM_PT_NORMAL) {
        return BIO_OK;
    }
    mUpdateView();
    return BIO_CHECK_PT_FAIL;
}

BResult MirrorClient::Put(MirrorPut &param)
{
    bool isAllocMem = false;
    char *value = param.value;
    BResult ret = PutAlignSize(value, param, isAllocMem);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Align size failed, ret: " << ret << ", key:" << param.key << ".");
        return ret;
    }

    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    do {
        isRetry = false;
        // 1. Get pt view entry.
        if (UNLIKELY((ret = GetPtEntry(ptId, ptEntry)) != BIO_OK)) {
            break;
        }

        // 2. Check pt view state
        if (UNLIKELY((ret = PutCheckPtState(ptEntry)) != BIO_OK)) {
            break;
        }

        // 3. Apply for write cache quota.
        QosApplyParam applyParam{startTime, param.key, param.length};
        ret = mBioQos->Apply(QOS_CONCURRENCY | QOS_QUOTA, QUOTA_WRITE, applyParam, &ptEntry);
        if (LIKELY(ret == BIO_OK)) {
            // 3. Put value to write cache.
            ret = PutImpl(param, ptId, ptEntry);
            mBioQos->Release(QOS_CONCURRENCY, QUOTA_WRITE);
            if (LIKELY(ret == BIO_OK)) {
                break;
            }
        }
        isRetry = FailHandler(ret, startTime, NO_44);
    } while (isRetry);

    if (isAllocMem) {
        free(param.value);
        param.value = value;
    }
    return ret;
}

BResult MirrorClient::PreparePutWithSpace(MirrorPut &param, CmPtInfo &ptEntry, CacheSpaceDesc &spaceInfo,
    PutRequest *&req)
{
    if (spaceInfo.descriptorSize > CACHE_SPACE_DEC_SIZE) {
        CLIENT_LOG_ERROR("Too large descriptorSize:" << spaceInfo.descriptorSize << ", it should be less than " <<
            CACHE_SPACE_DEC_SIZE << ".");
        return BIO_INNER_ERR;
    }

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
    mBioQos->GetKey(req->quotaNid, req->quotaCid);
    if (mIoStrategy[ptEntry.ptId]->expired > Monotonic::TimeSec()) {
        req->ioStrategy = mIoStrategy[ptEntry.ptId]->strategy;
    } else {
        req->ioStrategy = 0;
    }
    req->memFromServer = true;
    req->mrAddress = 0ULL;
    auto ret = memcpy_s(req->sliceBuf, spaceInfo.descriptorSize, spaceInfo.descriptorInfo, spaceInfo.descriptorSize);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
        delete[] reqTmp;
        req = nullptr;
    }
    return ret;
}

BResult MirrorClient::PutImpl(MirrorPut &param, CacheSpaceDesc &spaceInfo)
{
    uint16_t ptId = ParseLocation(spaceInfo.loc);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return ret;
    }

    // 填充请求request.
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
    ret = SendPutRequestImpl(ptEntry, param, req);
    BIO_TRACE_END(SDK_TRACE_PUT_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send put request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
    }
    delete[] static_cast<uint8_t *>(static_cast<void *>(req));
    return ret;
}

BResult MirrorClient::PutAlignSize(const char *value, MirrorPut &param, bool &isAllocMem)
{
    if (UNLIKELY((mScene == SCENE_BIGDATA) && (param.length % mAlignSize != 0))) {
        uint64_t length = (((param.length) + (mAlignSize)-1) & ~((mAlignSize)-1));
        BIO_TRACE_START(SDK_TRACE_PUT_ALIGN_IO);
        if ((param.value = static_cast<char *>(malloc(length))) == nullptr) {
            BIO_TRACE_END(SDK_TRACE_PUT_ALIGN_IO, BIO_ALLOC_FAIL);
            CLIENT_LOG_ERROR("Alloc memory failed, size:" << length << ".");
            return BIO_ALLOC_FAIL;
        }
        auto ret = memcpy_s(param.value, length, value, param.length);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
            free(param.value);
            param.value = nullptr;
            return BIO_INNER_ERR;
        }
        param.length = length;
        isAllocMem = true;
        BIO_TRACE_END(SDK_TRACE_PUT_ALIGN_IO, BIO_OK);
    }
    return BIO_OK;
}

bool MirrorClient::FailHandler(const BResult result, uint64_t startTime, uint64_t timeOut)
{
    uint64_t costTime = Monotonic::TimeSec() - startTime;
    LVOS_TP_START(SDK_MIRROR_CLIENT_SET_RETRY_TIME, &costTime, (mTimeOut+1));
    LVOS_TP_END;
    if (UNLIKELY(costTime >= timeOut)) { // 超过重试时间则不再进行重试.
        return false;
    }

    bool isRetry = false;
    uint16_t sleepTime = 0;
    switch (result) {
        case BIO_ALLOC_FAIL:
        case BIO_INNER_RETRY:
        case BIO_NET_RETRY:
        case BIO_CHECK_PT_FAIL:
        case BIO_DISK_IOERR:
            isRetry = true;
            sleepTime = BIO_IO_INTERAL_TIME;
            mUpdateView(); // 更新视图.
            break;
        case BIO_QUOTA_NOT_ENOUGH:
            isRetry = true;
            sleepTime = NO_1;
            break;
        default:
            isRetry = false;
            break;
    }

    sleep(sleepTime);
    return isRetry;
}

BResult MirrorClient::Put(MirrorPut &param, CacheSpaceDesc &spaceInfo)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = PutImpl(param, spaceInfo);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);
    return ret;
}

BResult MirrorClient::PutImpl(MirrorPut &param, uint16_t ptId, CmPtInfo &ptEntry)
{
    BIO_TRACE_START(SDK_TRACE_PUT_ALLOC_OFF);
    auto ret = AllocPutOffset(ptId, ptEntry.version, param.length, param.flowId, param.flowOffset, param.flowIndex);
    BIO_TRACE_END(SDK_TRACE_PUT_ALLOC_OFF, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc put offset failed, ret:" << ret << ", ptId:" << ptId << ", flowId:" << param.flowId <<
            ", key:" << param.key << ".");
        ret = (ret == BIO_NOT_EXISTS) ? BIO_INNER_RETRY : ret; // Flow实例不存在则内部重试.
        return ret;
    }

    ret = SendPutRequest(ptEntry, param);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send put request failed, ret:" << ret << ", ptId:" << ptId <<
            ", flowId:" << param.flowId << ", key:" << param.key << ".");
        Delete(ptId, param.flowId); // Put请求失败则删除该Flow, 不允许在此Flow上继续写.
        ret = (ret == BIO_NOT_EXISTS) ? BIO_INNER_RETRY : ret;
    }
    return ret;
}

BResult MirrorClient::Get(MirrorGet &param, uint64_t &realLen)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        QosApplyParam applyParam{startTime, param.key};
        ret = mBioQos->Apply(QOS_CONCURRENCY, QUOTA_READ, applyParam);
        ret = GetImpl(param, realLen);
        mBioQos->Release(QOS_CONCURRENCY, QUOTA_READ);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, NO_55);
    } while (isRetry);
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
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = DeleteKeyImpl(key, location);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
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
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = LoadImpl(key, offset, length, location, callback, context);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
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
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = ListAllImpl(prefix, objs);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
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
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = StatObjectImpl(key, location, stat);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
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

BResult MirrorClient::AllocSpaceImpl(uint16_t ptId, CmPtInfo &ptEntry, MirrorPut &param, CacheSpaceDesc &spaceInfo)
{
    // 1. 申请Flow偏移
    BIO_TRACE_START(SDK_TRACE_PUT_ALLOC_OFF);
    auto ret = AllocPutOffset(ptId, ptEntry.version, param.length, param.flowId, param.flowOffset, param.flowIndex);
    BIO_TRACE_END(SDK_TRACE_PUT_ALLOC_OFF, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc put offset failed, ret:" << ret << ", ptId:" << ptId << ", ptv:" << ptEntry.version
            << ", length:" << param.length << ".");
        return ret;
    }

    // 2. 申请写资源
    GetSliceResponse *rsp = nullptr;
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_GET_SLICE);
    ret = agent::BioClientAgent::Instance()->PrepareResource(ptEntry, param.flowId, param.flowOffset,
        param.flowIndex, param.length, &rsp);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE_GET_SLICE, BIO_OK);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc put space failed, ret:" << ret << ", flowId:" << param.flowId << ", flowOffset:" <<
            param.flowOffset << ", length:" << param.length << ".");
        Delete(ptId, param.flowId); // 申请失败删除该Flow, 不允许在该Flow上申请资源.
        return ret;
    }
    if (rsp == nullptr) {
        return BIO_ERR;
    }
    // 3. 拷贝空间地址信息
    spaceInfo.descriptorSize = rsp->sliceLen;
    ret = memcpy_s(spaceInfo.descriptorInfo, CACHE_SPACE_DEC_SIZE, rsp->sliceBuf, rsp->sliceLen);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Failed to data copy, sliceLen:" << rsp->sliceLen << ".");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        Delete(ptId, param.flowId); // 拷贝失败删除该Flow, 不允许在该Flow上申请资源.
        return BIO_INNER_ERR;
    }

    LVOS_TP_START(SDK_MIRROR_CLIENT_ADDRNUM_INVALID, &rsp->addrNum, (SLICE_ADDR_MAX_SIZE + 1));
    LVOS_TP_END;
    if (rsp->addrNum > CACHE_SPACE_ADDRESS_SIZE) {
        CLIENT_LOG_ERROR("rsp addrNum: " << rsp->addrNum << " is invalid.");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        Delete(ptId, param.flowId); // 拷贝失败删除该Flow, 不允许在该Flow上申请资源.
        return BIO_INNER_ERR;
    }
    spaceInfo.addressNum = rsp->addrNum;
    for (uint32_t idx = 0; idx < spaceInfo.addressNum; idx++) {
        if (mMode == CONVERGENCE) {
            spaceInfo.address[idx].address = rsp->addr[idx].chunkId + rsp->addr[idx].chunkOffset;
        } else {
            uint8_t *realAddr = net::BioClientNet::Instance()->GetShmAddress(rsp->addrOffset[idx], rsp->addr[idx].chunkLen);
            if (realAddr == nullptr) {
                CLIENT_LOG_ERROR("Alloc space get shm addr failed.");
                return BIO_INNER_ERR;
            }
            spaceInfo.address[idx].address = reinterpret_cast<uintptr_t>(realAddr);
        }
        spaceInfo.address[idx].size = rsp->addr[idx].chunkLen;
    }

    delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
    return BIO_OK;
}

BResult MirrorClient::AllocSpace(MirrorClient::MirrorPut &param, CacheSpaceDesc &spaceInfo)
{
    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    bool isRetry = false;
    BResult ret = BIO_OK;
    uint64_t startTime = Monotonic::TimeSec();
    do {
        isRetry = false;
        // 1. Get pt view entry.
        if (UNLIKELY((ret = GetPtEntry(ptId, ptEntry)) != BIO_OK)) {
            break;
        }

        // 2. Apply for write cache quota.
        static std::atomic<uint64_t> ref(1);
        std::string innerKey = "BioAllocSpace" + std::to_string(ref++);
        QosApplyParam applyParam{startTime, innerKey.c_str(), param.length};
        ret = mBioQos->Apply(QOS_CONCURRENCY | QOS_QUOTA, QUOTA_WRITE, applyParam, &ptEntry);
        if (LIKELY(ret == BIO_OK)) {
            // 3. Alloc write cache space.
            ret = AllocSpaceImpl(ptId, ptEntry, param, spaceInfo);
            mBioQos->Release(QOS_CONCURRENCY, QUOTA_WRITE);
            if (LIKELY(ret == BIO_OK)) {
                break;
            }
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);

    return ret;
}

BResult MirrorClient::AllocPutOffset(uint16_t ptId, uint64_t ptv, uint64_t len, uint64_t &flowId, uint64_t &offset,
    uint64_t &index)
{
    // 1. 查找到该PT对应的FLOW.
    FlowInstancePtr flowInst = Query(ptId);
    if (UNLIKELY(flowInst == nullptr)) {
        BResult ret = CreateFlow(ptId);
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

    // 2. 检查FLOW状态.
    bool isNormal = false;
    LVOS_TP_START(SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL, &isNormal, false);
    isNormal = flowInst->IsNormal();
    LVOS_TP_END;
    if (UNLIKELY(!isNormal)) {
        CLIENT_LOG_WARN("Check flow failed, ptId:" << ptId << ".");
        return BIO_INNER_RETRY;
    }
    if (UNLIKELY(flowInst->Version() != ptv)) {
        Delete(ptId, flowInst->FlowId()); // Flow版本号不一致则需要删除Flow, 再重新创建Flow.
        BResult ret = CreateFlow(ptId);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Create flow instance failed, need retry, ret: " << ret << ", ptId:" << ptId << ".");
        }
        return BIO_INNER_RETRY;
    }

    // 3. 申请Flow偏移
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
    mBioQos->GetKey(req->quotaNid, req->quotaCid);
    if (mIoStrategy[ptEntry.ptId]->expired > Monotonic::TimeSec()) {
        req->ioStrategy = mIoStrategy[ptEntry.ptId]->strategy;
    } else {
        req->ioStrategy = 0;
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
    mBioQos->GetKey(req->quotaNid, req->quotaCid);
    if (mIoStrategy[ptEntry.ptId]->expired > Monotonic::TimeSec()) {
        req->ioStrategy = mIoStrategy[ptEntry.ptId]->strategy;
    } else {
        req->ioStrategy = 0;
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
            realAddr = net::BioClientNet::Instance()->GetShmAddress(offset[i], addr[i].chunkLen);
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
    LVOS_TP_START(SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY, 0);
    for (uint32_t i = 0; i < ptEntry.copys.size(); i++) {
        if (ptEntry.copys[i].nodeId == mLocalNid.VNodeId() &&
            (ptEntry.copys[i].state == CM_COPY_RUNNING || ptEntry.copys[i].state == CM_COPY_RECOVERY)) {
            return true;
        }
    }
    LVOS_TP_END;
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
    if (rsp == nullptr) {
        return BIO_ERR;
    }

    LVOS_TP_START(SDK_MIRROR_RSP_NUM_ERROR, &(rsp->addrNum), (SLICE_ADDR_MAX_SIZE + 1));
    LVOS_TP_END;
    if (UNLIKELY(rsp->addrNum > SLICE_ADDR_MAX_SIZE)) {
        CLIENT_LOG_ERROR("rsp addrNum: " << rsp->addrNum << " is invalid.");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        return BIO_INNER_ERR;
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

    uint8_t* tmp = nullptr;
    LVOS_TP_START(SDK_MIRROR_CLIENT_PREPARE_FAIL, 0);
    tmp = new (std::nothrow) uint8_t[sizeof(PutRequest)];
    LVOS_TP_END;
    if (tmp == nullptr) {
        CLIENT_LOG_ERROR("Alloc memory failed.");
        net::BioClientNet::Instance()->Free(mr.address);
        return BIO_ALLOC_FAIL;
    }
    req = static_cast<PutRequest *>(static_cast<void *>(tmp));
    ConstructPutReq(req, ptEntry, param, param.flowId, param.flowOffset, param.flowIndex, mr);
    req->memFromServer = false;
    return BIO_OK;
}

BResult MirrorClient::Prepare(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req)
{
    if (IsExistLocalCopy(ptEntry)) {
        return PrepareFromServer(ptEntry, param, req); // 写资源从本地server申请.
    } else {
        return PrepareFromClient(ptEntry, param, req); // 写资源从client端申请.
    }
}

void MirrorClient::PutRemote(PutRequest *req, CmPtInfo &ptEntry, std::vector<uint32_t> &indexVec, Callback &callback)
{
    for (uint32_t i = 0; i < indexVec.size(); i++) {
        uint16_t dstNid = ptEntry.copys[indexVec[i]].nodeId;
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

BResult MirrorClient::SendPutRequestImpl(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *req)
{
    uint32_t quota = CalcPtQuota(ptEntry);
    ClientCallbackCtx cbCtx;
    InitCallbackCtx(cbCtx, quota);
    std::atomic<uint32_t> ioStrategy(0);

    auto cbFunc = [&ioStrategy](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        LVOS_TP_START(SDK_MIRROR_PUT_RECV_FAIL, &(cbCtx->result), BIO_INNER_RETRY);
        LVOS_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else if (resp != nullptr) {
            auto rsp = static_cast<PutResponse *>(resp);
            if (rsp->ioStrategy > WRITE_UNDERFS_BACK) {
                cbCtx->result = BIO_INVALID_PARAM;
            } else {
                ioStrategy = (rsp->ioStrategy > ioStrategy) ? rsp->ioStrategy : ioStrategy.load();
            }
        } else {
            cbCtx->result = BIO_INVALID_PARAM;
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
        mIoStrategy[ptEntry.ptId]->expired = Monotonic::TimeSec() + IO_EXTRATEGE_TIME;
        mIoStrategy[ptEntry.ptId]->strategy = ioStrategy.load();
    }
    return cbCtx.result;
}

BResult MirrorClient::SendPutRequest(CmPtInfo &ptEntry, MirrorPut &param)
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
    req->dataCrc = mEnableCrc ? BioCrcUtil::Crc32(param.value, param.length) : 0;

    BIO_TRACE_START(SDK_TRACE_PUT_SEND);
    ret = SendPutRequestImpl(ptEntry, param, req);
    BIO_TRACE_END(SDK_TRACE_PUT_SEND, ret);
    delete[] static_cast<uint8_t *>(static_cast<void *>(req));
    return ret;
}

bool MirrorClient::CheckGetRsp(GetResponse rsp)
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
        if (!CheckGetRsp(rsp)) {
            return BIO_INVALID_PARAM;
        }
        realLen = rsp.realLen;
        if (realLen > mrInfo.size) {
            CLIENT_LOG_ERROR("Read length greater than value size, realLen:" << realLen << ", size:" << mrInfo.size);
            return BIO_INNER_ERR;
        }
        BIO_TRACE_START(SDK_TRACE_GET_COPY2U);
        ret = memcpy_s(value, req.length, reinterpret_cast<void *>(mrInfo.address), realLen);
        BIO_TRACE_END(SDK_TRACE_GET_COPY2U, ret);
        if (UNLIKELY(ret != 0)) {
            CLIENT_LOG_ERROR("Copy data to user failed, ret:" << ret << ".");
        }
        if (req.enableCrc) {
            uint32_t currentCrc = BioCrcUtil::Crc32(value, realLen);
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
    CLIENT_LOG_DEBUG("Get master start, masterNid:" << masterNid << ", localNid:" << mLocalNid.VNodeId() << ", key:" <<
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
        if (ctx == nullptr) {
            LOG_ERROR("Send delete request callback ctx null");
            return;
        }
        if (resp == nullptr) {
            LOG_ERROR("Send delete request resp null");
            return;
        }
        auto cbCtx = (ClientCallbackCtx *)ctx;
        LVOS_TP_START(SDK_MIRROR_DELETE_RECV_FAIL, &result, BIO_INNER_RETRY);
        LVOS_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else {
            BResult res = resp != nullptr ? *(static_cast<BResult *>(resp)) : BIO_INNER_ERR;
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

    if (objInfo.size > BIO_IO_MAX_LEN) {
        return BIO_INVALID_PARAM;
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
    LVOS_TP_START(LISTALL_REMOTE_RSP_OVER_LIMIT, &rsp.num, 1500U);
    ret = net::BioClientNet::Instance()->SendSync<ListRequest, ListResponse>(static_cast<BioNodeId>(nid),
        BIO_OP_SDK_LIST, req, rsp);
    LVOS_TP_END;
    if (ret != BIO_OK) {
        net::BioClientNet::Instance()->Free(mr.address);
        return ret;
    }

    if (rsp.num != 0) {
        size_t objSize = 0;
        auto statInfo = reinterpret_cast<ObjStat *>(mr.address);
        for (uint32_t i = 0; i < rsp.num; i++) {
            objSize = objs.size();
            LVOS_TP_START(LISTALL_REMOTE_OVER_1000, &objSize, 1500U);
            LVOS_TP_END;
            if (objSize >= 1000U) {
                break;
            }
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
    BResult ret = BIO_OK;
    uint32_t index = 0;

    auto tempPtView = mPtView;
    for (auto &ptEntry : tempPtView) {
        uint16_t dstNid = ptEntry.second.masterNodeId;
        req.isListUnderFs = (index == 0);
        req.comm.ptId = ptEntry.second.ptId;
        req.comm.ptv = ptEntry.second.version;
        LVOS_TP_START(SDK_MIRROR_LIST_RECV_FAIL, &ret, BIO_INNER_RETRY);
        if (dstNid == mLocalNid.VNodeId()) {
            ret = ListLocal(req, objs);
        } else {
            ret = ListRemote(dstNid, req, objs);
        }
        LVOS_TP_END;
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Send list request failed, ret:" << ret << ", dstNid:" << dstNid << ", ptId:" <<
                ptEntry.second.ptId << ".");
            objs.clear();
            break;
        } else if (objs.size() > NO_1000) {
            break;
        }
        index++;
    }

    return ret;
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
        if (resp == nullptr) {
            callback(context, BIO_ERR);
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
