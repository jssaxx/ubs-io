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

            BResult Initialize(uint64_t ptId, FlowType flowType, std::vector<uint64_t> flowIds);

            BResult Destroy();

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

            inline void AddDataTruncOffset(uint64_t len)
            {
                mDataTruncOffset += len;
            }

            inline void AllocOffset(uint64_t len, uint64_t &offset, uint64_t &indexInFlow)
            {
                offset = mDataFlowInstance->AllocOffset(len, indexInFlow);
            }

            DEFINE_REF_COUNT_FUNCTIONS
        private:
            uint64_t mPtId;

            FlowPtr mMetaFlow;
            FlowInstancePtr mMetaFlowInstance;
            uint64_t mMetaTruncOffset;

            FlowPtr mDataFlow;
            FlowInstancePtr mDataFlowInstance;
            uint64_t mDataTruncOffset;
            std::atomic<uint64_t> mindexInFlow;

        DEFINE_REF_COUNT_VARIABLE
        };

        using RCacheFlowPtr = Ref<RCacheFlow>;
    }
}

#endif //BOOSTIO_RCACHE_FLOW_H
