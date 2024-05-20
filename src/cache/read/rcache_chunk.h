/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */


#ifndef BOOSTIO_RCACHE_CHUNK_H
#define BOOSTIO_RCACHE_CHUNK_H

#include <cstdint>
#include <new>
#include "bio_ref.h"
#include "flow.h"
#include "cache_def.h"

namespace ock {
namespace bio {
enum RCacheTierType {
    READ_CACHE_TIER_MEM = 0,
    READ_CACHE_TIER_DISK = 1,
    READ_CACHE_TIER_BUTT
};

enum MqType {
    MQ_COLD = 0,
    MQ_WARM = 1,
    MQ_HOT = 2,
    MQ_LOAD = 3,
    MQ_TYPE_BUTT
};

enum RCacheTrunkListType {
    RCACHE_TRUNK_LIST_TYPE_TRUNCATE = 0,
    RCACHE_TRUNK_LIST_TYPE_EVICT = 1,
    RCACHE_TRUNK_LIST_TYPE_BUTT
};

struct RCacheValue {
    uint64_t indexInFlow;
    uint64_t flowOffset;
    uint64_t length;

    RCacheValue(uint64_t index, uint64_t offset, uint64_t len) : indexInFlow(index), flowOffset(offset), length(len) {}

    inline std::string ToString()
    {
        std::ostringstream oss;
        oss << ", indexInFlow " << indexInFlow << ", flowOffset " << flowOffset << ", length " << length;
        return oss.str();
    }
};

class RCacheChunk;
using RCacheChunkPtr = Ref<RCacheChunk>;

class RCacheChunk {
public:
    RCacheChunk(Key key, RCacheValue &value);

    ~RCacheChunk();

    RCacheChunkPtr prev[RCACHE_TRUNK_LIST_TYPE_BUTT] = { nullptr };
    RCacheChunkPtr next[RCACHE_TRUNK_LIST_TYPE_BUTT] = { nullptr };

    inline Key GetKey()
    {
        return mKey;
    }

    inline RCacheValue &GetValue()
    {
        return mValue;
    }

    inline RCacheTierType GetTierType()
    {
        return tierType;
    }

    inline void SetTierType(RCacheTierType type)
    {
        tierType = type;
    }

    inline MqType GetMqType()
    {
        return mMqType;
    }

    inline void SetMqType(MqType type)
    {
        mMqType = type;
    }

    inline void SetValue(RCacheValue value)
    {
        mValue = value;
    }

    inline uint64_t GetState() const
    {
        return mState;
    }

    inline void SetState(uint64_t state)
    {
        mState = state;
    }

    std::mutex lock;

    inline std::string ToString()
    {
        std::ostringstream oss;
        oss << "chunk info, key " << mKey << ", tier " << tierType << ", mqType " << mMqType << ", hit " << hitCount <<
            mValue.ToString();
        return oss.str();
    }

    DEFINE_REF_COUNT_FUNCTIONS
private:
    Key mKey;
    RCacheValue mValue;
    uint32_t aTime;
    uint16_t hitCount{ 0 };
    RCacheTierType tierType;
    MqType mMqType;
    uint64_t mState{ 0 };

    DEFINE_REF_COUNT_VARIABLE
};
}
}

#endif // BOOSTIO_RCACHE_CHUNK_H
