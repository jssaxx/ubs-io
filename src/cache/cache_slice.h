/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_CACHE_SLICE_H
#define BOOSTIO_CACHE_SLICE_H

#include <utility>
#include "bio_log.h"
#include "securec.h"
#include "slice.h"
#include "cache_def.h"

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
        : flowId(fId), flowOffset(off), flowType(type), length(len), indexInFlow(index)
    {}

    bool Validate() const
    {
        ChkTrueNot(flowId != NO_MAX_VALUE64, false);
        ChkTrueNot(flowOffset != NO_MAX_VALUE64, false);
        ChkTrueNot(length != NO_MAX_VALUE64, false);
        return true;
    }
};

class RCacheSlice : public Slice {
public:
    RCacheSlice(uint16_t ptId, uint64_t length, std::vector<FlowAddr> &addrs, FlowType flowType = FLOW_MEMORY)
        : Slice(length, addrs, flowType), mPtId(ptId)
    {}

    ~RCacheSlice() override = default;

    uint16_t GetPtId() const
    {
        return mPtId;
    }

    std::string ToString() override
    {
        return Slice::ToString();
    }

    uint64_t GetSerializeLen() override
    {
        uint64_t len = 0;
        len += sizeof(mPtId);
        len += Slice::GetSerializeLen();
        return len;
    }

    BResult Serialize(char *data, uint64_t dataLen, uint64_t &length) override
    {
        ChkTrueNot(data != nullptr, BIO_INVALID_PARAM);
        uint64_t pos = 0;
        uint64_t cpyLen = dataLen;
        auto ret = memcpy_s(data + pos, cpyLen, &mPtId, sizeof(mPtId));
        ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
        pos += sizeof(mPtId);
        cpyLen -= sizeof(mPtId);
        ret = Slice::Serialize(data + pos, cpyLen, length);
        if (ret != BIO_OK) {
            return ret;
        }
        length += pos;
        return BIO_OK;
    }

    BResult Deserialize(char *data, uint64_t length) override
    {
        uint64_t pos = 0;
        ChkTrueNot(data != nullptr, BIO_INVALID_PARAM);
        ChkTrue(length >= pos + sizeof(mPtId), BIO_INVALID_PARAM,
            "Failed to deserialize data, length:" << length << "  pos + sizeof(mPtId):" << pos + sizeof(mPtId));
        memcpy_s(&mPtId, sizeof(mPtId), data + pos, sizeof(mPtId));
        pos += sizeof(mPtId);
        auto ret = Slice::Deserialize(data + pos, length - pos);
        if (ret != BIO_OK) {
            return ret;
        }
        return BIO_OK;
    }

private:
    uint16_t mPtId;
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

    inline FlowType GetFlowType() const
    {
        return Slice::GetFlowType();
    }

    inline uint64_t GetFlowId() const
    {
        return mFlowId;
    }

    inline uint64_t GetOffsetInFlow() const
    {
        return mOffsetInFlow;
    }

    inline uint64_t GetIndexInFlow() const
    {
        return mIndexInFlow;
    }

    inline std::string ToString() override
    {
        return Slice::ToString();
    }

    uint64_t GetSerializeLen() override
    {
        uint64_t len = 0;
        len += sizeof(mFlowId);
        len += sizeof(mOffsetInFlow);
        len += sizeof(mIndexInFlow);
        len += Slice::GetSerializeLen();
        return len;
    }

    BResult Serialize(char *data, uint64_t dataLen, uint64_t &length) override
    {
        ChkTrueNot(data != nullptr, BIO_INVALID_PARAM);
        uint64_t pos = 0;
        uint64_t cpyLen = dataLen;
        auto ret = memcpy_s(data + pos, cpyLen, &mFlowId, sizeof(mFlowId));
        ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
        pos += sizeof(mFlowId);
        cpyLen -= sizeof(mFlowId);
        ret = memcpy_s(data + pos, cpyLen, &mOffsetInFlow, sizeof(mOffsetInFlow));
        ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
        pos += sizeof(mOffsetInFlow);
        cpyLen -= sizeof(mOffsetInFlow);
        ret = memcpy_s(data + pos, cpyLen, &mIndexInFlow, sizeof(mIndexInFlow));
        ChkTrue(ret == BIO_OK, BIO_INNER_ERR, "Memory copy failed.");
        pos += sizeof(mIndexInFlow);
        cpyLen -= sizeof(mIndexInFlow);
        ret = Slice::Serialize(data + pos, cpyLen, length);
        if (ret != BIO_OK) {
            return ret;
        }
        length += pos;
        return BIO_OK;
    }

    BResult Deserialize(char *data, uint64_t length) override
    {
        uint64_t pos = 0;
        ChkTrueNot(data != nullptr, BIO_INVALID_PARAM);
        ChkTrue(length >= pos + sizeof(mFlowId), BIO_INVALID_PARAM,
            "Failed to deserialize data, length:" << length << "  pos + sizeof(mFlowId):" << pos + sizeof(mFlowId));
        memcpy_s(&mFlowId, sizeof(mFlowId), data + pos, sizeof(mFlowId));
        pos += sizeof(mFlowId);
        ChkTrue(length >= pos + sizeof(mOffsetInFlow), BIO_INVALID_PARAM,
            "Failed to deserialize data, length:" << length << "  pos + sizeof(mOffsetInFlow):" <<
            pos + sizeof(mOffsetInFlow));
        memcpy_s(&mOffsetInFlow, sizeof(mOffsetInFlow), data + pos, sizeof(mOffsetInFlow));
        pos += sizeof(mOffsetInFlow);
        ChkTrue(length >= pos + sizeof(mIndexInFlow), BIO_INVALID_PARAM,
            "Failed to deserialize data, length:" << length << "  pos + sizeof(mIndexInFlow):" <<
            pos + sizeof(mIndexInFlow));
        memcpy_s(&mIndexInFlow, sizeof(mIndexInFlow), data + pos, sizeof(mIndexInFlow));
        pos += sizeof(mIndexInFlow);
        auto ret = Slice::Deserialize(data + pos, length - pos);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
        return BIO_OK;
    }

private:
    uint64_t mFlowId{ 0 };
    uint64_t mOffsetInFlow{ 0 }; /* the offset of a slice in the flow. */
    uint64_t mIndexInFlow{ 0 };  /* the index of a slice in the flow, which is unique in a flow. */
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

class WCacheReplicaSlice {
public:
    WCacheReplicaSlice() = default;
    ~WCacheReplicaSlice()  = default;

    WCacheReplicaSlice(WCacheSliceRefPtr slice, RealIoStrategy strategy, uint64_t offset, uint32_t refCount,
        bool state = false)
        : sliceRefPtr(std::move(slice)), mStrategy(strategy), mNegoOffset(offset), mEvictRef(refCount), mState(state)
    {}

    inline WCacheSliceRefPtr GetSlice()
    {
        return sliceRefPtr;
    }

    inline uint64_t GeNegotiateOffset() const
    {
        return mNegoOffset;
    }

    inline uint32_t GetEvictRef() const
    {
        return mEvictRef;
    }

    inline uint32_t DecEvictRef()
    {
        return (--mEvictRef);
    }

    inline bool GetState() const
    {
        return mState;
    };

    inline void SetState(bool state)
    {
        mState = state;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    WCacheSliceRefPtr sliceRefPtr;
    RealIoStrategy mStrategy;
    uint64_t mNegoOffset = 0;
    uint32_t mEvictRef = 1;
    bool mState = false;

    DEFINE_REF_COUNT_VARIABLE
};
using WCacheReplicaSlicePtr = Ref<WCacheReplicaSlice>;

}
}

#endif // BOOSTIO_CACHE_SLICE_H
