/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_CACHE_FLOW_H
#define BOOSTIO_CACHE_FLOW_H

#include <cstdint>

/* cache flow prefix generate rule:
|           ptId        |          type          |     innerType   |
|24         ...       12|11        ...          9|8      ...      0|

Flow id generate  rule, define flow.h：
|         prefix       |          flowInnerId         |
|64        ...       41|40           ...             0|
*/

namespace ock {
namespace bio {
constexpr uint32_t CACHE_FLOW_ID_PREFIX_PT_ID_SHIFT = 11;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_TYPE_SHIFT = 8;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_MASK = 0xFFFFFF;

constexpr uint16_t CACHE_FLOW_ID_PREFIX_TYPE_WCACHE = 0;
constexpr uint16_t CACHE_FLOW_ID_PREFIX_TYPE_RCACHE = 1;

class CacheFlowIdManager {
public:
    inline static uint32_t GenerateCacheFlowIdPrefix(uint16_t ptId, uint16_t type, uint16_t innerType)
    {
        return CACHE_FLOW_ID_PREFIX_MASK & (((static_cast<uint32_t>(ptId)) << CACHE_FLOW_ID_PREFIX_PT_ID_SHIFT) |
            ((static_cast<uint32_t>(type)) << CACHE_FLOW_ID_PREFIX_TYPE_SHIFT) | (static_cast<uint32_t>(innerType)));
    }

    inline static uint64_t GetPtId(uint64_t flowId)
    {
        return flowId >> (NO_64 - 13);
    }
};
}
}

#endif // BOOSTIO_CACHE_FLOW_H
