/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Create Date : 2025
 */

#ifndef BOOST_IO_PROMETHEUS_MANAGER_H
#define BOOST_IO_PROMETHEUS_MANAGER_H


#include <thread>
#include <atomic>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <map>
#include <unordered_map>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/gauge.h>

#include "bio_tracepoint.h"
#include "bio_client_log.h"
#include "bio_ref.h"
#include "bio_err.h"
#include "bio_c.h"
#include "bio_types.h"
#include "message_op.h"
#include "bio_client.h"
#include "bio_client_agent.h"

namespace ock {
namespace bio {

class PrometheusManager;
using PrometheusManagerPtr = Ref<PrometheusManager>;
class PrometheusManager {
public:
    explicit PrometheusManager(const std::string &exposerAddress, const uint64_t timeOut,
                               const uint32_t scrapeIntervalSec)
        : mPrometheusExposer(exposerAddress),
          mPrometheusRegistry(std::make_shared<prometheus::Registry>())
    {
        mTimeOut = timeOut;
        mScrapeIntervalSec = scrapeIntervalSec;
        RegisterMetrics();
        mPrometheusExposer.RegisterCollectable(mPrometheusRegistry);
        CLIENT_LOG_INFO("Prometheus metrics available at http://configured IP address:port number/metrics");
    }

    static PrometheusManagerPtr &Instance(const std::string &exposerAddress, const uint32_t timeOut,
                                          const uint32_t scrapeIntervalSec);

    // 启动 Prometheus 监控
    BResult Start();

    // 停止 Prometheus 监控
    void Stop();

    ~PrometheusManager()
    {
        Stop();
    }

DEFINE_REF_COUNT_FUNCTIONS;

private:
    void UpdatePrometheusData();

    void UpdateCacheHit();

    void UpdateCacheResource();

    void UpdateTraceData();

    void GetAllNodeTracePoints(std::map<uint16_t, TraceDatabase> &nodesTracePoints);

    void GetTracePointsLocal(uint16_t nodeId, std::map<uint16_t, TraceDatabase> &nodesTracePoints);

    void GetRemoteTracePoints(std::vector<uint16_t> nodeIds,
                              std::map<uint16_t, TraceDatabase> &nodesTracePoints);

    bool FailHandler(BResult result, uint64_t startTime, uint64_t timeOut);

    inline uint32_t GetScrapeIntervalSec()
    {
        return mScrapeIntervalSec;
    }

    inline void UpdateCacheHitRatio(std::map<uint16_t, prometheus::Gauge*> &dataMap,
                                    prometheus::Family<prometheus::Gauge> *dataFamily,
                                    uint16_t key, double value)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto nodeCacheHitRatio = dataMap.find(key);
        if (nodeCacheHitRatio == dataMap.end()) {
            dataMap[key] = &dataFamily->Add({{"node", std::to_string(key)}});
            dataMap[key]->Set(value);
        } else {
            nodeCacheHitRatio->second->Set(value);
        }
    }

    inline void UpdateCacheMetric(std::map<uint16_t, prometheus::Gauge*> &cacheMetrics,
                                  prometheus::Family<prometheus::Gauge> *cacheMetricFamily,
                                  uint16_t nodeId, double value, bool isMB = false)
    {
        // 如果值是以MB为单位，进行单位转换
        double finalValue = isMB ? value / NO_1048576 : value;

        auto nodeCacheMetric = cacheMetrics.find(nodeId);
        if (nodeCacheMetric == cacheMetrics.end()) {
            cacheMetrics[nodeId] = &cacheMetricFamily->Add({{"node", std::to_string(nodeId)}});
            cacheMetrics[nodeId]->Set(value);
        } else {
            nodeCacheMetric->second->Set(finalValue);
        }
    }

    inline void UpdateTraceMetrics(std::unordered_map<uint16_t,
            std::unordered_map<std::string, prometheus::Gauge*>> &traceMaps,
                                   prometheus::Family<prometheus::Gauge> *traceFamily,
                                   uint16_t nodeId, std::string traceName, double value)
    {
        auto it = traceMaps.find(nodeId);
        if (it == traceMaps.end()) {
            traceMaps[nodeId] = std::unordered_map<std::string, prometheus::Gauge*>();
        }

        if (traceMaps[nodeId].find(traceName) == traceMaps[nodeId].end()) {
            std::map<std::string, std::string> labels;
            labels["node"] = std::to_string(nodeId);
            labels["trace_name"] = traceName;
            traceMaps[nodeId][traceName] = &traceFamily->Add(labels);
            traceMaps[nodeId][traceName]->Set(value);
        } else {
            traceMaps[nodeId][traceName]->Set(value);
        }
    }

    // 注册仪表盘
    inline prometheus::Family<prometheus::Gauge>* RegisterGauge(prometheus::Registry &registry, const std::string &name,
                                                                const std::string &help, const std::initializer_list
            <std::pair<std::string, std::string>> &labels = {})
    {
        auto gauge = &prometheus::BuildGauge()
                .Name(name)
                .Help(help)
                .Register(registry);
        if (!gauge) {
            CLIENT_LOG_ERROR("Failed to register gauge with name: " << name);
        }

        return gauge;
    }

    // 注册 cache hit 相关指标
    inline void RegisterCacheHitMetrics()
    {
        mTotalCacheHitRatioFamily = RegisterGauge(*mPrometheusRegistry,
                                                  "total_hit_ratio", "total hit ratio");
        mRCacheHitRatioFamily = RegisterGauge(*mPrometheusRegistry,
                                              "read_cache_hit_ratio", "read cache hit ratio");
        mRCacheHitMemRatioFamily = RegisterGauge(*mPrometheusRegistry,
                                                 "read_mem_cache_hit_ratio", "read memory cache hit ratio");
        mRCacheHitDiskRatioFamily = RegisterGauge(*mPrometheusRegistry,
                                                  "read_disk_cache_hit_ratio", "read disk cache hit ratio");
        mWCacheHitRatioFamily = RegisterGauge(*mPrometheusRegistry,
                                              "write_cache_hit_ratio", "write cache hit ratio");
        mWCacheHitMemRatioFamily = RegisterGauge(*mPrometheusRegistry,
                                                 "write_mem_cache_hit_ratio", "write memory cache hit ratio");
        mWCacheHitDiskRatioFamily = RegisterGauge(*mPrometheusRegistry,
                                                  "write_disk_cache_hit_ratio", "write disk cache hit ratio");
        mBackendHitRatioFamily = RegisterGauge(*mPrometheusRegistry,
                                               "backend_hit_ratio", "backend hit ratio");
    }

    // 注册 wcache 相关指标
    inline void RegisterWCacheMetrics()
    {
        mWCacheMemWaterLevelFamily = RegisterGauge(*mPrometheusRegistry,
                                                   "wcache_mem_water_level", "Write Cache Memory Water Level");
        mWCacheMemCapacityFamily = RegisterGauge(*mPrometheusRegistry,
                                                 "wcache_mem_capacity", "Write Cache Memory Capacity");
        mWCacheMemUsedSizeFamily = RegisterGauge(*mPrometheusRegistry,
                                                 "wcache_mem_used_size", "Write Cache Memory Used Size");
        mWCacheDiskWaterLevelFamily = RegisterGauge(*mPrometheusRegistry,
                                                    "wcache_disk_water_level", "Write Cache Disk Water Level");
        mWCacheDiskCapacityFamily = RegisterGauge(*mPrometheusRegistry,
                                                  "wcache_disk_capacity", "Write Cache Disk Capacity");
        mWCacheDiskUsedSizeFamily = RegisterGauge(*mPrometheusRegistry,
                                                  "wcache_disk_used_size", "Write Cache Disk Used Size");
    }

    // 注册 rcache 相关指标
    inline void RegisterRCacheMetrics()
    {
        mRCacheMemCapacityFamily = RegisterGauge(*mPrometheusRegistry,
                                                 "rcache_mem_capacity", "Read Cache Memory Capacity");
        mRCacheMemUsedSizeFamily = RegisterGauge(*mPrometheusRegistry,
                                                 "rcache_mem_used_size", "Read Cache Memory Used Size");
        mRCacheMemWaterLevelFamily = RegisterGauge(*mPrometheusRegistry,
                                                   "rcache_mem_water_level", "Read Cache Memory Water Level");
        mRCacheDiskCapacityFamily = RegisterGauge(*mPrometheusRegistry,
                                                  "rcache_disk_capacity", "Read Cache Memory Capacity");
        mRCacheDiskUsedSizeFamily = RegisterGauge(*mPrometheusRegistry,
                                                  "rcache_disk_used_size", "Read Cache Memory Used Size");
        mRCacheDiskWaterLevelFamily = RegisterGauge(*mPrometheusRegistry,
                                                    "rcache_disk_water_level", "Read Cache Memory Water Level");
    }

    // 注册 trace 相关指标
    inline void RegisterTraceMetrics()
    {
        mBeginFamily = RegisterGauge(*mPrometheusRegistry, "begin", "Total number of executions");
        mGoodEndFamily = RegisterGauge(*mPrometheusRegistry, "good_end", "Number of successful executions");
        mBadEndFamily = RegisterGauge(*mPrometheusRegistry, "bad_end", "Number of failed executions");
        mOnFlyFamily = RegisterGauge(*mPrometheusRegistry, "on_fly", "");
        mIOPSFamily = RegisterGauge(*mPrometheusRegistry, "iops", "Input/Output Operations Per Second");
        mMinFamily = RegisterGauge(*mPrometheusRegistry, "min", "Minimum execution time");
        mMaxFamily = RegisterGauge(*mPrometheusRegistry, "max", "Maximum execution time");
        mAvgFamily = RegisterGauge(*mPrometheusRegistry, "avg", "Average execution time");
        mTotalFamily = RegisterGauge(*mPrometheusRegistry, "total", "Total time consumed by such operations");
    }

    inline void RegisterMetrics()
    {
        // 注册mCacheHitRate 指标
        RegisterCacheHitMetrics();

        // 将Resource指标添加到注册表
        RegisterWCacheMetrics();
        RegisterRCacheMetrics();

        // 将trace指标添加到注册表
        RegisterTraceMetrics();
    }

private:
    std::atomic<bool> mPrometheusRunning {false};
    prometheus::Exposer mPrometheusExposer;
    std::shared_ptr<prometheus::Registry> mPrometheusRegistry;
    //  定时线程
    std::thread mUpdaterThread;
    std::mutex mMutex;
    uint32_t mTimeOut = NO_60;
    uint32_t mScrapeIntervalSec = 15;

private:
    //  监控命中率
    //  指标集合
    prometheus::Family<prometheus::Gauge>* mTotalCacheHitRatioFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheHitRatioFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheHitMemRatioFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheHitDiskRatioFamily;
    prometheus::Family<prometheus::Gauge>* mWCacheHitRatioFamily;
    prometheus::Family<prometheus::Gauge>* mWCacheHitMemRatioFamily;
    prometheus::Family<prometheus::Gauge>* mWCacheHitDiskRatioFamily;
    prometheus::Family<prometheus::Gauge>* mBackendHitRatioFamily;
    //  每个node的指标
    std::map<uint16_t, prometheus::Gauge*> mTotalCacheHitRatios;
    std::map<uint16_t, prometheus::Gauge*> mRCacheHitRatios;
    std::map<uint16_t, prometheus::Gauge*> mRCacheHitMemRatios;
    std::map<uint16_t, prometheus::Gauge*> mRCacheHitDiskRatios;
    std::map<uint16_t, prometheus::Gauge*> mWCacheHitRatios;
    std::map<uint16_t, prometheus::Gauge*> mWCacheHitMemRatios;
    std::map<uint16_t, prometheus::Gauge*> mWCacheHitDiskRatios;
    std::map<uint16_t, prometheus::Gauge*> mBackendHitRatios;
    //  监控资源使用
    //  指标集合
    prometheus::Family<prometheus::Gauge>* mWCacheMemWaterLevelFamily;
    prometheus::Family<prometheus::Gauge>* mWCacheMemCapacityFamily;
    prometheus::Family<prometheus::Gauge>* mWCacheMemUsedSizeFamily;
    prometheus::Family<prometheus::Gauge>* mWCacheDiskWaterLevelFamily;
    prometheus::Family<prometheus::Gauge>* mWCacheDiskCapacityFamily;
    prometheus::Family<prometheus::Gauge>* mWCacheDiskUsedSizeFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheMemUsedSizeFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheMemWaterLevelFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheMemCapacityFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheDiskUsedSizeFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheDiskWaterLevelFamily;
    prometheus::Family<prometheus::Gauge>* mRCacheDiskCapacityFamily;
    //  每个node的资源指标
    std::map<uint16_t, prometheus::Gauge*> mWCacheMemWaterLevels;
    std::map<uint16_t, prometheus::Gauge*> mWCacheMemCapacities;
    std::map<uint16_t, prometheus::Gauge*> mWCacheMemUsedSizes;
    std::map<uint16_t, prometheus::Gauge*> mWCacheDiskWaterLevels;
    std::map<uint16_t, prometheus::Gauge*> mWCacheDiskCapacities;
    std::map<uint16_t, prometheus::Gauge*> mWCacheDiskUsedSizes;
    std::map<uint16_t, prometheus::Gauge*> mRCacheMemWaterLevels;
    std::map<uint16_t, prometheus::Gauge*> mRCacheMemCapacities;
    std::map<uint16_t, prometheus::Gauge*> mRCacheMemUsedSizes;
    std::map<uint16_t, prometheus::Gauge*> mRCacheDiskCapacities;
    std::map<uint16_t, prometheus::Gauge*> mRCacheDiskUsedSizes;
    std::map<uint16_t, prometheus::Gauge*> mRCacheDiskWaterLevels;
    //  监控trace数据
    //  指标集合
    prometheus::Family<prometheus::Gauge>* mBeginFamily;
    prometheus::Family<prometheus::Gauge>* mGoodEndFamily;
    prometheus::Family<prometheus::Gauge>* mBadEndFamily;
    prometheus::Family<prometheus::Gauge>* mOnFlyFamily;
    prometheus::Family<prometheus::Gauge>* mIOPSFamily;
    prometheus::Family<prometheus::Gauge>* mMinFamily;
    prometheus::Family<prometheus::Gauge>* mMaxFamily;
    prometheus::Family<prometheus::Gauge>* mAvgFamily;
    prometheus::Family<prometheus::Gauge>* mTotalFamily;
    //  每个node的trace指标
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mBegins;
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mGoodEnds;
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mBadEnds;
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mOnFlys;
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mIOPSs;
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mMins;
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mMaxs;
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mAvgs;
    std::unordered_map<uint16_t, std::unordered_map<std::string, prometheus::Gauge*>> mTotals;
DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // BOOST_IO_PROMETHEUS_MANAGER_H