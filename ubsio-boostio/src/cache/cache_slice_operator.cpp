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
#include "bio_tracepoint_helper.h"
#include "bio_trace.h"
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace ock {
namespace bio {

// 异步IO上下文结构
struct AsyncIoContext {
    std::atomic<int32_t> ret{0};
    std::atomic<bool> completed{false};
    std::mutex mtx;
    std::condition_variable cv;
};

// 异步IO回调函数
static void AsyncIoCallback(void *ctx, int32_t ret) {
    auto *asyncCtx = reinterpret_cast<AsyncIoContext *>(ctx);
    asyncCtx->ret.store(ret);
    asyncCtx->completed.store(true);
    asyncCtx->cv.notify_one();
}

// 等待异步IO完成
static int32_t WaitAsyncIo(AsyncIoContext *asyncCtx) {
    std::unique_lock<std::mutex> lock(asyncCtx->mtx);
    asyncCtx->cv.wait(lock, [asyncCtx] { return asyncCtx->completed.load(); });
    return asyncCtx->ret.load();
}
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
        std::vector<AsyncIoContext> asyncCtxs(toAddrs.size());
        std::vector<BdmIoCtx> ioCtxs(toAddrs.size());
        uint64_t offset = 0;
        
        for (size_t i = 0; i < toAddrs.size(); i++) {
            auto &toAddr = toAddrs[i];
            BIO_TRACE_START(BDM_TRACE_WRITE_SYNC);
            ioCtxs[i] = {
                .cb = AsyncIoCallback,
                .ctx = &asyncCtxs[i]
            };
            ret = BdmWriteAsync(toAddr.chunkId, toAddr.chunkOffset,
                reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen, &ioCtxs[i]);
            if (ret != BIO_OK) {
                LOG_ERROR("Failed to submit async write, length:" << toAddr.chunkLen);
                return BIO_DISK_IOERR;
            }
            offset += toAddr.chunkLen;
        }
        
        for (size_t i = 0; i < asyncCtxs.size(); i++) {
            auto &toAddr = toAddrs[i];
            auto asyncRet = WaitAsyncIo(&asyncCtxs[i]);
            ret = (asyncRet == BIO_OK) ? BIO_OK : BIO_DISK_IOERR;
            BIO_TRACE_END(BDM_TRACE_WRITE_SYNC, ret);
            ChkTrue(ret == BIO_OK, ret, "Failed to copy length:" << toAddr.chunkLen);
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
        std::vector<AsyncIoContext> asyncCtxs(fromAddrs.size());
        std::vector<BdmIoCtx> ioCtxs(fromAddrs.size());
        uint64_t offset = 0;
        
        for (size_t i = 0; i < fromAddrs.size(); i++) {
            auto &fromAddr = fromAddrs[i];
            LOG_TRACE("Copy data from disk:" << " from off:" << fromAddr.chunkOffset << ", to off:" << offset);
            BIO_TRACE_START(BDM_TRACE_READ_SYNC);
            ioCtxs[i] = {
                .cb = AsyncIoCallback,
                .ctx = &asyncCtxs[i]
            };
            ret = BdmReadAsync(fromAddr.chunkId, fromAddr.chunkOffset, reinterpret_cast<void *>(to + offset),
                fromAddr.chunkLen, &ioCtxs[i]);
            if (ret != BIO_OK) {
                LOG_ERROR("Failed to submit async read, length:" << fromAddr.chunkLen);
                return BIO_DISK_IOERR;
            }
            offset += fromAddr.chunkLen;
        }
        
        for (size_t i = 0; i < asyncCtxs.size(); i++) {
            auto asyncRet = WaitAsyncIo(&asyncCtxs[i]);
            ret = (asyncRet == BIO_OK) ? BIO_OK : BIO_DISK_IOERR;
            BIO_TRACE_END(BDM_TRACE_READ_SYNC, ret);
            ChkTrue(ret == BIO_OK, ret,
                "Failed to copy data from disk chunkId:" << (fromAddrs[i].chunkId + fromAddrs[i].chunkOffset) <<
                " to memory by length:" << fromAddrs[i].chunkLen << ".");
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

    std::vector<AsyncIoContext> asyncCtxs;
    std::vector<BdmIoCtx> ioCtxs;
    std::vector<uint64_t> lens;
    std::vector<std::pair<uint64_t, uint64_t>> offsets;

    uint64_t len;
    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        BIO_TRACE_START(BDM_TRACE_READ_SYNC);
        BIO_TP_START(SLICE_COPY_DISK2MEMORY_OK, &ret, BIO_OK);
        AsyncIoContext asyncCtx;
        BdmIoCtx ioCtx = {
            .cb = AsyncIoCallback,
            .ctx = &asyncCtx
        };
        ret = BdmReadAsync(fromIt->chunkId, fromIt->chunkOffset + fromOffset,
            reinterpret_cast<void *>(toIt->chunkId + toIt->chunkOffset + toOffset), len, &ioCtx);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to submit async read, length:" << len);
            return BIO_DISK_IOERR;
        }
        asyncCtxs.push_back(asyncCtx);
        ioCtxs.push_back(ioCtx);
        lens.push_back(len);
        offsets.push_back({fromIt->chunkId, fromIt->chunkOffset + fromOffset});
        BIO_TP_END;
        
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
    
    for (size_t i = 0; i < asyncCtxs.size(); i++) {
        auto asyncRet = WaitAsyncIo(&asyncCtxs[i]);
        ret = (asyncRet == BIO_OK) ? BIO_OK : BIO_DISK_IOERR;
        BIO_TRACE_END(BDM_TRACE_READ_SYNC, ret);
        ChkTrue(ret == BIO_OK, ret,
            "Failed to copy data from disk chunkId:" << offsets[i].first << offsets[i].second <<
            " to memory by length:" << lens[i] << ".");
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

    std::vector<AsyncIoContext> asyncCtxs;
    std::vector<BdmIoCtx> ioCtxs;
    std::vector<uint64_t> lens;
    std::vector<std::pair<uint64_t, uint64_t>> offsets;

    uint64_t len;
    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        BIO_TRACE_START(BDM_TRACE_WRITE_SYNC);
        AsyncIoContext asyncCtx;
        BdmIoCtx ioCtx = {
            .cb = AsyncIoCallback,
            .ctx = &asyncCtx
        };
        auto ret = BdmWriteAsync(toIt->chunkId, toIt->chunkOffset + toOffset,
            reinterpret_cast<void *>(fromIt->chunkId + fromIt->chunkOffset + fromOffset), len, &ioCtx);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to submit async write, length:" << len);
            return BIO_DISK_IOERR;
        }
        asyncCtxs.push_back(asyncCtx);
        ioCtxs.push_back(ioCtx);
        lens.push_back(len);
        offsets.push_back({toIt->chunkId, toIt->chunkOffset + toOffset});
        
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
    
    for (size_t i = 0; i < asyncCtxs.size(); i++) {
        auto asyncRet = WaitAsyncIo(&asyncCtxs[i]);
        ret = (asyncRet == BIO_OK) ? BIO_OK : BIO_DISK_IOERR;
        BIO_TRACE_END(BDM_TRACE_WRITE_SYNC, ret);
        LOG_TRACE("Copy data from disk chunk:" << offsets[i].first << ", from off:" << offsets[i].second << ", to off:" << 
            toOffset << ", len:" << lens[i] << ".");
        ChkTrue(ret == BIO_OK, ret,
            "Failed to copy data from memory to disk address:" << offsets[i].first << offsets[i].second <<
            " by length:" << lens[i] << ".");
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
}
}
