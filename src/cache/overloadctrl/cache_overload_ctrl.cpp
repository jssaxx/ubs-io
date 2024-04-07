/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <functional>
#include <fstream>
#include <utility>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <iostream>
#include <sstream>

#include "bio_log.h"
#include "bio_types.h"
#include "bio_monotonic.h"
#include "cache_overload_ctrl.h"

namespace ock {
namespace bio {
void CacheOverloadCtrl::AddBandwidth(BwStatType type, uint64_t count)
{
    mOverloadCtrlGlbInfo.bwStatObj[type].curValue += count;
}

uint64_t CacheOverloadCtrl::GetBwStatAverageValue(BwStatObj &obj)
{
    uint16_t curIdx = (obj.curIdx + MAX_OVERLOAD_STAT_CYCLE_NUM - 1) % MAX_OVERLOAD_STAT_CYCLE_NUM;
    uint64_t startTime = obj.hisStartTime[curIdx];
    uint64_t endTime = obj.hisEndTime[curIdx];
    uint64_t maxValue = 0;
    uint64_t totalValue = 0;

    for (uint16_t idx = 0; idx < obj.cycleNum; idx++) {
        startTime = obj.hisStartTime[curIdx];
        totalValue += obj.hisValue[curIdx];
        if (endTime < startTime + 5000U) {
            if (obj.isStatPageCnt) {
                maxValue = std::max<uint64_t>(maxValue, ((obj.hisValue[curIdx] * 1000U * obj.pageSize) /
                    std::max<uint64_t>(obj.hisEndTime[curIdx] - obj.hisStartTime[curIdx], 1)));
            } else {
                maxValue = obj.hisValue[curIdx];
            }
        }
        curIdx = (curIdx + MAX_OVERLOAD_STAT_CYCLE_NUM - 1) % MAX_OVERLOAD_STAT_CYCLE_NUM;
    }

    obj.hisMaxValue = maxValue;
    if (totalValue == 0) {
        return 0;
    }

    if (obj.isStatPageCnt) {
        totalValue = ((totalValue) * 1000U * obj.pageSize) / (std::max<uint64_t>((endTime - startTime), 1));
    }
    return totalValue;
}

void CacheOverloadCtrl::UpdateBwStatValue(BwStatObj &obj, BwStatType type)
{
    const std::string bwTypeStr[BW_STAT_BUTT] = { "write", "evict" };
    uint64_t curTime = Monotonic::TimeMs();
    if (curTime >= (obj.calcBwCycleTime + obj.calcBwCycle)) {
        obj.calcBwCycleTime = curTime;
        obj.calcBwValue = GetBwStatAverageValue(obj);
    } else {
        return;
    }

    uint64_t hisIdx = obj.curIdx;
    uint64_t curValue = obj.curValue.load();
    obj.hisStartTime[hisIdx] = obj.curStartTime;
    obj.hisValue[hisIdx] = curValue;
    obj.hisEndTime[hisIdx] = curTime;
    obj.curIdx = (hisIdx + 1) % MAX_OVERLOAD_STAT_CYCLE_NUM;
    obj.curValue -= curValue;
    obj.curStartTime = curTime;
}

void CacheOverloadCtrl::UpdateCacheStatBw(BwStatType type)
{
    UpdateBwStatValue(mOverloadCtrlGlbInfo.bwStatObj[type], type);
}

int32_t CacheOverloadCtrl::GetCacheWaterMarkDirect(VmStatObj &obj)
{
    uint16_t curIdx = (obj.curIdx + MAX_OVERLOAD_STAT_CYCLE_NUM - 1) % MAX_OVERLOAD_STAT_CYCLE_NUM;
    uint16_t hisIdx = (obj.curIdx + MAX_OVERLOAD_STAT_CYCLE_NUM - 2) % MAX_OVERLOAD_STAT_CYCLE_NUM;
    uint64_t hisMW = 0;

    for (uint16_t idx = 0; idx < obj.cycleNum - 1; idx++) {
        hisMW += obj.hisValue[hisIdx];
        hisIdx = (hisIdx + MAX_OVERLOAD_STAT_CYCLE_NUM - 1) % MAX_OVERLOAD_STAT_CYCLE_NUM;
    }
    if (hisMW == 0) {
        return 0;
    }

    int32_t wmDir = 0;
    hisMW = hisMW / (static_cast<uint64_t>(std::max<uint64_t>((obj.cycleNum - 1), 1)));
    if (obj.hisValue[curIdx] > hisMW) {
        wmDir = 1;
    } else if (obj.hisValue[curIdx] < hisMW) {
        wmDir = -1;
    } else {
        wmDir = 0;
    }
    return wmDir;
}

void CacheOverloadCtrl::UpdateCacheWaterDirect()
{
    VmStatObj &obj = mOverloadCtrlGlbInfo.wmStatObj;
    uint64_t curTime = Monotonic::TimeMs();
    uint64_t hisIdx;

    if (curTime >= (obj.calcWmCycleTime + obj.calcWmCycle)) {
        obj.calcWmCycleTime = curTime;
        obj.calcWmValue = GetCacheWaterMarkDirect(obj);
    } else {
        return;
    }

    hisIdx = obj.curIdx;
    obj.hisValue[hisIdx] = GetCacheWaterMark();
    obj.curIdx = (hisIdx + 1) % MAX_OVERLOAD_STAT_CYCLE_NUM;
    obj.curStartTime = curTime;
}

void CacheOverloadCtrl::OverloadPeriodStatistics()
{
    LOG_INFO("Cache overload ctrl period statistics start.");
    constexpr uint64_t period = 5; // 5s
    uint64_t startTime = Monotonic::TimeSec();
    while (true) {
        if (!startWorker) {
            LOG_INFO("Cache overload ctrl period statistics end.");
            break;
        }
        if (Monotonic::TimeSec() - startTime < period) {
            sleep(NO_1);
            continue;
        }
        for (auto idx = static_cast<uint16_t>(BW_STAT_FRONT_WRITE); idx < BW_STAT_BUTT; idx++) {
            UpdateCacheStatBw(static_cast<BwStatType>(idx));
        }
        UpdateCacheWaterDirect();
        startTime = Monotonic::TimeSec();
    }
}

uint64_t CacheOverloadCtrl::GetCacheWriteBandwidth()
{
    return mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_FRONT_WRITE].curValue.load();
}

uint64_t CacheOverloadCtrl::GetCacheWaterMark()
{
    return 0;
}

void CacheOverloadCtrl::InitOverloadConfig()
{
    mOverloadCtrlConfig.downPreBwRatio = NO_5;                                         // 下调百分比5%
    mOverloadCtrlConfig.overloadAdjustBwRatio = NO_10;                                 // 过载调整百分比10%
    mOverloadCtrlConfig.minWriteReportBwWith = (static_cast<uint64_t>(NO_1)) << NO_30; // 中低水位写带宽最低上报值，1G
    mOverloadCtrlConfig.baseAdjustReportBw = (static_cast<uint64_t>(NO_4)) << NO_10; // 基本的带宽调整值，4K
    mOverloadCtrlConfig.zeroAdjustReportBw = (NO_1024 * NO_1024);                    // 保底的带宽调整值，1M
    mOverloadCtrlConfig.minReportBw = (NO_1024 * NO_1024);
    mOverloadCtrlConfig.maxWriteReportBw =
        (static_cast<uint64_t>(NO_1024 * NO_5)) << NO_10; // 上调带宽时的上限值1GB，避免调太大
    mOverloadCtrlConfig.remainConcurBound = NO_100;
    mOverloadCtrlConfig.remainConcurTryUp = 0;
}

void CacheOverloadCtrl::InitBwStatObj(BwStatObj &obj, uint32_t cycleMs, uint32_t cycleNum, uint32_t calcBWCycleMs,
    bool isStatPageCnt)
{
    obj.cycleTime = cycleMs;
    obj.cycleNum = cycleNum;
    obj.curIdx = 0;
    obj.isStatPageCnt = isStatPageCnt;
    obj.pageSize = 0;
    obj.curValue.store(0UL);
    obj.curStartTime = 0UL;
    obj.hisMaxValue = 0UL;
    obj.calcBwCycleTime = 0UL;
    obj.calcBwCycle = calcBWCycleMs;
    obj.calcBwValue = 0UL;
    for (uint16_t index = 0; index < MAX_OVERLOAD_STAT_CYCLE_NUM; index++) {
        obj.hisValue[index] = 0UL;
        obj.hisStartTime[index] = 0UL;
        obj.hisEndTime[index] = 0UL;
    }
}

void CacheOverloadCtrl::InitWmStatObj(VmStatObj &obj, uint32_t cycleMs, uint32_t cycleNum, uint32_t calcBWCycleMs)
{
    obj.cycleTime = cycleMs;
    obj.cycleNum = cycleNum;
    obj.curIdx = 0;
    obj.curStartTime = 0UL;
    obj.calcWmCycleTime = 0UL;
    obj.calcWmCycle = calcBWCycleMs;
    obj.calcWmValue = 0;
    for (uint16_t index = 0; index < MAX_OVERLOAD_STAT_CYCLE_NUM; index++) {
        obj.hisValue[index] = 0UL;
    }
}

void CacheOverloadCtrl::InitOverloadGlbInfo()
{
    InitBwStatObj(mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_FRONT_WRITE], 1000U, 10U, 1000U, false);
    InitBwStatObj(mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_EVICT], 1000U, 10U, 1000U, false);
    InitWmStatObj(mOverloadCtrlGlbInfo.wmStatObj, 500U, 5U, 1000U);
}

BResult CacheOverloadCtrl::Initialize()
{
    InitOverloadConfig();
    InitOverloadGlbInfo();

    mStatisticExecutor = ExecutorService::Create(NO_1, NO_1024);
    ChkTrue(mStatisticExecutor != nullptr, BIO_ALLOC_FAIL, "Failed to create overload info statistic executor.");
    mStatisticExecutor->SetThreadName("cache-overload-statis");
    auto ret = mStatisticExecutor->Start();
    if (!ret) {
        LOG_ERROR("Failed to start overload info statistic executor.");
        return BIO_INNER_ERR;
    }
    ret = mStatisticExecutor->Execute([this]() { OverloadPeriodStatistics(); });
    if (!ret) {
        LOG_ERROR("Set execute function failed.");
        return BIO_INNER_ERR;
    }

    startWorker = true;
    return BIO_OK;
}
}
}