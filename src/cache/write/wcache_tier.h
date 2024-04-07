/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_WCACHE_TIER_H
#define BOOSTIO_WCACHE_TIER_H

#include "bio_err.h"
#include "bio_lock.h"
#include "cache_def.h"
#include "cache_slice.h"
#include "cache_slice_operator.h"
#include "flow.h"

namespace ock {
namespace bio {
struct WFlowSliceMeta {
    char key[NO_512];
    uint64_t offset;
    uint64_t length;
    uint64_t magic;
    uint64_t hasEvict;
};

enum WCacheTierType {
    WCACHE_MEMORY,
    WCACHE_DISK,
    MAX_WCACHE_TIER,
};

struct WFlowMetaDataSlice {
    WCacheSlicePtr metaSlice;
    WCacheSlicePtr dataSlice;
};

class WFlowTruncateCursor {
public:
    WCacheSlicePtr GetTruncateSlice(const WCacheSlicePtr &slice);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    uint64_t mPreTruncateSliceIndex = { 0 };

    std::mutex mEvictedSliceListLock;
    std::set<WCacheSlicePtr, WCacheSliceCmp> mEvictedSlices;
    DEFINE_REF_COUNT_VARIABLE;
};
using WFlowTruncateCursorPtr = Ref<WFlowTruncateCursor>;

class WCacheTier {
public:
    BResult Init(WCacheTierType cacheTier, uint64_t flowId, uint16_t diskId);

    WCacheSliceRefPtr Write(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader,
        CacheAttr &attr);

    void AddEvictQueue(WCacheSliceRefPtr sliceRef);

    void DelEvictQueue(WCacheSliceRefPtr sliceRef);

    void RetryEvictSliceQueue(std::list<WCacheSliceRefPtr>::iterator start, std::list<WCacheSliceRefPtr>::iterator end);

    BResult GetMetaSlice(uint64_t indexInFlow, WCacheSlicePtr &slice);

    BResult GetDataSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult GetMetaDataSlice(uint64_t indexInFlow, uint64_t offset, uint64_t length, WFlowMetaDataSlice &metaDataSlice);

    uint64_t GetMetaVirCapacity();

    uint64_t GetMetaEvictOffset();

    uint64_t GetDataCapacity();

    uint64_t GetDataVirCapacity();

    uint64_t GetDataEvictOffset();

    BResult Destroy();

    BResult Evict(const WCacheSlicePtr &slice);

    bool IsEmptyEvictSliceQueue();

    std::list<WCacheSliceRefPtr> GetEvictSliceQueue();

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult ToFlowType(WCacheTierType tier, FlowType &flowType);
    static WCacheSlicePtr GetSlice(const FlowPtr &flow, const SliceKey &sliceKey, BResult &ret);
    static WCacheSlicePtr GetSlice(const FlowPtr &flow, uint64_t offset, uint64_t index, uint64_t length, BResult &ret);

private:
    FlowPtr mMetaFlow;
    FlowPtr mDataFlow;

    CacheSliceOperator mSliceOperator;

    WFlowTruncateCursorPtr mFlowTruncateCursor;

    SpinLock mEvictSliceQueueLock;
    std::list<WCacheSliceRefPtr> mEvictSliceQueue;

    DEFINE_REF_COUNT_VARIABLE;
};
using WCacheTierPtr = Ref<WCacheTier>;
}
}

#endif // BOOSTIO_WCACHE_TIER_H
