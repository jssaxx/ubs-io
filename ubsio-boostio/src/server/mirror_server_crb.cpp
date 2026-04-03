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

#include "mirror_server_crb.h"

#include <utility>
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
constexpr uint16_t CRB_JOB_THREAD_NUM = 8;
constexpr uint32_t CRB_JOB_QUEUE_SIZE = 8192;

BResult MirrorServerCrb::Init()
{
    BIO_TP_START(NO_PROCESS_MIRROR_SERVER_CRB_INIT, 0);
    if (mInited) {
        return BIO_OK;
    }
    BIO_TP_END;

    mTaskService = ExecutorService::Create(CRB_TASK_THREAD_NUM, CRB_TASK_QUEUE_SIZE);
    if (UNLIKELY(mTaskService == nullptr)) {
        LOG_ERROR("Failed to start executor service for crb task");
        return BIO_ERR;
    }

    mTaskService->SetThreadName("crb-task");
    auto result = mTaskService->Start();
    ChkTrue(result, BIO_INNER_ERR, "Mirror server crb task start failed.");

    mJobService = ExecutorService::Create(CRB_JOB_THREAD_NUM, CRB_JOB_QUEUE_SIZE);
    if (UNLIKELY(mJobService == nullptr)) {
        LOG_ERROR("Failed to start executor service for crb job");
        return BIO_ERR;
    }

    mJobService->SetThreadName("crb-job");
    result = mJobService->Start();
    ChkTrue(result, BIO_INNER_ERR, "Mirror server crb job start failed.");

    mInited = true;
    return BIO_OK;
}

void MirrorServerCrb::Exit()
{
    mInited = false;
    mTaskService->Stop();
    mJobService->Stop();
}

BResult MirrorServerCrb::NotifyPtChangeEvent(const std::map<uint16_t, CmPtInfo> &ptInfos, AfterPtEventProcess pFunc)
{
    CmPtTaskPtr ptTask = MakeRef<CmPtTask>();
    ChkTrueNot(ptTask != nullptr, BIO_ALLOC_FAIL);
    ptTask->pFunc = std::move(pFunc);
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
        LOG_INFO("Job pre: ptId:" << elem.ptId << ", version:" << elem.version);
        auto ret = mJobService->Execute([this, ptTask, elem]() { RunJobThread(ptTask, elem); });
        if (!ret) {
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
    if (ptTask->pFunc) {
        ptTask->pFunc();
    }

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

void MirrorServerCrb::RunJobThread(const CmPtTaskPtr &ptTask, CmPtInfo ptInfo)
{
    BResult ret = BIO_OK;

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
    size_t index;

    bool oldExist = false;
    bool oldMaster = false;

    mLock.LockRead();
    if (mPtInfos.find(ptInfo.ptId) != mPtInfos.end()) {
        for (index = 0; index < mPtInfos[ptInfo.ptId].copys.size(); index++) {
            if (mPtInfos[ptInfo.ptId].copys[index].nodeId == localNodeId) {
                oldExist = true;
                break;
            }
        }
        oldMaster = (mPtInfos[ptInfo.ptId].masterNodeId == localNodeId);
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
    bool curMaster = (ptInfo.masterNodeId == localNodeId);

    if (!oldMaster && curMaster) {
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
    return Cache::Instance().ExpiredClear(ptInfo.ptId, ptInfo.version);
}

BResult MirrorServerCrb::JobSyncData(CmPtInfo &ptInfo)
{
    BIO_TRACE_START(MIRROR_TRACE_SYNC_DATA);
    BResult ret = BIO_INNER_ERR;
    BIO_TP_START(SERVER_CRB_SEND_FLUSH_FAIL, &ret, BIO_INNER_RETRY);
    ret = SendSyncDataReq(ptInfo);
    BIO_TP_END;
    BIO_TRACE_END(MIRROR_TRACE_SYNC_DATA, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_WARN("Send sync data req fail:" << ret << ", ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);
        return ret;
    }
    LOG_INFO("Sync data succeed:" << "ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);

    ret = JobExpiredClear(ptInfo);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    LOG_INFO("Expired clear succeed:" << "ptId:" << ptInfo.ptId << ", version:" << ptInfo.version);

    return BIO_OK;
}

BResult MirrorServerCrb::SendSyncDataReq(CmPtInfo &ptInfo)
{
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;

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
