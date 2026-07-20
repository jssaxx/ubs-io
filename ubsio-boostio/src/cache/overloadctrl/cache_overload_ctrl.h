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

#ifndef CACHE_OVERLOAD_CTRL_H
#define CACHE_OVERLOAD_CTRL_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "bio_err.h"
#include "bio_execution.h"

namespace ock {
namespace bio {
constexpr uint32_t MAX_OVERLOAD_STAT_CYCLE_NUM = 64;
constexpr uint32_t CACHE_OLC_PERCENT_BASE = 100;
constexpr uint32_t PERCENT_100 = 100;

enum BwStatType
{
    BW_STAT_FRONT_WRITE = 0,
    BW_STAT_EVICT_TO_DISK,
    BW_STAT_BUTT
};

struct BwStatObj {
    uint32_t cycleTime;
    uint32_t cycleNum;
    uint16_t curIdx;
    std::atomic<uint64_t> curValue;
    uint64_t curStartTime;
    uint64_t calcBwCycleTime; // 计算平均带宽的起始时间
    uint64_t calcBwCycle;     // 计算平均带宽的周期
    uint64_t calcBwValue;     // 计算的平均带宽值
    uint64_t hisMaxValue;
    uint64_t hisValue[MAX_OVERLOAD_STAT_CYCLE_NUM];
    uint64_t hisStartTime[MAX_OVERLOAD_STAT_CYCLE_NUM];
    uint64_t hisEndTime[MAX_OVERLOAD_STAT_CYCLE_NUM];
};

struct OverloadCtrlGlbInfo {
    BwStatObj bwStatObj[BW_STAT_BUTT];
};

struct QuotaHolder {
    uint32_t nodeId;
    uint64_t clientId;
};

struct QuotaHolderHash {
    size_t operator()(const QuotaHolder &holder) const
    {
        return std::hash<uint64_t>()(static_cast<uint64_t>(holder.nodeId)) ^ std::hash<uint64_t>()(holder.clientId);
    }
};

struct QuotaHolderEqual {
    bool operator()(const QuotaHolder &holder1, const QuotaHolder &holder2) const
    {
        return (holder1.nodeId == holder2.nodeId) && (holder1.clientId == holder2.clientId);
    }
};

class CacheOverloadCtrl {
public:
    const uint32_t DEFAULT_ADJUST_QUOTA_CYCLE = 1000UL;                         // 1s
    const uint64_t MAX_PRELOAD_QUOTA_SIZE = NO_1 * NO_1024 * NO_1024 * NO_1024; // 1GB
    const uint64_t MIN_PRELOAD_QUOTA_SIZE = NO_128 * NO_1024 * NO_1024;         // 128MB

    CacheOverloadCtrl() = default;

    ~CacheOverloadCtrl()
    {
        startWorker = false;
        mStatisticExecutor->Stop();
    }

    static CacheOverloadCtrl &Instance()
    {
        static CacheOverloadCtrl instance;
        return instance;
    }

    BResult Initialize();

    void AddBandwidth(BwStatType type, uint64_t count);

    inline uint64_t GetAvailableQuota() const
    {
        return mWriteQuota;
    }

    inline std::unordered_map<QuotaHolder, uint64_t, QuotaHolderHash, QuotaHolderEqual> *GetHolders()
    {
        return &mHolders;
    }

    BResult AllocQuota(QuotaHolder holder, uint64_t allocSize, uint64_t &expectAllocSize)
    {
        WriteLocker<ReadWriteLock> lock(&mLock);
        if (mWriteQuota < allocSize) {
            LOG_WARN("Cache write quota not enough, remain quota:" << mWriteQuota << ", alloc quota:" << allocSize
                                                                   << ", holder:" << holder.nodeId << "-"
                                                                   << holder.clientId << ".");
            return BIO_QUOTA_NOT_ENOUGH;
        }

        mWriteQuota -= allocSize;
        auto iter = mHolders.find(holder);
        if (UNLIKELY(iter == mHolders.end())) {
            uint64_t size = mHolders.size();
            BIO_TP_START(QUOTA_HOLDER_SIZE_MAX, &size, (NO_8192 * NO_256 + 1));
            BIO_TP_END;
            if (size > MAX_HOLDER_SIZE) { // 限制quota的最大holder数量为8192*256个.
                LOG_WARN("Quota holder is oversize , holder:" << holder.nodeId << "-" << holder.clientId << ".");
                return BIO_QUOTA_NOT_ENOUGH;
            }
            mHolders.emplace(holder, allocSize);
            iter = mHolders.find(holder);
        } else {
            if (UINT64_MAX - iter->second < allocSize) {
                LOG_WARN("Alloc is over uint max , holder:" << holder.nodeId << "-" << holder.clientId << ".");
                return BIO_QUOTA_NOT_ENOUGH;
            }
            iter->second += allocSize;
        }
        // 计算下次申请的期望预期配额资源大小.
        expectAllocSize = GetAdjustWriteQuota(allocSize);
        LOG_DEBUG("Alloc quota success, alloc quota: " << allocSize << ", holder: " << holder.nodeId << "-"
                                                       << holder.clientId << ", remain quota:" << mWriteQuota
                                                       << ", hold quota:" << iter->second
                                                       << ", expect alloc quota:" << expectAllocSize << ".");
        return BIO_OK;
    }

    void ReleaseQuota(const char *key, QuotaHolder holder, uint64_t size, uint32_t proc)
    {
        WriteLocker<ReadWriteLock> lock(&mLock);
        auto iter = mHolders.find(holder);
        if (UNLIKELY(iter == mHolders.end())) {
            LOG_WARN("Not found holder record, holder: " << holder.nodeId << "-" << holder.clientId << ".");
            return;
        }
        iter->second = (iter->second > size) ? iter->second - size : 0;
        LOG_DEBUG("Release quota success, holder: " << holder.nodeId << "-" << holder.clientId << ", size:" << size
                                                    << ", hold quota:" << iter->second << ", proc:" << proc
                                                    << ", key:" << key << ".");
    }

    void FreeQuota(uint64_t size, uint32_t proc)
    {
        WriteLocker<ReadWriteLock> lock(&mLock);
        if (UINT64_MAX - mWriteQuota < size) {
            LOG_WARN("free size incorrect , add mWriteQuota over uint64 max.");
            return;
        }
        mWriteQuota += size;
        mWriteQuota = std::min(mWriteQuota, mLimitWriteQuota);
        LOG_DEBUG("Free quota success, size:" << size << ", remain quota:" << mWriteQuota << ", proc:" << proc << ".");
    }

    void RecycleQuota(QuotaHolder holder)
    {
        WriteLocker<ReadWriteLock> lock(&mLock);
        auto iter = mHolders.find(holder);
        if (iter == mHolders.end()) {
            LOG_INFO("Not found holder record, holder: " << holder.nodeId << "-" << holder.clientId << ".");
            return;
        }
        uint64_t recycleQuota = iter->second;
        mHolders.erase(iter);
        if (UINT64_MAX - mWriteQuota >= recycleQuota) {
            mWriteQuota += recycleQuota;
        }
        LOG_INFO("Recycle quota success, holder: " << holder.nodeId << "-" << holder.clientId
                                                   << ", recycle quota:" << recycleQuota << ".");
    }

    void Show(uint64_t &vmVec, uint64_t &totalQuota, uint64_t &remainQuota,
              std::unordered_map<QuotaHolder, uint64_t, QuotaHolderHash, QuotaHolderEqual> &holders);

    uint64_t LowWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc);
    uint64_t MidWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc);
    uint64_t HighWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc);
    uint64_t CalculateWriteQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint64_t vm);

private:
    uint64_t GetAdjustWriteQuota(uint64_t allocSize);
    uint64_t GetWmStatDirectValue();
    uint64_t GetBwStatAverageValue(BwStatObj &obj);
    void UpdateBwStatValue(BwStatObj &obj, BwStatType type);
    void UpdateCacheStatBw(BwStatType type);
    void OverloadPeriodStatistics();

    void InitBwStatObj(BwStatObj &obj, uint32_t cycleMs, uint32_t cycleNum);
    void InitOverloadGlbInfo();
    void InitQuotaManager();

private:
    // overload info statics
    std::atomic<bool> startWorker;
    OverloadCtrlGlbInfo mOverloadCtrlGlbInfo;
    ExecutorServicePtr mStatisticExecutor;

    // quota manager
    uint64_t mLimitWriteQuota = 0;
    uint64_t mWriteQuota = 0;
    ReadWriteLock mLock;
    std::unordered_map<QuotaHolder, uint64_t, QuotaHolderHash, QuotaHolderEqual> mHolders;
    uint64_t MAX_HOLDER_SIZE = NO_8192 * NO_256;

    // overload ctrl
    std::atomic<uint64_t> mAdjustWQuota;
    std::mutex mAdjustLock;
    uint64_t mAdjustCycleTime = 0;
    uint32_t mAdjustCycle;
};
} // namespace bio
} // namespace ock
#endif // CACHE_OVERLOAD_CTRL_H