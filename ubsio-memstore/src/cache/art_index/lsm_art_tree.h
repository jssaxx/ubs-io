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
    std::string key;
    void *value;
    bool isDelete;

    KVPair(std::string &&key, void *val, bool isDelete) : key(std::move(key)), value(val), isDelete(isDelete) {}
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

    inline void Insert(std::string &&key, void *value)
    {
        mActiveLock.Lock();
        mActiveBuffer.emplace_back(std::move(key), value, false);
        bool needsFlush = (mActiveBuffer.size() >= FLUSH_WATERMARK);
        mActiveLock.UnLock();

        if (needsFlush) {
            TriggerBackgroundFlush();
        }
    }

    inline void Delete(std::string &&key)
    {
        mActiveLock.Lock();
        mActiveBuffer.emplace_back(std::move(key), nullptr, true);
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
    static bool StartsWith(const std::string &str, const std::string &prefix)
    {
        return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
    }

    void BackgroundWorkerLoop();

    void TriggerBackgroundFlush()
    {
        bool expected = false;
        if (mIsFlushing.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
            mActiveLock.Lock();
            mFlushBuffer = std::move(mActiveBuffer);
            mActiveBuffer.reserve(ELASTIC_CAPACITY);
            mActiveLock.UnLock();

            mCv.notify_one();
        }
    }

    static int MergeCallback(void *data, const unsigned char *key, uint32_t keyLen, void *val);

    template <typename FilterFunc>
    void CollectBufferData(MergeContext &ctx, std::vector<KVPair> &pendingInserts, FilterFunc &&filter)
    {
        std::vector<const KVPair *> rawPtrs;
        rawPtrs.reserve(ELASTIC_CAPACITY * BUFF_SIZE_FACTOR);

        mActiveLock.Lock();
        for (const auto &kv : mFlushBuffer) {
            if (filter(kv.key))
                rawPtrs.emplace_back(&kv);
        }
        for (const auto &kv : mActiveBuffer) {
            if (filter(kv.key))
                rawPtrs.emplace_back(&kv);
        }

        if (rawPtrs.empty()) {
            mActiveLock.UnLock();
            return;
        }

        std::stable_sort(rawPtrs.begin(), rawPtrs.end(),
                         [](const KVPair *a, const KVPair *b) { return a->key < b->key; });

        for (size_t i = 0; i < rawPtrs.size(); ++i) {
            if (i + NO_1 < rawPtrs.size() && rawPtrs[i]->key == rawPtrs[i+NO_1]->key) {
                continue;
            }

            ctx.excludedKeys.emplace_back(rawPtrs[i]->key);
            if (!rawPtrs[i]->isDelete) {
                pendingInserts.emplace_back(*rawPtrs[i]);
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

