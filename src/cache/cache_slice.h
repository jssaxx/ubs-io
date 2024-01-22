/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_CACHE_SLICE_H
#define BOOSTIO_CACHE_SLICE_H

#include "bio_log.h"
#include "securec.h"
#include "slice.h"

namespace ock {
namespace bio {

struct SliceKey {
    uint64_t flowId;
    uint64_t flowOffset;
    FlowType flowType;
    uint64_t length;
    uint64_t indexInFlow; /* the slice ID in the flow, which is unique in a flow. */

public:
    SliceKey(uint64_t fId, uint64_t off, FlowType type, uint64_t len, uint64_t index)
            : flowId(fId), flowOffset(off), flowType(type), length(len), indexInFlow(index) {}

    bool Validate() const
    {
        ASSERT_RETURN(flowId != NO_MAX_VALUE64, false);
        ASSERT_RETURN(flowOffset != NO_MAX_VALUE64, false);
        ASSERT_RETURN(length != NO_MAX_VALUE64, false);
        return true;
    }
};

class RCacheSlice : public Slice {
public:
    RCacheSlice(uint64_t ptId, uint64_t length, std::vector<FlowAddr> &addrs, FlowType flowType = FLOW_MEMORY)
        : Slice(length, addrs, flowType), mPtId(ptId)
    {}

    ~RCacheSlice() override = default;

    uint64_t GetPtId() const
    {
        return mPtId;
    }

    std::string ToString() override
    {
        return Slice::ToString();
    }

    uint32_t GetSerializeLen() override
    {
        uint32_t len = 0;
        len += sizeof(mPtId);
        len += Slice::GetSerializeLen();
        return len;
    }

    BResult Serialize(char *data, uint32_t &length) override
    {
        uint32_t pos = 0;
        ASSERT_RETURN(data != nullptr, BIO_INVALID_PARAM);
        memcpy_s(data + pos, sizeof(mPtId), &mPtId, sizeof(mPtId));
        pos += sizeof(mPtId);
        auto ret = Slice::Serialize(data + pos, length);
        if (ret != BIO_OK) {
            return ret;
        }
        length += pos;
        return BIO_OK;
    }

    BResult Deserialize(char *data, uint32_t length) override
    {
        uint32_t pos = 0;
        ASSERT_RETURN(data != nullptr, BIO_INVALID_PARAM);
        ASSERT_RETURN(length >= pos + sizeof(mPtId), BIO_INVALID_PARAM);
        memcpy_s(&mPtId, sizeof(mPtId), data + pos, sizeof(mPtId));
        pos += sizeof(mPtId);
        auto ret = Slice::Deserialize(data + pos, length - pos);
        if (ret != BIO_OK) {
            return ret;
        }
        return BIO_OK;
    }

private:
    uint64_t mPtId;
};
using RCacheSlicePtr = Ref<RCacheSlice>;
using RCacheSliceRef = SliceRef<RCacheSlice>;
using RCacheSliceRefPtr = Ref<RCacheSliceRef>;

class WCacheSlice : public Slice {
public:
    WCacheSlice() = default;

    WCacheSlice(uint64_t flowId, uint64_t offsetInFlow, uint64_t indexInFlow, uint64_t length,
        std::vector<FlowAddr> &addrs, FlowType flowType = FLOW_MEMORY)
        : Slice(length, addrs, flowType), mFlowId(flowId), mOffsetInFlow(offsetInFlow), mIndexInFlow(indexInFlow)
    {}

    ~WCacheSlice() override = default;

    FlowType GetFlowType() const
    {
        return Slice::GetFlowType();
    }

    uint64_t GetFlowId() const
    {
        return mFlowId;
    }

    uint64_t GetOffsetInFlow() const
    {
        return mOffsetInFlow;
    }

    uint64_t GetIndexInFlow() const
    {
        return mIndexInFlow;
    }

    uint64_t GetSliceState() const
    {
        return mState;
    }

    void SetSliceState(uint64_t state)
    {
        mState = state;
    }

    std::string ToString() override
    {
        return Slice::ToString();
    }

    uint32_t GetSerializeLen() override
    {
        uint32_t len = 0;
        len += sizeof(mFlowId);
        len += sizeof(mOffsetInFlow);
        len += sizeof(mIndexInFlow);
        len += Slice::GetSerializeLen();
        return len;
    }

    BResult Serialize(char *data, uint32_t &length) override
    {
        uint32_t pos = 0;
        ASSERT_RETURN(data != nullptr, BIO_INVALID_PARAM);
        memcpy_s(data + pos, sizeof(mFlowId), &mFlowId, sizeof(mFlowId));
        pos += sizeof(mFlowId);
        memcpy_s(data + pos, sizeof(mOffsetInFlow), &mOffsetInFlow, sizeof(mOffsetInFlow));
        pos += sizeof(mOffsetInFlow);
        memcpy_s(data + pos, sizeof(mIndexInFlow), &mIndexInFlow, sizeof(mIndexInFlow));
        pos += sizeof(mIndexInFlow);
        auto ret = Slice::Serialize(data + pos, length);
        if (ret != BIO_OK) {
            return ret;
        }
        length += pos;
        return BIO_OK;
    }

    BResult Deserialize(char *data, uint32_t length) override
    {
        uint32_t pos = 0;
        ASSERT_RETURN(data != nullptr, BIO_INVALID_PARAM);
        ASSERT_RETURN(length >= pos + sizeof(mFlowId), BIO_INVALID_PARAM);
        memcpy_s(&mFlowId, sizeof(mFlowId), data + pos, sizeof(mFlowId));
        pos += sizeof(mFlowId);
        ASSERT_RETURN(length >= pos + sizeof(mOffsetInFlow), BIO_INVALID_PARAM);
        memcpy_s(&mOffsetInFlow, sizeof(mOffsetInFlow), data + pos, sizeof(mOffsetInFlow));
        pos += sizeof(mOffsetInFlow);
        ASSERT_RETURN(length >= pos + sizeof(mIndexInFlow), BIO_INVALID_PARAM);
        memcpy_s(&mIndexInFlow, sizeof(mIndexInFlow), data + pos, sizeof(mIndexInFlow));
        pos += sizeof(mIndexInFlow);
        auto ret = Slice::Deserialize(data + pos, length - pos);
        if (ret != BIO_OK) {
            return ret;
        }
        return BIO_OK;
    }

private:
    uint64_t mFlowId {0};
    uint64_t mOffsetInFlow {0}; /* the offset of a slice in the flow. */
    uint64_t mIndexInFlow {0};  /* the index of a slice in the flow, which is unique in a flow. */
    uint64_t mState {0};
};
using WCacheSlicePtr = Ref<WCacheSlice>;

using WCacheSliceRef = SliceRef<WCacheSlicePtr>;
using WCacheSliceRefPtr = Ref<WCacheSliceRef>;

class WCacheSliceCmp {
public:
    bool operator () (const WCacheSlicePtr &first, const WCacheSlicePtr &second)
    {
        if (first->GetIndexInFlow() == second->GetIndexInFlow()) {
            return true;
        }
        return first->GetIndexInFlow() < second->GetIndexInFlow();
    }
};
}
}

#endif // BOOSTIO_CACHE_SLICE_H
