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

#ifndef MMS_SEQUENCE_H
#define MMS_SEQUENCE_H

#include <vector>
#include <stdint.h>
#include <sys/types.h>
#include "securec.h"
#include "mms_err.h"
#include "mms_ref.h"
#include "mms_types.h"
#include "mms_lock.h"
#include "mms_monotonic.h"

namespace ock {
namespace mms {

struct SeqCursor {
    uint64_t left = 0; // 代表：当前协商淘汰的SEQNO
    uint64_t right = 0; // 代表：当前已分配的SEQNO
};

struct SeqStatus {
    bool finish = false;
};

struct SeqValue {
    volatile bool valid = false;
    uint64_t seqNo;
    void *data;
    uint32_t len;
};

struct SlidingQueue {
    SpinLock lock;
    SeqCursor cursor;
    SeqStatus statusList[SEQ_QUEUE_LEN];
};

struct NegoQueue {
    SpinLock lock;
    SeqValue valueList[SEQ_QUEUE_LEN];
    uint64_t negoSeqNo = 0;
    uint64_t commitSeqNo = 0;
};

class MmsSequence;
using MmsSequencePtr = Ref<MmsSequence>;
class MmsSequence {
public:
    MmsSequence() = default;
    ~MmsSequence() = default;

    BResult Initialize(uint32_t lev1Cap, uint32_t lev2Cap);

    void Exit();

    inline static MmsSequencePtr &Instance()
    {
        static auto instance = MakeRef<MmsSequence>();
        return instance;
    }

    inline void SetEnable(bool sequence)
    {
        mSequence = sequence;
    }

    BResult ResetSeqNoState2Mst(uint32_t lev1Id, uint32_t lev2Id, uint64_t seqNo);

    BResult ApplyForSeqNo2Mst(uint32_t lev1Id, uint32_t lev2Id, uint64_t &seqNo, uint64_t &negoSeqNo);

    BResult ReleaseSeqNo2Mst(uint32_t lev1Id, uint32_t lev2Id, uint64_t seqNo);

    BResult NegoSeqNo2Slv(uint32_t lev1Id, uint32_t lev2Id, uint64_t seqNo, void *data, uint32_t len,
                          uint64_t negoSeqNo);

    BResult GetSeqNoList2Slv(uint32_t lev1Id, uint32_t lev2Id, uint64_t *seqNoList, uint32_t &seqNum);

    BResult GetSeqNoData2Slv(uint32_t lev1Id, uint32_t lev2Id, uint64_t seqNo, void *data, uint32_t &len);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    void NegoSeqNoHandle(NegoQueue *queue, uint64_t seqNo, uint64_t negoSeqNo);
    void FreeNegoData();

private:
    std::vector<std::vector<SlidingQueue *>> mSlidingQueue;
    std::vector<std::vector<NegoQueue *>> mNegoQueue;
    SlidingQueue *mSlidingBuff = nullptr;
    NegoQueue *mNegoBuff = nullptr;
    uint32_t mLev1Cap = 0;
    uint32_t mLev2Cap = 0;

    bool mSequence = false;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
