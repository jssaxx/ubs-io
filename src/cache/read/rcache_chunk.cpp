/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "rcache_chunk.h"

using namespace ock::bio;

RCacheChunk::RCacheChunk(Key key, RCacheValue &value):mKey(key),mValue(value)
{

}

RCacheChunk::~RCacheChunk()
{

}



