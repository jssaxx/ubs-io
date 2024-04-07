/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_WCACHE_MANAGER_H
#define BOOSTIO_WCACHE_MANAGER_H

#include <atomic>
#include <unordered_map>
#include "bio_err.h"
#include "bio_ref.h"
#include "cache_def.h"
#include "slice.h"
#include "wcache.h"
#include "wcache_index.h"
#include "rcache_manager.h"

namespace ock {
namespace bio {
constexpr uint64_t WRITE_CACHE_EVICT_PERIOD = 5; // 5秒

class WCacheManager;
using WCacheManagerPtr = Ref<WCacheManager>;
class WCacheManager {
public:
    WCacheManager() = default;

    ~WCacheManager() = default;

    inline static WCacheManagerPtr &Instance()
    {
        static auto instance = MakeRef<WCacheManager>();
        return instance;
    }

    BResult Init(const RCacheManagerPtr &rCacheManager);

    void Exit();

public:
    BResult AllocateFlowId(uint16_t ptId, uint64_t ptv, uint64_t &flowId);

    BResult CreateWCache(uint64_t procId, uint64_t flowId, uint64_t ptId, uint64_t ptv, uint16_t diskId);

    BResult DeleteWCache(uint64_t flowId);

    BResult RecoverCache(FlowPtr metaFlow);

    BResult GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr,
        bool isDegrade);

    BResult Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
        uint64_t &realLen);

    BResult Stat(uint64_t ptId, const Key &key, CacheObjStat &cacheObjStat);

    BResult List(char *prefix, uint16_t ptId, std::unordered_map<std::string, CacheObjStat> &objs);

    BResult Delete(uint64_t ptId, const Key &key);

    void RegGetLocDiskStatus(GetLocDiskStatus getLocDiskStatus);

    void RegGetGlobEvictOffset(GetGlobEvictOffset evictOffset);

    void RegCheckLocRole(CheckLocRole localRole);

    BResult GetEvictOffset(uint64_t flowId, uint64_t &flowOffset);

    BResult Flush(uint64_t ptId, uint64_t ptv);

    BResult ExpiredClear(uint64_t ptId, uint64_t ptv);

    BResult HandleProcBroken(uint64_t procId);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    WCachePtr GetWCache(uint64_t flowId);
    BResult Read(uint64_t offset, const WCacheSlicePtr &srcSlice, const RCacheSlicePtr &destSlice,
        const SliceWriter &sliceWriter, uint64_t &realLen);
    BResult FlushImpl(uint64_t ptId, uint64_t ptv);
    BResult ExpiredClearImpl(uint64_t ptId, uint64_t ptv);
    BResult HandleProcBrokenHdl(uint64_t procId);
    BResult HandleProcBrokenImpl(uint64_t procId);

    void ScanOldCache(uint64_t ptId, uint64_t ptv, std::list<WCachePtr> &list);
    BResult ClearOldCache(uint64_t ptId, uint64_t ptv);

    void ScanProcCache(uint64_t procId, std::list<WCachePtr> &list);
    BResult ClearProcCache(uint32_t procId);

    void RetryEvictThread();
    void DestroyEvictThread();

private:
    ReadWriteLock mWCacheManagerLock;
    std::unordered_map<uint64_t, WCachePtr> mWCacheManager;
    std::vector<WCachePtr> mRetryManager[MAX_WCACHE_TIER];
    std::unordered_map<uint64_t, uint64_t> mDestroyManager;

    RCacheManagerPtr mRCacheManager;

    bool mRunning = true;
    ExecutorServicePtr mEvictService[MAX_WCACHE_TIER]{ nullptr, nullptr };
    ExecutorServicePtr mGcEvictService{ nullptr };
    ExecutorServicePtr mRetryEvictService{ nullptr };
    ExecutorServicePtr mDestroyEvictService{ nullptr };

    GetLocDiskStatus mGetLocDiskStatus{ nullptr };
    GetGlobEvictOffset mEvictOffset{ nullptr };
    CheckLocRole mLocRole{ nullptr };

    WCacheIndexPtr mCacheIndex;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}


#endif // BOOSTIO_WCACHE_MANAGER_H
