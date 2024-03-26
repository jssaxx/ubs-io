/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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
public:
    WCacheIndexTable() = default;
};
class WCacheIndex {
public:
    ~WCacheIndex();

    BResult Insert(uint64_t ptId, const Key &key, const WCacheSliceRefPtr &sliceRef);

    WCacheSliceRefPtr Aquire(uint64_t ptId, const Key &key);

    BResult FuzzyAquire(uint64_t ptId, const char *prefix, std::unordered_map<std::string, CacheObjStat> &objs);

    BResult Delete(uint64_t ptId, const Key &key, WCacheSliceRefPtr sliceRef);

    void ExpiredClear(uint64_t ptId);

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    WCacheIndexTable *GetIndexTable(uint64_t ptId);
    static uint32_t Hash(const Key &key);

private:
    ReadWriteLock mTableLock;
    std::unordered_map<uint64_t, WCacheIndexTable *> mTable;

    DEFINE_REF_COUNT_VARIABLE;
};
using WCacheIndexPtr = Ref<WCacheIndex>;
}
}


#endif // BOOSTIO_WCACHE_INDEX_H
