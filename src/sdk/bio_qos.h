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
enum QosMode {
    QOS_QUOTA = 0,
    QOS_CONCURRENCY = 1,
    QOS_BUTT
};

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
        mOriQuota[QUOTA_WRITE] = writeQuota;
        mOriQuota[QUOTA_READ] = readQuota;
        mBaseQuota[QUOTA_WRITE] = writeQuota;
        mBaseQuota[QUOTA_READ] = readQuota;
        mCurrentQuota[QUOTA_WRITE] = writeQuota;
        mCurrentQuota[QUOTA_READ] = readQuota;
    }

    inline void AllocQuota(uint64_t size, QuotaType type)
    {
        IoWaitEntry entry(size);
        {
            WriteLocker<ReadWriteLock> lock(&mLock[type]);
            if (LIKELY(mCurrentQuota[type] >= size)) {
                mCurrentQuota[type] -= size;
                mConcur[type]++;
                return;
            } else {
                mIoQueue[type].Push(&entry);
                CLIENT_LOG_WARN("Cache quota not enough, need io hang, base quota:" << mBaseQuota[type] <<
                    ", remaining quota:" << mCurrentQuota[type] << ", concur:" << mConcur[type] << ".");
            }
        }
        entry.Wait();
    }

    inline void ReleaseQuota(uint64_t size, QuotaType type)
    {
        WriteLocker<ReadWriteLock> lock(&mLock[type]);
        bool isDispatch = true;
        Recycle(size, type, isDispatch);
        if (LIKELY(isDispatch)) {
            Dispatch(type);
        }
    }

    void UpdateQuota(uint64_t quota, QuotaType type)
    {
        WriteLocker<ReadWriteLock> lock(&mLock[type]);
        if (quota == mCurrentQuota[type]) {
            return;
        }
        if (quota > mBaseQuota[type]) { // 上调配额
            mCurrentQuota[type] += (quota - mBaseQuota[type]);
            Dispatch(type);
        } else if (quota < mBaseQuota[type]) { // 下调配额
            uint64_t diff = mBaseQuota[type] - quota;
            if (mCurrentQuota[type] >= diff) {
                mCurrentQuota[type] -= diff;
            } else {
                mCompensationQuota[type] = (diff - mCurrentQuota[type]);
                mCurrentQuota[type] = 0;
            }
        }
        mBaseQuota[type] = quota;
    }

    inline void Show(std::vector<uint64_t> &ori, std::vector<uint64_t> &base, std::vector<uint64_t> &quota,
        std::vector<uint32_t> &concur)
    {
        for (uint32_t idx = QUOTA_WRITE; idx < QUOTA_BUTT; idx++) {
            {
                ReadLocker<ReadWriteLock> lock(&mLock[idx]);
                ori.emplace_back(mOriQuota[idx]);
                base.emplace_back(mBaseQuota[idx]);
                quota.emplace_back(mCurrentQuota[idx]);
                concur.emplace_back(mConcur[idx]);
            }
        }
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    void Recycle(uint64_t size, QuotaType type, bool &isDispatch);
    void Dispatch(QuotaType type);

private:
    ReadWriteLock mLock[QUOTA_BUTT];
    uint64_t mOriQuota[QUOTA_BUTT] = { 0, 0 };
    uint64_t mBaseQuota[QUOTA_BUTT] = { 0, 0 };
    uint64_t mCurrentQuota[QUOTA_BUTT] = { 0, 0 };
    uint64_t mCompensationQuota[QUOTA_BUTT] = { 0, 0 };

    uint32_t mConcur[QUOTA_BUTT] = { 0, 0};
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
        mBaseConcur[QUOTA_WRITE] = writeConcur;
        mBaseConcur[QUOTA_READ] = readConcur;
        CLIENT_LOG_INFO("Boostio write Concur:" << mBaseConcur[QUOTA_WRITE] << ", read Concur:" <<
            mBaseConcur[QUOTA_READ] << ".");
    }

    void ApplyConcur(QuotaType type)
    {
        IoWaitEntry entry(0);
        {
            WriteLocker<ReadWriteLock> lock(&mLock[type]);
            if (LIKELY(mCurrentConcur[type] < mBaseConcur[type])) {
                mCurrentConcur[type]++;
                return;
            } else {
                mIoQueue[type].Push(&entry);
                CLIENT_LOG_WARN("Concurrency not enough, Current concur:" << mCurrentConcur[type] << ".");
            }
        }
        entry.Wait();
    }

    void ReleaseConcur(QuotaType type)
    {
        WriteLocker<ReadWriteLock> lock(&mLock[type]);
        mCurrentConcur[type]--;
        if (mIoQueue[type].Empty()) {
            return;
        }
        auto entry = mIoQueue[type].Top();
        mCurrentConcur[type]++;
        entry->Wake();
        mIoQueue[type].Pop();
    }

    uint32_t RefCount(uint32_t index)
    {
        ReadLocker<ReadWriteLock> lock(&mLock[index]);
        return mCurrentConcur[index];
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    ReadWriteLock mLock[QUOTA_BUTT];
    uint64_t mBaseConcur[QUOTA_BUTT] = { 0, 0 };
    uint64_t mCurrentConcur[QUOTA_BUTT] = { 0, 0 };
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
        if (mStart) {
            return BIO_OK;
        }

        // 1. 创建资源配额控制
        mQuota = BioQuota::Instance();
        uint64_t writeQuota = writeRes;
        uint64_t readQuota = readRes;
        mQuota->Initialize(writeQuota, readQuota);

        // 2. 创建并发控制
        mConcur = BioConcurrency::Instance();
        mConcur->Initialize(writeConcur, readConcur);

        mStart = true;
        CLIENT_LOG_INFO("Qos initialize success, write quota:" << writeQuota << ", read quota:" << readQuota <<
            ", write concur" << writeConcur << ", read concur:" << readConcur << ".");
        return BIO_OK;
    }

    inline void Apply(QosMode mode, QuotaType type, uint64_t size = 0)
    {
        if (UNLIKELY(!mSwitch)) {
            return;
        }
        (mode == QOS_QUOTA) ? mQuota->AllocQuota(size, type) : mConcur->ApplyConcur(type);
    }

    inline void Release(QosMode mode, QuotaType type, uint64_t size = 0)
    {
        if (UNLIKELY(!mSwitch)) {
            return;
        }
        (mode == QOS_QUOTA) ? mQuota->ReleaseQuota(size, type) : mConcur->ReleaseConcur(type);
    }

    inline void Update(QuotaType type, uint64_t size = 0)
    {
        if (UNLIKELY(!mSwitch)) {
            return;
        }
        mQuota->UpdateQuota(size, type);
    }

    inline void Show(std::vector<uint64_t> &oriQuota, std::vector<uint64_t> &baseQuota, std::vector<uint64_t> &quota,
        std::vector<uint32_t> &concur)
    {
        mQuota->Show(oriQuota, baseQuota, quota, concur);
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
    bool mStart = false;
    bool mSwitch = true;
    BioQuotaPtr mQuota = nullptr;
    BioConcurrencyPtr mConcur = nullptr;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // BIO_QOS_H
