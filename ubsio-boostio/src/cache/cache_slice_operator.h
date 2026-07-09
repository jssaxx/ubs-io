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

#ifndef BOOSTIO_CACHE_SLICE_OPERATOR_H
#define BOOSTIO_CACHE_SLICE_OPERATOR_H

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include "cache_slice.h"
#include "slice_operator.h"

namespace ock {
namespace bio {
void PrewarmBdmBatchTempBufferPool();

class BdmCopyBatchContext {
public:
    BdmCopyBatchContext();
    ~BdmCopyBatchContext();
    BResult EnqueueDiskToMemory(const SlicePtr &from, const SlicePtr &to, BResult *result,
        WCacheSliceRefPtr &sliceRef);
    BResult EnqueueDiskToTempThenCopy(const SlicePtr &from, const SlicePtr &to, BResult *result,
        WCacheSliceRefPtr &sliceRef);
    BResult Submit();
    bool Empty() const;

private:
    struct SubmittedBatch;

    struct Entry {
        Entry(const SlicePtr &src, const SlicePtr &dst, BResult *keyResult, WCacheSliceRefPtr &&lease)
            : from(src), to(dst), result(keyResult), sliceRef(std::move(lease))
        {
        }
        Entry(const SlicePtr &src, const SlicePtr &dst, BResult *keyResult, WCacheSliceRefPtr &&lease,
            bool tempRead)
            : from(src), to(dst), result(keyResult), sliceRef(std::move(lease)), useTemp(tempRead)
        {
        }
        Entry(const Entry &) = delete;
        Entry &operator=(const Entry &) = delete;
        Entry(Entry &&other) noexcept
            : from(std::move(other.from)), to(std::move(other.to)), result(other.result),
              sliceRef(std::move(other.sliceRef)), useTemp(other.useTemp), tempBuf(other.tempBuf),
              tempLen(other.tempLen), tempOwned(other.tempOwned)
        {
            other.result = nullptr;
            other.useTemp = false;
            other.tempBuf = nullptr;
            other.tempLen = 0;
            other.tempOwned = true;
        }
        Entry &operator=(Entry &&) = delete;
        ~Entry();

        SlicePtr from;
        SlicePtr to;
        BResult *result;
        WCacheSliceRefPtr sliceRef;
        bool useTemp = false;
        char *tempBuf = nullptr;
        uint64_t tempLen = 0;
        bool tempOwned = true;
    };

    BResult EnqueueEntry(Entry &&entry, uint64_t entryLen);
    void SubmitWindow(std::vector<Entry> &window);
    std::unique_ptr<SubmittedBatch> SubmitEntriesAsync(std::vector<Entry> &entries);
    static void MarkPendingEntriesFailed(std::vector<Entry> &entryList, BResult result);
    void WaitAndRecord(SubmittedBatch &batch);
    BResult WaitSubmittedIo(SubmittedBatch &batch);
    BResult CopySubmittedResult(SubmittedBatch &batch);
    BResult WaitSubmittedBatch(SubmittedBatch &batch);

    mutable std::mutex mLock;
    std::vector<Entry> mEntries;
    std::vector<std::unique_ptr<SubmittedBatch>> mSubmitted;
    uint64_t mPendingBytes = 0;
    std::atomic<int32_t> mResult { BIO_OK };
};

class CacheSliceOperator : public SliceOperator {
public:
    BResult Copy(const SlicePtr &from, const SlicePtr &to) override;
    BResult Copy(const char *from, const SlicePtr &to) override;
    BResult Copy(const SlicePtr &from, char *to, uint32_t toLen) override;
    BResult Copy(const char *from, uint64_t start, uint32_t len, const SlicePtr &to) override;
    BResult GetSliceFromSliceIO(SlicePtr &partialSlice, const SlicePtr &wholeSlice, uint64_t offset,
        uint64_t length) override;
    static bool CanBatchDiskToMemory(const SlicePtr &from, const SlicePtr &to);

private:
    static bool Validate(const SlicePtr &from, const SlicePtr &to);
    static bool Validate(const SlicePtr &slice);
    static BResult CopyFromDiskToDisk(const SlicePtr &from, const SlicePtr &to);
    static BResult CopyFromDiskToMemory(const SlicePtr &from, const SlicePtr &to);
    static BResult CopyFromMemoryToDisk(const SlicePtr &from, const SlicePtr &to);
    static BResult CopyFromMemoryToMemory(const SlicePtr &from, const SlicePtr &to);
    static uint64_t MinLen(uint64_t from, uint64_t to);
};
}
}


#endif // BOOSTIO_CACHE_SLICE_OPERATOR_H
