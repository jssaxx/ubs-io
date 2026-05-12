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

#ifndef MMSCORE_MMS_RING_BUFFER_QUEUE_H
#define MMSCORE_MMS_RING_BUFFER_QUEUE_H

#include <semaphore.h>
#include <sstream>

#include "mms_lock.h"

namespace ock {
namespace mms {
/*
 * @brief A ring buffer, guarded by spin lock, allow flex capacity
 */
template <typename T> class RingBuffer {
public:
    explicit RingBuffer(uint32_t capacity) : mCapacity(capacity) {}

    RingBuffer() = delete;

    ~RingBuffer()
    {
        UnInitialize();
    }

    /*
     * @brief Set capacity, which should be called before initialize
     *
     * @param capacity         [in] capacity number
     */
    inline void Capacity(uint32_t capacity)
    {
        if (mRingBuf == nullptr) {
            mCapacity = capacity;
        }
    }

    /*
     * @brief Get capacity
     *
     * @return capacity
     */
    inline uint32_t Capacity() const
    {
        return mCapacity;
    }

    /*
     * @brief Initialize ring buffer
     *
     * @return 0 if successful, -1 in two cases:
     * a) capacity is not valid, i.e. 0
     * b) failed to malloc bucket for the ring
     */
    inline int32_t Initialize()
    {
        if (mCapacity == 0) {
            return -1;
        }

        mCount = 0;
        mHead = 0;
        mTail = 0;

        if (mRingBuf != nullptr) {
            return 0;
        }

        mRingBuf = new (std::nothrow) T[mCapacity];
        if (mRingBuf == nullptr) {
            return -1;
        }

        return 0;
    }

    /*
     * @brief UnInitialize the ring buffer
     */
    inline void UnInitialize()
    {
        if (mRingBuf == nullptr) {
            return;
        }

        delete[] mRingBuf;
        mRingBuf = nullptr;
    }

    /*
     * @brief Push back an item into ring buffer, must be initialized firstly
     *
     * @param item             [in] item to push back
     *
     * @return true if successful, false if the ring buffer is full
     */
    inline bool PushBack(const T &item)
    {
        mLock.Lock();
        if (mCapacity <= mCount) {
            mLock.UnLock();
            return false;
        }

        mRingBuf[mTail] = item;
        if (mTail != mCapacity - 1) {
            ++mTail;
        } else {
            mTail = 0;
        }
        ++mCount;
        mLock.UnLock();
        return true;
    }

    /*
     * @brief Push front an item into ring buffer, must be initialized firstly
     *
     * @param item             [in] item to push front
     *
     * @return true if successful, false if the ring buffer is full
     */
    inline bool PushFront(const T &item)
    {
        mLock.Lock();
        if (mCapacity <= mCount) {
            mLock.UnLock();
            return false;
        }

        /* move to tail */
        if (mHead == 0) {
            mHead = mCapacity - 1;
        } else {
            mHead--;
        }

        mRingBuf[mHead] = item;
        ++mCount;

        mLock.UnLock();
        return true;
    }

    /*
     * @brief Pop an item from front of ring buffer, must be initialized firstly
     *
     * @param item             [out] item popped in front
     *
     * @return true if successful, false if the ring buffer is empty
     */
    inline bool PopFront(T &item)
    {
        mLock.Lock();
        if (mCount == 0) {
            mLock.UnLock();
            return false;
        }

        item = mRingBuf[mHead];
        if (mHead != mCapacity - 1) {
            ++mHead;
        } else {
            mHead = 0;
        }
        --mCount;
        mLock.UnLock();
        return true;
    }

    /*
     * @brief Pop N items from front of ring buffer, must be initialized firstly
     * and caller must ensure items is not null
     *
     * @param items            [out] item popped in front
     * @param n                [out] item count popped in front
     *
     * @return true if successful, false if the ring buffer doesn't have n items
     */
    inline bool PopFrontN(T *items, uint32_t n)
    {
        mLock.Lock();
        if (mCount < n) {
            mLock.UnLock();
            return false;
        }

        for (uint32_t i = 0; i < n; ++i) {
            items[i] = mRingBuf[mHead];
            if (mHead != mCapacity - 1) {
                ++mHead;
            } else {
                mHead = 0;
            }
        }

        mCount -= n;

        mLock.UnLock();
        return true;
    }

    /*
     * @brief Pop N items from front of ring buffer, must be initialized firstly
     * and caller must ensure items is not null
     *
     * @param items            [out] item popped in front
     * @param n                [out] item count popped in front actually
     *
     * @return true if successful, false if the ring buffer is empty
     */
    inline bool PopFrontNFlex(T *items, uint32_t &n)
    {
        mLock.Lock();
        if (mCount == 0) {
            mLock.UnLock();
            return false;
        }

        n = mCount < n ? mCount : n;

        for (uint32_t i = 0; i < n; ++i) {
            items[i] = mRingBuf[mHead];
            if (mHead != mCapacity - 1) {
                ++mHead;
            } else {
                mHead = 0;
            }
        }

        mCount -= n;

        mLock.UnLock();
        return true;
    }

    /*
     * @brief Push front N items into ring buffer, must be initialized firstly
     * and caller must ensure items is not null
     *
     * @param items            [in] item to push front
     * @param n                [in] item count to push
     *
     * @return true if successful, false if the ring buffer doesn't have free bucket
     */
    inline bool PushFrontN(T *items, uint32_t n)
    {
        mLock.Lock();
        if (mCapacity < (mCount + n)) {
            mLock.UnLock();
            return false;
        }

        for (uint32_t i = 0; i < n; ++i) {
            // move to tail
            if (mHead == 0) {
                mHead = mCapacity - 1;
            } else {
                mHead--;
            }

            mRingBuf[mHead] = items[i];
        }
        mCount += n;

        mLock.UnLock();
        return true;
    }

    /*
     * @brief Push back N items into ring buffer, must be initialized firstly
     * and caller must ensure items is not null
     *
     * @param items            [in] item to push back
     * @param n                [in] item count to push
     *
     * @return true if successful, false if the ring buffer doesn't have free bucket
     */
    inline bool PushBackN(T *items, uint32_t n)
    {
        mLock.Lock();
        if (mCapacity < (mCount + n)) {
            mLock.UnLock();
            return false;
        }

        for (uint32_t i = 0; i < n; ++i) {
            mRingBuf[mTail] = items[i];
            if (mTail != mCapacity - 1) {
                ++mTail;
            } else {
                mTail = 0;
            }
        }
        mCount += n;

        mLock.UnLock();
        return true;
    }

    /*
     * @brief Get size of ring buffer
     *
     * @return size of item in ring buffer
     */
    inline uint32_t Size()
    {
        mLock.Lock();
        auto temp = mCount;
        mLock.UnLock();
        return temp;
    }

    /*
     * @brief Brief ring buffer info to string
     */
    inline std::string ToString()
    {
        std::ostringstream oss;
        oss << "head " << mHead << ", tail " << mTail << ", capacity " << mCapacity << ", count " << mCount;
        return oss.str();
    }

    RingBuffer(const RingBuffer &) = delete;
    RingBuffer(RingBuffer &&) = delete;
    RingBuffer &operator = (const RingBuffer &) = delete;
    RingBuffer &operator = (RingBuffer &&) = delete;

private:
    T *mRingBuf = nullptr;
    SpinLock mLock;
    uint32_t mCapacity = 0;
    uint32_t mCount = 0;
    uint32_t mHead = 0;
    uint32_t mTail = 0;
};

/*
 * @brief A blocking queue on top of ring buffer
 */
template <typename T> class RingBufferBlockingQueue {
public:
    explicit RingBufferBlockingQueue(uint32_t capacity) : mRingBuffer(capacity) {}

    ~RingBufferBlockingQueue()
    {
        UnInitialize();
    }

    /*
     * @brief Initialize the queue
     *
     * @return 0 if successful, -1 if sem_init failed or inner ring buffer initialization failed
     */
    inline int Initialize()
    {
        if (sem_init(&mSem, 0, 0) != 0) {
            return -1;
        }

        return mRingBuffer.Initialize();
    }

    /*
     * @brief UnInitialized
     */
    inline void UnInitialize()
    {
        mRingBuffer.UnInitialize();
        sem_destroy(&mSem);
    }

    /*
     * @brief Enqueue an item into queue and notify to waiters
     *
     * @param item             [in] item to be enqueued
     *
     * @return true if successful, false if queue is full
     */
    inline bool Enqueue(const T &item)
    {
        auto result = mRingBuffer.PushBack(item);
        if (result) {
            sem_post(&mSem);
        }
        return result;
    }

    /*
     * @brief Enqueue an item into queue and notify to waiters
     *
     * @param item             [in] item to be enqueued
     *
     * @return true if successful, false if queue is full
     */
    inline bool Enqueue(T &item)
    {
        auto result = mRingBuffer.PushBack(item);
        if (result) {
            sem_post(&mSem);
        }
        return result;
    }

    /*
     * @brief Enqueue an item into queue in the front and notify to waiters
     *
     * @param item             [in] item to be enqueued
     *
     * @return true if successful, false if queue is full
     */
    inline bool EnqueueFirst(T &item)
    {
        auto result = mRingBuffer.PushFront(item);
        if (result) {
            sem_post(&mSem);
        }
        return result;
    }

    /*
     * @brief Enqueue an item into queue in the front and notify to waiters
     *
     * @param item             [in] item to be enqueued
     *
     * @return true if successful, false if queue is full
     */
    inline bool EnqueueFirst(const T &item)
    {
        auto result = mRingBuffer.PushFront(item);
        if (result) {
            sem_post(&mSem);
        }
        return result;
    }

    /*
     * @brief Dequeue an item from queue in the front, wait if no item
     *
     * @param item             [in] item to be enqueued
     *
     * @return true if successful, false if queue is empty
     */
    inline bool Dequeue(T &item)
    {
        while (true) {
            auto result = mRingBuffer.PopFront(item);
            if (!result) {
                sem_wait(&mSem);
            } else {
                return result;
            }
        }
    }

private:
    RingBuffer<T> mRingBuffer; /* ring buffer to data store */
    sem_t mSem{};              /* semaphore to wait and notify */
};
}
}

#endif // MMSCORE_MMS_RING_BUFFER_QUEUE_H

