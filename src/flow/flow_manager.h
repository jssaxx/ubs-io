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

    BResult Exit();

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

    static uint64_t GetCacheUsedSize(uint64_t cacheType, FlowType type)
    {
        if (type == FLOW_MEMORY) {
            return mUsedRes[cacheType][0];
        } else {
            return mUsedRes[cacheType][NO_1];
        }
    }

    static uint64_t GetCacheType(uint64_t flowId)
    {
        return (flowId >> (NO_32 + NO_4)) & (NO_16 - NO_1);
    }

    static BResult MediaAlloc(FlowType type, uint32_t mediaId, uint64_t flowId, uint64_t flowOffset, uint64_t len,
        uint64_t *chunkId)
    {
        uint64_t cacheType = GetCacheType(flowId);
        if (type == FLOW_MEMORY) {
            BIO_TRACE_START(MEM_TRACE_SEG_ALLOC);
            auto ret = mMemAllocator.alloc(len, chunkId);
            BIO_TRACE_END(MEM_TRACE_SEG_ALLOC, ret);
            if (ret == 0) {
                if (cacheType == 0) {
                    mUsedRes[0][0] += len;
                    LOG_INFO("WCACHE MEM: cur used:" << mUsedRes[0][0] / NO_1048576 << ", flowId:" << flowId);
                }
                if (cacheType == NO_1) {
                    mUsedRes[NO_1][0] += len;
                    LOG_INFO("RCACHE MEM: cur used:" << mUsedRes[NO_1][0] / NO_1048576 << ", flowId:" << flowId);
                }
            }
            return ret;
        } else {
            BIO_TRACE_START(BDM_TRACE_SEG_ALLOC);
            auto ret = mDiskAllocator.alloc(mediaId, flowId, flowOffset, len, chunkId);
            BIO_TRACE_END(BDM_TRACE_SEG_ALLOC, ret);
            if (ret == 0) {
                if (cacheType == 0) {
                    mUsedRes[0][NO_1] += len;
                    LOG_INFO("WCACHE DISK: cur used:" << mUsedRes[0][NO_1] / NO_1048576 << ", flowId:" << flowId);
                }
                if (cacheType == NO_1) {
                    mUsedRes[NO_1][NO_1] += len;
                    LOG_INFO("RCACHE DISK: cur used:" << mUsedRes[NO_1][NO_1] / NO_1048576 << ", flowId:" << flowId);
                }
            }
            return ret;
        }
    }

    static void MediaFree(FlowType type, uint32_t mediaId, uint64_t len, uint64_t chunkId, uint64_t flowId)
    {
        uint64_t cacheType = GetCacheType(flowId);
        if (type == FLOW_MEMORY) {
            BIO_TRACE_START(MEM_TRACE_SEG_FREE);
            mMemAllocator.free(chunkId);
            BIO_TRACE_END(MEM_TRACE_SEG_FREE, 0);
            if (cacheType == 0) {
                mUsedRes[0][0] -= len;
                LOG_INFO("WCACHE MEM: cur used:" << mUsedRes[0][0] / NO_1048576 << ", flowId:" << flowId);
            }
            if (cacheType == NO_1) {
                mUsedRes[NO_1][0] -= len;
                LOG_INFO("RCACHE MEM: cur used:" << mUsedRes[NO_1][0] / NO_1048576 << ", flowId:" << flowId);
            }
        } else {
            BIO_TRACE_START(BDM_TRACE_SEG_FREE);
            mDiskAllocator.free(mediaId, len, chunkId);
            BIO_TRACE_END(BDM_TRACE_SEG_FREE, 0);
            if (cacheType == 0) {
                mUsedRes[0][NO_1] -= len;
                LOG_INFO("WCACHE DISK: cur used:" << mUsedRes[0][NO_1] / NO_1048576 << ", flowId:" << flowId);
            }
            if (cacheType == NO_1) {
                mUsedRes[NO_1][NO_1] += len;
                LOG_INFO("RCACHE DISK: cur used:" << mUsedRes[NO_1][NO_1] / NO_1048576 << ", flowId:" << flowId);
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

    static std::atomic<uint64_t> mUsedRes[NO_2][NO_2];

    static MemAllocator mMemAllocator;
    static DiskAllocator mDiskAllocator;

    bool mInited{ false };
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
