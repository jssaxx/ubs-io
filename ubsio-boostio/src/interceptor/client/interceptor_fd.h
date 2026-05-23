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

#ifndef BOOST_IO_INTERCEPTOR_FD_H
#define BOOST_IO_INTERCEPTOR_FD_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unistd.h>
#include <fcntl.h>

#include "bio_lock.h"
#include "interceptor_read_index.h"
#include "message.h"

namespace ock {
namespace bio {
struct PendingDirectSpaceWriteWindow {
    bool active = false;
    uint64_t baseOffset = 0;
    uint64_t dataLen = 0;
    uint32_t segNum = 0;
    InterceptorPwriteSpaceSegment segs[INTERCEPTOR_DIRECT_SPACE_MAX_SEGMENTS] {};

    void Reset()
    {
        active = false;
        baseOffset = 0;
        dataLen = 0;
        segNum = 0;
        for (auto &seg : segs) {
            seg = {};
        }
    }
};

struct PreparedDirectSpaceWriteWindow {
    bool ok = false;
    PendingDirectSpaceWriteWindow window;
};

struct RemoteReadWindowCache {
    bool active = false;
    uint64_t fileOffset = 0;
    uint64_t dataLen = 0;
    uint64_t addrOffset = 0;
    uint64_t addrLen = 0;

    bool Contains(uint64_t offset, size_t count) const
    {
        return active && dataLen != 0 && addrLen >= dataLen && offset >= fileOffset &&
            static_cast<uint64_t>(count) <= dataLen && offset - fileOffset <= dataLen - count;
    }

    void Reset()
    {
        active = false;
        fileOffset = 0;
        dataLen = 0;
        addrOffset = 0;
        addrLen = 0;
    }
};

constexpr size_t REMOTE_READ_PREFETCH_DEPTH_DEFAULT = 2;
constexpr size_t REMOTE_READ_PREFETCH_LIMIT_MAX = 8;
constexpr size_t REMOTE_READ_WINDOW_CACHE_SLOTS = REMOTE_READ_PREFETCH_LIMIT_MAX + 1;
constexpr size_t REMOTE_READ_MAX_PREFETCH_INFLIGHT = REMOTE_READ_PREFETCH_LIMIT_MAX;
constexpr size_t LOCAL_READ_INDEX_CACHE_SLOTS = 3;
constexpr int ENV_PARSE_BASE_DECIMAL = 10;

struct RemoteReadPrefetchConfig {
    size_t depth = REMOTE_READ_PREFETCH_DEPTH_DEFAULT;
    size_t cacheSlots = REMOTE_READ_PREFETCH_DEPTH_DEFAULT + 1;
    bool fromStart = true;
};

inline size_t ParseRemoteReadPrefetchEnv(const char *name, size_t defaultValue)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return defaultValue;
    }
    if (value[0] == '-') {
        return defaultValue;
    }
    char *end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, ENV_PARSE_BASE_DECIMAL);
    if (end == value || *end != '\0') {
        return defaultValue;
    }
    return std::min<size_t>(parsed, REMOTE_READ_PREFETCH_LIMIT_MAX);
}

inline bool ParseRemoteReadPrefetchBoolEnv(const char *name, bool defaultValue)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return defaultValue;
    }
    return value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
        value[0] == 'y' || value[0] == 'Y' || value[0] == 'o' || value[0] == 'O';
}

inline const RemoteReadPrefetchConfig &GetRemoteReadPrefetchConfig()
{
    static const RemoteReadPrefetchConfig config = [] {
        RemoteReadPrefetchConfig cfg;
        cfg.depth = ParseRemoteReadPrefetchEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_DEPTH",
            REMOTE_READ_PREFETCH_DEPTH_DEFAULT);
        cfg.cacheSlots = std::max<size_t>(1, std::min<size_t>(cfg.depth + 1, REMOTE_READ_WINDOW_CACHE_SLOTS));
        cfg.fromStart = ParseRemoteReadPrefetchBoolEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_FROM_START", true);
        return cfg;
    }();
    return config;
}

class OpenFile {
public:
    OpenFile(int fd, uint64_t inode, uint64_t fileSize = UINT64_MAX)
        : mFd(fd), mInode(inode), mRemoteReadEofOffset(fileSize), mKnownFileSize(fileSize)
    {}

    virtual ~OpenFile() {}

    inline uint64_t GetInode() const
    {
        return mInode;
    }

    bool IsActive() const
    {
        return mActive.load(std::memory_order_acquire);
    }

    void Deactivate()
    {
        mActive.store(false, std::memory_order_release);
    }

    off64_t ReserveOffset(size_t count)
    {
        std::lock_guard<std::mutex> lock(mOffsetMtx);
        off64_t offset = mOffset;
        mOffset += static_cast<off64_t>(count);
        return offset;
    }

    void CompleteReservedOffset(size_t reserved, ssize_t actual)
    {
        if (static_cast<size_t>(actual) == reserved) {
            return;
        }
        std::lock_guard<std::mutex> lock(mOffsetMtx);
        mOffset += static_cast<off64_t>(actual) - static_cast<off64_t>(reserved);
    }

    off64_t SetOffset(off64_t offset)
    {
        std::lock_guard<std::mutex> lock(mOffsetMtx);
        mOffset = offset;
        return mOffset;
    }

    off64_t CalcSeekOffset(off64_t offset, int whence)
    {
        std::lock_guard<std::mutex> lock(mOffsetMtx);
        off64_t newOffset = mOffset;
        if (whence == SEEK_SET) {
            newOffset = offset;
        } else if (whence == SEEK_CUR) {
            newOffset += offset;
        } else {
            return -1;
        }
        if (newOffset < 0) {
            return -1;
        }
        return newOffset;
    }

    void DropReadAddrCache()
    {
        InvalidateLocalReadIndexCache();
    }

    std::mutex &RemoteReadWindowMutex()
    {
        return mRemoteReadWindowMtx;
    }

    std::array<RemoteReadWindowCache, REMOTE_READ_WINDOW_CACHE_SLOTS> &RemoteReadWindows()
    {
        return mRemoteReadWindows;
    }

    size_t RemoteReadWindowSlotCount() const
    {
        return GetRemoteReadPrefetchConfig().cacheSlots;
    }

    uint64_t RemoteReadWindowGeneration() const
    {
        return mRemoteReadWindowGeneration;
    }

    void InvalidateRemoteReadWindowCache(
        std::array<RemoteReadWindowCache, REMOTE_READ_WINDOW_CACHE_SLOTS> *evicted = nullptr)
    {
        std::lock_guard<std::mutex> lock(mRemoteReadWindowMtx);
        if (evicted != nullptr) {
            *evicted = mRemoteReadWindows;
        }
        for (auto &cache : mRemoteReadWindows) {
            cache.Reset();
        }
        ++mRemoteReadWindowGeneration;
        for (auto &prefetch : mRemoteReadPrefetches) {
            prefetch.inFlight = false;
            prefetch.offset = 0;
        }
        mRemoteReadLastEnd.store(UINT64_MAX, std::memory_order_release);
        mRemoteReadEofOffset.store(mKnownFileSize.load(std::memory_order_acquire), std::memory_order_release);
    }

    void EvictRemoteReadWindowsBefore(uint64_t fileOffset,
        std::array<RemoteReadWindowCache, REMOTE_READ_WINDOW_CACHE_SLOTS> *evicted)
    {
        std::lock_guard<std::mutex> lock(mRemoteReadWindowMtx);
        size_t evictIdx = 0;
        for (auto &cache : mRemoteReadWindows) {
            if (!cache.active || cache.fileOffset >= fileOffset) {
                continue;
            }
            if (evicted != nullptr && evictIdx < evicted->size()) {
                (*evicted)[evictIdx++] = cache;
            }
            cache.Reset();
        }
    }

    bool TryStartRemoteReadPrefetch(uint64_t fileOffset, uint64_t &generation, size_t depthLimit)
    {
        std::lock_guard<std::mutex> lock(mRemoteReadWindowMtx);
        if (fileOffset >= mRemoteReadEofOffset.load(std::memory_order_acquire)) {
            return false;
        }
        for (const auto &cache : mRemoteReadWindows) {
            if (cache.Contains(fileOffset, 1U)) {
                return false;
            }
        }
        depthLimit = std::min(depthLimit, mRemoteReadPrefetches.size());
        RemoteReadPrefetch *freeSlot = nullptr;
        for (size_t idx = 0; idx < mRemoteReadPrefetches.size(); ++idx) {
            auto &prefetch = mRemoteReadPrefetches[idx];
            if (prefetch.inFlight && prefetch.offset == fileOffset) {
                return false;
            }
            if (idx < depthLimit && !prefetch.inFlight && freeSlot == nullptr) {
                freeSlot = &prefetch;
            }
        }
        if (freeSlot == nullptr) {
            return false;
        }
        freeSlot->inFlight = true;
        freeSlot->offset = fileOffset;
        generation = mRemoteReadWindowGeneration;
        return true;
    }

    bool IsRemoteReadPrefetchInFlight(uint64_t offset, size_t count)
    {
        std::lock_guard<std::mutex> lock(mRemoteReadWindowMtx);
        if (count > INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE) {
            return false;
        }
        for (const auto &prefetch : mRemoteReadPrefetches) {
            if (!prefetch.inFlight || offset < prefetch.offset) {
                continue;
            }
            uint64_t skip = offset - prefetch.offset;
            if (skip <= INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE - count) {
                return true;
            }
        }
        return false;
    }

    void FinishRemoteReadPrefetch(uint64_t fileOffset, uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(mRemoteReadWindowMtx);
        for (auto &prefetch : mRemoteReadPrefetches) {
            if (prefetch.inFlight && prefetch.offset == fileOffset && mRemoteReadWindowGeneration == generation) {
                prefetch.inFlight = false;
                prefetch.offset = 0;
                return;
            }
        }
    }

    bool RecordRemoteRead(uint64_t offset, size_t count)
    {
        uint64_t readEnd = offset + static_cast<uint64_t>(count);
        if (readEnd < offset) {
            mRemoteReadLastEnd.store(UINT64_MAX, std::memory_order_release);
            return false;
        }
        uint64_t lastEnd = mRemoteReadLastEnd.exchange(readEnd, std::memory_order_acq_rel);
        return lastEnd == offset;
    }

    bool IsRemoteReadSequential(uint64_t offset) const
    {
        return mRemoteReadLastEnd.load(std::memory_order_acquire) == offset;
    }

    void MarkRemoteReadEof(uint64_t eofOffset)
    {
        uint64_t current = mRemoteReadEofOffset.load(std::memory_order_relaxed);
        while (eofOffset < current &&
            !mRemoteReadEofOffset.compare_exchange_weak(current, eofOffset, std::memory_order_release,
                std::memory_order_relaxed)) {
        }
    }

    void ExpandKnownFileSize(uint64_t fileSize)
    {
        uint64_t knownSize = mKnownFileSize.load(std::memory_order_relaxed);
        uint64_t eofOffset = mRemoteReadEofOffset.load(std::memory_order_relaxed);
        if (fileSize <= knownSize && fileSize <= eofOffset) {
            return;
        }
        while (fileSize > knownSize &&
            !mKnownFileSize.compare_exchange_weak(knownSize, fileSize, std::memory_order_release,
                std::memory_order_relaxed)) {
        }
        while (fileSize > eofOffset &&
            !mRemoteReadEofOffset.compare_exchange_weak(eofOffset, fileSize, std::memory_order_release,
                std::memory_order_relaxed)) {
        }
    }

    bool LoadLocalReadIndexCache(uint64_t offset, size_t count, InterceptorReadIndexCache &cache)
    {
        std::lock_guard<std::mutex> lock(mLocalReadIndexMtx);
        for (const auto &item : mLocalReadIndexCaches) {
            if (item.entry == nullptr || item.dataLen == 0 || offset < item.fileOffset ||
                count > item.dataLen || offset - item.fileOffset > item.dataLen - count) {
                continue;
            }
            cache = item;
            return true;
        }
        return false;
    }

    void StoreLocalReadIndexCache(const InterceptorReadIndexCache &cache)
    {
        uint64_t startBlock = InterceptorReadIndexBlockOffset(cache.fileOffset);
        uint64_t lastOffset = cache.fileOffset + cache.dataLen - 1U;
        if (cache.dataLen == 0 || lastOffset < cache.fileOffset ||
            InterceptorReadIndexBlockOffset(lastOffset) != startBlock) {
            return;
        }
        std::lock_guard<std::mutex> lock(mLocalReadIndexMtx);
        for (auto &item : mLocalReadIndexCaches) {
            if (item.fileOffset == cache.fileOffset) {
                item = cache;
                return;
            }
        }
        for (auto &item : mLocalReadIndexCaches) {
            if (item.entry == nullptr) {
                item = cache;
                return;
            }
        }
        mLocalReadIndexCaches[mLocalReadIndexNextSlot] = cache;
        mLocalReadIndexNextSlot = (mLocalReadIndexNextSlot + 1U) % mLocalReadIndexCaches.size();
    }

    void InvalidateLocalReadIndexCache()
    {
        std::lock_guard<std::mutex> lock(mLocalReadIndexMtx);
        for (auto &item : mLocalReadIndexCaches) {
            item.entry = nullptr;
            item.seq = 0;
            item.dataLen = 0;
            item.addrNum = 0;
        }
        mLocalReadIndexNextSlot = 0;
    }

    std::mutex &PendingWriteMutex()
    {
        return mPendingWriteMtx;
    }

    PendingDirectSpaceWriteWindow &DirectSpaceWriteWindow()
    {
        return mDirectSpaceWriteWindow;
    }

    bool HasNextDirectSpaceWritePrepare() const
    {
        return mNextDirectSpaceWritePrepareActive;
    }

    void StartNextDirectSpaceWritePrepare(uint64_t offset, std::future<PreparedDirectSpaceWriteWindow> &&future)
    {
        mNextDirectSpaceWritePrepareOffset = offset;
        mNextDirectSpaceWritePrepare = std::move(future);
        mNextDirectSpaceWritePrepareActive = true;
    }

    std::future<PreparedDirectSpaceWriteWindow> TakeNextDirectSpaceWritePrepare(uint64_t &offset)
    {
        offset = mNextDirectSpaceWritePrepareOffset;
        mNextDirectSpaceWritePrepareOffset = 0;
        mNextDirectSpaceWritePrepareActive = false;
        return std::move(mNextDirectSpaceWritePrepare);
    }

private:
    int mFd;
    uint64_t mInode;
    std::atomic<bool> mActive{true};
    std::mutex mOffsetMtx;
    off64_t mOffset = 0;
    std::mutex mLocalReadIndexMtx;
    std::array<InterceptorReadIndexCache, LOCAL_READ_INDEX_CACHE_SLOTS> mLocalReadIndexCaches{};
    size_t mLocalReadIndexNextSlot = 0;
    std::mutex mPendingWriteMtx;
    PendingDirectSpaceWriteWindow mDirectSpaceWriteWindow;
    bool mNextDirectSpaceWritePrepareActive = false;
    uint64_t mNextDirectSpaceWritePrepareOffset = 0;
    std::future<PreparedDirectSpaceWriteWindow> mNextDirectSpaceWritePrepare;
    std::mutex mRemoteReadWindowMtx;
    std::array<RemoteReadWindowCache, REMOTE_READ_WINDOW_CACHE_SLOTS> mRemoteReadWindows{};
    struct RemoteReadPrefetch {
        bool inFlight = false;
        uint64_t offset = 0;
    };
    std::array<RemoteReadPrefetch, REMOTE_READ_MAX_PREFETCH_INFLIGHT> mRemoteReadPrefetches{};
    uint64_t mRemoteReadWindowGeneration = 0;
    std::atomic<uint64_t> mRemoteReadLastEnd{UINT64_MAX};
    std::atomic<uint64_t> mRemoteReadEofOffset;
    std::atomic<uint64_t> mKnownFileSize;
};

class OpenFileMap {
public:
    OpenFileMap() = default;

    virtual ~OpenFileMap();

    OpenFileMap(const OpenFileMap &) = delete;

    OpenFileMap &operator = (const OpenFileMap &) = delete;

    bool Exist(int fd);

    bool Add(int fd, std::shared_ptr<OpenFile> &&file);

    std::shared_ptr<OpenFile> At(int fd);

    const std::shared_ptr<OpenFile> &AtCached(int fd);

    std::vector<std::pair<int, std::shared_ptr<OpenFile>>> Snapshot();

    void Erase(int fd);

private:
    ReadWriteLock filesMtx;
    std::unordered_map<int, std::shared_ptr<OpenFile>> files;
};
}
}
#endif // BOOST_IO_INTERCEPTOR_FD_H
