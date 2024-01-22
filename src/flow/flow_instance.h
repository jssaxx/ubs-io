/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
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
    FlowInstance(const uint64_t flowId) : mFlowId(flowId) {}
    ~FlowInstance() = default;

    uint64_t FlowId() const
    {
        return mFlowId;
    }

    uint64_t AllocOffset(uint64_t len, uint64_t &offset)
    {
        offset = mIndex.fetch_add(1);
        return mOffset.fetch_add(len);
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    uint64_t mFlowId;
    std::atomic<uint64_t> mIndex { 0 };
    std::atomic<uint64_t> mOffset { 0 };

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
