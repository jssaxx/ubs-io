/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef MMS_NOTIFY_SHM_CLIENT_H
#define MMS_NOTIFY_SHM_CLIENT_H

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "mms_c.h"
#include "mms_message.h"
#include "mms_notify_shm.h"

namespace ock {
namespace mms {

class MmsNotifyShmConsumer {
public:
    MmsNotifyShmConsumer() = default;
    ~MmsNotifyShmConsumer();

    BResult Start(uint32_t serverPid, std::atomic<NotifyCallback> &callback, std::atomic<void *> &userData);
    void Stop();
    bool IsRunning() const;

private:
    BResult ReceiveSubscription(uint32_t serverPid, std::array<int32_t, NOTIFY_SHM_MAX_FDS> &fds, uint16_t &fdCount,
                                NotifyShmHandshake &handshake);
    BResult MapQueues(const std::array<int32_t, NOTIFY_SHM_MAX_FDS> &fds, uint16_t fdCount,
                      const NotifyShmHandshake &handshake);
    BResult StartWorkers(const NotifyShmHandshake &handshake);
    void StopLocked();
    void WorkerMain(uint16_t workerIndex, uint16_t cpuId);
    bool WaitEvent(uint16_t workerIndex);
    void InvokeCallback(const NotifyShmEvent &event);
    void BindWorker(uint16_t workerIndex, uint16_t cpuId);

private:
    std::mutex mLifecycleLock;
    std::vector<std::thread> mWorkers;
    std::atomic<NotifyCallback> *mCallback = nullptr;
    std::atomic<void *> *mUserData = nullptr;
    void *mAddress = nullptr;
    uint64_t mMemorySize = 0;
    std::vector<int32_t> mEventFds;
    std::vector<NotifyShmQueue> mQueues;
    bool mBusyPolling = true;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mStopping{false};
};

} // namespace mms
} // namespace ock

#endif
