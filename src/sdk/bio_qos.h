/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BIO_QOS_H
#define BIO_QOS_H

#include <atomic>
#include <list>
#include <vector>
#include <semaphore.h>
#include "bio_ref.h"
#include "bio_err.h"
#include "bio_def.h"
#include "bio_lock.h"
#include "bio_client_log.h"

namespace ock {
namespace bio {
constexpr uint8_t QOS_CONCURRENCY = 0x01;
constexpr uint8_t QOS_QUOTA = 0x10;

enum QuotaType {
    QUOTA_WRITE = 0,
    QUOTA_READ = 1,
    QUOTA_BUTT
};

struct IoWaitEntry {
    uint64_t size;
    sem_t sem;

    explicit IoWaitEntry(uint64_t allocSize) : size(allocSize)
    {
        sem_init(&sem, 0, 0);
    }

    ~IoWaitEntry()
    {
        sem_destroy(&sem);
    }

    inline void Wait()
    {
        sem_wait(&sem);
    }

    inline void Wake()
    {
        sem_post(&sem);
    }
};

class IoHangQueue {
public:
    IoHangQueue() = default;
    ~IoHangQueue() = default;

    inline void Push(IoWaitEntry *entry)
    {
        mTaskList.emplace_back(entry);
    }

    inline IoWaitEntry *Top()
    {
        auto iter = mTaskList.begin();
        return *iter;
    }

    inline void Pop()
    {
        mTaskList.pop_front();
    }

    inline bool Empty()
    {
        return mTaskList.empty();
    }

private:
    std::list<IoWaitEntry *> mTaskList;
};

class BioQuota;
using BioQuotaPtr = Ref<BioQuota>;
class BioQuota {
public:
    BioQuota() = default;
    ~BioQuota() = default;

    static BioQuotaPtr &Instance()
    {
        static auto instance = MakeRef<BioQuota>();
        return instance;
    }

    void Initialize(uint64_t writeQuota, uint64_t readQuota)
    {
        mMaxQuota[QUOTA_WRITE] = writeQuota;
        mMaxQuota[QUOTA_READ] = readQuota;
        mAdjustQuota[QUOTA_WRITE] = writeQuota;
        mAdjustQuota[QUOTA_READ] = readQuota;
        mAllocQuota[QUOTA_WRITE] = 0;
        mAllocQuota[QUOTA_READ] = 0;
    }

    inline void AllocQuota(uint64_t size, QuotaType type)
    {
        IoWaitEntry entry(size);
        {
            WriteLocker<ReadWriteLock> lock(&mLock[type]);
            uint64_t remain = mMaxQuota[type] - mAllocQuota[type];
            if (LIKELY(remain >= size)) {
                mAllocQuota[type] += size;
                return;
            } else {
                mIoQueue[type].Push(&entry);
                CLIENT_LOG_WARN("[QOS]Quota not enough, quota:" << mMaxQuota[type] << ", remain:" << remain << ".");
            }
        }
        entry.Wait();
    }

    inline void ReleaseQuota(uint64_t size, QuotaType type, uint64_t adjustQuota)
    {
        WriteLocker<ReadWriteLock> lock(&mLock[type]);
        mAllocQuota[type] -= size;
        if (LIKELY(adjustQuota <= mMaxQuota[type])) {
            int64_t diff = static_cast<int64_t>(mAdjustQuota[type]) - static_cast<int64_t>(adjustQuota);
            CLIENT_LOG_INFO("[QOS]Client adjust quota, [" << mAdjustQuota[type] << "-->" << adjustQuota << "].");
            mAllocQuota[type] += diff;
            mAdjustQuota[type] = adjustQuota;
        }
        Dispatch(type);
    }

    inline void Show(std::vector<uint64_t> &max, std::vector<uint64_t> &adjust, std::vector<uint64_t> &alloc)
    {
        for (uint32_t idx = QUOTA_WRITE; idx < QUOTA_BUTT; idx++) {
            {
                ReadLocker<ReadWriteLock> lock(&mLock[idx]);
                max.emplace_back(mMaxQuota[idx]);
                adjust.emplace_back(mAdjustQuota[idx]);
                alloc.emplace_back(mAllocQuota[idx]);
            }
        }
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    void Dispatch(QuotaType type);

private:
    ReadWriteLock mLock[QUOTA_BUTT];
    uint64_t mMaxQuota[QUOTA_BUTT] = { 0, 0 };
    uint64_t mAdjustQuota[QUOTA_BUTT] = { 0, 0 };
    uint64_t mAllocQuota[QUOTA_BUTT] = { 0, 0 };
    IoHangQueue mIoQueue[QUOTA_BUTT];
    DEFINE_REF_COUNT_VARIABLE;
};

class BioConcurrency;
using BioConcurrencyPtr = Ref<BioConcurrency>;
class BioConcurrency {
public:
    static BioConcurrencyPtr &Instance()
    {
        static auto instance = MakeRef<BioConcurrency>();
        return instance;
    }

    void Initialize(uint64_t writeConcur, uint64_t readConcur)
    {
        mOriConcur[QUOTA_WRITE] = writeConcur;
        mOriConcur[QUOTA_READ] = readConcur;
        mCurConcur[QUOTA_WRITE] = 0UL;
        mCurConcur[QUOTA_READ] = 0UL;
    }

    void ApplyConcur(QuotaType type)
    {
        IoWaitEntry entry(0);
        {
            WriteLocker<ReadWriteLock> lock(&mLock[type]);
            if (LIKELY(mCurConcur[type] < mOriConcur[type])) {
                mCurConcur[type]++;
                return;
            } else {
                mIoQueue[type].Push(&entry);
                CLIENT_LOG_WARN("[QOS]Concurrency not enough, need io hang, concur:" << mCurConcur[type] << ".");
            }
        }
        entry.Wait();
    }

    void ReleaseConcur(QuotaType type)
    {
        WriteLocker<ReadWriteLock> lock(&mLock[type]);
        mCurConcur[type]--;
        if (mIoQueue[type].Empty()) {
            return;
        }
        auto entry = mIoQueue[type].Top();
        mCurConcur[type]++;
        entry->Wake();
        mIoQueue[type].Pop();
    }

    void ConcurCount(std::vector<uint64_t> &concur)
    {
        for (uint32_t idx = QUOTA_WRITE; idx < QUOTA_BUTT; idx++) {
            {
                ReadLocker<ReadWriteLock> lock(&mLock[idx]);
                concur.emplace_back(mCurConcur[idx]);
            }
        }
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    ReadWriteLock mLock[QUOTA_BUTT];
    uint64_t mOriConcur[QUOTA_BUTT] = { 0, 0 };
    uint64_t mCurConcur[QUOTA_BUTT] = { 0, 0 };
    IoHangQueue mIoQueue[QUOTA_BUTT];
    DEFINE_REF_COUNT_VARIABLE;
};

class BioQos;
using BioQosPtr = Ref<BioQos>;
class BioQos {
public:
    BioQos() = default;
    ~BioQos() = default;

    static BioQosPtr &Instance()
    {
        static auto instance = MakeRef<BioQos>();
        return instance;
    }

    BResult Initialize(uint64_t writeRes, uint64_t readRes, uint64_t writeConcur, uint64_t readConcur)
    {
        if (LIKELY(mQuota == nullptr && mConcur == nullptr)) {
            mQuota = BioQuota::Instance();
            uint64_t writeQuota = writeRes;
            uint64_t readQuota = readRes;
            mQuota->Initialize(writeQuota, readQuota);
            mConcur = BioConcurrency::Instance();
            mConcur->Initialize(writeConcur, readConcur);
            mSwitch = true;
            CLIENT_LOG_INFO("[QOS]Qos initialize success, write quota:" << writeQuota << ", read quota:" <<
                readQuota << ", write concur:" << writeConcur << ", read concur:" << readConcur << ".");
        } else {
            CLIENT_LOG_WARN("[QOS]Repeated initialization qos module.");
        }
        return BIO_OK;
    }

    inline void Apply(uint8_t mode, QuotaType type, uint64_t size = 0)
    {
        if (LIKELY(mSwitch)) {
            if (LIKELY(mode & QOS_CONCURRENCY)) {
                mConcur->ApplyConcur(type);
            }
            if (LIKELY(mode & QOS_QUOTA)) {
                mQuota->AllocQuota(size, type);
            }
        }
    }

    inline void Release(uint8_t mode, QuotaType type, uint64_t size = 0, uint64_t adjust = UINT64_MAX)
    {
        if (LIKELY(mSwitch)) {
            if (LIKELY(mode & QOS_QUOTA)) {
                mQuota->ReleaseQuota(size, type, adjust);
            }
            if (LIKELY(mode & QOS_CONCURRENCY)) {
                mConcur->ReleaseConcur(type);
            }
        }
    }

    inline void Show(std::vector<uint64_t> &maxQuota, std::vector<uint64_t> &adjustQuota,
                     std::vector<uint64_t> &allocQuota, std::vector<uint64_t> &concur)
    {
        mQuota->Show(maxQuota, adjustQuota, allocQuota);
        mConcur->ConcurCount(concur);
    }

    inline bool Switch(const std::string &op = "")
    {
        if (op == "on") {
            mSwitch = true;
        } else if (op == "off") {
            mSwitch = false;
        }
        return mSwitch;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    bool mSwitch = true;
    BioQuotaPtr mQuota = nullptr;
    BioConcurrencyPtr mConcur = nullptr;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // BIO_QOS_H
