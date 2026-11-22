/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <algorithm>
#include <mutex>
#include <tuple>
#include "file_eviction_policy.h"

/*
 * LRU 队列：mHead 为循环链表哨兵，头部为热端，尾部为冷端。
 * mHead.next 指向头部，mHead.previous 指向尾部。
 *
 *  mHead <-> [head / hot] <-> ... <-> [tail / cold] <-> mHead
 *
 * LinkHead：插入头部，供 Insert、Promote 和 Recover 挂接节点。
 * LinkTail：插入尾部，供 ColdInsert 恢复候选。
 * TakeCandidates：从尾部向头部选择无引用的候选。
 */

namespace ock {
namespace bio {
namespace {
class LruFileEvictionPolicy final : public FileEvictionPolicy {
public:
    LruFileEvictionPolicy() noexcept
    {
        mHead.previous = &mHead;
        mHead.next = &mHead;
    }

    BResult Recover(FileIndex &files) noexcept override
    {
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        for (const auto &entry : mIndex) {
            if (UNLIKELY(entry.second.references.load(std::memory_order_acquire) != 0)) {
                return BIO_INNER_RETRY;
            }
        }
        mIndex.swap(files);
        mHead.previous = &mHead;
        mHead.next = &mHead;
        for (auto &entry : mIndex) {
            auto &node = entry.second.eviction;
            node.key = &entry.first;
            LinkHead(node);
        }
        return BIO_OK;
    }

    BResult Lookup(const std::string &key, FileRecord &file, FileReference &reference) noexcept override
    {
        if (UNLIKELY(reference.IsValid())) {
            return BIO_INVALID_PARAM;
        }
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        auto found = mIndex.find(key);
        if (found == mIndex.end() || found->second.record.sizeBytes == 0) {
            return BIO_NOT_EXISTS;
        }
        if (UNLIKELY(found->second.references.load(std::memory_order_relaxed) == UINT32_MAX)) {
            return BIO_INNER_RETRY;
        }
        file = found->second.record;
        reference.Acquire(found->second.references);
        Promote(found->second.eviction);
        return BIO_OK;
    }

    size_t Size() const noexcept override
    {
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        return mIndex.size();
    }

    BResult ForEachFile(
        const std::function<BResult(const std::string &, const FileRecord &)> &visit) const noexcept override
    {
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        for (const auto &entry : mIndex) {
            if (entry.second.record.sizeBytes != 0) {
                BResult ret = visit(entry.first, entry.second.record);
                if (UNLIKELY(ret != BIO_OK)) {
                    return ret;
                }
            }
        }
        return BIO_OK;
    }

    BResult Insert(const std::string &key) noexcept override
    {
        if (UNLIKELY(key.empty())) {
            return BIO_INVALID_PARAM;
        }
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        auto found = mIndex.find(key);
        if (found != mIndex.end()) {
            Promote(found->second.eviction);
            return BIO_ALREADY_EXISTS;
        }
        auto inserted = mIndex.emplace(std::piecewise_construct, std::forward_as_tuple(key),
            std::forward_as_tuple(FileRecord{"", 0}));
        auto &entry = inserted.first->second;
        entry.eviction.key = &inserted.first->first;
        entry.references.store(1, std::memory_order_relaxed);
        LinkHead(entry.eviction);
        return BIO_OK;
    }

    BResult ColdInsert(const EvictionCandidate &candidate) noexcept override
    {
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        auto inserted = mIndex.emplace(std::piecewise_construct, std::forward_as_tuple(candidate.key),
            std::forward_as_tuple(candidate.record));
        if (UNLIKELY(!inserted.second)) {
            return BIO_INNER_ERR;
        }
        auto &node = inserted.first->second.eviction;
        node.key = &inserted.first->first;
        LinkTail(node);
        return BIO_OK;
    }

    BResult CommitWrite(const std::string &key, FileRecord &file,
        const std::function<BResult()> &publish) noexcept override
    {
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        auto found = mIndex.find(key);
        if (UNLIKELY(found == mIndex.end() || found->second.record.sizeBytes != 0 ||
            found->second.references.load(std::memory_order_relaxed) == 0)) {
            return BIO_NOT_READY;
        }
        if (UNLIKELY(file.sizeBytes == 0)) {
            return BIO_INVALID_PARAM;
        }
        BResult ret = publish();
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
        auto &entry = found->second;
        std::swap(entry.record, file);
        entry.references.fetch_sub(1, std::memory_order_release);
        return BIO_OK;
    }

    void RollbackWrite(const std::string &key) noexcept override
    {
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        auto found = mIndex.find(key);
        if (found == mIndex.end() || found->second.record.sizeBytes != 0) {
            return;
        }
        Unlink(found->second.eviction);
        mIndex.erase(found);
    }

    BResult TakeCandidates(size_t limit,
        const std::function<bool(const FileRecord &)> &keepSelecting,
        std::vector<EvictionCandidate> &candidates) noexcept override
    {
        std::vector<EvictionCandidate> result;
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        result.reserve(std::min(limit, mIndex.size()));
        // Prepare the complete batch before removing its entries from the index and queue.
        for (auto *node = mHead.previous; node != &mHead && result.size() < limit; node = node->previous) {
            auto found = mIndex.find(*node->key);
            if (UNLIKELY(found->second.references.load(std::memory_order_acquire) != 0)) {
                continue;
            }
            result.push_back({found->first, found->second.record});
            if (!keepSelecting(found->second.record)) {
                break;
            }
        }
        for (const auto &candidate : result) {
            auto found = mIndex.find(candidate.key);
            Unlink(found->second.eviction);
            mIndex.erase(found);
        }
        candidates.swap(result);
        return BIO_OK;
    }

    BResult Remove(const std::string &key, EvictionCandidate &candidate) noexcept override
    {
        std::lock_guard<std::mutex> metadata(mMetadataMutex);
        auto found = mIndex.find(key);
        if (found == mIndex.end()) {
            return BIO_NOT_EXISTS;
        }
        if (UNLIKELY(found->second.references.load(std::memory_order_acquire) != 0)) {
            return BIO_INNER_RETRY;
        }
        candidate = {found->first, found->second.record};
        Unlink(found->second.eviction);
        mIndex.erase(found);
        return BIO_OK;
    }

private:
    void Unlink(FileEvictionNode &node) noexcept
    {
        if (node.previous != nullptr) {
            node.previous->next = node.next;
            node.next->previous = node.previous;
            node.previous = nullptr;
            node.next = nullptr;
        }
    }

    void Promote(FileEvictionNode &node) noexcept
    {
        Unlink(node);
        LinkHead(node);
    }

    void LinkTail(FileEvictionNode &node) noexcept
    {
        node.previous = mHead.previous;
        node.next = &mHead;
        mHead.previous->next = &node;
        mHead.previous = &node;
    }

    void LinkHead(FileEvictionNode &node) noexcept
    {
        node.previous = &mHead;
        node.next = mHead.next;
        mHead.next->previous = &node;
        mHead.next = &node;
    }

    // No borrowed node escapes the metadata lock.
    mutable std::mutex mMetadataMutex;
    FileIndex mIndex;
    FileEvictionNode mHead;
};
}

std::unique_ptr<FileEvictionPolicy> CreateFileEvictionPolicy(FileEvictionPolicyType type) noexcept
{
    if (type != FileEvictionPolicyType::LRU) {
        return nullptr;
    }
    return std::unique_ptr<FileEvictionPolicy>(new LruFileEvictionPolicy());
}
}
}
