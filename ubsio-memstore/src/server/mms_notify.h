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

#ifndef MMS_NOTIFY_H
#define MMS_NOTIFY_H

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <semaphore.h>
#include <thread>

#include "mms_c.h"
#include "mms_lock.h"
#include "mms_types.h"

namespace ock {
namespace mms {

struct NotifyEvent {
    char key[MAX_KEY_SIZE];
    uint16_t keyLen;
    OperateType opType;
};

class MmsNotifyDispatcher {
public:
    using RemoteNotifyHandler = std::function<void(const NotifyEvent *, uint16_t)>;

    static MmsNotifyDispatcher &Instance();

    CResult RegisterCallback(NotifyCallback callback);
    CResult RegisterRemoteNotifyHandler(RemoteNotifyHandler handler);
    void Notify(const char *key, uint16_t keyLen, OperateType opType);
    void Stop();

private:
    MmsNotifyDispatcher() = default;
    ~MmsNotifyDispatcher();

    MmsNotifyDispatcher(const MmsNotifyDispatcher &) = delete;
    MmsNotifyDispatcher &operator=(const MmsNotifyDispatcher &) = delete;

    bool StartWorkerLocked();
    void StopWorkerIfIdleLocked();
    void StopWorker();
    bool WaitEvent(NotifyEvent &event);
    bool PushEvent(const NotifyEvent &event);
    bool PopEvent(NotifyEvent &event);
    void HandleEventBatch(NotifyEvent *events, uint16_t eventNum);
    void NotifyLocalCallback(const NotifyEvent &event);
    void WorkerLoop();

private:
    static constexpr uint32_t QUEUE_CAPACITY = NO_10240;

    NotifyCallback mCallback = nullptr;
    RemoteNotifyHandler mRemoteNotifyHandler = nullptr;
    std::atomic<uint64_t> mQueueFullCount{0};
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mStop{false};
    bool mQueueInited = false;
    std::mutex mLifecycleMutex;
    SpinLock mQueueLock;
    sem_t mFreeSlots{};
    sem_t mUsedSlots{};
    std::array<NotifyEvent, QUEUE_CAPACITY> mQueue{};
    uint32_t mHead = 0;
    uint32_t mTail = 0;
    uint32_t mCount = 0;
    std::thread mWorker;
};

} // namespace mms
} // namespace ock

#endif // MMS_NOTIFY_H
