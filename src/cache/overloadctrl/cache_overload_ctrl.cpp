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
#include "cache.h"
#include "cache_overload_ctrl.h"

namespace ock {
namespace bio {
/** 低水位过载控制调整策略
 * case1：前端写入带宽 <= 后端内存淘汰到磁盘的带宽, 则写配额上调最大值的30%, 但不超过最大配额;
 * case2: 前端写入带宽 <= 2*后端内存淘汰到磁盘的带宽, 则写配额上调最大值的20%, 但不超过最大配额;
 * case3: 前端写入带宽 <= 4*后端内存淘汰到磁盘的带宽, 则写配额上调最大值的10%, 但不超过最大配额;
 * case4: 前端写入带宽 >= 4*后端内存淘汰到磁盘的带宽, 则写配额下调最大值的10%, 但不超过最大配额的2/3;
 */
uint64_t CacheOverloadCtrl::LowWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc)
{
    if (frontWriteBw <= evict2DiskBw) {
        proc = NO_1;
        uint32_t radio = NO_3 * mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota + (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, mMaxWriteQuota));
    } else if (frontWriteBw <= NO_2 * evict2DiskBw) {
        proc = NO_2;
        uint32_t radio = NO_2 * mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota + (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, mMaxWriteQuota));
    } else if (frontWriteBw <= NO_4 * evict2DiskBw) {
        proc = NO_3;
        uint32_t radio = mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota + (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, mMaxWriteQuota));
    } else {
        proc = NO_4;
        uint32_t radio = mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota - (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, (mMaxWriteQuota * NO_2 / NO_3)));
    }
    return mAdjustWQuota.load();
}

/** 中水位过载控制调整策略
 * case1：前端写入带宽 <= 后端内存淘汰到磁盘的带宽, 则写配额上调最大值的10%, 但不超过最大配额;
 * case2: 前端写入带宽 <= 2*后端内存淘汰到磁盘的带宽, 则写配额下调最大值的20%, 但不超过最大配额的1/3;
 * case3: 前端写入带宽 <= 4*后端内存淘汰到磁盘的带宽, 则写配额下调最大值的30%, 但不超过最大配额的1/3;
 * case4: 前端写入带宽 >= 4*后端内存淘汰到磁盘的带宽, 则写配额下调最大值的40%, 但不超过最大配额的1/3;
 */
uint64_t CacheOverloadCtrl::MidWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc)
{
    if (frontWriteBw <= evict2DiskBw) {
        proc = NO_1;
        uint32_t radio = mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota + (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, mMaxWriteQuota));
    } else if (frontWriteBw <= NO_2 * evict2DiskBw) {
        proc = NO_2;
        uint32_t radio = NO_2 * mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota - (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, mMaxWriteQuota / NO_3));
    } else if (frontWriteBw <= NO_4 * evict2DiskBw) {
        proc = NO_3;
        uint32_t radio = NO_3 * mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota - (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, mMaxWriteQuota / NO_3));
    } else {
        proc = NO_4;
        uint32_t radio = NO_4 * mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota - (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, mMaxWriteQuota / NO_3));
    }
    return mAdjustWQuota.load();
}

/** 高水位过载控制调整策略
 * case1：前端写入带宽 <= 后端内存淘汰到磁盘的带宽, 则写配额上调最大值的10%, 但不超过最大配额
 * case2: 前端写入带宽 <= 2*后端内存淘汰到磁盘的带宽, 则写配额下调最大值的20%, 但不超过最大配额的1/5;
 * case3: 前端写入带宽 <= 4*后端内存淘汰到磁盘的带宽, 则写配额下调最大值的30%, 但不超过最大配额的1/5;
 * case4: 前端写入带宽 >= 4*后端内存淘汰到磁盘的带宽, 则写配额下调最大值的40%, 但不超过最大配额的1/5;
 */
uint64_t CacheOverloadCtrl::HighWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc)
{
    if (frontWriteBw <= evict2DiskBw) {
        proc = NO_1;
        uint32_t radio = mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota + (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, mMaxWriteQuota));
    } else if (frontWriteBw <= NO_2 * evict2DiskBw) {
        proc = NO_2;
        uint32_t radio = NO_2 * mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota - (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, mMaxWriteQuota / NO_5));
    } else if (frontWriteBw <= NO_4 * evict2DiskBw) {
        proc = NO_3;
        uint32_t radio = NO_3 * mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota - (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, mMaxWriteQuota / NO_5));
    } else {
        proc = NO_4;
        uint32_t radio = NO_4 * mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota - (mMaxWriteQuota * radio / NO_100);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, mMaxWriteQuota / NO_5));
    }
    return mAdjustWQuota.load();
}

/** 红线水位过载控制调整策略
 * case1：前端写入带宽 <= 后端内存淘汰到磁盘的带宽, 则写配额上调最大值的10%, 但不超过最大配额;
 * case2: 前端写入带宽 <= 2*后端内存淘汰到磁盘的带宽, 则写配额调整到1G;
 */
uint64_t CacheOverloadCtrl::LimitedWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc)
{
    if (frontWriteBw <= evict2DiskBw) {
        proc = NO_1;
        uint32_t radio = mOverloadCtrlConfig.adjustBwRatio;
        uint64_t adjust = mAdjustWQuota + (mAdjustWQuota * radio / NO_100);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, mMaxWriteQuota));
    } else {
        proc = NO_2;
        mAdjustWQuota.store(mOverloadCtrlConfig.minWriteReportBwWith);
    }
    return mAdjustWQuota.load();
}

uint64_t CacheOverloadCtrl::CalculateWriteQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint64_t vm)
{
    if (vm <= NO_50) {
        return mMaxWriteQuota;
    }
    uint32_t proc = 0;
    uint64_t adjustWQuota = 0;
    if (vm < NO_70) {
        adjustWQuota = LowWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    } else if (vm < NO_80) {
        adjustWQuota = MidWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    } else if (vm < NO_90) {
        adjustWQuota = HighWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    } else {
        adjustWQuota = LimitedWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    }
    LOG_DEBUG("[QOS]Calculate write quota, frontWriteBw:" << frontWriteBw << ", evict2DiskBw:" << evict2DiskBw <<
        ", waterLevel:" << vm << ", adjustWQuota:" << adjustWQuota << ", proc:" << proc << ".");
    return adjustWQuota;
}

uint64_t CacheOverloadCtrl::GetWriteQuota()
{
    uint64_t retAdjustQuota = 0;
    bool isAdjust = false;
    {
        std::lock_guard<std::mutex> locker(mAdjustLock);
        uint64_t curTime = Monotonic::TimeMs();
        if (curTime - mAdjustCycleTime >= mAdjustCycle) {
            isAdjust = true;
            mAdjustCycleTime = curTime;
        }
    }
    if (isAdjust) {
        uint64_t frontWriteBw = mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_FRONT_WRITE].calcBwValue;
        uint64_t evict2DiskBw = mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_EVICT_TO_DISK].calcBwValue;
        uint64_t vm = GetWmStatDirectValue();
        retAdjustQuota = CalculateWriteQuota(frontWriteBw, evict2DiskBw, vm);
    } else {
        retAdjustQuota = mAdjustWQuota.load();
    }
    return retAdjustQuota;
}

void CacheOverloadCtrl::AddBandwidth(BwStatType type, uint64_t count)
{
    mOverloadCtrlGlbInfo.bwStatObj[type].curValue += count;
}

void CacheOverloadCtrl::Show(std::vector<uint64_t> &writeBwVec, std::vector<uint64_t> &evictBwVec, uint64_t &vmVec)
{
    BwStatObj &writeObj = mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_FRONT_WRITE];
    for (uint32_t idx = 0; idx < MAX_OVERLOAD_STAT_CYCLE_NUM; idx++) {
        writeBwVec.emplace_back(writeObj.hisValue[idx]);
    }
    BwStatObj &evictObj = mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_EVICT_TO_DISK];
    for (uint32_t idx = 0; idx < MAX_OVERLOAD_STAT_CYCLE_NUM; idx++) {
        evictBwVec.emplace_back(evictObj.hisValue[idx]);
    }
    vmVec = GetWmStatDirectValue();
}

uint64_t CacheOverloadCtrl::GetWmStatDirectValue()
{
    CacheResDescription desc = { 0 };
    Cache::Instance().GetCacheResources(desc, WRITE_CACHE);
    auto mVm = std::min<uint64_t>(PERCENT_100, desc.memUsedSize * CACHE_OLC_PERCENT_BASE / desc.memCapacity / NO_2);
    auto dVm = std::min<uint64_t>(PERCENT_100, desc.diskUsedSize * CACHE_OLC_PERCENT_BASE / desc.diskCapacity / NO_2);
    uint64_t retWm = std::max<uint64_t>(mVm, dVm);
    return retWm;
}

uint64_t CacheOverloadCtrl::GetBwStatAverageValue(BwStatObj &obj)
{
    uint16_t curIdx = (obj.curIdx + MAX_OVERLOAD_STAT_CYCLE_NUM - 1) % MAX_OVERLOAD_STAT_CYCLE_NUM;
    uint64_t totalValue = 0;
    for (uint16_t idx = 0; idx < obj.cycleNum; idx++) {
        totalValue += obj.hisValue[curIdx];
        curIdx = (curIdx + MAX_OVERLOAD_STAT_CYCLE_NUM - 1) % MAX_OVERLOAD_STAT_CYCLE_NUM;
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
        if (obj.calcBwValue != 0) {
            LOG_DEBUG("[QOS]Update bandwidth, " << bwTypeStr[type] << " average value:" << obj.calcBwValue << ".");
        }
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

void CacheOverloadCtrl::OverloadPeriodStatistics()
{
    LOG_DEBUG("[QOS]Cache overload ctrl period statistics start.");
    constexpr uint64_t period = 1;
    uint64_t startTime = Monotonic::TimeSec();
    while (true) {
        if (!startWorker) {
            break;
        }
        if (Monotonic::TimeSec() - startTime < period) {
            sleep(1);
            continue;
        }
        for (auto idx = static_cast<uint16_t>(BW_STAT_FRONT_WRITE); idx < BW_STAT_BUTT; idx++) {
            UpdateCacheStatBw(static_cast<BwStatType>(idx));
        }
        startTime = Monotonic::TimeSec();
    }
    LOG_DEBUG("[QOS]Cache overload ctrl period statistics end.");
}

void CacheOverloadCtrl::InitOverloadConfig()
{
    mOverloadCtrlConfig.adjustBwRatio = 10UL;              // 调整百分比10%
    mOverloadCtrlConfig.minWriteReportBwWith = 1UL << 30U; // 写带宽最低限制1G
    mOverloadCtrlConfig.maxWriteReportBw = 1UL << 30U;     // 写带宽最大上调值1GB
    mOverloadCtrlConfig.remainConcurBound = 128UL;
    mOverloadCtrlConfig.remainConcurTryUp = 0;
}

void CacheOverloadCtrl::InitBwStatObj(BwStatObj &obj, uint32_t cycleMs, uint32_t cycleNum)
{
    obj.cycleTime = cycleMs;
    obj.cycleNum = cycleNum;
    obj.curIdx = 0;
    obj.curValue.store(0UL);
    obj.curStartTime = 0UL;
    obj.calcBwCycleTime = 0UL;
    obj.calcBwCycle = cycleMs;
    obj.calcBwValue = 0UL;
    for (uint16_t index = 0; index < MAX_OVERLOAD_STAT_CYCLE_NUM; index++) {
        obj.hisValue[index] = 0UL;
        obj.hisStartTime[index] = 0UL;
        obj.hisEndTime[index] = 0UL;
    }
}

void CacheOverloadCtrl::InitOverloadGlbInfo()
{
    InitBwStatObj(mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_FRONT_WRITE], NO_1000, NO_10);
    InitBwStatObj(mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_EVICT_TO_DISK], NO_1000, NO_10);
    mAdjustCycleTime = Monotonic::TimeMs();
    mAdjustCycle = DEFAULT_ADJUST_QUOTA_CYCLE;
}

void CacheOverloadCtrl::InitOverloadQuotaInfo()
{
    CacheResDescription desc = { 0 };
    Cache::Instance().GetCacheResources(desc, WRITE_CACHE);
    mMaxWriteQuota = desc.memCapacity;
    mAdjustWQuota.store(mMaxWriteQuota);
    LOG_DEBUG("[QOS]Olc init success, write quota:" << mMaxWriteQuota << ", adjust quota:" << mAdjustWQuota << ".");
}

BResult CacheOverloadCtrl::Initialize()
{
    // 1. 初始化过载调控策略配置信息
    InitOverloadConfig();
    // 2. 初始化过载采集配置信息
    InitOverloadGlbInfo();
    // 3. 初始化资源配额
    InitOverloadQuotaInfo();

    // 4. 创建Executor, 用于周期性采集过载信息
    mStatisticExecutor = ExecutorService::Create(NO_1, NO_1024);
    ChkTrue(mStatisticExecutor != nullptr, BIO_ALLOC_FAIL, "[QOS]Failed to create overload info statistic executor.");
    mStatisticExecutor->SetThreadName("cache-overload-stat");
    if (!(mStatisticExecutor->Start())) {
        LOG_ERROR("[QOS]Failed to start overload info statistic executor.");
        return BIO_INNER_ERR;
    }
    if (!(mStatisticExecutor->Execute([this]() { OverloadPeriodStatistics(); }))) {
        LOG_ERROR("[QOS]Set execute function failed.");
        return BIO_INNER_ERR;
    }

    startWorker = true;
    return BIO_OK;
}
}
}