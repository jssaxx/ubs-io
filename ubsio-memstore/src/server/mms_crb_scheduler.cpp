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

#include <cstring>
#include "mms_def.h"
#include "mms_server.h"
#include "mms_crc_util.h"
#include "mms_trace.h"
#include "mms_crb_scheduler.h"

namespace ock {
namespace mms {

constexpr uint8_t MAX_CRB_REQUEST_RETRY_COUNT = 5;

constexpr uint64_t CRB_PT_RECOVER_FINISH_FLAG = 1;
constexpr uint32_t CRB_RECOVER_MESSAGE_BUFF_LEN = 128 * 1024;
static thread_local uint16_t g_groupIndex = NumaGroupIndex::Instance()->GetGroupIndex();

static bool IsCrbRetryable(BResult ret)
{
    return ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY || ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL;
}

void CrbScheduler::RegisterOpcode()
{
    mNetEngine->RegisterNewRequestHandler(
        MMS_OP_S_CRB_START_RECOVER, std::bind(&CrbScheduler::HandleNotifyStartRecover, this, std::placeholders::_1));
    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_CRB_RECEIVE_DATA,
                                          std::bind(&CrbScheduler::HandleCrbReceiveData, this, std::placeholders::_1));
    mNetEngine->RegisterNewRequestHandler(
        MMS_OP_C_CRB_START_CATCH_UP, std::bind(&CrbScheduler::HandleClientStartCatchUp, this, std::placeholders::_1));
}

BResult CrbScheduler::Init()
{
    mCm = Cm::Instance();
    if (UNLIKELY(mCm == nullptr)) {
        LOG_ERROR("Cm instance is nullptr.");
        return MMS_ALLOC_FAIL;
    }

    mNetEngine = MmsServer::Instance()->GetNetEngine();
    if (UNLIKELY(mNetEngine == nullptr)) {
        LOG_ERROR("Net engine is nullptr");
        return MMS_ALLOC_FAIL;
    }

    mCache = Cache::Instance();
    if (UNLIKELY(mCache == nullptr)) {
        LOG_ERROR("Cache instance is nullptr.");
        return MMS_ALLOC_FAIL;
    }

    mKvServer = MmsKvServer::Instance();
    if (UNLIKELY(mKvServer == nullptr)) {
        LOG_ERROR("Kv server instance is nullptr.");
        return MMS_ALLOC_FAIL;
    }

    mExeService = ExecutorService::Create(NO_8, NO_2048);
    if (UNLIKELY(mExeService == nullptr)) {
        LOG_ERROR("Failed to create crb executor.");
        return MMS_ALLOC_FAIL;
    }

    mExeService->SetThreadName("CrbExecutor");
    if (UNLIKELY(!(mExeService->Start()))) {
        LOG_ERROR("Failed to start execution service for crb executor.");
        mExeService->Stop();
        return MMS_ERR;
    }

    RegisterOpcode();
    return MMS_OK;
}

void CrbScheduler::Exit()
{
    mExeService->Stop();
}

void CrbScheduler::UpdateLocalCopys()
{
    WriteLocker<ReadWriteLock> lock(&mRecoverLock);
    mLocalPtCopys.clear();
    mLocalPtCopys = Cm::Instance()->GetLocalPtCopys();
    LOG_DEBUG("Update crb pt copys success.");
    for (auto &item : mLocalPtCopys) {
        LOG_DEBUG("node id:" << item.nodeId << ", node state:" << item.state << ".");
    }
}

BResult CrbScheduler::SelectNodeForRecover()
{
    WriteLocker<ReadWriteLock> lock(&mRecoverLock);
    uint16_t ptNum = mCm->GetPtNum();
    uint16_t localNodeId = mCm->GetLocalNid();
    uint16_t researchCount = 0;
    mRecoverNodes.clear();

    if (UNLIKELY(ptNum == 0 || mLocalPtCopys.empty())) {
        LOG_ERROR("Local pt copy view is empty, pt num:" << ptNum << ".");
        return MMS_NOT_READY;
    }
    if (UNLIKELY(mRecoverIndex >= mLocalPtCopys.size())) {
        mRecoverIndex = 0;
    }

    for (uint16_t pt = 0; pt < ptNum;) {
        if (mLocalPtCopys[mRecoverIndex].state == CmCopyState::CM_COPY_RUNNING &&
            mLocalPtCopys[mRecoverIndex].nodeId != localNodeId) {
            uint16_t nodeId = mLocalPtCopys[mRecoverIndex].nodeId;
            mRecoverNodes[nodeId].emplace(pt);
            researchCount = 0;
            ++pt;
        } else {
            researchCount++;
            if (researchCount == mLocalPtCopys.size()) {
                LOG_ERROR("All local copys is fault, no running node.");
                return MMS_CHECK_PT_FAIL;
            }
        }
        mRecoverIndex = (mRecoverIndex + NO_1) % mLocalPtCopys.size();
    }

    LOG_INFO("Select node for recover success.");
    PrintRecoverView();
    return MMS_OK;
}

BResult CrbScheduler::SendSingleCrbRequest(void *reqBuff, uint16_t dstNode, const CrbHandle &handle)
{
    BResult ret = MMS_OK;
    uint16_t retryCount = 0;
    do {
        ret = handle(reqBuff, dstNode);
        if (LIKELY(ret == MMS_OK)) {
            return MMS_OK;
        }

        LOG_ERROR("Send request failed, ret:" << ret << ", dst node:" << dstNode << ", retry count:" << ++retryCount
                                              << ".");
        if (retryCount > MAX_CRB_REQUEST_RETRY_COUNT) {
            LOG_ERROR("Send request failed after " << retryCount << " retries, exiting.");
            break;
        }

        if (!IsCrbRetryable(ret)) {
            break;
        }
    } while (true);

    return ret;
}

BResult CrbScheduler::SendCrbRequests()
{
    BResult ret;
    CrbStartRequest req;

    CrbHandle handle = [this](void *reqBuff, uint16_t dstNode) -> BResult {
        CrbStartRequest req = *(static_cast<CrbStartRequest *>(reqBuff));
        BResult rsp = MMS_OK;
        BResult ret =
            mNetEngine->SyncCall<CrbStartRequest, BResult>(dstNode, g_groupIndex, MMS_OP_S_CRB_START_RECOVER, req, rsp);
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }

        return rsp;
    };

    ReadLocker<ReadWriteLock> lock(&mRecoverLock);
    for (auto &item : mRecoverNodes) {
        for (auto &ptId : item.second) {
            req.head = {mCm->GetLocalNid(), MMS_OP_S_CRB_START_RECOVER, g_groupIndex, ptId, 0};
            ret = SendSingleCrbRequest(static_cast<void *>(&req), item.first, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send crb request failed, ret:" << ret << ", dst node:" << item.first << ".");
                return ret;
            }
        }
    }
    return MMS_OK;
}

BResult CrbScheduler::StartCatchUp()
{
    if (mServiceable) {
        LOG_INFO("This node is serviceable, not need recover.");
        return MMS_OK;
    }

    bool expected = false;
    if (!mIsRecovering.compare_exchange_strong(expected, true)) {
        LOG_INFO("Crb task is already running.");
        return MMS_OK;
    }

    LOG_INFO("Start recover...");
    mCache->SetRecoverStatus(true);
    BResult ret = SelectNodeForRecover();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Select recover node failed, ret:" << ret << ".");
        mIsRecovering.store(false);
        mCache->SetRecoverStatus(false);
        return ret;
    }

    ret = SendCrbRequests();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Send crb requests failed, ret:" << ret << ".");
        mIsRecovering.store(false);
        mCache->SetRecoverStatus(false);
        return ret;
    }

    return MMS_OK;
}

BResult CrbScheduler::HandleClientStartCatchUp(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(BasicRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    BResult ret = StartCatchUp();
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Start catchup failed, ret:" << ret << ".");
        mNetEngine->Reply(ctx, ret, nullptr, 0);
        return MMS_OK;
    }

    mNetEngine->Reply(ctx, MMS_OK, nullptr, 0);
    return MMS_OK;
}

BResult CrbScheduler::HandleCrbReceiveData(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) ||
        UNLIKELY(ctx.MessageDataLen() > CRB_RECOVER_MESSAGE_BUFF_LEN) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    if (!mIsRecovering) {
        LOG_WARN("Not recover status.");
        mNetEngine->Reply(ctx, MMS_NOT_READY, nullptr, 0);
        return MMS_OK;
    }

    BResult ret = mKvServer->PutLocal(ctx.MessageData(), ctx.MessageDataLen());
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Crb put local failed, ret:" << ret << ".");
        mNetEngine->Reply(ctx, ret, nullptr, 0);
        return MMS_OK;
    }

    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(ctx.MessageData());
    if (req->head.ptv == CRB_PT_RECOVER_FINISH_FLAG) {  // pt恢复完了
        LOG_INFO("Recover done, start report pt done:" << req->head.ptId << ".");
        ret = ReportPtRecoverDone(req->head.nodeId, req->head.ptId);
    }

    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

void CrbScheduler::HandlePtRecoverDone(uint16_t nodeId, uint16_t ptId)
{
    WriteLocker<ReadWriteLock> lock(&mRecoverLock);
    auto itNode = mRecoverNodes.find(nodeId);
    if (itNode == mRecoverNodes.end()) {
        return;
    }

    itNode->second.erase(ptId);
    if (itNode->second.empty()) {
        mRecoverNodes.erase(itNode);
        LOG_INFO("The node's all task is done:" << nodeId << ".");
    }

    LOG_INFO("Pt recover done, nodeId:" << nodeId << ", ptId:" << ptId << ".");
}

BResult CrbScheduler::ReportPtRecoverDone(uint16_t nodeId, uint16_t ptId)
{
    {
        ReadLocker<ReadWriteLock> lock(&mRecoverLock);
        auto itNode = mRecoverNodes.find(nodeId);
        if (itNode == mRecoverNodes.end() || itNode->second.find(ptId) == itNode->second.end()) {
            LOG_WARN("Ignore stale pt recover done, nodeId:" << nodeId << ", ptId:" << ptId << ".");
            return MMS_OK;
        }
    }

    CmPtInfo ptInfo;
    BResult ret = Cm::Instance()->GetPtInfo(ptId, ptInfo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt info failed, ret:" << ret << ", pt:" << ptId << ".");
        return ret;
    }

    uint16_t retryCount = 0;

    do {
        ret = Cm::Instance()->ReportPtFinish(ptId, ptInfo.version);
        if (LIKELY(ret == MMS_OK)) {
            LOG_INFO("Report pt finish success, pt:" << ptId << ".");
            break;
        }

        LOG_ERROR("Report pt finish failed, ret:" << ret << ", pt:" << ptId << ", retry count:" << ++retryCount << ".");
        if (retryCount > MAX_CRB_REQUEST_RETRY_COUNT) {
            LOG_ERROR("Report pt finish failed after " << retryCount << " retries, exiting.");
            break;
        }
    } while (true);

    if (LIKELY(ret == MMS_OK)) {
        HandlePtRecoverDone(nodeId, ptId);
    }
    return ret;
}

BResult CrbScheduler::HandleNotifyStartRecover(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(CrbStartRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("[CrbSched]Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    CrbStartRequest *req = reinterpret_cast<CrbStartRequest *>(ctx.MessageData());
    LOG_INFO("Receive a crb request, recover pt:" << req->head.ptId << ", recover node:" << req->head.nodeId << ".");

    BResult ret = HandleRecoverData(req);
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult CrbScheduler::CrbBatchSend(char *buff, uint32_t buffLen, uint16_t dstNode)
{
    CrbHandle handle = [this, buffLen](void *buff, uint16_t dstNode) -> BResult {
        BResult result = MMS_OK;
        BResult ret = mNetEngine->SyncCall(dstNode, g_groupIndex, MMS_OP_S_CRB_RECEIVE_DATA, buff, buffLen, result);
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }

        return result;
    };

    MMS_TRACE_START(CRB_BATCH_SEND_BUFF);
    BResult ret = SendSingleCrbRequest(static_cast<void *>(buff), dstNode, handle);
    MMS_TRACE_END(CRB_BATCH_SEND_BUFF, ret);

    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Send single crb request failed, ret:" << ret << ".");
        return ret;
    }

    LOG_DEBUG("Crb batch send success, buffLen:" << buffLen << ".");
    return MMS_OK;
}

void CrbScheduler::TaskDone(uint16_t nodeId, uint16_t taskPtId)
{
    WriteLocker<ReadWriteLock> lock(&mRecoverTasksLock);
    auto itNode = mRecoverTasks.find(nodeId);
    if (itNode == mRecoverTasks.end()) {
        return;
    }

    auto itPt = itNode->second.find(taskPtId);
    if (itPt == itNode->second.end()) {
        return;
    }

    itNode->second.erase(itPt);
    if (itNode->second.empty()) {
        mRecoverTasks.erase(itNode);
    }
}

BResult CrbScheduler::SelectMigrateNode(uint16_t &newRecoverNode)
{
    uint16_t localNodeId = mCm->GetLocalNid();
    uint16_t researchCount = 0;
    if (UNLIKELY(mLocalPtCopys.empty())) {
        LOG_ERROR("Local pt copy view is empty.");
        return MMS_NOT_READY;
    }
    if (UNLIKELY(mRecoverIndex >= mLocalPtCopys.size())) {
        mRecoverIndex = 0;
    }

    for (;;) {
        if (mLocalPtCopys[mRecoverIndex].state == CmCopyState::CM_COPY_RUNNING &&
            mLocalPtCopys[mRecoverIndex].nodeId != localNodeId) {
            newRecoverNode = mLocalPtCopys[mRecoverIndex].nodeId;
            mRecoverIndex = (mRecoverIndex + NO_1) % mLocalPtCopys.size();
            return MMS_OK;
        } else {
            mRecoverIndex = (mRecoverIndex + NO_1) % mLocalPtCopys.size();
            researchCount++;
            if (researchCount == mLocalPtCopys.size()) {
                LOG_ERROR("All copys is fault, no running node.");
                return MMS_CHECK_PT_FAIL;
            }
        }
    }
}

BResult CrbScheduler::SendMigrateRequest(uint16_t newRecoverNode, uint16_t ptId)
{
    CrbStartRequest req;
    CrbHandle handle = [this](void *reqBuff, uint16_t dstNode) -> BResult {
        CrbStartRequest req = *(static_cast<CrbStartRequest *>(reqBuff));
        BResult rsp = MMS_OK;
        BResult ret =
            mNetEngine->SyncCall<CrbStartRequest, BResult>(dstNode, g_groupIndex, MMS_OP_S_CRB_START_RECOVER, req, rsp);
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }

        return rsp;
    };

    req.head = {mCm->GetLocalNid(), MMS_OP_S_CRB_START_RECOVER, g_groupIndex, ptId, 0};
    BResult ret = SendSingleCrbRequest(static_cast<void *>(&req), newRecoverNode, handle);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Send crb request failed, ret:" << ret << ", dst node:" << newRecoverNode << ".");
        return ret;
    }

    return MMS_OK;
}

BResult CrbScheduler::MigrateCrbToNewNode(uint16_t nodeId, uint16_t ptId)
{
    uint16_t newRecoverNode;
    BResult ret;
    uint16_t retryCount = 0;
    uint16_t maxRetryCount = mLocalPtCopys.size();
    do {
        ret = SelectMigrateNode(newRecoverNode);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("No normal node can migrate, exit.");
            return MMS_INNER_ERR;
        }

        ret = SendMigrateRequest(newRecoverNode, ptId);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Send crb migrate request failed, select a new node and try again, retry count:" << ++retryCount
                                                                                                       << ".");
            if (retryCount > maxRetryCount) {
                LOG_ERROR("No node can migrate, exit.");
                return MMS_INNER_ERR;
            }
            continue;
        }

        break;
    } while (true);

    mRecoverNodes[newRecoverNode].emplace(ptId);
    LOG_INFO("Migrate crb success, ptId:" << ptId << ", old node:" << nodeId << ", new node:" << newRecoverNode << ".");
    PrintRecoverView();
    return MMS_OK;
}

void CrbScheduler::CrbBrokenHandle(const std::map<uint16_t, CmNodeInfo> &nodeInfos)
{
    // 正在进行数据恢复的节点故障了，迁移任务到新节点
    if (mIsRecovering) {
        WriteLocker<ReadWriteLock> lock(&mRecoverLock);
        BResult ret;
        for (auto &item : nodeInfos) {
            if (item.second.status != CmNodeStatus::CM_NODE_FAULT) {
                continue;
            }
            if (mRecoverNodes.find(item.first) == mRecoverNodes.end()) {
                continue;
            }

            std::unordered_set<uint16_t> ptIds = mRecoverNodes[item.first];
            mRecoverNodes.erase(item.first);
            for (auto &ptId : ptIds) {
                ret = MigrateCrbToNewNode(item.first, ptId);
                if (UNLIKELY(ret != MMS_OK)) {
                    mIsRecovering.store(false);  // 副本节点全挂了，退出恢复流程
                    mCache->SetRecoverStatus(false);
                    LOG_ERROR("Migrate crb to new node failed, ret:" << ret << ".");
                    return;
                }
            }
        }

        return;
    }

    WriteLocker<ReadWriteLock> lock(&mRecoverTasksLock);
    if (mRecoverTasks.empty()) {
        return;
    }

    for (auto &item : nodeInfos) {
        if (item.second.status == CmNodeStatus::CM_NODE_FAULT) {  // 被恢复的节点又挂了，清理掉后台恢复任务
            mRecoverTasks.erase(item.first);
        }
    }
}

// 消息组装:[IoDataRequest] [IoLocDesc_1][key_1][value_1] [IoLocDesc_2][key_2][value_2]...[IoLocDesc_n][key_n][value_n]
BResult CrbScheduler::EncodeKeyValueToBuff(char *msgBuff, uint32_t &buffOffset, uint16_t keyLen, IndexValue *indexValue)
{
    IoLocDesc *desc = reinterpret_cast<IoLocDesc *>(msgBuff + buffOffset);
    desc->offset = 0;
    desc->keyLen = keyLen + NO_1;
    desc->version = indexValue->version;
    buffOffset += IO_DESCRIPTION_LEN;

    // copy key
    BResult ret = strncpy_s(msgBuff + buffOffset, CRB_RECOVER_MESSAGE_BUFF_LEN - buffOffset, indexValue->key, keyLen);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Copy key failed.");
        return MMS_INNER_ERR;
    }
    buffOffset += (keyLen + NO_1);

    // copy value
    uint64_t readLen = Cache::Instance()->GetDataFromBlock(indexValue, msgBuff + buffOffset, 0,
                                                           indexValue->totalDataLen);
    if (UNLIKELY(readLen == 0)) {
        LOG_ERROR("Get data failed.");
        return MMS_INNER_ERR;
    }

    buffOffset += readLen;
    desc->valueLen = readLen;

    return MMS_OK;
}

BResult CrbScheduler::ProcessBucket(uint32_t bucketIndex, uint32_t &curItemNum, uint16_t dstNodeId, char *msgBuff,
                                    uint32_t &buffOffset)
{
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(msgBuff);
    uint64_t bucketAddr = mCache->GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);
    BResult ret = MMS_OK;

    CacheReadLock(&bucketNode->status);
    IndexNode *node = &bucketNode->head;
    while (node->valid == FLAG_VALID) {
        IndexValue *indexValue = reinterpret_cast<IndexValue *>(node->indexValueAddr);
        if (indexValue->ptId != req->head.ptId) {
            node = &indexValue->next;
            continue;
        }

        uint16_t keyLen = strlen(indexValue->key);
        uint64_t needLen = IO_DESCRIPTION_LEN + keyLen + NO_1 + indexValue->totalDataLen;
        if (UNLIKELY(needLen > (CRB_RECOVER_MESSAGE_BUFF_LEN - IO_DATA_REQUEST_LEN))) {
            CacheReadUnLock(&bucketNode->status);
            LOG_ERROR("Ioctx buff is too small, need len:" << needLen << ", buff len:" << CRB_RECOVER_MESSAGE_BUFF_LEN
                                                           << ".");
            return MMS_INVALID_PARAM;
        }

        if (needLen > (CRB_RECOVER_MESSAGE_BUFF_LEN - buffOffset)) {  // 装满了
            req->num = curItemNum;
            req->head.ptv = 0;
            if (mCrcSwitch) {
                static uint32_t skip =
                    sizeof(req->head) + sizeof(req->seqNo) + sizeof(req->negoSeqNo) + sizeof(req->crc);
                req->crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(msgBuff + skip), buffOffset - skip);
            }

            ret = CrbBatchSend(msgBuff, buffOffset, dstNodeId);
            if (UNLIKELY(ret != MMS_OK)) {
                CacheReadUnLock(&bucketNode->status);
                LOG_ERROR("Crb Batch send failed, ret: " << ret << ".");
                return ret;
            }

            curItemNum = 0;
            buffOffset = IO_DATA_REQUEST_LEN;
        }

        MMS_TRACE_START(CRB_COPY_VALUE_TO_BUFF);
        ret = EncodeKeyValueToBuff(msgBuff, buffOffset, keyLen, indexValue);
        MMS_TRACE_END(CRB_COPY_VALUE_TO_BUFF, ret);
        if (UNLIKELY(ret != MMS_OK)) {
            CacheReadUnLock(&bucketNode->status);
            LOG_ERROR("Encode key value to buff failed, ret:" << ret << ", key:" << indexValue->key << ".");
            return ret;
        }

        curItemNum++;
        node = &indexValue->next;
    }

    CacheReadUnLock(&bucketNode->status);
    return MMS_OK;
}

void CrbScheduler::BackGroundRecoverTask(uint16_t nodeId, uint16_t taskPtId)
{
    LOG_INFO("Start recover task, node:" << nodeId << ", ptId:" << taskPtId << ".");
    uint32_t bucketCount = mCache->GetBucketCount();
    char *msgBuff = new (std::nothrow) char[CRB_RECOVER_MESSAGE_BUFF_LEN];
    if (UNLIKELY(msgBuff == nullptr)) {
        LOG_ERROR("Alloc memory failed, size:" << CRB_RECOVER_MESSAGE_BUFF_LEN << ".");
        TaskDone(nodeId, taskPtId);
        return;
    }

    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(msgBuff);
    req->head = {mCm->GetLocalNid(), MMS_OP_S_CRB_RECEIVE_DATA, g_groupIndex, taskPtId, 0};
    uint32_t buffOffset = IO_DATA_REQUEST_LEN;
    BResult ret = MMS_OK;
    uint32_t curItemNum = 0;

    for (uint32_t bucketIndex = 0; bucketIndex < bucketCount; bucketIndex++) {
        {
            ReadLocker<ReadWriteLock> lock(&mRecoverTasksLock);
            auto it = mRecoverTasks.find(nodeId);
            if (it == mRecoverTasks.end() || it->second.find(taskPtId) == it->second.end()) {
                LOG_INFO("The task was terminated, node:" << nodeId << ", ptId:" << taskPtId << ".");
                delete[] msgBuff;
                return;
            }
        }

        ret = ProcessBucket(bucketIndex, curItemNum, nodeId, msgBuff, buffOffset);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Traverse hash bucket failed, ret:" << ret << ", bucket index:" << bucketIndex << ".");
            delete[] msgBuff;
            TaskDone(nodeId, taskPtId);
            return;
        }
    }

    req->head.ptv = CRB_PT_RECOVER_FINISH_FLAG;  // 借用该字段表示该pt是否已经完成恢复
    req->num = curItemNum;
    if (mCrcSwitch) {
        static uint32_t skip = sizeof(req->head) + sizeof(req->seqNo) + sizeof(req->negoSeqNo) + sizeof(req->crc);
        req->crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(msgBuff + skip), buffOffset - skip);
    }

    ret = CrbBatchSend(msgBuff, buffOffset, nodeId);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Crb Batch send failed, ret: " << ret << ", node:" << nodeId << ", pt:" << taskPtId << ".");
    }

    delete[] msgBuff;
    TaskDone(nodeId, taskPtId);
    LOG_INFO("Recover task done, node:" << nodeId << ", ptId:" << taskPtId << ".");
}

BResult CrbScheduler::HandleRecoverData(CrbStartRequest *req)
{
    {
        WriteLocker<ReadWriteLock> lock(&mRecoverTasksLock);
        auto nodeIt = mRecoverTasks.find(req->head.nodeId);
        if (nodeIt != mRecoverTasks.end()) {
            auto ptIt = nodeIt->second.find(req->head.ptId);
            if (ptIt != nodeIt->second.end()) {
                LOG_INFO("Recover task is exist, faulty node:" << req->head.nodeId << ", pt:" << req->head.ptId << ".");
                return MMS_OK;
            } else {
                nodeIt->second.emplace(req->head.ptId);
            }
        } else {
            mRecoverTasks[req->head.nodeId].emplace(req->head.ptId);
        }
    }

    uint16_t nodeId = req->head.nodeId;
    uint16_t ptId = req->head.ptId;

    bool ret = mExeService->Execute([this, nodeId, ptId]() { this->BackGroundRecoverTask(nodeId, ptId); });
    if (UNLIKELY(!ret)) {
        LOG_ERROR("Start crb recover task failed.");
        TaskDone(nodeId, ptId);
        return MMS_INNER_ERR;
    }

    LOG_INFO("Add a recover task, node:" << req->head.nodeId << ", pt:" << req->head.ptId << ".");
    return MMS_OK;
}

}  // namespace mms
}  // namespace ock
