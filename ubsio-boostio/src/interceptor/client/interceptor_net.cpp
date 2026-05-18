/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <utility>
#include <limits>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <cerrno>

#include "bio_err.h"
#include "bio_ref.h"
#include "interceptor_log.h"
#include "message.h"
#include "message_op.h"
#include "securec.h"
#include "interceptor_net.h"

using namespace ock::bio;

static void Log(int level, const char *msg)
{
    if (level == 0) {
        CLOG_DEBUG(msg);
    } else if (level == 1U) {
        CLOG_INFO(msg);
    } else if (level == 2U) {
        CLOG_WARN(msg);
    } else if (level == 3U) {
        CLOG_ERROR(msg);
    }
}

static bool ParseBoolEnv(const char *envName, bool defaultValue)
{
    const char *envValue = std::getenv(envName);
    if (envValue == nullptr || strlen(envValue) == 0) {
        return defaultValue;
    }
    return strcasecmp(envValue, "1") == 0 || strcasecmp(envValue, "true") == 0 ||
           strcasecmp(envValue, "yes") == 0 || strcasecmp(envValue, "on") == 0;
}

static BResult ParseUInt16Env(const char *envName, uint16_t defaultValue, uint16_t &value)
{
    value = defaultValue;
    const char *envValue = std::getenv(envName);
    if (envValue == nullptr || strlen(envValue) == 0) {
        return BIO_OK;
    }

    long parsed = 0;
    if (!StrUtil::StrToLong(envValue, parsed) || parsed <= 0 ||
        parsed > static_cast<long>(std::numeric_limits<uint16_t>::max())) {
        return BIO_INVALID_PARAM;
    }
    value = static_cast<uint16_t>(parsed);
    return BIO_OK;
}

static std::string FormatWorkerGroupCpuIdsRange(const std::vector<std::pair<uint32_t, uint32_t>> &ranges)
{
    std::ostringstream oss;
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (i != 0) {
            oss << ",";
        }
        const auto &range = ranges[i];
        if (range.first == UINT32_MAX && range.second == UINT32_MAX) {
            oss << "-1";
        } else {
            oss << range.first << "-" << range.second;
        }
    }
    return oss.str();
}

int32_t InterceptorClientNetService::StartNetService()
{
    if (UNLIKELY(mShutdown.load())) {
        return BIO_NOT_READY;
    }

    mNetEngine = MakeRef<NetEngine>();
    if (mNetEngine == nullptr) {
        return BIO_ALLOC_FAIL;
    }

    int16_t timeoutSec = NO_60;
    uint32_t coreThreadNum = 1;
    uint32_t queueSize = NO_128;
    auto ret = mNetEngine->Initialize(timeoutSec, coreThreadNum, queueSize, Log);
    if (UNLIKELY(ret != BIO_OK)) {
        StopNetServiceLocked();
        return ret;
    }

    NetOptions netOptions;
    netOptions.role = Role::NET_CLIENT;
    netOptions.protocol = ServiceProtocol::SHM;
    netOptions.workerGroupCpuIdsRange = DefaultWorkerGroupCpuIdsRange(IPC_WORKER_GROUP_CPU_RANGE_COUNT);
    const char *connCountEnv = std::getenv("INTERCEPTOR_IPC_CONN_COUNT");
    ret = ParseUInt16Env("INTERCEPTOR_IPC_CONN_COUNT", NO_2, netOptions.connCount);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Parse INTERCEPTOR_IPC_CONN_COUNT failed, value:" <<
            (connCountEnv == nullptr ? "<null>" : connCountEnv) << ".");
        StopNetServiceLocked();
        return ret;
    }
    if (connCountEnv != nullptr && strlen(connCountEnv) > 0) {
        CLOG_INFO("Apply INTERCEPTOR_IPC_CONN_COUNT success, value:" << netOptions.connCount << ".");
    }

    const char *busyLoopEnv = std::getenv("INTERCEPTOR_IPC_BUSY_LOOP");
    netOptions.isBusyLoop = ParseBoolEnv("INTERCEPTOR_IPC_BUSY_LOOP", false);
    if (busyLoopEnv != nullptr && strlen(busyLoopEnv) > 0) {
        CLOG_INFO("Apply INTERCEPTOR_IPC_BUSY_LOOP success, value:" <<
            (netOptions.isBusyLoop ? "true" : "false") << ".");
    }

    if (!CheckWorkerGroupCpuIdsRangeMatchConnCount(netOptions.workerGroupCpuIdsRange, netOptions.connCount,
        IPC_WORKER_GROUP_CPU_RANGE_COUNT)) {
        CLOG_ERROR("Interceptor ipc cpu range not match conn count, connCount:" << netOptions.connCount << ".");
        StopNetServiceLocked();
        return BIO_INVALID_PARAM;
    }
    netOptions.handlerCount = netOptions.connCount;
    ret = mNetEngine->Start(netOptions);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Start ipc engine failed, ret:" << ret << ", connCount:" << netOptions.connCount <<
            ", handlerCount:" << netOptions.handlerCount << ", busyLoop:" <<
            (netOptions.isBusyLoop ? "true" : "false") << ", cpuIds:" <<
            FormatWorkerGroupCpuIdsRange(netOptions.workerGroupCpuIdsRange) << ".");
        StopNetServiceLocked();
        return ret;
    }

    uint32_t currentPid = static_cast<uint32_t>(getpid());
    mPid.store(currentPid, std::memory_order_relaxed);
    ConnectInfo info(INVALID_NID, currentPid, INVALID_NID);
    info.isSelfPoll = true;
    ret = mNetEngine->SyncConnect(info);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Connect interceptor ipc channel failed, ret:" << ret << ", pid:" << currentPid << ".");
        StopNetServiceLocked();
        return ret;
    }

    CLOG_INFO("Start interceptor net service success, pid:" << currentPid << ", connCount:" << netOptions.connCount <<
        ", handlerCount:" << netOptions.handlerCount << ", busyLoop:" <<
        (netOptions.isBusyLoop ? "true" : "false") << ", cpuIds:" <<
        FormatWorkerGroupCpuIdsRange(netOptions.workerGroupCpuIdsRange) << ".");
    mReady.store(true);
    return 0;
}

void InterceptorClientNetService::OrphanInheritedState(uint32_t currentPid)
{
    uint32_t oldPid = mPid.load(std::memory_order_relaxed);
    CLOG_WARN("Orphan inherited interceptor net service, oldPid:" << oldPid << ", currentPid:" << currentPid << ".");
    mReady.store(false);

    if (mNetEngine != nullptr) {
        (void)mNetEngine.Detach();
    }

    mBioShmFd = -1;
    mBioShmOffset = 0;
    mBioShmLength = 0;
    mBioShmAddr = nullptr;
    mReadIndexFd = -1;
    mReadIndexLength = 0;
    mReadIndex = nullptr;
    mPid.store(0, std::memory_order_relaxed);
}

BResult InterceptorClientNetService::EnsureStartedForCurrentProcess()
{
    if (UNLIKELY(mShutdown.load())) {
        return BIO_NOT_READY;
    }

    uint32_t currentPid = static_cast<uint32_t>(getpid());
    uint32_t servicePid = mPid.load(std::memory_order_relaxed);
    if (UNLIKELY(servicePid != 0 && servicePid != currentPid)) {
        std::lock_guard<std::mutex> lock(mStartLock);
        if (UNLIKELY(mShutdown.load())) {
            return BIO_NOT_READY;
        }
        servicePid = mPid.load(std::memory_order_relaxed);
        if (servicePid != 0 && servicePid != currentPid) {
            OrphanInheritedState(currentPid);
        }
    }
    if (LIKELY(mReady.load())) {
        return BIO_OK;
    }
    std::lock_guard<std::mutex> lock(mStartLock);
    if (UNLIKELY(mShutdown.load())) {
        return BIO_NOT_READY;
    }
    currentPid = static_cast<uint32_t>(getpid());
    servicePid = mPid.load(std::memory_order_relaxed);
    if (UNLIKELY(servicePid != 0 && servicePid != currentPid)) {
        OrphanInheritedState(currentPid);
    }
    if (LIKELY(mReady.load())) {
        return BIO_OK;
    }
    auto ret = StartNetService();
    return ret == 0 ? BIO_OK : ret;
}

BResult InterceptorClientNetService::EnsureBioShmForCurrentProcess()
{
    auto ret = EnsureStartedForCurrentProcess();
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    if (LIKELY(mBioShmAddr != nullptr)) {
        return BIO_OK;
    }
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mBioShmAddr != nullptr) {
        return BIO_OK;
    }
    return CreateBioServerMem();
}

BResult InterceptorClientNetService::PrepareBeforeFork()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    uint32_t currentPid = static_cast<uint32_t>(getpid());
    uint32_t servicePid = mPid.load(std::memory_order_relaxed);
    if (UNLIKELY(servicePid != 0 && servicePid != currentPid)) {
        OrphanInheritedState(currentPid);
        return BIO_OK;
    }
    if (mReady.load() || mNetEngine != nullptr) {
        StopNetServiceLocked();
    }
    return BIO_OK;
}

BResult InterceptorClientNetService::PrepareAfterForkChild()
{
    uint32_t currentPid = static_cast<uint32_t>(getpid());
    uint32_t servicePid = mPid.load(std::memory_order_relaxed);
    if (servicePid != 0 && servicePid != currentPid) {
        OrphanInheritedState(currentPid);
    }
    return EnsureBioShmForCurrentProcess();
}

void InterceptorClientNetService::StopNetService()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    StopNetServiceLocked();
}

void InterceptorClientNetService::ShutdownNetService()
{
    mShutdown.store(true);
}

void InterceptorClientNetService::StopNetServiceLocked()
{
    uint32_t currentPid = static_cast<uint32_t>(getpid());
    uint32_t servicePid = mPid.load(std::memory_order_relaxed);
    if (UNLIKELY(servicePid != 0 && servicePid != currentPid)) {
        OrphanInheritedState(currentPid);
        return;
    }

    mReady.store(false);

    if (mBioShmAddr != nullptr && mBioShmLength > 0) {
        munmap(mBioShmAddr, mBioShmLength);
        mBioShmAddr = nullptr;
    }
    if (mBioShmFd >= 0) {
        close(mBioShmFd);
        mBioShmFd = -1;
    }
    mBioShmOffset = 0;
    mBioShmLength = 0;
    if (mReadIndex != nullptr && mReadIndexLength > 0) {
        munmap(mReadIndex, mReadIndexLength);
        mReadIndex = nullptr;
    }
    if (mReadIndexFd >= 0) {
        close(mReadIndexFd);
        mReadIndexFd = -1;
    }
    mReadIndexLength = 0;

    if (mNetEngine != nullptr) {
        mNetEngine->Stop();
        mNetEngine = nullptr;
    }

    mPid.store(0, std::memory_order_relaxed);
}

BResult InterceptorClientNetService::CreateBioServerMem()
{
    uint32_t servicePid = mPid.load(std::memory_order_relaxed);
    ShmInitRequest req{};
    req.comm.magic = MESSAGE_MAGIC;
    req.comm.pid = servicePid;

    ShmInitResponse rsp{};
    auto ret = mNetEngine->SyncCall<ShmInitRequest, ShmInitResponse>(INVALID_NID, BIO_OP_INTERCEPTOR_BIO_SHM_INIT,
        req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Send bio shm init request failed, ret:" << ret << ".");
        return ret;
    }

    bool hasReadIndex = rsp.readIndexLength >= sizeof(InterceptorReadIndexHeader) &&
        rsp.readIndexEntryCount > 0;
    int32_t realFds[INTERCEPTOR_SHM_FD_MAX_COUNT] = { -1, -1 };
    uint32_t fdCount = hasReadIndex ? INTERCEPTOR_SHM_FD_WITH_READ_INDEX_COUNT :
        INTERCEPTOR_SHM_FD_WITHOUT_READ_INDEX_COUNT;
    ret = mNetEngine->ReceiveFds(INVALID_NID, realFds, fdCount);
    if (UNLIKELY(ret != BIO_OK || realFds[0] < 0)) {
        CLOG_ERROR("Receive bio shm fd failed, ret:" << ret << ", fdCount:" << fdCount << ".");
        return BIO_ERR;
    }

    mBioShmFd = realFds[0];
    mBioShmOffset = rsp.offset;
    mBioShmLength = rsp.length;
    if (mBioShmLength == 0) {
        close(mBioShmFd);
        mBioShmFd = -1;
        if (realFds[1] >= 0) {
            close(realFds[1]);
        }
        return BIO_ERR;
    }

    auto address = mmap(nullptr, mBioShmLength, PROT_READ | PROT_WRITE, MAP_SHARED, mBioShmFd,
        static_cast<off_t>(mBioShmOffset));
    if (UNLIKELY(address == MAP_FAILED)) {
        CLOG_ERROR("Mmap bio shm size " << mBioShmLength << " offset " << mBioShmOffset << " failed, error:" <<
            strerror(errno));
        close(mBioShmFd);
        mBioShmFd = -1;
        if (realFds[1] >= 0) {
            close(realFds[1]);
        }
        return BIO_ERR;
    }
    mBioShmAddr = static_cast<uint8_t *>(address);
    if (hasReadIndex && realFds[1] >= 0) {
        auto *indexAddr = static_cast<InterceptorReadIndexHeader *>(mmap(nullptr, rsp.readIndexLength, PROT_READ,
            MAP_SHARED, realFds[1], 0));
        if (indexAddr == MAP_FAILED) {
            CLOG_WARN("Mmap read index shm failed, error:" << strerror(errno) << ".");
            close(realFds[1]);
        } else if (indexAddr->magic != INTERCEPTOR_READ_INDEX_MAGIC ||
            indexAddr->version != INTERCEPTOR_READ_INDEX_VERSION ||
            indexAddr->entryCount != rsp.readIndexEntryCount ||
            indexAddr->entrySize != sizeof(InterceptorReadIndexEntry)) {
            CLOG_WARN("Invalid read index header, magic:" << indexAddr->magic << ", version:" << indexAddr->version <<
                ", entryCount:" << indexAddr->entryCount << ", entrySize:" << indexAddr->entrySize << ".");
            munmap(indexAddr, rsp.readIndexLength);
            close(realFds[1]);
        } else {
            mReadIndexFd = realFds[1];
            mReadIndexLength = rsp.readIndexLength;
            mReadIndex = indexAddr;
        }
    }
    CLOG_INFO("Create bio server shared memory success, shmFd:" << mBioShmFd << ", size:" << mBioShmLength <<
        ", offset:" << mBioShmOffset << ", readIndex:" << (mReadIndex == nullptr ? "off" : "on") << ".");
    return BIO_OK;
}

bool InterceptorClientNetService::FindReadIndexCache(uint64_t inode, uint64_t offset, uint64_t minLen,
    InterceptorReadIndexCache &cache)
{
    if (UNLIKELY(mReadIndex == nullptr || mBioShmAddr == nullptr || minLen == 0)) {
        return false;
    }
    if (UNLIKELY(mReadIndex->magic != INTERCEPTOR_READ_INDEX_MAGIC ||
        mReadIndex->version != INTERCEPTOR_READ_INDEX_VERSION ||
        mReadIndex->entrySize != sizeof(InterceptorReadIndexEntry) ||
        mReadIndex->entryCount < INTERCEPTOR_READ_INDEX_BUCKET_WAY)) {
        return false;
    }

    uint64_t blockOffset = InterceptorReadIndexBlockOffset(offset);
    auto *entries = reinterpret_cast<InterceptorReadIndexEntry *>(mReadIndex + 1);
    uint64_t hash = InterceptorReadIndexHash(inode, blockOffset);
    uint32_t base = InterceptorReadIndexBucketBase(hash, mReadIndex->entryCount);

    for (uint32_t way = 0; way < INTERCEPTOR_READ_INDEX_BUCKET_WAY; ++way) {
        auto &entry = entries[base + way];
        uint64_t seq = __atomic_load_n(&entry.seq, __ATOMIC_ACQUIRE);
        if (UNLIKELY(seq == 0 || (seq & 1U) != 0)) {
            continue;
        }

        uint64_t entryInode = entry.inode;
        uint64_t fileOffset = entry.fileOffset;
        uint64_t dataLen = entry.dataLen;
        uint32_t addrNum = entry.addrNum;
        uint64_t addrOffset[SLICE_ADDR_SIZE] = {};
        uint64_t addrLen[SLICE_ADDR_SIZE] = {};
        for (uint32_t idx = 0; idx < SLICE_ADDR_SIZE; ++idx) {
            addrOffset[idx] = entry.addrOffset[idx];
            addrLen[idx] = entry.addrLen[idx];
        }

        uint64_t seqAfter = __atomic_load_n(&entry.seq, __ATOMIC_ACQUIRE);
        if (UNLIKELY(seq != seqAfter || (seqAfter & 1U) != 0 || entryInode != inode || addrNum == 0 ||
            addrNum > SLICE_ADDR_SIZE || offset < fileOffset)) {
            continue;
        }

        uint64_t skip = offset - fileOffset;
        if (UNLIKELY(minLen > dataLen || skip > dataLen - minLen)) {
            continue;
        }

        cache.entry = &entry;
        cache.seq = seqAfter;
        cache.inode = entryInode;
        cache.fileOffset = fileOffset;
        cache.dataLen = dataLen;
        cache.addrNum = addrNum;
        cache.reserved = 0;
        for (uint32_t idx = 0; idx < SLICE_ADDR_SIZE; ++idx) {
            cache.addrOffset[idx] = addrOffset[idx];
            cache.addrLen[idx] = addrLen[idx];
        }
        return true;
    }
    return false;
}

bool InterceptorClientNetService::CopyFromReadIndex(uint64_t inode, uint64_t offset, size_t count, void *buf,
    InterceptorReadIndexCache *cache)
{
    if (UNLIKELY(mReadIndex == nullptr || mBioShmAddr == nullptr || buf == nullptr || count == 0)) {
        return false;
    }
    if (UNLIKELY(mReadIndex->magic != INTERCEPTOR_READ_INDEX_MAGIC ||
        mReadIndex->version != INTERCEPTOR_READ_INDEX_VERSION ||
        mReadIndex->entrySize != sizeof(InterceptorReadIndexEntry) ||
        mReadIndex->entryCount < INTERCEPTOR_READ_INDEX_BUCKET_WAY)) {
        return false;
    }

    uint64_t blockOffset = InterceptorReadIndexBlockOffset(offset);
    auto *entries = reinterpret_cast<InterceptorReadIndexEntry *>(mReadIndex + 1);
    uint64_t hash = InterceptorReadIndexHash(inode, blockOffset);
    uint32_t base = InterceptorReadIndexBucketBase(hash, mReadIndex->entryCount);

    for (uint32_t way = 0; way < INTERCEPTOR_READ_INDEX_BUCKET_WAY; ++way) {
        auto &entry = entries[base + way];
        uint64_t seq = __atomic_load_n(&entry.seq, __ATOMIC_ACQUIRE);
        if (UNLIKELY(seq == 0 || (seq & 1U) != 0)) {
            continue;
        }

        uint64_t entryInode = entry.inode;
        uint64_t fileOffset = entry.fileOffset;
        uint64_t dataLen = entry.dataLen;
        uint32_t addrNum = entry.addrNum;
        uint64_t addrOffset[SLICE_ADDR_SIZE] = {};
        uint64_t addrLen[SLICE_ADDR_SIZE] = {};
        for (uint32_t idx = 0; idx < SLICE_ADDR_SIZE; ++idx) {
            addrOffset[idx] = entry.addrOffset[idx];
            addrLen[idx] = entry.addrLen[idx];
        }

        uint64_t seqAfter = __atomic_load_n(&entry.seq, __ATOMIC_ACQUIRE);
        if (UNLIKELY(seq != seqAfter || (seqAfter & 1U) != 0)) {
            continue;
        }
        if (UNLIKELY(entryInode != inode || addrNum == 0 || addrNum > SLICE_ADDR_SIZE || offset < fileOffset)) {
            continue;
        }

        uint64_t skip = offset - fileOffset;
        uint64_t left = static_cast<uint64_t>(count);
        if (UNLIKELY(left > dataLen || skip > dataLen - left)) {
            continue;
        }

        uint8_t *srcAddr[SLICE_ADDR_SIZE] = {};
        uint64_t copyLen[SLICE_ADDR_SIZE] = {};
        uint32_t copyNum = 0;
        bool invalidAddr = false;
        for (uint32_t idx = 0; idx < addrNum && left > 0; ++idx) {
            if (skip >= addrLen[idx]) {
                skip -= addrLen[idx];
                continue;
            }

            copyLen[copyNum] = std::min<uint64_t>(addrLen[idx] - skip, left);
            srcAddr[copyNum] = GetBioShmAddressFast(addrOffset[idx] + skip, copyLen[copyNum]);
            if (UNLIKELY(srcAddr[copyNum] == nullptr)) {
                invalidAddr = true;
                break;
            }

            left -= copyLen[copyNum];
            skip = 0;
            ++copyNum;
        }
        if (UNLIKELY(invalidAddr || left != 0)) {
            continue;
        }

        size_t copied = 0;
        for (uint32_t idx = 0; idx < copyNum; ++idx) {
            int ret = memcpy_s(static_cast<uint8_t *>(buf) + copied, count - copied, srcAddr[idx],
                static_cast<size_t>(copyLen[idx]));
            if (UNLIKELY(ret != 0)) {
                invalidAddr = true;
                break;
            }
            copied += static_cast<size_t>(copyLen[idx]);
        }
        if (UNLIKELY(invalidAddr)) {
            continue;
        }
        uint64_t seqDone = __atomic_load_n(&entry.seq, __ATOMIC_ACQUIRE);
        if (UNLIKELY(seqDone != seqAfter || (seqDone & 1U) != 0)) {
            continue;
        }
        if (cache != nullptr) {
            cache->entry = &entry;
            cache->seq = seqDone;
            cache->inode = entryInode;
            cache->fileOffset = fileOffset;
            cache->dataLen = dataLen;
            cache->addrNum = addrNum;
            cache->reserved = 0;
            for (uint32_t idx = 0; idx < SLICE_ADDR_SIZE; ++idx) {
                cache->addrOffset[idx] = addrOffset[idx];
                cache->addrLen[idx] = addrLen[idx];
            }
        }
        return true;
    }
    return false;
}

uint8_t *InterceptorClientNetService::GetBioShmAddress(uint64_t offset, uint32_t len)
{
    if (UNLIKELY(EnsureStartedForCurrentProcess() != BIO_OK)) {
        return nullptr;
    }
    if (UNLIKELY(mBioShmAddr == nullptr)) {
        std::lock_guard<std::mutex> lock(mStartLock);
        if (mBioShmAddr == nullptr && CreateBioServerMem() != BIO_OK) {
            return nullptr;
        }
    }
    if (UNLIKELY(mBioShmAddr == nullptr)) {
        return nullptr;
    }

    if (UNLIKELY(offset < mBioShmOffset)) {
        CLOG_ERROR("Invalid bio shm address, offset:" << offset << ", len:" << len << ", shmOffset:" <<
            mBioShmOffset << ", shmLength:" << mBioShmLength << ".");
        return nullptr;
    }
    uint64_t relativeOffset = offset - mBioShmOffset;
    if (UNLIKELY(len > mBioShmLength || relativeOffset > mBioShmLength - len)) {
        CLOG_ERROR("Invalid bio shm address, offset:" << offset << ", relativeOffset:" << relativeOffset <<
            ", len:" << len << ", shmOffset:" << mBioShmOffset << ", shmLength:" << mBioShmLength << ".");
        return nullptr;
    }
    return mBioShmAddr + relativeOffset;
}
