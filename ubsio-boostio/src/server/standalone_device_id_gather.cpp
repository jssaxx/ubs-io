/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "standalone_device_id_gather.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

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

constexpr uint32_t GATHER_MAGIC = 0x55424944U; // "UBID"
constexpr uint32_t GATHER_VERSION = 1U;
constexpr uint64_t NS_PER_MS = 1000000ULL;
constexpr useconds_t GATHER_POLL_INTERVAL_US = 10000U;
constexpr const char *MNT_NAMESPACE_PATH = "/proc/self/ns/mnt";
constexpr const char *IPC_NAMESPACE_PATH = "/proc/self/ns/ipc";
constexpr const char *SHM_NAME_PREFIX = "/ubsio_device_id_gather_";

bool UnlinkSharedMemory(const std::string &shmName);

enum class GatherState : uint32_t {
    COLLECTING = 1,
    READY = 2,
    ERROR = 3,
};

struct GatherSlot {
    uint32_t occupied;
    uint32_t logicDeviceId;
    int32_t pid;
    uint32_t reserved;
    uint64_t pidStartTime;
};

struct GatherHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t structSize;
    uint32_t deviceCount;
    uint32_t participantCount;
    uint32_t state;
    int32_t errorCode;
    uint32_t reserved;
    uint64_t generation;
    uint64_t deadlineNs;
    GatherSlot slots[DEVICE_SIZE];
};

enum class ProcessStatus {
    DEAD,
    ALIVE,
    UNKNOWN,
};

class GatherMapping {
public:
    ~GatherMapping()
    {
        if (mLocked) {
            (void)Unlock();
        }
        if (mFd >= 0) {
            (void)close(mFd);
        }
    }

    BResult Open(const std::string &name)
    {
        mFd = shm_open(name.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, S_IRUSR | S_IWUSR);
        if (mFd < 0) {
            LOG_ERROR("Open standalone device ID shared memory failed, name:" << name <<
                ", errno:" << errno << ", error:" << strerror(errno) << ".");
            return BIO_INNER_ERR;
        }

        auto ret = Lock();
        if (ret != BIO_OK) {
            return ret;
        }

        struct stat shmStat = {};
        if (fstat(mFd, &shmStat) != 0) {
            LOG_ERROR("Stat standalone device ID shared memory failed, errno:" << errno <<
                ", error:" << strerror(errno) << ".");
            return BIO_INNER_ERR;
        }
        if (shmStat.st_uid != geteuid()) {
            LOG_ERROR("Standalone device ID shared memory owner mismatch, owner:" << shmStat.st_uid <<
                ", expected:" << geteuid() << ".");
            return BIO_INNER_ERR;
        }
        if (fchmod(mFd, S_IRUSR | S_IWUSR) != 0) {
            LOG_ERROR("Set standalone device ID shared memory permission failed, errno:" << errno <<
                ", error:" << strerror(errno) << ".");
            return BIO_INNER_ERR;
        }

        int pathFd = shm_open(name.c_str(), O_RDONLY, 0);
        if (pathFd < 0) {
            LOG_ERROR("Open standalone device ID shared memory path for inode check failed, name:" << name <<
                ", errno:" << errno << ", error:" << strerror(errno) << ".");
            return BIO_INNER_ERR;
        }
        struct stat pathStat = {};
        if (fstat(pathFd, &pathStat) != 0) {
            LOG_ERROR("Stat standalone device ID shared memory path failed, name:" << name <<
                ", errno:" << errno << ", error:" << strerror(errno) << ".");
            (void)close(pathFd);
            return BIO_INNER_ERR;
        }
        (void)close(pathFd);
        if (shmStat.st_dev != pathStat.st_dev || shmStat.st_ino != pathStat.st_ino) {
            LOG_ERROR("Standalone device ID shared memory path was replaced, name:" << name <<
                ", fdDev:" << shmStat.st_dev << ", fdIno:" << shmStat.st_ino <<
                ", pathDev:" << pathStat.st_dev << ", pathIno:" << pathStat.st_ino << ".");
            return BIO_INNER_ERR;
        }

        if (shmStat.st_size == 0) {
            if (ftruncate(mFd, sizeof(GatherHeader)) != 0) {
                LOG_ERROR("Resize standalone device ID shared memory failed, errno:" << errno <<
                    ", error:" << strerror(errno) << ".");
                return BIO_INNER_ERR;
            }
            mStorageCreated = true;
        } else if (shmStat.st_size != static_cast<off_t>(sizeof(GatherHeader))) {
            LOG_ERROR("Unexpected standalone device ID shared memory size, actual:" << shmStat.st_size <<
                ", expected:" << sizeof(GatherHeader) << ".");
            (void)UnlinkSharedMemory(name);
            return BIO_INNER_ERR;
        }

        ret = ReadHeader();
        if (ret != BIO_OK) {
            return ret;
        }
        return BIO_OK;
    }

    BResult Lock()
    {
        if (mLocked) {
            return BIO_OK;
        }
        int32_t ret;
        do {
            ret = flock(mFd, LOCK_EX);
        } while (ret != 0 && errno == EINTR);
        if (ret != 0) {
            LOG_ERROR("Lock standalone device ID shared memory failed, errno:" << errno <<
                ", error:" << strerror(errno) << ".");
            return BIO_INNER_ERR;
        }
        mLocked = true;
        return BIO_OK;
    }

    BResult Unlock()
    {
        if (!mLocked) {
            return BIO_OK;
        }
        BResult ret = BIO_OK;
        if (mHeaderValid) {
            ret = WriteHeader();
        }
        if (flock(mFd, LOCK_UN) != 0) {
            LOG_WARN("Unlock standalone device ID shared memory failed, errno:" << errno <<
                ", error:" << strerror(errno) << ".");
            if (ret == BIO_OK) {
                ret = BIO_INNER_ERR;
            }
        }
        mLocked = false;
        return ret;
    }

    BResult ReadHeader()
    {
        if (mFd < 0) {
            return BIO_INNER_ERR;
        }
        ssize_t bytes;
        do {
            bytes = pread(mFd, &mHeader, sizeof(mHeader), 0);
        } while (bytes < 0 && errno == EINTR);
        if (bytes != static_cast<ssize_t>(sizeof(mHeader))) {
            LOG_ERROR("Read standalone device ID shared memory header failed, bytes:" << bytes <<
                ", errno:" << errno << ", error:" << strerror(errno) << ".");
            mHeaderValid = false;
            return BIO_INNER_ERR;
        }
        mHeaderValid = true;
        return BIO_OK;
    }

    BResult WriteHeader()
    {
        if (mFd < 0) {
            return BIO_INNER_ERR;
        }
        ssize_t bytes;
        do {
            bytes = pwrite(mFd, &mHeader, sizeof(mHeader), 0);
        } while (bytes < 0 && errno == EINTR);
        if (bytes != static_cast<ssize_t>(sizeof(mHeader))) {
            LOG_ERROR("Write standalone device ID shared memory header failed, bytes:" << bytes <<
                ", errno:" << errno << ", error:" << strerror(errno) << ".");
            return BIO_INNER_ERR;
        }
        return BIO_OK;
    }

    GatherHeader *Header()
    {
        return &mHeader;
    }

    bool StorageCreated() const
    {
        return mStorageCreated;
    }

private:
    int32_t mFd { -1 };
    GatherHeader mHeader {};
    bool mLocked { false };
    bool mStorageCreated { false };
    bool mHeaderValid { false };
};

uint64_t MonotonicTimeNs()
{
    struct timespec now = {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<uint64_t>(now.tv_nsec);
}

bool HeaderValid(const GatherHeader &header)
{
    return header.magic == GATHER_MAGIC && header.version == GATHER_VERSION &&
        header.structSize == sizeof(GatherHeader) && header.deviceCount > 0 && header.deviceCount <= DEVICE_SIZE;
}

uint64_t NextGeneration(uint64_t generation)
{
    return generation == std::numeric_limits<uint64_t>::max() ? 1 : generation + 1;
}

void InitializeRound(GatherHeader &header, uint32_t deviceCount, uint64_t generation, uint64_t nowNs,
    uint64_t timeoutMs)
{
    (void)memset(&header, 0, sizeof(header));
    header.magic = GATHER_MAGIC;
    header.version = GATHER_VERSION;
    header.structSize = sizeof(GatherHeader);
    header.deviceCount = deviceCount;
    header.state = static_cast<uint32_t>(GatherState::COLLECTING);
    header.errorCode = BIO_OK;
    header.generation = generation == 0 ? 1 : generation;
    header.deadlineNs = nowNs + timeoutMs * NS_PER_MS;
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
    // The substring starts at field 3 (state). Skip fields 3 through 21.
    for (uint32_t fieldIndex = 3; fieldIndex <= 21; ++fieldIndex) {
        if (!(fields >> field)) {
            status = ProcessStatus::UNKNOWN;
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

bool SlotIsAlive(const GatherSlot &slot)
{
    if (slot.occupied == 0) {
        return false;
    }
    uint64_t currentStartTime = 0;
    ProcessStatus status = ProcessStatus::DEAD;
    if (!ReadProcessStartTime(slot.pid, currentStartTime, status)) {
        return status == ProcessStatus::UNKNOWN;
    }
    return currentStartTime == slot.pidStartTime;
}

uint32_t RemoveDeadSlots(GatherHeader &header)
{
    uint32_t liveCount = 0;
    for (auto &slot : header.slots) {
        if (slot.occupied == 0) {
            continue;
        }
        if (SlotIsAlive(slot)) {
            ++liveCount;
            continue;
        }
        (void)memset(&slot, 0, sizeof(slot));
    }
    header.participantCount = liveCount;
    return liveCount;
}

void SetRoundError(GatherHeader &header, BResult errorCode)
{
    header.state = static_cast<uint32_t>(GatherState::ERROR);
    header.errorCode = errorCode;
}

bool RegisterProcess(GatherHeader &header, uint32_t logicDeviceId, int32_t pid, uint64_t pidStartTime)
{
    GatherSlot *freeSlot = nullptr;
    for (auto &slot : header.slots) {
        if (slot.occupied == 0) {
            if (freeSlot == nullptr) {
                freeSlot = &slot;
            }
            continue;
        }
        if (slot.pid == pid && slot.pidStartTime == pidStartTime) {
            if (slot.logicDeviceId == logicDeviceId) {
                return true;
            }
            SetRoundError(header, BIO_INVALID_PARAM);
            return false;
        }
        if (slot.logicDeviceId == logicDeviceId) {
            LOG_ERROR("Duplicate standalone logic device ID, logicDeviceId:" << logicDeviceId <<
                ", existingPid:" << slot.pid << ", inputPid:" << pid << ".");
            SetRoundError(header, BIO_INVALID_PARAM);
            return false;
        }
    }

    if (freeSlot == nullptr || header.participantCount >= header.deviceCount) {
        LOG_ERROR("Standalone device ID gather has no free slot, participantCount:" << header.participantCount <<
            ", deviceCount:" << header.deviceCount << ".");
        SetRoundError(header, BIO_INVALID_PARAM);
        return false;
    }

    freeSlot->occupied = 1;
    freeSlot->logicDeviceId = logicDeviceId;
    freeSlot->pid = pid;
    freeSlot->pidStartTime = pidStartTime;
    ++header.participantCount;
    return true;
}

void RemoveRegistration(GatherHeader &header, int32_t pid, uint64_t pidStartTime)
{
    for (auto &slot : header.slots) {
        if (slot.occupied != 0 && slot.pid == pid && slot.pidStartTime == pidStartTime) {
            (void)memset(&slot, 0, sizeof(slot));
            break;
        }
    }
    uint32_t participantCount = 0;
    for (const auto &slot : header.slots) {
        participantCount += slot.occupied != 0 ? 1U : 0U;
    }
    header.participantCount = participantCount;
}

BResult CopyLogicDeviceIds(const GatherHeader &header, std::vector<uint32_t> &logicDeviceIds)
{
    logicDeviceIds.clear();
    logicDeviceIds.reserve(header.deviceCount);
    for (const auto &slot : header.slots) {
        if (slot.occupied != 0) {
            logicDeviceIds.emplace_back(slot.logicDeviceId);
        }
    }
    if (logicDeviceIds.size() != header.deviceCount) {
        LOG_ERROR("Standalone device ID gather result count mismatch, actual:" << logicDeviceIds.size() <<
            ", expected:" << header.deviceCount << ".");
        return BIO_INNER_ERR;
    }
    return BIO_OK;
}

std::string DeviceIdsToString(const std::vector<uint32_t> &deviceIds)
{
    std::ostringstream output;
    output << "[";
    for (size_t index = 0; index < deviceIds.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << deviceIds[index];
    }
    output << "]";
    return output.str();
}

BResult CalculateVirtualDeviceIndex(std::vector<uint32_t> &logicDeviceIds, uint32_t currentLogicDeviceId,
    uint32_t &virtualDeviceIndex)
{
    std::sort(logicDeviceIds.begin(), logicDeviceIds.end());
    if (std::adjacent_find(logicDeviceIds.begin(), logicDeviceIds.end()) != logicDeviceIds.end()) {
        LOG_ERROR("Standalone device ID gather contains duplicate logic IDs, logicDeviceIds:" <<
            DeviceIdsToString(logicDeviceIds) << ".");
        return BIO_INVALID_PARAM;
    }
    auto position = std::lower_bound(logicDeviceIds.begin(), logicDeviceIds.end(), currentLogicDeviceId);
    if (position == logicDeviceIds.end() || *position != currentLogicDeviceId) {
        LOG_ERROR("Current logic device ID is absent from gather result, logicDeviceId:" << currentLogicDeviceId <<
            ", logicDeviceIds:" << DeviceIdsToString(logicDeviceIds) << ".");
        return BIO_INNER_ERR;
    }
    virtualDeviceIndex = static_cast<uint32_t>(std::distance(logicDeviceIds.begin(), position));
    LOG_INFO("Map standalone logic device ID to virtual index, logicDeviceIds:" <<
        DeviceIdsToString(logicDeviceIds) << ", currentLogicDeviceId:" << currentLogicDeviceId <<
        ", virtualDeviceIndex:" << virtualDeviceIndex << ".");
    return BIO_OK;
}

bool UnlinkSharedMemory(const std::string &shmName)
{
    if (shm_unlink(shmName.c_str()) == 0 || errno == ENOENT) {
        return true;
    }
    LOG_WARN("Unlink standalone device ID shared memory failed, name:" << shmName <<
        ", errno:" << errno << ", error:" << strerror(errno) << ".");
    return false;
}

}

constexpr uint64_t StandaloneDeviceIdGather::DEFAULT_TIMEOUT_MS;

BResult StandaloneDeviceIdGather::BuildShmName(std::string &shmName)
{
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

BResult StandaloneDeviceIdGather::Gather(uint32_t logicDeviceId, uint32_t deviceCount,
    uint32_t &virtualDeviceIndex, uint64_t timeoutMs)
{
    LOG_INFO("Gather enter, logicDeviceId:" << logicDeviceId << ", deviceCount:" << deviceCount <<
        ", timeoutMs:" << timeoutMs << ".");
    if (deviceCount == 0 || deviceCount > DEVICE_SIZE || timeoutMs == 0 ||
        timeoutMs > std::numeric_limits<uint64_t>::max() / NS_PER_MS) {
        LOG_ERROR("Invalid standalone device ID gather input, logicDeviceId:" << logicDeviceId <<
            ", deviceCount:" << deviceCount << ", timeoutMs:" << timeoutMs << ".");
        return BIO_INVALID_PARAM;
    }
    if (deviceCount == 1) {
        virtualDeviceIndex = 0;
        LOG_INFO("Map single standalone logic device ID to virtual index 0, logicDeviceId:" << logicDeviceId << ".");
        return BIO_OK;
    }

    std::string shmName;
    auto ret = BuildShmName(shmName);
    if (ret != BIO_OK) {
        return ret;
    }

    uint64_t selfStartTime = 0;
    ProcessStatus selfStatus = ProcessStatus::DEAD;
    int32_t selfPid = static_cast<int32_t>(getpid());
    if (!ReadProcessStartTime(selfPid, selfStartTime, selfStatus) || selfStatus != ProcessStatus::ALIVE) {
        LOG_ERROR("Read current process start time failed, pid:" << selfPid << ".");
        return BIO_INNER_ERR;
    }

    // READY objects should normally already be unlinked. Retry once with a new
    // named object if a process died between publishing READY and unlinking it.
    for (uint32_t openAttempt = 0; openAttempt < 2; ++openAttempt) {
        GatherMapping mapping;
        ret = mapping.Open(shmName);
        if (ret != BIO_OK) {
            if (openAttempt == 0) {
                continue;
            }
            return ret;
        }
        LOG_INFO("Gather SHM opened, openAttempt:" << openAttempt <<
            ", storageCreated:" << (mapping.StorageCreated() ? 1 : 0) << ".");
        auto *header = mapping.Header();
        uint64_t nowNs = MonotonicTimeNs();
        if (nowNs == 0) {
            return BIO_INNER_ERR;
        }

        if (mapping.StorageCreated() || !HeaderValid(*header)) {
            uint64_t generation = HeaderValid(*header) ? NextGeneration(header->generation) : 1;
            InitializeRound(*header, deviceCount, generation, nowNs, timeoutMs);
        } else if (header->state == static_cast<uint32_t>(GatherState::READY)) {
            (void)UnlinkSharedMemory(shmName);
            ret = mapping.Unlock();
            if (ret != BIO_OK) {
                return ret;
            }
            continue;
        } else {
            uint32_t liveCount = RemoveDeadSlots(*header);
            bool expired = header->deadlineNs <= nowNs;
            if (liveCount == 0 || expired) {
                uint64_t generation = NextGeneration(header->generation);
                LOG_INFO("Reset stale standalone device ID gather round, oldGeneration:" << header->generation <<
                    ", liveCount:" << liveCount << ", expired:" << expired << ".");
                InitializeRound(*header, deviceCount, generation, nowNs, timeoutMs);
            } else if (header->state == static_cast<uint32_t>(GatherState::ERROR)) {
                BResult errorCode = header->errorCode == BIO_OK ? BIO_ERR : header->errorCode;
                LOG_ERROR("Standalone device ID gather round is in error state, generation:" << header->generation <<
                    ", result:" << errorCode << ".");
                return errorCode;
            } else if (header->state != static_cast<uint32_t>(GatherState::COLLECTING)) {
                LOG_ERROR("Invalid standalone device ID gather state, generation:" << header->generation <<
                    ", state:" << header->state << ".");
                SetRoundError(*header, BIO_INNER_ERR);
                return BIO_INNER_ERR;
            } else if (header->deviceCount != deviceCount) {
                LOG_ERROR("Standalone device count mismatch in active gather round, configured:" << deviceCount <<
                    ", active:" << header->deviceCount << ".");
                SetRoundError(*header, BIO_INVALID_PARAM);
                return BIO_INVALID_PARAM;
            }
        }

        if (!RegisterProcess(*header, logicDeviceId, selfPid, selfStartTime)) {
            return header->errorCode == BIO_OK ? BIO_ERR : header->errorCode;
        }
        LOG_INFO("Gather registered, generation:" << header->generation <<
            ", participantCount:" << header->participantCount << ", deviceCount:" << header->deviceCount << ".");
        uint64_t myGeneration = header->generation;

        std::vector<uint32_t> logicDeviceIds;
        if (header->participantCount == header->deviceCount) {
            header->state = static_cast<uint32_t>(GatherState::READY);
            ret = CopyLogicDeviceIds(*header, logicDeviceIds);
            (void)UnlinkSharedMemory(shmName);
            BResult unlockRet = mapping.Unlock();
            if (unlockRet != BIO_OK) {
                return unlockRet;
            }
            return ret == BIO_OK ? CalculateVirtualDeviceIndex(logicDeviceIds, logicDeviceId, virtualDeviceIndex) : ret;
        }
        ret = mapping.Unlock();
        if (ret != BIO_OK) {
            return ret;
        }
        LOG_INFO("Gather waiting for peers, generation:" << myGeneration <<
            ", participantCount:" << header->participantCount << ", deviceCount:" << header->deviceCount << ".");

        while (true) {
            (void)usleep(GATHER_POLL_INTERVAL_US);
            ret = mapping.Lock();
            if (ret != BIO_OK) {
                return ret;
            }
            ret = mapping.ReadHeader();
            if (ret != BIO_OK) {
                return ret;
            }
            header = mapping.Header();
            if (header->generation != myGeneration) {
                LOG_ERROR("Standalone device ID gather round was replaced, expectedGeneration:" << myGeneration <<
                    ", currentGeneration:" << header->generation << ".");
                return BIO_NOT_READY;
            }
            if (header->state == static_cast<uint32_t>(GatherState::READY)) {
                ret = CopyLogicDeviceIds(*header, logicDeviceIds);
                (void)UnlinkSharedMemory(shmName);
                BResult unlockRet = mapping.Unlock();
                if (unlockRet != BIO_OK) {
                    return unlockRet;
                }
                return ret == BIO_OK ?
                    CalculateVirtualDeviceIndex(logicDeviceIds, logicDeviceId, virtualDeviceIndex) : ret;
            }
            if (header->state == static_cast<uint32_t>(GatherState::ERROR)) {
                BResult errorCode = header->errorCode == BIO_OK ? BIO_ERR : header->errorCode;
                RemoveRegistration(*header, selfPid, selfStartTime);
                if (header->participantCount == 0) {
                    (void)UnlinkSharedMemory(shmName);
                }
                return errorCode;
            }

            nowNs = MonotonicTimeNs();
            if (nowNs == 0 || nowNs >= header->deadlineNs) {
                LOG_ERROR("Wait standalone device ID gather timeout, generation:" << myGeneration <<
                    ", participantCount:" << header->participantCount << ", deviceCount:" <<
                    header->deviceCount << ".");
                SetRoundError(*header, BIO_NOT_READY);
                RemoveRegistration(*header, selfPid, selfStartTime);
                if (header->participantCount == 0) {
                    (void)UnlinkSharedMemory(shmName);
                }
                return BIO_NOT_READY;
            }
            ret = mapping.Unlock();
            if (ret != BIO_OK) {
                return ret;
            }
        }
    }

    LOG_ERROR("Failed to create a fresh standalone device ID gather round, shmName:" << shmName << ".");
    return BIO_INNER_ERR;
}

}
}
