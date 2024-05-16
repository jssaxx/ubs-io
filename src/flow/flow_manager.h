/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
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

    FlowPtr CreateObject(FlowType type, uint64_t flowId, uint32_t mediaId);

    FlowPtr GetObject(FlowType type, uint64_t flowId);

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
            if (ret == 0) {
                mUsedSize[cacheType][type][0] += len;
                uint64_t useSize = mUsedSize[cacheType][type][0];
                if (cacheType == FLOW_WCACHE) {
                    LOG_INFO("WCACHE MEM: used:" << useSize / NO_1MB << ", flowId:" << flowId);
                } else {
                    LOG_INFO("RCACHE MEM: used:" << useSize / NO_1MB << ", flowId:" << flowId);
                }
            }
            return ret;
        } else {
            BIO_TRACE_START(BDM_TRACE_SEG_ALLOC);
            auto ret = mDiskAllocator.alloc(mediaId, flowId, flowOffset, len, chunkId);
            BIO_TRACE_END(BDM_TRACE_SEG_ALLOC, ret);
            if (ret == 0) {
                mUsedSize[cacheType][type][mediaId] += len;
                uint64_t useSize = mUsedSize[cacheType][type][mediaId];
                if (cacheType == 0) {
                    LOG_INFO("WCACHE DISK:" << mediaId << ", used:" << useSize / NO_1MB << ", flowId:" << flowId);
                } else {
                    LOG_INFO("RCACHE DISK:" << mediaId << ", used:" << useSize / NO_1MB << ", flowId:" << flowId);
                }
            }
            return ret;
        }
    }

    static void MediaFree(FlowType type, uint32_t mediaId, uint64_t len, uint64_t chunkId, uint64_t flowId)
    {
        FlowCache cacheType = mGetCacheType(flowId);
        ChkTrueExNot((cacheType != FLOW_CACHE && type != FLOW_BUTT && mediaId < DEVICE_SIZE));

        if (type == FLOW_MEMORY) {
            BIO_TRACE_START(MEM_TRACE_SEG_FREE);
            mMemAllocator.free(chunkId);
            BIO_TRACE_END(MEM_TRACE_SEG_FREE, 0);
            mUsedSize[cacheType][type][0] -= len;
            uint64_t useSize = mUsedSize[cacheType][type][0];
            if (cacheType == FLOW_WCACHE) {
                LOG_INFO("WCACHE MEM: used:" << useSize / NO_1MB << ", flowId:" << flowId);
            } else {
                LOG_INFO("RCACHE MEM: used:" << useSize / NO_1MB << ", flowId:" << flowId);
            }
        } else {
            BIO_TRACE_START(BDM_TRACE_SEG_FREE);
            mDiskAllocator.free(mediaId, len, chunkId);
            BIO_TRACE_END(BDM_TRACE_SEG_FREE, 0);
            mUsedSize[cacheType][type][mediaId] -= len;
            uint64_t useSize = mUsedSize[cacheType][type][mediaId];
            if (cacheType == 0) {
                LOG_INFO("WCACHE DISK:" << mediaId << ", used:" << useSize / NO_1MB << ", flowId:" << flowId);
            } else {
                LOG_INFO("RCACHE DISK:" << mediaId << ", used:" << useSize / NO_1MB << ", flowId:" << flowId);
            }
        }
    }

    static void MediaRecover(FlowType type, uint32_t mediaId, uint64_t len, uint64_t flowId)
    {
        FlowCache cacheType = mGetCacheType(flowId);
        ChkTrueExNot((cacheType != FLOW_CACHE && type != FLOW_BUTT && mediaId < DEVICE_SIZE));

        if (type == FLOW_MEMORY) {
            mUsedSize[cacheType][type][0] += len;
            uint64_t useSize = mUsedSize[cacheType][type][0];
            if (cacheType == FLOW_WCACHE) {
                LOG_INFO("WCACHE MEM: used:" << useSize / NO_1MB << ", flowId:" << flowId);
            } else {
                LOG_INFO("RCACHE MEM: used:" << useSize / NO_1MB << ", flowId:" << flowId);
            }
        } else {
            mUsedSize[cacheType][type][mediaId] += len;
            uint64_t useSize = mUsedSize[cacheType][type][mediaId];
            if (cacheType == 0) {
                LOG_INFO("WCACHE DISK:" << mediaId << ", used:" << useSize / NO_1MB << ", flowId:" << flowId);
            } else {
                LOG_INFO("RCACHE DISK:" << mediaId << ", used:" << useSize / NO_1MB << ", flowId:" << flowId);
            }
        }
    }

    DEFINE_REF_COUNT_FUNCTIONS;

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
