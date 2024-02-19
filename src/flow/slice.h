/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_SLICE_H
#define BOOSTIO_SLICE_H

#include <vector>
#include <functional>
#include <sstream>
#include "flow_type.h"
#include "bio_lock.h"
#include "bio_ref.h"
#include "bio_err.h"
#include "bio_log.h"
#include "net_common.h"
#include "securec.h"

namespace ock {
namespace bio {
class Slice;
using SlicePtr = Ref<Slice>;

class SliceSerializer {
public:
    virtual ~SliceSerializer() = default;

    virtual std::string ToString() = 0;

    virtual uint32_t GetSerializeLen() = 0;

    virtual BResult Serialize(char *data, uint32_t &length) = 0;

    virtual BResult Deserialize(char *data, uint32_t length) = 0;
};

class Slice : public SliceSerializer {
public:
    Slice() = default;

    Slice(uint64_t length, std::vector<FlowAddr> &addrs, FlowType flowType = FLOW_MEMORY)
        : mFlowType(flowType), mLength(length), mAddrs(std::move(addrs))
    {}

    ~Slice() override = default;

    inline FlowType GetFlowType() const
    {
        return mFlowType;
    }

    inline uint64_t GetLength() const
    {
        return mLength;
    }

    inline const std::vector<FlowAddr> &GetAddrs() const
    {
        return mAddrs;
    }

    bool IsTheSameWith(const SlicePtr &other);

    SlicePtr Split(uint64_t offset, uint64_t length);

    std::string ToString() override;

    uint32_t GetSerializeLen() override;

    BResult Serialize(char *data, uint32_t &length) override;

    BResult Deserialize(char *data, uint32_t length) override;

    DEFINE_REF_COUNT_FUNCTIONS;

protected:
    DEFINE_REF_COUNT_VARIABLE;

private:
    FlowType mFlowType{ FLOW_MEMORY };
    uint64_t mLength{ 0 };
    std::vector<FlowAddr> mAddrs;
};

template <typename S> class SliceRef {
public:
    explicit SliceRef(const S &slice) : mSlice(slice), mRef(0) {}

    inline void Aquire()
    {
        if (mRef == 0) {
            std::lock_guard<std::mutex> lock(mSliceLock);
            ++mRef;
        } else {
            ++mRef;
        }
    }

    inline void Release()
    {
        if (--mRef == 0) {
            std::lock_guard<std::mutex> lock(mSliceLock);
            if (mRef == 0 && mNewSlice != nullptr && mSetSliceCallback != nullptr) {
                auto oldSlice = mSlice;
                mSlice = mNewSlice;
                mSetSliceCallback(oldSlice);

                mNewSlice = nullptr;
                mSetSliceCallback = nullptr;
            }
        }
    }

    inline bool Test()
    {
        return mRef > 0;
    }

    inline S &GetSlice()
    {
        return mSlice;
    }

    using SetSliceCallback = std::function<void(const S &oldSlice)>;
    void SetSlice(const S &newSlice, SetSliceCallback &setSliceCallback)
    {
        std::lock_guard<std::mutex> lock(mSliceLock);
        if (mRef == 0) {
            auto oldSlice = mSlice;
            mSlice = newSlice;
            setSliceCallback(oldSlice);
        } else {
            mNewSlice = newSlice;
            mSetSliceCallback = setSliceCallback;
        }
    }
    DEFINE_REF_COUNT_FUNCTIONS;

private:
    std::mutex mSliceLock;
    S mSlice{ nullptr };
    S mNewSlice{ nullptr };
    SetSliceCallback mSetSliceCallback{ nullptr };
    std::atomic<uint64_t> mRef{ 0 };
    DEFINE_REF_COUNT_VARIABLE;
};
using SliceRefPtr = Ref<SliceRef<SlicePtr>>;
}
}
#endif // BOOSTIO_SLICE_H
