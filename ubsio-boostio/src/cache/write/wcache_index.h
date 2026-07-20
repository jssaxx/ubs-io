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

#ifndef BOOSTIO_WCACHE_INDEX_H
#define BOOSTIO_WCACHE_INDEX_H

#include "cache_def.h"
#include "cache_slice.h"

namespace ock {
namespace bio {
constexpr uint32_t HASH_BUCKET_NUM = 1024;

struct WCacheIndexTable {
    ReadWriteLock sliceIndexLock[HASH_BUCKET_NUM];
    std::unordered_map<std::string, WCacheSliceRefPtr> sliceIndex[HASH_BUCKET_NUM];

    WCacheIndexTable() = default;
};

class WCacheIndex {
public:
    ~WCacheIndex();

    BResult Insert(uint16_t ptId, const Key &key, const WCacheSliceRefPtr &sliceRef);

    WCacheSliceRefPtr Aquire(uint16_t ptId, const Key &key);

    BResult FuzzyAquire(uint16_t ptId, const char *prefix, std::unordered_map<std::string, CacheObjStat> &objs);

    BResult Delete(uint16_t ptId, const Key &key, WCacheSliceRefPtr sliceRef);

    void ExpiredClear(uint16_t ptId);

    void Exit();

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    WCacheIndexTable *GetIndexTable(uint16_t ptId);
    static uint32_t Hash(const Key &key);

private:
    ReadWriteLock mTableLock;
    std::unordered_map<uint64_t, WCacheIndexTable *> mTable;

    DEFINE_REF_COUNT_VARIABLE;
};
using WCacheIndexPtr = Ref<WCacheIndex>;
} // namespace bio
} // namespace ock

#endif // BOOSTIO_WCACHE_INDEX_H
