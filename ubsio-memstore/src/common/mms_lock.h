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

#ifndef MMSCORE_MMS_LOCK_H
#define MMSCORE_MMS_LOCK_H

#include <mutex>
#include <atomic>
#include <pthread.h>

namespace ock {
namespace mms {
class Lock {
public:
    Lock() = default;
    ~Lock() = default;

    Lock(const Lock &) = delete;
    Lock &operator = (const Lock &) = delete;
    Lock(Lock &&) = delete;
    Lock &operator = (Lock &&) = delete;

    inline void DoLock()
    {
        mLock.lock();
    }

    inline void UnLock()
    {
        mLock.unlock();
    }

private:
    std::mutex mLock;
};

class RecursiveLock {
public:
    RecursiveLock() = default;
    ~RecursiveLock() = default;

    RecursiveLock(const RecursiveLock &) = delete;
    RecursiveLock &operator = (const RecursiveLock &) = delete;
    RecursiveLock(RecursiveLock &&) = delete;
    RecursiveLock &operator = (RecursiveLock &&) = delete;

    inline void DoLock()
    {
        mLock.lock();
    }
    inline void UnLock()
    {
        mLock.unlock();
    }

private:
    std::recursive_mutex mLock;
};

class ReadWriteLock {
public:
    ReadWriteLock()
    {
        pthread_rwlock_init(&mLock, nullptr);
    }
    ~ReadWriteLock()
    {
        pthread_rwlock_destroy(&mLock);
    }

    ReadWriteLock(const ReadWriteLock &) = delete;
    ReadWriteLock &operator = (const ReadWriteLock &) = delete;
    ReadWriteLock(ReadWriteLock &&) = delete;
    ReadWriteLock &operator = (ReadWriteLock &&) = delete;

    inline void LockRead()
    {
        pthread_rwlock_rdlock(&mLock);
    }

    inline void LockWrite()
    {
        pthread_rwlock_wrlock(&mLock);
    }

    inline void UnLock()
    {
        pthread_rwlock_unlock(&mLock);
    }

private:
    pthread_rwlock_t mLock{};
};

class SpinLock {
public:
    SpinLock() = default;
    ~SpinLock() = default;

    SpinLock(const SpinLock &) = delete;
    SpinLock &operator = (const SpinLock &) = delete;
    SpinLock(SpinLock &&) = delete;
    SpinLock &operator = (SpinLock &&) = delete;

    inline void TryLock()
    {
        mFlag.test_and_set(std::memory_order_acquire);
    }

    inline void Lock()
    {
        while (mFlag.test_and_set(std::memory_order_acquire)) {
        }
    }

    inline void UnLock()
    {
        mFlag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag mFlag = ATOMIC_FLAG_INIT;
};

template <class T> class Locker {
public:
    explicit Locker(T *lock) : mLock(lock)
    {
        if (mLock != nullptr) {
            mLock->DoLock();
        }
    }

    ~Locker()
    {
        if (mLock != nullptr) {
            mLock->UnLock();
        }
    }

    Locker(const Locker &) = delete;
    Locker &operator = (const Locker &) = delete;
    Locker(Locker &&) = delete;
    Locker &operator = (Locker &&) = delete;

private:
    T *mLock;
};

template <class T> class ReadLocker {
public:
    explicit ReadLocker(T *lock) : mLock(lock)
    {
        if (mLock != nullptr) {
            mLock->LockRead();
        }
    }

    ~ReadLocker()
    {
        if (mLock != nullptr) {
            mLock->UnLock();
        }
    }

    ReadLocker(const ReadLocker &) = delete;
    ReadLocker &operator = (const ReadLocker &) = delete;
    ReadLocker(ReadLocker &&) noexcept = delete;
    ReadLocker &operator = (ReadLocker &&) noexcept = delete;

private:
    T *mLock;
};

template <class T> class WriteLocker {
public:
    explicit WriteLocker(T *lock) : mLock(lock)
    {
        if (mLock != NULL) {
            mLock->LockWrite();
        }
    }

    ~WriteLocker()
    {
        if (mLock != NULL) {
            mLock->UnLock();
        }
    }

    WriteLocker(const WriteLocker &) = delete;
    WriteLocker &operator = (const WriteLocker &) = delete;
    WriteLocker(WriteLocker &&) noexcept = delete;
    WriteLocker &operator = (WriteLocker &&) noexcept = delete;

private:
    T *mLock;
};
#define GUARD(lLock, alias) Locker<Lock> __l##alias(lLock)
#define RECURSIVE_GUARD(mylock) Locker<RecursiveLock> __locker##mylock(mylock)
}
}

#endif

