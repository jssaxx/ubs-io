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

#ifndef BOOSTIO_BIO_FLOW_INSTANCE_H
#define BOOSTIO_BIO_FLOW_INSTANCE_H

#include <memory>
#include <atomic>

#include "bio_ref.h"

namespace ock {
namespace bio {
class FlowInstance;
using FlowInstancePtr = Ref<FlowInstance>;
class FlowInstance {
public:
    FlowInstance(const uint64_t flowId, uint64_t version = 0, bool isDegrade = false)
        : mFlowId(flowId), mVersion(version), mIsDegrade(isDegrade), mIsNormal(true) {}
    FlowInstance() : mIsNormal(false) {}
    ~FlowInstance() = default;

    inline uint64_t FlowId() const
    {
        return mFlowId;
    }

    inline uint64_t Version() const
    {
        return mVersion;
    }

    inline bool IsDegrade() const
    {
        return mIsDegrade;
    }

    inline bool IsNormal() const
    {
        return mIsNormal.load();
    }

    inline void Update(uint64_t flowId, uint64_t version, bool isDegrade)
    {
        mFlowId = flowId;
        mVersion = version;
        mIsDegrade = isDegrade;
        mIsNormal = true;
    }

    inline uint64_t AllocOffset(uint64_t len, uint64_t &offset)
    {
        lock.Lock();
        offset = mIndex++;
        uint64_t outOffset = mOffset;
        mOffset += len;
        lock.UnLock();
        return outOffset;
    }

    inline void RollbackOffset(uint64_t len)
    {
        lock.Lock();
        mIndex--;
        mOffset -= len;
        lock.UnLock();
        return;
    }

    inline uint64_t GetOffset()
    {
        return mOffset;
    }

    inline uint64_t GetIndex()
    {
        return mIndex;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    uint64_t mFlowId{ 0 };
    uint64_t mVersion{ 0 };
    bool mIsDegrade{ false };
    std::atomic<bool> mIsNormal;
    uint64_t mIndex{ 0 };
    uint64_t mOffset{ 0 };
    SpinLock lock;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
