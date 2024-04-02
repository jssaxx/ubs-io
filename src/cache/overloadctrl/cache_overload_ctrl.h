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
#define MAX_OVERLOAD_STAT_CYCLE_NUM (64)

enum BwStatType {
    BW_STAT_FRONT_WRITE = 0,
    BW_STAT_EVICT,
    BW_STAT_BUTT
};

struct BwStatObj {
    uint32_t cycleTime;
    uint32_t cycleNum;
    uint16_t curIdx;
    bool isStatPageCnt;
    uint32_t pageSize;
    std::atomic<uint64_t> curValue;
    uint64_t curStartTime;
    uint64_t hisMaxValue;
    uint64_t calcBwCycleTime; // Start time for calculating the average bandwidth
    uint64_t calcBwCycle;     // Period for calculating the average bandwidth
    uint64_t calcBwValue;     // Calculate the average bandwidth
    uint64_t hisValue[MAX_OVERLOAD_STAT_CYCLE_NUM];
    uint64_t hisStartTime[MAX_OVERLOAD_STAT_CYCLE_NUM];
    uint64_t hisEndTime[MAX_OVERLOAD_STAT_CYCLE_NUM];
};

struct VmStatObj {
    uint64_t cycleTime;
    uint32_t cycleNum;
    uint16_t curIdx;
    uint64_t curStartTime;
    uint64_t calcWmCycleTime; // Start time for calculating the average water level trend
    uint64_t calcWmCycle;     // Period for calculating the average water level trend
    int32_t calcWmValue;      // Calculate the average water level
    uint64_t hisValue[MAX_OVERLOAD_STAT_CYCLE_NUM];
};

struct OverloadCtrlGlbInfo {
    BwStatObj bwStatObj[BW_STAT_BUTT];
    VmStatObj wmStatObj;
};

struct OverloadCtrlConfig {
    uint32_t downPreBwRatio;
    uint64_t overloadAdjustBwRatio;
    uint64_t minWriteReportBwWith;
    uint64_t maxWriteReportBw;
    uint64_t baseAdjustReportBw;
    uint64_t zeroAdjustReportBw;
    uint64_t minReportBw;
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

private:
    void InitBwStatObj(BwStatObj &obj, uint32_t cycleMs, uint32_t cycleNum, uint32_t calcBWCycleMs,
        bool isStatPageCnt);
    void InitWmStatObj(VmStatObj &obj, uint32_t cycleMs, uint32_t cycleNum, uint32_t calcBWCycleMs);
    void InitOverloadConfig();
    void InitOverloadGlbInfo();

    uint64_t GetBwStatAverageValue(BwStatObj &obj);
    int32_t GetCacheWaterMarkDirect(VmStatObj &obj);
    void UpdateBwStatValue(BwStatObj &obj, BwStatType type);
    void UpdateCacheStatBw(BwStatType type);
    void UpdateCacheWaterDirect();
    void OverloadPeriodStatistics();

    uint64_t GetCacheWriteBandwidth();
    uint64_t GetCacheWaterMark();

private:
    std::atomic<bool> startWorker;
    OverloadCtrlGlbInfo mOverloadCtrlGlbInfo;
    OverloadCtrlConfig mOverloadCtrlConfig;
    ExecutorServicePtr mStatisticExecutor;
};
}
}
#endif // CACHE_OVERLOAD_CTRL_H