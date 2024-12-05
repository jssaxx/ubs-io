/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#include "flow.h"
#include "flow_manager.h"
#include "bio_types.h"
#include "bio_tracepoint_helper.h"
#include "bio_trace.h"


namespace ock {
namespace bio {
BResult Flow::GetAddrByOffset(uint64_t offset, uint32_t len, std::vector<FlowAddr> &flowAddr)
{
    LOG_TRACE("Flow:" << mFlowId << ", type:" << mType << ", offset:" << offset << ", len:" << len);
    bool isInvalidRange = false;
    LVOS_TP_START(WCACHE_FLOW_OFFSET_FAIL, &isInvalidRange, true);
    isInvalidRange = (offset < mTruncateOffset);
    LVOS_TP_END;
    if (UNLIKELY(isInvalidRange)) {
        LOG_ERROR("Invalid offset:" << offset << ", flowId:" << mFlowId << ", truncate:" << mTruncateOffset);
        return BIO_ERR;
    }

    if (UINT64_MAX - offset < len) {
        LOG_ERROR("Invalid offset:" << offset << ", flowId:" << mFlowId << ", len:" << len);
        return BIO_ERR;
    }
    if (offset + len > mPreLoadOffset) {
        BIO_TRACE_START(FLOW_TRACE_PRELOAD_MEMORY);
        BResult ret = BIO_INNER_ERR;
        LVOS_TP_START(WCACHE_HOLD_WAIT_FAIL, &ret, BIO_ERR);
        ret = HoldWait(offset + len);
        LVOS_TP_END;
        if (ret != BIO_OK) {
            BIO_TRACE_END(FLOW_TRACE_PRELOAD_MEMORY, ret);
            return ret;
        }
        BIO_TRACE_END(FLOW_TRACE_PRELOAD_MEMORY, 0);
    }

    {
        WriteLocker<ReadWriteLock> lock(&mLock);
        if (mWritenOffset < offset + len) {
            mWritenOffset = offset + len;
        }
    }
    BIO_TRACE_START(FLOW_TRACE_GETADDR);
    mLock.LockRead();
    uint64_t remainLen = len;
    uint64_t curOffset = offset - (mTruncateOffset / mChunkSize * mChunkSize);
    uint64_t curLen;
    while (remainLen > 0) {
        curLen = mChunkSize - curOffset % mChunkSize;
        curLen = (remainLen > curLen) ? curLen : remainLen;
        flowAddr.emplace_back(mChunkList[curOffset / mChunkSize], curOffset % mChunkSize, curLen);
        curOffset += curLen;
        remainLen -= curLen;
    }
    mLock.UnLock();
    BIO_TRACE_END(FLOW_TRACE_GETADDR, 0);

    PreLoadSchedule();
    return BIO_OK;
}

BResult Flow::TruncateOffset(uint64_t offset)
{
    std::vector<uint64_t> cleanList;

    LOG_DEBUG("Flow truncate offset, Flow:" << mFlowId << ", type:" << mType << ", truncate:" << offset);

    if (offset > mPreLoadOffset || offset > mWritenOffset) {
        LOG_ERROR("Invalid offset:" << offset << ", preLoad:" << mPreLoadOffset << ", writen:" << mWritenOffset);
        return BIO_ERR;
    }

    mLock.LockWrite();
    if (mTruncateOffset >= offset) {
        mLock.UnLock();
        return BIO_OK;
    }

    BIO_TRACE_START(FLOW_TRACE_TRUNCATE);
    uint64_t startOffset = mTruncateOffset / mChunkSize * mChunkSize;
    while (startOffset + mChunkSize <= offset && mChunkList.size() != 0) {
        cleanList.push_back(mChunkList[0]);
        mChunkList.erase(mChunkList.begin());
        startOffset += mChunkSize;
    }
    mTruncateOffset = offset;
    mLock.UnLock();

    for (uint32_t i = 0; i < cleanList.size(); i++) {
        uint64_t chunkId = cleanList[i];
        FlowManager::MediaFree(mRole, mType, mMediaId, mChunkSize, chunkId, mFlowId);
    }
    BIO_TRACE_END(FLOW_TRACE_TRUNCATE, 0);
    return BIO_OK;
}

BResult Flow::Seal()
{
    BResult ret = BIO_OK;
    LVOS_TP_START(FLOW_SEAL_ERR, &ret, BIO_ERR);
    if (mSealed) {
        return ret;
    }
    mSealed = true;

    LOG_INFO("Seal flow:" << mFlowId);

    BIO_TRACE_START(FLOW_TRACE_SEAL);
    uint64_t writenOffset = mPreLoadOffset;
    mWritenOffset = writenOffset;
    ret = TruncateOffset(mWritenOffset);
    if (ret != BIO_OK) {
        LOG_ERROR("Truncate offset failed, ret " << ret);
    }
    BIO_TRACE_END(FLOW_TRACE_SEAL, 0);
    LVOS_TP_END;
    LVOS_TP_START(FLOW_SEAL_OK, &ret, BIO_OK);
    LVOS_TP_END;
    return ret;
}

void Flow::PreLoadHandle()
{
    uint64_t chunkId;
    uint64_t offset;
    bool isReady;
    do {
        BIO_TRACE_START(FLOW_TRACE_PRELOAD_ALLOC);
        auto ret = FlowManager::MediaAlloc(mType, mMediaId, mFlowId, mPreLoadOffset, mChunkSize, &chunkId);
        if (ret != BIO_OK) {
            LOG_ERROR("Media alloc failed:" << ret << ", type:" << mType << ", mediaId:" << mMediaId <<
                ", chunkSize:" << mChunkSize);
            HoldClean(NO_MAX_VALUE64, BIO_INNER_RETRY, false);
            BIO_TRACE_END(FLOW_TRACE_PRELOAD_ALLOC, ret);
            break;
        }
        BIO_TRACE_END(FLOW_TRACE_PRELOAD_ALLOC, ret);
        mLock.LockWrite();
        mChunkList.push_back(chunkId);
        mPreLoadOffset += mChunkSize;
        isReady = (mPreLoadOffset >= mWritenOffset + mPreLoadSize);
        offset = mPreLoadOffset;
        mLock.UnLock();
        HoldClean(offset, BIO_OK, !isReady);
    } while (!isReady);
}

void Flow::PreLoadSchedule()
{
    mLock.LockRead();
    if (mPreLoadOffset >= mWritenOffset + mPreLoadSize) {
        mLock.UnLock();
        return;
    }
    mLock.UnLock();

    mLock.LockWrite();
    bool preloadFlag = mPreLoadFlag;
    if (!mPreLoadFlag) {
        mPreLoadFlag = true;
    }
    mLock.UnLock();

    if (preloadFlag) {
        LOG_DEBUG("PreLoadSchedule: not ready:" << mType << ", Flow:" << mFlowId);
        return;
    }
    std::function<void()> func = std::bind(&Flow::PreLoadHandle, this);
    auto ret = FlowManager::Instance()->PreLoadObject(mType, func);
    if (ret != BIO_OK) {
        LOG_ERROR("Preload failed:" << ret << ", flowId:" << mFlowId);
        mPreLoadFlag = false;
        return;
    }
}

void Flow::HoldClean(uint64_t realOffset, int32_t ret, bool preLoadFlag)
{
    IoHoldCtx *waitCtx = nullptr;
    mLock.LockWrite();
    mPreLoadFlag = preLoadFlag;
    while (!mHoldList.empty()) {
        waitCtx = mHoldList.front();
        if (waitCtx->needOffset <= realOffset) {
            mHoldList.pop_front();
            waitCtx->ret = ret;
            sem_post(&waitCtx->sem);
        } else {
            break;
        }
    }
    mLock.UnLock();
}

BResult Flow::HoldWait(uint64_t needOffset)
{
    IoHoldCtx ioHoldCtx;

    BIO_TRACE_START(FLOW_TRACE_HOLDWAIT);
    sem_init(&ioHoldCtx.sem, 0, 0);
    ioHoldCtx.needOffset = needOffset;

    mLock.LockWrite();
    if (mWritenOffset < needOffset) {
        mWritenOffset = needOffset;
    }
    if (mPreLoadOffset >= needOffset) {
        mLock.UnLock();
        BIO_TRACE_END(FLOW_TRACE_HOLDWAIT, 0);
        return BIO_OK;
    }
    mHoldList.push_back(&ioHoldCtx);
    mLock.UnLock();
    PreLoadSchedule();

    sem_wait(&ioHoldCtx.sem);
    sem_destroy(&ioHoldCtx.sem);
    BIO_TRACE_END(FLOW_TRACE_HOLDWAIT, 0);

    return ioHoldCtx.ret;
}

BResult Flow::RecoverChunk(uint64_t offset, uint64_t chunkId)
{
    if (mRecoverList.find(offset) != mRecoverList.end()) {
        LOG_ERROR("Repeat confict, flowId:" << mFlowId << ", flowOffset:" << offset << ".");
        return BIO_ERR;
    }
    LOG_TRACE("Recover chunk: flowId:" << mFlowId << ", flowOffset:" << offset << ".");
    mRecoverList[offset] = chunkId;
    return BIO_OK;
}

BResult Flow::RecoverCheck()
{
    if (mRecoverList.empty()) {
        return BIO_OK;
    }
    bool isFirst = true;
    for (auto &elem : mRecoverList) {
        mChunkList.push_back(elem.second);
        if (isFirst) {
            mTruncateOffset = elem.first;
            mPreLoadOffset = elem.first + mChunkSize;
            isFirst = false;
            continue;
        }
        if (mPreLoadOffset != elem.first) {
            LOG_ERROR("Recover check fail, preLoad:" << mPreLoadOffset << ", current offset:" << elem.first);
            return BIO_ERR;
        }
        mPreLoadOffset = elem.first + mChunkSize;
    }
    LOG_INFO("Recover succeed, flowId:" << mFlowId << ", truncate:" << mTruncateOffset << ", preLoad:" <<
        mPreLoadOffset);
    return BIO_OK;
}
}
}
