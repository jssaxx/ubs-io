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
#include "lsm_art_tree.h"

int LsmArtTree::SearchPrefix(const unsigned char *prefix, int prefixLen, art_callback cb, void *data)
{
    MergeContext ctx;
    ctx.userCb = cb;
    ctx.userData = data;

    std::vector<KVPair> pendingInserts;
    std::string prefixStr(reinterpret_cast<const char *>(prefix), prefixLen);

    CollectBufferData(ctx, pendingInserts, [&prefixStr](const std::string &k) { return StartsWith(k, prefixStr); });
    for (const auto &kv : pendingInserts) {
        int res = cb(data, reinterpret_cast<const unsigned char *>(kv.key.data()), kv.key.size(), kv.value);
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

    std::vector<KVPair> pendingInserts;
    std::string sKey(reinterpret_cast<const char *>(startBound.key), startBound.len);
    std::string eKey(reinterpret_cast<const char *>(endBound.key), endBound.len);

    CollectBufferData(ctx, pendingInserts, [&sKey, &eKey](const std::string &k) { return k >= sKey && k <= eKey; });
    for (const auto &kv : pendingInserts) {
        int res = cb(data, reinterpret_cast<const unsigned char *>(kv.key.data()), kv.key.size(), kv.value);
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

        if (mIsFlushing.load(std::memory_order_acquire)) {
            mArtLock.Lock();
            for (const auto &kv : mFlushBuffer) {
                if (kv.isDelete) {
                    art_delete(&mArtTree, reinterpret_cast<const unsigned char *>(kv.key.data()), kv.key.size());
                } else {
                    art_insert(&mArtTree, reinterpret_cast<const unsigned char *>(kv.key.data()), kv.key.size(),
                               kv.value);
                }
            }
            mArtLock.UnLock();

            mActiveLock.Lock();
            mFlushBuffer.clear();
            mActiveLock.UnLock();

            mIsFlushing.store(false, std::memory_order_release);
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
