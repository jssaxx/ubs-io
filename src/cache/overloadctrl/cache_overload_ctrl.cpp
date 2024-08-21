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
uint64_t CacheOverloadCtrl::LowWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc)
{
    if (frontWriteBw <= evict2DiskBw) {
        proc = NO_1;
        uint64_t adjust = mAdjustWQuota.load() + ROUND_DOWN(GetAvailableQuota() / NO_100 * NO_2, NO_4096);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, MAX_PRELOAD_QUOTA_SIZE));
    } else if (frontWriteBw <= NO_4 * evict2DiskBw) {
        proc = NO_2;
        uint64_t adjust = mAdjustWQuota.load() + ROUND_DOWN(GetAvailableQuota() / NO_100, NO_4096);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, MAX_PRELOAD_QUOTA_SIZE));
    } else {
        proc = NO_3;
        uint64_t adjust = mAdjustWQuota.load() - ROUND_DOWN(mAdjustWQuota.load() / NO_100, NO_4096);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, MIN_PRELOAD_QUOTA_SIZE));
    }
    return mAdjustWQuota.load();
}

uint64_t CacheOverloadCtrl::MidWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc)
{
    if (frontWriteBw <= evict2DiskBw) {
        proc = NO_1;
        uint64_t adjust = mAdjustWQuota.load() + ROUND_DOWN(GetAvailableQuota() / NO_100, NO_4096);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, MAX_PRELOAD_QUOTA_SIZE));
    } else if (frontWriteBw <= NO_4 * evict2DiskBw) {
        proc = NO_2;
        uint64_t adjust = mAdjustWQuota.load() - ROUND_DOWN(mAdjustWQuota.load() / NO_100 * NO_2, NO_4096);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, MIN_PRELOAD_QUOTA_SIZE));
    } else {
        proc = NO_3;
        uint64_t adjust = mAdjustWQuota.load() - ROUND_DOWN(mAdjustWQuota.load() / NO_100 * NO_5, NO_4096);
        mAdjustWQuota.store(std::max<uint64_t>(adjust, MIN_PRELOAD_QUOTA_SIZE));
    }
    return mAdjustWQuota.load();
}

uint64_t CacheOverloadCtrl::HighWaterLevelQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint32_t &proc)
{
    if (frontWriteBw <= evict2DiskBw) {
        proc = NO_1;
        uint64_t adjust = mAdjustWQuota.load() + ROUND_DOWN(GetAvailableQuota() / NO_100, NO_4096);
        mAdjustWQuota.store(std::min<uint64_t>(adjust, MAX_PRELOAD_QUOTA_SIZE));
    } else {
        proc = NO_2;
        mAdjustWQuota.store(MIN_PRELOAD_QUOTA_SIZE);
    }
    return mAdjustWQuota.load();
}

uint64_t CacheOverloadCtrl::CalculateWriteQuota(uint64_t frontWriteBw, uint64_t evict2DiskBw, uint64_t vm)
{
    uint32_t proc = 0;
    uint64_t adjustWQuota = 0;
    if (vm <= NO_50) {
        adjustWQuota = LowWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    } else if (vm < NO_80) {
        adjustWQuota = MidWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    } else {
        adjustWQuota = HighWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    }
    LOG_DEBUG("Calculate real write quota, frontWriteBw:" << frontWriteBw << ", evict2DiskBw:" << evict2DiskBw <<
        ", waterLevel:" << vm << ", adjustWQuota:" << adjustWQuota << ", proc:" << proc << ".");
    return adjustWQuota;
}

uint64_t CacheOverloadCtrl::GetAdjustWriteQuota(uint64_t allocSize)
{
    static bool dynamicAdjust = true;
    if (!dynamicAdjust) {
        return allocSize;
    }

    // 1. 间隔1秒钟做一次申请配额资源大小的动态调整, 避免动态调整频率过高.
    bool isAdjust = false;
    {
        std::lock_guard<std::mutex> locker(mAdjustLock);
        uint64_t curTime = Monotonic::TimeMs();
        if (curTime - mAdjustCycleTime >= mAdjustCycle) {
            isAdjust = true;
            mAdjustCycleTime = curTime;
        }
    }

    // 2. 根据前端写入带宽、后端刷盘带宽和水位情况来综合得到实际申请配额的大小.
    uint64_t retAdjustQuota = 0;
    if (LIKELY(isAdjust)) {
        uint64_t frontWriteBw = mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_FRONT_WRITE].calcBwValue;
        uint64_t evict2DiskBw = mOverloadCtrlGlbInfo.bwStatObj[BW_STAT_EVICT_TO_DISK].calcBwValue;
        retAdjustQuota = CalculateWriteQuota(frontWriteBw, evict2DiskBw, GetWmStatDirectValue());
    } else {
        retAdjustQuota = mAdjustWQuota.load();
    }
    return retAdjustQuota;
}

void CacheOverloadCtrl::AddBandwidth(BwStatType type, uint64_t count)
{
    mOverloadCtrlGlbInfo.bwStatObj[type].curValue += count;
}

void CacheOverloadCtrl::Show(uint64_t &vmVec, uint64_t &totalQuota, uint64_t &remainQuota,
    std::unordered_map<QuotaHolder, uint64_t, QuotaHolderHash, QuotaHolderEqual> &holders)
{
    vmVec = GetWmStatDirectValue();
    totalQuota = mLimitWriteQuota;
    remainQuota = mWriteQuota;
    holders = mHolders;
}

uint64_t CacheOverloadCtrl::GetWmStatDirectValue()
{
    CacheResDescription desc = { 0 };
    Cache::Instance().GetCacheResources(desc, WRITE_CACHE);
    auto mVm = std::min<uint64_t>(PERCENT_100, desc.memUsedSize * CACHE_OLC_PERCENT_BASE / desc.memCapacity);
    auto dVm = std::min<uint64_t>(PERCENT_100, desc.diskUsedSize * CACHE_OLC_PERCENT_BASE / desc.diskCapacity);
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
    LOG_DEBUG("Cache overload ctrl period statistics start.");
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
    LOG_DEBUG("Cache overload ctrl period statistics end.");
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

void CacheOverloadCtrl::InitQuotaManager()
{
    CacheResDescription desc = { 0 };
    Cache::Instance().GetCacheResources(desc, WRITE_CACHE);
    mLimitWriteQuota = static_cast<uint64_t>(desc.memCapacity / NO_100 * NO_80); // 预留空间用于meta flow的存放缓存索引元数据.
    mWriteQuota = mLimitWriteQuota;
    mAdjustWQuota.store(NO_128 * NO_1024 * NO_1024);
    LOG_INFO("Initialize write cache quota pool success, write quota:" << mWriteQuota << ".");
}

BResult CacheOverloadCtrl::Initialize()
{
    // 1. 初始化过载采集配置信息.
    InitOverloadGlbInfo();
    // 2. 初始化写缓存配额管理.
    InitQuotaManager();

    if (!(BioConfig::Instance()->GetDaemonConfig().enableQos)) {
        return BIO_OK;
    }

    // 3. 创建Executor, 用于周期性采集过载信息.
    mStatisticExecutor = ExecutorService::Create(NO_1, NO_1024);
    ChkTrue(mStatisticExecutor != nullptr, BIO_ALLOC_FAIL, "Failed to create overload info statistic executor.");
    mStatisticExecutor->SetThreadName("cache-overload-stat");
    if (!(mStatisticExecutor->Start())) {
        LOG_ERROR("Failed to start overload info statistic executor.");
        return BIO_INNER_ERR;
    }
    startWorker = true;
    if (!(mStatisticExecutor->Execute([this]() { OverloadPeriodStatistics(); }))) {
        LOG_ERROR("Set execute function failed.");
        startWorker = false;
        return BIO_INNER_ERR;
    }
    return BIO_OK;
}
}
}