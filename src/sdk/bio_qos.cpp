/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include "message_op.h"
#include "bio_client_agent.h"
#include "bio_client.h"
#include "bio_qos.h"

using namespace ock::bio;

void BioQuota::WakeForce(uint16_t nodeSet, bool isLock)
{
    if (!isLock) {
        mLock.LockWrite();
    }
    mTaskRunFlag.find(nodeSet)->second = false;

    auto iter = mIoQueueMap.find(nodeSet);
    if (UNLIKELY(iter == mIoQueueMap.end())) {
        if (!isLock) {
            mLock.UnLock();
        }
        return;
    }
    uint64_t currentTime = Monotonic::TimeSec();
    do {
        auto entry = iter->second.Top();
        if (LIKELY(entry != nullptr)) {
            iter->second.Pop();
            if (UNLIKELY((currentTime - entry->time) >= NO_45)) {
                CLIENT_LOG_WARN("IO hang time is too long, nodeSet:" << nodeSet << ", key:" << entry->key << ".");
                BIO_TRACE_START(SDK_TRACE_QOS_WAKE_BUSY);
                entry->Wake(BIO_QUOTA_NOT_ENOUGH);
                BIO_TRACE_END(SDK_TRACE_QOS_WAKE_BUSY, BIO_OK);
            } else {
                CLIENT_LOG_DEBUG("IO wake, put retry, nodeSet:" << nodeSet << ", key:" << entry->key << ".");
                BIO_TRACE_START(SDK_TRACE_QOS_WAKE_RETRY);
                entry->Wake(BIO_INNER_RETRY);
                BIO_TRACE_END(SDK_TRACE_QOS_WAKE_RETRY, BIO_OK);
            }
        } else {
            break;
        }
    } while (true);
    if (!isLock) {
        mLock.UnLock();
    }
}

void BioQuota::UpdateQuotaRes(CmPtInfo *ptEntry, uint16_t nodeSet, uint64_t allocQuota)
{
    // 1. 更新写配额资源
    WriteLocker<ReadWriteLock> lock(&mLock);
    auto iter = mQuotaMgr.find(nodeSet);
    if (UNLIKELY(iter == mQuotaMgr.end())) {
        mQuotaMgr.emplace(nodeSet, 0);
        iter = mQuotaMgr.find(nodeSet);
    }
    uint64_t oriQuota = iter->second;
    iter->second += allocQuota;
    CLIENT_LOG_DEBUG("Update quota resource, nodeSet:" << nodeSet << ", alloc quota size:" << allocQuota <<
        ", held quota form " << oriQuota << " to " << iter->second << ".");

    // 2. 重置状态
    mTaskRunFlag.find(nodeSet)->second = false;

    // 3. 唤醒等待队列中的IO
    auto iter2 = mIoQueueMap.find(nodeSet);
    if (UNLIKELY(iter2 == mIoQueueMap.end())) {
        return;
    }
    uint64_t currentTime = Monotonic::TimeSec();
    do {
        auto entry = iter2->second.Top();
        if (LIKELY(entry != nullptr)) {
            if (UNLIKELY((currentTime - entry->time) >= NO_45)) {
                CLIENT_LOG_WARN("IO hang time is too long, nodeSet:" << nodeSet << ", key:" << entry->key << ".");
                BIO_TRACE_START(SDK_TRACE_QOS_WAKE_BUSY);
                iter2->second.Pop();
                entry->Wake(BIO_QUOTA_NOT_ENOUGH);
                BIO_TRACE_END(SDK_TRACE_QOS_WAKE_BUSY, BIO_OK);
            } else if (LIKELY(iter->second >= entry->size)) {
                iter->second -= entry->size;
                CLIENT_LOG_DEBUG("Put go on, nodeSet:" << nodeSet << ", key:" << entry->key << ", size:" <<
                    entry->size << ", remain quota:" << iter->second << ".");
                BIO_TRACE_START(SDK_TRACE_QOS_WAKE_OK);
                iter2->second.Pop();
                entry->Wake(BIO_OK);
                BIO_TRACE_END(SDK_TRACE_QOS_WAKE_OK, BIO_OK);
            } else {
                CLIENT_LOG_DEBUG("IO wake execute preload task, nodeSet:" << nodeSet << ", key:" << entry->key <<
                    ", size:" << entry->size << ", remain quota:" << iter->second << ".");
                ExecutePreloadTask(ptEntry, nodeSet); // 写资源配额不满则继续启动预取任务.
                break;
            }
        } else {
            break;
        }
    } while (true);
}

void BioQuota::RollbackAllocQuotaReq(CmPtInfo *ptEntry, std::vector<uint16_t> nodeVec, std::vector<uint64_t> quotaVec)
{
    if (nodeVec.empty()) {
        return;
    }

    BResult ret = BIO_INNER_ERR;
    uint16_t localNid = BioClient::Instance()->GetMirror()->GetLocalNodeInfo().VNodeId();
    for (uint32_t idx = 0; idx < nodeVec.size(); idx++) {
        FreeQuotaRequest req = { { MESSAGE_MAGIC, ptEntry->ptId, ptEntry->version, localNid, getpid() },
            mLocalNodeId, mClientId, quotaVec[idx] };
        if (nodeVec[idx] == localNid) {
            ret = agent::BioClientAgent::Instance()->FreeQuota(req);
        } else {
            ret = SendFreeQuotaRemote(nodeVec[idx], req);
        }
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Send free quota resource failed, ret:" << ret << ", dstNid:" << nodeVec[idx]  << ".");
        } else {
            CLIENT_LOG_DEBUG("Free quota resource success, dstNid:" << nodeVec[idx]  << ", quota:" << quotaVec[idx]);
        }
    }
}

BResult BioQuota::SendFreeQuotaRemote(uint16_t nodeId, FreeQuotaRequest &req)
{
    BResult hdlRet = BIO_INNER_ERR;
    auto ret = net::BioClientNet::Instance()->SendSync<FreeQuotaRequest, BResult>(nodeId,
        BIO_OP_SDK_FREE_QUOTA, req, hdlRet);
    if (ret == BIO_OK && hdlRet != BIO_OK) {
        ret = hdlRet;
    }
    return ret;
}

BResult BioQuota::SendAllocQuotaRemote(uint16_t dstNid, AllocQuotaRequest &req, uint64_t &expectPreloadSize)
{
    AllocQuotaResponse rsp = { 0 };
    auto ret = net::BioClientNet::Instance()->SendSync<AllocQuotaRequest, AllocQuotaResponse>(dstNid,
        BIO_OP_SDK_ALLOC_QUOTA, req, rsp);
    expectPreloadSize = std::min<uint64_t>(expectPreloadSize, rsp.exceptQuota);
    return ret;
}

void BioQuota::AsyncPreloadQuota(CmPtInfo *ptEntry, uint16_t nodeSet)
{
    BResult ret = BIO_INNER_ERR;
    uint16_t localNid = BioClient::Instance()->GetMirror()->GetLocalNodeInfo().VNodeId();
    uint64_t expectPreloadSize = UINT64_MAX;
    std::vector<uint16_t> successNodeVec;
    std::vector<uint64_t> successQuotaVec;

    uint64_t preloadSize = mPreloadSize;
    AllocQuotaRequest req = { { MESSAGE_MAGIC, ptEntry->ptId, ptEntry->version, localNid, getpid() },
        mLocalNodeId, mClientId, preloadSize };
    BIO_TRACE_START(SDK_TRACE_QOS_PRELOAD);
    for (auto &item : ptEntry->copys) {
        uint16_t dstNid = item.nodeId;
        if (dstNid == localNid) {
            ret = agent::BioClientAgent::Instance()->AllocQuota(req, expectPreloadSize);
        } else {
            ret = SendAllocQuotaRemote(dstNid, req, expectPreloadSize);
        }
        if (UNLIKELY(ret != BIO_OK)) {
            CLIENT_LOG_ERROR("Send preload quota failed, ret:" << ret << ", dstNid:" << dstNid << ".");
            break;
        }
        successNodeVec.push_back(dstNid);
        successQuotaVec.push_back(preloadSize);
    }
    BIO_TRACE_END(SDK_TRACE_QOS_PRELOAD, ret);

    mPreloadSize = (ret == BIO_OK) ? expectPreloadSize : QUOTA_MIN_PRELOAD_SIZE;
    CLIENT_LOG_DEBUG("Dynamic adjust preload size, ret:" << ret << ", preload quota form " << preloadSize << " to" <<
        mPreloadSize << ".");
    if (UNLIKELY(ret != BIO_OK)) {
        RollbackAllocQuotaReq(ptEntry, successNodeVec, successQuotaVec);
        WakeForce(nodeSet, false);
    } else {
        UpdateQuotaRes(ptEntry, nodeSet, preloadSize);
    }
}

BResult BioQuota::Initialize(uint32_t scene)
{
    // 1. 获取流控的配置信息
    auto ret = agent::BioClientAgent::Instance()->GetLocalQuotaInfo(scene, mEnable, mPreloadSize);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get local qos info failed, ret:" << ret << ", nodeId:" << mLocalNodeId << ", clientId" <<
            mClientId << ".");
        return ret;
    }

    // 2. 创建配额申请线程池
    if (mEnable) {
        mQuotaAllocExecutor = ExecutorService::Create(NO_1, NO_4096);
        if (mQuotaAllocExecutor == nullptr) {
            CLIENT_LOG_ERROR("Failed to create quota alloc executor.");
            return BIO_ERR;
        }
        mQuotaAllocExecutor->SetThreadName("sdk-quota-alloc");
        if (!(mQuotaAllocExecutor->Start())) {
            CLIENT_LOG_ERROR("Failed to start quota alloc executor.");
            return BIO_ERR;
        }
    }

    CLIENT_LOG_INFO("Initialize bio quota success, enable:" << mEnable << ", preload size:" << mPreloadSize <<
        ", nid:" << mLocalNodeId << ", cid:" << mClientId << ".");
    return BIO_OK;
}

BResult BioQos::Initialize(uint32_t nodeId, WorkerMode mode, uint32_t scene)
{
    uint64_t pid = (mode == CONVERGENCE) ? 0 : static_cast<uint64_t>(getpid());
    mQuota = BioQuota::Instance(nodeId, pid);
    if (UNLIKELY(mQuota == nullptr)) {
        CLIENT_LOG_ERROR("Bio quota instance failed, nodeId:" << nodeId << ", pid:" << pid << ".");
        return BIO_INNER_ERR;
    }
    BResult ret = mQuota->Initialize(scene);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Bio quota initialize failed, ret:" << ret << ".");
        return ret;
    }

    mConcur = BioConcurrency::Instance();
    if (UNLIKELY(mQuota == nullptr)) {
        CLIENT_LOG_ERROR("Bio concurrency instance failed.");
        return BIO_INNER_ERR;
    }
    return mConcur->Initialize(scene);
}
