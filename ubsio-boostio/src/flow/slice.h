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

#ifndef BOOSTIO_SLICE_H
#define BOOSTIO_SLICE_H

#include <functional>
#include <sstream>
#include <vector>
#include "bio_err.h"
#include "bio_lock.h"
#include "bio_log.h"
#include "bio_ref.h"
#include "flow_type.h"
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

    virtual uint64_t GetSerializeLen() = 0;

    virtual BResult Serialize(char *data, uint64_t dataLen, uint64_t &length) = 0;

    virtual BResult Deserialize(char *data, uint64_t length) = 0;
};

class Slice : public SliceSerializer {
public:
    Slice() = default;

    Slice(uint64_t length, std::vector<FlowAddr> &addrs, FlowType flowType = FLOW_MEMORY)
        : mFlowType(flowType),
          mLength(length),
          mAddrs(std::move(addrs))
    {
    }

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

    uint64_t GetSerializeLen() override;

    BResult Serialize(char *data, uint64_t dataLen, uint64_t &length) override;

    BResult Deserialize(char *data, uint64_t length) override;

    inline uint32_t GetDataCrc() const
    {
        return mdataCrc;
    }

    inline void SetDataCrc(uint32_t crc)
    {
        mdataCrc = crc;
    }

    BResult CalculateDataCrc(uint32_t &valueCrc, uint64_t dataOffset, uint64_t dataLength);

    BResult VerifyDataCrc(uint32_t originCrc, uint64_t dataOffset, uint64_t dataLength, Slice *slice);

    DEFINE_REF_COUNT_FUNCTIONS;

protected:
    DEFINE_REF_COUNT_VARIABLE;

private:
    uint32_t mdataCrc{0};
    FlowType mFlowType{FLOW_MEMORY};
    uint64_t mLength{0};
    std::vector<FlowAddr> mAddrs;
};

enum SliceState : uint8_t
{
    SLICE_VALID = 1,
    SLICE_INVALID = 2,
};

template <typename S>
class SliceRef {
public:
    explicit SliceRef(const S &slice) : mSlice(slice), mRef(0) {}

    inline bool Aquire()
    {
        bool isSucceed = false;
        std::lock_guard<std::mutex> lock(mSliceLock);
        if (mSlice != nullptr && mState == SLICE_VALID) {
            ++mRef;
            isSucceed = true;
        }
        return isSucceed;
    }

    inline void Release()
    {
        S execSlice = nullptr;
        SetSliceCallback execSliceCallback = nullptr;

        {
            std::lock_guard<std::mutex> lock(mSliceLock);
            --mRef;
            if (mRef == 0 && mSetSliceCallback != nullptr) {
                execSlice = mSlice;
                mSlice = mNewSlice;
                execSliceCallback = mSetSliceCallback;
                mNewSlice = nullptr;
                mSetSliceCallback = nullptr;
            }
        }

        if (execSliceCallback != nullptr) {
            execSliceCallback(execSlice);
        }
    }

    inline bool OpLock()
    {
        bool expectval = false;
        if (mOpLock.compare_exchange_weak(expectval, true)) {
            return true;
        }
        return false;
    }

    inline void OpUnLock()
    {
        mOpLock = false;
    }

    inline void SetState(SliceState state)
    {
        mState = state;
    }

    inline SliceState GetState() const
    {
        return mState;
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
        S execSlice = nullptr;
        SetSliceCallback execSliceCallback = nullptr;

        {
            std::lock_guard<std::mutex> lock(mSliceLock);
            if (mRef == 0) {
                execSlice = mSlice;
                mSlice = newSlice;
                execSliceCallback = setSliceCallback;
            } else {
                mNewSlice = newSlice;
                mSetSliceCallback = setSliceCallback;
            }
        }

        if (execSliceCallback != nullptr) {
            execSliceCallback(execSlice);
        }
    }
    DEFINE_REF_COUNT_FUNCTIONS;

private:
    std::mutex mSliceLock;
    S mSlice{nullptr};
    S mNewSlice{nullptr};
    SetSliceCallback mSetSliceCallback{nullptr};
    std::atomic<uint64_t> mRef{0};
    std::atomic<bool> mOpLock{false};
    std::atomic<SliceState> mState{SLICE_VALID};
    DEFINE_REF_COUNT_VARIABLE;
};
using SliceRefPtr = Ref<SliceRef<SlicePtr>>;
} // namespace bio
} // namespace ock
#endif // BOOSTIO_SLICE_H
