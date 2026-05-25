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

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <exception>
#include <future>
#include <sys/uio.h>
#include <thread>
#include "bio_c.h"
#include "bio_def.h"
#include "bio_err.h"
#include "bio_monotonic.h"
#include "interceptor_context.h"
#include "interceptor_log.h"
#include "interceptor_net.h"
#include "message_op.h"
#include "securec.h"
#include "proxy_operations.h"

using namespace ock::bio;

#define CONTEXT BioInterceptorContext::GetInstance()

namespace {
constexpr ssize_t AGGREGATE_WRITE_FALLBACK = -2;
constexpr ssize_t DIRECT_WRITE_FALLBACK = -2;
constexpr uint64_t REMOTE_READ_LARGE_IO_PREFETCH_BYTES = 1024 * 1024;
constexpr uint64_t REMOTE_READ_PREFETCH_WAIT_NS = 50 * 1000 * 1000;
constexpr uint32_t REMOTE_READ_PREFETCH_WAIT_SPIN = 8;
constexpr uint32_t REMOTE_READ_PREFETCH_WAIT_SLEEP_US = 100;
constexpr uint32_t READ_BUFFER_RELEASE_FLUSH_WAIT_MS = 100;
constexpr uint32_t READ_BUFFER_RELEASE_RETRY_WAIT_MS = 1;
constexpr uint64_t DIRECT_WRITE_PREPARE_AHEAD_TRIGGER_BYTES = INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE / 2;

void LogReadPathSuccess(const char *path, int fd, uint64_t inode, off_t offset, size_t nbytes, ssize_t ret)
{
    CLOG_DEBUG("Interceptor read path success, path:" << path << ", fd:" << fd << ", inode:" << inode <<
        ", offset:" << offset << ", nbytes:" << nbytes << ", ret:" << ret << ".");
}

void LogWritePathSuccess(const char *path, int fd, uint64_t inode, off_t offset, size_t nbytes, ssize_t ret)
{
    CLOG_DEBUG("Interceptor write path success, path:" << path << ", fd:" << fd << ", inode:" << inode <<
        ", offset:" << offset << ", nbytes:" << nbytes << ", ret:" << ret << ".");
}

void LogReadInterceptSuccess(const char *api, int fd, uint64_t inode, off_t offset, size_t nbytes)
{
    CLOG_DEBUG("Interceptor read success, api:" << api << ", fd:" << fd << ", inode:" << inode <<
        ", offset:" << offset << ", nbytes:" << nbytes << ".");
}

void LogWriteInterceptSuccess(const char *api, int fd, uint64_t inode, off_t offset, size_t nbytes)
{
    CLOG_DEBUG("Interceptor write success, api:" << api << ", fd:" << fd << ", inode:" << inode <<
        ", offset:" << offset << ", nbytes:" << nbytes << ".");
}

bool IsDirectSpaceWriteSupported(off_t offset, size_t count)
{
    if (offset < 0 || count == 0 || count > INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE) {
        return false;
    }
    uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(count) - 1U;
    return end >= static_cast<uint64_t>(offset);
}

uint64_t WriteAggregateWindowBase(uint64_t offset)
{
    return offset / MAX_INTERCEPTOR_IO_SIZE * MAX_INTERCEPTOR_IO_SIZE;
}

bool IsWriteAggregateWindowStart(uint64_t offset)
{
    return offset == WriteAggregateWindowBase(offset);
}

bool ParseDirectWriteBoolEnv(const char *name, bool defaultValue)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return defaultValue;
    }
    return value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
        value[0] == 'y' || value[0] == 'Y' || value[0] == 'o' || value[0] == 'O';
}

bool DirectWriteAggregateEnabled()
{
    static const bool enabled = ParseDirectWriteBoolEnv("INTERCEPTOR_DIRECT_WRITE_AGGREGATE", true);
    return enabled;
}

}

static thread_local OpenFile *g_lastReadIndexFile = nullptr;
static thread_local InterceptorReadIndexCache g_lastReadIndexCache{};

static BResult ReleaseReadBuffer(uint64_t addrOffset, uint64_t length);

struct PendingReadBuffer {
    uint64_t addrOffset = 0;
    uint64_t length = 0;
};

class AsyncReadBufferReleaser {
public:
    ~AsyncReadBufferReleaser()
    {
        Stop();
    }

    void Push(uint64_t addrOffset, uint64_t length)
    {
        if (length == 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(mMutex);
        StartLocked();
        PendingReadBuffer pending;
        pending.addrOffset = addrOffset;
        pending.length = length;
        mBuffers.push_back(pending);
        mCond.notify_one();
    }

    void Flush()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (!mStarted) {
            return;
        }
        mDrained.wait_for(lock, std::chrono::milliseconds(READ_BUFFER_RELEASE_FLUSH_WAIT_MS), [this] {
            return mBuffers.empty() && !mProcessing;
        });
    }

    void Shutdown()
    {
        Flush();
        Stop();
    }

private:
    void StartLocked()
    {
        if (mStarted) {
            return;
        }
        mStarted = true;
        mWorker = std::thread([this] {
            WorkerLoop();
        });
    }

    void WorkerLoop()
    {
        while (true) {
            PendingReadBuffer pending;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCond.wait(lock, [this] {
                    return mStopping.load(std::memory_order_acquire) || !mBuffers.empty();
                });
                if (mStopping.load(std::memory_order_acquire)) {
                    mBuffers.clear();
                    mDrained.notify_all();
                    return;
                }
                pending = mBuffers.front();
                mBuffers.pop_front();
                mProcessing = true;
            }

            BResult ret = ReleaseReadBuffer(pending.addrOffset, pending.length);
            if ((ret == BIO_NET_RETRY || ret == BIO_NOT_READY) && !mStopping.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(READ_BUFFER_RELEASE_RETRY_WAIT_MS));
                std::lock_guard<std::mutex> lock(mMutex);
                mBuffers.push_front(pending);
            }

            {
                std::lock_guard<std::mutex> lock(mMutex);
                mProcessing = false;
                if (mBuffers.empty()) {
                    mDrained.notify_all();
                }
            }
        }
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mStopping.store(true, std::memory_order_release);
            mBuffers.clear();
            mCond.notify_all();
        }
        if (mWorker.joinable()) {
            mWorker.join();
        }
    }

    std::mutex mMutex;
    std::condition_variable mCond;
    std::condition_variable mDrained;
    std::deque<PendingReadBuffer> mBuffers;
    std::thread mWorker;
    bool mStarted = false;
    std::atomic<bool> mStopping{false};
    bool mProcessing = false;
};

static AsyncReadBufferReleaser g_readBufferReleaser;

void ProxyOperations::FlushPendingReadBufferReleaseForCurrentThread()
{
    g_readBufferReleaser.Shutdown();
}

static bool IsReadRespValid(const InterceptorPreadOut &resp, uint64_t reqLen)
{
    if (resp.ret != 0 || resp.dataLen > reqLen) {
        return false;
    }
    if (resp.dataLen == 0) {
        return true;
    }
    if (resp.dataSource != INTERCEPTOR_PREAD_DATA_BIO_SHM &&
        resp.dataSource != INTERCEPTOR_PREAD_DATA_READ_SHM) {
        return false;
    }
    if (resp.addrNum == 0 || resp.addrNum > SLICE_ADDR_SIZE) {
        return false;
    }

    uint64_t addrLen = 0;
    for (uint32_t idx = 0; idx < resp.addrNum; idx++) {
        addrLen += resp.addrLen[idx];
    }
    return addrLen >= resp.dataLen;
}

static void QueueReadBufferReleaseAsync(uint64_t addrOffset, uint64_t length)
{
    g_readBufferReleaser.Push(addrOffset, length);
}

void ProxyOperations::QueueReadBufferRelease(uint64_t addrOffset, uint64_t length)
{
    QueueReadBufferReleaseAsync(addrOffset, length);
}

static BResult ReleaseReadBuffer(uint64_t addrOffset, uint64_t length)
{
    if (length == 0) {
        return BIO_OK;
    }
    InterceptorReadBufferReleaseIn req{};
    auto pidRet = InterceptorClientNetService::Instance().GetReadySendPid(req.pid);
    if (UNLIKELY(pidRet != BIO_OK || req.pid == 0)) {
        CLOG_DEBUG("Release read shm buffer skipped because net service is not ready, pidRet:" << pidRet <<
            ", offset:" << addrOffset << ", length:" << length << ".");
        return pidRet != BIO_OK ? pidRet : BIO_NOT_READY;
    }
    req.addrOffset = addrOffset;
    req.length = length;
    uint32_t *rsp = nullptr;
    uint64_t rspLen = 0;
    auto ret = InterceptorClientNetService::Instance().SendSync<InterceptorReadBufferReleaseIn, uint32_t>(
        INVALID_NID, BIO_OP_INTERCEPTOR_READ_BUFFER_RELEASE, req, &rsp, rspLen);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_DEBUG("Release read shm buffer failed, pid:" << req.pid << ", offset:" << req.addrOffset <<
            ", length:" << req.length << ", ret:" << ret << ".");
        return ret;
    }
    return BIO_OK;
}

static void ReleaseRemoteReadWindow(const RemoteReadWindowCache &cache)
{
    if (!cache.active || cache.addrLen == 0) {
        return;
    }
    QueueReadBufferReleaseAsync(cache.addrOffset, cache.addrLen);
}

static bool InvalidateReadIndexOnNode(BioNodeId nodeId, uint64_t inode, uint64_t offset, uint64_t length,
    bool broadcastRemote)
{
    InterceptorReadIndexInvalidateIn req{};
    req.inode = inode;
    req.offset = offset;
    req.length = length;
    req.broadcastRemote = broadcastRemote ? 1U : 0U;
    uint32_t *rsp = nullptr;
    uint64_t rspLen = 0;
    BResult ret = InterceptorClientNetService::Instance().SendSync<InterceptorReadIndexInvalidateIn, uint32_t>(
        nodeId, BIO_OP_INTERCEPTOR_READ_INDEX_INVALIDATE, req, &rsp, rspLen);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Invalidate read-index failed, dstNid:" << nodeId << ", inode:" << inode <<
            ", offset:" << offset << ", length:" << length << ", ret:" << ret << ".");
        return false;
    }
    return true;
}

static bool InvalidateReadIndexForNativeWrite(uint64_t inode, off_t offset, uint64_t length)
{
    if (offset < 0 || length == 0) {
        return true;
    }

    uint64_t writeOffset = static_cast<uint64_t>(offset);
    return InvalidateReadIndexOnNode(INVALID_NID, inode, writeOffset, length, true);
}

static void NoteWriteFileSize(const std::shared_ptr<OpenFile> &file, off_t offset, ssize_t length)
{
    if (file == nullptr || offset < 0 || length <= 0) {
        return;
    }
    uint64_t writeOffset = static_cast<uint64_t>(offset);
    uint64_t writeLength = static_cast<uint64_t>(length);
    if (writeOffset > UINT64_MAX - writeLength) {
        return;
    }
    file->ExpandKnownFileSize(writeOffset + writeLength);
}

static void DropReadCaches(const std::shared_ptr<OpenFile> &file)
{
    std::array<RemoteReadWindowCache, REMOTE_READ_WINDOW_CACHE_SLOTS> evicted{};
    file->DropReadAddrCache();
    file->InvalidateRemoteReadWindowCache(&evicted);
    for (const auto &cache : evicted) {
        ReleaseRemoteReadWindow(cache);
    }
}

static void ReleaseRemoteReadWindowsBefore(const std::shared_ptr<OpenFile> &file, uint64_t offset)
{
    std::array<RemoteReadWindowCache, REMOTE_READ_WINDOW_CACHE_SLOTS> evicted{};
    file->EvictRemoteReadWindowsBefore(InterceptorReadIndexBlockOffset(offset), &evicted);
    for (const auto &cache : evicted) {
        ReleaseRemoteReadWindow(cache);
    }
}

static bool IsRemoteReadWindowResp(const InterceptorPreadOut &resp)
{
    return resp.dataSource == INTERCEPTOR_PREAD_DATA_READ_SHM && resp.windowDataLen != 0 &&
        resp.windowAddrLen >= resp.windowDataLen;
}

static void ReleaseReadBufferIfNeeded(const InterceptorPreadOut &resp)
{
    if (resp.dataSource != INTERCEPTOR_PREAD_DATA_READ_SHM || resp.addrNum == 0) {
        return;
    }

    if (IsRemoteReadWindowResp(resp)) {
        QueueReadBufferReleaseAsync(resp.windowAddrOffset, resp.windowAddrLen);
    } else {
        QueueReadBufferReleaseAsync(resp.addrOffset[0], resp.addrLen[0]);
    }
}

static int SendReadRequest(int fd, uint64_t inode, uint32_t pid, uint64_t nbytes, int64_t offset,
    uint64_t startTime, InterceptorPreadOut &resp, bool logError = true, uint32_t flags = 0)
{
    InterceptorPreadIn request{};
    request.pid = pid;
    request.fd = fd;
    request.inode = inode;
    request.offset = offset;
    request.nbytes = nbytes;
    request.startTime = startTime;
    request.flags = flags;

    auto ret = InterceptorClientNetService::Instance().SendSync<InterceptorPreadIn, InterceptorPreadOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_READ, request, resp);
    uint64_t elapsedNs = Monotonic::TimeNs() - startTime;
    bool success = (ret == 0 && IsReadRespValid(resp, nbytes));
    if (UNLIKELY(!success)) {
        if (!logError) {
            return -1;
        }
        CLOG_ERROR("Interceptor read failed, sendRet:" << ret << ", respRet:" << resp.ret << ", fd:" << fd <<
            ", inode:" << inode << ", offset:" << offset << ", nbytes:" << nbytes << ", dataLen:" << resp.dataLen <<
            ", addrNum:" << resp.addrNum << ", flags:" << flags << ", elapsed_ns:" << elapsedNs << ".");
        return -1;
    }
    return 0;
}

static ssize_t CopyReadResp(int fd, const InterceptorPreadOut &resp, void *buf, size_t count, bool releaseAfterCopy)
{
    size_t copied = 0;
    uint64_t left = resp.dataLen;
    for (uint32_t idx = 0; idx < resp.addrNum; idx++) {
        uint64_t copyLen = std::min<uint64_t>(resp.addrLen[idx], left);
        if (copyLen == 0) {
            continue;
        }
        if (UNLIKELY(copyLen > count - copied)) {
            if (releaseAfterCopy) {
                ReleaseReadBufferIfNeeded(resp);
            }
            return -1;
        }
        uint8_t *src = InterceptorClientNetService::Instance().GetBioShmAddress(resp.addrOffset[idx], copyLen);
        if (UNLIKELY(src == nullptr)) {
            CLOG_ERROR("Get bio shm read address failed, fd:" << fd << ", idx:" << idx << ", offset:" <<
                resp.addrOffset[idx] << ", len:" << copyLen << ".");
            if (releaseAfterCopy) {
                ReleaseReadBufferIfNeeded(resp);
            }
            return -1;
        }
        int ret = memcpy_s(static_cast<uint8_t *>(buf) + copied, count - copied, src, static_cast<size_t>(copyLen));
        if (UNLIKELY(ret != 0)) {
            if (releaseAfterCopy) {
                ReleaseReadBufferIfNeeded(resp);
            }
            return -1;
        }
        copied += copyLen;
        left -= copyLen;
        if (left == 0) {
            break;
        }
    }
    if (releaseAfterCopy) {
        ReleaseReadBufferIfNeeded(resp);
    }
    return (left == 0) ? static_cast<ssize_t>(resp.dataLen) : -1;
}

static bool CopyFromRemoteReadWindowCache(const std::shared_ptr<OpenFile> &file, uint64_t offset, size_t count,
    void *buf)
{
    RemoteReadWindowCache evicted{};
    bool needRelease = false;
    bool hit = false;
    {
        std::lock_guard<std::mutex> lock(file->RemoteReadWindowMutex());
        auto &caches = file->RemoteReadWindows();
        for (size_t idx = 0; idx < file->RemoteReadWindowSlotCount(); ++idx) {
            auto &cache = caches[idx];
            if (!cache.Contains(offset, count)) {
                continue;
            }
            uint64_t skip = offset - cache.fileOffset;
            uint8_t *src = InterceptorClientNetService::Instance().GetBioShmAddress(cache.addrOffset + skip,
                static_cast<uint32_t>(count));
            if (UNLIKELY(src == nullptr)) {
                evicted = cache;
                cache.Reset();
                needRelease = true;
            } else {
                int ret = memcpy_s(buf, count, src, count);
                hit = (ret == 0);
            }
            break;
        }
    }
    if (needRelease) {
        ReleaseRemoteReadWindow(evicted);
    }
    return hit;
}

static bool StoreRemoteReadWindowCache(const std::shared_ptr<OpenFile> &file, const InterceptorPreadOut &resp,
    const uint64_t *generation = nullptr, const uint64_t *protectedOffset = nullptr)
{
    if (!IsRemoteReadWindowResp(resp)) {
        return false;
    }
    RemoteReadWindowCache evicted{};
    {
        std::lock_guard<std::mutex> lock(file->RemoteReadWindowMutex());
        if (generation != nullptr && file->RemoteReadWindowGeneration() != *generation) {
            return false;
        }
        auto &caches = file->RemoteReadWindows();
        RemoteReadWindowCache *target = nullptr;
        size_t slotCount = file->RemoteReadWindowSlotCount();
        for (size_t idx = 0; idx < slotCount; ++idx) {
            auto &cache = caches[idx];
            if (cache.active && cache.fileOffset == resp.windowOffset) {
                target = &cache;
                break;
            }
        }
        if (target == nullptr) {
            for (size_t idx = 0; idx < slotCount; ++idx) {
                auto &cache = caches[idx];
                if (!cache.active) {
                    target = &cache;
                    break;
                }
            }
        }
        if (target == nullptr) {
            for (size_t idx = 0; idx < slotCount; ++idx) {
                auto &cache = caches[idx];
                if (protectedOffset == nullptr || !cache.Contains(*protectedOffset, 1U)) {
                    target = &cache;
                    break;
                }
            }
        }
        if (target == nullptr) {
            return false;
        }
        if (target->active && target->addrOffset != resp.windowAddrOffset) {
            evicted = *target;
        }
        target->active = true;
        target->fileOffset = resp.windowOffset;
        target->dataLen = resp.windowDataLen;
        target->addrOffset = resp.windowAddrOffset;
        target->addrLen = resp.windowAddrLen;
    }
    if (resp.windowDataLen < INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE) {
        file->MarkRemoteReadEof(resp.windowOffset + resp.windowDataLen);
    }
    ReleaseRemoteReadWindow(evicted);
    return true;
}

static bool CopyFromReadIndexCache(const std::shared_ptr<OpenFile> &file, uint64_t offset, size_t count, void *buf,
    InterceptorReadIndexCache *hitCache = nullptr)
{
    InterceptorReadIndexCache cache{};
    OpenFile *filePtr = file.get();
    if (g_lastReadIndexFile == filePtr && g_lastReadIndexCache.entry != nullptr &&
        g_lastReadIndexCache.dataLen != 0 && offset >= g_lastReadIndexCache.fileOffset &&
        count <= g_lastReadIndexCache.dataLen &&
        offset - g_lastReadIndexCache.fileOffset <= g_lastReadIndexCache.dataLen - count) {
        cache = g_lastReadIndexCache;
    } else if (file->LoadLocalReadIndexCache(offset, count, cache)) {
        g_lastReadIndexFile = filePtr;
        g_lastReadIndexCache = cache;
    } else {
        return false;
    }

    uint64_t seq = __atomic_load_n(&cache.entry->seq, __ATOMIC_ACQUIRE);
    if (UNLIKELY(seq == 0 || (seq & 1U) != 0 || seq != cache.seq || cache.inode != file->GetInode() ||
        cache.addrNum == 0 || cache.addrNum > SLICE_ADDR_SIZE || offset < cache.fileOffset)) {
        if (g_lastReadIndexFile == filePtr) {
            g_lastReadIndexFile = nullptr;
            g_lastReadIndexCache = {};
        }
        file->InvalidateLocalReadIndexCache();
        return false;
    }

    uint64_t skip = offset - cache.fileOffset;
    uint64_t left = static_cast<uint64_t>(count);
    if (UNLIKELY(left > cache.dataLen || skip > cache.dataLen - left)) {
        return false;
    }

    if (cache.addrNum == 1U) {
        if (UNLIKELY(skip > cache.addrLen[0] || left > cache.addrLen[0] - skip)) {
            return false;
        }
        uint8_t *src = InterceptorClientNetService::Instance().GetBioShmAddressFast(
            cache.addrOffset[0] + skip, left);
        if (UNLIKELY(src == nullptr)) {
            if (g_lastReadIndexFile == filePtr) {
                g_lastReadIndexFile = nullptr;
                g_lastReadIndexCache = {};
            }
            file->InvalidateLocalReadIndexCache();
            return false;
        }
        int ret = memcpy_s(buf, count, src, static_cast<size_t>(left));
        if (UNLIKELY(ret != 0)) {
            return false;
        }
        uint64_t seqDone = __atomic_load_n(&cache.entry->seq, __ATOMIC_ACQUIRE);
        if (UNLIKELY(seqDone != seq || (seqDone & 1U) != 0)) {
            if (g_lastReadIndexFile == filePtr) {
                g_lastReadIndexFile = nullptr;
                g_lastReadIndexCache = {};
            }
            file->InvalidateLocalReadIndexCache();
            return false;
        }
        if (hitCache != nullptr) {
            cache.seq = seqDone;
            *hitCache = cache;
        }
        return true;
    }

    uint8_t *srcAddr[SLICE_ADDR_SIZE] = {};
    uint64_t copyLen[SLICE_ADDR_SIZE] = {};
    uint32_t copyNum = 0;
    for (uint32_t idx = 0; idx < cache.addrNum && left > 0; ++idx) {
        if (skip >= cache.addrLen[idx]) {
            skip -= cache.addrLen[idx];
            continue;
        }

        copyLen[copyNum] = std::min<uint64_t>(cache.addrLen[idx] - skip, left);
        srcAddr[copyNum] = InterceptorClientNetService::Instance().GetBioShmAddressFast(
            cache.addrOffset[idx] + skip, copyLen[copyNum]);
        if (UNLIKELY(srcAddr[copyNum] == nullptr)) {
            if (g_lastReadIndexFile == filePtr) {
                g_lastReadIndexFile = nullptr;
                g_lastReadIndexCache = {};
            }
            file->InvalidateLocalReadIndexCache();
            return false;
        }

        left -= copyLen[copyNum];
        skip = 0;
        ++copyNum;
    }
    if (UNLIKELY(left != 0)) {
        return false;
    }

    size_t copied = 0;
    for (uint32_t idx = 0; idx < copyNum; ++idx) {
        int ret = memcpy_s(static_cast<uint8_t *>(buf) + copied, count - copied, srcAddr[idx],
            static_cast<size_t>(copyLen[idx]));
        if (UNLIKELY(ret != 0)) {
            return false;
        }
        copied += static_cast<size_t>(copyLen[idx]);
    }
    uint64_t seqDone = __atomic_load_n(&cache.entry->seq, __ATOMIC_ACQUIRE);
    if (UNLIKELY(seqDone != seq || (seqDone & 1U) != 0)) {
        if (g_lastReadIndexFile == filePtr) {
            g_lastReadIndexFile = nullptr;
            g_lastReadIndexCache = {};
        }
        file->InvalidateLocalReadIndexCache();
        return false;
    }
    if (hitCache != nullptr) {
        cache.seq = seqDone;
        *hitCache = cache;
    }
    return true;
}

static uint64_t BytesUntilReadIndexWindowEnd(uint64_t offset, uint64_t left)
{
    uint64_t windowOffset = InterceptorReadIndexBlockOffset(offset);
    uint64_t inWindow = offset - windowOffset;
    uint64_t windowLeft = MAX_INTERCEPTOR_IO_SIZE - inWindow;
    return std::min<uint64_t>(left, windowLeft);
}

static bool IsSingleReadIndexWindow(const InterceptorReadIndexCache &cache)
{
    if (cache.dataLen == 0) {
        return false;
    }
    uint64_t startBlock = InterceptorReadIndexBlockOffset(cache.fileOffset);
    uint64_t lastOffset = cache.fileOffset + cache.dataLen - 1U;
    return lastOffset >= cache.fileOffset && InterceptorReadIndexBlockOffset(lastOffset) == startBlock;
}

static bool CopyFromReadIndexCacheWindowed(const std::shared_ptr<OpenFile> &file, uint64_t offset, size_t count,
    void *buf)
{
    uint64_t left = static_cast<uint64_t>(count);
    uint64_t curOffset = offset;
    size_t copied = 0;
    while (left > 0) {
        uint64_t piece = BytesUntilReadIndexWindowEnd(curOffset, left);
        if (!CopyFromReadIndexCache(file, curOffset, static_cast<size_t>(piece),
            static_cast<uint8_t *>(buf) + copied)) {
            return false;
        }
        curOffset += piece;
        copied += static_cast<size_t>(piece);
        left -= piece;
    }
    return true;
}

static bool CopyFromGlobalReadIndexWindowed(const std::shared_ptr<OpenFile> &file, uint64_t offset, size_t count,
    void *buf)
{
    uint64_t left = static_cast<uint64_t>(count);
    uint64_t curOffset = offset;
    size_t copied = 0;
    InterceptorReadIndexCache cache{};
    while (left > 0) {
        uint64_t piece = BytesUntilReadIndexWindowEnd(curOffset, left);
        bool hit = InterceptorClientNetService::Instance().CopyFromReadIndex(file->GetInode(), curOffset,
            static_cast<size_t>(piece), static_cast<uint8_t *>(buf) + copied, &cache);
        if (!hit) {
            return false;
        }
        if (IsSingleReadIndexWindow(cache)) {
            file->StoreLocalReadIndexCache(cache);
        }
        curOffset += piece;
        copied += static_cast<size_t>(piece);
        left -= piece;
    }
    return true;
}

static int LoadReadResp(const std::shared_ptr<OpenFile> &file, int fd, size_t count, off_t offset,
    InterceptorPreadOut &resp, bool prefetch = false)
{
    uint32_t pid = 0;
    auto pidRet = InterceptorClientNetService::Instance().GetReadySendPid(pid);
    if (UNLIKELY(pidRet != BIO_OK || pid == 0)) {
        CLOG_ERROR("Load interceptor read failed because net service is not ready, fd:" << fd << ", inode:" <<
            file->GetInode() << ", offset:" << offset << ", nbytes:" << count << ", ret:" << pidRet << ".");
        return -1;
    }
    uint64_t inode = file->GetInode();

    uint64_t reqLen = count;
    int64_t reqOffset = static_cast<int64_t>(offset);
    uint64_t startTime = Monotonic::TimeNs();

    CLOG_DEBUG("Load interceptor read, fd:" << fd << ", inode:" << inode <<
        ", offset:" << reqOffset << ", nbytes:" << reqLen << ".");
    uint32_t flags = INTERCEPTOR_PREAD_FLAG_BIO_FALLBACK;
    if (prefetch) {
        flags |= INTERCEPTOR_PREAD_FLAG_PREFETCH;
    }
    int ret = SendReadRequest(fd, inode, pid, reqLen, reqOffset, startTime, resp, false, flags);
    if (UNLIKELY(ret != 0)) {
        return ret;
    }

    return 0;
}

static bool WaitRemoteReadPrefetch(const std::shared_ptr<OpenFile> &file, uint64_t offset, size_t count, void *buf)
{
    if (!file->IsRemoteReadPrefetchInFlight(offset, count)) {
        return false;
    }
    uint64_t deadline = Monotonic::TimeNs() + REMOTE_READ_PREFETCH_WAIT_NS;
    uint32_t waitLoops = 0;
    do {
        if (CopyFromRemoteReadWindowCache(file, offset, count, buf)) {
            return true;
        }
        if (waitLoops++ < REMOTE_READ_PREFETCH_WAIT_SPIN) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(REMOTE_READ_PREFETCH_WAIT_SLEEP_US));
        }
    } while (file->IsRemoteReadPrefetchInFlight(offset, count) && Monotonic::TimeNs() < deadline);
    return CopyFromRemoteReadWindowCache(file, offset, count, buf);
}

static void StartRemoteReadWindowPrefetch(const std::shared_ptr<OpenFile> &file, int fd, uint64_t prefetchOffset,
    uint64_t protectedOffset)
{
    uint64_t generation = 0;
    if (!file->TryStartRemoteReadPrefetch(prefetchOffset, generation, GetRemoteReadPrefetchConfig().depth)) {
        return;
    }

    std::thread([file, fd, prefetchOffset, protectedOffset, generation]() {
        InterceptorPreadOut resp{};
        int ret = -1;
        if (file->IsActive()) {
            ret = LoadReadResp(file, fd, 1U, static_cast<off_t>(prefetchOffset), resp, true);
        }
        if (ret == 0 && resp.dataLen == 0) {
            file->MarkRemoteReadEof(prefetchOffset);
        }
        bool stored = ret == 0 && IsRemoteReadWindowResp(resp) && file->IsActive() &&
            StoreRemoteReadWindowCache(file, resp, &generation, &protectedOffset);
        if (!stored) {
            ReleaseReadBufferIfNeeded(resp);
        }
        file->FinishRemoteReadPrefetch(prefetchOffset, generation);
    }).detach();
}

static void PrefetchRemoteReadWindowsFromBase(const std::shared_ptr<OpenFile> &file, int fd, uint64_t windowOffset)
{
    const auto &config = GetRemoteReadPrefetchConfig();
    size_t depth = config.depth;
    uint64_t nextOffset = windowOffset;
    for (size_t idx = 0; idx < depth; ++idx) {
        if (nextOffset > UINT64_MAX - INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE) {
            return;
        }
        nextOffset += INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;
        StartRemoteReadWindowPrefetch(file, fd, nextOffset, windowOffset);
    }
}

static void MaybePrefetchNextRemoteReadWindow(const std::shared_ptr<OpenFile> &file, int fd, uint64_t offset,
    size_t count)
{
    if (count == 0) {
        return;
    }

    bool sequential = file->RecordRemoteRead(offset, count);
    if (!sequential) {
        return;
    }

    uint64_t windowOffset = InterceptorReadIndexBlockOffset(offset);
    PrefetchRemoteReadWindowsFromBase(file, fd, windowOffset);
}

static void MaybePrefetchRemoteReadAtStart(const std::shared_ptr<OpenFile> &file, int fd, uint64_t offset,
    size_t count)
{
    const auto &config = GetRemoteReadPrefetchConfig();
    if (count == 0 || config.depth == 0) {
        return;
    }

    bool sequential = file->IsRemoteReadSequential(offset);
    bool largeIo = count >= REMOTE_READ_LARGE_IO_PREFETCH_BYTES;
    if (!sequential || (!config.fromStart && !largeIo)) {
        return;
    }

    uint64_t windowOffset = InterceptorReadIndexBlockOffset(offset);
    PrefetchRemoteReadWindowsFromBase(file, fd, windowOffset);
}

ssize_t ProxyOperations::PreadInner(const char *api, const std::shared_ptr<OpenFile> &file, int fd, void *buf,
    size_t count, off_t offset)
{
    if (count == 0) {
        LogReadInterceptSuccess(api, fd, file->GetInode(), offset, count);
        return 0;
    }

    if (UNLIKELY(!FlushPendingWriteWindow(file, fd))) {
        errno = EIO;
        return -1;
    }

    if (count > MAX_INTERCEPTOR_IO_SIZE) {
        CLOG_DEBUG("Fallback " << api << " to native for oversize request, fd:" << fd << ", inode:" <<
            file->GetInode() << ", offset:" << offset << ", nbytes:" << count << ".");
        return CONTEXT.GetOperations()->pread(fd, buf, count, offset);
    }

    ssize_t ret = PreadShmInner(file, fd, buf, count, offset);
    if (ret >= 0) {
        LogReadInterceptSuccess(api, fd, file->GetInode(), offset, count);
    }
    return ret;
}

ssize_t ProxyOperations::PreadShmInner(const std::shared_ptr<OpenFile> &file, int fd, void *buf, size_t count,
    off_t offset)
{
    if (offset >= 0) {
        if (CopyFromReadIndexCacheWindowed(file, static_cast<uint64_t>(offset), count, buf)) {
            LogReadPathSuccess("local_read_index", fd, file->GetInode(), offset, count, static_cast<ssize_t>(count));
            return static_cast<ssize_t>(count);
        }

        bool hit = CopyFromGlobalReadIndexWindowed(file, static_cast<uint64_t>(offset), count, buf);
        if (hit) {
            if (g_lastReadIndexFile == file.get()) {
                g_lastReadIndexFile = nullptr;
                g_lastReadIndexCache = {};
            }
            LogReadPathSuccess("global_read_index", fd, file->GetInode(), offset, count, static_cast<ssize_t>(count));
            return static_cast<ssize_t>(count);
        }

        if (CopyFromRemoteReadWindowCache(file, static_cast<uint64_t>(offset), count, buf)) {
            ReleaseRemoteReadWindowsBefore(file, static_cast<uint64_t>(offset));
            MaybePrefetchNextRemoteReadWindow(file, fd, static_cast<uint64_t>(offset), count);
            LogReadPathSuccess("remote_read_window", fd, file->GetInode(), offset, count,
                static_cast<ssize_t>(count));
            return static_cast<ssize_t>(count);
        }

        if (WaitRemoteReadPrefetch(file, static_cast<uint64_t>(offset), count, buf)) {
            ReleaseRemoteReadWindowsBefore(file, static_cast<uint64_t>(offset));
            MaybePrefetchNextRemoteReadWindow(file, fd, static_cast<uint64_t>(offset), count);
            LogReadPathSuccess("remote_read_prefetch", fd, file->GetInode(), offset, count,
                static_cast<ssize_t>(count));
            return static_cast<ssize_t>(count);
        }

        MaybePrefetchRemoteReadAtStart(file, fd, static_cast<uint64_t>(offset), count);

        InterceptorPreadOut resp{};
        int loadRet = LoadReadResp(file, fd, count, offset, resp);
        if (UNLIKELY(loadRet != 0)) {
            return -1;
        }

        if (resp.dataLen == 0) {
            file->MarkRemoteReadEof(static_cast<uint64_t>(offset));
            return 0;
        }

        bool cacheRemoteWindow = IsRemoteReadWindowResp(resp);
        ssize_t retLen = CopyReadResp(fd, resp, buf, count, !cacheRemoteWindow);
        if (UNLIKELY(retLen < 0)) {
            if (cacheRemoteWindow) {
                ReleaseReadBufferIfNeeded(resp);
            }
            return -1;
        }
        if (cacheRemoteWindow) {
            StoreRemoteReadWindowCache(file, resp);
            ReleaseRemoteReadWindowsBefore(file, static_cast<uint64_t>(offset));
            MaybePrefetchNextRemoteReadWindow(file, fd, static_cast<uint64_t>(offset), count);
        }

        LogReadPathSuccess("hook", fd, file->GetInode(), offset, count, retLen);
        return retLen;
    }

    InterceptorPreadOut resp{};
    int loadRet = LoadReadResp(file, fd, count, offset, resp);
    if (UNLIKELY(loadRet != 0)) {
        return -1;
    }

    if (resp.dataLen == 0) {
        return 0;
    }

    bool cacheRemoteWindow = IsRemoteReadWindowResp(resp);
    ssize_t retLen = CopyReadResp(fd, resp, buf, count, !cacheRemoteWindow);
    if (UNLIKELY(retLen < 0)) {
        if (cacheRemoteWindow) {
            ReleaseReadBufferIfNeeded(resp);
        }
        return -1;
    }
    if (cacheRemoteWindow) {
        StoreRemoteReadWindowCache(file, resp);
        if (offset >= 0) {
            ReleaseRemoteReadWindowsBefore(file, static_cast<uint64_t>(offset));
            MaybePrefetchNextRemoteReadWindow(file, fd, static_cast<uint64_t>(offset), count);
        }
    }

    LogReadPathSuccess("hook", fd, file->GetInode(), offset, count, retLen);
    return retLen;
}

ssize_t ProxyOperations::Pread(int fd, void *buf, size_t count, off_t offset)
{
    CLOG_DEBUG("Pread fd:" << fd << ", offset:" << offset << ", length:" << count << ".");
    const auto &file = CONTEXT.files.AtCached(fd);
    if (file == nullptr) {
        CLOG_DEBUG("Fallback pread to native, fd:" << fd << ", offset:" << offset << ", nbytes:" << count << ".");
        return CONTEXT.GetOperations()->pread(fd, buf, count, offset);
    }

    return PreadInner("pread", file, fd, buf, count, offset);
}

ssize_t ProxyOperations::Pread64(int fd, void *buf, size_t count, off64_t offset)
{
    CLOG_DEBUG("Pread64 fd:" << fd << ", offset:" << offset << ", length:" << count << ".");
    const auto &file = CONTEXT.files.AtCached(fd);
    if (file == nullptr) {
        CLOG_DEBUG("Fallback pread64 to native, fd:" << fd << ", offset:" << offset << ", nbytes:" << count << ".");
        return CONTEXT.GetOperations()->pread64(fd, buf, count, offset);
    }

    return PreadInner("pread64", file, fd, buf, count, offset);
}

ssize_t ProxyOperations::Read(int fd, void *buf, size_t nbytes)
{
    CLOG_DEBUG("Read fd:" << fd << ", length:" << nbytes << ".");
    const auto &file = CONTEXT.files.AtCached(fd);
    if (file == nullptr) {
        CLOG_DEBUG("Fallback read to native, fd:" << fd << ", nbytes:" << nbytes << ".");
        return CONTEXT.GetOperations()->read(fd, buf, nbytes);
    }

    off_t offset = file->ReserveOffset(nbytes);

    auto ret = PreadInner("read", file, fd, buf, nbytes, offset);
    if (UNLIKELY(ret < 0)) {
        file->CompleteReservedOffset(nbytes, 0);
        errno = EIO;
        return -1;
    }

    file->CompleteReservedOffset(nbytes, ret);
    return ret;
}

ssize_t ProxyOperations::PwriteInner(const char *api, const std::shared_ptr<OpenFile> &file, int fd,
    const void *buf, size_t count, off_t offset)
{
    if (count == 0) {
        LogWriteInterceptSuccess(api, fd, file->GetInode(), offset, count);
        return 0;
    }
    if (count > MAX_INTERCEPTOR_IO_SIZE && UNLIKELY(!FlushPendingWriteWindow(file, fd))) {
        errno = EIO;
        return -1;
    }
    DropReadCaches(file);
    if (count <= MAX_INTERCEPTOR_IO_SIZE) {
        bool intercepted = false;
        ssize_t ret = PwriteShmInner(file, fd, buf, count, offset, &intercepted);
        NoteWriteFileSize(file, offset, ret);
        if (ret >= 0 && intercepted) {
            LogWriteInterceptSuccess(api, fd, file->GetInode(), offset, count);
        }
        return ret;
    }
    CLOG_DEBUG("Fallback " << api << " to native for oversize request, fd:" << fd << ", inode:" <<
        file->GetInode() << ", offset:" << offset << ", nbytes:" << count << ".");
    if (UNLIKELY(!InvalidateReadIndexForNativeWrite(file->GetInode(), offset, count))) {
        errno = EIO;
        return -1;
    }
    ssize_t ret = CONTEXT.GetOperations()->pwrite64(fd, buf, count, offset);
    if (ret > 0 && UNLIKELY(!InvalidateReadIndexForNativeWrite(file->GetInode(), offset,
        static_cast<uint64_t>(ret)))) {
        errno = EIO;
        return -1;
    }
    NoteWriteFileSize(file, offset, ret);
    return ret;
}

static bool PrepareDirectSpaceWrite(const std::shared_ptr<OpenFile> &file, int fd, uint64_t nbytes, off_t offset,
    const char *api, InterceptorPwritePrepareSpaceIn &prepareReq, InterceptorPwritePrepareSpaceOut &prepareResp)
{
    auto shmRet = InterceptorClientNetService::Instance().EnsureBioShmForCurrentProcess();
    if (UNLIKELY(shmRet != BIO_OK)) {
        CLOG_DEBUG("Direct write without bio shm, api:" << api << ", ret:" << shmRet << ", fd:" << fd <<
            ", inode:" << file->GetInode() << ", offset:" << offset << ", nbytes:" << nbytes << ".");
        return false;
    }

    prepareReq = {};
    prepareReq.pid = InterceptorClientNetService::Instance().GetSendPid();
    prepareReq.fd = fd;
    prepareReq.inode = file->GetInode();
    prepareReq.offset = offset;
    prepareReq.nbytes = nbytes;
    prepareReq.startTime = Monotonic::TimeNs();

    auto &service = InterceptorClientNetService::Instance();
    int32_t ret = service.SendSync<InterceptorPwritePrepareSpaceIn, InterceptorPwritePrepareSpaceOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_WRITE_PREPARE_SPACE, prepareReq, prepareResp);
    if (UNLIKELY(ret != BIO_OK || prepareResp.ret != 0 || prepareResp.segNum == 0 ||
        prepareResp.segNum > INTERCEPTOR_DIRECT_SPACE_MAX_SEGMENTS)) {
        CLOG_DEBUG("Prepare direct write space failed, api:" << api << ", sendRet:" << ret << ", respRet:" <<
            prepareResp.ret << ", fd:" << fd << ", inode:" << file->GetInode() << ", offset:" << offset <<
            ", nbytes:" << nbytes << ", segNum:" << prepareResp.segNum << ".");
        return false;
    }
    return true;
}

static bool IsValidDirectSpaceWriteSegment(const InterceptorPwriteSpaceSegment &seg, uint64_t expectedOffset,
    uint64_t requestEnd)
{
    return seg.ret == 0 && seg.offset == expectedOffset && seg.nbytes != 0 &&
        seg.nbytes <= requestEnd - expectedOffset && seg.addrNum != 0 && seg.addrNum <= CACHE_SPACE_ADDRESS_SIZE &&
        (seg.mode == INTERCEPTOR_WRITE_MODE_LOCAL || seg.mode == INTERCEPTOR_WRITE_MODE_REMOTE);
}

static ssize_t CommitDirectSpaceWrite(const std::shared_ptr<OpenFile> &file, int fd, uint64_t nbytes, off_t offset,
    const char *api, const InterceptorPwritePrepareSpaceIn &prepareReq,
    const InterceptorPwritePrepareSpaceOut &prepareResp)
{
    InterceptorPwriteCommitSpaceIn commitReq{};
    commitReq.pid = prepareReq.pid;
    commitReq.fd = fd;
    commitReq.inode = prepareReq.inode;
    commitReq.offset = offset;
    commitReq.nbytes = nbytes;
    commitReq.startTime = prepareReq.startTime;
    commitReq.segNum = prepareResp.segNum;
    for (uint32_t segIdx = 0; segIdx < prepareResp.segNum; ++segIdx) {
        commitReq.segs[segIdx] = prepareResp.segs[segIdx];
    }

    InterceptorPwriteCommitSpaceOut commitResp{};
    int32_t ret = InterceptorClientNetService::Instance().SendSync<InterceptorPwriteCommitSpaceIn,
        InterceptorPwriteCommitSpaceOut>(INVALID_NID, BIO_OP_INTERCEPTOR_WRITE_COMMIT_SPACE, commitReq,
            commitResp);
    if (UNLIKELY(ret != BIO_OK || commitResp.ret != 0)) {
        CLOG_DEBUG("Commit direct write space failed, api:" << api << ", sendRet:" << ret << ", respRet:" <<
            commitResp.ret << ", fd:" << fd << ", inode:" << file->GetInode() << ", offset:" << offset <<
            ", nbytes:" << nbytes << ", committedBytes:" << commitResp.committedBytes << ".");
        if (ret == BIO_OK && commitResp.committedBytes != 0 &&
            commitResp.committedBytes < static_cast<uint64_t>(SSIZE_MAX)) {
            return static_cast<ssize_t>(commitResp.committedBytes);
        }
        return -1;
    }
    return static_cast<ssize_t>(nbytes);
}

static void AbortPreparedDirectSpaceWrite(int fd, uint64_t inode, int64_t offset, uint32_t segNum,
    const InterceptorPwriteSpaceSegment *segs)
{
    if (segNum == 0 || segNum > INTERCEPTOR_DIRECT_SPACE_MAX_SEGMENTS || segs == nullptr) {
        return;
    }

    InterceptorPwriteCommitSpaceIn commitReq{};
    commitReq.pid = InterceptorClientNetService::Instance().GetSendPid();
    commitReq.fd = fd;
    commitReq.inode = inode;
    commitReq.offset = offset;
    commitReq.segNum = segNum;
    commitReq.abortOnly = 1;
    for (uint32_t idx = 0; idx < segNum; ++idx) {
        commitReq.segs[idx] = segs[idx];
    }

    InterceptorPwriteCommitSpaceOut commitResp{};
    int32_t ret = InterceptorClientNetService::Instance().SendSync<InterceptorPwriteCommitSpaceIn,
        InterceptorPwriteCommitSpaceOut>(INVALID_NID, BIO_OP_INTERCEPTOR_WRITE_COMMIT_SPACE, commitReq,
            commitResp);
    if (UNLIKELY(ret != BIO_OK || commitResp.ret != 0)) {
        CLOG_DEBUG("Abort direct write space failed, sendRet:" << ret << ", respRet:" << commitResp.ret <<
            ", fd:" << fd << ", inode:" << inode << ", offset:" << offset << ", segNum:" << segNum << ".");
    }
}

static void AbortPreparedDirectSpaceWrite(int fd, uint64_t inode, int64_t offset,
    const InterceptorPwritePrepareSpaceOut &prepareResp)
{
    AbortPreparedDirectSpaceWrite(fd, inode, offset, prepareResp.segNum, prepareResp.segs);
}

static ssize_t PwriteNativeFallback(const std::shared_ptr<OpenFile> &file, int fd, const void *buf, size_t count,
    off_t offset)
{
    CLOG_DEBUG("Fallback write to native pwrite64, fd:" << fd << ", inode:" << file->GetInode() <<
        ", offset:" << offset << ", nbytes:" << count << ".");
    return CONTEXT.GetOperations()->pwrite64(fd, buf, count, offset);
}

ssize_t ProxyOperations::PwriteDirectSpaceInner(const std::shared_ptr<OpenFile> &file, int fd, const void *buf,
    size_t count, off_t offset)
{
    if (!IsDirectSpaceWriteSupported(offset, count)) {
        return DIRECT_WRITE_FALLBACK;
    }

    InterceptorPwritePrepareSpaceIn prepareReq{};
    InterceptorPwritePrepareSpaceOut prepareResp{};
    if (!PrepareDirectSpaceWrite(file, fd, count, offset, "direct", prepareReq, prepareResp)) {
        return -1;
    }

    uint64_t expectedOffset = static_cast<uint64_t>(offset);
    uint64_t requestEnd = expectedOffset + static_cast<uint64_t>(count);
    size_t copied = 0;
    for (uint32_t segIdx = 0; segIdx < prepareResp.segNum; ++segIdx) {
        auto &seg = prepareResp.segs[segIdx];
        if (UNLIKELY(!IsValidDirectSpaceWriteSegment(seg, expectedOffset, requestEnd))) {
            CLOG_DEBUG("Invalid direct write segment, fd:" << fd << ", inode:" << file->GetInode() <<
                ", segIdx:" << segIdx << ", segRet:" << seg.ret << ", segOffset:" << seg.offset <<
                ", expectedOffset:" << expectedOffset << ", segLen:" << seg.nbytes << ", addrNum:" <<
                seg.addrNum << ".");
            AbortPreparedDirectSpaceWrite(fd, file->GetInode(), offset, prepareResp);
            return -1;
        }

        uint64_t segLeft = seg.nbytes;
        size_t segCopied = 0;
        for (uint32_t addrIdx = 0; addrIdx < seg.addrNum && segLeft > 0; ++addrIdx) {
            uint64_t copyLen = std::min<uint64_t>(seg.addrLen[addrIdx], segLeft);
            if (copyLen == 0) {
                continue;
            }
            uint8_t *dst = InterceptorClientNetService::Instance().GetBioShmAddressFast(
                seg.addrOffset[addrIdx], copyLen);
            if (UNLIKELY(dst == nullptr)) {
                CLOG_DEBUG("Get direct write bio shm address failed, fd:" << fd << ", inode:" <<
                    file->GetInode() << ", segIdx:" << segIdx << ", addrIdx:" << addrIdx << ", offset:" <<
                    seg.addrOffset[addrIdx] << ", len:" << copyLen << ".");
                AbortPreparedDirectSpaceWrite(fd, file->GetInode(), offset, prepareResp);
                return -1;
            }
            int ret = memcpy_s(dst, static_cast<size_t>(copyLen),
                static_cast<const uint8_t *>(buf) + copied + segCopied, static_cast<size_t>(copyLen));
            if (UNLIKELY(ret != 0)) {
                AbortPreparedDirectSpaceWrite(fd, file->GetInode(), offset, prepareResp);
                return -1;
            }
            segCopied += static_cast<size_t>(copyLen);
            segLeft -= copyLen;
        }
        if (UNLIKELY(segLeft != 0 || segCopied != seg.nbytes)) {
            CLOG_DEBUG("Direct write segment copied length mismatch, fd:" << fd << ", inode:" <<
                file->GetInode() << ", segIdx:" << segIdx << ", copied:" << segCopied << ", nbytes:" <<
                seg.nbytes << ".");
            AbortPreparedDirectSpaceWrite(fd, file->GetInode(), offset, prepareResp);
            return -1;
        }
        copied += static_cast<size_t>(seg.nbytes);
        expectedOffset += seg.nbytes;
    }
    if (UNLIKELY(copied != count || expectedOffset != requestEnd)) {
        CLOG_DEBUG("Direct write copied length mismatch, fd:" << fd << ", inode:" << file->GetInode() <<
            ", copied:" << copied << ", nbytes:" << count << ", expectedOffset:" << expectedOffset <<
            ", requestEnd:" << requestEnd << ".");
        AbortPreparedDirectSpaceWrite(fd, file->GetInode(), offset, prepareResp);
        return -1;
    }

    return CommitDirectSpaceWrite(file, fd, count, offset, "direct", prepareReq, prepareResp);
}

static bool PrepareDirectSpaceWriteWindow(const std::shared_ptr<OpenFile> &file, int fd, uint64_t baseOffset,
    PendingDirectSpaceWriteWindow &window)
{
    auto shmRet = InterceptorClientNetService::Instance().EnsureBioShmForCurrentProcess();
    if (UNLIKELY(shmRet != BIO_OK)) {
        CLOG_DEBUG("Prepare direct-space write window without bio shm, ret:" << shmRet << ", fd:" << fd <<
            ", inode:" << file->GetInode() << ", baseOffset:" << baseOffset << ".");
        return false;
    }

    InterceptorPwritePrepareSpaceIn prepareReq{};
    prepareReq.pid = InterceptorClientNetService::Instance().GetSendPid();
    prepareReq.fd = fd;
    prepareReq.inode = file->GetInode();
    prepareReq.offset = static_cast<int64_t>(baseOffset);
    prepareReq.nbytes = INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;
    prepareReq.startTime = Monotonic::TimeNs();

    InterceptorPwritePrepareSpaceOut prepareResp{};
    int32_t ret = BIO_ERR;
    auto &service = InterceptorClientNetService::Instance();
    ret = service.SendSync<InterceptorPwritePrepareSpaceIn, InterceptorPwritePrepareSpaceOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_WRITE_PREPARE_SPACE, prepareReq, prepareResp);
    if (UNLIKELY(ret != BIO_OK || prepareResp.ret != 0 || prepareResp.segNum != 1 ||
        prepareResp.segs[0].ret != 0 || prepareResp.segs[0].offset != baseOffset ||
        prepareResp.segs[0].nbytes != INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE ||
        prepareResp.segs[0].addrNum == 0 || prepareResp.segs[0].addrNum > CACHE_SPACE_ADDRESS_SIZE ||
        (prepareResp.segs[0].mode != INTERCEPTOR_WRITE_MODE_LOCAL &&
            prepareResp.segs[0].mode != INTERCEPTOR_WRITE_MODE_REMOTE))) {
        CLOG_DEBUG("Prepare direct-space write window failed, sendRet:" << ret << ", respRet:" << prepareResp.ret <<
            ", fd:" << fd << ", inode:" << file->GetInode() << ", baseOffset:" << baseOffset <<
            ", segNum:" << prepareResp.segNum << ".");
        return false;
    }

    window.Reset();
    window.active = true;
    window.baseOffset = baseOffset;
    window.dataLen = 0;
    window.segNum = prepareResp.segNum;
    window.segs[0] = prepareResp.segs[0];
    return true;
}

static void AbortPreparedNextWriteWindow(const std::shared_ptr<OpenFile> &file, int fd,
    PreparedDirectSpaceWriteWindow &prepared)
{
    if (!prepared.ok || !prepared.window.active) {
        prepared.window.Reset();
        return;
    }
    AbortPreparedDirectSpaceWrite(fd, file->GetInode(), static_cast<int64_t>(prepared.window.baseOffset),
        prepared.window.segNum, prepared.window.segs);
    prepared.window.Reset();
    prepared.ok = false;
}

static PreparedDirectSpaceWriteWindow PrepareNextDirectSpaceWriteWindow(const std::shared_ptr<OpenFile> &file,
    int fd, uint64_t baseOffset)
{
    PreparedDirectSpaceWriteWindow prepared{};
    if (file == nullptr || !file->IsActive()) {
        return prepared;
    }
    prepared.ok = PrepareDirectSpaceWriteWindow(file, fd, baseOffset, prepared.window);
    if (prepared.ok && !file->IsActive()) {
        AbortPreparedNextWriteWindow(file, fd, prepared);
    }
    return prepared;
}

static bool GetNextDirectSpaceWritePrepareResultLocked(const std::shared_ptr<OpenFile> &file, int fd,
    uint64_t &preparedOffset, PreparedDirectSpaceWriteWindow &prepared)
{
    if (!file->HasNextDirectSpaceWritePrepare()) {
        return false;
    }
    std::future<PreparedDirectSpaceWriteWindow> future = file->TakeNextDirectSpaceWritePrepare(preparedOffset);
    if (!future.valid()) {
        return false;
    }
    try {
        prepared = future.get();
    } catch (const std::exception &e) {
        CLOG_DEBUG("Prepare next direct-space write window failed with exception, fd:" << fd << ", inode:" <<
            file->GetInode() << ", baseOffset:" << preparedOffset << ", error:" << e.what() << ".");
        return false;
    } catch (...) {
        CLOG_DEBUG("Prepare next direct-space write window failed with unknown exception, fd:" << fd <<
            ", inode:" << file->GetInode() << ", baseOffset:" << preparedOffset << ".");
        return false;
    }
    return true;
}

static void ClearNextDirectSpaceWritePrepareLocked(const std::shared_ptr<OpenFile> &file, int fd)
{
    uint64_t preparedOffset = 0;
    PreparedDirectSpaceWriteWindow prepared{};
    if (GetNextDirectSpaceWritePrepareResultLocked(file, fd, preparedOffset, prepared)) {
        AbortPreparedNextWriteWindow(file, fd, prepared);
    }
}

static bool ConsumeNextDirectSpaceWritePrepareLocked(const std::shared_ptr<OpenFile> &file, int fd,
    uint64_t baseOffset, PendingDirectSpaceWriteWindow &window)
{
    uint64_t preparedOffset = 0;
    PreparedDirectSpaceWriteWindow prepared{};
    if (!GetNextDirectSpaceWritePrepareResultLocked(file, fd, preparedOffset, prepared)) {
        return false;
    }
    if (!prepared.ok || !prepared.window.active || preparedOffset != baseOffset ||
        prepared.window.baseOffset != baseOffset) {
        AbortPreparedNextWriteWindow(file, fd, prepared);
        return false;
    }
    window = prepared.window;
    prepared.window.Reset();
    prepared.ok = false;
    return true;
}

static void MaybeStartNextDirectSpaceWritePrepareLocked(const std::shared_ptr<OpenFile> &file, int fd,
    const PendingDirectSpaceWriteWindow &window)
{
    if (!window.active || window.dataLen < DIRECT_WRITE_PREPARE_AHEAD_TRIGGER_BYTES ||
        file->HasNextDirectSpaceWritePrepare() ||
        window.baseOffset > UINT64_MAX - INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE) {
        return;
    }
    uint64_t nextOffset = window.baseOffset + INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;
    try {
        auto future = std::async(std::launch::async, PrepareNextDirectSpaceWriteWindow, file, fd, nextOffset);
        file->StartNextDirectSpaceWritePrepare(nextOffset, std::move(future));
    } catch (const std::exception &e) {
        CLOG_DEBUG("Start next direct-space write prepare failed, fd:" << fd << ", inode:" << file->GetInode() <<
            ", baseOffset:" << nextOffset << ", error:" << e.what() << ".");
    } catch (...) {
        CLOG_DEBUG("Start next direct-space write prepare failed with unknown exception, fd:" << fd <<
            ", inode:" << file->GetInode() << ", baseOffset:" << nextOffset << ".");
    }
}

static bool CopyToDirectSpaceSegment(const InterceptorPwriteSpaceSegment &seg, uint64_t segOffset, const void *buf,
    size_t count, uint32_t &failedAddrIdx, uint64_t &failedAddrOffset, uint64_t &failedLen)
{
    failedAddrIdx = UINT32_MAX;
    failedAddrOffset = 0;
    failedLen = 0;
    if (UNLIKELY(segOffset > seg.nbytes || count > seg.nbytes - segOffset)) {
        return false;
    }

    const auto *src = static_cast<const uint8_t *>(buf);
    uint64_t skip = segOffset;
    size_t copied = 0;
    for (uint32_t addrIdx = 0; addrIdx < seg.addrNum && copied < count; ++addrIdx) {
        uint64_t addrLen = seg.addrLen[addrIdx];
        if (skip >= addrLen) {
            skip -= addrLen;
            continue;
        }
        uint64_t copyLen = std::min<uint64_t>(addrLen - skip, static_cast<uint64_t>(count - copied));
        uint8_t *dst = InterceptorClientNetService::Instance().GetBioShmAddressFast(
            seg.addrOffset[addrIdx] + skip, copyLen);
        if (UNLIKELY(dst == nullptr)) {
            failedAddrIdx = addrIdx;
            failedAddrOffset = seg.addrOffset[addrIdx] + skip;
            failedLen = copyLen;
            return false;
        }
        int ret = memcpy_s(dst, static_cast<size_t>(copyLen), src + copied, static_cast<size_t>(copyLen));
        if (UNLIKELY(ret != 0)) {
            return false;
        }
        copied += static_cast<size_t>(copyLen);
        skip = 0;
    }
    return copied == count;
}

static bool SendDirectSpaceWriteCommit(uint64_t inode, int fd, InterceptorPwriteCommitSpaceIn &commitReq,
    uint64_t baseOffset, uint64_t dataLen)
{
    InterceptorPwriteCommitSpaceOut commitResp{};
    int32_t ret = BIO_ERR;
    ret = InterceptorClientNetService::Instance().SendSync<InterceptorPwriteCommitSpaceIn,
        InterceptorPwriteCommitSpaceOut>(INVALID_NID, BIO_OP_INTERCEPTOR_WRITE_COMMIT_SPACE, commitReq,
            commitResp);
    bool ok = ret == BIO_OK && commitResp.ret == 0 && commitResp.committedBytes == dataLen;
    if (UNLIKELY(!ok)) {
        CLOG_ERROR("Commit direct-space write window failed, sendRet:" << ret << ", respRet:" << commitResp.ret <<
            ", fd:" << fd << ", inode:" << inode << ", baseOffset:" << baseOffset << ", dataLen:" << dataLen <<
            ", committedBytes:" << commitResp.committedBytes << ".");
    } else {
        CLOG_DEBUG("Commit direct-space write window success, fd:" << fd << ", inode:" << inode <<
            ", baseOffset:" << baseOffset << ", dataLen:" << dataLen << ".");
    }
    return ok;
}

static InterceptorPwriteCommitSpaceIn BuildDirectSpaceWriteCommitReq(const std::shared_ptr<OpenFile> &file, int fd,
    const PendingDirectSpaceWriteWindow &window)
{
    InterceptorPwriteCommitSpaceIn commitReq{};
    commitReq.pid = InterceptorClientNetService::Instance().GetSendPid();
    commitReq.fd = fd;
    commitReq.inode = file->GetInode();
    commitReq.offset = static_cast<int64_t>(window.baseOffset);
    commitReq.nbytes = window.dataLen;
    commitReq.startTime = Monotonic::TimeNs();
    commitReq.segNum = window.segNum;
    for (uint32_t idx = 0; idx < window.segNum; ++idx) {
        commitReq.segs[idx] = window.segs[idx];
    }
    commitReq.segs[0].nbytes = window.dataLen;
    return commitReq;
}

static void AbortDirectSpaceWriteWindow(const std::shared_ptr<OpenFile> &file, int fd,
    PendingDirectSpaceWriteWindow &window)
{
    if (!window.active) {
        return;
    }
    AbortPreparedDirectSpaceWrite(fd, file->GetInode(), static_cast<int64_t>(window.baseOffset), window.segNum,
        window.segs);
    window.Reset();
}

static bool CommitDirectSpaceWriteWindowLocked(const std::shared_ptr<OpenFile> &file, int fd,
    PendingDirectSpaceWriteWindow &window)
{
    if (!window.active) {
        return true;
    }
    if (window.dataLen == 0) {
        AbortDirectSpaceWriteWindow(file, fd, window);
        return true;
    }

    InterceptorPwriteCommitSpaceIn commitReq = BuildDirectSpaceWriteCommitReq(file, fd, window);
    uint64_t baseOffset = window.baseOffset;
    uint64_t dataLen = window.dataLen;
    bool ok = SendDirectSpaceWriteCommit(file->GetInode(), fd, commitReq, baseOffset, dataLen);
    window.Reset();
    return ok;
}

bool ProxyOperations::FlushPendingWriteWindow(const std::shared_ptr<OpenFile> &file, int fd)
{
    if (file == nullptr) {
        return true;
    }
    std::lock_guard<std::mutex> lock(file->PendingWriteMutex());
    auto &directWindow = file->DirectSpaceWriteWindow();
    bool ok = CommitDirectSpaceWriteWindowLocked(file, fd, directWindow);
    ClearNextDirectSpaceWritePrepareLocked(file, fd);
    return ok;
}

ssize_t ProxyOperations::PwriteAggregatedDirectSpaceInner(const std::shared_ptr<OpenFile> &file, int fd,
    const void *buf, size_t count, off_t offset)
{
    if (!DirectWriteAggregateEnabled()) {
        return AGGREGATE_WRITE_FALLBACK;
    }
    if (offset < 0 || count == 0 || count > INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE) {
        return AGGREGATE_WRITE_FALLBACK;
    }

    std::lock_guard<std::mutex> lock(file->PendingWriteMutex());

    auto &window = file->DirectSpaceWriteWindow();
    uint64_t writeOffset = static_cast<uint64_t>(offset);
    uint64_t baseOffset = WriteAggregateWindowBase(writeOffset);
    if (window.active) {
        uint64_t expected = window.baseOffset + window.dataLen;
        if (writeOffset != expected || baseOffset != window.baseOffset ||
            count > INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE - window.dataLen) {
            if (UNLIKELY(!CommitDirectSpaceWriteWindowLocked(file, fd, window))) {
                ClearNextDirectSpaceWritePrepareLocked(file, fd);
                return -1;
            }
            ClearNextDirectSpaceWritePrepareLocked(file, fd);
            return AGGREGATE_WRITE_FALLBACK;
        }
    } else {
        if (!IsWriteAggregateWindowStart(writeOffset)) {
            ClearNextDirectSpaceWritePrepareLocked(file, fd);
            return AGGREGATE_WRITE_FALLBACK;
        }
        if (!ConsumeNextDirectSpaceWritePrepareLocked(file, fd, baseOffset, window) &&
            !PrepareDirectSpaceWriteWindow(file, fd, baseOffset, window)) {
            return AGGREGATE_WRITE_FALLBACK;
        }
    }

    uint32_t failedAddrIdx = UINT32_MAX;
    uint64_t failedAddrOffset = 0;
    uint64_t failedLen = 0;
    if (UNLIKELY(!CopyToDirectSpaceSegment(window.segs[0], window.dataLen, buf, count,
        failedAddrIdx, failedAddrOffset, failedLen))) {
        CLOG_DEBUG("Copy direct-space write window failed, fd:" << fd << ", inode:" <<
            file->GetInode() << ", baseOffset:" << window.baseOffset << ", dataLen:" << window.dataLen <<
            ", count:" << count << ", addrIdx:" << failedAddrIdx << ", addrOffset:" << failedAddrOffset <<
            ", len:" << failedLen << ".");
        AbortPreparedDirectSpaceWrite(fd, file->GetInode(), static_cast<int64_t>(window.baseOffset), window.segNum,
            window.segs);
        window.Reset();
        return -1;
    }
    window.dataLen += static_cast<uint64_t>(count);
    if (window.dataLen == INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE) {
        if (UNLIKELY(!CommitDirectSpaceWriteWindowLocked(file, fd, window))) {
            ClearNextDirectSpaceWritePrepareLocked(file, fd);
            return -1;
        }
    } else {
        MaybeStartNextDirectSpaceWritePrepareLocked(file, fd, window);
    }
    return static_cast<ssize_t>(count);
}

ssize_t ProxyOperations::PwriteShmInner(const std::shared_ptr<OpenFile> &file, int fd, const void *buf,
    size_t count, off_t offset, bool *intercepted)
{
    if (intercepted != nullptr) {
        *intercepted = false;
    }
    ssize_t directAggregateRet = PwriteAggregatedDirectSpaceInner(file, fd, buf, count, offset);
    if (directAggregateRet >= 0 || directAggregateRet == -1) {
        if (directAggregateRet >= 0) {
            LogWritePathSuccess("aggregate", fd, file->GetInode(), offset, count, directAggregateRet);
            if (intercepted != nullptr) {
                *intercepted = true;
            }
        }
        return directAggregateRet;
    }

    ssize_t ret = PwriteDirectSpaceInner(file, fd, buf, count, offset);
    if (ret >= 0) {
        LogWritePathSuccess("direct", fd, file->GetInode(), offset, count, ret);
        if (intercepted != nullptr) {
            *intercepted = true;
        }
        return ret;
    }
    if (ret == DIRECT_WRITE_FALLBACK) {
        ret = PwriteNativeFallback(file, fd, buf, count, offset);
        return ret;
    }

    CLOG_ERROR("Direct-space write failed without fallback, fd:" << fd << ", inode:" <<
        file->GetInode() << ", offset:" << offset << ", nbytes:" << count << ".");
    return -1;
}

ssize_t ProxyOperations::Write(int fd, const void *buf, size_t nbytes)
{
    CLOG_DEBUG("Write fd:" << fd << ", count:" << nbytes << ".");
    const auto &file = CONTEXT.files.AtCached(fd);
    if (file == nullptr) {
        CLOG_DEBUG("Fallback write to native, fd:" << fd << ", nbytes:" << nbytes << ".");
        return CONTEXT.GetOperations()->write(fd, buf, nbytes);
    }

    off_t offset = file->ReserveOffset(nbytes);

    auto ret = PwriteInner("write", file, fd, buf, nbytes, offset);
    if (UNLIKELY(ret < 0)) {
        file->CompleteReservedOffset(nbytes, 0);
        errno = EIO;
        return -1;
    }

    file->CompleteReservedOffset(nbytes, ret);
    return ret;
}

ssize_t ProxyOperations::Pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    CLOG_DEBUG("Pwrite fd:" << fd << ", offset:" << offset << ", length:" << count << ".");
    const auto &file = CONTEXT.files.AtCached(fd);
    if (file == nullptr) {
        CLOG_DEBUG("Fallback pwrite to native, fd:" << fd << ", offset:" << offset << ", nbytes:" << count << ".");
        return CONTEXT.GetOperations()->pwrite(fd, buf, count, offset);
    }

    auto ret = PwriteInner("pwrite", file, fd, buf, count, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }
    return ret;
}

ssize_t ProxyOperations::Pwrite64(int fd, const void *buf, size_t count, off64_t offset)
{
    CLOG_DEBUG("Pwrite64 fd:" << fd << ", offset:" << offset << ", length:" << count << ".");
    const auto &file = CONTEXT.files.AtCached(fd);
    if (file == nullptr) {
        CLOG_DEBUG("Fallback pwrite64 to native, fd:" << fd << ", offset:" << offset << ", nbytes:" << count << ".");
        return CONTEXT.GetOperations()->pwrite64(fd, buf, count, offset);
    }

    auto ret = PwriteInner("pwrite64", file, fd, buf, count, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }
    return ret;
}
