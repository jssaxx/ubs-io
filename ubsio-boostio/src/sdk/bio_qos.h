/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef BIO_QOS_H
#define BIO_QOS_H

#include <atomic>
#include <list>
#include <utility>
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

struct QosApplyParam {
    uint64_t startTime;
    const char* key;
    uint64_t size;
    QosApplyParam(uint64_t time, const char *keyName, uint64_t dataSize) : startTime(time),
        key(keyName), size(dataSize) {}
    QosApplyParam(uint64_t time, const char *keyName) : startTime(time), key(keyName), size(0) {}
};

struct IoWaitEntry {
    const std::string key;
    BResult result;
    uint64_t size;
    uint64_t time;
    sem_t sem;

    IoWaitEntry(std::string k, uint64_t allocSize) : key(k), result(BIO_OK), size(allocSize)
    {
        time = Monotonic::TimeSec();
        sem_init(&sem, 0, 0);
    }

    IoWaitEntry(std::string k, uint64_t allocSize, uint64_t originTime) : key(k), result(BIO_OK),
        size(allocSize), time(originTime)
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

    inline void Wake(BResult ret)
    {
        result = ret;
        sem_post(&sem);
    }

    inline BResult Result() const
    {
        return result;
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
        if (mTaskList.empty()) {
            return nullptr;
        }
        auto iter = mTaskList.begin();
        return *iter;
    }

    inline void Pop()
    {
        auto temp = mTaskList.begin();
        mTaskList.pop_front();
    }

    inline bool Empty()
    {
        return mTaskList.empty();
    }

    inline uint32_t Size()
    {
        return mTaskList.size();
    }

private:
    std::list<IoWaitEntry *> mTaskList;
};

class BioQuota;
using BioQuotaPtr = Ref<BioQuota>;
class BioQuota {
public:
    const uint32_t MAX_IO_HANG_COUNT = NO_64;
    const uint64_t QUOTA_MIN_PRELOAD_SIZE = NO_128 * NO_1024 * NO_1024;

    BioQuota(uint32_t nid, uint64_t pid) : mLocalNodeId(nid), mClientId(pid) {}

    ~BioQuota() = default;

    static BioQuotaPtr &Instance(uint32_t nid, uint64_t pid)
    {
        static auto instance = MakeRef<BioQuota>(nid, pid);
        return instance;
    }

    BResult Initialize(uint32_t scene);

    inline bool Enable() const
    {
        return mEnable;
    }

    inline std::unordered_map<uint16_t, IoHangQueue>* GetIoQueueMap()
    {
        return &mIoQueueMap;
    }

    inline std::unordered_map<uint16_t, bool>* GetTaskRunFlag()
    {
        return &mTaskRunFlag;
    }

    inline uint16_t GenerateNodeSet(CmPtInfo *ptEntry)
    {
        std::vector<uint16_t> nodeVec;
        for (auto &item : ptEntry->copys) {
            nodeVec.push_back(item.nodeId);
        }
        uint16_t nodeSet = 0;
        std::sort(nodeVec.begin(), nodeVec.end());
        for (uint32_t idx = 0; idx < nodeVec.size(); idx++) {
            nodeSet += (nodeVec[idx] * idx * NO_10);
        }
        return nodeSet;
    }

    BResult HangIO(const char *key, CmPtInfo *ptEntry, uint16_t nodeSet, IoWaitEntry *entry)
    {
        // 1. 判断是否触发过载熔断.
        auto iter = mIoQueueMap.find(nodeSet);
        if (UNLIKELY(iter == mIoQueueMap.end())) {
            mIoQueueMap.emplace(nodeSet, IoHangQueue());
            iter = mIoQueueMap.find(nodeSet);
        }
        uint32_t count = iter->second.Size();
        if (UNLIKELY(count >= MAX_IO_HANG_COUNT)) {
            CLIENT_LOG_WARN("IO hang is too much, nodeSet:" << nodeSet << ", key:" << key << ", count:" << count);
            return BIO_QUOTA_NOT_ENOUGH;
        }

        // 2. 加入悬挂队列.
        BIO_TRACE_START(SDK_TRACE_QOS_HANG_IO);
        iter->second.Push(entry);
        BIO_TRACE_END(SDK_TRACE_QOS_HANG_IO, BIO_OK);

        // 3. 启动加载写资源配额任务
        ExecutePreloadTask(ptEntry, nodeSet);
        return BIO_OK;
    }

    BResult AllocQuota(const char *key, CmPtInfo *ptEntry, uint64_t size, uint64_t startTime)
    {
        IoWaitEntry entry(key, size, startTime);
        uint16_t nodeSet = GenerateNodeSet(ptEntry);
        {
            WriteLocker<ReadWriteLock> lock(&mLock);
            auto iter = mQuotaMgr.find(nodeSet);
            if (LIKELY(iter != mQuotaMgr.end() && iter->second >= size)) {
                iter->second -= size;
                CLIENT_LOG_DEBUG("Alloc quota success, nodeSet:" << nodeSet << ", key:" << key << ", size:" << size <<
                    ", remain quota:" << iter->second << ".");
                return BIO_OK;
            }

            CLIENT_LOG_DEBUG("Add IO hang queue, nodeSet:" << nodeSet << ", key:" << key << ", size:" << size << ".");
            auto ret = HangIO(key, ptEntry, nodeSet, &entry);
            if (UNLIKELY(ret != BIO_OK)) {
                return ret;
            }
        }
        entry.Wait();
        return entry.Result();
    }

    inline void ExecutePreloadTask(CmPtInfo *ptEntry, uint64_t nodeSet)
    {
        // 1. 更新任务状态, 防止重复执行加载任务.
        auto iter = mTaskRunFlag.find(nodeSet);
        if (iter == mTaskRunFlag.end()) {
            mTaskRunFlag.emplace(nodeSet, true);
        } else if (iter != mTaskRunFlag.end() && iter->second) {
            return;
        } else {
            iter->second = true;
        }

        // 2. 启动任务.
        if (!(mQuotaAllocExecutor->Execute([this, ptEntry, nodeSet]() { AsyncPreloadQuota(ptEntry, nodeSet); }))) {
            CLIENT_LOG_ERROR("Execute preload quota task failed, nodeSet:" << nodeSet << ".");
            WakeForce(nodeSet, true); // 任务启动失败则强制唤醒该nodeSet上的所有悬挂IO进行重试.
        }
    }

    inline void GetKey(uint64_t &nid, uint64_t &cid) const
    {
        nid = static_cast<uint64_t>(mLocalNodeId);
        cid = mClientId;
    }

    void WakeForce(uint16_t nodeSet, bool isLock);

    void RollbackAllocQuotaReq(CmPtInfo *ptEntry, std::vector<uint16_t> nodeVec, std::vector<uint64_t> quotaVec);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    void UpdateQuotaRes(CmPtInfo *ptEntry, uint16_t nodeSet, uint64_t allocQuota);

    BResult SendAllocQuotaRemote(uint16_t dstNid, AllocQuotaRequest &req, uint64_t &expectPreloadSize);

    BResult SendFreeQuotaRemote(uint16_t nodeId, FreeQuotaRequest &req);

    void AsyncPreloadQuota(CmPtInfo *ptEntry, uint16_t nodeSet);

private:
    bool mEnable = false;
    uint32_t mLocalNodeId;
    uint64_t mClientId = 0;
    uint64_t mPreloadSize = 0;
    ReadWriteLock mLock;
    std::unordered_map<uint16_t, uint64_t> mQuotaMgr; // <nodeSet, quota>
    std::unordered_map<uint16_t, IoHangQueue> mIoQueueMap; // <nodeSet, ioHangQue>
    std::unordered_map<uint16_t, bool> mTaskRunFlag; // <nodeSet, state>
    ExecutorServicePtr mQuotaAllocExecutor;

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

    BResult Initialize(uint32_t scene)
    {
        const uint64_t defaultWriteConcur = NO_128;
        const uint64_t defaultReadConcur = NO_1024;
        mOriConcur[QUOTA_WRITE] = (scene == 1) ? NO_32 : defaultWriteConcur; // scene等于1表示是大数据场景.
        mOriConcur[QUOTA_READ] = (scene == 1) ? NO_32 : defaultReadConcur;
        mCurConcur[QUOTA_WRITE] = 0UL;
        mCurConcur[QUOTA_READ] = 0UL;
        CLIENT_LOG_INFO("Initialize concur success, writeConcur:" << mOriConcur[QUOTA_WRITE] <<", readConcur:" <<
            mOriConcur[QUOTA_READ] << ".");
        return BIO_OK;
    }

     inline BResult ApplyConcur(QuotaType type, const char *key)
    {
        IoWaitEntry entry(key, 0);
        {
            WriteLocker<ReadWriteLock> lock(&mLock[type]);
            if (LIKELY(mCurConcur[type] < mOriConcur[type])) {
                mCurConcur[type]++;
                return BIO_OK;
            } else {
                mIoQueue[type].Push(&entry);
                CLIENT_LOG_DEBUG("Concur not enough, need io wait, concur:" << mCurConcur[type] << ", key:" << key);
            }
        }
        entry.Wait();
        return BIO_OK;
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
        CLIENT_LOG_DEBUG("Release concur, need io wake, key:" << entry->key << ".");
        entry->Wake(BIO_OK);
        mIoQueue[type].Pop();
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

    BResult Initialize(uint32_t nodeId, WorkerMode mode, uint32_t scene);

    inline BioQuotaPtr GetQuotaPtr()
    {
        return mQuota;
    }

    inline BResult Apply(uint8_t mode, QuotaType type, QosApplyParam applyParam, CmPtInfo *ptEntry = nullptr)
    {
        if (UNLIKELY(!mQuota->Enable())) {
            return BIO_OK;
        }

        BResult ret = BIO_OK;
        if (UNLIKELY((Monotonic::TimeSec() - applyParam.startTime) >= NO_45 && applyParam.size != 0)) {
            CLIENT_LOG_WARN("QOS apply timeout, key:" << applyParam.key << ", time:" <<
                Monotonic::TimeSec() - applyParam.startTime);
            return BIO_QUOTA_TIMEOUT;
        }
        bool isAllocConcur = false;
        BIO_TRACE_START(SDK_TRACE_QOS_APPLY);
        if (LIKELY(mode & QOS_CONCURRENCY)) {
            ret = mConcur->ApplyConcur(type, applyParam.key);
            isAllocConcur = true;
        }
        if (LIKELY(mode & QOS_QUOTA)) {
            ret = mQuota->AllocQuota(applyParam.key, ptEntry, applyParam.size, applyParam.startTime);
            if (ret != BIO_OK && isAllocConcur) {
                mConcur->ReleaseConcur(type);
            }
        }
        BIO_TRACE_END(SDK_TRACE_QOS_APPLY, BIO_OK);
        return ret;
    }

    inline void Release(uint8_t mode, QuotaType type)
    {
        if (UNLIKELY(!mQuota->Enable())) {
            return;
        }
        BIO_TRACE_START(SDK_TRACE_QOS_RELEASE);
        if (LIKELY(mode & QOS_CONCURRENCY)) {
            mConcur->ReleaseConcur(type);
        }
        BIO_TRACE_END(SDK_TRACE_QOS_RELEASE, BIO_OK);
    }

    inline void GetKey(uint64_t &nid, uint64_t &cid)
    {
        mQuota->GetKey(nid, cid);
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BioQuotaPtr mQuota = nullptr;
    BioConcurrencyPtr mConcur = nullptr;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // BIO_QOS_H
