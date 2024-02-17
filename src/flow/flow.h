/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#ifndef BOOSTIO_BIO_FLOW_H
#define BOOSTIO_BIO_FLOW_H

#include <memory>
#include <atomic>
#include <vector>
#include <list>
#include <map>
#include <functional>
#include <semaphore.h>

#include "bio_ref.h"
#include "bio_err.h"
#include "bio_log.h"
#include "bio_lock.h"
#include "slice.h"
#include "flow_type.h"

namespace ock {
namespace bio {
struct IoHoldCtx {
    uint64_t needOffset;
    sem_t sem;
    int32_t ret;
};

class FlowManager;

class Flow;
using FlowPtr = Ref<Flow>;
class Flow {
public:
    Flow(FlowType type, uint64_t flowId, uint32_t mediaId, uint64_t chunkSize, uint64_t preLoadSize) : mType(type),
        mFlowId(flowId), mMediaId(mediaId), mChunkSize(chunkSize), mPreLoadSize(preLoadSize) {}
    ~Flow() = default;

    BResult GetAddrByOffset(uint64_t offset, uint32_t len, std::vector<FlowAddr> &flowAddr);

    BResult TruncateOffset(uint64_t offset);

    BResult Seal();

    inline FlowType GetFlowType()
    {
        return mType;
    }

    inline uint64_t GetFlowId()
    {
        return mFlowId;
    }

    inline uint64_t GetValidLen()
    {
        return mWritenOffset - mTruncateOffset;
    }

    inline uint64_t GetTotalLen()
    {
        return mPreLoadOffset - mTruncateOffset;
    }

    inline uint64_t GetTruncateOffset()
    {
        return mTruncateOffset;
    }

    inline void SetWrittenOffset(uint64_t writtenOffset)
    {
        mWritenOffset = writtenOffset;
        return;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult RecoverChunk(uint64_t offset, uint64_t chunkId);
    BResult RecoverCheck();
    void PreLoadHandle();
    void PreLoadSchedule();
    void HoldClean(uint64_t realOffset, int32_t ret, bool preLoadFlag);
    BResult HoldWait(uint64_t offset);

private:
    FlowType mType;
    uint64_t mFlowId;
    uint32_t mMediaId;
    uint64_t mChunkSize;
    uint64_t mPreLoadSize;

    ReadWriteLock mLock;

    std::vector<uint64_t> mChunkList;

    std::atomic<uint64_t> mTruncateOffset { 0 };
    std::atomic<uint64_t> mWritenOffset { 0 };
    std::atomic<uint64_t> mPreLoadOffset { 0 };

    bool mPreLoadFlag { false };
    std::list<IoHoldCtx *> mHoldList;

    std::map<uint64_t, uint64_t> mRecoverList;
    friend class FlowManager;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
