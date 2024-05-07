/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include "mirror_server_crb.h"
#include "mirror_server.h"
#include "bio_monotonic.h"
#include "bio_trace.h"
#include "bio_def.h"

namespace ock {
namespace bio {
constexpr uint16_t CRB_TASK_RETRY_MAX_TIME = 36000;
constexpr uint16_t CRB_TASK_INTERAL_TIME = 5;
constexpr uint16_t CRB_JOB_RETRY_MAX_TIME = 5;
constexpr uint16_t CRB_JOB_INTERAL_TIME = 1;
constexpr uint16_t CRB_TASK_THREAD_NUM = 1;
constexpr uint32_t CRB_TASK_QUEUE_SIZE = 8192;
constexpr uint16_t CRB_JOB_THREAD_NUM = 4;
constexpr uint32_t CRB_JOB_QUEUE_SIZE = 8192;

BResult MirrorServerCrb::Init()
{
    LVOS_TP_START(NO_PROCESS_MIRROR_SERVER_CRB_INIT, 0);
    if (mInited) {
        return BIO_OK;
    }
    LVOS_TP_END;

    BResult result;

    LVOS_TP_START(MIRROR_SERVER_TASK_FAIL, &mTaskService, nullptr);
    mTaskService = ExecutorService::Create(CRB_TASK_THREAD_NUM, CRB_TASK_QUEUE_SIZE);
    LVOS_TP_END;
    LVOS_TP_START(MIRROR_SERVER_TASK_FAIL_RESET_OUTER, &mTaskService);
    LVOS_TP_END;
    if (UNLIKELY(mTaskService == nullptr)) {
        LVOS_TP_START(MIRROR_SERVER_TASK_FAIL_RESET, &mTaskService);
        LVOS_TP_END;
        LOG_ERROR("Failed to start executor service for crb task");
        return BIO_ERR;
    }

    LVOS_TP_START(NO_PROCESS_MIRROR_SERVER_TASK_START, 0);
    mTaskService->SetThreadName("crb-task");
    result = mTaskService->Start();
    ChkTrueNot(result, BIO_INNER_ERR);
    LVOS_TP_END;

    LVOS_TP_START(MIRROR_SERVER_JOB_FAIL, &mJobService, nullptr);
    mJobService = ExecutorService::Create(CRB_JOB_THREAD_NUM, CRB_JOB_QUEUE_SIZE);
    LVOS_TP_END;
    if (UNLIKELY(mJobService == nullptr)) {
        LVOS_TP_START(MIRROR_SERVER_JOB_FAIL_RESET, &mJobService);
        LVOS_TP_END;
        LOG_ERROR("Failed to start executor service for crb job");
        return BIO_ERR;
    }

    mJobService->SetThreadName("crb-job");
    result = mJobService->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    mInited = true;
    return BIO_OK;
}

void MirrorServerCrb::Exit()
{
    mInited = false;
    mTaskService->Stop();
    mJobService->Stop();
}

BResult MirrorServerCrb::NotifyPtChangeEvent(const std::map<uint16_t, CmPtInfo> &ptInfos)
{
    CmPtTaskPtr ptTask = MakeRef<CmPtTask>();
    ChkTrueNot(ptTask != nullptr, BIO_ALLOC_FAIL);

    for (auto it = ptInfos.begin(); it != ptInfos.end(); ++it) {
        CmPtInfo ptInfo;
        ptInfo.Clone(it->second);
        ptTask->ptList.push_back(ptInfo);
    }
    auto ret = mTaskService->Execute([this, ptTask]() { RunTaskThread(ptTask); });
    if (ret == false) {
        LOG_ERROR("Notify Pt crb task failed");
        return BIO_ERR;
    }
    return BIO_OK;
}

void MirrorServerCrb::RunTaskThread(CmPtTaskPtr ptTask)
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();

    do {
        isRetry = false;
        RunTaskThreadImpl(ptTask);
        if (ptTask->ptList.size() != 0) {
            retryTime = Monotonic::TimeSec() - startTime;
            if (retryTime < CRB_TASK_RETRY_MAX_TIME) {
                isRetry = true;
                sleep(CRB_TASK_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return;
}

void MirrorServerCrb::RunTaskThreadImpl(CmPtTaskPtr ptTask)
{
    ptTask->jobNum = 0;

    for (auto &elem : ptTask->ptList) {
        auto ret = mJobService->Execute([this, ptTask, elem]() { RunJobThread(ptTask, elem); });
        if (ret == false) {
            LOG_INFO("Delay retry ptId:" << elem.ptId);
            JobAddRetryList(ptTask, elem);
            ptTask->jobNum++;
            continue;
        }
    }

    sem_init(&ptTask->jobSem, 0, 0);
    if (ptTask->jobNum != ptTask->ptList.size()) {
        sem_wait(&ptTask->jobSem);
    }
    sem_destroy(&ptTask->jobSem);

    ptTask->ptList.clear();
    ptTask->ptList.swap(ptTask->retryList);

    RunTaskThreadFinish(ptTask);
    return;
}

void MirrorServerCrb::RunTaskThreadFinish(CmPtTaskPtr ptTask)
{
    auto ret = Cm::Instance()->ReportPtFinish(ptTask->ptFinish);
    if (ret != BIO_OK) {
        LOG_ERROR("Report ptFinish fail:" << ret);
        return;
    }
    for (auto &elem : ptTask->ptFinish) {
        LOG_INFO("Report ptFinish: ptId:" << elem.ptId << ", version:" << elem.version);
    }
    ptTask->ptFinish.clear();
    return;
}

void MirrorServerCrb::RunJobThread(CmPtTaskPtr ptTask, CmPtInfo ptInfo)
{
    BResult ret;

    LOG_INFO("Job begin: ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);

    if (!JobPreCheck(ptInfo)) {
        ptTask->JobFinish(ptInfo.ptId, ptInfo.version);
        return;
    }

    uint16_t curIndex;
    bool curExist = false;
    ret = JobPreHandle(ptInfo, curIndex, curExist);
    if (ret != BIO_OK) {
        LOG_WARN("Pre handle fail:" << ret << ", ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);
        if (ret == BIO_INNER_RETRY) {
            JobAddRetryList(ptTask, ptInfo);
            ptTask->JobFinish(ptInfo.ptId, ptInfo.version);
            return;
        }
        ptTask->JobFinish(ptInfo.ptId, ptInfo.version);
        UpdatePt(ptInfo);
        return;
    }

    if (!curExist) {
        ptTask->JobFinish(ptInfo.ptId, ptInfo.version);
        UpdatePt(ptInfo);
        return;
    }

    if (ptInfo.copys[curIndex].state != CM_COPY_RECOVERY) {
        ptTask->JobFinish(ptInfo.ptId, ptInfo.version);
        UpdatePt(ptInfo);
        return;
    }

    ret = JobSyncData(ptInfo);
    if (ret == BIO_OK) {
        JobAddFinishList(ptTask, ptInfo);
        UpdatePt(ptInfo);
    } else if (ret == BIO_ALLOC_FAIL || ret == BIO_INNER_RETRY || ret == BIO_NET_RETRY) {
        JobAddRetryList(ptTask, ptInfo);
    }

    ptTask->JobFinish(ptInfo.ptId, ptInfo.version);
    return;
}

void MirrorServerCrb::UpdatePt(CmPtInfo &ptInfo)
{
    mLock.LockWrite();
    CmPtInfo cloneInfo;
    cloneInfo.Clone(ptInfo);
    mPtInfos[ptInfo.ptId] = cloneInfo;
    mLock.UnLock();
}

void MirrorServerCrb::JobAddFinishList(CmPtTaskPtr ptTask, CmPtInfo &ptInfo)
{
    ptTask->lock.Lock();
    ptTask->ptFinish.push_back(CmPtFinish(ptInfo.version, ptInfo.ptId));
    ptTask->lock.UnLock();
}

void MirrorServerCrb::JobAddRetryList(CmPtTaskPtr ptTask, CmPtInfo &ptInfo)
{
    ptTask->lock.Lock();
    ptTask->retryList.push_back(ptInfo);
    ptTask->lock.UnLock();
}

bool MirrorServerCrb::JobPreCheck(CmPtInfo &ptInfo)
{
    if (ptInfo.state == CM_PT_INIT || ptInfo.state == CM_PT_FAULT || ptInfo.state == CM_PT_BUTT) {
        return false;
    }

    CmPtInfo cache;
    auto ret = Cm::Instance()->GetPtInfo(ptInfo.ptId, cache);
    if (ret != BIO_OK) {
        LOG_ERROR("Get ptInfo fail:" << ret << ", ptId:" << ptInfo.ptId);
        return false;
    }

    if (cache.state != ptInfo.state || cache.version != ptInfo.version) {
        return false;
    }

    return true;
}

BResult MirrorServerCrb::JobPreHandle(CmPtInfo &ptInfo, uint16_t &curIndex, bool &curExist)
{
    bool isFirst = false;

    uint16_t localNodeId = Cm::Instance()->GetCmLocalNodeId().nodeId;
    uint16_t index;

    bool oldExist = false;

    mLock.LockRead();
    if (mPtInfos.find(ptInfo.ptId) != mPtInfos.end()) {
        for (index = 0; index < mPtInfos[ptInfo.ptId].copys.size(); index++) {
            if (mPtInfos[ptInfo.ptId].copys[index].nodeId == localNodeId) {
                oldExist = true;
                break;
            }
        }
    } else {
        isFirst = true;
    }
    mLock.UnLock();

    for (index = 0; index < ptInfo.copys.size(); index++) {
        if (ptInfo.copys[index].nodeId == localNodeId) {
            curIndex = index;
            curExist = true;
            break;
        }
    }

    if (!oldExist && curExist) {
        auto ret = Cache::Instance().ExtraCreateRCache(ptInfo.ptId, ptInfo.version);
        if (ret != BIO_OK) {
            return BIO_INNER_RETRY;
        }
    }

    if (!curExist && (isFirst || oldExist)) {
        auto ret = JobExpiredClear(ptInfo);
        if (ret != BIO_OK) {
            return ret;
        }
    }

    return BIO_OK;
}

BResult MirrorServerCrb::JobExpiredClear(CmPtInfo &ptInfo)
{
    auto ret = Cache::Instance().ExpiredClear(ptInfo.ptId, ptInfo.version);
    if (ret != BIO_OK) {
        LOG_WARN("Expired clear fail:" << ret << ", ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);
        return ret;
    }
    return BIO_OK;
}

BResult MirrorServerCrb::JobSyncData(CmPtInfo &ptInfo)
{
    BIO_TRACE_START(MIRROR_TRACE_SYNC_DATA);
    BResult ret;
    LVOS_TP_START(SERVER_CRB_SEND_FLUSH_FAIL, &ret, BIO_INNER_RETRY);
    ret = SendSyncDataReq(ptInfo);
    LVOS_TP_END;
    BIO_TRACE_END(MIRROR_TRACE_SYNC_DATA, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_WARN("Send sync data req fail:" << ret << ", ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);
        return ret;
    }
    LOG_INFO("Sync data succeed:"
        << "ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);

    ret = JobExpiredClear(ptInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    LOG_INFO("Expired clear succeed:"
        << "ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);

    return BIO_OK;
}

BResult MirrorServerCrb::SendSyncDataReq(CmPtInfo &ptInfo)
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret;

    do {
        isRetry = false;
        ret = MirrorServer::Instance()->SendSyncData(ptInfo.ptId, ptInfo.masterNodeId, ptInfo.version);
        if (ret == BIO_CHECK_PT_FAIL) {
            ret = BIO_INNER_RETRY;
        }
        if (UNLIKELY(ret != BIO_OK && ret != BIO_INNER_RETRY)) {
            LOG_ERROR("Send sync sync data failed:" << ret << ", ptId:" << ptInfo.ptId << ", version:" <<
                ptInfo.version);
            return ret;
        }
        if (ret == BIO_INNER_RETRY) {
            LOG_INFO("Delay retry, ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);
            retryTime = Monotonic::TimeSec() - startTime;
            if (retryTime < CRB_JOB_RETRY_MAX_TIME) {
                isRetry = true;
                sleep(CRB_JOB_INTERAL_TIME);
            }
        }
    } while (isRetry);

    return ret;
}
}
}
