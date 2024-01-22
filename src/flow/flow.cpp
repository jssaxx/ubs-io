/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#include "flow.h"
#include "flow_manager.h"
#include "bio_types.h"

namespace ock {
namespace bio {
BResult Flow::GetAddrByOffset(uint64_t offset, uint32_t len, std::vector<FlowAddr> &flowAddr)
{
    LOG_INFO("Flow:" << mFlowId << ", type:" << mType << ", offset:" << offset << ", len:" << len);

    if (offset < mTruncateOffset) {
        LOG_ERROR("Invalid offset:" << offset << ", flowId:" << mFlowId << ", truncate:" << mTruncateOffset);
        return BIO_ERR;
    }

    if (offset + len >= mPreLoadOffset) {
        auto ret = HoldWait(offset + len);
        if (ret != BIO_OK) {
            return ret;
        }
    }

    {
        WriteLocker<ReadWriteLock> lock(&mLock);
        if (mWritenOffset < offset + len) {
            mWritenOffset = offset + len;
        }
    }

    mLock.LockRead();
    uint64_t remainLen = len;
    uint64_t curOffset = offset - (mTruncateOffset / mChunkSize * mChunkSize);
    uint64_t curLen;
    while (remainLen > 0) {
        curLen = mChunkSize - curOffset % mChunkSize;
        curLen = (remainLen > curLen) ? curLen : remainLen;
        flowAddr.push_back(FlowAddr(mChunkList[curOffset / mChunkSize], curOffset % mChunkSize, curLen));
        LOG_INFO("ChunkId:" << mChunkList[curOffset / mChunkSize] << ", chunkOffset:" << curOffset % mChunkSize << ", curLen:" << curLen);
        curOffset += curLen;
        remainLen -= curLen;
    }
    mLock.UnLock();

    PreLoadSchedule();
    return BIO_OK;
}

BResult Flow::TruncateOffset(uint64_t offset)
{
    std::vector<uint64_t> cleanList;

    LOG_INFO("Flow:" << mFlowId << ", type:" << mType << ", truncate:" << offset);

    if (offset >= mPreLoadOffset || offset > mWritenOffset) {
        LOG_ERROR("Invalid offset:" << offset << ", preLoad:" << mPreLoadOffset << ", writen:" << mWritenOffset);
        return BIO_ERR;
    }

    mLock.LockWrite();
    if (mTruncateOffset >= offset) {
        mLock.UnLock();
        return BIO_OK;
    }
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
        FlowManager::MediaFree(mType, mMediaId, mChunkSize, chunkId);
    }
    return BIO_OK;
}

BResult Flow::Seal()
{
    return BIO_OK;
}

void Flow::PreLoadHandle()
{
    uint64_t chunkId;
    uint64_t offset;
    bool isReady;
    do {
        auto ret = FlowManager::MediaAlloc(mType, mMediaId, mFlowId, mChunkSize, &chunkId);
        if (ret != BIO_OK) {
            LOG_ERROR("Media alloc failed:" << ret << ", type:" << mType << ", mediaId:" <<
                mMediaId << ", chunkSize:" << mChunkSize);
            HoldClean(NO_MAX_VALUE64, BIO_ERR, false);
            break;
        }

        mLock.LockWrite();
        mChunkList.push_back(chunkId);
        mPreLoadOffset += mChunkSize;
        isReady = (mPreLoadOffset >= mWritenOffset + mPreLoadSize);
        offset = mPreLoadOffset;
        mLock.UnLock();
        HoldClean(offset, BIO_OK, !isReady);
    } while (!isReady);
    return;
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
    if (mPreLoadFlag == false) {
        mPreLoadFlag = true;
    }
    mLock.UnLock();

    if (preloadFlag == true) {
        LOG_INFO("PreLoadSchedule: not ready:" << mType);
        return;
    }

    std::function<void()> func = std::bind(&Flow::PreLoadHandle, this);
    auto ret = FlowManager::Instance()->PreLoadObject(func);
    if (ret != BIO_OK) {
        LOG_ERROR("Preload failed:" << ret << ", flowId:" << mFlowId);
        mPreLoadFlag = false;
        return;
    }
    return;
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
    return;
}

BResult Flow::HoldWait(uint64_t needOffset)
{
    IoHoldCtx ioHoldCtx;

    sem_init(&ioHoldCtx.sem, 0, 0);
    ioHoldCtx.needOffset = needOffset;

    mLock.LockWrite();
    if (mWritenOffset < needOffset) {
        mWritenOffset = needOffset;
    }
    if (mPreLoadOffset > needOffset) {
        mLock.UnLock();
        return BIO_OK;
    }
    mHoldList.push_back(&ioHoldCtx);
    mLock.UnLock();

    PreLoadSchedule();

    sem_wait(&ioHoldCtx.sem);
    sem_destroy(&ioHoldCtx.sem);

    return ioHoldCtx.ret;
}

BResult Flow::RecoverChunk(uint64_t offset, uint64_t chunkId)
{
    if (mRecoverList.find(offset) != mRecoverList.end()) {
        return BIO_ERR;
    }
    mRecoverList[offset] = chunkId;
    return BIO_OK;
}

BResult Flow::RecoverCheck()
{
    return BIO_OK;
}
}
}
