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

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <utility>

#include "mms_def.h"
#include "mms_log.h"
#include "mms_message.h"
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

void MmsNotifyDispatcher::Notify(const char *key, uint16_t keyLen, OperateType opType)
{
    if (UNLIKELY(key == nullptr || keyLen == 0 || keyLen >= MAX_KEY_SIZE ||
        !mRunning.load(std::memory_order_acquire))) {
        return;
    }

    NotifyEvent event{};
    auto ret = memcpy_s(event.key, MAX_KEY_SIZE, key, keyLen);
    if (UNLIKELY(ret != EOK)) {
        LOG_ERROR("Copy notify key failed, key:" << key << ", ret:" << ret << ".");
        return;
    }
    event.key[keyLen] = '\0';
    event.keyLen = keyLen;
    event.opType = opType;

    bool pushed = PushEvent(event);
    if (UNLIKELY(!pushed && !mStop.load(std::memory_order_acquire))) {
        LOG_ERROR("Push notify event failed, key:" << event.key << ".");
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
        if (sem_init(&mFreeSlots, 0, QUEUE_CAPACITY) != 0) {
            LOG_ERROR("Init notify free sem failed, errno:" << errno << ".");
            return false;
        }
        if (sem_init(&mUsedSlots, 0, 0) != 0) {
            LOG_ERROR("Init notify used sem failed, errno:" << errno << ".");
            auto destroyRet = sem_destroy(&mFreeSlots);
            if (destroyRet != 0) {
                LOG_ERROR("Destroy notify free sem failed, errno:" << errno << ".");
            }
            return false;
        }
        mQueueInited = true;
        mStop.store(false, std::memory_order_release);
        mHead = 0;
        mTail = 0;
        mCount = 0;
        mWorker = std::thread(&MmsNotifyDispatcher::WorkerLoop, this);
        mRunning.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception &ex) {
        LOG_ERROR("Start notify worker failed, error:" << ex.what() << ".");
    } catch (...) {
        LOG_ERROR("Start notify worker failed.");
    }
    if (mQueueInited) {
        auto destroyFreeRet = sem_destroy(&mFreeSlots);
        if (destroyFreeRet != 0) {
            LOG_ERROR("Destroy notify free sem failed, errno:" << errno << ".");
        }
        auto destroyUsedRet = sem_destroy(&mUsedSlots);
        if (destroyUsedRet != 0) {
            LOG_ERROR("Destroy notify used sem failed, errno:" << errno << ".");
        }
        mQueueInited = false;
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

    mStop.store(true, std::memory_order_release);
    for (uint32_t index = 0; index < QUEUE_CAPACITY; ++index) {
        auto postRet = sem_post(&mFreeSlots);
        if (UNLIKELY(postRet != 0)) {
            LOG_ERROR("Post notify free sem failed, errno:" << errno << ".");
        }
    }
    auto postRet = sem_post(&mUsedSlots);
    if (UNLIKELY(postRet != 0)) {
        LOG_ERROR("Post notify used sem failed, errno:" << errno << ".");
    }
    worker = std::move(mWorker);
    if (worker.joinable()) {
        worker.join();
    }

    if (mQueueInited) {
        auto destroyFreeRet = sem_destroy(&mFreeSlots);
        if (destroyFreeRet != 0) {
            LOG_ERROR("Destroy notify free sem failed, errno:" << errno << ".");
        }
        auto destroyUsedRet = sem_destroy(&mUsedSlots);
        if (destroyUsedRet != 0) {
            LOG_ERROR("Destroy notify used sem failed, errno:" << errno << ".");
        }
        mQueueInited = false;
    }
    mRunning.store(false, std::memory_order_release);
}

bool MmsNotifyDispatcher::WaitEvent(NotifyEvent &event)
{
    while (sem_wait(&mUsedSlots) != 0) {
        if (errno != EINTR) {
            LOG_ERROR("Wait notify used sem failed, errno:" << errno << ".");
            return false;
        }
    }

    if (UNLIKELY(!PopEvent(event))) {
        if (mStop.load(std::memory_order_acquire)) {
            return false;
        }
        LOG_ERROR("Pop notify event failed.");
        return false;
    }
    auto postRet = sem_post(&mFreeSlots);
    if (UNLIKELY(postRet != 0)) {
        LOG_ERROR("Post notify free sem failed, errno:" << errno << ".");
    }
    return true;
}

bool MmsNotifyDispatcher::PushEvent(const NotifyEvent &event)
{
    if (UNLIKELY(sem_trywait(&mFreeSlots) != 0)) {
        if (errno != EAGAIN) {
            LOG_ERROR("Try wait notify free sem failed, errno:" << errno << ".");
            return false;
        }
        uint64_t fullCount = mQueueFullCount.fetch_add(NO_1, std::memory_order_relaxed) + NO_1;
        LOG_ERROR("Server notify queue full, count:" << fullCount << ", key:" << event.key << ".");
        while (sem_wait(&mFreeSlots) != 0) {
            if (errno != EINTR) {
                LOG_ERROR("Wait notify free sem failed, errno:" << errno << ".");
                return false;
            }
        }
    }

    if (UNLIKELY(mStop.load(std::memory_order_acquire))) {
        auto postRet = sem_post(&mFreeSlots);
        if (postRet != 0) {
            LOG_ERROR("Post notify free sem failed, errno:" << errno << ".");
        }
        return false;
    }

    mQueueLock.Lock();
    mQueue[mTail] = event;
    mTail = (mTail + NO_1) % QUEUE_CAPACITY;
    ++mCount;
    mQueueLock.UnLock();

    auto postRet = sem_post(&mUsedSlots);
    if (UNLIKELY(postRet != 0)) {
        LOG_ERROR("Post notify used sem failed, errno:" << errno << ".");
        return false;
    }
    return true;
}

bool MmsNotifyDispatcher::PopEvent(NotifyEvent &event)
{
    mQueueLock.Lock();
    if (mCount == 0) {
        mQueueLock.UnLock();
        return false;
    }
    event = mQueue[mHead];
    mHead = (mHead + NO_1) % QUEUE_CAPACITY;
    --mCount;
    mQueueLock.UnLock();
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

void MmsNotifyDispatcher::HandleEventBatch(NotifyEvent *events, uint16_t eventNum)
{
    if (eventNum == 0) {
        return;
    }

    for (uint16_t index = 0; index < eventNum; ++index) {
        NotifyLocalCallback(events[index]);
    }
    if (mRemoteNotifyHandler != nullptr) {
        mRemoteNotifyHandler(events, eventNum);
    }
}

void MmsNotifyDispatcher::WorkerLoop()
{
    NotifyEvent events[NOTIFY_DATA_CHANGE_BATCH_NUM];
    while (true) {
        if (!WaitEvent(events[0])) {
            break;
        }
        uint16_t eventNum = NO_1;
        while (eventNum < NOTIFY_DATA_CHANGE_BATCH_NUM && sem_trywait(&mUsedSlots) == 0) {
            if (UNLIKELY(!PopEvent(events[eventNum]))) {
                auto postUsedRet = sem_post(&mUsedSlots);
                if (UNLIKELY(postUsedRet != 0)) {
                    LOG_ERROR("Post notify used sem failed, errno:" << errno << ".");
                }
                break;
            }
            auto postRet = sem_post(&mFreeSlots);
            if (UNLIKELY(postRet != 0)) {
                LOG_ERROR("Post notify free sem failed, errno:" << errno << ".");
            }
            ++eventNum;
        }
        HandleEventBatch(events, eventNum);
    }
}

} // namespace mms
} // namespace ock
