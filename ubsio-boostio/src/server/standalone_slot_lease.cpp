/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "standalone_slot_lease.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "bio_log.h"
#include "bio_types.h"

namespace ock {
namespace bio {
namespace {

constexpr uint32_t SLOT_LEASE_MAGIC = 0x5542534CU; // "UBSL"
constexpr uint32_t SLOT_LEASE_VERSION = 1U;
constexpr uint64_t NANOSECONDS_PER_SECOND = 1000000000ULL;
constexpr useconds_t INITIALIZATION_READY_POLL_INTERVAL_US = 10000U;
constexpr uint64_t INITIALIZATION_READY_TIMEOUT_SECONDS = 180ULL;
constexpr uint64_t INITIALIZATION_READY_TIMEOUT_NS = INITIALIZATION_READY_TIMEOUT_SECONDS * NANOSECONDS_PER_SECOND;
constexpr uint32_t SHM_OPEN_RETRY_TIMES = 100U;
// Field numbers follow proc_pid_stat(5); parsing starts after the parenthesized comm field.
constexpr size_t PROC_STAT_STATE_OFFSET_FROM_COMM_END = 2U;
constexpr uint32_t PROC_STAT_STATE_FIELD_INDEX = 3U;
constexpr uint32_t PROC_STAT_THREAD_COUNT_FIELD_INDEX = 20U;
constexpr uint32_t PROC_STAT_START_TIME_FIELD_INDEX = 22U;
constexpr const char *MNT_NAMESPACE_PATH = "/proc/self/ns/mnt";
constexpr const char *IPC_NAMESPACE_PATH = "/proc/self/ns/ipc";
constexpr const char *SHM_NAME_PREFIX = "/ubsio_standalone_slot_lease_";

#ifdef DEBUG_UT
std::string gShmNameForTest;
#endif

enum class InitializationState : uint32_t {
    READY = 1,
    INITIALIZING = 2,
    FAILED = 3,
};

enum class ProcessStatus {
    DEAD,
    ALIVE,
    UNKNOWN,
};

struct SlotOwner {
    uint32_t occupied;
    int32_t pid;
    uint64_t pidStartTime;
    uint64_t generation;
};

struct SlotLeaseHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t structSize;
    uint32_t slotCount;
    uint32_t initializationState;
    int32_t initializerPid;
    uint64_t initializerStartTime;
    uint64_t nextGeneration;
    SlotOwner slots[DEVICE_SIZE];
};

struct ProcessInfo {
    uint64_t startTime;
    uint64_t threadCount;
    char state;
};

uint64_t MonotonicTimeNs()
{
    struct timespec now = {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(now.tv_sec) * NANOSECONDS_PER_SECOND + static_cast<uint64_t>(now.tv_nsec);
}

bool ReadProcessInfo(int32_t pid, ProcessInfo &processInfo, ProcessStatus &status)
{
    processInfo = {};
    status = ProcessStatus::DEAD;
    if (pid <= 0) {
        return false;
    }
    errno = 0;
    std::ifstream statFile("/proc/" + std::to_string(pid) + "/stat");
    if (!statFile.is_open()) {
        if (errno != ENOENT && errno != ESRCH) {
            status = ProcessStatus::UNKNOWN;
        }
        return false;
    }
    std::string line;
    std::getline(statFile, line);
    auto commandEnd = line.rfind(')');
    if (commandEnd == std::string::npos || commandEnd + PROC_STAT_STATE_OFFSET_FROM_COMM_END >= line.size()) {
        status = ProcessStatus::UNKNOWN;
        return false;
    }
    std::istringstream fields(line.substr(commandEnd + PROC_STAT_STATE_OFFSET_FROM_COMM_END));
    std::string field;
    for (uint32_t fieldIndex = PROC_STAT_STATE_FIELD_INDEX; fieldIndex < PROC_STAT_START_TIME_FIELD_INDEX;
         ++fieldIndex) {
        if (!(fields >> field)) {
            status = ProcessStatus::UNKNOWN;
            return false;
        }
        if (fieldIndex == PROC_STAT_STATE_FIELD_INDEX) {
            processInfo.state = field.front();
        } else if (fieldIndex == PROC_STAT_THREAD_COUNT_FIELD_INDEX) {
            std::istringstream threadCountField(field);
            if (!(threadCountField >> processInfo.threadCount)) {
                status = ProcessStatus::UNKNOWN;
                return false;
            }
        }
    }
    if (!(fields >> processInfo.startTime)) {
        status = ProcessStatus::UNKNOWN;
        return false;
    }
    status = ProcessStatus::ALIVE;
    return true;
}

ProcessStatus ReadThreadGroupStatus(const ProcessInfo &processInfo)
{
    if (processInfo.state != 'Z' && processInfo.state != 'X' && processInfo.state != 'x') {
        return ProcessStatus::ALIVE;
    }
    if (processInfo.threadCount > 1) {
        return ProcessStatus::ALIVE;
    }
    return processInfo.threadCount == 1 ? ProcessStatus::DEAD : ProcessStatus::UNKNOWN;
}

ProcessStatus ReadProcessStatus(int32_t pid, const ProcessInfo &processInfo)
{
#ifdef SYS_pidfd_open
    int32_t pidFd = static_cast<int32_t>(syscall(SYS_pidfd_open, pid, 0));
    if (pidFd < 0) {
        if (errno == ESRCH) {
            return ProcessStatus::DEAD;
        }
    } else {
        struct pollfd event = {};
        event.fd = pidFd;
        event.events = POLLIN;
        int32_t pollResult = 0;
        do {
            pollResult = poll(&event, 1, 0);
        } while (pollResult < 0 && errno == EINTR);
        (void)close(pidFd);
        if (pollResult == 0) {
            return ProcessStatus::ALIVE;
        }
        if (pollResult > 0 && (event.revents & POLLIN) != 0) {
            return ProcessStatus::DEAD;
        }
    }
#endif
    // On kernels without pidfd support, stat keeps the full thread count while a zombie leader has live workers.
    return ReadThreadGroupStatus(processInfo);
}

bool ProcessIsAlive(int32_t pid, uint64_t expectedStartTime)
{
    ProcessInfo processInfo = {};
    ProcessStatus status = ProcessStatus::DEAD;
    if (!ReadProcessInfo(pid, processInfo, status)) {
        return status == ProcessStatus::UNKNOWN;
    }
    if (processInfo.startTime != expectedStartTime) {
        return false;
    }
    // A thread-group leader may be a zombie while other threads are still running. A pidfd becomes readable only
    // after the whole process exits, so an uncertain result must retain the lease to avoid duplicate slot ownership.
    return ReadProcessStatus(pid, processInfo) != ProcessStatus::DEAD;
}

bool HeaderValid(const SlotLeaseHeader &header)
{
    return header.magic == SLOT_LEASE_MAGIC && header.version == SLOT_LEASE_VERSION &&
        header.structSize == sizeof(SlotLeaseHeader) && header.slotCount > 0 && header.slotCount <= DEVICE_SIZE;
}

void InitializeHeader(SlotLeaseHeader &header, uint32_t slotCount, int32_t pid, uint64_t pidStartTime)
{
    header = {};
    header.magic = SLOT_LEASE_MAGIC;
    header.version = SLOT_LEASE_VERSION;
    header.structSize = sizeof(SlotLeaseHeader);
    header.slotCount = slotCount;
    header.initializationState = static_cast<uint32_t>(InitializationState::INITIALIZING);
    header.initializerPid = pid;
    header.initializerStartTime = pidStartTime;
    header.nextGeneration = 1;
}

uint32_t RemoveDeadOwners(SlotLeaseHeader &header)
{
    uint32_t liveCount = 0;
    for (auto &slot : header.slots) {
        if (slot.occupied == 0) {
            continue;
        }
        if (ProcessIsAlive(slot.pid, slot.pidStartTime)) {
            ++liveCount;
        } else {
            slot = {};
        }
    }
    return liveCount;
}

bool InitializerIsAlive(const SlotLeaseHeader &header)
{
    return header.initializerPid > 0 && ProcessIsAlive(header.initializerPid, header.initializerStartTime);
}

int32_t LockFile(int32_t fd)
{
    int32_t ret;
    do {
        ret = flock(fd, LOCK_EX);
    } while (ret != 0 && errno == EINTR);
    return ret;
}

void UnlockFile(int32_t fd)
{
    if (flock(fd, LOCK_UN) != 0) {
        LOG_WARN("Unlock standalone slot lease shared memory failed, errno:" << errno <<
            ", error:" << strerror(errno) << ".");
    }
}

void CloseLeaseFile(int32_t fd)
{
    if (fd >= 0) {
        (void)close(fd);
    }
}

bool ReadHeader(int32_t fd, SlotLeaseHeader &header)
{
    auto *bytes = reinterpret_cast<char *>(&header);
    size_t offset = 0;
    while (offset < sizeof(header)) {
        ssize_t readSize = pread(fd, bytes + offset, sizeof(header) - offset, static_cast<off_t>(offset));
        if (readSize < 0 && errno == EINTR) {
            continue;
        }
        if (readSize <= 0) {
            LOG_ERROR("Read standalone slot lease header failed, offset:" << offset << ", result:" << readSize <<
                ", errno:" << (readSize < 0 ? errno : 0) << ".");
            return false;
        }
        offset += static_cast<size_t>(readSize);
    }
    return true;
}

bool WriteHeader(int32_t fd, const SlotLeaseHeader &header)
{
    const auto *bytes = reinterpret_cast<const char *>(&header);
    size_t offset = 0;
    while (offset < sizeof(header)) {
        ssize_t written = pwrite(fd, bytes + offset, sizeof(header) - offset, static_cast<off_t>(offset));
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            LOG_ERROR("Write standalone slot lease header failed, offset:" << offset << ", result:" << written <<
                ", errno:" << (written < 0 ? errno : 0) << ".");
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool IsCurrentSharedMemory(int32_t fd, const std::string &shmName)
{
    int32_t currentFd = shm_open(shmName.c_str(), O_RDWR | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (currentFd < 0) {
        return false;
    }
    struct stat openedStat = {};
    struct stat currentStat = {};
    bool isCurrent = fstat(fd, &openedStat) == 0 && fstat(currentFd, &currentStat) == 0 &&
        openedStat.st_dev == currentStat.st_dev && openedStat.st_ino == currentStat.st_ino;
    (void)close(currentFd);
    return isCurrent;
}

void UnlinkSharedMemory(const std::string &shmName)
{
    if (!shmName.empty() && shm_unlink(shmName.c_str()) != 0 && errno != ENOENT) {
        LOG_WARN("Unlink standalone slot lease shared memory failed, name:" << shmName <<
            ", errno:" << errno << ", error:" << strerror(errno) << ".");
    }
}


}

StandaloneSlotLease::~StandaloneSlotLease()
{
    Release();
}

BResult StandaloneSlotLease::BuildShmName(std::string &shmName)
{
#ifdef DEBUG_UT
    if (!gShmNameForTest.empty()) {
        shmName = gShmNameForTest;
        return BIO_OK;
    }
#endif
    struct stat mountNamespace = {};
    struct stat ipcNamespace = {};
    if (stat(MNT_NAMESPACE_PATH, &mountNamespace) != 0 || stat(IPC_NAMESPACE_PATH, &ipcNamespace) != 0) {
        LOG_ERROR("Get standalone container namespace identity failed, errno:" << errno <<
            ", error:" << strerror(errno) << ".");
        return BIO_INNER_ERR;
    }
    std::ostringstream name;
    name << SHM_NAME_PREFIX << std::hex << static_cast<uint64_t>(mountNamespace.st_dev) << "_" <<
        static_cast<uint64_t>(mountNamespace.st_ino) << "_" << static_cast<uint64_t>(ipcNamespace.st_dev) << "_" <<
        static_cast<uint64_t>(ipcNamespace.st_ino);
    shmName = name.str();
    return BIO_OK;
}

#ifdef DEBUG_UT
void StandaloneSlotLease::SetShmNameForTest(const std::string &shmName)
{
    gShmNameForTest = shmName;
}
#endif

BResult StandaloneSlotLease::Acquire(uint32_t slotCount, uint32_t &slotIndex)
{
    for (uint32_t attempt = 0; attempt < SHM_OPEN_RETRY_TIMES; ++attempt) {
        bool retryStale = false;
        auto ret = AcquireOnce(slotCount, slotIndex, retryStale);
        if (!retryStale) {
            return ret;
        }
    }
    LOG_ERROR("Recover stale standalone slot lease exceeded retry limit.");
    return BIO_NOT_READY;
}

BResult StandaloneSlotLease::AcquireOnce(uint32_t slotCount, uint32_t &slotIndex, bool &retryStale)
{
    retryStale = false;
    if (IsAcquired() && mPid != static_cast<int32_t>(getpid())) {
        CloseLeaseFile(mFd);
        ResetLocalState();
    }
    if (IsAcquired()) {
        slotIndex = mSlotIndex;
        return BIO_OK;
    }
    if (slotCount == 0 || slotCount > DEVICE_SIZE) {
        LOG_ERROR("Invalid standalone slot count:" << slotCount << ".");
        return BIO_INVALID_PARAM;
    }

    mPid = static_cast<int32_t>(getpid());
    ProcessInfo selfInfo = {};
    ProcessStatus selfStatus = ProcessStatus::DEAD;
    if (!ReadProcessInfo(mPid, selfInfo, selfStatus) || selfStatus != ProcessStatus::ALIVE) {
        LOG_ERROR("Read standalone slot owner process start time failed, pid:" << mPid << ".");
        return BIO_INNER_ERR;
    }
    mPidStartTime = selfInfo.startTime;

    std::string shmName;
    auto ret = BuildShmName(shmName);
    if (ret != BIO_OK) {
        return ret;
    }
    int32_t fd = -1;
    struct stat shmStat = {};
    for (uint32_t attempt = 0; attempt < SHM_OPEN_RETRY_TIMES; ++attempt) {
        fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            LOG_ERROR("Open standalone slot lease shared memory failed, name:" << shmName <<
                ", errno:" << errno << ", error:" << strerror(errno) << ".");
            return BIO_INNER_ERR;
        }
        if (LockFile(fd) != 0) {
            LOG_ERROR("Lock standalone slot lease shared memory failed, errno:" << errno <<
                ", error:" << strerror(errno) << ".");
            (void)close(fd);
            return BIO_INNER_ERR;
        }
        if (fstat(fd, &shmStat) != 0 || shmStat.st_uid != geteuid()) {
            LOG_ERROR("Validate standalone slot lease shared memory failed, errno:" << errno << ".");
            UnlockFile(fd);
            (void)close(fd);
            return BIO_INNER_ERR;
        }
        if (IsCurrentSharedMemory(fd, shmName)) {
            break;
        }
        UnlockFile(fd);
        (void)close(fd);
        fd = -1;
    }
    if (fd < 0) {
        LOG_ERROR("Open current standalone slot lease shared memory exceeded retry limit, name:" << shmName << ".");
        return BIO_NOT_READY;
    }
    if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        LOG_ERROR("Set standalone slot lease shared memory permission failed, errno:" << errno << ".");
        UnlockFile(fd);
        (void)close(fd);
        return BIO_INNER_ERR;
    }
    bool storageCreated = shmStat.st_size == 0;
    if (storageCreated && ftruncate(fd, sizeof(SlotLeaseHeader)) != 0) {
        LOG_ERROR("Resize standalone slot lease shared memory failed, errno:" << errno << ".");
        if (IsCurrentSharedMemory(fd, shmName)) {
            UnlinkSharedMemory(shmName);
        }
        UnlockFile(fd);
        (void)close(fd);
        return BIO_INNER_ERR;
    }
    if (!storageCreated && shmStat.st_size != static_cast<off_t>(sizeof(SlotLeaseHeader))) {
        LOG_ERROR("Unexpected standalone slot lease shared memory size, actual:" << shmStat.st_size <<
            ", expected:" << sizeof(SlotLeaseHeader) << ".");
        UnlockFile(fd);
        (void)close(fd);
        return BIO_INNER_ERR;
    }

    int32_t allocationError = posix_fallocate(fd, 0, sizeof(SlotLeaseHeader));
    if (allocationError != 0) {
        LOG_ERROR("Reserve standalone slot lease shared memory failed, error:" << allocationError << ".");
        if (storageCreated && IsCurrentSharedMemory(fd, shmName)) {
            UnlinkSharedMemory(shmName);
        }
        UnlockFile(fd);
        (void)close(fd);
        return BIO_INNER_ERR;
    }
    SlotLeaseHeader header = {};
    uint64_t waitStartNs = MonotonicTimeNs();
    bool waitedForInitialization = false;
    bool initializer = false;

    while (true) {
        if (!IsCurrentSharedMemory(fd, shmName)) {
            LOG_ERROR("Standalone slot lease shared memory was replaced while acquiring a slot.");
            UnlockFile(fd);
            CloseLeaseFile(fd);
            return BIO_NOT_READY;
        }
        if (!storageCreated && !ReadHeader(fd, header)) {
            UnlockFile(fd);
            CloseLeaseFile(fd);
            return BIO_INNER_ERR;
        }
        if (storageCreated || !HeaderValid(header)) {
            InitializeHeader(header, slotCount, mPid, mPidStartTime);
            initializer = true;
            storageCreated = false;
        } else {
            uint32_t liveCount = RemoveDeadOwners(header);
            if (header.initializationState == static_cast<uint32_t>(InitializationState::FAILED)) {
                LOG_ERROR("Standalone initialization failed.");
                if (liveCount == 0 && IsCurrentSharedMemory(fd, shmName)) {
                    UnlinkSharedMemory(shmName);
                }
                // Only an already-failed, ownerless generation may be retried. A caller
                // that waited for this generation must report its initialization failure.
                retryStale = liveCount == 0 && !waitedForInitialization && !IsCurrentSharedMemory(fd, shmName);
                UnlockFile(fd);
                CloseLeaseFile(fd);
                return BIO_NOT_READY;
            }
            if (header.initializationState == static_cast<uint32_t>(InitializationState::INITIALIZING) &&
                !InitializerIsAlive(header)) {
                LOG_ERROR("Standalone initializer exited before publishing ready, initializerPid:" <<
                    header.initializerPid << ".");
                header.initializationState = static_cast<uint32_t>(InitializationState::FAILED);
                header.initializerPid = 0;
                header.initializerStartTime = 0;
                if (!WriteHeader(fd, header)) {
                    UnlockFile(fd);
                    CloseLeaseFile(fd);
                    return BIO_INNER_ERR;
                }
                UnlinkSharedMemory(shmName);
                UnlockFile(fd);
                CloseLeaseFile(fd);
                return BIO_NOT_READY;
            }
            if (header.slotCount != slotCount) {
                if (liveCount == 0) {
                    InitializeHeader(header, slotCount, mPid, mPidStartTime);
                    initializer = true;
                } else {
                    LOG_ERROR("Standalone slot count mismatch while leases are active, configured:" <<
                        slotCount << ", active:" << header.slotCount << ".");
                    UnlockFile(fd);
                    CloseLeaseFile(fd);
                    return BIO_INVALID_PARAM;
                }
            } else if (header.initializationState == static_cast<uint32_t>(InitializationState::INITIALIZING)) {
                if (header.initializerPid != mPid || header.initializerStartTime != mPidStartTime) {
                    uint64_t nowNs = MonotonicTimeNs();
                    if (waitStartNs == 0 || nowNs == 0 || nowNs - waitStartNs >= INITIALIZATION_READY_TIMEOUT_NS) {
                        LOG_ERROR("Wait standalone initialization timeout, initializerPid:" <<
                            header.initializerPid << ".");
                        UnlockFile(fd);
                        CloseLeaseFile(fd);
                        return BIO_NOT_READY;
                    }
                    UnlockFile(fd);
                    waitedForInitialization = true;
                    (void)usleep(INITIALIZATION_READY_POLL_INTERVAL_US);
                    if (LockFile(fd) != 0) {
                        CloseLeaseFile(fd);
                        return BIO_INNER_ERR;
                    }
                    continue;
                }
            } else if (header.initializationState != static_cast<uint32_t>(InitializationState::READY)) {
                LOG_ERROR("Invalid standalone slot initialization state:" << header.initializationState << ".");
                UnlockFile(fd);
                CloseLeaseFile(fd);
                return BIO_INNER_ERR;
            }
        }

        uint32_t freeSlotIndex = UINT32_MAX;
        uint32_t selectedSlotIndex = UINT32_MAX;
        for (uint32_t index = 0; index < header.slotCount; ++index) {
            auto &slot = header.slots[index];
            if (slot.occupied != 0 && slot.pid == mPid && slot.pidStartTime == mPidStartTime) {
                selectedSlotIndex = index;
                break;
            }
            if (slot.occupied == 0 && freeSlotIndex == UINT32_MAX) {
                freeSlotIndex = index;
            }
        }
        if (selectedSlotIndex == UINT32_MAX) {
            selectedSlotIndex = freeSlotIndex;
        }
        if (selectedSlotIndex == UINT32_MAX) {
            LOG_ERROR("No free standalone disk slot, slotCount:" << header.slotCount << ".");
            UnlockFile(fd);
            CloseLeaseFile(fd);
            return BIO_NOT_READY;
        }
        auto &owner = header.slots[selectedSlotIndex];
        if (owner.occupied == 0) {
            owner.occupied = 1;
            owner.pid = mPid;
            owner.pidStartTime = mPidStartTime;
            owner.generation = header.nextGeneration++;
            if (header.nextGeneration == 0) {
                header.nextGeneration = 1;
            }
        }
        if (!WriteHeader(fd, header)) {
            UnlockFile(fd);
            CloseLeaseFile(fd);
            return BIO_INNER_ERR;
        }
        slotIndex = selectedSlotIndex;
        mFd = fd;
        mSlotIndex = selectedSlotIndex;
        mInitializer = initializer;
        mShmName = shmName;
        LOG_INFO("Acquired standalone disk slot, slotIndex:" << slotIndex << ", slotCount:" << slotCount <<
            ", pid:" << mPid << ", generation:" << owner.generation <<
            ", initializer:" << (mInitializer ? 1 : 0) << ".");
        UnlockFile(fd);
        return BIO_OK;
    }
}

BResult StandaloneSlotLease::PublishReady()
{
    if (!IsAcquired() || !mInitializer) {
        return BIO_OK;
    }
    if (LockFile(mFd) != 0) {
        return BIO_INNER_ERR;
    }
    SlotLeaseHeader header = {};
    if (!ReadHeader(mFd, header)) {
        UnlockFile(mFd);
        return BIO_INNER_ERR;
    }
    if (!HeaderValid(header) || header.initializationState != static_cast<uint32_t>(InitializationState::INITIALIZING) ||
        header.initializerPid != mPid || header.initializerStartTime != mPidStartTime) {
        UnlockFile(mFd);
        return BIO_INNER_ERR;
    }
    header.initializationState = static_cast<uint32_t>(InitializationState::READY);
    header.initializerPid = 0;
    header.initializerStartTime = 0;
    if (!WriteHeader(mFd, header)) {
        UnlockFile(mFd);
        return BIO_INNER_ERR;
    }
    mInitializer = false;
    UnlockFile(mFd);
    LOG_INFO("Published standalone initialization ready, slotIndex:" << mSlotIndex << ".");
    return BIO_OK;
}

void StandaloneSlotLease::Release()
{
    if (!IsAcquired()) {
        return;
    }
    if (mPid != static_cast<int32_t>(getpid())) {
        CloseLeaseFile(mFd);
        ResetLocalState();
        return;
    }
    if (LockFile(mFd) == 0) {
        SlotLeaseHeader header = {};
        if (ReadHeader(mFd, header) && HeaderValid(header) && mSlotIndex < header.slotCount) {
            auto &owner = header.slots[mSlotIndex];
            if (owner.occupied != 0 && owner.pid == mPid && owner.pidStartTime == mPidStartTime) {
                owner = {};
            }
            if (mInitializer && header.initializationState == static_cast<uint32_t>(InitializationState::INITIALIZING) &&
                header.initializerPid == mPid && header.initializerStartTime == mPidStartTime) {
                header.initializationState = static_cast<uint32_t>(InitializationState::FAILED);
                header.initializerPid = 0;
                header.initializerStartTime = 0;
            }
            uint32_t liveCount = RemoveDeadOwners(header);
            if (!WriteHeader(mFd, header)) {
                LOG_ERROR("Persist standalone slot lease release failed, slotIndex:" << mSlotIndex << ".");
            }
            if (liveCount == 0 && IsCurrentSharedMemory(mFd, mShmName)) {
                UnlinkSharedMemory(mShmName);
            }
        }
        UnlockFile(mFd);
    }
    LOG_INFO("Released standalone disk slot, slotIndex:" << mSlotIndex << ", pid:" << mPid << ".");
    CloseLeaseFile(mFd);
    ResetLocalState();
}

void StandaloneSlotLease::ResetLocalState()
{
    mFd = -1;
    mSlotIndex = UINT32_MAX;
    mPid = -1;
    mPidStartTime = 0;
    mInitializer = false;
    mShmName.clear();
}

}
}
