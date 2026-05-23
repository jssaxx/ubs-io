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
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>

#include "mms_c.h"
#include "mms_types.h"

namespace ock {
namespace mms {

struct NotifyEvent {
    char key[MAX_KEY_SIZE];
    OperateType opType;
};

class MmsNotifyDispatcher {
public:
    using RemoteNotifyHandler = std::function<void(const char *, OperateType)>;

    static MmsNotifyDispatcher &Instance();

    CResult RegisterCallback(NotifyCallback callback);
    CResult RegisterRemoteNotifyHandler(RemoteNotifyHandler handler);
    void Notify(const char *key, OperateType opType);
    void Stop();

private:
    MmsNotifyDispatcher() = default;
    ~MmsNotifyDispatcher();

    MmsNotifyDispatcher(const MmsNotifyDispatcher &) = delete;
    MmsNotifyDispatcher &operator=(const MmsNotifyDispatcher &) = delete;

    bool StartWorkerLocked();
    void StopWorkerIfIdleLocked();
    void StopWorker();
    void ResetQueue();
    enum class EnqueueResult {
        SUCCESS,
        FULL,
        ERROR,
    };

    EnqueueResult TryEnqueue(const char *key, size_t keyLen, OperateType opType);
    EnqueueResult EnqueueOverflow(const char *key, size_t keyLen, OperateType opType);
    bool TryDequeue(NotifyEvent &event);
    bool TryDequeueOverflow(NotifyEvent &event);
    void HandleEvent(const NotifyEvent &event);
    void NotifyLocalCallback(const NotifyEvent &event);
    void WorkerLoop();

private:
    static constexpr uint32_t QUEUE_CAPACITY = 8192; // 队列容量，必须是2的幂
    static constexpr uint32_t QUEUE_MASK = QUEUE_CAPACITY - 1;

    struct NotifyCell {
        std::atomic<size_t> sequence{0};
        NotifyEvent event{};
    };

    NotifyCallback mCallback = nullptr;
    RemoteNotifyHandler mRemoteNotifyHandler = nullptr;
    std::atomic<size_t> mEnqueuePos{0};
    std::atomic<size_t> mDequeuePos{0};
    std::atomic<uint64_t> mQueueFullCount{0};
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mStop{false};
    std::atomic<bool> mOverflowActive{false};
    std::mutex mLifecycleMutex;
    std::mutex mOverflowMutex;
    std::deque<NotifyEvent> mOverflowQueue;
    std::unique_ptr<NotifyCell[]> mQueue;
    std::thread mWorker;
};

} // namespace mms
} // namespace ock

#endif // MMS_NOTIFY_H
