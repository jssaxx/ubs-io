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

#ifndef BOOSTIO_BIO_FLOW_ID_ALLOCATOR_H
#define BOOSTIO_BIO_FLOW_ID_ALLOCATOR_H

#include <atomic>
#include <memory>
#include <vector>

#include "bio_ref.h"

namespace ock {
namespace bio {
constexpr uint64_t FLOW_ID_MASK = 0xFFFFFFFF;
constexpr uint64_t FLOW_ID_SHIFT = 32UL;

class FlowIdAllocator;
using FlowIdAllocatorPtr = Ref<FlowIdAllocator>;
class FlowIdAllocator {
public:
    FlowIdAllocator() = default;
    ~FlowIdAllocator() = default;

    static FlowIdAllocatorPtr &Instance()
    {
        static auto instance = MakeRef<FlowIdAllocator>();
        return instance;
    }

    uint64_t GenerateFlowId(uint32_t prefix)
    {
        uint64_t innerFlowId = ++mNextFlowId;
        return (((static_cast<uint64_t>(prefix)) << FLOW_ID_SHIFT) | (innerFlowId & FLOW_ID_MASK));
    }

    void GenerateFlowIds(const std::vector<uint32_t> &prefix, std::vector<uint64_t> &flowIds)
    {
        uint64_t flowId;
        uint64_t innerFlowId = ++mNextFlowId;
        for (auto i : prefix) {
            flowId = (((static_cast<uint64_t>(i)) << FLOW_ID_SHIFT) | (innerFlowId & FLOW_ID_MASK));
            flowIds.push_back(flowId);
        }
    }

    void SyncFlowId(uint64_t flowId)
    {
        uint64_t innerFlowId = flowId & FLOW_ID_MASK;
        if (mNextFlowId < innerFlowId) {
            mNextFlowId = innerFlowId;
        }
    }

    uint32_t GetPrefixFormFlowId(uint64_t flowId)
    {
        return static_cast<uint32_t>(flowId >> FLOW_ID_SHIFT);
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    std::atomic<uint64_t> mNextFlowId{ 0 };
    DEFINE_REF_COUNT_VARIABLE;
};
}
}

#endif // BOOSTIO_BIO_FLOW_ID_ALLOCATOR_H
