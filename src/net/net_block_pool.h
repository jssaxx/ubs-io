/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef NET_BLOCK_POOL_H
#define NET_BLOCK_POOL_H

#include <unordered_map>
#include <utility>

#include "net_common.h"

namespace ock {
namespace bio {
/*
 * A thread safe linked list with buckets for multiple threads cases,
 * used for MR segment allocation.
 *
 * This linked list doesn't allocate extract memory for linked node,
 * the linked node info stores on the start place of free memory segment.
 * The linked node info needs to clean after allocated, since these memory segments
 * are allocated to end user possibly.
 */
class NetBlockPool {
public:
    NetBlockPool() = default;
    ~NetBlockPool() = default;

    NetBlockPool(const NetBlockPool &) = delete;
    NetBlockPool(NetBlockPool &&) = delete;
    NetBlockPool &operator = (const NetBlockPool &) = delete;
    NetBlockPool &operator = (NetBlockPool &&) = delete;

    BResult Start(uintptr_t address, uint64_t blockSize, uint64_t count)
    {
        for (uint64_t i = 0; i < count; i++) {
            PushFront(address + i * blockSize);
        }
        mIsStarted.store(true);
        return BIO_OK;
    }

    void Stop()
    {
        mIsStarted.store(false);
        mPopRRIdx = 0;
        mPushRRIdx = 0;
        for (uint16_t i = 0; i < BUCKET_COUNT; i++) {
            mBuckets[i].next = nullptr;
            mBuckets[i].count = 0;
        }
    }

    inline BResult AllocOne(uintptr_t &address)
    {
        if (mIsStarted.load() == false) {
            NET_LOG_ERROR("Net block pool not ready.");
            return BIO_NOT_READY;
        }
        if (!Pop(address)) {
            return BIO_ALLOC_FAIL;
        }
        return BIO_OK;
    }

    inline void ReleaseOne(uintptr_t address)
    {
        if (mIsStarted.load() == false) {
            NET_LOG_ERROR("Net block pool not ready.");
            return;
        }
        PushFront(address);
    }

    inline BResult AllocMany(uint32_t count, std::vector<uintptr_t> &items)
    {
        if (mIsStarted.load() == false) {
            NET_LOG_ERROR("Net block pool not ready.");
            return BIO_NOT_READY;
        }
        if (!PopN(count, items)) {
            return BIO_ALLOC_FAIL;
        }
        return BIO_OK;
    }

    inline void ReleaseMany(std::vector<uintptr_t> &items)
    {
        if (mIsStarted.load() == false) {
            NET_LOG_ERROR("Net block pool not ready.");
            return;
        }
        PushN(items);
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    inline void PushFront(uintptr_t item)
    {
        BioBucketLinkedListMeta *buckets = &mBuckets[__sync_fetch_and_add(&mPushRRIdx, 1) % BUCKET_COUNT];
        auto *newNode = reinterpret_cast<BioLinkedListNode *>(item);

        buckets->lock.Lock();
        newNode->next = buckets->next;
        buckets->next = newNode;
        buckets->count++;
        buckets->lock.UnLock();
    }

    inline bool Pop(uintptr_t &item)
    {
        uint16_t leftBucketsCount = BUCKET_COUNT;
        do {
            BioBucketLinkedListMeta *buckets = &mBuckets[__sync_fetch_and_add(&mPopRRIdx, 1) % BUCKET_COUNT];

            buckets->lock.Lock();
            if (UNLIKELY(buckets->count == 0)) {
                buckets->lock.UnLock();
                continue;
            }

            item = reinterpret_cast<uintptr_t>(buckets->next);

            buckets->next = buckets->next->next;
            buckets->count--;
            buckets->lock.UnLock();
            return true;
        } while (--leftBucketsCount > 0);

        return false;
    }

    inline bool PopN(uint32_t count, std::vector<uintptr_t> &items)
    {
        if (UNLIKELY(count == 0)) {
            return false;
        }

        items.reserve(count);
        items.clear();

        /* traverse every bucket for balance */
        for (uint32_t i = 0; i < count; i++) {
            uintptr_t tmpItem = 0;
            if (UNLIKELY(!Pop(tmpItem))) {
                for (auto &iter : items) {
                    PushFront(iter);
                }
                return false;
            }
            items.emplace_back(tmpItem);
        }

        return true;
    }

    inline void PushN(std::vector<uintptr_t> &items)
    {
        if (UNLIKELY(items.empty())) {
            return;
        }

        for (auto &iter : items) {
            PushFront(iter);
        }
    }

private:
    static const uint16_t BUCKET_COUNT = 32;
    /*
     * Node info for linked list
     */
    struct BioLinkedListNode {
        struct BioLinkedListNode *next = nullptr; /* point to next node, which is memory segment */
    };

    /*
     * The meta info for one linked list,
     */
    struct BioBucketLinkedListMeta {
        BioLinkedListNode *next = nullptr; /* point to the real memory segment */
        SpinLock lock{};                   /* spin lock for insertion & deletion of memory */
        uint32_t count = 0;                /* the count of current linked list */
    };

    std::atomic<bool> mIsStarted{ false };
    /* NOTE: to make sure the size of this class is same with one cache line of CPU */
    uint32_t mPopRRIdx = 0;                           /* round-robin index for pop */
    uint32_t mPushRRIdx = 0;                          /* round-robin index for push */
    BioBucketLinkedListMeta mBuckets[BUCKET_COUNT]{}; /* buckets linked list */
    DEFINE_REF_COUNT_VARIABLE;
};

using NetBlockPoolPtr = Ref<NetBlockPool>;
}
}

#endif // NET_BLOCK_POOL_H