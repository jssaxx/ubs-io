/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#ifndef BOOSTIO_BIO_FLOW_MANAGER_H
#define BOOSTIO_BIO_FLOW_MANAGER_H

#include <map>
#include <mutex>

#include "bio_ref.h"
#include "bio_err.h"

#include "flow.h"
#include "flow_task_pool.h"

namespace ock {
namespace bio {
struct MemAllocator {
    std::function<BResult(uint64_t, uint64_t *)> alloc { nullptr };
    std::function<void(uint64_t)> free { nullptr };
};
struct DiskAllocator {
    std::function<BResult(uint32_t, uint64_t, uint64_t, uint64_t *)> alloc { nullptr };
    std::function<void(uint32_t, uint64_t, uint64_t)> free { nullptr };
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

    FlowPtr CreateObject(FlowType type, uint64_t flowId);

    BResult DestroyObject(FlowType type, uint64_t flowId);

    BResult GetAllObject(FlowType type, std::map<uint64_t, FlowPtr> &objManager);

    BResult PreLoadObject(std::function<void(void)> handle);

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

    static BResult MediaAlloc(FlowType type, uint32_t mediaId, uint64_t bucketId, uint64_t len, uint64_t *chunkId)
    {
        if (type == FLOW_MEMORY) {
            return mMemAllocator.alloc(len, chunkId);
        } else {
            return mDiskAllocator.alloc(mediaId, bucketId, len, chunkId);
        }
    }

    static void MediaFree(FlowType type, uint32_t mediaId, uint64_t len, uint64_t chunkId)
    {
        if (type == FLOW_MEMORY) {
            mMemAllocator.free(chunkId);
        } else {
            mDiskAllocator.free(mediaId, len, chunkId);
        }
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult Recover();
    BResult RecoverChunk(uint32_t mediaId, uint64_t chunkId, uint64_t flowId);

private:
    std::map<uint64_t, FlowPtr> mMemObjManager;
    std::map<uint64_t, FlowPtr> mDiskObjManager;

    std::mutex mMutex;

    FlowTaskPoolPtr mTaskPool { nullptr };

    static MemAllocator mMemAllocator;
    static DiskAllocator mDiskAllocator;

    bool mInited { false };
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
