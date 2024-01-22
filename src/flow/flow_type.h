/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_FLOW_TYPE_H
#define BOOSTIO_FLOW_TYPE_H

#include <cstdint>

namespace ock {
namespace bio {
enum FlowType : uint8_t {
    FLOW_MEMORY = 1,
    FLOW_DISK = 2,
};

struct MrInfo {
    uint64_t address;
    uint32_t size;
};

struct FlowAddr {
    uint64_t chunkId;
    uint32_t chunkOffset;
    uint32_t chunkLen;
public:
    FlowAddr() = default;
    FlowAddr(MrInfo &mrInfo)
        : chunkId(mrInfo.address), chunkOffset(0), chunkLen(mrInfo.size)
    {}
    FlowAddr(uint64_t inChunkId, uint32_t inChunkOffset, uint32_t inChunkLen)
        : chunkId(inChunkId), chunkOffset(inChunkOffset), chunkLen(inChunkLen)
    {}
    void ToMrInfo(MrInfo &mrInfo)
    {
        mrInfo.address = chunkId + chunkOffset;
        mrInfo.size = chunkLen;
    }
};
}
}

#endif // BOOSTIO_FLOW_TYPE_H
