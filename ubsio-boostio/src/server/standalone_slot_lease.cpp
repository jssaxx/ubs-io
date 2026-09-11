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
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "bio_log.h"
#include "bio_types.h"

namespace ock {
namespace bio {
namespace {

constexpr uint32_t SLOT_LEASE_MAGIC = 0x5542534CU; // "UBSL"
constexpr uint32_t SLOT_LEASE_VERSION = 1U;
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;
constexpr useconds_t LAYOUT_READY_POLL_INTERVAL_US = 10000U;
constexpr uint64_t LAYOUT_READY_TIMEOUT_NS = 180ULL * 1000ULL * 1000ULL * 1000ULL;
constexpr uint32_t SHM_OPEN_RETRY_TIMES = 100U;
constexpr const char *MNT_NAMESPACE_PATH = "/proc/self/ns/mnt";
constexpr const char *IPC_NAMESPACE_PATH = "/proc/self/ns/ipc";
constexpr const char *SHM_NAME_PREFIX = "/ubsio_standalone_slot_lease_";

#ifdef DEBUG_UT
std::string gShmNameForTest;
#endif

enum class LayoutState : uint32_t {
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
    uint64_t layoutFingerprint;
    uint32_t layoutState;
    int32_t initializerPid;
    uint64_t initializerStartTime;
    uint64_t nextGeneration;
    SlotOwner slots[DEVICE_SIZE];
};

uint64_t MonotonicTimeNs()
{
    struct timespec now = {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<uint64_t>(now.tv_nsec);
}

bool ReadProcessStartTime(int32_t pid, uint64_t &startTime, ProcessStatus &status)
{
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
    if (commandEnd == std::string::npos || commandEnd + 2 >= line.size()) {
        status = ProcessStatus::UNKNOWN;
        return false;
    }
    std::istringstream fields(line.substr(commandEnd + 2));
    std::string field;
    for (uint32_t fieldIndex = 3; fieldIndex <= 21; ++fieldIndex) {
        if (!(fields >> field)) {
            status = ProcessStatus::UNKNOWN;
            return false;
        }
        // Zombies retain their /proc entry and start time until reaped; Z/X/x cannot own a live lease.
        if (fieldIndex == 3 && (field == "Z" || field == "X" || field == "x")) {
            return false;
        }
    }
    if (!(fields >> startTime)) {
        status = ProcessStatus::UNKNOWN;
        return false;
    }
    status = ProcessStatus::ALIVE;
    return true;
}

bool ProcessIsAlive(int32_t pid, uint64_t expectedStartTime)
{
    uint64_t currentStartTime = 0;
    ProcessStatus status = ProcessStatus::DEAD;
    if (!ReadProcessStartTime(pid, currentStartTime, status)) {
        return status == ProcessStatus::UNKNOWN;
    }
    return currentStartTime == expectedStartTime;
}

bool HeaderValid(const SlotLeaseHeader &header)
{
    return header.magic == SLOT_LEASE_MAGIC && header.version == SLOT_LEASE_VERSION &&
        header.structSize == sizeof(SlotLeaseHeader) && header.slotCount > 0 && header.slotCount <= DEVICE_SIZE;
}

void InitializeHeader(SlotLeaseHeader &header, uint32_t slotCount, uint64_t layoutFingerprint, int32_t pid,
    uint64_t pidStartTime)
{
    header = {};
    header.magic = SLOT_LEASE_MAGIC;
    header.version = SLOT_LEASE_VERSION;
    header.structSize = sizeof(SlotLeaseHeader);
    header.slotCount = slotCount;
    header.layoutFingerprint = layoutFingerprint;
    header.layoutState = static_cast<uint32_t>(LayoutState::INITIALIZING);
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

void CloseMapping(int32_t fd, void *mapping)
{
    if (mapping != nullptr) {
        (void)munmap(mapping, sizeof(SlotLeaseHeader));
    }
    if (fd >= 0) {
        (void)close(fd);
    }
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

void HashBytes(uint64_t &hash, const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= FNV_PRIME;
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

uint64_t StandaloneSlotLease::BuildLayoutFingerprint(uint32_t slotCount,
    const std::vector<std::string> &diskPaths, const std::vector<int64_t> &diskCaps, uint64_t segmentSize)
{
    uint64_t hash = FNV_OFFSET_BASIS;
    HashBytes(hash, &slotCount, sizeof(slotCount));
    HashBytes(hash, &segmentSize, sizeof(segmentSize));
    uint64_t diskCount = diskPaths.size();
    HashBytes(hash, &diskCount, sizeof(diskCount));
    for (size_t index = 0; index < diskPaths.size(); ++index) {
        uint64_t pathSize = diskPaths[index].size();
        HashBytes(hash, &pathSize, sizeof(pathSize));
        HashBytes(hash, diskPaths[index].data(), diskPaths[index].size());
        HashBytes(hash, &diskCaps[index], sizeof(diskCaps[index]));
    }
    return hash;
}

BResult StandaloneSlotLease::Acquire(uint32_t slotCount, const std::vector<std::string> &diskPaths,
    const std::vector<int64_t> &diskCaps, uint64_t segmentSize, uint32_t &slotIndex)
{
    if (IsAcquired() && mPid != static_cast<int32_t>(getpid())) {
        CloseMapping(mFd, mMapping);
        ResetLocalState();
    }
    if (IsAcquired()) {
        slotIndex = mSlotIndex;
        return BIO_OK;
    }
    if (slotCount == 0 || slotCount > DEVICE_SIZE || diskPaths.empty() || diskPaths.size() != diskCaps.size()) {
        LOG_ERROR("Invalid standalone slot lease config, slotCount:" << slotCount <<
            ", diskPathCount:" << diskPaths.size() << ", diskCapCount:" << diskCaps.size() << ".");
        return BIO_INVALID_PARAM;
    }

    mPid = static_cast<int32_t>(getpid());
    ProcessStatus selfStatus = ProcessStatus::DEAD;
    if (!ReadProcessStartTime(mPid, mPidStartTime, selfStatus) || selfStatus != ProcessStatus::ALIVE) {
        LOG_ERROR("Read standalone slot owner process start time failed, pid:" << mPid << ".");
        return BIO_INNER_ERR;
    }

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

    void *mapping = mmap(nullptr, sizeof(SlotLeaseHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        LOG_ERROR("Map standalone slot lease shared memory failed, errno:" << errno << ".");
        UnlockFile(fd);
        (void)close(fd);
        return BIO_INNER_ERR;
    }
    auto *header = static_cast<SlotLeaseHeader *>(mapping);
    uint64_t fingerprint = BuildLayoutFingerprint(slotCount, diskPaths, diskCaps, segmentSize);
    uint64_t waitStartNs = MonotonicTimeNs();

    while (true) {
        if (storageCreated || !HeaderValid(*header)) {
            InitializeHeader(*header, slotCount, fingerprint, mPid, mPidStartTime);
            mLayoutInitializer = true;
            storageCreated = false;
        } else {
            uint32_t liveCount = RemoveDeadOwners(*header);
            if (header->layoutState == static_cast<uint32_t>(LayoutState::FAILED)) {
                LOG_ERROR("Standalone slot layout initialization failed.");
                if (liveCount == 0 && IsCurrentSharedMemory(fd, shmName)) {
                    UnlinkSharedMemory(shmName);
                }
                UnlockFile(fd);
                CloseMapping(fd, mapping);
                return BIO_NOT_READY;
            }
            if (header->layoutState == static_cast<uint32_t>(LayoutState::INITIALIZING) &&
                !InitializerIsAlive(*header)) {
                LOG_ERROR("Standalone slot layout initializer exited before publishing ready, initializerPid:" <<
                    header->initializerPid << ".");
                header->layoutState = static_cast<uint32_t>(LayoutState::FAILED);
                header->initializerPid = 0;
                header->initializerStartTime = 0;
                UnlinkSharedMemory(shmName);
                UnlockFile(fd);
                CloseMapping(fd, mapping);
                return BIO_NOT_READY;
            }
            bool layoutMatches = header->slotCount == slotCount && header->layoutFingerprint == fingerprint;
            if (!layoutMatches) {
                if (liveCount != 0) {
                    LOG_ERROR("Standalone slot layout mismatch while leases are active, configuredSlotCount:" <<
                        slotCount << ", activeSlotCount:" << header->slotCount << ".");
                    UnlockFile(fd);
                    CloseMapping(fd, mapping);
                    return BIO_INVALID_PARAM;
                }
                InitializeHeader(*header, slotCount, fingerprint, mPid, mPidStartTime);
                mLayoutInitializer = true;
            } else if (header->layoutState == static_cast<uint32_t>(LayoutState::INITIALIZING)) {
                if (header->initializerPid != mPid || header->initializerStartTime != mPidStartTime) {
                    uint64_t nowNs = MonotonicTimeNs();
                    if (waitStartNs == 0 || nowNs == 0 || nowNs - waitStartNs >= LAYOUT_READY_TIMEOUT_NS) {
                        LOG_ERROR("Wait standalone slot layout initialization timeout, initializerPid:" <<
                            header->initializerPid << ".");
                        UnlockFile(fd);
                        CloseMapping(fd, mapping);
                        return BIO_NOT_READY;
                    }
                    UnlockFile(fd);
                    (void)usleep(LAYOUT_READY_POLL_INTERVAL_US);
                    if (LockFile(fd) != 0) {
                        CloseMapping(fd, mapping);
                        return BIO_INNER_ERR;
                    }
                    continue;
                }
            } else if (header->layoutState != static_cast<uint32_t>(LayoutState::READY)) {
                LOG_ERROR("Invalid standalone slot layout state:" << header->layoutState << ".");
                UnlockFile(fd);
                CloseMapping(fd, mapping);
                return BIO_INNER_ERR;
            }
        }

        if (!IsCurrentSharedMemory(fd, shmName)) {
            LOG_ERROR("Standalone slot lease shared memory was replaced while acquiring a slot.");
            UnlockFile(fd);
            CloseMapping(fd, mapping);
            return BIO_NOT_READY;
        }

        uint32_t freeSlotIndex = UINT32_MAX;
        for (uint32_t index = 0; index < header->slotCount; ++index) {
            auto &slot = header->slots[index];
            if (slot.occupied != 0 && slot.pid == mPid && slot.pidStartTime == mPidStartTime) {
                mSlotIndex = index;
                break;
            }
            if (slot.occupied == 0 && freeSlotIndex == UINT32_MAX) {
                freeSlotIndex = index;
            }
        }
        if (mSlotIndex == UINT32_MAX) {
            mSlotIndex = freeSlotIndex;
        }
        if (mSlotIndex == UINT32_MAX) {
            LOG_ERROR("No free standalone disk slot, slotCount:" << header->slotCount << ".");
            UnlockFile(fd);
            CloseMapping(fd, mapping);
            return BIO_NOT_READY;
        }
        auto &owner = header->slots[mSlotIndex];
        if (owner.occupied == 0) {
            owner.occupied = 1;
            owner.pid = mPid;
            owner.pidStartTime = mPidStartTime;
            owner.generation = header->nextGeneration++;
            if (header->nextGeneration == 0) {
                header->nextGeneration = 1;
            }
        }
        slotIndex = mSlotIndex;
        mFd = fd;
        mMapping = mapping;
        mShmName = shmName;
        LOG_INFO("Acquired standalone disk slot, slotIndex:" << slotIndex << ", slotCount:" << slotCount <<
            ", pid:" << mPid << ", generation:" << owner.generation <<
            ", layoutInitializer:" << (mLayoutInitializer ? 1 : 0) << ".");
        UnlockFile(fd);
        return BIO_OK;
    }
}

BResult StandaloneSlotLease::PublishLayoutReady()
{
    if (!IsAcquired() || !mLayoutInitializer) {
        return BIO_OK;
    }
    if (LockFile(mFd) != 0) {
        return BIO_INNER_ERR;
    }
    auto *header = static_cast<SlotLeaseHeader *>(mMapping);
    if (!HeaderValid(*header) || header->layoutState != static_cast<uint32_t>(LayoutState::INITIALIZING) ||
        header->initializerPid != mPid || header->initializerStartTime != mPidStartTime) {
        UnlockFile(mFd);
        return BIO_INNER_ERR;
    }
    header->layoutState = static_cast<uint32_t>(LayoutState::READY);
    header->initializerPid = 0;
    header->initializerStartTime = 0;
    mLayoutInitializer = false;
    UnlockFile(mFd);
    LOG_INFO("Published standalone disk layout ready, slotIndex:" << mSlotIndex << ".");
    return BIO_OK;
}

void StandaloneSlotLease::Release()
{
    if (!IsAcquired()) {
        return;
    }
    if (mPid != static_cast<int32_t>(getpid())) {
        CloseMapping(mFd, mMapping);
        ResetLocalState();
        return;
    }
    if (LockFile(mFd) == 0) {
        auto *header = static_cast<SlotLeaseHeader *>(mMapping);
        if (HeaderValid(*header) && mSlotIndex < header->slotCount) {
            auto &owner = header->slots[mSlotIndex];
            if (owner.occupied != 0 && owner.pid == mPid && owner.pidStartTime == mPidStartTime) {
                owner = {};
            }
            if (mLayoutInitializer && header->layoutState == static_cast<uint32_t>(LayoutState::INITIALIZING) &&
                header->initializerPid == mPid && header->initializerStartTime == mPidStartTime) {
                header->layoutState = static_cast<uint32_t>(LayoutState::FAILED);
                header->initializerPid = 0;
                header->initializerStartTime = 0;
            }
            if (RemoveDeadOwners(*header) == 0) {
                UnlinkSharedMemory(mShmName);
            }
        }
        UnlockFile(mFd);
    }
    LOG_INFO("Released standalone disk slot, slotIndex:" << mSlotIndex << ", pid:" << mPid << ".");
    CloseMapping(mFd, mMapping);
    ResetLocalState();
}

void StandaloneSlotLease::ResetLocalState()
{
    mFd = -1;
    mMapping = nullptr;
    mSlotIndex = UINT32_MAX;
    mPid = -1;
    mPidStartTime = 0;
    mLayoutInitializer = false;
    mShmName.clear();
}

}
}
