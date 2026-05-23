/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "mms_notify.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <utility>

#include "mms_def.h"
#include "mms_log.h"
#include "securec.h"

namespace ock {
namespace mms {

MmsNotifyDispatcher &MmsNotifyDispatcher::Instance()
{
    static MmsNotifyDispatcher instance;
    return instance;
}

MmsNotifyDispatcher::~MmsNotifyDispatcher()
{
    Stop();
}

CResult MmsNotifyDispatcher::RegisterCallback(NotifyCallback callback)
{
    std::lock_guard<std::mutex> lifecycleLock(mLifecycleMutex);
    if (callback == nullptr) {
        mCallback = nullptr;
        StopWorkerIfIdleLocked();
        return RET_MMS_OK;
    }

    if (mRunning.load(std::memory_order_acquire)) {
        if (mCallback == nullptr) {
            mCallback = callback;
            return RET_MMS_OK;
        }
        return (mCallback == callback) ? RET_MMS_OK : RET_MMS_EPERM;
    }

    mCallback = callback;
    if (!StartWorkerLocked()) {
        mCallback = nullptr;
        return RET_MMS_ERROR;
    }
    return RET_MMS_OK;
}

CResult MmsNotifyDispatcher::RegisterRemoteNotifyHandler(RemoteNotifyHandler handler)
{
    std::lock_guard<std::mutex> lifecycleLock(mLifecycleMutex);
    mRemoteNotifyHandler = std::move(handler);
    if (mRemoteNotifyHandler == nullptr) {
        StopWorkerIfIdleLocked();
        return RET_MMS_OK;
    }

    if (mRunning.load(std::memory_order_acquire)) {
        return RET_MMS_OK;
    }
    if (!StartWorkerLocked()) {
        mRemoteNotifyHandler = nullptr;
        return RET_MMS_ERROR;
    }
    return RET_MMS_OK;
}

void MmsNotifyDispatcher::Notify(const char *key, OperateType opType)
{
    if (UNLIKELY(key == nullptr || !mRunning.load(std::memory_order_acquire) ||
                 mStop.load(std::memory_order_acquire))) {
        return;
    }

    size_t keyLen = strnlen(key, MAX_KEY_SIZE);
    if (UNLIKELY(keyLen == MAX_KEY_SIZE)) {
        LOG_ERROR("Copy notify key failed, key:" << key << ".");
        return;
    }

    if (UNLIKELY(mOverflowActive.load(std::memory_order_acquire))) {
        auto enqueueRet = EnqueueOverflow(key, keyLen, opType);
        if (UNLIKELY(enqueueRet != EnqueueResult::SUCCESS)) {
            LOG_ERROR("Notify overflow enqueue failed, key:" << key << ".");
        }
        return;
    }

    while (true) {
        auto enqueueRet = TryEnqueue(key, keyLen, opType);
        if (LIKELY(enqueueRet == EnqueueResult::SUCCESS)) {
            return;
        }
        if (UNLIKELY(enqueueRet == EnqueueResult::FULL)) {
            uint64_t fullCount = mQueueFullCount.fetch_add(NO_1, std::memory_order_relaxed) + NO_1;
            LOG_ERROR("Server notify queue full, count:" << fullCount << ", key:" << key << ".");
            enqueueRet = EnqueueOverflow(key, keyLen, opType);
            if (UNLIKELY(enqueueRet != EnqueueResult::SUCCESS)) {
                LOG_ERROR("Notify overflow enqueue failed, key:" << key << ".");
            }
            return;
        }
        if (UNLIKELY(enqueueRet == EnqueueResult::ERROR)) {
            LOG_ERROR("Notify enqueue failed, key:" << key << ".");
            return;
        }
    }
}

void MmsNotifyDispatcher::Stop()
{
    std::lock_guard<std::mutex> lifecycleLock(mLifecycleMutex);
    StopWorker();
}

bool MmsNotifyDispatcher::StartWorkerLocked()
{
    try {
        if (mQueue == nullptr) {
            mQueue.reset(new NotifyCell[QUEUE_CAPACITY]);
        }
        ResetQueue();
        mStop.store(false, std::memory_order_release);
        mWorker = std::thread(&MmsNotifyDispatcher::WorkerLoop, this);
        mRunning.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception &ex) {
        LOG_ERROR("Start notify worker failed, error:" << ex.what() << ".");
    } catch (...) {
        LOG_ERROR("Start notify worker failed.");
    }
    return false;
}

void MmsNotifyDispatcher::StopWorkerIfIdleLocked()
{
    if (mCallback != nullptr || mRemoteNotifyHandler != nullptr) {
        return;
    }
    StopWorker();
}

void MmsNotifyDispatcher::StopWorker()
{
    std::thread worker;
    if (!mRunning.load(std::memory_order_acquire)) {
        return;
    }

    mRunning.store(false, std::memory_order_release);
    mStop.store(true, std::memory_order_release);
    worker = std::move(mWorker);

    if (worker.joinable()) {
        worker.join();
    }

    mStop.store(false, std::memory_order_release);
}

void MmsNotifyDispatcher::ResetQueue()
{
    mEnqueuePos.store(0, std::memory_order_relaxed);
    mDequeuePos.store(0, std::memory_order_relaxed);
    if (mQueue == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mOverflowMutex);
        mOverflowQueue.clear();
    }
    mOverflowActive.store(false, std::memory_order_release);
    for (uint32_t index = 0; index < QUEUE_CAPACITY; ++index) {
        mQueue[index].sequence.store(index, std::memory_order_relaxed);
    }
}

MmsNotifyDispatcher::EnqueueResult MmsNotifyDispatcher::TryEnqueue(const char *key, size_t keyLen, OperateType opType)
{
    size_t pos = mEnqueuePos.load(std::memory_order_relaxed);
    NotifyCell *cell = nullptr;
    while (true) {
        cell = &mQueue[pos & QUEUE_MASK];
        size_t seq = cell->sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
        if (diff == 0) {
            if (mEnqueuePos.compare_exchange_weak(pos, pos + NO_1, std::memory_order_relaxed)) {
                break;
            }
            continue;
        }
        if (diff < 0) {
            return EnqueueResult::FULL;
        }
        pos = mEnqueuePos.load(std::memory_order_relaxed);
    }

    auto ret = memcpy_s(cell->event.key, MAX_KEY_SIZE, key, keyLen + NO_1);
    if (UNLIKELY(ret != EOK)) {
        cell->event.key[0] = '\0';
        cell->event.opType = opType;
        cell->sequence.store(pos + NO_1, std::memory_order_release);
        return EnqueueResult::ERROR;
    }
    cell->event.opType = opType;
    cell->sequence.store(pos + NO_1, std::memory_order_release);
    return EnqueueResult::SUCCESS;
}

MmsNotifyDispatcher::EnqueueResult MmsNotifyDispatcher::EnqueueOverflow(const char *key, size_t keyLen,
                                                                        OperateType opType)
{
    NotifyEvent event{};
    auto ret = memcpy_s(event.key, MAX_KEY_SIZE, key, keyLen + NO_1);
    if (UNLIKELY(ret != EOK)) {
        return EnqueueResult::ERROR;
    }
    event.opType = opType;

    try {
        std::lock_guard<std::mutex> lock(mOverflowMutex);
        mOverflowActive.store(true, std::memory_order_release);
        mOverflowQueue.emplace_back(event);
    } catch (const std::exception &ex) {
        LOG_ERROR("Notify overflow enqueue failed, error:" << ex.what() << ".");
        return EnqueueResult::ERROR;
    } catch (...) {
        LOG_ERROR("Notify overflow enqueue failed.");
        return EnqueueResult::ERROR;
    }
    return EnqueueResult::SUCCESS;
}

bool MmsNotifyDispatcher::TryDequeue(NotifyEvent &event)
{
    size_t pos = mDequeuePos.load(std::memory_order_relaxed);
    NotifyCell *cell = &mQueue[pos & QUEUE_MASK];
    size_t seq = cell->sequence.load(std::memory_order_acquire);
    intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + NO_1);
    if (diff != 0) {
        return false;
    }

    mDequeuePos.store(pos + NO_1, std::memory_order_relaxed);
    event = cell->event;
    cell->sequence.store(pos + QUEUE_CAPACITY, std::memory_order_release);
    return true;
}

bool MmsNotifyDispatcher::TryDequeueOverflow(NotifyEvent &event)
{
    std::lock_guard<std::mutex> lock(mOverflowMutex);
    if (mOverflowQueue.empty()) {
        return false;
    }

    event = mOverflowQueue.front();
    mOverflowQueue.pop_front();
    if (mOverflowQueue.empty()) {
        mOverflowActive.store(false, std::memory_order_release);
    }
    return true;
}

void MmsNotifyDispatcher::NotifyLocalCallback(const NotifyEvent &event)
{
    auto callback = mCallback;
    if (callback == nullptr) {
        return;
    }

    try {
        callback(event.key, event.opType);
    } catch (const std::exception &ex) {
        LOG_ERROR("Notify callback failed, error:" << ex.what() << ".");
    } catch (...) {
        LOG_ERROR("Notify callback failed.");
    }
}

void MmsNotifyDispatcher::HandleEvent(const NotifyEvent &event)
{
    if (UNLIKELY(event.key[0] == '\0')) {
        return;
    }

    NotifyLocalCallback(event);
    if (mRemoteNotifyHandler != nullptr) {
        mRemoteNotifyHandler(event.key, event.opType);
    }
}

void MmsNotifyDispatcher::WorkerLoop()
{
    while (true) {
        NotifyEvent event{};
        bool hasEvent = false;
        while (TryDequeue(event)) {
            hasEvent = true;
            HandleEvent(event);
        }
        while (TryDequeueOverflow(event)) {
            hasEvent = true;
            HandleEvent(event);
        }
        if (UNLIKELY(!hasEvent)) {
            if (mStop.load(std::memory_order_acquire)) {
                break;
            }
            CPU_RELAX();
        }
    }
}

} // namespace mms
} // namespace ock
