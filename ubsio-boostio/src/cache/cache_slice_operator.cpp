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

#include "cache_slice_operator.h"
#include <semaphore.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <unordered_map>
#include <vector>
#include "bdm_core.h"
#include "bio_config_instance.h"
#include "bio_tracepoint_helper.h"
#include "bio_trace.h"

namespace ock {
namespace bio {
namespace {
constexpr uint32_t BDM_BATCH_READ_WINDOW_DEFAULT_KEYS = 128;
constexpr uint32_t BDM_BATCH_READ_WINDOW_PIPELINE_DEPTH = 4;
constexpr uint64_t BDM_BATCH_READ_WINDOW_MAX_BYTES = 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t BDM_BATCH_READ_MB = 1024ULL * 1024ULL;
constexpr uint64_t BDM_ID_SHIFT = 48UL;
constexpr uint64_t BDM_ID_MASK = 0xFFFFUL;
constexpr size_t BDM_BATCH_TEMP_ALIGN = 512UL;
constexpr size_t BDM_BATCH_TEMP_POOL_BUCKET_LIMIT = 128UL;
constexpr BResult BDM_BATCH_PENDING = BIO_INNER_RETRY;

struct BdmBatchContext;

struct BdmBatchRequest {
    BdmBatchRequest(uint64_t chunk, uint64_t off, void *buffer, uint64_t length)
        : chunkId(chunk), offset(off), buf(buffer), len(length)
    {
    }
    BdmBatchRequest(const BdmBatchRequest &) = delete;
    BdmBatchRequest &operator=(const BdmBatchRequest &) = delete;
    BdmBatchRequest(BdmBatchRequest &&other) noexcept
        : chunkId(other.chunkId), offset(other.offset), buf(other.buf), len(other.len), ioCtx(other.ioCtx),
          batch(other.batch), result(other.result.load(std::memory_order_relaxed)),
          done(other.done.load(std::memory_order_relaxed))
    {
    }

    uint64_t chunkId;
    uint64_t offset;
    void *buf;
    uint64_t len;
    BdmIoCtx ioCtx = {};
    BdmBatchContext *batch = nullptr;
    std::atomic<int32_t> result { BDM_CODE_OK };
    std::atomic<bool> done { false };
};

struct BdmBatchContext {
    sem_t sem;
    std::atomic<uint32_t> pending { 0 };
    std::atomic<int32_t> result { BDM_CODE_OK };
    bool semInited = false;
};

void CompleteBdmBatchRequest(BdmBatchRequest *req, int32_t ret)
{
    if (req == nullptr || req->batch == nullptr || req->done.exchange(true)) {
        return;
    }

    req->result.store(ret, std::memory_order_relaxed);
    if (ret != BDM_CODE_OK) {
        int32_t expected = BDM_CODE_OK;
        req->batch->result.compare_exchange_strong(expected, ret);
    }

    if (req->batch->pending.fetch_sub(1) == 1) {
        sem_post(&req->batch->sem);
    }
}

void BdmBatchCallback(void *ctx, int32_t ret)
{
    CompleteBdmBatchRequest(static_cast<BdmBatchRequest *>(ctx), ret);
}

BResult WaitBdmBatch(BdmBatchContext &batch)
{
    if (!batch.semInited) {
        return batch.result.load() == BDM_CODE_OK ? BIO_OK : BIO_DISK_IOERR;
    }

    while (true) {
        if (sem_wait(&batch.sem) == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        LOG_ERROR("Wait bdm batch semaphore failed.");
        return BIO_ERR;
    }

    return batch.result.load() == BDM_CODE_OK ? BIO_OK : BIO_DISK_IOERR;
}

uint32_t BdmBatchId(uint64_t chunkId)
{
    return static_cast<uint32_t>((chunkId >> BDM_ID_SHIFT) & BDM_ID_MASK);
}

uint32_t GetBdmBatchReadWindowKeys()
{
    uint32_t windowKeys = BioConfig::Instance()->GetDaemonConfig().bdmBatchReadWindowKeys;
    return windowKeys == 0 ? BDM_BATCH_READ_WINDOW_DEFAULT_KEYS : windowKeys;
}

uint64_t GetBdmBatchReadWindowBytes()
{
    uint64_t windowBytes =
        static_cast<uint64_t>(BioConfig::Instance()->GetDaemonConfig().bdmBatchReadWindowBytesMb) * BDM_BATCH_READ_MB;
    return windowBytes == 0 ? BDM_BATCH_READ_WINDOW_MAX_BYTES : windowBytes;
}

uint32_t GetBdmBatchReadPipelineDepth()
{
    uint32_t pipelineDepth = BioConfig::Instance()->GetDaemonConfig().bdmBatchReadPipelineDepth;
    return pipelineDepth == 0 ? BDM_BATCH_READ_WINDOW_PIPELINE_DEPTH : pipelineDepth;
}

class BdmBatchTempBufferPool {
public:
    ~BdmBatchTempBufferPool()
    {
        std::lock_guard<std::mutex> lock(mLock);
        for (auto &bucket : mBuffers) {
            for (auto *buffer : bucket.second) {
                free(buffer);
            }
        }
    }

    char *Alloc(uint64_t len)
    {
        std::call_once(mInitFlag, [this]() { Init(); });
        uint64_t bucketSize = NormalizeSize(len);
        if (bucketSize == 0) {
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(mLock);
            auto &bucket = mBuffers[bucketSize];
            if (!bucket.empty()) {
                char *buffer = bucket.back();
                bucket.pop_back();
                mCachedBytes -= bucketSize;
                return buffer;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mLock);
            mAllocatedBytes += bucketSize;
        }

        void *ptr = nullptr;
        if (posix_memalign(&ptr, BDM_BATCH_TEMP_ALIGN, bucketSize) != 0) {
            std::lock_guard<std::mutex> lock(mLock);
            mAllocatedBytes -= bucketSize;
            return nullptr;
        }
        return static_cast<char *>(ptr);
    }

    void Free(char *buffer, uint64_t len)
    {
        if (buffer == nullptr) {
            return;
        }
        std::call_once(mInitFlag, [this]() { Init(); });

        uint64_t bucketSize = NormalizeSize(len);
        if (bucketSize == 0) {
            free(buffer);
            return;
        }

        std::lock_guard<std::mutex> lock(mLock);
        auto &bucket = mBuffers[bucketSize];
        if ((mMaxCachedBytes == 0 && bucket.size() >= BDM_BATCH_TEMP_POOL_BUCKET_LIMIT) ||
            (mMaxCachedBytes != 0 && mCachedBytes + bucketSize > mMaxCachedBytes)) {
            free(buffer);
            mAllocatedBytes -= bucketSize;
            return;
        }
        bucket.emplace_back(buffer);
        mCachedBytes += bucketSize;
    }

    void Prewarm()
    {
        std::call_once(mInitFlag, [this]() { Init(); });
    }

private:
    void Init()
    {
        const auto &daemonConfig = BioConfig::Instance()->GetDaemonConfig();
        uint64_t configuredBytes = static_cast<uint64_t>(daemonConfig.bdmBatchReadTempPoolMb) * BDM_BATCH_READ_MB;
        if (configuredBytes == 0) {
            return;
        }

        mMaxCachedBytes = configuredBytes;
        uint64_t bufferSize = NormalizeSize(daemonConfig.segment);
        if (bufferSize == 0) {
            return;
        }

        uint64_t bufferCount = configuredBytes / bufferSize;
        uint64_t warmupCount = 0;
        for (uint64_t i = 0; i < bufferCount; i++) {
            void *ptr = nullptr;
            if (posix_memalign(&ptr, BDM_BATCH_TEMP_ALIGN, bufferSize) != 0) {
                break;
            }
            std::lock_guard<std::mutex> lock(mLock);
            mBuffers[bufferSize].emplace_back(static_cast<char *>(ptr));
            mCachedBytes += bufferSize;
            mAllocatedBytes += bufferSize;
            warmupCount++;
        }
        if (warmupCount != bufferCount) {
            LOG_WARN("Prewarm bdm batch temp buffer pool incomplete, expected:" << bufferCount <<
                ", actual:" << warmupCount << ", bufferSize:" << bufferSize << ".");
        }
    }

    static uint64_t NormalizeSize(uint64_t len)
    {
        if (len == 0 || len > UINT64_MAX - BDM_BATCH_TEMP_ALIGN + 1) {
            return 0;
        }
        return ((len + BDM_BATCH_TEMP_ALIGN - 1) / BDM_BATCH_TEMP_ALIGN) * BDM_BATCH_TEMP_ALIGN;
    }

    std::mutex mLock;
    std::unordered_map<uint64_t, std::vector<char *>> mBuffers;
    std::once_flag mInitFlag;
    uint64_t mCachedBytes = 0;
    uint64_t mAllocatedBytes = 0;
    uint64_t mMaxCachedBytes = 0;
};

BdmBatchTempBufferPool &GetBdmBatchTempBufferPool()
{
    static BdmBatchTempBufferPool pool;
    return pool;
}

int32_t SubmitBdmBatchGroup(std::vector<BdmBatchRequest> &requests, const std::vector<uint32_t> &indices, bool isRead)
{
    if (indices.size() == 1 && BdmGetIoEngine() != BDM_IO_ENGINE_SYNC) {
        auto &request = requests[indices[0]];
        return isRead ? BdmReadAsync(request.chunkId, request.offset, request.buf, request.len, &request.ioCtx) :
                        BdmWriteAsync(request.chunkId, request.offset, request.buf, request.len, &request.ioCtx);
    }

    std::vector<BdmBatchIo> ios;
    ios.reserve(indices.size());
    for (uint32_t index : indices) {
        auto &request = requests[index];
        BdmBatchIo io = { request.chunkId, request.offset, request.buf, request.len, &request.ioCtx };
        ios.emplace_back(io);
    }
    return isRead ? BdmReadBatchAsync(ios.data(), static_cast<uint32_t>(ios.size())) :
                    BdmWriteBatchAsync(ios.data(), static_cast<uint32_t>(ios.size()));
}

BResult SubmitBdmBatchAsync(std::vector<BdmBatchRequest> &requests, bool isRead, BdmBatchContext &batch)
{
    if (requests.empty()) {
        return BIO_OK;
    }

    if (sem_init(&batch.sem, 0, 0) != 0) {
        LOG_ERROR("Init bdm batch semaphore failed.");
        return BIO_ERR;
    }
    batch.semInited = true;
    batch.pending.store(static_cast<uint32_t>(requests.size()));

    for (auto &request : requests) {
        request.batch = &batch;
        request.ioCtx.cb = BdmBatchCallback;
        request.ioCtx.ctx = &request;
    }

    BResult result = BIO_OK;
    try {
        std::unordered_map<uint32_t, std::vector<uint32_t>> groupedIndices;
        groupedIndices.reserve(requests.size());
        for (uint32_t i = 0; i < requests.size(); i++) {
            groupedIndices[BdmBatchId(requests[i].chunkId)].emplace_back(i);
        }

        for (const auto &group : groupedIndices) {
            int32_t ret = SubmitBdmBatchGroup(requests, group.second, isRead);
            if (ret == BDM_CODE_OK) {
                continue;
            }
            result = BIO_DISK_IOERR;
            for (uint32_t index : group.second) {
                CompleteBdmBatchRequest(&requests[index], ret);
            }
        }
    } catch (const std::bad_alloc &) {
        result = BIO_ALLOC_FAIL;
        for (auto &request : requests) {
            CompleteBdmBatchRequest(&request, BDM_CODE_ERR);
        }
    }
    return result;
}

BResult SubmitBdmBatch(std::vector<BdmBatchRequest> &requests, bool isRead)
{
    BdmBatchContext batch;
    BResult submitRet = SubmitBdmBatchAsync(requests, isRead, batch);
    BResult waitRet = WaitBdmBatch(batch);
    if (batch.semInited) {
        sem_destroy(&batch.sem);
    }
    return submitRet == BIO_OK ? waitRet : submitRet;
}

void AppendDiskToMemoryRequests(const SlicePtr &from, const SlicePtr &to, std::vector<BdmBatchRequest> &requests,
    std::vector<uint32_t> *requestToEntry, uint32_t entryIndex)
{
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();
    auto fromIt = fromAddrs.begin();
    auto toIt = toAddrs.begin();
    uint64_t fromOffset = 0;
    uint64_t toOffset = 0;

    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        uint64_t fromRemain = fromIt->chunkLen - fromOffset;
        uint64_t toRemain = toIt->chunkLen - toOffset;
        uint64_t len = fromRemain < toRemain ? fromRemain : toRemain;
        requests.emplace_back(fromIt->chunkId, fromIt->chunkOffset + fromOffset,
            reinterpret_cast<void *>(toIt->chunkId + toIt->chunkOffset + toOffset), len);
        if (requestToEntry != nullptr) {
            requestToEntry->emplace_back(entryIndex);
        }
        fromOffset += len;
        if (fromOffset == fromIt->chunkLen) {
            fromOffset = 0;
            fromIt++;
        }
        toOffset += len;
        if (toOffset == toIt->chunkLen) {
            toOffset = 0;
            toIt++;
        }
    }
}

void AppendDiskToBufferRequests(const SlicePtr &from, char *to, std::vector<BdmBatchRequest> &requests,
    std::vector<uint32_t> *requestToEntry, uint32_t entryIndex)
{
    auto &fromAddrs = from->GetAddrs();
    uint64_t offset = 0;
    for (auto fromAddr : fromAddrs) {
        requests.emplace_back(fromAddr.chunkId, fromAddr.chunkOffset, reinterpret_cast<void *>(to + offset),
            fromAddr.chunkLen);
        if (requestToEntry != nullptr) {
            requestToEntry->emplace_back(entryIndex);
        }
        offset += fromAddr.chunkLen;
    }
}

bool CanBatchDiskToTemp(const SlicePtr &from, const SlicePtr &to)
{
    return from != nullptr && to != nullptr && from->GetLength() == to->GetLength() &&
        from->GetFlowType() == FLOW_DISK && !from->IsTheSameWith(to);
}

char *AllocBdmBatchTempBuffer(uint64_t len)
{
    return GetBdmBatchTempBufferPool().Alloc(len);
}

uint64_t NormalizeBdmBatchTempSize(uint64_t len)
{
    if (len == 0 || len > UINT64_MAX - BDM_BATCH_TEMP_ALIGN + 1) {
        return 0;
    }
    return ((len + BDM_BATCH_TEMP_ALIGN - 1) / BDM_BATCH_TEMP_ALIGN) * BDM_BATCH_TEMP_ALIGN;
}

void FreeBdmBatchTempBuffer(char *buffer, uint64_t len)
{
    GetBdmBatchTempBufferPool().Free(buffer, len);
}

}

void PrewarmBdmBatchTempBufferPool()
{
    GetBdmBatchTempBufferPool().Prewarm();
}

bool CacheSliceOperator::CanBatchDiskToMemory(const SlicePtr &from, const SlicePtr &to)
{
    return Validate(from, to) && from->GetFlowType() == FLOW_DISK && to->GetFlowType() == FLOW_MEMORY &&
        !from->IsTheSameWith(to);
}

BdmCopyBatchContext::Entry::~Entry()
{
    if (tempBuf != nullptr && tempOwned) {
        FreeBdmBatchTempBuffer(tempBuf, tempLen);
        tempBuf = nullptr;
        tempLen = 0;
    }
    if (sliceRef != nullptr) {
        sliceRef->Release();
        sliceRef = nullptr;
    }
}

struct BdmCopyBatchContext::SubmittedBatch {
    ~SubmittedBatch()
    {
        if (batch.semInited) {
            sem_destroy(&batch.sem);
        }
        if (tempArena != nullptr) {
            FreeBdmBatchTempBuffer(tempArena, tempArenaLen);
            tempArena = nullptr;
            tempArenaLen = 0;
        }
    }

    BdmBatchContext batch;
    std::vector<Entry> entries;
    std::vector<BdmBatchRequest> requests;
    std::vector<uint32_t> requestToEntry;
    std::vector<uint8_t> entryFailed;
    BResult submitRet = BIO_OK;
    BResult waitRet = BIO_OK;
    bool ioCompleted = false;
    bool resultCopied = false;
    char *tempArena = nullptr;
    uint64_t tempArenaLen = 0;
};

BdmCopyBatchContext::BdmCopyBatchContext()
{
    try {
        mEntries.reserve(GetBdmBatchReadWindowKeys());
    } catch (const std::bad_alloc &) {
        LOG_WARN("Reserve bdm batch entries failed.");
    }
}
BdmCopyBatchContext::~BdmCopyBatchContext() = default;

BResult BdmCopyBatchContext::EnqueueDiskToMemory(const SlicePtr &from, const SlicePtr &to, BResult *result,
    WCacheSliceRefPtr &sliceRef)
{
    if (!CacheSliceOperator::CanBatchDiskToMemory(from, to) || sliceRef == nullptr) {
        return BIO_INVALID_PARAM;
    }

    Entry entry(from, to, result, std::move(sliceRef));
    return EnqueueEntry(std::move(entry), from->GetLength());
}

BResult BdmCopyBatchContext::EnqueueDiskToTempThenCopy(const SlicePtr &from, const SlicePtr &to, BResult *result,
    WCacheSliceRefPtr &sliceRef)
{
    if (!CanBatchDiskToTemp(from, to) || sliceRef == nullptr) {
        return BIO_INVALID_PARAM;
    }

    Entry entry(from, to, result, std::move(sliceRef), true);
    return EnqueueEntry(std::move(entry), from->GetLength());
}

BResult BdmCopyBatchContext::EnqueueEntry(Entry &&entry, uint64_t entryLen)
{
    std::vector<std::vector<Entry>> readyWindows;
    auto flushPending = [this, &readyWindows]() {
        if (mEntries.empty()) {
            return;
        }
        readyWindows.emplace_back();
        readyWindows.back().swap(mEntries);
        mPendingBytes = 0;
    };

    try {
        std::lock_guard<std::mutex> lock(mLock);
        uint32_t windowMaxKeys = GetBdmBatchReadWindowKeys();
        uint64_t windowMaxBytes = GetBdmBatchReadWindowBytes();
        if (!mEntries.empty() &&
            (mEntries.size() >= windowMaxKeys || mPendingBytes + entryLen > windowMaxBytes)) {
            flushPending();
        }

        mEntries.emplace_back(std::move(entry));
        if (mEntries.back().result != nullptr) {
            *mEntries.back().result = BDM_BATCH_PENDING;
        }
        mPendingBytes += entryLen;

        if (mEntries.size() >= windowMaxKeys || mPendingBytes >= windowMaxBytes) {
            flushPending();
        }
    } catch (const std::bad_alloc &) {
        return BIO_ALLOC_FAIL;
    }

    for (auto &window : readyWindows) {
        SubmitWindow(window);
    }
    return BIO_OK;
}

void BdmCopyBatchContext::SubmitWindow(std::vector<Entry> &window)
{
    if (window.empty()) {
        return;
    }

    uint32_t pipelineDepth = GetBdmBatchReadPipelineDepth();
    std::unique_ptr<SubmittedBatch> waitBatch;
    try {
        std::lock_guard<std::mutex> lock(mLock);
        mSubmitted.reserve(pipelineDepth);
        if (mSubmitted.size() >= pipelineDepth) {
            waitBatch = std::move(mSubmitted.front());
            mSubmitted.erase(mSubmitted.begin());
        }
    } catch (const std::bad_alloc &) {
        int32_t expected = BIO_OK;
        mResult.compare_exchange_strong(expected, BIO_ALLOC_FAIL);
        MarkPendingEntriesFailed(window, BIO_ALLOC_FAIL);
        return;
    }
    if (waitBatch != nullptr) {
        BResult ret = WaitSubmittedIo(*waitBatch);
        if (ret != BIO_OK) {
            int32_t expected = BIO_OK;
            mResult.compare_exchange_strong(expected, ret);
        }
    }

    auto batch = SubmitEntriesAsync(window);
    if (batch == nullptr) {
        int32_t expected = BIO_OK;
        mResult.compare_exchange_strong(expected, BIO_ALLOC_FAIL);
        MarkPendingEntriesFailed(window, BIO_ALLOC_FAIL);
        if (waitBatch != nullptr) {
            WaitAndRecord(*waitBatch);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mLock);
        mSubmitted.emplace_back(std::move(batch));
    }

    if (waitBatch != nullptr) {
        WaitAndRecord(*waitBatch);
    }
}

void BdmCopyBatchContext::MarkPendingEntriesFailed(std::vector<Entry> &entryList, BResult result)
{
    for (auto &entry : entryList) {
        if (entry.result != nullptr && *entry.result == BDM_BATCH_PENDING) {
            *entry.result = result;
        }
    }
}

std::unique_ptr<BdmCopyBatchContext::SubmittedBatch> BdmCopyBatchContext::SubmitEntriesAsync(
    std::vector<Entry> &entries)
{
    std::unique_ptr<SubmittedBatch> submitted;
    try {
        submitted.reset(new SubmittedBatch());
        submitted->entries.swap(entries);
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    auto &submittedEntries = submitted->entries;
    auto &requests = submitted->requests;
    auto &requestToEntry = submitted->requestToEntry;
    if (submittedEntries.empty()) {
        return submitted;
    }

    uint64_t requestCount = 0;
    for (const auto &entry : submittedEntries) {
        requestCount += entry.from->GetAddrs().size() + entry.to->GetAddrs().size();
    }

    try {
        submitted->entryFailed.assign(submittedEntries.size(), 0);
        requests.reserve(requestCount);
        requestToEntry.reserve(requestCount);
        uint64_t tempArenaOffset = 0;
        for (const auto &entry : submittedEntries) {
            if (!entry.useTemp || !CanBatchDiskToTemp(entry.from, entry.to)) {
                continue;
            }
            uint64_t normalizedLen = NormalizeBdmBatchTempSize(entry.to->GetLength());
            if (normalizedLen == 0 || tempArenaOffset > UINT64_MAX - normalizedLen) {
                tempArenaOffset = 0;
                break;
            }
            tempArenaOffset += normalizedLen;
        }
        if (tempArenaOffset != 0) {
            submitted->tempArena = AllocBdmBatchTempBuffer(tempArenaOffset);
            if (submitted->tempArena != nullptr) {
                submitted->tempArenaLen = tempArenaOffset;
            }
        }
        tempArenaOffset = 0;
        for (uint32_t i = 0; i < submittedEntries.size(); i++) {
            if (submittedEntries[i].useTemp) {
                if (!CanBatchDiskToTemp(submittedEntries[i].from, submittedEntries[i].to)) {
                    if (submittedEntries[i].result != nullptr) {
                        *submittedEntries[i].result = BIO_INVALID_PARAM;
                    }
                    submitted->submitRet = BIO_INVALID_PARAM;
                    continue;
                }
                submittedEntries[i].tempLen = submittedEntries[i].to->GetLength();
                if (submitted->tempArena != nullptr) {
                    uint64_t normalizedLen = NormalizeBdmBatchTempSize(submittedEntries[i].tempLen);
                    submittedEntries[i].tempBuf = submitted->tempArena + tempArenaOffset;
                    submittedEntries[i].tempOwned = false;
                    tempArenaOffset += normalizedLen;
                } else {
                    submittedEntries[i].tempBuf = AllocBdmBatchTempBuffer(submittedEntries[i].tempLen);
                }
                if (submittedEntries[i].tempBuf == nullptr) {
                    if (submittedEntries[i].result != nullptr) {
                        *submittedEntries[i].result = BIO_ALLOC_FAIL;
                    }
                    submitted->submitRet = BIO_ALLOC_FAIL;
                    continue;
                }
                AppendDiskToBufferRequests(submittedEntries[i].from, submittedEntries[i].tempBuf, requests,
                    &requestToEntry, i);
                continue;
            }
            if (!CacheSliceOperator::CanBatchDiskToMemory(submittedEntries[i].from, submittedEntries[i].to)) {
                if (submittedEntries[i].result != nullptr) {
                    *submittedEntries[i].result = BIO_INVALID_PARAM;
                }
                submitted->submitRet = BIO_INVALID_PARAM;
                continue;
            }
            AppendDiskToMemoryRequests(submittedEntries[i].from, submittedEntries[i].to, requests, &requestToEntry, i);
        }
    } catch (const std::bad_alloc &) {
        submitted->submitRet = BIO_ALLOC_FAIL;
        requests.clear();
        requestToEntry.clear();
        return submitted;
    }

    if (requests.empty()) {
        return submitted;
    }

    BIO_TRACE_START(BDM_TRACE_READ_BATCH_SUBMIT);
    BResult ret = SubmitBdmBatchAsync(requests, true, submitted->batch);
    BIO_TRACE_END(BDM_TRACE_READ_BATCH_SUBMIT, ret);
    if (ret != BIO_OK) {
        submitted->submitRet = ret;
    }
    return submitted;
}

BResult BdmCopyBatchContext::WaitSubmittedIo(SubmittedBatch &batch)
{
    if (batch.ioCompleted) {
        return batch.waitRet;
    }

    BResult ret = batch.submitRet;
    if (!batch.requests.empty()) {
        BIO_TRACE_START(BDM_TRACE_READ_BATCH_WAIT);
        BResult waitRet = WaitBdmBatch(batch.batch);
        BIO_TRACE_END(BDM_TRACE_READ_BATCH_WAIT, waitRet);
        if (ret == BIO_OK) {
            ret = waitRet;
        }
    }

    BIO_TRACE_START(BDM_TRACE_READ_BATCH_RESULT_SCAN);
    bool hasRequestFailure = false;
    for (uint32_t i = 0; i < batch.requests.size(); i++) {
        if (batch.requests[i].result.load(std::memory_order_relaxed) == BDM_CODE_OK) {
            continue;
        }
        hasRequestFailure = true;
        ret = BIO_DISK_IOERR;
        uint32_t entryIndex = batch.requestToEntry[i];
        if (entryIndex < batch.entryFailed.size()) {
            batch.entryFailed[entryIndex] = 1;
        }
        if (entryIndex < batch.entries.size() && batch.entries[entryIndex].result != nullptr) {
            *batch.entries[entryIndex].result = BIO_DISK_IOERR;
        }
    }
    if (ret != BIO_OK && !hasRequestFailure) {
        for (auto &entry : batch.entries) {
            if (entry.result != nullptr && (*entry.result == BDM_BATCH_PENDING || *entry.result == BIO_OK)) {
                *entry.result = ret;
            }
        }
    }

    BIO_TRACE_END(BDM_TRACE_READ_BATCH_RESULT_SCAN, ret);
    batch.waitRet = ret;
    batch.ioCompleted = true;
    return ret;
}

BResult BdmCopyBatchContext::CopySubmittedResult(SubmittedBatch &batch)
{
    if (batch.resultCopied) {
        return batch.waitRet;
    }

    BResult ret = WaitSubmittedIo(batch);
    CacheSliceOperator sliceOperator;
    for (auto &entry : batch.entries) {
        if (entry.result == nullptr || *entry.result != BDM_BATCH_PENDING) {
            continue;
        }
        if (entry.tempBuf != nullptr) {
            uint32_t entryIndex = static_cast<uint32_t>(&entry - batch.entries.data());
            if (entryIndex < batch.entryFailed.size() && batch.entryFailed[entryIndex] != 0) {
                continue;
            }
            BIO_TRACE_START(BDM_TRACE_READ_BATCH_SCRATCH_COPY);
            BResult copyRet = sliceOperator.Copy(entry.tempBuf, entry.to);
            BIO_TRACE_END(BDM_TRACE_READ_BATCH_SCRATCH_COPY, copyRet);
            if (copyRet != BIO_OK) {
                *entry.result = copyRet;
                ret = copyRet;
                continue;
            }
        }
        if (entry.result != nullptr && *entry.result == BDM_BATCH_PENDING) {
            *entry.result = BIO_OK;
        }
    }
    batch.waitRet = ret;
    batch.resultCopied = true;
    return ret;
}

BResult BdmCopyBatchContext::WaitSubmittedBatch(SubmittedBatch &batch)
{
    BResult ret = WaitSubmittedIo(batch);
    BResult copyRet = CopySubmittedResult(batch);
    return ret == BIO_OK ? copyRet : ret;
}

void BdmCopyBatchContext::WaitAndRecord(SubmittedBatch &batch)
{
    BResult ret = CopySubmittedResult(batch);
    if (ret != BIO_OK) {
        int32_t expected = BIO_OK;
        mResult.compare_exchange_strong(expected, ret);
    }
}

BResult BdmCopyBatchContext::Submit()
{
    std::vector<Entry> entries;
    std::vector<std::unique_ptr<SubmittedBatch>> submitted;
    {
        std::lock_guard<std::mutex> lock(mLock);
        entries.swap(mEntries);
        mPendingBytes = 0;
    }

    if (!entries.empty()) {
        SubmitWindow(entries);
    }

    {
        std::lock_guard<std::mutex> lock(mLock);
        submitted.swap(mSubmitted);
    }
    for (auto &batch : submitted) {
        WaitAndRecord(*batch);
    }
    return mResult.load();
}

bool BdmCopyBatchContext::Empty() const
{
    std::lock_guard<std::mutex> lock(mLock);
    return mEntries.empty() && mSubmitted.empty();
}

BResult CacheSliceOperator::Copy(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(Validate(from, to), BIO_INVALID_PARAM);

    // have some address, don't need copy.
    if (from->IsTheSameWith(to)) {
        return BIO_OK;
    }

    if (from->GetFlowType() == FLOW_DISK) {
        if (to->GetFlowType() == FLOW_DISK) {
            // copy from disk to disk.
            return CopyFromDiskToDisk(from, to);
        }
        // copy from disk to memory.
        return CopyFromDiskToMemory(from, to);
    }

    if (to->GetFlowType() == FLOW_DISK) {
        // copy from memory to disk.
        return CopyFromMemoryToDisk(from, to);
    }
    // copy from memory to memory.
    return CopyFromMemoryToMemory(from, to);
}

BResult CacheSliceOperator::Copy(const char *from, const SlicePtr &to)
{
    ChkTrueNot(Validate(to), BIO_INVALID_PARAM);
    ChkTrueNot(from != nullptr, BIO_INVALID_PARAM);
    BResult ret = BIO_INNER_ERR;

    if (to->GetFlowType() == FLOW_MEMORY) {
        auto &toAddrs = to->GetAddrs();
        uint64_t offset = 0;
        for (auto toAddr : toAddrs) {
            BIO_TP_START(SLICE_OPERATOR_FLOW_MEMORY, &ret, BIO_ERR);
            ret = memcpy_s(reinterpret_cast<void *>(toAddr.chunkId + toAddr.chunkOffset), toAddr.chunkLen,
                reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen);
            BIO_TP_END;
            ChkTrue(ret == BIO_OK, ret, "Failed to copy data, length:" << toAddr.chunkLen);
            offset += toAddr.chunkLen;
        }
        return BIO_OK;
    } else {
        auto &toAddrs = to->GetAddrs();
        uint64_t offset = 0;
        std::vector<BdmBatchRequest> requests;
        requests.reserve(toAddrs.size());
        for (auto toAddr : toAddrs) {
            requests.emplace_back(toAddr.chunkId, toAddr.chunkOffset,
                reinterpret_cast<void *>(const_cast<char *>(from + offset)), toAddr.chunkLen);
            offset += toAddr.chunkLen;
        }
        BIO_TRACE_START(BDM_TRACE_WRITE_SYNC);
        ret = SubmitBdmBatch(requests, false);
        BIO_TRACE_END(BDM_TRACE_WRITE_SYNC, ret);
        ChkTrue(ret == BIO_OK, ret, "Failed to copy length:" << to->GetLength());
        return BIO_OK;
    }
}

BResult CacheSliceOperator::Copy(const char *from, uint64_t start, uint32_t len, const SlicePtr &to)
{
    ChkTrueNot(Validate(to), BIO_INVALID_PARAM);
    ChkTrueNot(from != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(to->GetFlowType() == FLOW_MEMORY, BIO_INVALID_PARAM);

    auto &toAddrs = to->GetAddrs();
    uint64_t offset1 = start;
    uint64_t offset2 = 0;
    uint32_t len1 = len;
    uint64_t offset = 0;
    for (auto toAddr : toAddrs) {
        if (offset + toAddr.chunkLen <= offset1) {
            offset += toAddr.chunkLen;
            continue;
        }
        uint64_t boff = offset1 - offset;
        uint64_t blen = toAddr.chunkLen - boff;
        blen = (len1 > blen) ? blen : len1;
        if (blen == 0) {
            break;
        }
        len1 -= blen;
        int32_t ret = BIO_INNER_ERR;
        BIO_TP_START(SLICE_OPERATOR_4_FLOW_MEMORY, &ret, BIO_ERR);
        ret = memcpy_s(reinterpret_cast<void *>(toAddr.chunkId + toAddr.chunkOffset + boff), blen,
            reinterpret_cast<void *>(const_cast<char *>(from + offset2)), blen);
        BIO_TP_END;
        ChkTrue(ret == BIO_OK, ret, "Failed to copy data, length:" << blen << ".");
        offset += toAddr.chunkLen;
        offset1 = offset;
        offset2 += blen;
    }
    return BIO_OK;
}

BResult CacheSliceOperator::Copy(const SlicePtr &from, char *to, uint32_t toLen)
{
    ChkTrueNot(Validate(from), BIO_INVALID_PARAM);
    ChkTrueNot(to != nullptr, BIO_INVALID_PARAM);
    ChkTrueNot(toLen != 0, BIO_INVALID_PARAM);
    BResult ret = BIO_INNER_ERR;
    uint64_t cpyLength = toLen;

    if (from->GetFlowType() == FLOW_MEMORY) {
        auto &fromAddrs = from->GetAddrs();
        uint64_t offset = 0;
        for (auto fromAddr : fromAddrs) {
            BIO_TP_START(SLICE_OPERATOR_2_FLOW_MEMORY, &ret, BIO_ERR);
            ret = memcpy_s(reinterpret_cast<void *>(const_cast<char *>(to + offset)), cpyLength,
                reinterpret_cast<void *>(fromAddr.chunkId + fromAddr.chunkOffset), fromAddr.chunkLen);
            BIO_TP_END;
            ChkTrue(ret == BIO_OK, ret, "Failed to copy data, length:" << fromAddr.chunkLen << ".");
            offset += fromAddr.chunkLen;
            cpyLength -= fromAddr.chunkLen;
        }
        return BIO_OK;
    } else {
        auto &fromAddrs = from->GetAddrs();
        uint64_t offset = 0;
        std::vector<BdmBatchRequest> requests;
        requests.reserve(fromAddrs.size());
        for (auto fromAddr : fromAddrs) {
            LOG_TRACE("Copy data from disk:" << " from off:" << fromAddr.chunkOffset << ", to off:" << offset);
            requests.emplace_back(fromAddr.chunkId, fromAddr.chunkOffset,
                reinterpret_cast<void *>(to + offset), fromAddr.chunkLen);
            offset += fromAddr.chunkLen;
        }
        BIO_TRACE_START(BDM_TRACE_READ_SYNC);
        ret = SubmitBdmBatch(requests, true);
        BIO_TRACE_END(BDM_TRACE_READ_SYNC, ret);
        ChkTrue(ret == BIO_OK, ret, "Failed to copy data from disk to memory by length:" << from->GetLength() << ".");
        return BIO_OK;
    }
}

BResult CacheSliceOperator::GetSliceFromSliceIO(SlicePtr &partialSlice, const SlicePtr &wholeSlice, uint64_t offset,
    uint64_t length)
{
    ChkTrueNot(Validate(wholeSlice), BIO_INVALID_PARAM);
    std::vector<FlowAddr> flowAddr;
    std::vector<FlowAddr> addrVec = wholeSlice->GetAddrs();
    uint64_t totalLength = length;
    for (auto addr : addrVec) {
        if (offset >= addr.chunkLen) {
            offset -= addr.chunkLen;
            continue;
        }

        uint64_t size = addr.chunkLen;
        uint32_t chunkOffset = addr.chunkOffset;
        uint32_t chunkLen = addr.chunkLen;
        if (offset != 0) {
            chunkOffset += offset;
            chunkLen = length > chunkLen - offset ? chunkLen - offset : length;
            offset = 0;
        } else {
            chunkLen = length > chunkLen ? chunkLen : length;
        }
        flowAddr.emplace_back(addr.chunkId, chunkOffset, chunkLen);
        length -= chunkLen;
        if (length == 0) {
            break;
        }
    }
    partialSlice = MakeRef<Slice>(totalLength, flowAddr, wholeSlice->GetFlowType());
    if (UNLIKELY(partialSlice == nullptr)) {
        LOG_ERROR("Alloc slice memory for read cache failed.");
        return BIO_ERR;
    }
    return BIO_OK;
}

bool CacheSliceOperator::Validate(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);

    ChkTrue(from->GetLength() == to->GetLength(), false,
        "Failed to validate slices, src slice length:" << from->GetLength() << " dst slice length:" << to->GetLength());

    return true;
}

bool CacheSliceOperator::Validate(const SlicePtr &slice)
{
    ChkTrueNot(slice != nullptr, false);
    return true;
}

BResult CacheSliceOperator::CopyFromDiskToDisk(const SlicePtr &from, const SlicePtr &to)
{
    LOG_ERROR("don't support copy from disk to disk.");
    return BIO_ERR;
}

BResult CacheSliceOperator::CopyFromDiskToMemory(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);
    BResult ret = BIO_INNER_ERR;
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();
    auto fromIt = fromAddrs.begin();
    auto toIt = toAddrs.begin();
    uint64_t fromOffset = 0;
    uint64_t toOffset = 0;
    std::vector<BdmBatchRequest> requests;
    requests.reserve(fromAddrs.size() + toAddrs.size());

    uint64_t len;
    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        requests.emplace_back(fromIt->chunkId, fromIt->chunkOffset + fromOffset,
            reinterpret_cast<void *>(toIt->chunkId + toIt->chunkOffset + toOffset), len);
        fromOffset += len;
        if (fromOffset == fromIt->chunkLen) {
            fromOffset = 0;
            fromIt++;
        }
        toOffset += len;
        if (toOffset == toIt->chunkLen) {
            toOffset = 0;
            toIt++;
        }
    }
    BIO_TRACE_START(BDM_TRACE_READ_SYNC);
    ret = SubmitBdmBatch(requests, true);
    BIO_TRACE_END(BDM_TRACE_READ_SYNC, ret);
    ChkTrue(ret == BIO_OK, ret, "Failed to copy data from disk to memory by length:" << from->GetLength() << ".");
    return BIO_OK;
}

BResult CacheSliceOperator::CopyFromMemoryToDisk(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();

    auto fromIt = fromAddrs.begin();
    auto toIt = toAddrs.begin();

    uint64_t fromOffset = 0;
    uint64_t toOffset = 0;
    std::vector<BdmBatchRequest> requests;
    requests.reserve(fromAddrs.size() + toAddrs.size());

    uint64_t len;
    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        requests.emplace_back(toIt->chunkId, toIt->chunkOffset + toOffset,
            reinterpret_cast<void *>(fromIt->chunkId + fromIt->chunkOffset + fromOffset), len);
        LOG_TRACE("Copy data from disk chunk:" << fromIt->chunkOffset << ", from off:" << fromOffset << ", to off:" <<
            toOffset << ", len:" << len << ".");
        fromOffset += len;
        if (fromOffset == fromIt->chunkLen) {
            fromOffset = 0;
            fromIt++;
        }
        toOffset += len;
        if (toOffset == toIt->chunkLen) {
            toOffset = 0;
            toIt++;
        }
        LOG_TRACE("next from off:" << fromOffset << ", to off:" << toOffset);
    }
    BIO_TRACE_START(BDM_TRACE_WRITE_SYNC);
    auto ret = SubmitBdmBatch(requests, false);
    BIO_TRACE_END(BDM_TRACE_WRITE_SYNC, ret);
    ChkTrue(ret == BIO_OK, ret, "Failed to copy data from memory to disk by length:" << to->GetLength() << ".");
    return BIO_OK;
}

BResult CacheSliceOperator::CopyFromMemoryToMemory(const SlicePtr &from, const SlicePtr &to)
{
    ChkTrueNot(from != nullptr, false);
    ChkTrueNot(to != nullptr, false);
    BResult ret = BIO_INNER_ERR;
    auto &fromAddrs = from->GetAddrs();
    auto &toAddrs = to->GetAddrs();
    auto fromIt = fromAddrs.begin();
    auto toIt = toAddrs.begin();
    uint64_t fromOffset = 0;
    uint64_t toOffset = 0;
    uint64_t len = 0;

    while (fromIt != fromAddrs.end() && toIt != toAddrs.end()) {
        len = MinLen(fromIt->chunkLen - fromOffset, toIt->chunkLen - toOffset);
        BIO_TP_START(SLICE_COPY_MEMORY2MEMORY_ERR, &ret, BIO_ERR);
        BIO_TRACE_START(WCACHE_TRACE_GET_MEMORY_COPY);
        ret = memcpy_s(reinterpret_cast<void *>(toIt->chunkId + toIt->chunkOffset + toOffset), len,
            reinterpret_cast<void *>(fromIt->chunkId + fromIt->chunkOffset + fromOffset), len);
        BIO_TRACE_END(WCACHE_TRACE_GET_MEMORY_COPY, ret);
        BIO_TP_END;
        ChkTrue(ret == BIO_OK, ret, "Failed to copy data, length:" << len << ".");
        fromOffset += len;
        if (fromOffset == fromIt->chunkLen) {
            fromOffset = 0;
            fromIt++;
        }
        toOffset += len;
        if (toOffset == toIt->chunkLen) {
            toOffset = 0;
            toIt++;
        }
    }

    return BIO_OK;
}

inline uint64_t CacheSliceOperator::MinLen(uint64_t from, uint64_t to)
{
    return (from < to) ? from : to;
}
}
}
