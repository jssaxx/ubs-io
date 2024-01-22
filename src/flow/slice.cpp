/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "slice.h"
#include "bio_log.h"
#include "securec.h"

namespace ock {
namespace bio {
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
    if (offset >= mLength || length > mLength || offset + length > mLength) {
        return nullptr;
    }
    if (offset == 0 && length == mLength) {
        return this;
    }
    std::vector<FlowAddr> newAddrs;
    uint64_t rangeStart = 0;
    uint64_t rangeEnd = 0;
    for (const auto &addr : mAddrs) {
        rangeEnd = rangeStart + addr.chunkLen;
        // first segment.
        if (offset >= rangeStart && offset < rangeEnd) {
            auto chunkOffset = offset - rangeStart + addr.chunkOffset;
            if (offset + length <= rangeEnd) {
                // first and last segment.
                newAddrs.emplace_back(addr.chunkId, chunkOffset, length);
                break;
            } else {
                newAddrs.emplace_back(addr.chunkId, chunkOffset, addr.chunkLen - (offset - rangeStart));
            }
        } else {
            if (offset + length >= rangeEnd) {
                // middle segment.
                newAddrs.emplace_back(addr);
            } else {
                // last segment.
                newAddrs.emplace_back(addr.chunkId, addr.chunkOffset, length - rangeStart);
                break;
            }
        }

        rangeStart += addr.chunkLen;
    }

    return MakeRef<Slice>(length, newAddrs, mFlowType);
}

uint32_t Slice::GetSerializeLen()
{
    uint32_t len = 0;
    len += sizeof(mFlowType);
    len += sizeof(mLength);
    size_t vsize = mAddrs.size();
    len += sizeof(vsize);
    len += sizeof(FlowAddr) * vsize;
    return len;
}

BResult Slice::Serialize(char *data, uint32_t &length)
{
    uint32_t pos = 0;
    ASSERT_RETURN(data != nullptr, BIO_INVALID_PARAM);
    memcpy_s(data + pos, sizeof(mFlowType), &mFlowType, sizeof(mFlowType));
    pos += sizeof(mFlowType);
    memcpy_s(data + pos, sizeof(mLength), &mLength, sizeof(mLength));
    pos += sizeof(mLength);
    size_t vsize = mAddrs.size();
    memcpy_s(data + pos, sizeof(vsize), &vsize, sizeof(vsize));
    pos += sizeof(vsize);
    for (size_t i = 0; i < vsize; i++) {
        memcpy_s(data + pos, sizeof(FlowAddr), &mAddrs[i], sizeof(FlowAddr));
        pos += sizeof(FlowAddr);
    }
    length = pos;
    return BIO_OK;
}

BResult Slice::Deserialize(char *data, uint32_t length)
{
    uint32_t pos = 0;
    ASSERT_RETURN(data != nullptr, BIO_INVALID_PARAM);
    ASSERT_RETURN(length >= pos + sizeof(mFlowType), BIO_INVALID_PARAM);
    memcpy_s(&mFlowType, sizeof(mFlowType), data + pos, sizeof(mFlowType));
    pos += sizeof(mFlowType);
    ASSERT_RETURN(length >= pos + sizeof(mLength), BIO_INVALID_PARAM);
    memcpy_s(&mLength, sizeof(mLength), data + pos, sizeof(mLength));
    pos += sizeof(mLength);
    size_t vsize = mAddrs.size();
    ASSERT_RETURN(length >= pos + sizeof(vsize), BIO_INVALID_PARAM);
    memcpy_s(&vsize, sizeof(vsize), data + pos, sizeof(vsize));
    pos += sizeof(vsize);
    for (size_t i = 0; i < vsize; i++) {
        FlowAddr flowAddr;
        ASSERT_RETURN(length >= pos + sizeof(FlowAddr), BIO_INVALID_PARAM);
        memcpy_s(&flowAddr, sizeof(FlowAddr), data + pos, sizeof(FlowAddr));
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
        ss << "(" << addr.chunkId << "," << addr.chunkOffset << "," << addr.chunkLen << ")";
    }
    return ss.str();
}
}
}
