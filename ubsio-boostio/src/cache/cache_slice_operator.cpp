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

#include "cache_slice_operator.h"
#include "bdm_core.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"

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
    BResult ret = BIO_INNER_ERR;

    if (to->GetFlowType() == FLOW_MEMORY) {
        auto &toAddrs = to->GetAddrs();
        uint64_t offset = 0;
        for (auto toAddr : toAddrs) {
            BIO_TP_START(SLICE_OPERATOR_FLOW_MEMORY, &ret, BIO_ERR);
            ret = memcpy_s(reinterpret_cast<void *>(toAddr.chunkId + toAddr.chunkOffset), toAddr.chunkLen,
                           reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen);
            BIO_TP_END;
            ChkTrue(ret == BIO_OK, ret, "Failed to copy data, length:" << toAddr.chunkLen);
            offset += toAddr.chunkLen;
        }
        return BIO_OK;
    } else {
        auto &toAddrs = to->GetAddrs();
        uint64_t offset = 0;
        for (auto toAddr : toAddrs) {
            BIO_TRACE_START(BDM_TRACE_WRITE_SYNC);
            ret = BdmWrite(toAddr.chunkId, toAddr.chunkOffset,
                           reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen);
            ret = (ret == BIO_OK) ? BIO_OK : BIO_DISK_IOERR;
            BIO_TRACE_END(BDM_TRACE_WRITE_SYNC, ret);
            ChkTrue(ret == BIO_OK, ret, "Failed to copy length:" << toAddr.chunkLen);
            offset += toAddr.chunkLen;
        }
        return BIO_OK;
    }
}

BResult CacheSliceOperator::Copy(const char *from, uint64_t start, uint32_t len, const SlicePtr &to)
{
    ChkTrueNot(Validate(to), BIO_INVALID_PARAM);
    ChkTrueNot(from != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(to->GetFlowType() == FLOW_MEMORY, BIO_INVALID_PARAM);

    auto &toAddrs = to->GetAddrs();
    uint64_t offset1 = start;
    uint64_t offset2 = 0;
    uint32_t len1 = len;
    uint64_t offset = 0;
    for (auto toAddr : toAddrs) {
        if (offset + toAddr.chunkLen <= offset1) {
            offset += toAddr.chunkLen;
            continue;
        }
        uint64_t boff = offset1 - offset;
        uint64_t blen = toAddr.chunkLen - boff;
        blen = (len1 > blen) ? blen : len1;
        if (blen == 0) {
            break;
        }
        len1 -= blen;
        int32_t ret = BIO_INNER_ERR;
        BIO_TP_START(SLICE_OPERATOR_4_FLOW_MEMORY, &ret, BIO_ERR);
        ret = memcpy_s(reinterpret_cast<void *>(toAddr.chunkId + toAddr.chunkOffset + boff), blen,
                       reinterpret_cast<void *>(const_cast<char *>(from + offset2)), blen);
        BIO_TP_END;
        ChkTrue(ret == BIO_OK, ret, "Failed to copy data, length:" << blen << ".");
        offset += toAddr.chunkLen;
        offset1 = offset;
        offset2 += blen;
    }
    return BIO_OK;
}

BResult CacheSliceOperator::Copy(const SlicePtr &from, char *to, uint32_t toLen)
{
    ChkTrueNot(Validate(from), BIO_INVALID_PARAM);
    ChkTrueNot(to != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(toLen != 0, BIO_INVALID_PARAM);
    BResult ret = BIO_INNER_ERR;
    uint64_t cpyLength = toLen;

    if (from->GetFlowType() == FLOW_MEMORY) {
        auto &fromAddrs = from->GetAddrs();
        uint64_t offset = 0;
        for (auto fromAddr : fromAddrs) {
            BIO_TP_START(SLICE_OPERATOR_2_FLOW_MEMORY, &ret, BIO_ERR);
            ret = memcpy_s(reinterpret_cast<void *>(const_cast<char *>(to + offset)), cpyLength,
                           reinterpret_cast<void *>(fromAddr.chunkId + fromAddr.chunkOffset), fromAddr.chunkLen);
            BIO_TP_END;
            ChkTrue(ret == BIO_OK, ret, "Failed to copy data, length:" << fromAddr.chunkLen << ".");
            offset += fromAddr.chunkLen;
            cpyLength -= fromAddr.chunkLen;
        }
        return BIO_OK;
    } else {
        auto &fromAddrs = from->GetAddrs();
        uint64_t offset = 0;
        for (auto fromAddr : fromAddrs) {
            LOG_TRACE("Copy data from disk: from off:" << fromAddr.chunkOffset << ", to off:" << offset);
            BIO_TRACE_START(BDM_TRACE_READ_SYNC);
            ret = BdmRead(fromAddr.chunkId, fromAddr.chunkOffset, reinterpret_cast<void *>(to + offset),
                          fromAddr.chunkLen);
            ret = (ret == BIO_OK) ? BIO_OK : BIO_DISK_IOERR;
            BIO_TRACE_END(BDM_TRACE_READ_SYNC, ret);
            ChkTrue(ret == BIO_OK, ret,
                    "Failed to copy data from disk chunkId:" << (fromAddr.chunkId + fromAddr.chunkOffset)
                                                             << " to memory by length:" << fromAddr.chunkLen << ".");
            offset += fromAddr.chunkLen;
        }
        return BIO_OK;
    }
}

BResult CacheSliceOperator::GetSliceFromSliceIO(SlicePtr &partialSlice, const SlicePtr &wholeSlice, uint64_t offset,
                                                uint64_t length)
{
    ChkTrueNot(Validate(wholeSlice), BIO_INVALID_PARAM);
    std::vector<FlowAddr> flowAddr;
    std::vector<FlowAddr> addrVec = wholeSlice->GetAddrs();
    uint64_t totalLength = length;
    for (auto addr : addrVec) {
        if (offset >= addr.chunkLen) {
            offset -= addr.chunkLen;
            continue;
        }

        uint64_t size = addr.chunkLen;
        uint32_t chunkOffset = addr.chunkOffset;
        uint32_t chunkLen = addr.chunkLen;
        if (offset != 0) {
            chunkOffset += offset;
            chunkLen = length > chunkLen - offset ? chunkLen - offset : length;
            offset = 0;
        } else {
            chunkLen = length > chunkLen ? chunkLen : length;
        }
        flowAddr.emplace_back(addr.chunkId, chunkOffset, chunkLen);
        length -= chunkLen;
        if (length == 0) {
            break;
        }
    }
    partialSlice = MakeRef<Slice>(totalLength, flowAddr, wholeSlice->GetFlowType());
    if (UNLIKELY(partialSlice == nullptr)) {
        LOG_ERROR("Alloc slice memory for read cache failed.");
        return BIO_ERR;
    }
    return BIO_OK;
}

bool CacheSliceOperator::Validate(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);

    ChkTrue(from->GetLength() == to->GetLength(), false,
            "Failed to validate slices, src slice length:" << from->GetLength()
                                                           << " dst slice length:" << to->GetLength());

    return true;
}

bool CacheSliceOperator::Validate(const SlicePtr &slice)
{
    ChkTrueNot(slice != nullptr, false);
    return true;
}

BResult CacheSliceOperator::CopyFromDiskToDisk(const SlicePtr &from, const SlicePtr &to)
{
    LOG_ERROR("don't support copy from disk to disk.");
    return BIO_ERR;
}

BResult CacheSliceOperator::CopyFromDiskToMemory(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);
    BResult ret = BIO_INNER_ERR;
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
        BIO_TP_START(SLICE_COPY_DISK2MEMORY_OK, &ret, BIO_OK);
        ret = BdmRead(fromIt->chunkId, fromIt->chunkOffset + fromOffset,
                      reinterpret_cast<void *>(toIt->chunkId + toIt->chunkOffset + toOffset), len);
        BIO_TP_END;
        ret = (ret == BIO_OK) ? BIO_OK : BIO_DISK_IOERR;
        BIO_TRACE_END(BDM_TRACE_READ_SYNC, ret);
        ChkTrue(ret == BIO_OK, ret,
                "Failed to copy data from disk chunkId:" << fromIt->chunkId + fromIt->chunkOffset + fromOffset
                                                         << " to memory by length:" << len << ".");
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
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);
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
        ret = (ret == BIO_OK) ? BIO_OK : BIO_DISK_IOERR;
        BIO_TRACE_END(BDM_TRACE_WRITE_SYNC, ret);
        LOG_TRACE("Copy data from disk chunk:" << fromIt->chunkOffset << ", from off:" << fromOffset
                                               << ", to off:" << toOffset << ", len:" << len << ".");
        ChkTrue(ret == BIO_OK, ret,
                "Failed to copy data from memory to disk address:" << toIt->chunkId + toIt->chunkOffset + toOffset
                                                                   << " by length:" << len << ".");
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
        LOG_TRACE("next from off:" << fromOffset << ", to off:" << toOffset);
    }
    return BIO_OK;
}

BResult CacheSliceOperator::CopyFromMemoryToMemory(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);
    BResult ret = BIO_INNER_ERR;
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();
    auto fromIt = fromAddrs.begin();
    auto toIt = toAddrs.begin();
    uint64_t fromOffset = 0;
    uint64_t toOffset = 0;
    uint64_t len = 0;

    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        BIO_TP_START(SLICE_COPY_MEMORY2MEMORY_ERR, &ret, BIO_ERR);
        ret = memcpy_s(reinterpret_cast<void *>(toIt->chunkId + toIt->chunkOffset + toOffset), len,
                       reinterpret_cast<void *>(fromIt->chunkId + fromIt->chunkOffset + fromOffset), len);
        BIO_TP_END;
        ChkTrue(ret == BIO_OK, ret, "Failed to copy data, length:" << len << ".");
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

inline uint64_t CacheSliceOperator::MinLen(uint64_t from, uint64_t to)
{
    return (from < to) ? from : to;
}
} // namespace bio
} // namespace ock
