/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include <thread>
#include <chrono>
#include "bio_tracepoint_helper.h"
#include "rcache_gc.h"

using namespace ock::bio;

RCacheGC::RCacheGC() : garbageData(0ULL), workStatus(false), workIndex(0)
{
    for (uint32_t i = 0; i < READ_CACHE_TIER_BUTT; i++) {
        for (uint32_t j = 0; j < READ_CACHE_GC_SERVICE_NUM; j++) {
            works[i][j] = nullptr;
        }
    }
}

RCacheGC::~RCacheGC() {}

BResult RCacheGC::GCOneRCacheHandle(RCachePtr rCache, RCacheTierType tier)
{
    return BIO_OK;
}

BResult RCacheGC::GcHandle(uint32_t index, RCacheTierType tier)
{
    GCRCacheLock[index].Lock();
    std::list<RCachePtr> list = GCRCache[index];
    GCRCacheLock[index].UnLock();

    for (auto iter = list.begin(); iter != list.end(); iter++) {
        RCachePtr rCache = *iter;
        BResult result = GCOneRCacheHandle(rCache, tier);
        if (result != BIO_OK) {
            LOG_ERROR("Gc handle read cache pt " << rCache->GetPtId() << " failed, error code" << result);
        }
    }

    return BIO_NEED_WAIT;
}

void *RCacheGC::Worker(void *context)
{
    RCacheGCWorkerParam *para = static_cast<RCacheGCWorkerParam *>(context);
    RCacheGCPtr rCacheGc = para->rCacheEvict;

    BResult result;
    while (rCacheGc->GetWorkStatus()) {
        result = rCacheGc->GcHandle(para->index, para->tier);
        if (result != BIO_NEED_WAIT && result != BIO_OK) {
            LOG_ERROR("Gc handle read cache index " << para->index << " tier " << para->tier <<
                " failed, error code " << result);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(READ_CACHE_GC_INTERVAL_MS));
    }

    delete para;
    return nullptr;
}

BResult RCacheGC::Initialize()
{
    RCacheGCWorkerParam *para = nullptr;

    workStatus.store(true);
    for (int32_t tier = 0; tier < READ_CACHE_TIER_BUTT; tier++) {
        for (uint32_t i = 0; i < READ_CACHE_GC_SERVICE_NUM; i++) {
            LVOS_TP_START(RCACHE_GC_PARAM_FAIL, 0);
            para = new (std::nothrow) RCacheGCWorkerParam();
            LVOS_TP_END;
            if (para == nullptr) {
                LOG_ERROR("Alloc read cache GC para memory failed");
                return BIO_ALLOC_FAIL;
            }

            para->tier = static_cast<RCacheTierType>(tier);
            para->index = i;
            para->rCacheEvict = this;
            std::thread *th = nullptr;
            LVOS_TP_START(RCACHE_GC_THREAD_FAIL, &th, nullptr);
            th = new (std::nothrow) std::thread(Worker, static_cast<void *>(para));
            LVOS_TP_END;
            if (th != nullptr) {
                pthread_setname_np(th->native_handle(), "GcWorker");
                works[tier][i] = th;
            } else {
                LOG_ERROR("Create thread for read cache GC failed");
                delete para;
                return BIO_ALLOC_FAIL;
            }
        }
    }

    return BIO_OK;
}

BResult RCacheGC::Start(RCachePtr rCachePtr)
{
    uint32_t index = rCachePtr->GetWorkIndex() % READ_CACHE_GC_SERVICE_MASK;
    GCRCacheLock[index].Lock();
    GCRCache[index].push_back(rCachePtr);
    GCRCacheLock[index].UnLock();
    return BIO_OK;
}

BResult RCacheGC::Stop(RCachePtr rCachePtr)
{
    uint32_t index = rCachePtr->GetWorkIndex() % READ_CACHE_GC_SERVICE_MASK;
    GCRCacheLock[index].Lock();
    auto iter = std::find(GCRCache[index].begin(), GCRCache[index].end(), rCachePtr);
    if (iter != GCRCache[index].end()) {
        GCRCache[index].erase(iter);
        GCRCacheLock[index].UnLock();
        return BIO_OK;
    }
    GCRCacheLock[index].UnLock();
    return BIO_NOT_EXISTS;
}

void RCacheGC::Destroy()
{
    workStatus.store(false);
    for (auto &work : works) {
        for (auto &th : work) {
            if (th) {
                th->join();
                delete th;
                th = nullptr;
            }
        }
    }
}
