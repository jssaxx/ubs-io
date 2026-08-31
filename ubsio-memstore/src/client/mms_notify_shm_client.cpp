/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "mms_notify_shm_client.h"

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <string>

#include "mms_client_log.h"
#include "mms_def.h"
#include "mms_monotonic.h"
#include "mms_trace.h"
#include "securec.h"

namespace ock {
namespace mms {

static void CloseReceivedFds(msghdr &message)
{
    for (cmsghdr *header = CMSG_FIRSTHDR(&message); header != nullptr; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS || header->cmsg_len < CMSG_LEN(0)) {
            continue;
        }
        size_t dataSize = header->cmsg_len - CMSG_LEN(0);
        if (dataSize % sizeof(int32_t) != 0 || dataSize > sizeof(int32_t) * NOTIFY_SHM_MAX_FDS) {
            continue;
        }
        auto *receivedFds = reinterpret_cast<int32_t *>(CMSG_DATA(header));
        size_t fdCount = dataSize / sizeof(int32_t);
        for (size_t index = 0; index < fdCount; ++index) {
            close(receivedFds[index]);
        }
    }
}

MmsNotifyShmConsumer::~MmsNotifyShmConsumer()
{
    Stop();
}

BResult MmsNotifyShmConsumer::Start(uint32_t serverPid, std::atomic<NotifyCallback> &callback,
                                    std::atomic<void *> &userData)
{
    std::lock_guard<std::mutex> lock(mLifecycleLock);
    StopLocked();
    mCallback = &callback;
    mUserData = &userData;

    std::array<int32_t, NOTIFY_SHM_MAX_FDS> fds{};
    fds.fill(-1);
    uint16_t fdCount = 0;
    NotifyShmHandshake handshake{};
    auto ret = ReceiveSubscription(serverPid, fds, fdCount, handshake);
    if (ret != MMS_OK) {
        return ret;
    }
    ret = MapQueues(fds, fdCount, handshake);
    if (ret != MMS_OK) {
        return ret;
    }
    ret = StartWorkers(handshake);
    if (ret != MMS_OK) {
        StopLocked();
    }
    return ret;
}

void MmsNotifyShmConsumer::Stop()
{
    std::lock_guard<std::mutex> lock(mLifecycleLock);
    StopLocked();
}

bool MmsNotifyShmConsumer::IsRunning() const
{
    return mRunning.load(std::memory_order_acquire);
}

BResult MmsNotifyShmConsumer::ReceiveSubscription(uint32_t serverPid, std::array<int32_t, NOTIFY_SHM_MAX_FDS> &fds,
                                                  uint16_t &fdCount, NotifyShmHandshake &handshake)
{
    int32_t socketFd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (socketFd < 0) {
        CLIENT_LOG_ERROR("Create notify shm socket failed, errno:" << errno << ".");
        return MMS_INNER_ERR;
    }

    char socketName[NOTIFY_SHM_SOCKET_NAME_SIZE] = {};
    if (!NotifyShmQueue::BuildSocketName(serverPid, socketName, sizeof(socketName))) {
        close(socketFd);
        return MMS_INVALID_PARAM;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    size_t nameSize = strlen(socketName);
    auto copyRet = memcpy_s(address.sun_path + NO_1, sizeof(address.sun_path) - NO_1, socketName, nameSize);
    if (copyRet != EOK) {
        close(socketFd);
        return MMS_INNER_ERR;
    }
    socklen_t addressSize = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + NO_1 + nameSize);
    if (connect(socketFd, reinterpret_cast<sockaddr *>(&address), addressSize) != 0) {
        CLIENT_LOG_ERROR("Connect notify shm socket failed, errno:" << errno << ".");
        close(socketFd);
        return MMS_NET_RETRY;
    }

    iovec ioVec{};
    ioVec.iov_base = &handshake;
    ioVec.iov_len = sizeof(handshake);
    char control[CMSG_SPACE(sizeof(int32_t) * NOTIFY_SHM_MAX_FDS)] = {};
    msghdr message{};
    message.msg_iov = &ioVec;
    message.msg_iovlen = NO_1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t recvSize = recvmsg(socketFd, &message, MSG_CMSG_CLOEXEC);
    close(socketFd);
    if (recvSize != static_cast<ssize_t>(sizeof(handshake)) || (message.msg_flags & MSG_CTRUNC) != 0) {
        CLIENT_LOG_ERROR("Receive notify shm subscription failed, size:" << recvSize << ", errno:" << errno << ".");
        CloseReceivedFds(message);
        return MMS_INNER_ERR;
    }

    cmsghdr *controlHeader = CMSG_FIRSTHDR(&message);
    bool invalidLength = controlHeader == nullptr || controlHeader->cmsg_len < CMSG_LEN(0);
    size_t controlDataSize = invalidLength ? 0 : controlHeader->cmsg_len - CMSG_LEN(0);
    bool invalidControl =
        controlHeader == nullptr || controlHeader->cmsg_level != SOL_SOCKET || controlHeader->cmsg_type != SCM_RIGHTS ||
        invalidLength || controlDataSize == 0 || controlDataSize % sizeof(int32_t) != 0 ||
        controlDataSize > sizeof(int32_t) * NOTIFY_SHM_MAX_FDS || CMSG_NXTHDR(&message, controlHeader) != nullptr;
    if (invalidControl) {
        CLIENT_LOG_ERROR("Receive notify shm fds failed.");
        CloseReceivedFds(message);
        return MMS_INNER_ERR;
    }
    fdCount = static_cast<uint16_t>(controlDataSize / sizeof(int32_t));
    copyRet = memcpy_s(fds.data(), sizeof(int32_t) * fds.size(), CMSG_DATA(controlHeader), controlDataSize);
    if (copyRet != EOK) {
        CloseReceivedFds(message);
        fds.fill(-1);
        fdCount = 0;
        return MMS_INNER_ERR;
    }
    return MMS_OK;
}

BResult MmsNotifyShmConsumer::MapQueues(const std::array<int32_t, NOTIFY_SHM_MAX_FDS> &fds, uint16_t fdCount,
                                        const NotifyShmHandshake &handshake)
{
    int32_t memFd = fds[NO_0];
    bool invalidHandshake =
        handshake.result != MMS_OK || handshake.workerNum == 0 || handshake.workerNum > NOTIFY_SHM_MAX_WORKERS ||
        handshake.cpuNum < handshake.workerNum || handshake.queueNum != handshake.workerNum ||
        fdCount != static_cast<uint16_t>(handshake.queueNum + NO_1) ||
        handshake.memorySize != NotifyShmQueue::GetMemorySize(handshake.queueDepth, handshake.queueNum);
    if (UNLIKELY(memFd < 0 || invalidHandshake)) {
        CLIENT_LOG_ERROR("Invalid notify shm handshake, result:" << handshake.result
                                                                 << ", depth:" << handshake.queueDepth
                                                                 << ", worker num:" << handshake.workerNum << ".");
        for (uint16_t index = 0; index < fdCount; ++index) {
            if (fds[index] >= 0) {
                close(fds[index]);
            }
        }
        return MMS_INVALID_PARAM;
    }

    void *address = mmap(nullptr, handshake.memorySize, PROT_READ | PROT_WRITE, MAP_SHARED, memFd, 0);
    close(memFd);
    if (address == MAP_FAILED) {
        CLIENT_LOG_ERROR("Map notify shm queue failed, errno:" << errno << ".");
        for (uint16_t index = NO_1; index < fdCount; ++index) {
            close(fds[index]);
        }
        return MMS_INNER_ERR;
    }

    mQueues.resize(handshake.queueNum);
    mEventFds.reserve(handshake.queueNum);
    for (uint16_t queueIndex = 0; queueIndex < handshake.queueNum; ++queueIndex) {
        int32_t eventFd = fds[queueIndex + NO_1];
        if (eventFd < 0 ||
            !mQueues[queueIndex].Attach(address, handshake.memorySize, handshake.queueDepth, queueIndex)) {
            CLIENT_LOG_ERROR("Attach notify shm queue failed, queue:" << queueIndex << ".");
            for (uint16_t index = queueIndex + NO_1; index < fdCount; ++index) {
                if (fds[index] >= 0) {
                    close(fds[index]);
                }
            }
            for (int32_t mappedFd : mEventFds) {
                close(mappedFd);
            }
            mEventFds.clear();
            mQueues.clear();
            munmap(address, handshake.memorySize);
            return MMS_INNER_ERR;
        }
        mEventFds.emplace_back(eventFd);
    }

    mAddress = address;
    mMemorySize = handshake.memorySize;
    mBusyPolling = handshake.busyPolling != 0;
    return MMS_OK;
}

BResult MmsNotifyShmConsumer::StartWorkers(const NotifyShmHandshake &handshake)
{
    mStopping.store(false, std::memory_order_release);
    mRunning.store(true, std::memory_order_release);
    try {
        mWorkers.reserve(handshake.workerNum);
        for (uint16_t index = 0; index < handshake.workerNum; ++index) {
            mWorkers.emplace_back(&MmsNotifyShmConsumer::WorkerMain, this, index, handshake.cpuIds[index]);
        }
    } catch (const std::exception &ex) {
        CLIENT_LOG_ERROR("Start notify shm worker failed, error:" << ex.what() << ".");
        return MMS_INNER_ERR;
    } catch (...) {
        CLIENT_LOG_ERROR("Start notify shm worker failed.");
        return MMS_INNER_ERR;
    }
    return MMS_OK;
}

void MmsNotifyShmConsumer::StopLocked()
{
    mRunning.store(false, std::memory_order_release);
    mStopping.store(true, std::memory_order_release);
    for (size_t index = 0; index < mQueues.size(); ++index) {
        mQueues[index].Close();
        if (!mBusyPolling && index < mEventFds.size()) {
            uint64_t wakeValue = NO_1;
            ssize_t writeSize = write(mEventFds[index], &wakeValue, sizeof(wakeValue));
            if (writeSize != static_cast<ssize_t>(sizeof(wakeValue))) {
                CLIENT_LOG_WARN("Wake notify shm worker failed, worker:" << index << ", errno:" << errno << ".");
            }
        }
    }
    for (auto &worker : mWorkers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    mWorkers.clear();

    if (mAddress != nullptr) {
        munmap(mAddress, mMemorySize);
    }
    for (auto &queue : mQueues) {
        queue.Reset();
    }
    mQueues.clear();
    for (int32_t eventFd : mEventFds) {
        close(eventFd);
    }
    mEventFds.clear();
    mAddress = nullptr;
    mMemorySize = 0;
    mCallback = nullptr;
    mUserData = nullptr;
}

void MmsNotifyShmConsumer::WorkerMain(uint16_t workerIndex, uint16_t cpuId)
{
    BindWorker(workerIndex, cpuId);
    while (!mStopping.load(std::memory_order_acquire)) {
        if (!mBusyPolling) {
            if (!WaitEvent(workerIndex)) {
                break;
            }
        }

        NotifyShmEvent event;
        uint64_t dequeueStartNs = tracemark::TraceMark::IsEnable() ? Monotonic::TimeNs() : 0;
        auto result = mQueues[workerIndex].DequeueSingleConsumer(event);
        if (result == NotifyShmResult::OK) {
            MMS_TRACE_ASYNC_BEGIN(SDK_TRACE_NOTIFY_SHM_DEQUEUE);
            MMS_TRACE_ASYNC_END(SDK_TRACE_NOTIFY_SHM_DEQUEUE, MMS_OK, dequeueStartNs);
            MMS_TRACE_ASYNC_BEGIN(SDK_TRACE_NOTIFY_SHM_QUEUE_WAIT);
            MMS_TRACE_ASYNC_END(SDK_TRACE_NOTIFY_SHM_QUEUE_WAIT, MMS_OK, event.enqueueTimeNs);
            InvokeCallback(event);
            continue;
        }
        if (result == NotifyShmResult::CLOSED) {
            break;
        }
        if (result == NotifyShmResult::EMPTY) {
            NotifyShmQueue::CpuRelax();
        }
    }
    mRunning.store(false, std::memory_order_release);
}

bool MmsNotifyShmConsumer::WaitEvent(uint16_t workerIndex)
{
    if (mBusyPolling) {
        NotifyShmQueue::CpuRelax();
        return true;
    }

    uint64_t value = 0;
    while (read(mEventFds[workerIndex], &value, sizeof(value)) < 0) {
        if (errno != EINTR) {
            if (!mStopping.load(std::memory_order_acquire)) {
                CLIENT_LOG_ERROR("Wait notify shm event failed, errno:" << errno << ".");
            }
            return false;
        }
    }
    return true;
}

void MmsNotifyShmConsumer::InvokeCallback(const NotifyShmEvent &event)
{
    if (UNLIKELY(event.keyLen == 0 || event.keyLen >= MAX_KEY_SIZE || event.opType >= OP_BUTT)) {
        CLIENT_LOG_ERROR("Invalid notify shm event, key len:" << event.keyLen << ", op type:" << event.opType << ".");
        return;
    }

    auto callback = mCallback->load(std::memory_order_acquire);
    if (callback == nullptr) {
        return;
    }
    void *userData = mUserData->load(std::memory_order_acquire);
    try {
        MMS_TRACE_START(SDK_TRACE_USER_CALLBACK);
        callback(event.key, event.keyLen, static_cast<OperateType>(event.opType), userData);
        MMS_TRACE_END(SDK_TRACE_USER_CALLBACK, MMS_OK);
    } catch (const std::exception &ex) {
        CLIENT_LOG_ERROR("Notify callback failed, error:" << ex.what() << ".");
    } catch (...) {
        CLIENT_LOG_ERROR("Notify callback failed.");
    }
}

void MmsNotifyShmConsumer::BindWorker(uint16_t workerIndex, uint16_t cpuId)
{
    std::string threadName = "notify-shm-" + std::to_string(workerIndex);
    auto ret = pthread_setname_np(pthread_self(), threadName.c_str());
    if (ret != 0) {
        CLIENT_LOG_WARN("Set notify shm worker name failed, ret:" << ret << ".");
    }

    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    CPU_SET(cpuId, &cpuSet);
    ret = pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet);
    if (ret != 0) {
        CLIENT_LOG_WARN("Bind notify shm worker failed, worker:" << workerIndex << ", cpu:" << cpuId << ", ret:" << ret
                                                                 << ".");
    }
}

} // namespace mms
} // namespace ock
