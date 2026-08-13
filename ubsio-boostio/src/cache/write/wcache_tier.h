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

#ifndef BOOSTIO_WCACHE_TIER_H
#define BOOSTIO_WCACHE_TIER_H

#include <array>

#include "bio_err.h"
#include "bio_lock.h"
#include "cache_def.h"
#include "cache_slice.h"
#include "cache_slice_operator.h"
#include "flow.h"

namespace ock {
namespace bio {
struct WFlowSliceMeta {
    char key[NO_512 - NO_32]; // 512对齐，走DIRECT IO
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

    BResult Write(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader,
        WCacheSliceRefPtr &destSliceRef);

    void AddEvictQueue(WCacheSliceRefPtr sliceRef);

    void AddEvictNegotiateIndexMap(uint64_t indexInMap, uint8_t refNum);

    void DelEvictNegotiateQueue(WCacheReplicaSlicePtr repSlicePtr);

    void AddEvictNegotiateMap(WCacheSliceRefPtr &sliceRef);

    void DelEvictNegotiateMap(WCacheReplicaSlicePtr repSlicePtr);

    void RetryEvictQueue(WCacheSliceRefPtr sliceRef);

    void DelEvictQueue(WCacheSliceRefPtr sliceRef);

    std::map<uint64_t, std::array<uint8_t, NO_256>> *GetEvictMapPtr()
    {
        return &mNegotiateIndexMap;
    }

    inline void NegotiateIndexMapLockRead()
    {
        mNegotiateIndexMapLock.LockRead();
    }

    inline void NegotiateIndexMapUnLock()
    {
        mNegotiateIndexMapLock.UnLock();
    }

    BResult GetMetaSlice(uint64_t indexInFlow, WCacheSlicePtr &slice);

    BResult GetDataSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice);

    BResult GetMetaDataSlice(uint64_t indexInFlow, uint64_t offset, uint64_t length, WFlowMetaDataSlice &metaDataSlice);

    uint64_t GetMetaVirCapacity();

    uint64_t GetMetaEvictOffset();

    uint64_t GetDataCapacity();

    uint64_t GetDataVirCapacity();

    uint64_t GetDataEvictOffset();

    BResult Seal();

    void Destroy();

    BResult Evict(const WCacheSlicePtr &slice);

    bool IsEmptyEvictSliceQueue();

    bool IsEmptyNegotiateMap();

    WCacheSliceRefPtr GetEvictSlice();

    void GetNegotiateSlice(std::vector<uint64_t> &offsetVec, uint32_t limit);

    BResult UpdateNegotiateState(uint64_t indexInflow);
    void FlushNegotiateMap();
    void EvictNegotiateMapToQueue(uint64_t indexInFlow);
    void DelEvictIndexArray(uint64_t indexInMap);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult ToFlowType(WCacheTierType tier, FlowType &flowType);
    static BResult GetSlice(const FlowPtr &flow, const SliceKey &sliceKey, WCacheSlicePtr &slice);
    static BResult GetSlice(const FlowPtr &flow, uint64_t offset, uint64_t index, uint64_t length,
        WCacheSlicePtr &slice);

private:
    uint8_t INVALID_REF_NUM = NO_U8_255;
    static constexpr uint32_t ARRAY_SIZE_IN_NEGOTIATE_MAP = NO_256;
    WCacheTierType type;
    FlowPtr mMetaFlow;
    FlowPtr mDataFlow;

    CacheSliceOperator mSliceOperator;
    WFlowTruncateCursorPtr mFlowTruncateCursor;

    SpinLock mEvictSliceQueueLock;
    std::list<WCacheSliceRefPtr> mEvictSliceQueue;

    SpinLock mEvictNegotiateMapLock;
    std::unordered_map<uint64_t, WCacheSliceRefPtr> mEvictNegotiateMap;

    ReadWriteLock mNegotiateIndexMapLock;
    std::map<uint64_t, std::array<uint8_t, ARRAY_SIZE_IN_NEGOTIATE_MAP>> mNegotiateIndexMap;
    uint64_t mCurNegotiateIndex = { 0 };

    DEFINE_REF_COUNT_VARIABLE;
};
using WCacheTierPtr = Ref<WCacheTier>;
}
}

#endif // BOOSTIO_WCACHE_TIER_H
