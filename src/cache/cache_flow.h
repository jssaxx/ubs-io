/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_CACHE_FLOW_H
#define BOOSTIO_CACHE_FLOW_H

#include "cache_def.h"

/* cache flow prefix generate rule:
|           ptId        |          type          |     innerType   |
|24         ...       12|11        ...          9|8      ...      0|

Flow id generate  rule, define flow.h：
|         prefix       |          flowInnerId         |
|64        ...       41|40           ...             0|
*/

namespace ock {
namespace bio {
constexpr uint64_t CACHE_FLOW_ID_INNER_MASK = 0xFFFFFFFF;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_SHIFT = 32;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_PT_ID_SHIFT = 19;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_PT_SN_SHIFT = 8;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_TYPE_SHIFT = 4;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_MASK = 0xFFFFFFFF;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_PT_ID_MASK = 0x1FFF;   // 13
constexpr uint32_t CACHE_FLOW_ID_PREFIX_PT_SN_MASK = 0x7FF;    // 11
constexpr uint32_t CACHE_FLOW_ID_PREFIX_TYPE_MASK = 0xF;       // 4
constexpr uint32_t CACHE_FLOW_ID_PREFIX_INNER_TYPE_MASK = 0xF; // 4

constexpr uint32_t WCACHE_FLOW_MEM_META_PREFIX = 1;
constexpr uint32_t WCACHE_FLOW_MEM_DATA_PREFIX = 2;
constexpr uint32_t WCACHE_FLOW_DISK_META_PREFIX = 3;
constexpr uint32_t WCACHE_FLOW_DISK_DATA_PREFIX = 4;

constexpr uint32_t RCACHE_FLOW_MEM_META_PREFIX = 5;
constexpr uint32_t RCACHE_FLOW_MEM_DATA_PREFIX = 6;
constexpr uint32_t RCACHE_FLOW_DISK_META_PREFIX = 7;
constexpr uint32_t RCACHE_FLOW_DISK_DATA_PREFIX = 8;

class CacheFlowIdManager {
public:
    inline static uint32_t GenerateCacheFlowIdPrefix(uint16_t ptId, uint64_t ptv, uint16_t type, uint16_t innerType)
    {
        uint32_t ptIdP = ptId & CACHE_FLOW_ID_PREFIX_PT_ID_MASK;
        uint32_t ptvP = static_cast<uint32_t>(ptv) & CACHE_FLOW_ID_PREFIX_PT_SN_MASK;
        uint32_t typeP = type & CACHE_FLOW_ID_PREFIX_TYPE_MASK;
        uint32_t innerTypeP = innerType & CACHE_FLOW_ID_PREFIX_INNER_TYPE_MASK;

        return (ptIdP << CACHE_FLOW_ID_PREFIX_PT_ID_SHIFT) | (ptvP << CACHE_FLOW_ID_PREFIX_PT_SN_SHIFT) |
            (typeP << CACHE_FLOW_ID_PREFIX_TYPE_SHIFT) | (innerTypeP);
    }

    inline static uint16_t GetPtId(uint64_t flowId)
    {
        return static_cast<uint16_t>(flowId >> (CACHE_FLOW_ID_PREFIX_SHIFT + CACHE_FLOW_ID_PREFIX_PT_ID_SHIFT));
    }
    inline static uint64_t GetType(uint64_t flowId)
    {
        return (flowId >> (CACHE_FLOW_ID_PREFIX_SHIFT + CACHE_FLOW_ID_PREFIX_TYPE_SHIFT)) &
            CACHE_FLOW_ID_PREFIX_TYPE_MASK;
    }
    inline static uint64_t GetInnerType(uint64_t flowId)
    {
        return (flowId >> CACHE_FLOW_ID_PREFIX_SHIFT) & CACHE_FLOW_ID_PREFIX_INNER_TYPE_MASK;
    }
    inline static uint64_t GenOutFlowId(uint64_t flowId)
    {
        uint64_t innerFlowId = flowId & CACHE_FLOW_ID_INNER_MASK;
        uint64_t prefix = flowId >> (CACHE_FLOW_ID_PREFIX_SHIFT + CACHE_FLOW_ID_PREFIX_TYPE_SHIFT);
        uint64_t outPrefix = prefix << (CACHE_FLOW_ID_PREFIX_SHIFT + CACHE_FLOW_ID_PREFIX_TYPE_SHIFT);
        return (outPrefix | innerFlowId);
    }
};
}
}

#endif // BOOSTIO_CACHE_FLOW_H
