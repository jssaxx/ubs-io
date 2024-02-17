/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "rcache_chunk.h"

using namespace ock::bio;

RCacheChunk::RCacheChunk(Key key, RCacheValue &value)
    : mKey(key), mValue(value), aTime(0), hitCount(0), tierType(READ_CACHE_TIER_BUTT), mMqType(MQ_TYPE_BUTT), mState(0)
{
}

RCacheChunk::~RCacheChunk() = default;



