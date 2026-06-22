/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "mms_notify_shm_server.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <linux/memfd.h>
#include <new>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#include "mms_log.h"
#include "mms_monotonic.h"
#include "mms_trace.h"
#include "securec.h"

namespace ock {
namespace mms {

static constexpr uint32_t NOTIFY_SHM_PRODUCER_SPIN_COUNT = 1000;

MmsNotifyShmPublisher::~MmsNotifyShmPublisher()
{
    StopListener();
    CloseSubscription();
    ReleaseSessions();
}

BResult MmsNotifyShmPublisher::Initialize(const MmsConfig::NotifyShmConfig &config)
{
    mConfig = config;
    return StartListener();
}

bool MmsNotifyShmPublisher::IsActive() const
{
    return mActiveSession.load(std::memory_order_acquire) != nullptr;
}

BResult MmsNotifyShmPublisher::StartListener()
{
    mListenFd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (mListenFd < 0) {
        LOG_ERROR("Create notify shm socket failed, errno:" << errno << ".");
        return MMS_INNER_ERR;
    }

    char socketName[NOTIFY_SHM_SOCKET_NAME_SIZE] = {};
    if (!NotifyShmQueue::BuildSocketName(static_cast<uint32_t>(getpid()), socketName, sizeof(socketName))) {
        close(mListenFd);
        mListenFd = -1;
        return MMS_INNER_ERR;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    size_t nameSize = strlen(socketName);
    auto ret = memcpy_s(address.sun_path + NO_1, sizeof(address.sun_path) - NO_1, socketName, nameSize);
    if (ret != EOK) {
        close(mListenFd);
        mListenFd = -1;
        return MMS_INNER_ERR;
    }
    socklen_t addressSize = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + NO_1 + nameSize);
    if (bind(mListenFd, reinterpret_cast<sockaddr *>(&address), addressSize) != 0 || listen(mListenFd, NO_8) != 0) {
        LOG_ERROR("Start notify shm socket failed, errno:" << errno << ".");
        close(mListenFd);
        mListenFd = -1;
        return MMS_INNER_ERR;
    }

    mStopping.store(false, std::memory_order_release);
    try {
        mAcceptThread = std::thread(&MmsNotifyShmPublisher::AcceptLoop, this);
    } catch (const std::exception &ex) {
        LOG_ERROR("Start notify shm accept thread failed, error:" << ex.what() << ".");
        close(mListenFd);
        mListenFd = -1;
        return MMS_INNER_ERR;
    } catch (...) {
        LOG_ERROR("Start notify shm accept thread failed.");
        close(mListenFd);
        mListenFd = -1;
        return MMS_INNER_ERR;
    }
    return MMS_OK;
}

void MmsNotifyShmPublisher::StopListener()
{
    mStopping.store(true, std::memory_order_release);
    if (mListenFd >= 0) {
        shutdown(mListenFd, SHUT_RDWR);
        close(mListenFd);
        mListenFd = -1;
    }
    if (mAcceptThread.joinable()) {
        mAcceptThread.join();
    }
}

void MmsNotifyShmPublisher::AcceptLoop()
{
    auto ret = pthread_setname_np(pthread_self(), "notify-shm-ctl");
    if (ret != 0) {
        LOG_WARN("Set notify shm accept thread name failed, ret:" << ret << ".");
    }
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    CPU_SET(mConfig.workerCpuIds.front(), &cpuSet);
    ret = pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet);
    if (ret != 0) {
        LOG_WARN("Bind notify shm accept thread failed, cpu:" << mConfig.workerCpuIds.front() <<
            ", ret:" << ret << ".");
    }

    while (!mStopping.load(std::memory_order_acquire)) {
        int32_t clientFd = accept4(mListenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (!mStopping.load(std::memory_order_acquire) && errno != EINTR) {
                LOG_ERROR("Accept notify shm client failed, errno:" << errno << ".");
            }
            continue;
        }

        ucred credential{};
        socklen_t credentialSize = sizeof(credential);
        if (getsockopt(clientFd, SOL_SOCKET, SO_PEERCRED, &credential, &credentialSize) != 0 ||
            credential.pid <= 0 || credential.uid != geteuid()) {
            LOG_ERROR("Get notify shm client credential failed, errno:" << errno << ".");
            close(clientFd);
            continue;
        }
        (void)HandleClient(clientFd, static_cast<uint32_t>(credential.pid));
        close(clientFd);
    }
}

BResult MmsNotifyShmPublisher::HandleClient(int32_t clientFd, uint32_t clientPid)
{
    std::array<int32_t, NOTIFY_SHM_MAX_FDS> fds{};
    fds.fill(-1);
    uint16_t fdCount = 0;
    NotifyShmHandshake handshake{};
    auto ret = CreateSubscription(clientPid, fds, fdCount, handshake);
    if (ret != MMS_OK) {
        return ret;
    }
    ActivateSubscription();
    ret = SendSubscription(clientFd, fds, fdCount, handshake);
    if (ret != MMS_OK) {
        CloseSubscription();
        return ret;
    }
    LOG_INFO("Notify shm client subscribed, pid:" << clientPid << ".");
    return MMS_OK;
}

BResult MmsNotifyShmPublisher::CreateSubscription(uint32_t clientPid,
    std::array<int32_t, NOTIFY_SHM_MAX_FDS> &fds, uint16_t &fdCount, NotifyShmHandshake &handshake)
{
    std::lock_guard<std::mutex> lock(mLifecycleLock);
    if (mPendingSession != nullptr) {
        CloseQueues(*mPendingSession);
        mPendingSession = nullptr;
    }

    uint16_t queueNum = mConfig.workerNum;
    uint32_t queueDepth = NotifyShmQueue::CalculateShardDepth(mConfig.queueDepth, queueNum);
    if (queueDepth == 0) {
        LOG_ERROR("Calculate notify shm shard depth failed, total depth:" << mConfig.queueDepth <<
            ", queue num:" << queueNum << ".");
        return MMS_INVALID_PARAM;
    }
    uint64_t memorySize = NotifyShmQueue::GetMemorySize(queueDepth, queueNum);
    int32_t memFd = static_cast<int32_t>(syscall(SYS_memfd_create, "mms-notify", MFD_CLOEXEC));
    if (memFd < 0 || ftruncate(memFd, static_cast<off_t>(memorySize)) != 0) {
        LOG_ERROR("Create notify shm memory failed, errno:" << errno << ".");
        if (memFd >= 0) {
            close(memFd);
        }
        return MMS_INNER_ERR;
    }

    void *address = mmap(nullptr, memorySize, PROT_READ | PROT_WRITE, MAP_SHARED, memFd, 0);
    if (address == MAP_FAILED || !NotifyShmQueue::Initialize(address, memorySize, queueDepth, queueNum)) {
        LOG_ERROR("Map or initialize notify shm memory failed, errno:" << errno << ".");
        if (address != MAP_FAILED) {
            munmap(address, memorySize);
        }
        close(memFd);
        return MMS_INNER_ERR;
    }

    std::unique_ptr<Session> session(new (std::nothrow) Session());
    if (session == nullptr) {
        munmap(address, memorySize);
        close(memFd);
        return MMS_ALLOC_FAIL;
    }
    session->memFd = memFd;
    session->address = address;
    session->memorySize = memorySize;
    session->queueDepth = queueDepth;
    session->busyPolling = mConfig.busyPolling;
    session->clientPid = clientPid;
    session->eventFds.reserve(queueNum);
    session->queues.resize(queueNum);
    for (uint16_t queueIndex = 0; queueIndex < queueNum; ++queueIndex) {
        int32_t eventFd = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE);
        if (eventFd < 0 ||
            !session->queues[queueIndex].Attach(address, memorySize, queueDepth, queueIndex)) {
            LOG_ERROR("Create notify shm shard failed, shard:" << queueIndex << ", errno:" << errno << ".");
            if (eventFd >= 0) {
                close(eventFd);
            }
            for (int32_t fd : session->eventFds) {
                close(fd);
            }
            munmap(address, memorySize);
            close(memFd);
            return MMS_INNER_ERR;
        }
        session->eventFds.emplace_back(eventFd);
    }
#ifdef SYS_pidfd_open
    session->pidFd = static_cast<int32_t>(syscall(SYS_pidfd_open, static_cast<pid_t>(clientPid), 0));
#endif
    mPendingSession = session.get();
    mSessions.emplace_back(std::move(session));

    fds[NO_0] = memFd;
    for (uint16_t queueIndex = 0; queueIndex < queueNum; ++queueIndex) {
        fds[queueIndex + NO_1] = mPendingSession->eventFds[queueIndex];
    }
    fdCount = static_cast<uint16_t>(queueNum + NO_1);
    handshake = {};
    handshake.result = MMS_OK;
    handshake.queueDepth = queueDepth;
    handshake.memorySize = memorySize;
    handshake.workerNum = mConfig.workerNum;
    handshake.queueNum = queueNum;
    handshake.cpuNum = static_cast<uint16_t>(mConfig.workerCpuIds.size());
    handshake.busyPolling = static_cast<uint8_t>(mConfig.busyPolling);
    for (uint16_t index = 0; index < handshake.cpuNum; ++index) {
        handshake.cpuIds[index] = mConfig.workerCpuIds[index];
    }
    return MMS_OK;
}

BResult MmsNotifyShmPublisher::SendSubscription(int32_t clientFd,
    const std::array<int32_t, NOTIFY_SHM_MAX_FDS> &fds, uint16_t fdCount, const NotifyShmHandshake &handshake)
{
    iovec ioVec{};
    ioVec.iov_base = const_cast<NotifyShmHandshake *>(&handshake);
    ioVec.iov_len = sizeof(handshake);

    char control[CMSG_SPACE(sizeof(int32_t) * NOTIFY_SHM_MAX_FDS)] = {};
    msghdr message{};
    message.msg_iov = &ioVec;
    message.msg_iovlen = NO_1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr *controlHeader = CMSG_FIRSTHDR(&message);
    controlHeader->cmsg_level = SOL_SOCKET;
    controlHeader->cmsg_type = SCM_RIGHTS;
    uint32_t fdsSize = sizeof(int32_t) * fdCount;
    controlHeader->cmsg_len = CMSG_LEN(fdsSize);
    message.msg_controllen = CMSG_SPACE(fdsSize);
    auto ret = memcpy_s(CMSG_DATA(controlHeader), fdsSize, fds.data(), fdsSize);
    if (ret != EOK) {
        return MMS_INNER_ERR;
    }

    ssize_t sendSize = sendmsg(clientFd, &message, MSG_NOSIGNAL);
    if (sendSize != static_cast<ssize_t>(sizeof(handshake))) {
        LOG_ERROR("Send notify shm subscription failed, errno:" << errno << ".");
        return MMS_INNER_ERR;
    }
    return MMS_OK;
}

void MmsNotifyShmPublisher::ActivateSubscription()
{
    std::lock_guard<std::mutex> lock(mLifecycleLock);
    Session *oldSession = mActiveSession.exchange(mPendingSession, std::memory_order_acq_rel);
    if (oldSession != nullptr) {
        CloseQueues(*oldSession);
    }
    mPendingSession = nullptr;
}

void MmsNotifyShmPublisher::CloseSubscription()
{
    Session *session = mActiveSession.exchange(nullptr, std::memory_order_acq_rel);
    if (session != nullptr) {
        CloseQueues(*session);
    }
}

bool MmsNotifyShmPublisher::PublishBatch(const NotifyShmPublishItem *items, uint32_t itemNum)
{
    if (itemNum == 0) {
        return true;
    }
    Session *session = mActiveSession.load(std::memory_order_acquire);
    if (session == nullptr) {
        return true;
    }

    uint64_t enqueueTimeNs = tracemark::TraceMark::IsEnable() ? Monotonic::TimeNs() : 0;
    // Keep each producer on one shard to avoid moving hot queue cursors between workers.
    static thread_local ProducerState producerState;
    uint16_t queueIndex = SelectQueue(*session, producerState);

    MMS_TRACE_START(SERVER_TRACE_NOTIFY_SHM_ENQUEUE);
    NotifyShmResult result = NotifyShmResult::OK;
    uint32_t publishedNum = 0;
    while (publishedNum < itemNum) {
        uint32_t batchNum = std::min(itemNum - publishedNum, session->queueDepth);
        result = EnqueueBatch(*session, producerState, items + publishedNum, batchNum, enqueueTimeNs);
        if (result != NotifyShmResult::OK) {
            break;
        }
        publishedNum += batchNum;
    }
    MMS_TRACE_END(SERVER_TRACE_NOTIFY_SHM_ENQUEUE, result == NotifyShmResult::OK ? MMS_OK : MMS_ERR);
    if (result != NotifyShmResult::OK) {
        if (result == NotifyShmResult::CLOSED) {
            Session *expected = session;
            (void)mActiveSession.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
            return true;
        }
        return false;
    }

    if (!session->busyPolling) {
        uint64_t notifyValue = publishedNum;
        ssize_t writeSize = write(session->eventFds[queueIndex], &notifyValue, sizeof(notifyValue));
        if (UNLIKELY(writeSize != static_cast<ssize_t>(sizeof(notifyValue)) && errno != EAGAIN)) {
            LOG_ERROR("Signal notify shm consumer failed, errno:" << errno << ".");
            return false;
        }
    }
    return true;
}

NotifyShmResult MmsNotifyShmPublisher::EnqueueBatch(
    Session &session, ProducerState &producerState,
    const NotifyShmPublishItem *items, uint32_t itemNum, uint64_t enqueueTimeNs)
{
    uint32_t retryCount = 0;
    uint64_t waitStartNs = 0;
    while (true) {
        auto result = session.queues[producerState.queueIndex].TryEnqueueBatch(
            items, itemNum, enqueueTimeNs, producerState.queueContext);
        if (result != NotifyShmResult::FULL) {
            if (waitStartNs != 0) {
                MMS_TRACE_ASYNC_END(SERVER_TRACE_NOTIFY_SHM_QUEUE_WAIT,
                    result == NotifyShmResult::OK ? MMS_OK : MMS_ERR, waitStartNs);
            }
            return result;
        }

        if (waitStartNs == 0) {
            waitStartNs = Monotonic::TimeNs();
            MMS_TRACE_ASYNC_BEGIN(SERVER_TRACE_NOTIFY_SHM_QUEUE_WAIT);
        }
        if (++retryCount < NOTIFY_SHM_PRODUCER_SPIN_COUNT) {
            NotifyShmQueue::CpuRelax();
            continue;
        }

        retryCount = 0;
        if (UNLIKELY(!IsClientAlive(session))) {
            CloseQueues(session);
            LOG_ERROR("Notify shm client exited, pid:" << session.clientPid << ".");
            continue;
        }
        sched_yield();
    }
}

uint16_t MmsNotifyShmPublisher::SelectQueue(Session &session, ProducerState &producerState)
{
    if (producerState.session != &session) {
        producerState.session = &session;
        uint32_t queueNum = static_cast<uint32_t>(session.queues.size());
        uint32_t nextQueueIndex = session.nextQueueIndex.fetch_add(NO_1, std::memory_order_relaxed);
        producerState.queueIndex = static_cast<uint16_t>(nextQueueIndex % queueNum);
        producerState.queueContext = {};
    }
    return producerState.queueIndex;
}

void MmsNotifyShmPublisher::CloseQueues(Session &session)
{
    for (auto &queue : session.queues) {
        queue.Close();
    }
}

bool MmsNotifyShmPublisher::IsClientAlive(const Session &session) const
{
    if (session.clientPid == 0) {
        return false;
    }
    if (session.pidFd >= 0) {
        pollfd descriptor{session.pidFd, POLLIN, 0};
        return poll(&descriptor, NO_1, 0) == 0;
    }
    return kill(static_cast<pid_t>(session.clientPid), 0) == 0 || errno == EPERM;
}

void MmsNotifyShmPublisher::ReleaseSessions()
{
    for (auto &session : mSessions) {
        if (session->address != nullptr) {
            munmap(session->address, session->memorySize);
        }
        for (int32_t eventFd : session->eventFds) {
            close(eventFd);
        }
        if (session->pidFd >= 0) {
            close(session->pidFd);
        }
        if (session->memFd >= 0) {
            close(session->memFd);
        }
    }
    mSessions.clear();
}

}
}
