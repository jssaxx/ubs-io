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
constexpr uint64_t CACHE_FLOW_ID_INNER_MASK = 0xFFFFFFFFFF;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_SHIFT = 40;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_PT_ID_SHIFT = 11;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_TYPE_SHIFT = 8;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_MASK = 0xFFFFFF;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_TYPE_MASK = 0x7;
constexpr uint32_t CACHE_FLOW_ID_PREFIX_INNER_TYPE_MASK = 0xFF;

constexpr uint16_t CACHE_FLOW_ID_PREFIX_TYPE_WCACHE = 0;
constexpr uint16_t CACHE_FLOW_ID_PREFIX_TYPE_RCACHE = 1;

constexpr uint32_t WCACHE_FLOW_MEM_META_PREFIX  = 1;
constexpr uint32_t WCACHE_FLOW_MEM_DATA_PREFIX  = 2;
constexpr uint32_t WCACHE_FLOW_DISK_META_PREFIX = 3;
constexpr uint32_t WCACHE_FLOW_DISK_DATA_PREFIX = 4;

constexpr uint32_t RCACHE_FLOW_MEM_META_PREFIX  = 5;
constexpr uint32_t RCACHE_FLOW_MEM_DATA_PREFIX  = 6;
constexpr uint32_t RCACHE_FLOW_DISK_META_PREFIX = 7;
constexpr uint32_t RCACHE_FLOW_DISK_DATA_PREFIX = 8;

class CacheFlowIdManager {
public:
    inline static uint32_t GenerateCacheFlowIdPrefix(uint16_t ptId, uint16_t type, uint16_t innerType)
    {
        return CACHE_FLOW_ID_PREFIX_MASK & (((static_cast<uint32_t>(ptId)) << CACHE_FLOW_ID_PREFIX_PT_ID_SHIFT) |
            ((static_cast<uint32_t>(type)) << CACHE_FLOW_ID_PREFIX_TYPE_SHIFT) | (static_cast<uint32_t>(innerType)));
    }

    inline static uint64_t GetPtId(uint64_t flowId)
    {
        return flowId >> (CACHE_FLOW_ID_PREFIX_SHIFT + CACHE_FLOW_ID_PREFIX_PT_ID_SHIFT);
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
