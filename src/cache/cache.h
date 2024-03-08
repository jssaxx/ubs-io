/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_CACHE_H
#define BOOSTIO_BIO_CACHE_H

#include <atomic>
#include "bio_err.h"
#include "cache_def.h"
#include "wcache_manager.h"
#include "rcache_manager.h"

namespace ock {
namespace bio {

class Cache {
public:
    BResult Init();

    BResult Recover();

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

    BResult CreateWCache(uint64_t procId, uint64_t ptId, uint64_t ptv, uint16_t diskId, uint64_t flowId);

    BResult CreateRCache(uint64_t ptId, uint16_t diskId);

    BResult DeleteCache(uint64_t ptId);

    BResult GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr);

    BResult Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
                uint64_t &realLen);

    BResult Load(uint64_t ptId, const Key &key, uint64_t offset, uint64_t len, uint64_t &realLen);

    BResult Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat);

    BResult List(char *prefix, uint16_t ptId, uint32_t flag, std::unordered_map<std::string, CacheObjStat> &objs);

    BResult Delete(uint64_t ptId, const Key &key);

    void RegGetLocDiskId(GetLocDiskId getLocDiskId);

    void RegCheckDegrade(CheckDegrade checkDegrade);

    void RegGetGlobEvictOffset(GetGlobEvictOffset evictOffset);

    void RegCheckLocRole(CheckLocRole locRole);

    BResult HandleProcBroken(uint32_t procId);

    BResult GetEvictOffset(uint64_t flowId, uint64_t &flowOffset);

    BResult Flush(uint64_t ptId, uint64_t ptv);

    BResult ExpiredClear(uint64_t ptId, uint64_t ptv, bool retained);

private:
    BResult ExtraCreateRCache(uint64_t ptId);

    BResult PutByPass(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr);

private:
    WCacheManagerPtr mWCacheManager{ nullptr };
    RCacheManagerPtr mRCacheManager{ nullptr };
    GetLocDiskId mGetLocDiskId{ nullptr };
    CheckDegrade mCheckDegrade{ nullptr };
};
}
}

#endif // BOOSTIO_BIO_CACHE_H
