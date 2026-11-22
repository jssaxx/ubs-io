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

#ifndef BOOSTIO_FILE_EVICTION_POLICY_H
#define BOOSTIO_FILE_EVICTION_POLICY_H

#include <cstddef>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "bio_err.h"
#include "bio_file_eviction_policy.h"

namespace ock {
namespace bio {
struct FileRecord {
    std::string leaf;
    uint64_t sizeBytes;
};

struct FileEvictionNode {
    FileEvictionNode() noexcept = default;
    FileEvictionNode(const FileEvictionNode &) = delete;
    FileEvictionNode &operator=(const FileEvictionNode &) = delete;

    const std::string *key{ nullptr };
    FileEvictionNode *previous{ nullptr };
    FileEvictionNode *next{ nullptr };
};

struct IndexedFile {
    explicit IndexedFile(FileRecord file) noexcept : record(std::move(file)) {}
    FileRecord record;
    FileEvictionNode eviction;
    std::atomic<uint32_t> references{0};
};

using FileIndex = std::unordered_map<std::string, IndexedFile>;

class FileReference {
public:
    FileReference() noexcept = default;
    FileReference(const FileReference &) = delete;
    FileReference &operator=(const FileReference &) = delete;
    ~FileReference() noexcept
    {
        Reset();
    }

    bool IsValid() const noexcept
    {
        return mReferences != nullptr;
    }

    void Acquire(std::atomic<uint32_t> &references) noexcept
    {
        mReferences = &references;
        mReferences->fetch_add(1, std::memory_order_relaxed);
    }

    void Reset() noexcept
    {
        if (mReferences != nullptr) {
            mReferences->fetch_sub(1, std::memory_order_release);
            mReferences = nullptr;
        }
    }

private:
    std::atomic<uint32_t> *mReferences{nullptr};
};

struct EvictionCandidate {
    std::string key;
    FileRecord record;
};

class FileEvictionPolicy {
public:
    FileEvictionPolicy() noexcept = default;
    FileEvictionPolicy(const FileEvictionPolicy &) = delete;
    FileEvictionPolicy &operator=(const FileEvictionPolicy &) = delete;
    virtual ~FileEvictionPolicy() noexcept = default;

    virtual BResult Recover(FileIndex &files) noexcept = 0;

    virtual BResult Lookup(const std::string &key, FileRecord &file, FileReference &reference) noexcept = 0;

    virtual size_t Size() const noexcept = 0;

    virtual BResult ForEachFile(
        const std::function<BResult(const std::string &, const FileRecord &)> &visit) const noexcept = 0;

    virtual BResult Insert(const std::string &key) noexcept = 0;

    virtual BResult ColdInsert(const EvictionCandidate &candidate) noexcept = 0;

    virtual BResult CommitWrite(const std::string &key, FileRecord &file,
        const std::function<BResult()> &publish) noexcept = 0;

    virtual void RollbackWrite(const std::string &key) noexcept = 0;

    virtual BResult TakeCandidates(size_t limit,
        const std::function<bool(const FileRecord &)> &keepSelecting,
        std::vector<EvictionCandidate> &candidates) noexcept = 0;

    virtual BResult Remove(const std::string &key, EvictionCandidate &candidate) noexcept = 0;
};

std::unique_ptr<FileEvictionPolicy> CreateFileEvictionPolicy(FileEvictionPolicyType type) noexcept;
}
}

#endif // BOOSTIO_FILE_EVICTION_POLICY_H
