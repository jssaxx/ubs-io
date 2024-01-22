/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include <thread>
#include <chrono>
#include "rcache_gc.h"

using namespace ock::bio;

RCacheGC::RCacheGC():garbageData{0ULL}, workStatus(false), workIndex(0)
{
}

RCacheGC::~RCacheGC()
{
}

BResult RCacheGC::GCOneRCacheHandle(RCachePtr rCache, RCacheTierType tier)
{
    return BIO_OK;
}

BResult RCacheGC::GcHandle(uint32_t index, RCacheTierType tier)
{
    BResult result;
    RCachePtr rCache = nullptr;

    for (auto iter = GCRCache[index].begin(); iter != GCRCache[index].end();iter++) {
        rCache = *iter;
        result = GCOneRCacheHandle(rCache, tier);
        if (result != BIO_OK) {
            LOG_ERROR("Gc handle read cache pt " << rCache->GetPtId() << "failed, error code" << result);
        }
    }

    return BIO_NEED_WAIT;
}

void *RCacheGC::Worker(void *context)
{
    RCacheGCWorkerParam *para = static_cast<RCacheGCWorkerParam*>(context);
    RCacheGCPtr rCacheGc = para->rCacheEvict;

    BResult result;
    while (rCacheGc->GetWorkStatus()) {
        result = rCacheGc->GcHandle(para->index, para->tier);
        if (result != BIO_NEED_WAIT && result != BIO_OK) {
            LOG_ERROR("Gc handle read cache index " << para->index << "tier " << para->tier << "failed, error code" << result);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(READ_CACHE_GC_INTERVAL_MS));
    }

    return nullptr;
}

BResult RCacheGC::Initialize()
{
    RCacheGCWorkerParam *para = nullptr;

    workStatus.store(true);

    for (int32_t tier = 0; tier < READ_CACHE_TIER_BUTT; tier++) {
        for (uint32_t i = 0; i < READ_CACHE_GC_SERVICE_NUM; i++) {
            para = new (std::nothrow) RCacheGCWorkerParam();
            if (para == nullptr) {
                LOG_ERROR("Alloc read cache GC para memory failed");
                return BIO_ALLOC_FAIL;
            }

            para->tier  = static_cast<RCacheTierType>(tier);
            para->index = i;
            para->rCacheEvict = this;
            auto *th = new std::thread(Worker, static_cast<void*>(para));
            if (th) {
                pthread_setname_np(th->native_handle(), "GcWorker");
                works[i] = th;
            } else {
                LOG_ERROR("Create thread for read cache GC failed");
                return BIO_ALLOC_FAIL;
            }
        }
    }

    return BIO_OK;
}

BResult RCacheGC::Start(RCachePtr rCachePtr)
{
    uint32_t index = (workIndex++) % READ_CACHE_GC_SERVICE_MASK;

    GCRCacheLock->Lock();
    GCRCache[index].push_back(rCachePtr);
    GCRCacheLock->UnLock();

    return BIO_OK;
}

BResult RCacheGC::Stop(RCachePtr rCachePtr)
{
    workStatus.store(false);

    for (int32_t tier = 0; tier < READ_CACHE_TIER_BUTT; tier++) {
        for (uint32_t i = 0; i < READ_CACHE_GC_SERVICE_NUM; i++) {
            works[i]->join();
        }
    }

    GCRCacheLock->Lock();
    for (uint32_t index = 0; index < READ_CACHE_GC_SERVICE_NUM; index++) {
        GCRCache[index].clear();
    }
    GCRCacheLock->UnLock();
    return BIO_OK;
}

BResult RCacheGC::Destroy()
{
    return BIO_OK;
}
