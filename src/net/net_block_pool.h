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
class BioNetBlockPool {
public:
    BioNetBlockPool() = default;
    ~BioNetBlockPool() = default;

    BioNetBlockPool(const BioNetBlockPool &) = delete;
    BioNetBlockPool(BioNetBlockPool &&) = delete;
    BioNetBlockPool &operator = (const BioNetBlockPool &) = delete;
    BioNetBlockPool &operator = (BioNetBlockPool &&) = delete;


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
        ASSERT_RETURN(mIsStarted.load() == true, BIO_NOT_READY);
        if (!Pop(address)) {
            return BIO_ALLOC_FAIL;
        }

        return BIO_OK;
    }

    inline void ReleaseOne(uintptr_t address)
    {
        ASSERT_RET_VOID(mIsStarted.load() == true);
        PushFront(address);
    }

    inline BResult AllocMany(uint32_t count, std::vector<uintptr_t> &items)
    {
        ASSERT_RETURN(mIsStarted.load() == true, BIO_NOT_READY);
        if (!PopN(count, items)) {
            return BIO_ALLOC_FAIL;
        }
        return BIO_OK;
    }

    inline void ReleaseMany(std::vector<uintptr_t> &items)
    {
        ASSERT_RET_VOID(mIsStarted.load() == true);
        PushN(items);
    }

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    /*
     * @brief Push one item to linked list
     *
     * @param item         [in] the address of memory to added to list
     */
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
        if (UNLIKELY(items.size() == 0)) {
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

using BioNetBlockPoolPtr = Ref<BioNetBlockPool>;
}
}

#endif // NET_BLOCK_POOL_H
