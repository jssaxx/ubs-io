/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <functional>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <iomanip>
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

constexpr uint16_t SDK_DISPATH_BATCH_GET_THREAD_NUM = 128;
constexpr uint16_t SDK_DISPATH_BATCH_GET_QUEUE_SIZE = 8192;
constexpr uint16_t SDK_DISPATH_BATCH_EXIST_THREAD_NUM = 128;
constexpr uint16_t SDK_DISPATH_BATCH_EXIST_QUEUE_SIZE = 8192;
constexpr uint16_t SDK_DISPATH_BATCH_COUNT_MAX_NUM = 128;

BResult MirrorClient::SendCreateFlowRequestRemote(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
    FlowInfo &flowInfo)
{
    BResult ret = BIO_OK;
    CreateFlowRequest req;
    if (opType == 0) {
        req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() }, opType, 0, false };
    } else if (opType == 1) {
        req = { { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() },
            opType,
            flowInfo.flowId,
            flowInfo.isDegrade };
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
        flowInfo = { rsp.flowId, rsp.isDegrade, rsp.index, rsp.offset, rsp.isNewFlow };
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
    }
    return ret;
}

BResult MirrorClient::CreateFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
    FlowInfo &flowInfo)
{
    if (LIKELY(nodeId == mLocalNid.VNodeId())) {
        return agent::BioClientAgent::Instance()->CreateFlowLocal(getpid(), ptEntry, ptId, opType, flowInfo);
    } else {
        return SendCreateFlowRequestRemote(nodeId, ptEntry, ptId, opType, flowInfo);
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

    FlowInfo flowInfo = { UINT64_MAX, false, 0, 0, true };
    ret = CreateFlowImpl(ptEntry.masterNodeId, ptEntry, ptId, 0, flowInfo);
    if (UNLIKELY(ret != BIO_OK || flowInfo.flowId == UINT64_MAX)) {
        CLIENT_LOG_ERROR("Create master flow failed, ret:" << ret << ", ptId:" << ptId << ", masterNid:" <<
            ptEntry.masterNodeId << ".");
        Delete(ptId, 0);
        return ret;
    }

    if (flowInfo.isNewFlow) {
        for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
            if (ptEntry.copys[idx].nodeId == ptEntry.masterNodeId) {
                continue;
            }
            if (ptEntry.copys[idx].state != CM_COPY_RUNNING && ptEntry.copys[idx].state != CM_COPY_RECOVERY) {
                continue;
            }
            ret = CreateFlowImpl(ptEntry.copys[idx].nodeId, ptEntry, ptId, 1, flowInfo);
            if (UNLIKELY(ret != BIO_OK)) {
                CLIENT_LOG_ERROR("Create slave flow failed, ret:" << ret << ", ptId:" << ptId << ", slaveNid:" <<
                    ptEntry.copys[idx].nodeId << ".");
                Delete(ptId, 0);
                return ret;
            }
        }
    }

    UpdateParams para{ ptId, ptEntry.version, flowInfo.flowId, flowInfo.isDegrade, flowInfo.index, flowInfo.offset };
    ret = Update(para);
    if (ret != BIO_OK) {
        LOG_ERROR("Update flow info failed, ret " << ret);
        return ret;
    }

    CLIENT_LOG_INFO("Create flow instance success, ptId:" << ptId << ", ptv:" << ptEntry.version << ", flowId:" <<
        flowInfo.flowId << ", isDegrade:" << flowInfo.isDegrade << ", index:" << flowInfo.index << ", offset:" <<
        flowInfo.offset << ", isNewFlow:" << flowInfo.isNewFlow << ".");
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

BResult MirrorClient::CreateDataMessageMemRemote()
{
    // 1. 从本端Server申请一个内存池.
    uint64_t dataMemSize = (1024UL * 1024UL * 1024UL * 5UL);
    int32_t memFd = 0;
    uint64_t offset = 0;
    uint64_t blockSize = 0;
    auto ret = agent::BioClientAgent::Instance()->CreateDataMessageMemPool(getpid(),
                                                                           dataMemSize, memFd, offset, blockSize);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Create data message memory pool failed, ret:" << ret << ".");
        return ret;
    }
    if (memFd != -1 && offset != 0) {
        CLIENT_LOG_ERROR("Create data message memory pool failed, offset:" << offset << ", len:" << dataMemSize << ".");
        return BIO_ERR;
    }
    int32_t realFd = -1;
    auto result = net::BioClientNet::Instance()->ReceiveFds(INVALID_NID, &realFd, 1U);
    if (result != BIO_OK) {
        CLIENT_LOG_ERROR("receive file mem fd failed, ret:" << result << ".");
        return BIO_ERR;
    }

    auto address = mmap(nullptr, dataMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, realFd, offset);
    if (address == MAP_FAILED) {
        CLIENT_LOG_ERROR("Mmap shm size " << dataMemSize << " offset " << offset << " failed, err:" << strerror(errno));
        close(realFd);
        realFd = -1;
        return BIO_ERR;
    }
    memset_s(address, dataMemSize, 0, dataMemSize);
    mDataMsgMemAddr = static_cast<uint8_t *>(address);
    mDataMsgMemSize = dataMemSize;
    mDataMsgMemFd = realFd;
    mDataMsgMemBlockSize = blockSize;
    // 2. 注册RDMA内存.
    ret = net::BioClientNet::Instance()->RegisterMemoryRegion(mDataMsgMemAddr, dataMemSize, mDataMsgMemMr);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to register mr by size " << dataMemSize << ".");
        if (munmap(address, dataMemSize) == -1) {
            NET_LOG_ERROR("munmap address failed.");
        }
        close(realFd);
        realFd = -1;
        mDataMsgMemAddr = nullptr;
        mDataMsgMemSize = 0;
        mDataMsgMemFd = -1;
        return ret;
    }

    // 3. 创建memory block分配器.
    mDataMsgMemPool = MakeRef<NetBlockPool>();
    if (mDataMsgMemPool == nullptr) {
        CLIENT_LOG_ERROR("Make block pool ptr failed.");
        net::BioClientNet::Instance()->GetNetEngine()->DestroyMemoryRegion(mDataMsgMemMr);
        if (munmap(address, dataMemSize) == -1) {
            NET_LOG_ERROR("munmap address failed.");
        }
        close(realFd);
        realFd = -1;
        mDataMsgMemAddr = nullptr;
        mDataMsgMemSize = 0;
        mDataMsgMemFd = -1;
        return BIO_ALLOC_FAIL;
    }
    ret = mDataMsgMemPool->Start(reinterpret_cast<uintptr_t>(mDataMsgMemAddr),
                                 mDataMsgMemBlockSize, dataMemSize / mDataMsgMemBlockSize);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start block pool " << dataMemSize << ".");
        net::BioClientNet::Instance()->GetNetEngine()->DestroyMemoryRegion(mDataMsgMemMr);
        if (munmap(address, dataMemSize) == -1) {
            NET_LOG_ERROR("munmap address failed.");
        }
        close(realFd);
        realFd = -1;
        mDataMsgMemAddr = nullptr;
        mDataMsgMemSize = 0;
        mDataMsgMemFd = -1;
        mDataMsgMemPool = nullptr;
    }  else {
        CLIENT_LOG_INFO("Create data message memory pool success, size:" << dataMemSize <<
            ", blockSize:" << mDataMsgMemBlockSize);
    }
    return ret;
}

BResult MirrorClient::CreateDataMessageMemLocal()
{
    // 1. 注册RDMA内存.
    const uint64_t dataMemSize = (128UL * 1024UL * 1024UL); // 默认128MB内存空间
    auto ret = net::BioClientNet::Instance()->RegisterMemoryRegion(dataMemSize, mDataMsgMemMr);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to register mr by size " << dataMemSize);
        return ret;
    }
    mDataMsgMemAddr = reinterpret_cast<uint8_t *>(mDataMsgMemMr.GetAddress());
    mDataMsgMemSize = dataMemSize;
    mDataMsgMemFd = -1;
    mDataMsgMemBlockSize = NO_4194304;
    // 2. 创建memory block分配器.
    mDataMsgMemPool = MakeRef<NetBlockPool>();
    if (mDataMsgMemPool == nullptr) {
        CLIENT_LOG_ERROR("Make block pool ptr failed.");
        net::BioClientNet::Instance()->GetNetEngine()->DestroyMemoryRegion(mDataMsgMemMr);
        mDataMsgMemAddr = nullptr;
        mDataMsgMemSize = 0;
        mDataMsgMemBlockSize = NO_4096 * NO_1024;
        return BIO_ALLOC_FAIL;
    }
    ret = mDataMsgMemPool->Start(reinterpret_cast<uintptr_t>(mDataMsgMemAddr),
                                 mDataMsgMemBlockSize, dataMemSize / mDataMsgMemBlockSize);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to start block pool " << dataMemSize << ".");
        net::BioClientNet::Instance()->GetNetEngine()->DestroyMemoryRegion(mDataMsgMemMr);
        mDataMsgMemAddr = nullptr;
        mDataMsgMemSize = 0;
        mDataMsgMemBlockSize = NO_4096 * NO_1024;
        mDataMsgMemPool = nullptr;
    }  else {
        CLIENT_LOG_INFO("Create data message memory pool success, size:" << dataMemSize << ".");
    }
    return ret;
}

BResult MirrorClient::CreateDataMessageMem()
{
    DestroyDataMessageMem();
    if (mMode == CONVERGENCE) {
        return CreateDataMessageMemLocal();
    } else {
        return CreateDataMessageMemRemote();
    }
}

void MirrorClient::DestroyDataMessageMem()
{
    if (mDataMsgMemPool != nullptr) {
        mDataMsgMemPool->Stop();
        mDataMsgMemPool = nullptr;
    }
    if (mDataMsgMemAddr != nullptr) {
        net::BioClientNet::Instance()->GetNetEngine()->DestroyMemoryRegion(mDataMsgMemMr);
        if (mMode == SEPARATES && mDataMsgMemSize != 0) {
            if (munmap(mDataMsgMemAddr, mDataMsgMemSize) == -1) {
                NET_LOG_ERROR("munmap data message memory failed.");
            }
        }
        mDataMsgMemAddr = nullptr;
    }
    if (mDataMsgMemFd >= 0) {
        close(mDataMsgMemFd);
        mDataMsgMemFd = -1;
    }
    mDataMsgMemSize = 0;
    mDataMsgMemBlockSize = NO_4096 * NO_1024;
}

BResult MirrorClient::RecoverDataMessageMem()
{
    if (mMode == CONVERGENCE) {
        return BIO_OK;
    }
    CLIENT_LOG_INFO("Recover mirror client data message memory pool.");
    return CreateDataMessageMem();
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
    BResult ret = BIO_OK;

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

void MirrorClient::FreeIoStrategy()
{
    DestroyDataMessageMem();
    for (auto &ioStrategy : mIoStrategy) {
        delete ioStrategy.second;
    }
    mIoStrategy.clear();
    return;
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
    BIO_TP_START(SDK_MIRROR_SELECT_PT_FAIL, &ptId, UINT16_MAX);
    BIO_TP_END;
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
    BIO_TP_START(SDK_MIRROR_SET_PT_ID_FAIL, &ptId, 0);
    BIO_TP_END;
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
    BIO_TP_START(SDK_MIRROR_PT_VIEW_FIND_FAIL, &isFind, false);
    isFind = (iter != mPtView.end());
    BIO_TP_END;
    if (UNLIKELY(!isFind)) {
        mLock.UnLock();
        CLIENT_LOG_ERROR("Invalid pt id:" << ptId << ".");
        return BIO_INVALID_PARAM;
    }

    bool isPtNormal = false;
    BIO_TP_START(SDK_MIRROR_CHECK_PT_FAIL, &isPtNormal, false);
    isPtNormal = (iter->second.state != CM_PT_FAULT);
    BIO_TP_END;
    if (UNLIKELY(!isPtNormal)) {
        CLIENT_LOG_ERROR("Pt stat is fault, pt id:" << ptId << ", state:" << iter->second.state << ".");
        mLock.UnLock();
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
    uint32_t timeOut, bool enableCrc, bool enableTrans)
{
    mEnableTrance = enableTrans;
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

    mBatchGetExecutor = ExecutorService::Create(SDK_DISPATH_BATCH_GET_THREAD_NUM,
                                                SDK_DISPATH_BATCH_GET_QUEUE_SIZE);
    if (UNLIKELY(mBatchGetExecutor == nullptr)) {
        LOG_ERROR("Failed to create execution service for get kv, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    ret = mBatchGetExecutor->Start();
    if (!ret) {
        LOG_ERROR("Failed to start execution service for get kv, probably out of memory");
        return BIO_INNER_ERR;
    }

    mBatchExistExecutor = ExecutorService::Create(SDK_DISPATH_BATCH_EXIST_THREAD_NUM,
                                                  SDK_DISPATH_BATCH_EXIST_QUEUE_SIZE);
    if (UNLIKELY(mBatchExistExecutor == nullptr)) {
        LOG_ERROR("Failed to create execution service for get kv, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    ret = mBatchExistExecutor->Start();
    if (!ret) {
        LOG_ERROR("Failed to start execution service for get kv, probably out of memory");
        return BIO_INNER_ERR;
    }

    CLIENT_LOG_INFO("Mirror client initialize, clcEnable: " << mEnableCrc << "， scene:" << mScene << ", alignSize:" <<
        mAlignSize << ", timeOut:" << mTimeOut << ".");
    return BIO_OK;
}

BResult MirrorClient::Start()
{
    // 1. 创建数据消息内存池.
    auto ret = CreateDataMessageMem();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to create mirror client data message memory, ret:" << ret << ".");
        return ret;
    }

    // 2. 创建亲和的Flow.
    ret = LoadAffinityFlow();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
    }
    return ret;
}

BResult MirrorClient::PutCheckPtState(CmPtInfo ptEntry)
{
    if (ptEntry.state == CM_PT_NORMAL || ptEntry.state == CM_PT_BYPASS) {
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
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);

    if (isAllocMem) {
        free(param.value);
        param.value = value;
    }
    return ret;
}

BResult MirrorClient::AsyncPut(MirrorPut &param, BioAsyncPutCallback callback, void* context)
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
            isRetry = FailHandler(ret, startTime, mTimeOut);
            continue;
        }

        // 2. Apply for write cache quota.
        QosApplyParam applyParam{startTime, param.key, param.length};
        ret = mBioQos->Apply(QOS_CONCURRENCY | QOS_QUOTA, QUOTA_WRITE, applyParam, &ptEntry);
        if (LIKELY(ret == BIO_OK)) {
            // 3. Put value to write cache.
            ret = AsyncPutImpl(param, ptId, ptEntry, callback, context);
            mBioQos->Release(QOS_CONCURRENCY, QUOTA_WRITE);
            if (LIKELY(ret == BIO_OK)) {
                break;
            }
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
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
    if (spaceInfo.descriptorSize == 0 || spaceInfo.descriptorSize > CACHE_SPACE_DEC_SIZE) {
        CLIENT_LOG_ERROR("Too large descriptorSize:" << spaceInfo.descriptorSize << ", it should be less than " <<
            CACHE_SPACE_DEC_SIZE << ".");
        return BIO_INNER_ERR;
    }

    uint8_t *reqTmp = nullptr;
    BIO_TP_START(SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL, 0);
    reqTmp = new(std::nothrow) uint8_t[sizeof(PutRequest) + spaceInfo.descriptorSize];
    BIO_TP_END;
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
    BIO_TP_START(SDK_MIRROR_CLIENT_SET_RETRY_TIME, &costTime, (mTimeOut+1));
    BIO_TP_END;
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

BResult MirrorClient::AsyncPutImpl(MirrorPut &param, uint16_t ptId, CmPtInfo &ptEntry, BioAsyncPutCallback callback,
    void* context)
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

    ret = SendAsyncPutRequest(ptEntry, param, callback, context);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send async put request failed, ret:" << ret << ", ptId:" << ptId <<
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
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);
    return ret;
}

BResult MirrorClient::BatchGetKeyDiskAddr(MirrorBatchGetKeyAddr &param)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = BatchGetKeyDiskAddrImpl(param);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);
    return ret;
}

BResult MirrorClient::BatchGet(CacheAttr attr, const char **keys, const uint32_t count, uint64_t *offsets,
                               uint64_t *lengths, ObjLocation *locations, uintptr_t *valueAddrs,
                               uint64_t *realLengths, int32_t *results)
{
    MirrorClient::MirrorBatchGet param{ { attr.mTenantId, attr.affinity, attr.strategy },
                                        keys, count, offsets, lengths, locations,
                                        valueAddrs, realLengths, results };
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = BatchGetImpl(param);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);
    return ret;
}

inline void MirrorClient::DispathBatchGetRecycleResource(uint32_t parallelNum,
                                                         DispathBatchGetResult *taskResults, uintptr_t *valueAddrs)
{
    for (uint32_t i = 0; i < parallelNum; i++) {
        if (taskResults[i].result == BIO_OK) {
            uint32_t basic = i * SDK_DISPATH_BATCH_COUNT_MAX_NUM;
            for (uint32_t j = 0; j < taskResults[i].count; j++) {
                mDataMsgMemPool->ReleaseOne(valueAddrs[basic + j]);
            }
        }
    }
}

BResult MirrorClient::DispathBatchGet(CacheAttr attr, const char **keys, const uint32_t count, uint64_t *offsets,
                                      uint64_t *lengths, ObjLocation *locations, uintptr_t *valueAddrs,
                                      uint64_t *realLengths, int32_t *results)
{
    BResult ret = BIO_OK;
    uint32_t parallelNum = (count + SDK_DISPATH_BATCH_COUNT_MAX_NUM - 1) / SDK_DISPATH_BATCH_COUNT_MAX_NUM;
    volatile uint32_t taskNum = parallelNum;
    sem_t sem;
    sem_init(&sem, 0, 0);
    uint32_t index = 0;
    uint32_t keyNum = 0;
    uint32_t resultIndex = 0;
    DispathBatchGetResult *taskResults =
            reinterpret_cast<DispathBatchGetResult *>(malloc(sizeof(DispathBatchGetResult) * parallelNum));
    if (taskResults == nullptr) {
        LOG_ERROR("Dispath batch get malloc mem fail.");
        return BIO_ALLOC_FAIL;
    }
    for (uint32_t i = 0; i < parallelNum; i++) {
        if (i < parallelNum - 1) {
            keyNum = SDK_DISPATH_BATCH_COUNT_MAX_NUM;
        } else {
            keyNum = count - i * SDK_DISPATH_BATCH_COUNT_MAX_NUM;
        }
        MirrorBatchGet param;
        const char ** keysParam = keys + index;
        uint32_t countParam = keyNum;
        uint64_t *offsetsParam = offsets + index;
        uint64_t *lengthsParam = lengths + index;
        ObjLocation *locationsParam = locations + index;
        uintptr_t *valuesAddrParam = valueAddrs + index;
        uint64_t *realLengthsParam = realLengths + index;
        int32_t *resultsParam = results + index;
        resultIndex = i;
        std::function<void()> func = [ &, resultIndex,
                                       keysParam, countParam, offsetsParam, lengthsParam,
                                       locationsParam, valuesAddrParam, realLengthsParam, resultsParam]() {
            taskResults[resultIndex].result = BatchGet({attr.mTenantId, attr.affinity, attr.strategy},
                                                       keysParam, countParam, offsetsParam,
                                                       lengthsParam, locationsParam, valuesAddrParam,
                                                       realLengthsParam, resultsParam);
            if (__sync_sub_and_fetch(&taskNum, 1) == 0) {
                // 最后一个任务唤醒主线程.
                sem_post(&sem);
            }
        };

        if (!mBatchGetExecutor->Execute(func)) {
            LOG_ERROR("Execute disapth batch get failed, batch num: " << param.count << " i:" << i);
            ret = BIO_INNER_ERR;
        }
        index += keyNum;
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    if (ret != BIO_OK) {
        // TODO 资源回收
        DispathBatchGetRecycleResource(parallelNum, taskResults, valueAddrs);
        return ret;
    } else {
        for (uint32_t i = 0; i < parallelNum; i++) {
            if (taskResults[i].result != BIO_OK) {
                // TODO 资源回收
                DispathBatchGetRecycleResource(parallelNum, taskResults, valueAddrs);
                return taskResults[i].result;
            }
        }
    }
    return BIO_OK;
}

BResult MirrorClient::BatchGetLocal(MirrorBatchGetLocalHbm &param)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = BatchGetLocalImpl(param);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);
    return ret;
}

BResult MirrorClient::BatchGetRemote(MirrorBatchGetRemoteHbm &param)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = BatchGetRemoteImpl(param);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);

    if (ret == BIO_OK && !mEnableTrance) {
        return BIO_DATA_IN_DRAM;
    }
    return ret;
}

void MirrorClient::BatchFree(uintptr_t *valueAddrs, const uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        mDataMsgMemPool->ReleaseOne(valueAddrs[i]);
    }
}

BResult MirrorClient::AsyncGet(MirrorGet &param, AsyncOpParam &opParam)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        QosApplyParam applyParam{startTime, param.key};
        ret = mBioQos->Apply(QOS_CONCURRENCY, QUOTA_READ, applyParam);
        if (LIKELY(ret == BIO_OK)) {
            ret = GetImpl(param, opParam);
            mBioQos->Release(QOS_CONCURRENCY, QUOTA_READ);
            if (LIKELY(ret == BIO_OK)) {
                break;
            }
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
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

BResult MirrorClient::BatchGetKeyDiskAddrImpl(MirrorBatchGetKeyAddr &param)
{
    size_t reqLen = sizeof(BatchParseKeyAddrRequest) + param.count * sizeof(BatchKeyInfo);
    BatchParseKeyAddrRequest* req = reinterpret_cast<BatchParseKeyAddrRequest*>(malloc(reqLen));
    if (UNLIKELY(req == nullptr)) {
        CLIENT_LOG_ERROR("Alloc batch get key disk addr request memory failed.");
        return BIO_ALLOC_FAIL;
    }

    req->count = param.count;
    req->magic = MESSAGE_MAGIC;
    for (uint32_t i = 0; i < param.count; i++) {
        req->infos[i].ptId = param.locations[i].location[0];
        CopyKey(req->infos[i].key, param.keys[i], KEY_MAX_SIZE);
    }

    auto ret = SendBatchGetKeyDiskAddrRequest(req, reqLen, param.infos);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send get key disk addr request failed, ret:" << ret << ".");
    }
    free(req);
    return ret;
}

BResult MirrorClient::BatchGetImpl(MirrorBatchGet &param)
{
    BResult ret = BIO_OK;
    std::vector<uint32_t> nodes(param.count);
    std::unordered_map<uint16_t, BatchGetPlan> planSend;
    for (uint32_t i = 0; i < param.count; i++) {
        uint16_t ptId =  ParseLocation(param.locations[i]);
        CmPtInfo ptEntry;
        ret = GetPtEntry(ptId, ptEntry);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Get pt entry failed ret:" << ret << ", ptId:" << ptId << ".");
            return ret;
        }
        bool isGetLocal = IsExistLocalCopy(ptEntry);
        nodes[i] = isGetLocal ? mLocalNid.VNodeId() : ptEntry.masterNodeId;
        if (planSend.find(nodes[i]) == planSend.end()) {

            planSend.emplace(std::pair<uint16_t, BatchGetPlan>(nodes[i],
                                                               {1, 0, 0, nullptr}));
        } else {
            planSend.find(nodes[i])->second.count++;
        }
    }

    auto it = planSend.begin();
    for (uint16_t i = 0; i < planSend.size(); i++) {
        size_t reqLen = sizeof(BatchGetRequest) + it->second.count * sizeof(GetKeyInfo);
        it->second.req = reinterpret_cast<BatchGetRequest*>(malloc(reqLen));
        if (UNLIKELY(it->second.req == nullptr)) {
            CLIENT_LOG_ERROR("Alloc batch get request memory failed.");
            // todo 异常回退资源；
            auto callbackIt = planSend.begin();
            for (uint16_t j = 0; j < i; j++) {
                if (callbackIt->second.req != nullptr) {
                    free(callbackIt->second.req);
                }
                callbackIt++;
            }
            return BIO_ALLOC_FAIL;
        }
        it->second.reqLen = reqLen;
        it->second.req->count = it->second.count;
        it->second.req->pid = getpid();
        it->second.req->srcNid = mLocalNid.VNodeId();
        it->second.req->isConvDeploy = (mMode == WorkerMode::CONVERGENCE);
        it++;
    }


    for (uint32_t i = 0; i < param.count; i++) {
        param.results[i] = BIO_OK;
        uintptr_t address = 0;
        BResult ret = mDataMsgMemPool->AllocOne(address);   // 从client shmem pool申请内存资源.
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Alloc rdma memory failed, ret:" << ret << ".");
            for (uint32_t j = 0; j < i; j++) {
                mDataMsgMemPool->ReleaseOne(param.valuesAddr[j]);  // rollback.
            }
            auto callbackIt = planSend.begin();
            for (uint16_t i = 0; i < planSend.size(); i++) {
                if (callbackIt->second.req != nullptr) {
                    free(callbackIt->second.req);
                }
                callbackIt++;
            }
            return BIO_ALLOC_FAIL;
        }
        param.valuesAddr[i] = address;
        auto& plan = planSend[nodes[i]];
        plan.req->keysInfo[plan.index].address = address;
        plan.req->keysInfo[plan.index].mrKey = mDataMsgMemMr.GetHcomMrs()[0]->GetLKey();;
        plan.req->keysInfo[plan.index].addressOffset = reinterpret_cast<uint8_t *>(address) - mDataMsgMemAddr;
        CopyKey(plan.req->keysInfo[plan.index].key, param.keys[i], KEY_MAX_SIZE);
        plan.req->keysInfo[plan.index].offset = param.offsets[i];
        plan.req->keysInfo[plan.index].length = param.lengths[i];
        plan.req->keysInfo[plan.index].ptId = param.locations[i].location[0];
        plan.req->keysInfo[plan.index].size = mDataMsgMemBlockSize; // 4M
        plan.req->keysInfo[plan.index].result = &(param.results[i]);
        plan.req->keysInfo[plan.index].realLength = &(param.realLengths[i]);
        plan.index++;
    }

    BIO_TRACE_START(SDK_TRACE_BATCH_GET_SEND);
    ret = SendBatchGetRequest(planSend);
    BIO_TRACE_END(SDK_TRACE_BATCH_GET_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send get request failed, ret:" << ret << ".");
    }

    // todo 清理req;
    for (auto plan : planSend) {
        if (plan.second.req != nullptr) {
            free(plan.second.req);
        }
    }
    return ret;
}

BResult MirrorClient::BatchGetRemoteImpl(MirrorBatchGetRemoteHbm &param)
{
    BResult ret = BIO_OK;
    std::vector<uint32_t> nodes(param.count);
    std::unordered_map<uint16_t, BatchGetPlanHbm> planSend;
    for (uint32_t i = 0; i < param.count; i++) {
        uint16_t ptId =  ParseLocation(param.locations[i]);
        CmPtInfo ptEntry;
        ret = GetPtEntry(ptId, ptEntry);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Get pt entry failed ret:" << ret << ", ptId:" << ptId << ".");
            return ret;
        }
        nodes[i] = ptEntry.masterNodeId;
        if (planSend.find(nodes[i]) == planSend.end()) {
            planSend.emplace(std::pair<uint16_t, BatchGetPlanHbm>(nodes[i],
                                                               {1, 0, 0, nullptr}));
        } else {
            planSend.find(nodes[i])->second.count++;
        }
    }

    auto it = planSend.begin();
    for (uint16_t i = 0; i < planSend.size(); i++) {
        size_t reqLen = 0;
        if (mEnableTrance) {
            reqLen = sizeof(BatchGetRemoteHbmRequest) + it->second.count * (sizeof(GetKeyRemoteHbmInfo) + param.col * (sizeof(uintptr_t) + sizeof(size_t)));
        } else {
            reqLen = sizeof(BatchGetRemoteHbmRequest) + it->second.count * sizeof(GetKeyRemoteHbmInfo);
        }

        it->second.req = reinterpret_cast<BatchGetRemoteHbmRequest*>(malloc(reqLen));
        if (UNLIKELY(it->second.req == nullptr)) {
            CLIENT_LOG_ERROR("Alloc batch get request memory failed.");
            // todo 异常回退资源；
            auto callbackIt = planSend.begin();
            for (uint16_t j = 0; j < i; j++) {
                if (callbackIt->second.req != nullptr) {
                    free(callbackIt->second.req);
                }
                callbackIt++;
            }
            return BIO_ALLOC_FAIL;
        }
        it->second.req->enableTrance = false;
        if (mEnableTrance) {
            it->second.req->enableTrance = true;
            it->second.enableMem = reinterpret_cast<char *>(it->second.req) +
                                                               sizeof(BatchGetRemoteHbmRequest) +
                                                               it->second.count * sizeof(GetKeyRemoteHbmInfo);
            std::string uniqueId = net::BioClientNet::Instance()->GetTransNetEngine()->GetLocalUniqueId();
            CopyKey(it->second.req->uuid, uniqueId.c_str(), MAX_UUID_SIZE);
        }
        it->second.reqLen = reqLen;
        it->second.req->count = it->second.count;
        it->second.req->pid = getpid();
        it->second.req->srcNid = mLocalNid.VNodeId();
        it->second.req->isConvDeploy = (mMode == WorkerMode::CONVERGENCE);
        it++;
    }

    for (uint32_t i = 0; i < param.count; i++) {
        param.results[i] = BIO_OK;
        uintptr_t address = 0;
        BResult ret = BIO_OK;
        auto& plan = planSend[nodes[i]];
        if (mEnableTrance) {
            plan.req->keysInfo[plan.index].hbmMemAddr = reinterpret_cast<uintptr_t*>(plan.enableMem + plan.index * (param.col * (sizeof(uintptr_t) + sizeof(size_t))));
            plan.req->keysInfo[plan.index].memSize = reinterpret_cast<size_t*>(reinterpret_cast<char*>(plan.req->keysInfo[plan.index].hbmMemAddr) + param.col * sizeof(uintptr_t));
            plan.req->keysInfo[plan.index].memCount = param.col;
            plan.req->keysInfo[plan.index].hbmMemPosition = reinterpret_cast<uintptr_t>(plan.req->keysInfo[plan.index].hbmMemAddr) - reinterpret_cast<uintptr_t>(plan.req);
            plan.req->keysInfo[plan.index].memSizePosition = reinterpret_cast<uintptr_t>(plan.req->keysInfo[plan.index].memSize) - reinterpret_cast<uintptr_t>(plan.req);
            for (uint32_t j = 0; j < param.col; j++) {
                plan.req->keysInfo[plan.index].hbmMemAddr[j] = param.memAddr[i][j];
                plan.req->keysInfo[plan.index].memSize[j] = param.memSize[i][j];
            }
        } else {
            ret = mDataMsgMemPool->AllocOne(address);   // 从client shmem pool申请内存资源.
            if (UNLIKELY(ret != BIO_OK)) {
                CLIENT_LOG_ERROR("Alloc rdma memory failed, ret:" << ret << ".");
                BatchGetHbmRecycleResouces(i, param);
                auto callbackIt = planSend.begin();
                for (uint16_t i = 0; i < planSend.size(); i++) {
                    if (callbackIt->second.req != nullptr) {
                        free(callbackIt->second.req);
                    }
                    callbackIt++;
                }
                return BIO_ALLOC_FAIL;
            }
            param.valueAddrs[i] = address;
            plan.req->keysInfo[plan.index].address = address;
            plan.req->keysInfo[plan.index].size = mDataMsgMemBlockSize; // 4M
            plan.req->keysInfo[plan.index].mrKey = mDataMsgMemMr.GetHcomMrs()[0]->GetLKey();
        }
        CopyKey(plan.req->keysInfo[plan.index].key, param.keys[i], KEY_MAX_SIZE);
        plan.req->keysInfo[plan.index].ptId = param.locations[i].location[0];
        plan.req->keysInfo[plan.index].result = &(param.results[i]);
        plan.index++;
    }

    BIO_TRACE_START(SDK_TRACE_BATCH_GET_SEND_HBM_REMOTE);
    ret = SendBatchGetRemoteHbmRequest(planSend);
    BIO_TRACE_END(SDK_TRACE_BATCH_GET_SEND_HBM_REMOTE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send get request failed, ret:" << ret << ".");
    }

    // todo 清理req;
    for (auto plan : planSend) {
        if (plan.second.req != nullptr) {
            free(plan.second.req);
        }
    }
    return ret;
}

BResult MirrorClient::BatchGetLocalImpl(MirrorBatchGetLocalHbm &param)
{
    size_t reqLen = sizeof(BatchGetLocalHbmRequest) + param.count * sizeof(GetKeyLocalHbmInfo);
    BatchGetLocalHbmRequest *req = reinterpret_cast<BatchGetLocalHbmRequest*>(malloc(reqLen));
    if (UNLIKELY(req == nullptr)) {
        CLIENT_LOG_ERROR("Alloc batch get request memory failed.");
        return BIO_ALLOC_FAIL;
    }

    BResult ret = BIO_OK;
    for (uint32_t i = 0; i < param.count; i++) {
        param.results[i] = BIO_OK;
        uintptr_t address = 0;
        ret = mDataMsgMemPool->AllocOne(address);   // 从client shmem pool申请内存资源.
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Alloc rdma memory failed, ret:" << ret << ".");
            for (uint32_t j = 0; j < i; j++) {
                mDataMsgMemPool->ReleaseOne(param.valuesAddr[i]);  // rollback.
            }
            free(req);
            return BIO_ALLOC_FAIL;
        }
        param.valuesAddr[i] = address;
        req->keysInfo[i].addressOffset = reinterpret_cast<uint8_t *>(address) - mDataMsgMemAddr;
        CopyKey(req->keysInfo[i].key, param.keys[i], KEY_MAX_SIZE);
        req->keysInfo[i].length = param.lengths[i];
        req->keysInfo[i].ptId = param.locations[i].location[0];
        req->keysInfo[i].size = mDataMsgMemBlockSize; // 4M
        req->keysInfo[i].result = &(param.results[i]);
    }
    req->count = param.count;
    req->pid = getpid();
    req->isConvDeploy = (mMode == WorkerMode::CONVERGENCE);

    BIO_TRACE_START(SDK_TRACE_BATCH_GET_SEND_HBM_LOCAL);
    ret = SendBatchGetLocalHbmRequest(req,  reqLen);
    BIO_TRACE_END(SDK_TRACE_BATCH_GET_SEND_HBM_LOCAL, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send get request failed, ret:" << ret << ".");
        for (uint32_t j = 0; j < param.count; j++) {
            mDataMsgMemPool->ReleaseOne(param.valuesAddr[j]);  // rollback.
        }
    }
    free(req);
    return ret;
}

BResult MirrorClient::GetImpl(MirrorGet &param, AsyncOpParam &opParam)
{
    uint16_t ptId = ParseLocation(param.location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << param.key << ".");
        return ret;
    }

    GetRequest req;
    req.comm = {MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid()};
    CopyKey(req.key, param.key, KEY_MAX_SIZE);
    req.ptId = ptId;
    req.offset = param.offset;
    req.length = param.length;
    req.isConvDeploy = (mMode == WorkerMode::CONVERGENCE);
    req.enableCrc = mEnableCrc;
    BIO_TRACE_START(SDK_TRACE_GET_SEND);
    ret = SendGetRequest(ptEntry, req, param.value, opParam);
    BIO_TRACE_END(SDK_TRACE_GET_SEND, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send async get request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << param.key);
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

BResult MirrorClient::Load(LoadPara &para, const ObjLocation &location, const Bio::LoadCallback &callback,
                           void *context)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = LoadImpl(para, location, callback, context);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);
    return ret;
}

BResult MirrorClient::LoadImpl(LoadPara &para, const ObjLocation &location, const Bio::LoadCallback &callback,
                               void *context)
{
    uint16_t ptId = ParseLocation(location);
    CmPtInfo ptEntry;
    BResult ret = GetPtEntry(ptId, ptEntry);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get pt entry failed, ret: " << ret << ", ptId:" << ptId << ", key:" << para.key << ".");
        return ret;
    }

    LoadRequest req;
    req.comm = { MESSAGE_MAGIC, ptId, ptEntry.version, mLocalNid.VNodeId(), getpid() };
    CopyKey(req.key, para.key, KEY_MAX_SIZE);
    req.offset = para.offset;
    req.length = para.length;
    ret = SendLoadRequest(ptEntry, req, callback, context);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send stat request failed, ret:" << ret << ", ptId:" << ptId << ", key:" << para.key << ".");
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

BResult MirrorClient::DispathBatchExist(const char *key[], ObjLocation location[], uint32_t count, bool *result)
{
    BResult ret = BIO_OK;
    uint32_t parallelNum = (count + SDK_DISPATH_BATCH_COUNT_MAX_NUM - 1) / SDK_DISPATH_BATCH_COUNT_MAX_NUM;
    volatile uint32_t taskNum = parallelNum;
    sem_t sem;
    sem_init(&sem, 0, 0);
    uint32_t index = 0;
    uint32_t keyNum = 0;
    uint32_t resultIndex = 0;
    BResult *taskResults = reinterpret_cast<BResult*>(malloc(sizeof(BResult) * parallelNum));
    if (taskResults == nullptr) {
        LOG_ERROR("Dispath batch exist malloc mem fail.");
        return BIO_ALLOC_FAIL;
    }
    for (uint32_t i = 0; i < parallelNum; i++) {
        if (i < parallelNum - 1) {
            keyNum = SDK_DISPATH_BATCH_COUNT_MAX_NUM;
        } else {
            keyNum = count - i * SDK_DISPATH_BATCH_COUNT_MAX_NUM;
        }
        const char **keys = key + index;
        ObjLocation *locations = location + index;
        uint32_t counts = keyNum;
        bool *results = result + index;
        resultIndex = i;
        std::function<void()> func = [&, resultIndex, keys, locations, counts, results]() {
            taskResults[resultIndex] = BatchExist(keys, locations, counts, results);
            if (__sync_sub_and_fetch(&taskNum, 1) == 0) {
                // 最后一个任务唤醒主线程.
                sem_post(&sem);
            }
        };

        if (!mBatchExistExecutor->Execute(func)) {
            LOG_ERROR("Execute disapth batch get failed, batch num: " << count << " i:" << i);
            ret = BIO_INNER_ERR;
        }
        index += keyNum;
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    if (ret != BIO_OK) {
        return ret;
    } else {
        for (uint32_t i = 0; i < parallelNum; i++) {
            if (taskResults[resultIndex] != BIO_OK) {
                return taskResults[resultIndex];
            }
        }
    }
    return BIO_OK;
}

BResult MirrorClient::BatchExist(const char *key[], ObjLocation location[], uint32_t count, bool *result)
{
    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        ret = BatchExistImpl(key, location, count, result);
        if (LIKELY(ret == BIO_OK)) {
            break;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);
    return ret;
}

BResult MirrorClient::SendBatchExistRequest(std::unordered_map<uint16_t, BatchExistPlan> &planSend,
                                            std::vector<BatchExistSendKeyInfo> &keysInfo)
{
    uint16_t quota = planSend.size();
    ClientCallbackCtx cbCtx;
    InitCallbackCtx(cbCtx, quota);
    auto cbFunc = [&planSend, &keysInfo](void *ctx, void *resp,
            uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else if (resp != nullptr) {
            auto rsp = static_cast<BatchExistResponse *>(resp);
            for (uint16_t i = 0; i < rsp->count; i++) {
                keysInfo[rsp->index[i]].result &= rsp->result[i];
            }
        } else {
            cbCtx->result = BIO_INVALID_PARAM;
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    bool sendLocal = false;
    for (auto plan : planSend) {
        if (plan.first == mLocalNid.VNodeId()) {
            sendLocal = true;
            continue;
        }
        BIO_TRACE_START(SDK_TRACE_BATCH_EXIST_REMOTE);
        BatchExistRemote(plan.first, plan.second.reqLen, plan.second.req, callback);
        BIO_TRACE_END(SDK_TRACE_BATCH_EXIST_REMOTE, BIO_OK);
    }

    if (sendLocal) {
        BIO_TRACE_START(SDK_TRACE_BATCH_EXIST_LOCAL);
        BatchExistLocal(planSend[mLocalNid.VNodeId()].reqLen, planSend[mLocalNid.VNodeId()].req, callback);
        BIO_TRACE_END(SDK_TRACE_BATCH_EXIST_LOCAL, BIO_OK);
    }

    sem_wait(&cbCtx.sem);
    sem_destroy(&cbCtx.sem);
    return cbCtx.result;
}

BResult MirrorClient::BatchExistImpl(const char *key[], ObjLocation location[], uint32_t count, bool *result)
{
    BResult ret = BIO_OK;
    std::unordered_map<uint16_t, BatchExistPlan> planSend;
    planSend.clear();
    std::vector<BatchExistSendKeyInfo> keysInfo;
    keysInfo.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint16_t ptId =  ParseLocation(location[i]);
        CmPtInfo ptEntry;
        ret = GetPtEntry(ptId, ptEntry);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Get pt entry failed ret:" << ret << ", ptId:" << ptId << ".");
            return ret;
        }
        keysInfo.emplace_back(ptEntry);
        for (auto copy : ptEntry.copys) {
            if (planSend.find(copy.nodeId) == planSend.end()) {
                planSend.insert(std::pair<uint16_t, BatchExistPlan>(copy.nodeId, { 1, ptEntry.version, 0, 0, nullptr}));
            } else {
                planSend.find(copy.nodeId)->second.count++;
            }
        }
    }

    auto it = planSend.begin();
    for (size_t i = 0; i < planSend.size(); i++) {
        size_t reqLen = sizeof(BatchExistRequest) + it->second.count * sizeof(BatchExistKeyInfo);
        it->second.req = reinterpret_cast<BatchExistRequest*>(malloc(reqLen));
        if (UNLIKELY(it->second.req == nullptr)) {
            CLIENT_LOG_ERROR("Alloc batch get request memory failed.");
            auto callbackIt = planSend.begin();
            for (uint16_t j = 0; j < i; j++) {
                free(callbackIt->second.req);
                callbackIt++;
            }
            return BIO_ALLOC_FAIL;
        }
        it->second.reqLen = reqLen;
        it->second.req->count = it->second.count;
        it++;
    }

    for (uint16_t i = 0; i < keysInfo.size(); i++) {
        for (auto copy : keysInfo[i].ptEntry.copys) {
            auto& plan = planSend[copy.nodeId];
            CopyKey(plan.req->keys[plan.index].key, key[i], KEY_MAX_SIZE);
            plan.req->keys[plan.index].index = i;
            plan.req->keys[plan.index].ptVec = ParseLocation(location[i]);
            plan.index++;
        }
    }

    ret = SendBatchExistRequest(planSend, keysInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send batch exist request failed, ret:" << ret <<".");
    } else {
        for (uint16_t i = 0; i < keysInfo.size(); i++) {
            result[i] = keysInfo[i].result;
        }
    }

    for (auto plan : planSend) {
        if (plan.second.req != nullptr) {
            free(plan.second.req);
        }
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

BResult MirrorClient::AddDisk(const char *diskPath)
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();
    uint64_t retryCnt = 0;
    BResult ret = BIO_OK;

    do {
        isRetry = false;
        ret = AddDiskImpl(diskPath);
        if (LIKELY(ret == BIO_OK)) {
            return BIO_OK;
        }
        if (ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY || ret == BIO_CHECK_PT_FAIL) {
            CLIENT_LOG_INFO("Add disk delay retry, times:" << ++retryCnt << ", ret:" << ret << ".");
            BIO_TP_START(SDK_MIRROR_CLIENT_SET_RETRY_TIME, &retryTime, (mTimeOut + 1));
            BIO_TP_END;
            retryTime = Monotonic::TimeSec() - startTime;
            if (retryTime < BIO_INIT_TIMEOUT_TIME) {
                isRetry = true;
                sleep(BIO_IO_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ret;
}

BResult MirrorClient::AddDiskImpl(const char *diskPath)
{
    AddDiskRequest req{};
    req.comm = { MESSAGE_MAGIC, 0, 0, mLocalNid.VNodeId(), getpid() };
    auto pathLen = strlen(diskPath);
    BResult ret = memcpy_s(req.diskPath, FILE_PATH_MAX_LEN, diskPath, pathLen);
    if (ret != BIO_OK) {
        LOG_ERROR("Req copy disk path failed, ret:" << ret << ", path:" << diskPath << ".");
        return ret;
    }
    req.diskPath[pathLen] = '\0';

    ret = SendAddDiskRequest(req);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send add disk request failed, ret:" << ret << ", diskPath:" << req.diskPath << ".");
    }
    return ret;
}

BResult MirrorClient::SendAddDiskRequest(AddDiskRequest &req)
{
    AddDiskResponse rsp;
    return agent::BioClientAgent::Instance()->AddDisk(req, rsp);
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
        Delete(ptId, param.flowId); // 申请失败删除该Flow, 不允许在该Flow上申请资源.
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
    BIO_TP_START(SDK_MIRROR_CLIENT_ADDRNUM_INVALID, &rsp->addrNum, (SLICE_ADDR_MAX_SIZE + 1));
    BIO_TP_END;
    if (rsp->addrNum > CACHE_SPACE_ADDRESS_SIZE) {
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        Delete(ptId, param.flowId); // 拷贝失败删除该Flow, 不允许在该Flow上申请资源.
        return BIO_INNER_ERR;
    }
    spaceInfo.addressNum = rsp->addrNum;
    for (uint32_t idx = 0; idx < spaceInfo.addressNum; idx++) {
        if (mMode == CONVERGENCE) {
            spaceInfo.address[idx].address = rsp->addr[idx].chunkId + rsp->addr[idx].chunkOffset;
        } else {
            uint8_t *realAddr = net::BioClientNet::Instance()->GetShmAddress(rsp->addrOffset[idx],
                rsp->addr[idx].chunkLen);
            if (realAddr == nullptr) {
                CLIENT_LOG_ERROR("Invalid response addr offset or chunk len.");
                delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
                Delete(ptId, param.flowId); // 拷贝失败删除该Flow, 不允许在该Flow上申请资源.
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
    BIO_TP_START(SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL, &isNormal, false);
    isNormal = flowInst->IsNormal();
    BIO_TP_END;
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
    auto ret = memcpy_s(req->sliceBuf, req->sliceLen, rsp->sliceBuf, rsp->sliceLen);
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("slice buffer memcpy failed.");
    }
}

void MirrorClient::ConstructPutReq(PutRequest *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId,
    uint64_t flowOffset, uint64_t flowIndex, NetMrInfo &mr, TransData &transData)
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
    req->mrOffset = 0;
    req->mrSize = mr.size;
    req->mrKey = mr.key;
    req->sliceLen = 0;
    if (transData.enableTrans) {
        std::string uniqueId = net::BioClientNet::Instance()->GetTransNetEngine()->GetLocalUniqueId();
        CopyKey(req->uuid, uniqueId.c_str(), MAX_UUID_SIZE);
    }
    req->localTransAddr = transData.localTransAddr;
    req->transDataLen = transData.transDataLen;
    req->enableTrans = transData.enableTrans;
    mBioQos->GetKey(req->quotaNid, req->quotaCid);
    if (mIoStrategy[ptEntry.ptId]->expired > Monotonic::TimeSec()) {
        req->ioStrategy = mIoStrategy[ptEntry.ptId]->strategy;
    } else {
        req->ioStrategy = 0;
    }
}

BResult MirrorClient::DataCopy(const char *from, uint32_t fromLen, SliceAddrDesc *addr, uint64_t *offset,
    uint32_t addrNum)
{
    // 检查slice内存大小是否大于等于fromLen.
    uint64_t totalLen = 0;
    for (uint32_t i = 0; i < addrNum; i++) {
        totalLen += addr[i].chunkLen;
    }
    if (totalLen < fromLen) {
        return BIO_INNER_ERR;
    }

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
    BIO_TP_START(SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY, 0);
    for (uint32_t i = 0; i < ptEntry.copys.size(); i++) {
        if (ptEntry.copys[i].nodeId == mLocalNid.VNodeId() &&
            (ptEntry.copys[i].state == CM_COPY_RUNNING || ptEntry.copys[i].state == CM_COPY_RECOVERY)) {
            return true;
        }
    }
    BIO_TP_END;
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

    BIO_TP_START(SDK_MIRROR_RSP_NUM_ERROR, &(rsp->addrNum), (SLICE_ADDR_MAX_SIZE + 1));
    BIO_TP_END;
    if (UNLIKELY(rsp->addrNum > SLICE_ADDR_MAX_SIZE)) {
        CLIENT_LOG_ERROR("rsp addrNum: " << rsp->addrNum << " is invalid.");
        delete[] static_cast<uint8_t *>(static_cast<void *>(rsp));
        return BIO_INNER_ERR;
    }

    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE_COPY_DATA);
    ret = DataCopy(param.value, param.length, rsp->addr, rsp->addrOffset, rsp->addrNum);
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
    uintptr_t address = 0;
    uintptr_t transMem = 0;
    TransData transData;
    CLIENT_LOG_ERROR("PrepareFromClient start");
    if (ptEntry.copys[0].nodeId == mLocalNid.VNodeId() || !mEnableTrance) {
        BResult ret = mDataMsgMemPool->AllocOne(address);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Alloc rdma memory failed, ret:" << ret << ", length:" << param.length << ".");
            return BIO_ALLOC_FAIL;
        }
        CLIENT_LOG_ERROR("put local start copy data to trans mem, src:" << std::hex << std::showbase << (uintptr_t)(param.value) 
                 << ", dst:" << transMem 
                 << ", len:" << std::dec << param.length);
        ret = memcpy_s(reinterpret_cast<char *>(address), mDataMsgMemBlockSize, param.value, param.length);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Copy data failed, ret:" << ret << ", key:" << param.key << ", flowId:" << param.flowId <<
                ", flowOffset:" << param.flowOffset << ", length:" << param.length << ", blockSize:" << mDataMsgMemBlockSize);
            mDataMsgMemPool->ReleaseOne(address);
            return BIO_ALLOC_FAIL;
        }
        CLIENT_LOG_ERROR("put local end copy data to trans mem, src");
        transData.enableTrans = false;
        transData.localTransAddr = 0;
        transData.transDataLen = 0;
    } else{
        auto ret = net::BioClientNet::Instance()->GetTransNetEngine()->AllocOneBlock(transMem);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Alloc memory failed, ret:" << ret << ", key:" << param.key << ".");
            return BIO_ALLOC_FAIL;
        }
        CLIENT_LOG_ERROR("put remote start copy data to trans mem, src:" << std::hex << std::showbase << (uintptr_t)(param.value) 
                 << ", dst:" << transMem 
                 << ", len:" << std::dec << param.length);
        ret = memcpy_s(reinterpret_cast<char *>(transMem), mDataMsgMemBlockSize, param.value, param.length);
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Copy data failed, ret:" << ret << ", key:" << param.key << ", flowId:" << param.flowId <<
                ", flowOffset:" << param.flowOffset << ", length:" << param.length << ", blockSize:" << mDataMsgMemBlockSize);
            net::BioClientNet::Instance()->GetTransNetEngine()->FreeOneBlock(transMem);
            return BIO_ALLOC_FAIL;
        }
        CLIENT_LOG_ERROR("put local end copy data to trans mem, src");
        transData.enableTrans = true;
        transData.localTransAddr = transMem;
        transData.transDataLen = param.length;
    }

    uint8_t* tmp = nullptr;
    BIO_TP_START(SDK_MIRROR_CLIENT_PREPARE_FAIL, 0);
    tmp = new (std::nothrow) uint8_t[sizeof(PutRequest)];
    BIO_TP_END;
    if (tmp == nullptr) {
        CLIENT_LOG_ERROR("Alloc memory failed.");
        if (address) {
            mDataMsgMemPool->ReleaseOne(address);
        }
        if (transMem) {
            net::BioClientNet::Instance()->GetTransNetEngine()->FreeOneBlock(transMem);
        }
        return BIO_ALLOC_FAIL;
    }
    NetMrInfo mr(address, mDataMsgMemBlockSize, mDataMsgMemMr.GetHcomMrs()[0]->GetLKey());
    req = static_cast<PutRequest *>(static_cast<void *>(tmp));
    ConstructPutReq(req, ptEntry, param, param.flowId, param.flowOffset, param.flowIndex, mr, transData);
    req->memFromServer = false;
    req->mrOffset = reinterpret_cast<uint8_t *>(address) - mDataMsgMemAddr;
    return BIO_OK;
}

BResult MirrorClient::Prepare(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req)
{
    if (IsExistLocalCopy(ptEntry) && param.attr.affinity != GLOBAL_BALANCE) {
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

void MirrorClient::InitAsyncPutCbCtx(AsyncPutCbCtx &cbCtx, uint32_t quota)
{
    cbCtx.result = BIO_OK;
    cbCtx.quota = quota;
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
        BIO_TP_START(SDK_MIRROR_PUT_RECV_FAIL, &(cbCtx->result), BIO_INNER_RETRY);
        BIO_TP_END;
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

    if (req->mrAddress) {
        mDataMsgMemPool->ReleaseOne(req->mrAddress);
    }
    if (req->enableTrans || req->localTransAddr != 0) {
        net::BioClientNet::Instance()->GetTransNetEngine()->FreeOneBlock(req->localTransAddr);
    }
    if (cbCtx.result == BIO_OK) {
        mIoStrategy[ptEntry.ptId]->expired = Monotonic::TimeSec() + IO_EXTRATEGE_TIME;
        mIoStrategy[ptEntry.ptId]->strategy = ioStrategy.load();
    }
    return cbCtx.result;
}

BResult MirrorClient::SendAsyncPutRequestImpl(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *req,
    BioAsyncPutCallback asyncPutCallback, void* context)
{
    uint32_t quota = CalcPtQuota(ptEntry);
    AsyncPutCbCtx *cbCtx = new (std::nothrow) AsyncPutCbCtx;
    if (UNLIKELY(cbCtx == nullptr)) {
        CLIENT_LOG_ERROR("Alloc callback failed.");
        net::BioClientNet::Instance()->Free(req->mrAddress);
        delete[] static_cast<uint8_t *>(static_cast<void *>(req));
        return BIO_ALLOC_FAIL;
    }
    InitAsyncPutCbCtx(*cbCtx, quota);
    auto *ioStrategy = new (std::nothrow) std::atomic<uint32_t>(0);
    if (ioStrategy == nullptr) {
        CLIENT_LOG_ERROR("Alloc strategy failed.");
        return BIO_ALLOC_FAIL;
    }

    auto cbFunc = [this, ptEntry, asyncPutCallback, context, ioStrategy, req](void *ctx,
        void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (AsyncPutCbCtx *)ctx;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else if (resp != nullptr) {
            auto rsp = static_cast<PutResponse *>(resp);
            if (rsp->ioStrategy > WRITE_UNDERFS_BACK) {
                cbCtx->result = BIO_INVALID_PARAM;
            } else {
                *ioStrategy = (rsp->ioStrategy > (*ioStrategy).load()) ? rsp->ioStrategy : (*ioStrategy).load();
            }
        } else {
            cbCtx->result = BIO_INVALID_PARAM;
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            asyncPutCallback(context, cbCtx->result);
            mDataMsgMemPool->ReleaseOne(req->mrAddress);
            if (cbCtx->result == BIO_OK) {
                mIoStrategy[ptEntry.ptId]->expired = Monotonic::TimeSec() + IO_EXTRATEGE_TIME;
                mIoStrategy[ptEntry.ptId]->strategy.store((*ioStrategy).load());
            }
            delete[] static_cast<uint8_t *>(static_cast<void *>(req));
            delete ioStrategy;
            delete cbCtx;
        }
    };
    Callback callback(cbFunc, static_cast<void *>(cbCtx));

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

    return BIO_OK;
}

BResult MirrorClient::SendPutRequest(CmPtInfo &ptEntry, MirrorPut &param)
{
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE);
    PutRequest *req = nullptr;
    BResult ret = BIO_OK;
    ret = Prepare(ptEntry, param, req);
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

BResult MirrorClient::SendAsyncPutRequest(CmPtInfo &ptEntry, MirrorPut &param, BioAsyncPutCallback callback,
    void* context)
{
    BIO_TRACE_START(SDK_TRACE_PUT_PREPARE);
    PutRequest *req = nullptr;
    BResult ret = Prepare(ptEntry, param, req);
    BIO_TRACE_END(SDK_TRACE_PUT_PREPARE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Prepare async put resource failed, ret:" << ret << ", key:" << param.key << ", length:" <<
            param.length << ", flowId:" << param.flowId << ", flowOffset:" << param.flowOffset << ".");
        return ret;
    }
    req->dataCrc = mEnableCrc ? BioCrcUtil::Crc32(param.value, param.length) : 0;

    BIO_TRACE_START(SDK_TRACE_PUT_SEND);
    ret = SendAsyncPutRequestImpl(ptEntry, param, req, callback, context);
    BIO_TRACE_END(SDK_TRACE_PUT_SEND, ret);
    return ret;
}

BResult MirrorClient::GetServerRemote(GetRequest &req, uint16_t dstNid, char *value, uint64_t &realLen)
{
    uintptr_t address = 0;
    BResult ret = mDataMsgMemPool->AllocOne(address);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc data message mem failed, ret:" << ret << ", length:" << req.length << ", page size:" <<
            net::BioClientNet::Instance()->GetDataPage() << ".");
        return BIO_ALLOC_FAIL;
    }
    req.isMr = 1;
    req.address = address;
    req.size = mDataMsgMemBlockSize;
    req.mrKey = mDataMsgMemMr.GetHcomMrs()[0]->GetLKey();

    GetResponse rsp;
    ret = net::BioClientNet::Instance()->SendSync<GetRequest, GetResponse>(static_cast<BioNodeId>(dstNid),
        BIO_OP_SDK_GET, req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send sync get request failed, ret:" << ret << ", key:" << req.key << ", offset:" <<
            req.offset << ", length:" << req.length << ", dstNid:" << dstNid << ".");
    } else {
        if (rsp.num > SLICE_ADDR_SIZE) {
            return BIO_INVALID_PARAM;
        }
        realLen = rsp.realLen;
        if (realLen > req.length) {
            CLIENT_LOG_ERROR("Read length greater than value size, realLen:" << realLen <<
                ", size:" << mDataMsgMemBlockSize);
            return BIO_INNER_ERR;
        }

        BIO_TRACE_START(SDK_TRACE_GET_COPY2U);
        ret = memcpy_s(value, req.length, reinterpret_cast<void *>(address), realLen);
        BIO_TRACE_END(SDK_TRACE_GET_COPY2U, ret);
        if (UNLIKELY(ret != 0)) {
            CLIENT_LOG_ERROR("Copy data to user failed, ret:" << ret << ".");
        } else if (req.enableCrc) {
            uint32_t currentCrc = BioCrcUtil::Crc32(value, realLen);
            if (currentCrc != rsp.dataCrc) {
                CLIENT_LOG_ERROR("Client Get failed to verify the CRC, key:" << req.key << ", origin crc:" <<
                    rsp.dataCrc << ", current crc:" << currentCrc);
                ret = BIO_CRC_ERR;
            }
        }
    }
    mDataMsgMemPool->ReleaseOne(address);
    return ret;
}

BResult MirrorClient::GetServerRemote(GetRequest &req, uint16_t masterNid, char *value, Callback callback)
{
    net::BioClientNet::Instance()->SendAsyncBuff(static_cast<BioNodeId>(masterNid), BIO_OP_SDK_GET,
                                                 static_cast<void *>(&req), sizeof(GetRequest), callback);
    return BIO_OK;
}

BResult MirrorClient::GetFromServer(GetRequest &req, uint16_t serverNid, char *value, uint64_t &realLen)
{
    CLIENT_LOG_DEBUG("Get master start, serverNid:" << serverNid << ", localNid:" << mLocalNid.VNodeId() << ", key:" <<
        req.key << ", offset:" << req.offset << ", length:" << req.length << ".");
    BResult ret = BIO_INNER_ERR;
    BIO_TP_START(SDK_MIRROR_GET_RECV_FAIL, &ret, BIO_INNER_RETRY);
    if (serverNid == mLocalNid.VNodeId()) {
        BIO_TRACE_START(SDK_TRACE_GET_LOCAL);
        ret = agent::BioClientAgent::Instance()->GetLocal(req, value, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_LOCAL, ret);
    } else {
        BIO_TRACE_START(SDK_TRACE_GET_REMOTE);
        ret = GetServerRemote(req, serverNid, value, realLen);
        BIO_TRACE_END(SDK_TRACE_GET_REMOTE, ret);
    }
    BIO_TP_END;
    return ret;
}

BResult MirrorClient::SendGetRequest(CmPtInfo &ptEntry, GetRequest &req, char *value, uint64_t &realLen)
{
    bool isGetLocal = IsExistLocalCopy(ptEntry);
    uint16_t serverNid = isGetLocal ? mLocalNid.VNodeId() : ptEntry.masterNodeId;
    return GetFromServer(req, serverNid, value, realLen);
}

BResult MirrorClient::SendBatchGetKeyDiskAddrRequest(BatchParseKeyAddrRequest *req, uint32_t reqLen,
                                                     KeyAddrInfo* infos)
{
    return agent::BioClientAgent::Instance()->BatchGetKeyDiskAddrLocal(req, reqLen, infos);
}

void MirrorClient::SendBatchGetRemote(uint16_t nodeId, uint32_t reqLen, BatchGetRequest *req, Callback &callback)
{
    BIO_TRACE_START(SDK_TRACE_BATCH_GET_REMOTE_SYNC);
    net::BioClientNet::Instance()->SendAsyncBuff(static_cast<BioNodeId>(nodeId), BIO_OP_SDK_BATCH_GET,
                                                 static_cast<void *>(req), reqLen, callback);
    BIO_TRACE_END(SDK_TRACE_BATCH_GET_REMOTE_SYNC, BIO_OK);
}

BResult MirrorClient::SendBatchGetRequest(std::unordered_map<uint16_t, BatchGetPlan> &planSend)
{
    uint16_t quota = planSend.size();
    ClientCallbackCtx cbCtx;
    InitCallbackCtx(cbCtx, quota);
    auto cbFunc = [&planSend](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else if (resp != nullptr) {
            auto rsp = static_cast<BatchGetResponse *>(resp);
            for (uint32_t i = 0; i < rsp->count; i++) {
                *(planSend[rsp->nodeId].req->keysInfo[i].result) = rsp->results[i];
                *(planSend[rsp->nodeId].req->keysInfo[i].realLength) = rsp->realLengths[i];
            }
        } else {
            cbCtx->result = BIO_INVALID_PARAM;
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    auto it = planSend.begin();
    bool sendLocal = false;
    for (auto plan : planSend) {
        if (plan.first == mLocalNid.VNodeId()) {
            sendLocal = true;
            continue;
        }
        BIO_TRACE_START(SDK_TRACE_BATCH_GET_REMOTE);
        SendBatchGetRemote(plan.first, plan.second.reqLen, plan.second.req, callback);
        BIO_TRACE_END(SDK_TRACE_BATCH_GET_REMOTE, BIO_OK);
    }

    if (sendLocal) {
        BIO_TRACE_START(SDK_TRACE_BATCH_GET_LOCAL);
        agent::BioClientAgent::Instance()->BatchGetLocal(planSend[mLocalNid.VNodeId()].req,
                                                         planSend[mLocalNid.VNodeId()].reqLen, callback);
        BIO_TRACE_END(SDK_TRACE_BATCH_GET_LOCAL, BIO_OK);
    }

    sem_wait(&cbCtx.sem);
    sem_destroy(&cbCtx.sem);
    return cbCtx.result;
}

BResult MirrorClient::SendBatchGetRemoteHbmRequest(std::unordered_map<uint16_t, BatchGetPlanHbm> &planSend)
{
    uint16_t quota = planSend.size();
    ClientCallbackCtx cbCtx;
    InitCallbackCtx(cbCtx, quota);
    auto cbFunc = [&planSend](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (ClientCallbackCtx *)ctx;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else if (resp != nullptr) {
            auto rsp = static_cast<BatchGetRemoteHbmResponse *>(resp);
            for (uint32_t i = 0; i < rsp->count; i++) {
                *(planSend[rsp->nodeId].req->keysInfo[i].result) = rsp->results[i];
            }
        } else {
            cbCtx->result = BIO_INVALID_PARAM;
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    auto it = planSend.begin();
    for (auto plan : planSend) {
        net::BioClientNet::Instance()->SendAsyncBuff(static_cast<BioNodeId>(plan.first), BIO_OP_SDK_BATCH_GET_REMTOE_HBM,
                                                     static_cast<void *>(plan.second.req), plan.second.reqLen, callback);
    }


    sem_wait(&cbCtx.sem);
    sem_destroy(&cbCtx.sem);
    return cbCtx.result;
}

BResult MirrorClient::SendBatchGetLocalHbmRequest(BatchGetLocalHbmRequest *req, uint32_t reqLen)
{
    return agent::BioClientAgent::Instance()->BatchGetLocalHbm(req, reqLen);
}

BResult MirrorClient::GetShmDataCallBack(GetResponse *rsp, uint64_t &realLen, const GetRequest &req, char *value)
{
    BResult ret = BIO_OK;
    if (UNLIKELY(rsp->num > SLICE_ADDR_SIZE)) {
        CLIENT_LOG_ERROR("Param check failed, num:" << rsp->num);
        return BIO_INVALID_PARAM;
    }
    realLen = rsp->realLen;
    if (UNLIKELY(realLen > req.length)) {
        CLIENT_LOG_ERROR("Len check failed, reaLen:" << realLen << ",reqLen:" << req.length);
        return BIO_INNER_ERR;
    }
    uint64_t off = 0;
    uint64_t cpyLength = req.length;
    for (uint32_t idx = 0; idx < rsp->num; idx++) {
        uint8_t *addr = net::BioClientNet::Instance()->GetShmAddress(rsp->addrOffset[idx], rsp->addrLen[idx]);
        if (UNLIKELY(addr == nullptr)) {
            CLIENT_LOG_ERROR("Send sync request get shm addr failed.");
            break;
        }

        ret = memcpy_s(static_cast<void *>(value + off), cpyLength, reinterpret_cast<void *>(addr), rsp->addrLen[idx]);
        if (UNLIKELY(ret != 0)) {
            CLIENT_LOG_ERROR("Memory copy data to user failed, ret:" << ret << ", idx:" << idx << ", len:" <<
                rsp->addrLen[idx] << ".");
            break;
        }
        off += rsp->addrLen[idx];
        cpyLength -= rsp->addrLen[idx];
    }

    if (rsp->isAlloc) {
        FreeMemRequest freeReq = {req.comm, rsp->num, 0, {0}};
        for (uint32_t idx = 0; idx < rsp->num; idx++) {
            freeReq.addr[idx] = rsp->addrOffset[idx];
        }
        auto freeRet =
                net::BioClientNet::Instance()->SendAsync<FreeMemRequest>(INVALID_NID, BIO_OP_SDK_FREE_MEM, freeReq);
        if (freeRet != BIO_OK) {
            CLIENT_LOG_ERROR("Send async free request failed, ret:" << ret << ".");
        }
    }
    if (req.enableCrc && ret == BIO_OK) {
        uint32_t currentCrc = BioCrcUtil::Crc32(value, rsp->realLen);
        if (UNLIKELY(rsp->dataCrc != currentCrc)) {
            CLIENT_LOG_ERROR("Client get failed to verify the CRC, << key:" << req.key << ", origin crc:" <<
                rsp->dataCrc << ", current crc:"<< currentCrc);
            ret = BIO_CRC_ERR;
        }
    }
    return ret;
}

BResult MirrorClient::GetRpcDataCallBack(GetResponse *rsp, const GetRequest &req, char *value, uint64_t &realLen)
{
    BResult ret = BIO_OK;
    if (rsp->num > SLICE_ADDR_SIZE) {
        return BIO_INVALID_PARAM;
    }
    realLen = rsp->realLen;
    if (realLen > req.size) {
        return BIO_INNER_ERR;
    }

    BIO_TRACE_START(SDK_TRACE_GET_COPY2U);
    ret = memcpy_s(value, req.length, reinterpret_cast<void *>(req.address), realLen);
    BIO_TRACE_END(SDK_TRACE_GET_COPY2U, ret);
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("Copy data to user failed, ret:" << ret << ".");
    } else if (req.enableCrc) {
        uint32_t currentCrc = BioCrcUtil::Crc32(value, realLen);
        if (currentCrc != rsp->dataCrc) {
            CLIENT_LOG_ERROR("Client Get failed to verify the CRC, key:" << req.key << ", origin crc:" <<
                rsp->dataCrc << ", current crc:" << currentCrc);
            ret = BIO_CRC_ERR;
        }
    }
    net::BioClientNet::Instance()->Free(req.address);
    return ret;
}

BResult MirrorClient::GetFromServer(GetRequest &req, uint16_t serverNid, char *value, AsyncOpParam &opParam)
{
    CLIENT_LOG_DEBUG("Async get master start, serverNid:" << serverNid << ", localNid:" << mLocalNid.VNodeId()
        << ", key:" <<req.key << ", offset:" << req.offset << ", length:" << req.length << ".");
    ClientCallbackCtx cbCtx;
    cbCtx.result = BIO_OK;
    cbCtx.resp = nullptr;
    cbCtx.respLen = 0;
    if (serverNid != mLocalNid.VNodeId()) {
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
    }

    auto cbFunc = [this, serverNid, opParam, value, req](void *ctx, void *resp, uint32_t len, int32_t result) {
        uint64_t realLen = 0;
        if (UNLIKELY(result == BIO_OK && resp != nullptr)) {
            auto rsp = static_cast<GetResponse *>(resp);
            realLen = rsp->realLen;
            if (this->mLocalNid.VNodeId() == serverNid) {
                if (this->mMode != CONVERGENCE) {
                    result = GetShmDataCallBack(rsp, realLen, req, value);
                }
            } else {
                result = GetRpcDataCallBack(rsp, req, value, realLen);
            }
        }
        opParam.func(opParam.context, result, realLen);
    };

    BResult ret = BIO_INNER_ERR;
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));
    BIO_TP_START(SDK_MIRROR_GET_RECV_FAIL, &ret, BIO_INNER_RETRY);
    if (serverNid == mLocalNid.VNodeId()) {
        BIO_TRACE_START(SDK_TRACE_GET_LOCAL);
        ret = agent::BioClientAgent::Instance()->GetLocal(req, value, callback);
        BIO_TRACE_END(SDK_TRACE_GET_LOCAL, ret);
    } else {
        BIO_TRACE_START(SDK_TRACE_GET_REMOTE);
        ret = GetServerRemote(req, serverNid, value, callback);
        BIO_TRACE_END(SDK_TRACE_GET_REMOTE, ret);
    }
    BIO_TP_END;
    return ret;
}

BResult MirrorClient::SendGetRequest(CmPtInfo &ptEntry, GetRequest &req, char *value, AsyncOpParam &opParam)
{
    bool isGetLocal = IsExistLocalCopy(ptEntry);
    uint16_t serverNid = isGetLocal ? mLocalNid.VNodeId() : ptEntry.masterNodeId;
    return GetFromServer(req, serverNid, value, opParam);
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
        auto cbCtx = (ClientCallbackCtx *)ctx;
        BIO_TP_START(SDK_MIRROR_DELETE_RECV_FAIL, &result, BIO_INNER_RETRY);
        BIO_TP_END;
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        } else {
            BResult res = (resp != nullptr) ? *(static_cast<BResult *>(resp)) : BIO_INNER_ERR;
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
    BIO_TP_START(SDK_MIRROR_STAT_RECV_FAIL, &ret, BIO_INNER_RETRY);
    if (dstNid == mLocalNid.VNodeId()) {
        ret = StatLocal(req, objInfo);
    } else {
        ret = StatRemote(dstNid, req, objInfo);
    }

    BIO_TP_END;
    return ret;
}

inline void MirrorClient::BatchExistRemote(uint16_t nodeId, uint32_t reqLen, BatchExistRequest *req, Callback &callback)
{
    net::BioClientNet::Instance()->SendAsyncBuff(static_cast<BioNodeId>(nodeId), BIO_OP_SDK_BATCH_EXIST,
                                                 static_cast<void *>(req), reqLen, callback);
}

inline void MirrorClient::BatchExistLocal(uint32_t reqLen, BatchExistRequest *req, Callback &callback)
{
    agent::BioClientAgent::Instance()->BatchExistLocal(reqLen, req, callback);
}

BResult MirrorClient::SendNotifyUpdateRequest(bool &flag)
{
    BResult ret = BIO_INNER_ERR;
    BIO_TP_START(SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL, &ret, BIO_INNER_RETRY);
    ret = agent::BioClientAgent::Instance()->NotifyUpdate(flag);
    BIO_TP_END;
    return ret;
}

BResult MirrorClient::SendCheckUpdateReadyRequest()
{
    BResult ret = BIO_INNER_ERR;
    BIO_TP_START(SDK_MIRROR_CHECK_UPDATE_RECV_FAIL, &ret, BIO_INNER_RETRY);
    ret = agent::BioClientAgent::Instance()->CheckUpdateReady();
    BIO_TP_END;
    return ret;
}

BResult MirrorClient::ListRemote(uint16_t nid, ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    uint64_t maxSize = sizeof(ObjStat) * 1000U;
    uintptr_t address = 0;
    BResult ret = mDataMsgMemPool->AllocOne(address);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Alloc rdma memory failed.");
        return BIO_ALLOC_FAIL;
    }

    req.address = address;
    req.size = maxSize;
    req.mrKey = mDataMsgMemMr.GetHcomMrs()[0]->GetLKey();
    ListResponse rsp;
    BIO_TP_START(LISTALL_REMOTE_RSP_OVER_LIMIT, &rsp.num, 1500U);
    ret = net::BioClientNet::Instance()->SendSync<ListRequest, ListResponse>(static_cast<BioNodeId>(nid),
        BIO_OP_SDK_LIST, req, rsp);
    BIO_TP_END;
    if (ret != BIO_OK) {
        mDataMsgMemPool->ReleaseOne(address);
        return ret;
    }
    if (UNLIKELY(rsp.num > 1000U || rsp.buffLen != 0)) {
        return BIO_INNER_RETRY;
    }

    if (rsp.num != 0) {
        size_t objSize = 0;
        auto statInfo = reinterpret_cast<ObjStat *>(address);
        for (uint32_t i = 0; i < rsp.num; i++) {
            objSize = objs.size();
            BIO_TP_START(LISTALL_REMOTE_OVER_1000, &objSize, 1500U);
            BIO_TP_END;
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
    mDataMsgMemPool->ReleaseOne(address);
    return BIO_OK;
}

BResult MirrorClient::ListLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    return agent::BioClientAgent::Instance()->ListLocal(req, objs);
}

BResult MirrorClient::SendListRequest(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs)
{
    BResult ret = BIO_ERR;
    uint32_t index = 0;

    auto tempPtView = mPtView;
    for (auto &ptEntry : tempPtView) {
        uint16_t dstNid = ptEntry.second.masterNodeId;
        req.isListUnderFs = (index == tempPtView.size() - 1);
        req.comm.ptId = ptEntry.second.ptId;
        req.comm.ptv = ptEntry.second.version;
        BIO_TP_START(SDK_MIRROR_LIST_RECV_FAIL, &ret, BIO_INNER_RETRY);
        if (dstNid == mLocalNid.VNodeId()) {
            ret = ListLocal(req, objs);
        } else {
            ret = ListRemote(dstNid, req, objs);
        }
        BIO_TP_END;
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
        BIO_TP_START(SDK_MIRROR_LOAD_RECV_FAIL, &ret, BIO_INNER_RETRY);
        ret = agent::BioClientAgent::Instance()->LoadLocal(req);
        BIO_TP_END;
        callback(context, ret);
        return BIO_OK;
    }

    auto cbFunc = [&callback, context](void *ctx, void *resp, uint32_t len, int32_t result) {
        BIO_TP_START(SDK_MIRROR_LOAD_RECV_FAIL, &result, BIO_INNER_RETRY);
        BIO_TP_END;
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

BResult MirrorClient::QueryCacheResourceImpl(std::vector<CacheResourcesDesc> &nodeDesc)
{
    CacheResourceRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, mLocalNid.VNodeId(), getpid() };

    BResult ret = BIO_ERR;
    BIO_TP_START(SDK_MIRROR_CLIENT_QUERY_CACHE_RESOURCE_SEND_FAIL, &ret, BIO_INNER_ERR);
    ret = SendCacheResourceRequest(req, nodeDesc);
    BIO_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send query cache resource request failed, ret:" << ret);
    }
    return ret;
}

BResult MirrorClient::SendCacheResourceRequest(CacheResourceRequest &req, std::vector<CacheResourcesDesc> &nodeDesc)
{
    uint16_t localId = UINT16_MAX;
    std::vector<uint16_t> remoteId;
    mUpdateView(); // 更新视图
    auto nodeView = GetNodeView();
    for (const auto& node: nodeView) {
        if (node.second.status == CM_NODE_FAULT) {
            continue;
        }
        if (node.first.nodeId == mLocalNid.VNodeId()) {
            localId = mLocalNid.VNodeId();
            continue;
        }
        remoteId.emplace_back(node.first.nodeId);
    }

    CalcCacheResourceLocal(req, localId, nodeDesc);
    CalcCacheResourceRemote(req, remoteId, nodeDesc);
    return BIO_OK;
}

BResult MirrorClient::GetCacheHitRatioImpl(std::unordered_map<uint16_t, CacheHitDesc> &nodeDesc)
{
    CacheHitRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, mLocalNid.VNodeId(), getpid() };

    BResult ret = BIO_ERR;
    BIO_TP_START(SDK_MIRROR_CLIENT_QUERY_CACHE_HIT_SEND_FAIL, &ret, BIO_INNER_ERR);
    ret = SendCacheHitRequest(req, nodeDesc);
    BIO_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send get cache hit request failed, ret:" << ret);
    }

    return ret;
}

BResult MirrorClient::SendCacheHitRequest(CacheHitRequest &req, std::unordered_map<uint16_t, CacheHitDesc> &nodeDesc)
{
    uint16_t localId = UINT16_MAX;
    std::vector<uint16_t> remoteId;

    mUpdateView(); // 更新视图
    auto nodeView = GetNodeView();
    for (const auto& node: nodeView) {
        if (node.second.status == CM_NODE_FAULT) {
            continue;
        }
        if (node.first.nodeId == mLocalNid.VNodeId()) {
            localId = mLocalNid.VNodeId();
            continue;
        }
        remoteId.emplace_back(node.first.nodeId);
    }

    GetCacheHitLocal(req, localId, nodeDesc);
    GetCacheHitRemote(req, remoteId, nodeDesc);
    return BIO_OK;
}

void MirrorClient::GetCacheHitLocal(CacheHitRequest &req, uint16_t localId,
                                    std::unordered_map<uint16_t, CacheHitDesc> &nodeDesc)
{
    if (UNLIKELY(localId == UINT16_MAX)) {
        return;
    }
    BResult ret = BIO_OK;
    ret = agent::BioClientAgent::Instance()->GetCacheHitLocal(req, nodeDesc);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send get cache hit request failed, ret: " << ret << ". nodeId: " << localId);
    }
}

void MirrorClient::GetCacheHitRemote(CacheHitRequest &req, std::vector<uint16_t> &remoteId,
                                     std::unordered_map<uint16_t, CacheHitDesc> &nodeDesc)
{
    for (auto id : remoteId) {
        CacheHitResponse rsp;
        BResult ret = net::BioClientNet::Instance()->SendSync<CacheHitRequest, CacheHitResponse>(
            static_cast<BioNodeId>(id), BIO_OP_SDK_GET_CACHE_HIT, req, rsp);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Send get cache hit request failed, ret: " << ret << ". nodeId: " << id);
            continue;
        }
        nodeDesc[rsp.nodeId].rCacheHitMemCount.store(rsp.rCacheHitMemCount);
        nodeDesc[rsp.nodeId].rCacheHitDiskCount.store(rsp.rCacheHitDiskCount);
        nodeDesc[rsp.nodeId].rCacheHitCount.store(rsp.rCacheHitCount);
        nodeDesc[rsp.nodeId].rCacheTotalCount.store(rsp.rCacheTotalCount);
        nodeDesc[rsp.nodeId].wCacheHitMemCount.store(rsp.wCacheHitMemCount);
        nodeDesc[rsp.nodeId].wCacheHitDiskCount.store(rsp.wCacheHitDiskCount);
        nodeDesc[rsp.nodeId].wCacheHitCount.store(rsp.wCacheHitCount);
        nodeDesc[rsp.nodeId].wCacheTotalCount.store(rsp.wCacheTotalCount);
        nodeDesc[rsp.nodeId].backendHitCount.store(rsp.backendHitCount);
    }
}

void MirrorClient::CalcCacheResourceLocal(CacheResourceRequest &req, uint16_t localId,
                                          std::vector<CacheResourcesDesc> &nodeDesc)
{
    if (UNLIKELY(localId == UINT16_MAX)) {
        return;
    }
    BResult ret = agent::BioClientAgent::Instance()->CalcCacheResourceLocal(req, nodeDesc);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send calc cache resource request failed, ret: " << ret << ". nodeId: " << localId);
    }
}

void MirrorClient::CalcCacheResourceRemote(CacheResourceRequest &req, std::vector<uint16_t> &remoteId,
                                           std::vector<CacheResourcesDesc> &nodeDesc)
{
    for (auto id : remoteId) {
        CacheResourceResponse rsp;
        BResult ret = net::BioClientNet::Instance()->SendSync<CacheResourceRequest, CacheResourceResponse>(
            static_cast<BioNodeId>(id), BIO_OP_SDK_QUERY_CACHE_RESOURCE, req, rsp);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Send calc cache resource request failed, ret: " << ret << ". nodeId: " << id);
            continue;
        }
        CacheResourcesDesc tempDesc;
        tempDesc.nodeId = rsp.nodeId;
        tempDesc.rCacheMemCapacity = rsp.rCacheMemCapacity;
        tempDesc.rCacheDiskCapacity = rsp.rCacheDiskCapacity;
        tempDesc.wCacheMemCapacity = rsp.wCacheMemCapacity;
        tempDesc.wCacheDiskCapacity = rsp.wCacheDiskCapacity;
        tempDesc.rCacheDiskUsedSize = rsp.rCacheDiskUsedSize;
        tempDesc.rCacheMemUsedSize = rsp.rCacheMemUsedSize;
        tempDesc.wCacheMemUsedSize = rsp.wCacheMemUsedSize;
        tempDesc.wCacheDiskUsedSize = rsp.wCacheDiskUsedSize;
        nodeDesc.push_back(tempDesc);
    }
}