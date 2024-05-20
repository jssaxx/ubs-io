/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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

    BResult Initialize(uint64_t ptId, uint16_t diskId, FlowType flowType, std::vector<uint64_t> flowIds);

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
        mMetaTruncOffset += len;
    }

    inline uint64_t GetDataTruncOffset()
    {
        return mDataTruncOffset;
    }

    inline void UpdateDataTruncOffset(uint64_t off, uint64_t len)
    {
        mDataTruncOffset = off + len;
    }

    BResult AllocOffset(uint64_t len, uint64_t &offset, uint64_t &indexInFlow);

    DEFINE_REF_COUNT_FUNCTIONS
private:
    uint64_t mPtId = 0;
    uint16_t mDiskId = 0;

    FlowPtr mMetaFlow = nullptr;
    FlowInstancePtr mMetaFlowInstance = nullptr;
    uint64_t mMetaTruncOffset = 0;

    FlowPtr mDataFlow = nullptr;
    FlowInstancePtr mDataFlowInstance = nullptr;
    uint64_t mDataTruncOffset = 0;
    std::atomic<uint64_t> mindexInFlow{ 0 };

    ReadWriteLock mLock;

    DEFINE_REF_COUNT_VARIABLE
};

using RCacheFlowPtr = Ref<RCacheFlow>;
}
}

#endif // BOOSTIO_RCACHE_FLOW_H
