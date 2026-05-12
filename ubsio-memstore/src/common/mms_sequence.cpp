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

#include "mms_sequence.h"
#include "mms_def.h"

namespace ock {
namespace mms {

constexpr uint64_t g_seqExpiredTime = 180;

BResult MmsSequence::Initialize(uint32_t lev1Cap, uint32_t lev2Cap)
{
    if (UNLIKELY(lev1Cap == 0 || lev2Cap == 0)) {
        return MMS_ERR;
    }

    mSlidingBuff = new SlidingQueue[lev1Cap * lev2Cap];
    if (UNLIKELY(mSlidingBuff == nullptr)) {
        return MMS_ERR;
    }

    mNegoBuff = new NegoQueue[lev1Cap * lev2Cap];
    if (UNLIKELY(mNegoBuff == nullptr)) {
        delete[] mSlidingBuff;
        return MMS_ERR;
    }

    uint32_t index;

    mSlidingQueue.reserve(lev1Cap);
    mSlidingQueue.clear();
    index = 0;
    for (uint32_t i = 0; i < lev1Cap; i++) {
        std::vector<SlidingQueue *> queue;
        for (uint32_t j = 0; j < lev2Cap; j++) {
            queue.push_back(&mSlidingBuff[index]);
            index++;
        }
        mSlidingQueue.emplace_back(queue);
    }
    mNegoQueue.reserve(lev1Cap);
    mNegoQueue.clear();
    index = 0;
    for (uint32_t i = 0; i < lev1Cap; i++) {
        std::vector<NegoQueue *> queue;
        for (uint32_t j = 0; j < lev2Cap; j++) {
            queue.emplace_back(&mNegoBuff[index]);
            index++;
        }
        mNegoQueue.emplace_back(queue);
    }
    mLev1Cap = lev1Cap;
    mLev2Cap = lev2Cap;
    return MMS_OK;
}

void MmsSequence::Exit()
{
    mSlidingQueue.clear();
    mNegoQueue.clear();

    if (mSlidingBuff != nullptr) {
        delete[] mSlidingBuff;
        mSlidingBuff = nullptr;
    }
    if (mNegoBuff != nullptr) {
        delete[] mNegoBuff;
        mNegoBuff = nullptr;
    }
}

BResult MmsSequence::ResetSeqNoState2Mst(uint32_t lev1Id, uint32_t lev2Id, uint64_t seqNo)
{
    if (!mSequence) {
        return MMS_OK;
    }

    if (UNLIKELY(lev1Id >= mLev1Cap || lev2Id >= mLev2Cap)) {
        return MMS_ERR;
    }

    SlidingQueue *queue = mSlidingQueue[lev1Id][lev2Id];
    queue->lock.Lock();
    queue->cursor.left = queue->cursor.right = seqNo;
    for (uint32_t idx = 0; idx < SEQ_QUEUE_LEN; idx++) {
        queue->statusList[idx].finish = false;
    }
    queue->lock.UnLock();
    return MMS_OK;
}

BResult MmsSequence::ApplyForSeqNo2Mst(uint32_t lev1Id, uint32_t lev2Id, uint64_t &seqNo, uint64_t &negoSeqNo)
{
    if (!mSequence) {
        return MMS_OK;
    }

    if (UNLIKELY(lev1Id >= mLev1Cap || lev2Id >= mLev2Cap)) {
        return MMS_ERR;
    }

    SlidingQueue *queue = mSlidingQueue[lev1Id][lev2Id];
    queue->lock.Lock();
    if (queue->cursor.right - queue->cursor.left < SEQ_QUEUE_LEN) {
        queue->cursor.right++;
        seqNo = queue->cursor.right;
        negoSeqNo = queue->cursor.left;
        queue->lock.UnLock();
        return MMS_OK;
    }
    queue->lock.UnLock();
    return MMS_INNER_RETRY;
}

BResult MmsSequence::ReleaseSeqNo2Mst(uint32_t lev1Id, uint32_t lev2Id, uint64_t seqNo)
{
    if (!mSequence) {
        return MMS_OK;
    }

    if (UNLIKELY(lev1Id >= mLev1Cap || lev2Id >= mLev2Cap)) {
        return MMS_ERR;
    }

    SlidingQueue *queue = mSlidingQueue[lev1Id][lev2Id];
    queue->lock.Lock();
    uint64_t seqIdx = seqNo & SEQ_QUEUE_MASK;
    queue->statusList[seqIdx].finish = true;
    while (queue->cursor.left < queue->cursor.right) {
        seqIdx = (queue->cursor.left + NO_1) & SEQ_QUEUE_MASK;
        if (queue->statusList[seqIdx].finish) {
            queue->statusList[seqIdx].finish = false;
            queue->cursor.left++;
            continue;
        }
        break;
    };
    queue->lock.UnLock();
    return MMS_OK;
}

BResult MmsSequence::NegoSeqNo2Slv(uint32_t lev1Id, uint32_t lev2Id, uint64_t seqNo, void *data, uint32_t len,
                                   uint64_t negoSeqNo)
{
    if (!mSequence) {
        return MMS_OK;
    }

    if (UNLIKELY(lev1Id >= mLev1Cap || lev2Id >= mLev2Cap)) {
        return MMS_ERR;
    }

    void *dataTmp = malloc(len);
    if (UNLIKELY(dataTmp == nullptr)) {
        return MMS_ALLOC_FAIL;
    }
    if (UNLIKELY(memcpy_s(dataTmp, len, data, len) != MMS_OK)) {
        free(dataTmp);
        return MMS_ERR;
    }

    NegoQueue *queue = mNegoQueue[lev1Id][lev2Id];
    queue->lock.Lock();
    uint64_t seqIdx = seqNo & SEQ_QUEUE_MASK;
    if (LIKELY(!queue->valueList[seqIdx].valid)) {
        queue->valueList[seqIdx].valid = true;
        queue->valueList[seqIdx].seqNo = seqNo;
        queue->valueList[seqIdx].data = dataTmp;
        queue->valueList[seqIdx].len = len;
        NegoSeqNoHandle(queue, seqNo, negoSeqNo);
        queue->lock.UnLock();
        return MMS_OK;
    }
    if (queue->valueList[seqIdx].seqNo == seqNo) {
        queue->lock.UnLock();
        free(dataTmp);
        return MMS_OK;
    } else {
        queue->lock.UnLock();
        free(dataTmp);
        return MMS_KEY_CONFLICT;
    }
}

void MmsSequence::NegoSeqNoHandle(NegoQueue *queue, uint64_t seqNo, uint64_t negoSeqNo)
{
    while (queue->negoSeqNo < negoSeqNo) {
        uint64_t seqIdx = (queue->negoSeqNo + NO_1) & SEQ_QUEUE_MASK;
        if (queue->valueList[seqIdx].valid) {
            queue->valueList[seqIdx].valid = false;
            free(queue->valueList[seqIdx].data);
            queue->valueList[seqIdx].data = nullptr;
            queue->negoSeqNo++;
            continue;
        }
        break;
    }

    if (queue->commitSeqNo < seqNo) { queue->commitSeqNo = seqNo; }
    return;
}

BResult MmsSequence::GetSeqNoList2Slv(uint32_t lev1Id, uint32_t lev2Id, uint64_t *seqNoList, uint32_t &seqNum)
{
    if (!mSequence) {
        seqNum = 0;
        return MMS_OK;
    }

    if (UNLIKELY(lev1Id >= mLev1Cap || lev2Id >= mLev2Cap)) {
        return MMS_ERR;
    }

    seqNum = 0;

    NegoQueue *queue = mNegoQueue[lev1Id][lev2Id];
    queue->lock.Lock();
    for (uint64_t seqNo = queue->negoSeqNo; seqNo < queue->commitSeqNo; seqNo++) {
        uint64_t seqIdx = (queue->negoSeqNo + NO_1) & SEQ_QUEUE_MASK;
        if (queue->valueList[seqIdx].valid) {
            seqNoList[seqNum] = queue->valueList[seqIdx].seqNo;
            seqNum++;
        }
    }
    queue->lock.UnLock();
    return MMS_OK;
}

BResult MmsSequence::GetSeqNoData2Slv(uint32_t lev1Id, uint32_t lev2Id, uint64_t seqNo, void *data, uint32_t &len)
{
    if (!mSequence) {
        return MMS_ERR;
    }

    if (UNLIKELY(lev1Id >= mLev1Cap || lev2Id >= mLev2Cap)) {
        return MMS_ERR;
    }

    NegoQueue *queue = mNegoQueue[lev1Id][lev2Id];
    queue->lock.Lock();
    uint64_t seqIdx = seqNo & SEQ_QUEUE_MASK;
    if (LIKELY(queue->valueList[seqIdx].valid)) {
        if (UNLIKELY(memcpy_s(data, len, queue->valueList[seqIdx].data, queue->valueList[seqIdx].len) != MMS_OK)) {
            queue->lock.UnLock();
            return MMS_ERR;
        }
        len = queue->valueList[seqIdx].len;
        queue->lock.UnLock();
        return MMS_OK;
    }
    queue->lock.UnLock();
    return MMS_NOT_EXISTS;
}
}
}

