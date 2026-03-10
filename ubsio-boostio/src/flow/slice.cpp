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

#include "bio_log.h"
#include "bio_crc_util.h"
#include "bio_tracepoint_helper.h"
#include "bdm_core.h"
#include "securec.h"
#include "slice.h"
#include <atomic>
#include <semaphore.h>

namespace ock {
namespace bio {

// 异步IO上下文结构
struct AsyncIoContext {
    std::atomic<int32_t> ret{0};
    std::atomic<bool> completed{false};
};

// 批量异步IO等待上下文
struct BatchAsyncIoContext {
    std::atomic<uint32_t> quota{0};
    std::atomic<int32_t> ret{0};
    sem_t sem;
};

// 异步IO回调函数
static void AsyncIoCallback(void *ctx, int32_t ret) {
    auto *asyncCtx = reinterpret_cast<AsyncIoContext *>(ctx);
    asyncCtx->ret.store(ret);
    asyncCtx->completed.store(true);
}

// 批量异步IO回调函数
static void BatchAsyncIoCallback(void *ctx, int32_t ret) {
    auto *batchCtx = reinterpret_cast<BatchAsyncIoContext *>(ctx);
    if (ret != BIO_OK && batchCtx->ret.load() == BIO_OK) {
        batchCtx->ret.store(ret);
    }
    if (batchCtx->quota.fetch_sub(1) == 1) {
        sem_post(&batchCtx->sem);
    }
}

// 等待批量异步IO完成
static int32_t WaitBatchAsyncIo(BatchAsyncIoContext *batchCtx) {
    sem_wait(&batchCtx->sem);
    return batchCtx->ret.load();
}

// 初始化批量异步IO上下文
static void InitBatchAsyncIoContext(BatchAsyncIoContext *batchCtx, uint32_t quota) {
    batchCtx->quota.store(quota);
    batchCtx->ret.store(BIO_OK);
    sem_init(&batchCtx->sem, 0, 0);
}

// 销毁批量异步IO上下文
static void DestroyBatchAsyncIoContext(BatchAsyncIoContext *batchCtx) {
    sem_destroy(&batchCtx->sem);
}
bool Slice::IsTheSameWith(const SlicePtr &other)
{
    if (other->GetFlowType() != GetFlowType()) {
        return false;
    }

    if (other->GetLength() != GetLength()) {
        return false;
    }

    auto &fromAddrs = other->GetAddrs();
    auto &toAddrs = GetAddrs();

    if (fromAddrs.size() != toAddrs.size()) {
        return false;
    }

    for (uint32_t i = 0; i < fromAddrs.size(); ++i) {
        auto &fromAddr = fromAddrs[i];
        auto &toAddr = toAddrs[i];
        if (fromAddr.chunkId != toAddr.chunkId) {
            return false;
        }
        if (fromAddr.chunkOffset != toAddr.chunkOffset) {
            return false;
        }
        if (fromAddr.chunkLen != toAddr.chunkLen) {
            return false;
        }
    }
    return true;
}

SlicePtr Slice::Split(uint64_t offset, uint64_t length)
{
    if (UNLIKELY(offset >= mLength || length > mLength || offset + length > mLength)) {
        return nullptr;
    }
    if (offset == 0 && length == mLength) {
        return this;
    }
    std::vector<FlowAddr> newAddrs;
    uint64_t rangeStart = 0;
    uint64_t rangeEnd = 0;
    uint64_t splitStart = offset;
    for (const auto &addr : mAddrs) {
        rangeEnd = rangeStart + addr.chunkLen;
        if (splitStart >= rangeStart && splitStart < rangeEnd) {
            uint64_t splitOffset = splitStart - rangeStart;
            uint64_t splitLen = rangeEnd - splitStart;
            if (splitLen >= length - (splitStart - offset)) {
                newAddrs.emplace_back(addr.chunkId, addr.chunkOffset + splitOffset, length - (splitStart - offset));
                LOG_DEBUG("Split, offset:" << addr.chunkOffset + splitOffset << ", len:" <<
                    length - (splitStart - offset));
                break;
            } else {
                LOG_DEBUG("Split, offset:" << addr.chunkOffset + splitOffset << ", len:" << splitLen);
                newAddrs.emplace_back(addr.chunkId, addr.chunkOffset + splitOffset, splitLen);
            }
            splitStart += splitLen;
        }
        rangeStart += addr.chunkLen;
    }

    return MakeRef<Slice>(length, newAddrs, mFlowType);
}

uint64_t Slice::GetSerializeLen()
{
    uint64_t len = 0;
    len += sizeof(mdataCrc);
    len += sizeof(mFlowType);
    len += sizeof(mLength);
    size_t vsize = mAddrs.size();
    len += sizeof(vsize);
    len += sizeof(FlowAddr) * vsize;
    return len;
}

BResult Slice::Serialize(char *data, uint64_t dataLen, uint64_t &length)
{
    uint64_t pos = 0;
    uint64_t cpyLen = dataLen;
    ChkTrueNot(data != nullptr, BIO_INVALID_PARAM);
    BResult ret = BIO_OK;
    ret = memcpy_s(data + pos, cpyLen, &mdataCrc, sizeof(mdataCrc));
    ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
    pos += sizeof(mdataCrc);
    cpyLen -= sizeof(mdataCrc);
    ret = memcpy_s(data + pos, cpyLen, &mFlowType, sizeof(mFlowType));
    ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
    pos += sizeof(mFlowType);
    cpyLen -= sizeof(mFlowType);
    ret = memcpy_s(data + pos, cpyLen, &mLength, sizeof(mLength));
    ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
    pos += sizeof(mLength);
    cpyLen -= sizeof(mLength);
    size_t vsize = mAddrs.size();
    ret = memcpy_s(data + pos, cpyLen, &vsize, sizeof(vsize));
    ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
    pos += sizeof(vsize);
    cpyLen -= sizeof(vsize);
    for (size_t i = 0; i < vsize; i++) {
        ret = memcpy_s(data + pos, cpyLen, &mAddrs[i], sizeof(FlowAddr));
        ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
        pos += sizeof(FlowAddr);
        cpyLen -= sizeof(FlowAddr);
    }
    length = pos;
    return BIO_OK;
}

BResult Slice::Deserialize(char *data, uint64_t length)
{
    uint64_t pos = 0;
    ChkTrueNot(data != nullptr, BIO_INVALID_PARAM);
    ChkTrue(length >= pos + sizeof(mdataCrc), BIO_INVALID_PARAM,
            "Failed to deserialize data, length:" << length << "  pos + sizeof(mFlowType):" << pos + sizeof(mdataCrc));
    int ret = memcpy_s(&mdataCrc, sizeof(mdataCrc), data + pos, sizeof(mdataCrc));
    ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "datacrc memory copy failed.");
    pos += sizeof(mdataCrc);
    ChkTrue(length >= pos + sizeof(mFlowType), BIO_INVALID_PARAM,
        "Failed to deserialize data, length:" << length << "  pos + sizeof(mFlowType):" << pos + sizeof(mFlowType));
    ret = memcpy_s(&mFlowType, sizeof(mFlowType), data + pos, sizeof(mFlowType));
    ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "flow data memory copy failed.");
    pos += sizeof(mFlowType);
    ChkTrue(length >= pos + sizeof(mLength), BIO_INVALID_PARAM,
        "Failed to deserialize data, length:" << length << "  pos + sizeof(mLength):" << pos + sizeof(mLength));
    ret = memcpy_s(&mLength, sizeof(mLength), data + pos, sizeof(mLength));
    ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "length memory copy failed.");
    pos += sizeof(mLength);
    size_t vsize = 0;
    BIO_TP_START(DESERIALIZE_SET_VSIZE, &vsize, NO_3);
    ChkTrue(length >= pos + sizeof(vsize), BIO_INVALID_PARAM,
        "Failed to deserialize data, length:" << length << "  pos + sizeof(vsize):" << pos + sizeof(vsize));
    ret = memcpy_s(&vsize, sizeof(vsize), data + pos, sizeof(vsize));
    ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "vsize memory copy failed.");
    pos += sizeof(vsize);
    BIO_TP_END;
    ChkTrue(vsize <= NO_4, BIO_INVALID_PARAM, "Failed to deserialize data, vsize:" << vsize << ", failed.");
    for (size_t i = 0; i < vsize; i++) {
        FlowAddr flowAddr;
        ChkTrue(length >= pos + sizeof(FlowAddr), BIO_INVALID_PARAM,
            "Failed to deserialize data, length:" << length << " pos + sizeof(FlowAddr):" << pos + sizeof(FlowAddr));
        ret = memcpy_s(&flowAddr, sizeof(FlowAddr), data + pos, sizeof(FlowAddr));
        ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "flow addr memory copy failed.");
        mAddrs.push_back(flowAddr);
        pos += sizeof(FlowAddr);
    }
    return BIO_OK;
}

std::string Slice::ToString()
{
    std::stringstream ss;
    ss << "type:" << mFlowType << ",length:" << mLength;
    ss << ",addr:";
    for (const auto &addr : mAddrs) {
        ss << "(" << addr.chunkOffset << "," << addr.chunkLen << ")";
    }
    return ss.str();
}

BResult Slice::CalculateDataCrc(uint32_t &valueCrc, uint64_t dataOffset, uint64_t dataLength)
{
    char *value = reinterpret_cast<char *>(aligned_alloc(NO_4096, mLength));
    ChkTrueNot(value != nullptr, BIO_ALLOC_FAIL);

    uint64_t cpyLength = mLength;
    uint64_t offset = 0;
    if (mFlowType == FLOW_MEMORY) {
        for (auto fromAddr : mAddrs) {
            auto ret = memcpy_s(reinterpret_cast<void *>(value + offset), cpyLength,
                reinterpret_cast<void *>(fromAddr.chunkId + fromAddr.chunkOffset), fromAddr.chunkLen);
            if (ret != BIO_OK) {
                LOG_ERROR("Failed to copy data, length:" << fromAddr.chunkLen);
                free(value);
                value = nullptr;
                return ret;
            }
            offset += fromAddr.chunkLen;
            cpyLength -= fromAddr.chunkLen;
        }
    } else {
        std::vector<AsyncIoContext> asyncCtxs(mAddrs.size());
        std::vector<BdmIoCtx> ioCtxs(mAddrs.size());
        BatchAsyncIoContext batchCtx;
        InitBatchAsyncIoContext(&batchCtx, mAddrs.size());
        
        for (size_t i = 0; i < mAddrs.size(); i++) {
            auto &fromAddr = mAddrs[i];
            ioCtxs[i] = {
                .cb = BatchAsyncIoCallback,
                .ctx = &batchCtx
            };
            auto ret = BdmReadAsync(fromAddr.chunkId, fromAddr.chunkOffset, reinterpret_cast<void *>(value + offset),
                fromAddr.chunkLen, &ioCtxs[i]);
            if (ret != BIO_OK) {
                LOG_ERROR("Failed to copy data from disk chunkId:" << (fromAddr.chunkId + fromAddr.chunkOffset) <<
                    " to memory by length:" << fromAddr.chunkLen << ".");
                DestroyBatchAsyncIoContext(&batchCtx);
                free(value);
                value = nullptr;
                return BIO_DISK_IOERR;
            }
            offset += fromAddr.chunkLen;
        }
        
        auto ret = WaitBatchAsyncIo(&batchCtx);
        DestroyBatchAsyncIoContext(&batchCtx);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to copy data from disk to memory.");
            free(value);
            value = nullptr;
            return BIO_DISK_IOERR;
        }
    }

    valueCrc = BioCrcUtil::Crc32(value + dataOffset, dataLength);
    free(value);
    value = nullptr;
    return BIO_OK;
}

BResult Slice::VerifyDataCrc(uint32_t originCrc, uint64_t dataOffset, uint64_t dataLength, Slice *slice)
{
    uint32_t currentCrc = 0;
    auto ret = CalculateDataCrc(currentCrc, dataOffset, dataLength);
    if (ret != BIO_OK || originCrc != currentCrc) {
        LOG_ERROR("slice verify the CRC fail, ret:" << ret << " origin crc:" <<
            originCrc << ", current crc:" << currentCrc);
        return ret != BIO_OK ? ret : BIO_CRC_ERR;
    }
    if (slice != nullptr) {
        slice->SetDataCrc(currentCrc);
    }
    return BIO_OK;
}
}
}
