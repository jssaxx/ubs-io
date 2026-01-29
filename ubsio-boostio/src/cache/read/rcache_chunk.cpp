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

#include "rcache_chunk.h"

using namespace ock::bio;

RCacheChunk::RCacheChunk(Key key, RCacheValue &value)
    : mKey(key), mValue(value), aTime(0), hitCount(0), tierType(READ_CACHE_TIER_BUTT), mMqType(MQ_TYPE_BUTT), mState(0)
{}

RCacheChunk::~RCacheChunk() = default;
