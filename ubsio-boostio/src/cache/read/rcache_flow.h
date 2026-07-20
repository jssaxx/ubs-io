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

#ifndef BOOSTIO_RCACHE_FLOW_H
#define BOOSTIO_RCACHE_FLOW_H

#include <cstdint>
#include "bio_ref.h"
#include "flow.h"
#include "flow_instance.h"

namespace ock {
namespace bio {
class RCacheFlow {
public:
    RCacheFlow();

    ~RCacheFlow();

    BResult Initialize(uint16_t ptId, uint16_t diskId, FlowType flowType, std::vector<uint64_t> flowIds);

    void Destroy();

    FlowPtr &GetMetaFlow();

    FlowPtr &GetDataFlow();

    FlowInstancePtr &GetMetaFlowInstance();

    FlowInstancePtr &GetDataFlowInstance();

    inline uint64_t GetMetaTruncOffset()
    {
        return mMetaTruncOffset;
    }

    inline void AddMetaTruncOffset(uint64_t len)
    {
        if (UINT64_MAX - mMetaTruncOffset < len) {
            return;
        }
        mMetaTruncOffset += len;
    }

    inline uint64_t GetDataTruncOffset()
    {
        return mDataTruncOffset;
    }

    inline void UpdateDataTruncOffset(uint64_t off, uint64_t len)
    {
        if (UINT64_MAX - off < len) {
            return;
        }
        mDataTruncOffset = off + len;
    }

    BResult AllocOffset(uint64_t len, uint64_t &offset, uint64_t &indexInFlow);

    DEFINE_REF_COUNT_FUNCTIONS
private:
    uint16_t mPtId = 0;
    uint16_t mDiskId = 0;

    FlowPtr mMetaFlow = nullptr;
    FlowInstancePtr mMetaFlowInstance = nullptr;
    uint64_t mMetaTruncOffset = 0;

    FlowPtr mDataFlow = nullptr;
    FlowInstancePtr mDataFlowInstance = nullptr;
    uint64_t mDataTruncOffset = 0;
    std::atomic<uint64_t> mindexInFlow{0};

    ReadWriteLock mLock;

    DEFINE_REF_COUNT_VARIABLE
};

using RCacheFlowPtr = Ref<RCacheFlow>;
} // namespace bio
} // namespace ock

#endif // BOOSTIO_RCACHE_FLOW_H
