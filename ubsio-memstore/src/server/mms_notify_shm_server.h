/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef MMS_NOTIFY_SHM_SERVER_H
#define MMS_NOTIFY_SHM_SERVER_H

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "mms_config_instance.h"
#include "mms_notify_shm.h"

namespace ock {
namespace mms {

class MmsNotifyShmPublisher {
public:
    MmsNotifyShmPublisher() = default;
    ~MmsNotifyShmPublisher();

    BResult Initialize(const MmsConfig::NotifyShmConfig &config);
    bool IsActive() const;
    bool PublishBatch(const NotifyShmPublishItem *items, uint32_t itemNum);

private:
    struct Session {
        int32_t memFd = -1;
        int32_t pidFd = -1;
        void *address = nullptr;
        uint64_t memorySize = 0;
        std::vector<int32_t> eventFds;
        std::vector<NotifyShmQueue> queues;
        std::atomic<uint32_t> nextQueueIndex{0};
        uint32_t queueDepth = 0;
        bool busyPolling = true;
        uint32_t clientPid = 0;
    };

    struct ProducerState {
        const Session *session = nullptr;
        uint16_t queueIndex = 0;
        NotifyShmProducerContext queueContext;
    };

    BResult StartListener();
    void StopListener();
    void AcceptLoop();
    BResult HandleClient(int32_t clientFd, uint32_t clientPid);
    BResult CreateSubscription(uint32_t clientPid, std::array<int32_t, NOTIFY_SHM_MAX_FDS> &fds, uint16_t &fdCount,
                               NotifyShmHandshake &handshake);
    BResult SendSubscription(int32_t clientFd, const std::array<int32_t, NOTIFY_SHM_MAX_FDS> &fds, uint16_t fdCount,
                             const NotifyShmHandshake &handshake);
    void ActivateSubscription();
    void CloseSubscription();
    NotifyShmResult EnqueueBatch(Session &session, ProducerState &producerState, const NotifyShmPublishItem *items,
                                 uint32_t itemNum, uint64_t enqueueTimeNs);
    uint16_t SelectQueue(Session &session, ProducerState &producerState);
    void CloseQueues(Session &session);
    bool IsClientAlive(const Session &session) const;
    void ReleaseSessions();

private:
    MmsConfig::NotifyShmConfig mConfig;
    std::mutex mLifecycleLock;
    std::vector<std::unique_ptr<Session>> mSessions;
    Session *mPendingSession = nullptr;
    std::atomic<Session *> mActiveSession{nullptr};
    std::atomic<bool> mStopping{false};
    std::thread mAcceptThread;
    int32_t mListenFd = -1;
};

} // namespace mms
} // namespace ock

#endif
