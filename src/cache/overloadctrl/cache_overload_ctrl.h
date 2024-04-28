/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef CACHE_OVERLOAD_CTRL_H
#define CACHE_OVERLOAD_CTRL_H

#include <cstdint>
#include <memory>
#include <thread>
#include <atomic>

#include "bio_err.h"
#include "bio_execution.h"

namespace ock {
namespace bio {
constexpr uint32_t MAX_OVERLOAD_STAT_CYCLE_NUM = 64;
constexpr uint32_t CACHE_OLC_PERCENT_BASE = 100;
constexpr uint32_t PERCENT_100 = 100;

enum BwStatType {
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

struct OverloadCtrlConfig {
    uint64_t adjustBwRatio;
    uint64_t minWriteReportBwWith;
    uint64_t maxWriteReportBw;
    uint16_t remainConcurBound;
    uint16_t remainConcurTryUp;
};

class CacheOverloadCtrl {
public:
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

    uint64_t GetWriteQuota();

    uint64_t GetReadQuota();

    void Show(std::vector<uint64_t> &writeBwVec, std::vector<uint64_t> &evictBwVec, uint64_t &vmVec);

private:
    uint64_t LowWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc);
    uint64_t MidWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc);
    uint64_t HighWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc);
    uint64_t LimitedWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc);
    uint64_t CalculateWriteQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint64_t memVm);

    uint64_t GetWmStatDirectValue();
    uint64_t GetBwStatAverageValue(BwStatObj &obj);
    void UpdateBwStatValue(BwStatObj &obj, BwStatType type);
    void UpdateCacheStatBw(BwStatType type);
    void OverloadPeriodStatistics();

    void InitBwStatObj(BwStatObj &obj, uint32_t cycleMs, uint32_t cycleNum);
    void InitOverloadConfig();
    void InitOverloadGlbInfo();
    void InitOverloadQuotaInfo();

private:
    std::atomic<bool> startWorker;
    OverloadCtrlGlbInfo mOverloadCtrlGlbInfo;
    OverloadCtrlConfig mOverloadCtrlConfig;
    ExecutorServicePtr mStatisticExecutor;
    uint64_t mMaxWriteQuota = 0;
    std::atomic<uint64_t> mAdjustWQuota;
};
}
}
#endif // CACHE_OVERLOAD_CTRL_H