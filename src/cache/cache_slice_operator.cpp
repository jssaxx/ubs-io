/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "cache_slice_operator.h"
#include "bdm_core.h"
#include "bio_trace.h"

namespace ock {
namespace bio {
BResult CacheSliceOperator::Copy(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(Validate(from, to), BIO_INVALID_PARAM);

    // have some address, don't need copy.
    if (from->IsTheSameWith(to)) {
        return BIO_OK;
    }

    if (from->GetFlowType() == FLOW_DISK) {
        if (to->GetFlowType() == FLOW_DISK) {
            // copy from disk to disk.
            return CopyFromDiskToDisk(from, to);
        }
        // copy from disk to memory.
        return CopyFromDiskToMemory(from, to);
    }

    if (to->GetFlowType() == FLOW_DISK) {
        // copy from memory to disk.
        return CopyFromMemoryToDisk(from, to);
    }
    // copy from memory to memory.
    return CopyFromMemoryToMemory(from, to);
}

BResult CacheSliceOperator::Copy(const char *from, const SlicePtr &to)
{
    ChkTrueNot(Validate(to), BIO_INVALID_PARAM);
    ChkTrueNot(from != nullptr, BIO_INVALID_PARAM);

    if (to->GetFlowType() == FLOW_MEMORY) {
        auto &toAddrs = to->GetAddrs();
        uint64_t offset = 0;
        for (auto toAddr : toAddrs) {
            auto ret = memcpy_s(reinterpret_cast<void *>(toAddr.chunkId + toAddr.chunkOffset), toAddr.chunkLen,
                reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen);
            ChkTrue(ret == BIO_OK, ret,
                "Failed to copy data from memory address:" << from + offset << " to memory address:" <<
                toAddr.chunkId + toAddr.chunkOffset << " by length:" << toAddr.chunkLen);
            offset += toAddr.chunkLen;
        }
        return BIO_OK;
    } else {
        auto &toAddrs = to->GetAddrs();
        uint64_t offset = 0;
        for (auto toAddr : toAddrs) {
            BIO_TRACE_START(BDM_TRACE_WRITE_SYNC);
            auto ret = BdmWrite(toAddr.chunkId, toAddr.chunkOffset,
                reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen);
            BIO_TRACE_END(BDM_TRACE_WRITE_SYNC, ret);
            ChkTrue(ret == BIO_OK, ret,
                "Failed to copy data from memory address:" << from + offset << " to disk address:" <<
                toAddr.chunkId + toAddr.chunkOffset << " by length:" << toAddr.chunkLen);
            offset += toAddr.chunkLen;
        }
        return BIO_OK;
    }
}

BResult CacheSliceOperator::Copy(const SlicePtr &from, char *to)
{
    ChkTrueNot(Validate(from), BIO_INVALID_PARAM);
    ChkTrueNot(to != nullptr, BIO_INVALID_PARAM);

    if (from->GetFlowType() == FLOW_MEMORY) {
        auto &fromAddrs = from->GetAddrs();
        uint64_t offset = 0;
        for (auto fromAddr : fromAddrs) {
            auto ret = memcpy_s(reinterpret_cast<void *>(const_cast<char *>(to + offset)), fromAddr.chunkLen,
                reinterpret_cast<void *>(fromAddr.chunkId + fromAddr.chunkOffset), fromAddr.chunkLen);
            ChkTrue(ret == BIO_OK, ret, "Failed to copy data from memory address:" << (fromAddr.chunkId +
                    fromAddr.chunkOffset) << " to memory address:" << to + offset << " by length:" <<
                    fromAddr.chunkLen);
            offset += fromAddr.chunkLen;
        }
        return BIO_OK;
    } else {
        auto &fromAddrs = from->GetAddrs();
        uint64_t offset = 0;
        for (auto fromAddr : fromAddrs) {
            LOG_DEBUG("Copy data from disk:" << ", from off:" << fromAddr.chunkOffset << ", to off:" << offset);
            BIO_TRACE_START(BDM_TRACE_READ_SYNC);
            auto ret =
                BdmRead(fromAddr.chunkId, fromAddr.chunkOffset, reinterpret_cast<void *>(to + offset), fromAddr.chunkLen);
            BIO_TRACE_END(BDM_TRACE_READ_SYNC, ret);
            ChkTrue(ret == BIO_OK, ret, "Failed to copy data from disk address:" << (fromAddr.chunkId +
                    fromAddr.chunkOffset) << " to memory address:" << to + offset << " by length:" << fromAddr.chunkLen);
            offset += fromAddr.chunkLen;
        }
        return BIO_OK;
    }
}

bool CacheSliceOperator::Validate(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);

    ChkTrue(from->GetLength() == to->GetLength(), false,
        "Failed to validate slices, src slice length:" << from->GetLength() << " dst slice length:" << to->GetLength());

    return true;
}

bool CacheSliceOperator::Validate(const SlicePtr &slice)
{
    ChkTrueNot(slice != nullptr, false);
    return true;
}

BResult CacheSliceOperator::CopyFromDiskToDisk(const SlicePtr &from, const SlicePtr &to)
{
    // TODO: implement me.
    LOG_ERROR("don't support copy from disk to disk.");
    return BIO_ERR;
}

BResult CacheSliceOperator::CopyFromDiskToMemory(const SlicePtr &from, const SlicePtr &to)
{
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();

    auto fromIt = fromAddrs.begin();
    auto toIt = toAddrs.begin();

    uint64_t fromOffset = 0;
    uint64_t toOffset = 0;

    uint64_t len;
    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        BIO_TRACE_START(BDM_TRACE_READ_SYNC);
        auto ret = BdmRead(fromIt->chunkId, fromIt->chunkOffset + fromOffset,
            reinterpret_cast<void *>(toIt->chunkId + toIt->chunkOffset + toOffset), len);
        BIO_TRACE_END(BDM_TRACE_READ_SYNC, ret);
        ChkTrue(ret == BIO_OK, ret,
                "Failed to copy data from disk address:" << fromIt->chunkId + fromIt->chunkOffset + fromOffset
                << " to memory address:" << toIt->chunkId + toIt->chunkOffset + toOffset << " by length:" << len);
        fromOffset += len;
        if (fromOffset == fromIt->chunkLen) {
            fromOffset = 0;
            fromIt++;
        }
        toOffset += len;
        if (toOffset == toIt->chunkLen) {
            toOffset = 0;
            toIt++;
        }
    }
    return BIO_OK;
}

BResult CacheSliceOperator::CopyFromMemoryToDisk(const SlicePtr &from, const SlicePtr &to)
{
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();

    auto fromIt = fromAddrs.begin();
    auto toIt = toAddrs.begin();

    uint64_t fromOffset = 0;
    uint64_t toOffset = 0;

    uint64_t len;
    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        BIO_TRACE_START(BDM_TRACE_WRITE_SYNC);
        auto ret = BdmWrite(toIt->chunkId, toIt->chunkOffset + toOffset,
            reinterpret_cast<void *>(fromIt->chunkId + fromIt->chunkOffset + fromOffset), len);
        BIO_TRACE_END(BDM_TRACE_WRITE_SYNC, ret);
        LOG_DEBUG("Copy data:" << "from disk chunk:" << fromIt->chunkOffset << ", from off:" << fromOffset <<
                  ", to off:" << toOffset << ", len:" << len);
        ChkTrue(ret == BIO_OK, ret,
            "Failed to copy data from memory address:" << fromIt->chunkId + fromIt->chunkOffset + fromOffset <<
            " to disk address:" << toIt->chunkId + toIt->chunkOffset + toOffset << " by length:" << len);
        fromOffset += len;
        if (fromOffset == fromIt->chunkLen) {
            fromOffset = 0;
            fromIt++;
        }
        toOffset += len;
        if (toOffset == toIt->chunkLen) {
            toOffset = 0;
            toIt++;
        }
        LOG_DEBUG("next:" << ", from off:" << fromOffset << ", to off:" << toOffset);
    }
    return BIO_OK;
}

BResult CacheSliceOperator::CopyFromMemoryToMemory(const SlicePtr &from, const SlicePtr &to)
{
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();

    auto fromIt = fromAddrs.begin();
    auto toIt = toAddrs.begin();

    uint64_t fromOffset = 0;
    uint64_t toOffset = 0;

    uint64_t len;
    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        auto ret = memcpy_s(reinterpret_cast<void *>(toIt->chunkId + toIt->chunkOffset + toOffset), len,
            reinterpret_cast<void *>(fromIt->chunkId + fromIt->chunkOffset + fromOffset), len);
        LOG_DEBUG("Copy data:" << "from memory chunk:" << fromIt->chunkOffset << ", from off:" << fromOffset <<
            ", to off:" << toOffset << ", len:" << len);
        ChkTrue(ret == BIO_OK, ret,
            "Failed to copy data from memory address:" << fromIt->chunkId + fromIt->chunkOffset + fromOffset <<
            " to  memory address:" << toIt->chunkId + toIt->chunkOffset + toOffset << " by length:" << len);
        fromOffset += len;
        if (fromOffset == fromIt->chunkLen) {
            fromOffset = 0;
            fromIt++;
        }
        toOffset += len;
        if (toOffset == toIt->chunkLen) {
            toOffset = 0;
            toIt++;
        }
        LOG_DEBUG("next:" << ", from off:" << fromOffset << ", to off:" << toOffset);
    }
    return BIO_OK;
}

uint64_t CacheSliceOperator::MinLen(uint64_t from, uint64_t to)
{
    return (from < to) ? from : to;
}
}
}
