/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_CACHE_H
#define BOOSTIO_BIO_CACHE_H

#include <atomic>
#include "bio.h"
#include "bio_err.h"
#include "cache_def.h"
#include "wcache_manager.h"
#include "rcache_manager.h"

namespace ock {
namespace bio {

class Cache {
public:
    BResult Init();

    void Exit();

    inline static Cache &Instance()
    {
        static Cache cacheInstance;
        return cacheInstance;
    }

    BResult RegisterCacheClient(uint64_t &cacheId);

    inline BResult AllocateFlowId(uint64_t cacheId, uint16_t ptId, uint64_t &flowId)
    {
        return mWCacheManager->AllocateFlowId(ptId, flowId);
    }

    BResult CreateWCache(uint64_t cacheId, uint64_t ptId, uint64_t ptv, uint16_t diskId, uint64_t flowId);

    BResult CreateRCache(uint64_t ptId, uint16_t diskId);

    BResult DeleteCache(uint64_t ptId);

    BResult GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr);

    BResult Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
                uint64_t &realLen);

    BResult Load(uint64_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen);

    BResult Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat);

    BResult Delete(uint64_t ptId, const Key &key);

    void RegGetGlobEvictOffset(GetGlobEvictOffset evictOffset);

    BResult GetEvictOffset(uint64_t flowId, uint64_t &flowOffset);

    BResult Flush(uint64_t ptId, uint64_t ptv);

    BResult ExpiredClear(uint64_t ptId, uint64_t ptv);

private:
    BResult Recover();

private:
    WCacheManagerPtr mWCacheManager{ nullptr };
    RCacheManagerPtr mRCacheManager{ nullptr };
};
}
}

#endif // BOOSTIO_BIO_CACHE_H
