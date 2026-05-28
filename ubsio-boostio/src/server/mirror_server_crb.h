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

#ifndef MIRROR_SERVER_CRB_H
#define MIRROR_SERVER_CRB_H

#include <semaphore.h>
#include "bio_execution.h"
#include "bio_lock.h"
#include "bio_log.h"
#include "bio_ref.h"
#include "cm.h"

namespace ock {
namespace bio {
using AfterPtEventProcess = std::function<void()>;

struct CmPtTask;
using CmPtTaskPtr = Ref<CmPtTask>;
struct CmPtTask {
    std::vector<CmPtInfo> ptList;
    std::vector<CmPtFinish> ptFinish;

    SpinLock lock;
    std::vector<CmPtInfo> retryList;

    std::atomic<uint32_t> jobNum;
    sem_t jobSem;
    AfterPtEventProcess pFunc;
    DEFINE_REF_COUNT_FUNCTIONS;

public:
    void JobFinish(uint16_t ptId, uint64_t version)
    {
        LOG_INFO("Job end: ptId:" << ptId << ", version:" << version);
        jobNum++;
        if (jobNum == ptList.size()) {
            sem_post(&jobSem);
        }
    }

    DEFINE_REF_COUNT_VARIABLE;
};

class MirrorServerCrb;
using MirrorServerCrbPtr = Ref<MirrorServerCrb>;
class MirrorServerCrb {
public:
    MirrorServerCrb() = default;

    ~MirrorServerCrb() = default;

    inline static MirrorServerCrbPtr &Instance()
    {
        static auto instance = MakeRef<MirrorServerCrb>();
        return instance;
    }

    BResult Init();

    void Exit();

public:
    BResult NotifyPtChangeEvent(const std::map<uint16_t, CmPtInfo> &ptInfos, AfterPtEventProcess pFunc);

    void JobAddFinishList(CmPtTaskPtr ptTask, CmPtInfo &ptInfo);
    void JobAddRetryList(CmPtTaskPtr ptTask, CmPtInfo &ptInfo);
    bool JobPreCheck(CmPtInfo &ptInfo);
    BResult JobPreHandle(CmPtInfo &ptInfo, uint16_t &curIndex, bool &curExist);
    BResult JobExpiredClear(CmPtInfo &ptInfo);
    BResult JobSyncData(CmPtInfo &ptInfo);

    DEFINE_REF_COUNT_FUNCTIONS

private:
    void RunTaskThread(CmPtTaskPtr ptTask);
    void RunTaskThreadImpl(CmPtTaskPtr ptTask);
    void RunTaskThreadFinish(CmPtTaskPtr ptTask);
    void RunJobThread(CmPtTaskPtr ptTask, CmPtInfo ptInfo);

    void UpdatePt(CmPtInfo &ptInfo);

    BResult SendSyncDataReq(CmPtInfo &ptInfo);

private:
    ReadWriteLock mLock;
    std::map<uint16_t, CmPtInfo> mPtInfos;
    ExecutorServicePtr mTaskService{nullptr};
    ExecutorServicePtr mJobService{nullptr};
    bool mInited{false};

    DEFINE_REF_COUNT_VARIABLE
};
} // namespace bio
} // namespace ock
#endif // MIRROR_SERVER_CRB_H
