/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "cache_slice_operator.h"
#include "bdm_core.h"

namespace ock {
namespace bio {
BResult CacheSliceOperator::Copy(const SlicePtr &from, const SlicePtr &to)
{
    ASSERT_RETURN(Validate(from, to), BIO_INVALID_PARAM);

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
    ASSERT_RETURN(Validate(to), BIO_INVALID_PARAM);
    ASSERT_RETURN(from != nullptr, BIO_INVALID_PARAM);

    if (to->GetFlowType() == FLOW_MEMORY) {
        auto &toAddrs = to->GetAddrs();
        uint64_t offset = 0;
        for (auto toAddr : toAddrs) {
            auto ret = memcpy_s(reinterpret_cast<void *>(toAddr.chunkId + toAddr.chunkOffset), toAddr.chunkLen,
                reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen);
            ASSERT_RETURN(ret == BIO_OK, ret);
            offset += toAddr.chunkLen;
        }
        return BIO_OK;
    } else {
        auto &toAddrs = to->GetAddrs();
        uint64_t offset = 0;
        for (auto toAddr : toAddrs) {
            auto ret = BdmWrite(toAddr.chunkId, toAddr.chunkOffset,
                reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen);
            ASSERT_RETURN(ret == BIO_OK, ret);
            offset += toAddr.chunkLen;
        }
        return BIO_OK;
    }
}

BResult CacheSliceOperator::Copy(const SlicePtr &from, char *to)
{
    ASSERT_RETURN(Validate(from), BIO_INVALID_PARAM);
    ASSERT_RETURN(to != nullptr, BIO_INVALID_PARAM);

    if (from->GetFlowType() == FLOW_MEMORY) {
        auto &fromAddrs = from->GetAddrs();
        uint64_t offset = 0;
        for (auto fromAddr : fromAddrs) {
            auto ret = memcpy_s(reinterpret_cast<void *>(const_cast<char *>(to + offset)), fromAddr.chunkLen,
                reinterpret_cast<void *>(fromAddr.chunkId + fromAddr.chunkOffset), fromAddr.chunkLen);
            ASSERT_RETURN(ret == BIO_OK, ret);
            offset += fromAddr.chunkLen;
        }
        return BIO_OK;
    } else {
        auto &fromAddrs = from->GetAddrs();
        uint64_t offset = 0;
        for (auto fromAddr : fromAddrs) {
            auto ret =
                BdmRead(fromAddr.chunkId, fromAddr.chunkOffset, reinterpret_cast<void *>(to + offset), fromAddr.chunkLen);
            ASSERT_RETURN(ret == BIO_OK, ret);
            offset += fromAddr.chunkLen;
        }
        return BIO_OK;
    }
}

bool CacheSliceOperator::Validate(const SlicePtr &from, const SlicePtr &to)
{
    ASSERT_RETURN(from != nullptr, false);
    ASSERT_RETURN(to != nullptr, false);

    ASSERT_RETURN(from->GetLength() == to->GetLength(), false);

    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();

    ASSERT_RETURN(fromAddrs.size() == toAddrs.size(), false);

    for (uint32_t i = 0; i < fromAddrs.size(); ++i) {
        auto &fromAddr = fromAddrs[i];
        auto &toAddr = toAddrs[i];
        ASSERT_RETURN(fromAddr.chunkOffset == toAddr.chunkOffset, false);
        ASSERT_RETURN(fromAddr.chunkLen == toAddr.chunkLen, false);
    }
    return true;
}

bool CacheSliceOperator::Validate(const SlicePtr &slice)
{
    ASSERT_RETURN(slice != nullptr, false);
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
    for (uint32_t i = 0; i < fromAddrs.size(); ++i) {
        auto &fromAddr = fromAddrs[i];
        auto &toAddr = toAddrs[i];

        auto ret = BdmRead(fromAddr.chunkId, fromAddr.chunkOffset, reinterpret_cast<void *>(toAddr.chunkId + toAddr.chunkOffset),
            fromAddr.chunkLen);
        ASSERT_RETURN(ret == BIO_OK, ret);
    }
    return BIO_OK;
}

BResult CacheSliceOperator::CopyFromMemoryToDisk(const SlicePtr &from, const SlicePtr &to)
{
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();
    for (uint32_t i = 0; i < fromAddrs.size(); ++i) {
        auto &fromAddr = fromAddrs[i];
        auto &toAddr = toAddrs[i];

        auto ret =
            BdmWrite(toAddr.chunkId, toAddr.chunkOffset, reinterpret_cast<void *>(fromAddr.chunkId + fromAddr.chunkOffset), toAddr.chunkLen);
        ASSERT_RETURN(ret == BIO_OK, ret);
    }
    return BIO_OK;
}

BResult CacheSliceOperator::CopyFromMemoryToMemory(const SlicePtr &from, const SlicePtr &to)
{
    // TODO: implement me.
    LOG_ERROR("don't support copy from memory to memory.");
    return BIO_ERR;
}
}
}
