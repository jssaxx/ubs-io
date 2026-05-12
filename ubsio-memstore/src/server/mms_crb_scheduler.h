/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef MMS_CRB_SCHEDULER_H
#define MMS_CRB_SCHEDULER_H

#include <unordered_map>
#include "mms_log.h"
#include "mms_ref.h"
#include "mms_types.h"
#include "mms_err.h"
#include "cm.h"
#include "mms_message.h"
#include "net_common.h"
#include "mms_cache.h"
#include "mms_kv_server.h"

namespace ock {
namespace mms {
using CrbHandle = std::function<BResult(void *reqBuff, uint16_t dstNode)>;

class CrbScheduler;
using CrbSchedulerPtr = Ref<CrbScheduler>;
class CrbScheduler {
public:
    static CrbSchedulerPtr &Instance()
    {
        static auto instance = MakeRef<CrbScheduler>();
        return instance;
    }

    void RegisterOpcode();

    BResult Init();

    void Exit();

    inline void ClearDeletedData()
    {
        mCache->ClearDeletedData();
    }

    void NotifyServiceable(bool serviceable)
    {
        mServiceable.store(serviceable);
        if (!serviceable) {
            return;
        }

        if (mIsRecovering.load()) {
            mCache->SetRecoverStatus(false);
            ClearDeletedData();  // CRB结束，清理cache里的墓碑数据
            mIsRecovering.store(false);
        }
    }

    inline void SetCrcSwitch(bool crcSwitch)
    {
        mCrcSwitch = crcSwitch;
    }

    inline void PrintRecoverView() const
    {
        LOG_INFO("Recover view:");
        for (auto &item : mRecoverNodes) {
            for (auto &pt : item.second) {
                LOG_INFO("recover pt id:" << pt << ", recover node:" << item.first << ".");
            }
        }
    }

    // faulty node
    void UpdateLocalCopys();
    BResult SelectNodeForRecover();
    BResult SendSingleCrbRequest(void *reqBuff, uint16_t dstNode, const CrbHandle &handle);
    BResult SendCrbRequests();
    BResult StartCatchUp();
    BResult HandleClientStartCatchUp(ServiceContext &ctx);
    BResult HandleCrbReceiveData(ServiceContext &ctx);
    BResult ReportPtRecoverDone(uint16_t nodeId, uint16_t ptId);
    BResult SelectMigrateNode(uint16_t &newRecoverNode);
    BResult SendMigrateRequest(uint16_t newRecoverNode, uint16_t ptId);
    BResult MigrateCrbToNewNode(uint16_t nodeId, uint16_t ptId);
    void HandlePtRecoverDone(uint16_t nodeId, uint16_t ptId);

    // recover node
    BResult HandleNotifyStartRecover(ServiceContext &ctx);
    BResult HandleRecoverData(CrbStartRequest *req);
    void BackGroundRecoverTask(uint16_t nodeId, uint16_t ptId);
    BResult EncodeKeyValueToBuff(char *msgBuff, uint32_t &buffOffset, uint16_t keyLen, IndexValue *indexValue);
    BResult ProcessBucket(uint32_t bucketIndex, uint32_t &curItemNum, uint16_t dstNodeId, char *msgBuff,
                          uint32_t &buffOffset);
    BResult CrbBatchSend(char *buff, uint32_t buffLen, uint16_t dstNode);
    void TaskDone(uint16_t nodeId, uint16_t taskPtId);

    void CrbBrokenHandle(const std::map<uint16_t, CmNodeInfo> &nodeInfos);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    std::atomic<bool> mIsRecovering{false};
    std::atomic<bool> mServiceable{false};
    bool mCrcSwitch = false;

    CmPtr mCm = nullptr;
    NetEnginePtr mNetEngine = nullptr;
    CachePtr mCache = nullptr;
    MmsKvServerPtr mKvServer = nullptr;

    uint16_t mRecoverIndex = 0;
    ReadWriteLock mRecoverLock;
    std::vector<CmPtCopy> mLocalPtCopys{};
    std::unordered_map<uint16_t, std::unordered_set<uint16_t>> mRecoverNodes{};  // key:nodeId, value:ptIds

    ReadWriteLock mRecoverTasksLock;
    std::unordered_map<uint16_t, std::unordered_set<uint16_t>> mRecoverTasks{};  // <nodeId, ptIds>

    ExecutorServicePtr mExeService = nullptr;
    DEFINE_REF_COUNT_VARIABLE;
};
}  // namespace mms
}  // namespace ock

#endif  // MMS_CRB_SCHEDULER_H

