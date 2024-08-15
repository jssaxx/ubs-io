/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_FLOW_TYPE_H
#define BOOSTIO_FLOW_TYPE_H

#include <cstdint>

namespace ock {
namespace bio {
enum FlowType : uint16_t {
    FLOW_MEMORY = 0,
    FLOW_DISK = 1,
    FLOW_BUTT
};

enum FlowRole : uint16_t {
    FLOW_META = 0,
    FLOW_DATA = 1,
    FLOW_ROLE_BUTT
};

enum FlowCache : uint16_t {
    FLOW_WCACHE = 0,
    FLOW_RCACHE = 1,
    FLOW_CACHE
};

struct MrInfo {
    uint64_t address;
    uint32_t size;
};

struct FlowAddr {
    uint64_t chunkId;
    uint32_t chunkOffset;
    uint32_t chunkLen;

    FlowAddr() = default;
    explicit FlowAddr(MrInfo &mrInfo) : chunkId(mrInfo.address), chunkOffset(0), chunkLen(mrInfo.size) {}
    FlowAddr(uint64_t inChunkId, uint32_t inChunkOffset, uint32_t inChunkLen)
        : chunkId(inChunkId), chunkOffset(inChunkOffset), chunkLen(inChunkLen)
    {}
    void ToMrInfo(MrInfo &mrInfo) const
    {
        mrInfo.address = chunkId + chunkOffset;
        mrInfo.size = chunkLen;
    }
};
}
}

#endif // BOOSTIO_FLOW_TYPE_H
