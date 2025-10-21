/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Create Date : 2025
 */
#include "prometheus_manager.h"
namespace ock {
namespace bio {
BioClientPtr gBioClient = BioClient::Instance();

PrometheusManagerPtr &PrometheusManager::Instance(const std::string &exposerAddress, const uint32_t timeOut,
                                                  const uint32_t scrapeIntervalSec)
{
    static auto instance = MakeRef<PrometheusManager>(exposerAddress, timeOut, scrapeIntervalSec);
    return instance;
}

BResult PrometheusManager::Start()
{
    if (mPrometheusRunning.load()) {
        CLIENT_LOG_INFO("PrometheusManager is already running.");
        return BIO_ALREADY_DONE;
    }

    mPrometheusRunning.store(true);
    mUpdaterThread = std::thread(&PrometheusManager::UpdatePrometheusData, this);

    CLIENT_LOG_INFO("PrometheusManager started.");
    return BIO_OK;
}

void PrometheusManager::Stop()
{
    if (!mPrometheusRunning.load()) {
        CLIENT_LOG_INFO("PrometheusManager is not running.");
        return;
    }

    mPrometheusRunning.store(false);
    if (mUpdaterThread.joinable()) {
        mUpdaterThread.join();
    }

    CLIENT_LOG_INFO("PrometheusManager stopped.");
    return;
}

void PrometheusManager::GetFaultNodeId(std::vector<uint16_t> &faultNodesId)
{
    auto nodeView = gBioClient->GetMirror()->GetNodeView();

    for (const auto &node : nodeView) {
        if (node.second.status == CM_NODE_FAULT) {
            faultNodesId.push_back(node.first.nodeId);
        }
    }
}

void PrometheusManager::UpdatePrometheusData()
{
    while (mPrometheusRunning.load()) {
        //  更新cache hit
        UpdateCacheHit();

        //  更新resource信息
        UpdateCacheResource();

        //  更新trace信息
        UpdateTraceData();

        std::this_thread::sleep_for(std::chrono::seconds(GetScrapeIntervalSec()));
    }
}

void PrometheusManager::UpdateCacheHit()
{
    //  更新cache hit rate信息
    CacheHitFinalDesc desc;
    CacheHitFinalDesc *nodeDesc = NULL;
    uint64_t nodeNum = 0;
    auto ret = BioShowCacheHitRatio(&desc, &nodeDesc, &nodeNum);
    if (ret != RET_CACHE_OK) {
        BioFreeCacheHitPtr(&nodeDesc, nodeNum);
        return;
    }

    std::vector<uint16_t> faultNodesId;
    GetFaultNodeId(faultNodesId);
    for (const auto &nodeId : faultNodesId) {
        UpdateFaultNodeCacheHitRatio(mTotalCacheHitRatios, mTotalCacheHitRatioFamily, nodeId);
        UpdateFaultNodeCacheHitRatio(mRCacheHitRatios, mRCacheHitRatioFamily, nodeId);
        UpdateFaultNodeCacheHitRatio(mRCacheHitMemRatios, mRCacheHitMemRatioFamily, nodeId);
        UpdateFaultNodeCacheHitRatio(mRCacheHitDiskRatios, mRCacheHitDiskRatioFamily, nodeId);
        UpdateFaultNodeCacheHitRatio(mWCacheHitRatios, mWCacheHitRatioFamily, nodeId);
        UpdateFaultNodeCacheHitRatio(mWCacheHitMemRatios, mWCacheHitMemRatioFamily, nodeId);
        UpdateFaultNodeCacheHitRatio(mWCacheHitDiskRatios, mWCacheHitDiskRatioFamily, nodeId);
        UpdateFaultNodeCacheHitRatio(mBackendHitRatios, mBackendHitRatioFamily, nodeId);
    }

    uint16_t nodeId = UINT16_MAX;
    double rCacheHitMemRatio = desc.wCacheTotalCount != 0 ?
                               (double)desc.rCacheHitMemCount / (double)desc.wCacheTotalCount : 0;
    double rCacheHitDiskRatio = desc.wCacheTotalCount != 0 ?
                                (double)desc.rCacheHitDiskCount / (double)desc.wCacheTotalCount : 0;
    double rCacheHitRatio = desc.wCacheTotalCount != 0 ?
                            (double)desc.rCacheHitCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitMemRatio = desc.wCacheHitMemCount != 0 ?
                               (double)desc.wCacheHitMemCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitDiskRatio = desc.wCacheHitDiskCount != 0 ?
                                (double)desc.wCacheHitDiskCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitRatio = desc.wCacheTotalCount != 0 ?
                            (double)desc.wCacheHitCount / (double)desc.wCacheTotalCount : 0;
    double backendHitRatio = desc.wCacheTotalCount != 0 ?
                             (double)desc.backendHitCount / (double)desc.wCacheTotalCount : 0;
    double totalCacheHitRatio = rCacheHitRatio + wCacheHitRatio;

    UpdateCacheHitRatio(mTotalCacheHitRatios, mTotalCacheHitRatioFamily, nodeId, totalCacheHitRatio);
    UpdateCacheHitRatio(mRCacheHitRatios, mRCacheHitRatioFamily, nodeId, rCacheHitRatio);
    UpdateCacheHitRatio(mRCacheHitMemRatios, mRCacheHitMemRatioFamily, nodeId, rCacheHitMemRatio);
    UpdateCacheHitRatio(mRCacheHitDiskRatios, mRCacheHitDiskRatioFamily, nodeId, rCacheHitDiskRatio);
    UpdateCacheHitRatio(mWCacheHitRatios, mWCacheHitRatioFamily, nodeId, wCacheHitRatio);
    UpdateCacheHitRatio(mWCacheHitMemRatios, mWCacheHitMemRatioFamily, nodeId, wCacheHitMemRatio);
    UpdateCacheHitRatio(mWCacheHitDiskRatios, mWCacheHitDiskRatioFamily, nodeId, wCacheHitDiskRatio);
    UpdateCacheHitRatio(mBackendHitRatios, mBackendHitRatioFamily, nodeId, backendHitRatio);

    for (int i = 0; i < nodeNum; i++) {
        nodeId = nodeDesc[i].nodeId;
        double nodeRCacheHitMemRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                       (double)nodeDesc[i].rCacheHitMemCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeRCacheHitDiskRatio = nodeDesc[i].wCacheTotalCount != 0 ? (double)nodeDesc[i].rCacheHitDiskCount
                                        / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeRCacheHitRatio = nodeDesc[i].wCacheTotalCount != 0 ? (double)nodeDesc[i].rCacheHitCount
                                    / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitMemRatio = nodeDesc[i].wCacheTotalCount != 0 ? (double)nodeDesc[i].wCacheHitMemCount
                                       / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitDiskRatio = nodeDesc[i].wCacheTotalCount != 0 ? (double)nodeDesc[i].wCacheHitDiskCount
                                        / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                    (double)nodeDesc[i].wCacheHitCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeBackendHitRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                     (double)nodeDesc[i].backendHitCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeTotalCacheHitRatio = nodeRCacheHitRatio + nodeWCacheHitRatio;

        UpdateCacheHitRatio(mTotalCacheHitRatios, mTotalCacheHitRatioFamily, nodeId, nodeTotalCacheHitRatio);
        UpdateCacheHitRatio(mRCacheHitRatios, mRCacheHitRatioFamily, nodeId, nodeRCacheHitRatio);
        UpdateCacheHitRatio(mRCacheHitMemRatios, mRCacheHitMemRatioFamily, nodeId, nodeRCacheHitMemRatio);
        UpdateCacheHitRatio(mRCacheHitDiskRatios, mRCacheHitDiskRatioFamily, nodeId, nodeRCacheHitDiskRatio);
        UpdateCacheHitRatio(mWCacheHitRatios, mWCacheHitRatioFamily, nodeId, nodeWCacheHitRatio);
        UpdateCacheHitRatio(mWCacheHitMemRatios, mWCacheHitMemRatioFamily, nodeId, nodeWCacheHitMemRatio);
        UpdateCacheHitRatio(mWCacheHitDiskRatios, mWCacheHitDiskRatioFamily, nodeId, nodeWCacheHitDiskRatio);
        UpdateCacheHitRatio(mBackendHitRatios, mBackendHitRatioFamily, nodeId, nodeBackendHitRatio);
    }

    BioFreeCacheHitPtr(&nodeDesc, nodeNum);
}

void PrometheusManager::UpdateCacheResource()
{
    //  更新cache resource信息
    CacheResourcesDesc *nodeDesc = NULL;
    uint64_t nodeNum = 0;
    auto ret = BioShowCacheResource(&nodeDesc, &nodeNum);
    if (ret != RET_CACHE_OK) {
        BioFreeCacheResourcePtr(&nodeDesc, nodeNum);
        return;
    }
    std::vector<uint16_t> faultNodesId;
    GetFaultNodeId(faultNodesId);
    for (const auto &faultNodeId : faultNodesId) {
        UpdateFaultNodeCacheMetric(mWCacheMemWaterLevels, mWCacheMemWaterLevelFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mWCacheMemCapacities, mWCacheMemCapacityFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mWCacheMemUsedSizes, mWCacheMemUsedSizeFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mWCacheDiskWaterLevels, mWCacheDiskWaterLevelFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mWCacheDiskCapacities, mWCacheDiskCapacityFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mWCacheDiskUsedSizes, mWCacheDiskUsedSizeFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mRCacheMemWaterLevels, mRCacheMemWaterLevelFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mRCacheMemCapacities, mRCacheMemCapacityFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mRCacheMemUsedSizes, mRCacheMemUsedSizeFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mRCacheDiskWaterLevels, mRCacheDiskWaterLevelFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mRCacheDiskCapacities, mRCacheDiskCapacityFamily, faultNodeId);
        UpdateFaultNodeCacheMetric(mRCacheDiskUsedSizes, mRCacheDiskUsedSizeFamily, faultNodeId);
    }

    //  更新每个node的指标
    for (int i = 0; i < nodeNum; i++) {
        uint16_t nodeId = nodeDesc[i].nodeId;

        // 更新写缓存水位
        double wCacheMemWaterLever = (double)nodeDesc[i].wCacheMemUsedSize / (double)nodeDesc[i].wCacheMemCapacity;
        UpdateCacheMetric(mWCacheMemWaterLevels, mWCacheMemWaterLevelFamily, nodeId, wCacheMemWaterLever);

        // 更新写缓存容量（单位转换为MB）
        UpdateCacheMetric(mWCacheMemCapacities, mWCacheMemCapacityFamily, nodeId, nodeDesc[i].wCacheMemCapacity, true);

        // 更新写缓存已用大小（单位转换为MB）
        UpdateCacheMetric(mWCacheMemUsedSizes, mWCacheMemUsedSizeFamily, nodeId, nodeDesc[i].wCacheMemUsedSize, true);

        // 更新写缓存磁盘水位
        double wCacheDiskWaterLever = (double)nodeDesc[i].wCacheDiskUsedSize / (double)nodeDesc[i].wCacheDiskCapacity;
        UpdateCacheMetric(mWCacheDiskWaterLevels, mWCacheDiskWaterLevelFamily, nodeId, wCacheDiskWaterLever);

        // 更新写缓存磁盘容量（单位转换为MB）
        UpdateCacheMetric(mWCacheDiskCapacities, mWCacheDiskCapacityFamily,
                          nodeId, nodeDesc[i].wCacheDiskCapacity, true);

        // 更新写缓存磁盘已用大小（单位转换为MB）
        UpdateCacheMetric(mWCacheDiskUsedSizes, mWCacheDiskUsedSizeFamily,
                          nodeId, nodeDesc[i].wCacheDiskUsedSize, true);

        // 更新读缓存水位
        double rCacheMemWaterLever = (double)nodeDesc[i].rCacheMemUsedSize / (double)nodeDesc[i].rCacheMemCapacity;
        UpdateCacheMetric(mRCacheMemWaterLevels, mRCacheMemWaterLevelFamily, nodeId, rCacheMemWaterLever);

        // 更新读缓存容量（单位转换为MB）
        UpdateCacheMetric(mRCacheMemCapacities, mRCacheMemCapacityFamily, nodeId, nodeDesc[i].rCacheMemCapacity, true);

        // 更新读缓存已用大小（单位转换为MB）
        UpdateCacheMetric(mRCacheMemUsedSizes, mRCacheMemUsedSizeFamily, nodeId, nodeDesc[i].rCacheMemUsedSize, true);

        // 更新读缓存磁盘水位
        double rCacheDiskWaterLever = (double)nodeDesc[i].rCacheDiskUsedSize / (double)nodeDesc[i].rCacheDiskCapacity;
        UpdateCacheMetric(mRCacheDiskWaterLevels, mRCacheDiskWaterLevelFamily, nodeId, rCacheDiskWaterLever);

        // 更新读缓存磁盘容量（单位转换为MB）
        UpdateCacheMetric(mRCacheDiskCapacities, mRCacheDiskCapacityFamily,
                          nodeId, nodeDesc[i].rCacheDiskCapacity, true);

        // 更新读缓存磁盘已用大小（单位转换为MB）
        UpdateCacheMetric(mRCacheDiskUsedSizes, mRCacheDiskUsedSizeFamily,
                          nodeId, nodeDesc[i].rCacheDiskUsedSize, true);
    }

    BioFreeCacheResourcePtr(&nodeDesc, nodeNum);
}

void PrometheusManager::UpdateTraceData()
{
    std::map<uint16_t, TraceDatabase> nodesTracePoints;
    GetAllNodeTracePoints(nodesTracePoints);

    std::vector<uint16_t> faultNodesId;
    GetFaultNodeId(faultNodesId);
    for (const auto &faultNodeId : faultNodesId) {
        UpdateFaultNodeTraceMetrics(mBegins, mBeginFamily, faultNodeId);
        UpdateFaultNodeTraceMetrics(mGoodEnds, mGoodEndFamily, faultNodeId);
        UpdateFaultNodeTraceMetrics(mBadEnds, mBadEndFamily, faultNodeId);
        UpdateFaultNodeTraceMetrics(mOnFlys, mOnFlyFamily, faultNodeId);
        UpdateFaultNodeTraceMetrics(mIOPSs, mIOPSFamily, faultNodeId);
        UpdateFaultNodeTraceMetrics(mMins, mMinFamily, faultNodeId);
        UpdateFaultNodeTraceMetrics(mMaxs, mMaxFamily, faultNodeId);
        UpdateFaultNodeTraceMetrics(mAvgs, mAvgFamily, faultNodeId);
        UpdateFaultNodeTraceMetrics(mTotals, mTotalFamily, faultNodeId);
    }

    for (const auto &tracePoints : nodesTracePoints) {
        if (tracePoints.second.count == 0) {
            continue;
        }

        uint16_t nodeId = tracePoints.first;
        for (int i = 0; i < tracePoints.second.count; ++i) {
            const auto &traceData = tracePoints.second.traces[i];
            auto traceName = traceData.traceName;
            auto beginData = traceData.metrics.beginData;
            auto goodEndData = traceData.metrics.goodEnd;
            auto badEndData = traceData.metrics.badEnd;
            auto onFlyData = ((beginData > goodEndData + badEndData) ? (beginData - goodEndData - badEndData) : 0);
            auto iopsData = (static_cast<double>(beginData) / ock::htracer::iopsDiff);
            auto minData = traceData.metrics.min;
            auto maxData = traceData.metrics.max;
            auto avgData = (goodEndData == 0 ? 0 : static_cast<double>(traceData.metrics.total)
                                                   / static_cast<double>(goodEndData));
            auto total = traceData.metrics.total;

            UpdateTraceMetrics(mBegins, mBeginFamily, nodeId, traceName, beginData);
            UpdateTraceMetrics(mGoodEnds, mGoodEndFamily, nodeId, traceName, goodEndData);
            UpdateTraceMetrics(mBadEnds, mBadEndFamily, nodeId, traceName, badEndData);
            UpdateTraceMetrics(mOnFlys, mOnFlyFamily, nodeId, traceName, onFlyData);
            UpdateTraceMetrics(mIOPSs, mIOPSFamily, nodeId, traceName, iopsData);
            UpdateTraceMetrics(mMins, mMinFamily, nodeId, traceName, minData);
            UpdateTraceMetrics(mMaxs, mMaxFamily, nodeId, traceName, maxData);
            UpdateTraceMetrics(mAvgs, mAvgFamily, nodeId, traceName, avgData);
            UpdateTraceMetrics(mTotals, mTotalFamily, nodeId, traceName, total);
        }
    }
}

void PrometheusManager::GetAllNodeTracePoints(std::map<uint16_t, TraceDatabase> &nodesTracePoints)
{
    uint16_t localId = UINT16_MAX;
    std::vector<uint16_t> remoteIds;

    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = gBioClient->GetMirror()->GetNodeView();
    for (const auto node : nodeView) {
        if (node.second.status == CM_NODE_FAULT) {
            continue;
        }

        if (node.first.nodeId == gBioClient->GetMirror()->GetLocalNodeInfo().VNodeId()) {
            localId = gBioClient->GetMirror()->GetLocalNodeInfo().VNodeId();
            continue;
        }
        remoteIds.push_back(node.first.nodeId);
    }

    GetTracePointsLocal(localId, nodesTracePoints);
    GetRemoteTracePoints(remoteIds, nodesTracePoints);
}

void PrometheusManager::GetTracePointsLocal(uint16_t nodeId,
                                            std::map<uint16_t, TraceDatabase> &nodesTracePoints)
{
    if (UNLIKELY(nodeId == UINT16_MAX)) {
        return;
    }

    GetTracePointsRequest req;
    req.nodeId = nodeId;
    req.comm = { MESSAGE_MAGIC, 0, 0, gBioClient->GetMirror()->GetLocalNodeInfo().VNodeId(), getpid() };

    BResult ret = agent::BioClientAgent::Instance()->GetTracePointsLocal(req, nodesTracePoints);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get local trace points failed, ret: " << ret << ", nodeId: " << nodeId);
    }
}

void PrometheusManager::GetRemoteTracePoints(std::vector<uint16_t> nodeIds,
                                             std::map<uint16_t, TraceDatabase> &nodesTracePoints)
{
    GetTracePointsRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, gBioClient->GetMirror()->GetLocalNodeInfo().VNodeId(), getpid() };

    bool isRetry = false;
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = BIO_OK;
    do {
        isRetry = false;
        for (auto nodeId : nodeIds) {
            GetTracePointsResponse rsp;
            ret = net::BioClientNet::Instance()->SendSync<GetTracePointsRequest, GetTracePointsResponse>(
                    static_cast<BioNodeId>(nodeId), BIO_OP_SDK_GET_TRACE_POINTS, req, rsp);
            if (ret != BIO_OK) {
                CLIENT_LOG_ERROR("Get remote trace points failed, ret: " << ret << ", nodeId: " << nodeId);
                break;
            }

            if (rsp.traceDatabase.count == 0) {
                continue;
            }
            nodesTracePoints[nodeId] = rsp.traceDatabase;
        }
        isRetry = FailHandler(ret, startTime, mTimeOut);
    } while (isRetry);
}

bool PrometheusManager::FailHandler(const BResult result, uint64_t startTime, uint64_t timeOut)
{
    uint64_t costTime = Monotonic::TimeSec() - startTime;
    if (UNLIKELY(costTime >= timeOut)) { // 超过重试时间则不再进行重试.
        return false;
    }

    bool isRetry = false;
    uint16_t sleepTime = 0;
    switch (result) {
        case BIO_INNER_RETRY:
        case BIO_NET_RETRY:
        case BIO_CHECK_PT_FAIL:
            isRetry = true;
            sleepTime = BIO_IO_INTERAL_TIME;
            gBioClient->BioClientUpdateView(); // 更新视图.
            break;
        default:
            isRetry = false;
            break;
    }

    sleep(sleepTime);
    return isRetry;
}
}
}