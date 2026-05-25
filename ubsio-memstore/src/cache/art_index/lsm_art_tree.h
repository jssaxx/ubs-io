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

#ifndef MMSCORE_LSM_ART_TREE_H
#define MMSCORE_LSM_ART_TREE_H

#include <string>
#include <cstring>
#include <vector>
#include <atomic>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include "art.h"
#include "art_range.h"
#include "mms_lock.h"
#include "mms_ref.h"
#include "mms_types.h"

namespace ock {
namespace mms {

static constexpr uint16_t FLUSH_WATERMARK = 1024;
static constexpr uint16_t ELASTIC_CAPACITY = 2048;  // 防止vector触发扩容
static constexpr uint8_t BUFF_SIZE_FACTOR = 2;

struct KVPair {
    const unsigned char *key;
    uint32_t keyLen;
    void *value;
    bool isDelete;
    std::string ownedKey;

    KVPair(const unsigned char *key, uint32_t keyLen, void *val)
        : key(nullptr), keyLen(keyLen), value(val), isDelete(false),
          ownedKey(reinterpret_cast<const char *>(key), keyLen)
    {
        this->key = reinterpret_cast<const unsigned char *>(ownedKey.data());
    }

    KVPair(const unsigned char *key, uint32_t keyLen)
        : key(nullptr), keyLen(keyLen), value(nullptr), isDelete(true),
          ownedKey(reinterpret_cast<const char *>(key), keyLen)
    {
        this->key = reinterpret_cast<const unsigned char *>(ownedKey.data());
    }

    KVPair(KVPair &&other) noexcept
        : key(other.key), keyLen(other.keyLen), value(other.value), isDelete(other.isDelete),
          ownedKey(std::move(other.ownedKey))
    {
        if (!ownedKey.empty()) {
            key = reinterpret_cast<const unsigned char *>(ownedKey.data());
        }
    }

    KVPair &operator=(KVPair &&other) noexcept
    {
        key = other.key;
        keyLen = other.keyLen;
        value = other.value;
        isDelete = other.isDelete;
        ownedKey = std::move(other.ownedKey);
        if (!ownedKey.empty()) {
            key = reinterpret_cast<const unsigned char *>(ownedKey.data());
        }
        return *this;
    }

    KVPair(const KVPair &) = delete;
    KVPair &operator=(const KVPair &) = delete;
};

struct KeyValueRef {
    std::string ownedKey;
    uint32_t keyLen;
    void *value;
};

struct MergeContext {
    std::vector<std::string> excludedKeys; // 存储buff里delete的墓碑数据和已经通过筛选的数据
    art_callback userCb;
    void *userData;
};

class LsmArtTree;
using LsmArtTreePtr = Ref<LsmArtTree>;

class LsmArtTree {
public:
    LsmArtTree()
    {
        art_tree_init(&mArtTree);
        mActiveBuffer.reserve(ELASTIC_CAPACITY);
        mFlushThread = std::thread(&LsmArtTree::BackgroundWorkerLoop, this);
    }

    ~LsmArtTree() noexcept
    {
        mStopWorker.store(true, std::memory_order_release);
        mCv.notify_one();

        if (mFlushThread.joinable()) {
            mFlushThread.join();
        }
        art_tree_destroy(&mArtTree);
    }

    static inline LsmArtTreePtr &Instance()
    {
        static LsmArtTreePtr instance = MakeRef<LsmArtTree>();
        return instance;
    }

    inline void Insert(const unsigned char *key, uint32_t keyLen, void *value)
    {
        if (key == nullptr || keyLen == 0 || value == nullptr) {
            return;
        }

        mActiveLock.Lock();
        mActiveBuffer.emplace_back(key, keyLen, value);
        bool needsFlush = (mActiveBuffer.size() >= FLUSH_WATERMARK);
        mActiveLock.UnLock();

        if (needsFlush) {
            TriggerBackgroundFlush();
        }
    }

    inline void Delete(const unsigned char *key, uint32_t keyLen)
    {
        if (key == nullptr || keyLen == 0) {
            return;
        }

        mActiveLock.Lock();
        auto isSameKey = [key, keyLen](const KVPair &kv) {
            return CompareKey(kv.key, kv.keyLen, key, keyLen) == 0;
        };
        mActiveBuffer.erase(std::remove_if(mActiveBuffer.begin(), mActiveBuffer.end(), isSameKey),
                            mActiveBuffer.end());
        mActiveBuffer.emplace_back(key, keyLen);
        bool needsFlush = (mActiveBuffer.size() >= FLUSH_WATERMARK);
        mActiveLock.UnLock();

        if (needsFlush) {
            TriggerBackgroundFlush();
        }
    }

    int SearchPrefix(const unsigned char *prefix, int prefixLen, art_callback cb, void *data);
    int SearchRange(const art_range_bound &startBound, const art_range_bound &endBound, art_callback cb, void *data);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    static int CompareKey(const unsigned char *left, uint32_t leftLen, const unsigned char *right, uint32_t rightLen)
    {
        uint32_t minLen = std::min(leftLen, rightLen);
        int cmp = memcmp(left, right, minLen);
        if (cmp != 0) {
            return cmp;
        }
        if (leftLen == rightLen) {
            return 0;
        }
        return (leftLen < rightLen) ? -1 : 1;
    }

    static bool StartsWith(const unsigned char *key, uint32_t keyLen, const unsigned char *prefix, uint32_t prefixLen)
    {
        return keyLen >= prefixLen && memcmp(key, prefix, prefixLen) == 0;
    }

    void BackgroundWorkerLoop();

    void TriggerBackgroundFlush()
    {
        mActiveLock.Lock();
        bool needsNotify = PrepareFlushLocked();
        mActiveLock.UnLock();
        if (needsNotify) {
            mCv.notify_one();
        }
    }

    static int MergeCallback(void *data, const unsigned char *key, uint32_t keyLen, void *val);

    bool PrepareFlushLocked()
    {
        if (mIsFlushing.load(std::memory_order_acquire) || mActiveBuffer.empty()) {
            return false;
        }
        mFlushBuffer = std::move(mActiveBuffer);
        mActiveBuffer.reserve(ELASTIC_CAPACITY);
        mIsFlushing.store(true, std::memory_order_release);
        return true;
    }

    bool PrepareNextFlushLocked()
    {
        if (mActiveBuffer.size() < FLUSH_WATERMARK) {
            mIsFlushing.store(false, std::memory_order_release);
            return false;
        }
        mFlushBuffer = std::move(mActiveBuffer);
        mActiveBuffer.reserve(ELASTIC_CAPACITY);
        return true;
    }

    template <typename FilterFunc>
    void CollectBufferData(MergeContext &ctx, std::vector<KeyValueRef> &pendingInserts, FilterFunc &&filter)
    {
        std::vector<const KVPair *> rawPtrs;
        rawPtrs.reserve(ELASTIC_CAPACITY * BUFF_SIZE_FACTOR);

        mActiveLock.Lock();
        for (const auto &kv : mFlushBuffer) {
            if (filter(kv.key, kv.keyLen)) {
                rawPtrs.emplace_back(&kv);
            }
        }
        for (const auto &kv : mActiveBuffer) {
            if (filter(kv.key, kv.keyLen)) {
                rawPtrs.emplace_back(&kv);
            }
        }

        if (rawPtrs.empty()) {
            mActiveLock.UnLock();
            return;
        }

        std::stable_sort(rawPtrs.begin(), rawPtrs.end(),
                         [](const KVPair *a, const KVPair *b) {
                             return CompareKey(a->key, a->keyLen, b->key, b->keyLen) < 0;
                         });

        for (size_t i = 0; i < rawPtrs.size(); ++i) {
            if (i + NO_1 < rawPtrs.size() &&
                CompareKey(rawPtrs[i]->key, rawPtrs[i]->keyLen, rawPtrs[i + NO_1]->key,
                           rawPtrs[i + NO_1]->keyLen) == 0) {
                continue;
            }

            ctx.excludedKeys.emplace_back(reinterpret_cast<const char *>(rawPtrs[i]->key), rawPtrs[i]->keyLen);
            if (!rawPtrs[i]->isDelete) {
                pendingInserts.push_back({rawPtrs[i]->ownedKey, rawPtrs[i]->keyLen, rawPtrs[i]->value});
            }
        }
        mActiveLock.UnLock();
    }

private:
    art_tree mArtTree;
    SpinLock mArtLock;

    std::vector<KVPair> mActiveBuffer;
    SpinLock mActiveLock;

    std::vector<KVPair> mFlushBuffer;
    std::atomic<bool> mIsFlushing{false};

    std::thread mFlushThread;
    std::atomic<bool> mStopWorker{false};
    std::mutex mCvMutex;
    std::condition_variable mCv;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif  // MMSCORE_LSM_ART_TREE_H
