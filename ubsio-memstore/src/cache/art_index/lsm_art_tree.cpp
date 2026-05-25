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

#include "lsm_art_tree.h"

namespace ock {
namespace mms {

int LsmArtTree::SearchPrefix(const unsigned char *prefix, int prefixLen, art_callback cb, void *data)
{
    MergeContext ctx;
    ctx.userCb = cb;
    ctx.userData = data;

    std::vector<KeyValueRef> pendingInserts;
    CollectBufferData(ctx, pendingInserts, [prefix, prefixLen](const unsigned char *key, uint32_t keyLen) {
        return StartsWith(key, keyLen, prefix, static_cast<uint32_t>(prefixLen));
    });
    for (const auto &kv : pendingInserts) {
        int res = cb(data, reinterpret_cast<const unsigned char *>(kv.ownedKey.data()), kv.keyLen, kv.value);
        if (res != 0) {
            return res;
        }
    }

    mArtLock.Lock();
    int res = art_iter_prefix(&mArtTree, prefix, prefixLen, MergeCallback, &ctx);
    mArtLock.UnLock();

    return res;
}

int LsmArtTree::SearchRange(const art_range_bound &startBound, const art_range_bound &endBound, art_callback cb,
                            void *data)
{
    MergeContext ctx;
    ctx.userCb = cb;
    ctx.userData = data;

    std::vector<KeyValueRef> pendingInserts;
    CollectBufferData(ctx, pendingInserts, [&startBound, &endBound](const unsigned char *key, uint32_t keyLen) {
        return CompareKey(key, keyLen, startBound.key, static_cast<uint32_t>(startBound.len)) >= 0 &&
               CompareKey(key, keyLen, endBound.key, static_cast<uint32_t>(endBound.len)) <= 0;
    });
    for (const auto &kv : pendingInserts) {
        int res = cb(data, reinterpret_cast<const unsigned char *>(kv.ownedKey.data()), kv.keyLen, kv.value);
        if (res != 0) {
            return res;
        }
    }

    mArtLock.Lock();
    int res = art_search_range_external(&mArtTree, &startBound, &endBound, MergeCallback, &ctx);
    mArtLock.UnLock();

    return res;
}

void LsmArtTree::BackgroundWorkerLoop()
{
    while (true) {
        std::unique_lock<std::mutex> lock(mCvMutex);
        mCv.wait(lock, [this] {
            return mIsFlushing.load(std::memory_order_acquire) || mStopWorker.load(std::memory_order_acquire);
        });

        if (mStopWorker.load(std::memory_order_acquire) && !mIsFlushing.load(std::memory_order_acquire)) {
            break;
        }

        while (mIsFlushing.load(std::memory_order_acquire)) {
            mArtLock.Lock();
            for (const auto &kv : mFlushBuffer) {
                if (kv.isDelete) {
                    art_delete(&mArtTree, kv.key, kv.keyLen);
                } else {
                    art_insert(&mArtTree, kv.key, kv.keyLen, kv.value);
                }
            }
            mArtLock.UnLock();

            mActiveLock.Lock();
            mFlushBuffer.clear();
            bool hasNextFlush = PrepareNextFlushLocked();
            mActiveLock.UnLock();
            if (!hasNextFlush) {
                break;
            }
        }
    }
}

int LsmArtTree::MergeCallback(void *data, const unsigned char *key, uint32_t keyLen, void *val)
{
    auto *ctx = static_cast<MergeContext *>(data);

    if (!ctx->excludedKeys.empty()) {
        auto it = std::lower_bound(ctx->excludedKeys.begin(), ctx->excludedKeys.end(), keyLen,
                                   [key](const std::string &curKey, uint32_t artKeyLen) {
                                       size_t min_len = std::min(curKey.size(), static_cast<size_t>(artKeyLen));
                                       int cmp = memcmp(curKey.data(), key, min_len);
                                       if (cmp != 0) {
                                           return cmp < 0;
                                       }
                                       return curKey.size() < artKeyLen;
                                   });
        if (it != ctx->excludedKeys.end() && it->size() == keyLen && memcmp(it->data(), key, keyLen) == 0) {
            return 0;
        }
    }

    return ctx->userCb(ctx->userData, key, keyLen, val);
}
}
}
