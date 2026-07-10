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

#ifndef BOOSTIO_BIO_FLOW_MANAGER_H
#define BOOSTIO_BIO_FLOW_MANAGER_H

#include <map>
#include <mutex>

#include "bio_ref.h"
#include "bio_err.h"
#include "bio_trace.h"
#include "bio_log.h"
#include "bio_types.h"

#include "cache_overload_ctrl.h"
#include "flow.h"
#include "flow_task_pool.h"

namespace ock {
namespace bio {
struct MemAllocator {
    std::function<BResult(uint64_t, uint64_t *)> alloc{ nullptr };
    std::function<void(uint64_t)> free{ nullptr };
};
struct DiskAllocator {
    std::function<BResult(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t *)> alloc{ nullptr };
    std::function<void(uint32_t, uint64_t, uint64_t)> free{ nullptr };
};
using GetCacheType = std::function<FlowCache(uint64_t flowId)>;

constexpr uint32_t NO_1MB = 1048576; // 1MB

class FlowManager;
using FlowManagerPtr = Ref<FlowManager>;
class FlowManager {
public:
    FlowManager() = default;
    ~FlowManager() = default;

    static FlowManagerPtr &Instance()
    {
        static auto instance = MakeRef<FlowManager>();
        return instance;
    }

    BResult Init();

    void Exit();

    FlowPtr CreateObject(FlowRole role, FlowType type, uint64_t flowId, uint32_t mediaId);

    BResult DestroyObject(FlowType type, uint64_t flowId);

    BResult GetAllObject(FlowType type, std::map<uint64_t, FlowPtr> &objManager);

    BResult PreLoadObject(FlowType type, std::function<void(void)> handle);

    static void RegisterMemAllocator(MemAllocator memAllocator)
    {
        mMemAllocator.alloc = memAllocator.alloc;
        mMemAllocator.free = memAllocator.free;
    }

    static void RegisterDiskAllocator(DiskAllocator diskAllocator)
    {
        mDiskAllocator.alloc = diskAllocator.alloc;
        mDiskAllocator.free = diskAllocator.free;
    }

    static void RegisterGetCacheType(GetCacheType getCacheType)
    {
        mGetCacheType = getCacheType;
    }

    static uint64_t GetCacheUsedSize(FlowCache cacheType, FlowType type, uint32_t mediaId)
    {
        ChkTrue((cacheType != FLOW_CACHE && type != FLOW_BUTT && mediaId < DEVICE_SIZE), NO_MAX_VALUE64,
            "Check Failed, cacheType:" << cacheType << " type:" << type);
        return mUsedSize[cacheType][type][mediaId];
    }

    static BResult MediaAlloc(FlowType type, uint32_t mediaId, uint64_t flowId, uint64_t flowOffset, uint64_t len,
        uint64_t *chunkId)
    {
        FlowCache cacheType = mGetCacheType(flowId);
        ChkTrue((cacheType != FLOW_CACHE && type != FLOW_BUTT && mediaId < DEVICE_SIZE), BIO_ERR,
            "Check Failed, cacheType:" << cacheType << " type:" << type);
        if (type == FLOW_MEMORY) {
            BIO_TRACE_START(MEM_TRACE_SEG_ALLOC);
            auto ret = mMemAllocator.alloc(len, chunkId);
            BIO_TRACE_END(MEM_TRACE_SEG_ALLOC, ret);
            if (ret == BIO_OK) {
                mUsedSize[cacheType][type][0] += len;
            }
            return ret;
        } else {
            BIO_TRACE_START(BDM_TRACE_SEG_ALLOC);
            auto ret = mDiskAllocator.alloc(mediaId, flowId, flowOffset, len, chunkId);
            BIO_TRACE_END(BDM_TRACE_SEG_ALLOC, ret);
            if (ret == BIO_OK) {
                mUsedSize[cacheType][type][mediaId] += len;
            }
            return ret;
        }
    }

    static void MediaFree(FlowRole role, FlowType type, uint32_t mediaId, uint64_t len, uint64_t chunkId,
        uint64_t flowId)
    {
        FlowCache cacheType = mGetCacheType(flowId);
        ChkTrueExNot((cacheType != FLOW_CACHE && type != FLOW_BUTT && mediaId < DEVICE_SIZE));
        if (type == FLOW_MEMORY) {
            BIO_TRACE_START(MEM_TRACE_SEG_FREE);
            mMemAllocator.free(chunkId);
            BIO_TRACE_END(MEM_TRACE_SEG_FREE, BIO_OK);
            mUsedSize[cacheType][type][0] -= len;
            // 内存资源释放触发quota资源释放.
            if (cacheType == FLOW_WCACHE && role == FLOW_DATA) {
                BIO_TRACE_START(WCACHE_TRACE_QUOTA_FREE);
                CacheOverloadCtrl::Instance().FreeQuota(len, 0);
                BIO_TRACE_END(WCACHE_TRACE_QUOTA_FREE, BIO_OK);
            }
        } else {
            BIO_TRACE_START(BDM_TRACE_SEG_FREE);
            mDiskAllocator.free(mediaId, len, chunkId);
            BIO_TRACE_END(BDM_TRACE_SEG_FREE, BIO_OK);
            mUsedSize[cacheType][type][mediaId] -= len;
        }
    }

    static void MediaRecover(FlowType type, uint32_t mediaId, uint64_t len, uint64_t flowId)
    {
        FlowCache cacheType = mGetCacheType(flowId);
        ChkTrueExNot((cacheType != FLOW_CACHE && type != FLOW_BUTT && mediaId < DEVICE_SIZE));
        if (type == FLOW_MEMORY) {
            mUsedSize[cacheType][type][0] += len;
        } else {
            mUsedSize[cacheType][type][mediaId] += len;
        }
    }

    DEFINE_REF_COUNT_FUNCTIONS

private:
    BResult Recover();
    BResult RecoverChunk(uint32_t mediaId, uint64_t chunkId, uint64_t flowId, uint64_t flowOffset);

private:
    std::map<uint64_t, FlowPtr> mMemObjManager;
    std::map<uint64_t, FlowPtr> mDiskObjManager;

    std::mutex mMutex;
    FlowTaskPoolPtr mTaskPool[FLOW_BUTT]{ nullptr, nullptr };

    static std::atomic<uint64_t> mUsedSize[FLOW_CACHE][FLOW_BUTT][DEVICE_SIZE];

    static MemAllocator mMemAllocator;
    static DiskAllocator mDiskAllocator;
    static GetCacheType mGetCacheType;

    bool mInited{ false };
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
