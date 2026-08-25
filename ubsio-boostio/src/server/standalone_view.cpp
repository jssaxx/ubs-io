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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include "bdm_core.h"
#include "bio_log.h"
#include "standalone_view.h"

namespace ock {
namespace bio {
namespace {
constexpr uint16_t STANDALONE_NODE_ID = 0;
constexpr uint64_t STANDALONE_PT_VERSION = 1;

std::string FormatPtView(const StandaloneView::PtView &ptView)
{
    std::ostringstream oss;
    oss << "{\"ptCount\":" << ptView.size() << ",\"ptEntries\":[";
    bool firstPt = true;
    for (const auto &ptEntry : ptView) {
        if (!firstPt) {
            oss << ",";
        }
        firstPt = false;

        const CmPtInfo &pt = ptEntry.second;
        oss << "{\"mapPtId\":" << ptEntry.first << ",\"ptInfo\":{"
            << "\"version\":" << pt.version << ",\"referNum\":" << pt.referNum << ",\"ptId\":" << pt.ptId
            << ",\"state\":" << pt.state << ",\"masterNodeId\":" << pt.masterNodeId
            << ",\"masterDiskId\":" << pt.masterDiskId << ",\"copys\":[";
        for (uint32_t index = 0; index < pt.copys.size(); ++index) {
            if (index != 0) {
                oss << ",";
            }
            const CmPtCopy &copy = pt.copys[index];
            oss << "{\"nodeId\":" << copy.nodeId << ",\"diskId\":" << copy.diskId << ",\"state\":"
                << copy.state << "}";
        }
        oss << "]}}";
    }
    oss << "]}";
    return oss.str();
}

CmDiskStatus ToCmDiskStatus(uint16_t diskId)
{
    return BdmGetDiskStatus(diskId) == BDM_DISK_STATE_NORMAL ? CM_DISK_NORMAL : CM_DISK_FAULT;
}

BResult BuildPtDiskMap(const std::vector<int64_t> &diskCaps, const std::vector<CmDiskInfo> &disks, uint32_t ptNum,
    bool hasDiskCache, std::vector<uint16_t> &diskByPt)
{
    diskByPt.clear();
    diskByPt.reserve(ptNum);
    if (!hasDiskCache) {
        diskByPt.assign(ptNum, 0);
        return BIO_OK;
    }

    if (diskCaps.size() < disks.size()) {
        LOG_ERROR("Standalone disk capacity config is incomplete, diskNum:" << disks.size() <<
            ", diskCapNum:" << diskCaps.size() << ".");
        return BIO_INVALID_PARAM;
    }

    long double totalCap = 0;
    std::vector<uint16_t> normalDiskIds;
    normalDiskIds.reserve(disks.size());
    for (const auto &disk : disks) {
        if (disk.diskStatus != CM_DISK_NORMAL) {
            continue;
        }
        uint16_t diskId = disk.diskId;
        if (diskId >= diskCaps.size()) {
            LOG_ERROR("Standalone normal disk id exceeds capacity config, diskId:" << diskId << ", diskCapNum:" <<
                diskCaps.size() << ".");
            return BIO_INVALID_PARAM;
        }
        if (diskCaps[diskId] <= 0) {
            LOG_ERROR("Invalid standalone disk capacity, diskId:" << diskId << ", cap:" << diskCaps[diskId] << ".");
            return BIO_INVALID_PARAM;
        }
        normalDiskIds.emplace_back(diskId);
        totalCap += static_cast<long double>(diskCaps[diskId]);
    }
    if (normalDiskIds.empty()) {
        LOG_ERROR("Standalone pt disk map requires at least one normal disk.");
        return BIO_ERR;
    }

    uint32_t assignedPtNum = 0;
    for (size_t index = 0; index < normalDiskIds.size(); ++index) {
        uint16_t diskId = normalDiskIds[index];
        uint32_t ptCount = ptNum - assignedPtNum;
        if (index + 1 < normalDiskIds.size()) {
            ptCount = static_cast<uint32_t>(static_cast<long double>(diskCaps[diskId]) * ptNum / totalCap);
            assignedPtNum += ptCount;
        }
        for (uint32_t index = 0; index < ptCount; ++index) {
            diskByPt.emplace_back(diskId);
        }
    }
    if (diskByPt.size() != ptNum) {
        LOG_ERROR("Standalone pt disk map size mismatch, ptNum:" << ptNum << ", diskMapSize:" << diskByPt.size() <<
            ".");
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult ComputePtTargetDistribution(const std::vector<uint16_t> &diskIds, const std::vector<int64_t> &diskCaps,
    uint32_t ptNum, std::map<uint16_t, uint32_t> &targetPtCount)
{
    long double totalCapacity = 0;
    for (uint16_t diskId : diskIds) {
        if (diskId >= diskCaps.size() || diskCaps[diskId] <= 0) {
            LOG_ERROR("Invalid standalone disk capacity, diskId:" << diskId << ", diskCapNum:" <<
                diskCaps.size() << ".");
            return BIO_INVALID_PARAM;
        }
        totalCapacity += static_cast<long double>(diskCaps[diskId]);
    }

    std::vector<std::pair<long double, uint16_t>> remainders;
    uint32_t assignedPtNum = 0;
    for (uint16_t diskId : diskIds) {
        long double ideal = static_cast<long double>(diskCaps[diskId]) * ptNum / totalCapacity;
        uint32_t target = static_cast<uint32_t>(ideal);
        targetPtCount[diskId] = target;
        assignedPtNum += target;
        remainders.emplace_back(ideal - target, diskId);
    }
    std::sort(remainders.begin(), remainders.end(), [](const std::pair<long double, uint16_t> &left,
        const std::pair<long double, uint16_t> &right) {
        if (left.first != right.first) {
            return left.first > right.first;
        }
        return left.second < right.second;
    });
    for (uint32_t index = 0; assignedPtNum < ptNum; ++index, ++assignedPtNum) {
        ++targetPtCount[remainders[index].second];
    }
    return BIO_OK;
}
}

BResult StandaloneView::Build(const BioConfigPtr &config, CmNodeId &localNid, NodeView &nodeView, PtView &ptView)
{
    if (config == nullptr) {
        LOG_ERROR("Standalone mode build view failed, config is nullptr.");
        return BIO_INVALID_PARAM;
    }
    const auto &cmConfig = config->GetCmConfig();
    const auto &netConfig = config->GetNetConfig();
    const auto &daemonConfig = config->GetDaemonConfig();

    const auto configDiskNum = static_cast<uint32_t>(daemonConfig.diskList.size());
    const bool hasDiskCache = daemonConfig.hasDiskCache;
    if (configDiskNum == 0 && hasDiskCache) {
        LOG_ERROR("Standalone mode requires at least one cache disk.");
        return BIO_INVALID_PARAM;
    }

    uint32_t diskNum = hasDiskCache ? std::min<uint32_t>(configDiskNum, static_cast<uint32_t>(DISK_DEV_NUM)) : 1;
    if (configDiskNum > DISK_DEV_NUM) {
        LOG_WARN("Standalone mode only uses first " << DISK_DEV_NUM << " disks, configured:" << configDiskNum << ".");
    }

    std::vector<CmDiskInfo> disks;
    disks.reserve(diskNum);
    bool hasNormalDisk = false;
    for (uint16_t diskId = 0; diskId < diskNum; ++diskId) {
        CmDiskStatus diskStatus = hasDiskCache ? ToCmDiskStatus(diskId) : CM_DISK_NORMAL;
        hasNormalDisk = hasNormalDisk || diskStatus == CM_DISK_NORMAL;
        disks.push_back({ diskId, diskStatus });
    }
    if (!hasNormalDisk) {
        LOG_ERROR("Standalone mode requires at least one normal cache disk.");
        return BIO_ERR;
    }

    localNid = CmNodeId(static_cast<uint16_t>(cmConfig.groupId), STANDALONE_NODE_ID);
    CmNodeInfo localNode(localNid, netConfig.dataIp, netConfig.dataPort, CM_NODE_NORMAL, disks);
    nodeView.clear();
    nodeView.emplace(localNid, localNode);

    uint32_t ptNum = static_cast<uint32_t>(std::max<int32_t>(cmConfig.ptNum, 1));
    if (ptNum > UINT16_MAX) {
        LOG_ERROR("Invalid standalone pt num:" << ptNum << ".");
        return BIO_INVALID_PARAM;
    }
    std::vector<uint16_t> diskByPt;
    BResult ret = BuildPtDiskMap(daemonConfig.diskCaps, disks, ptNum, hasDiskCache, diskByPt);
    if (ret != BIO_OK) {
        return ret;
    }
    ptView.clear();
    for (uint16_t ptId = 0; ptId < ptNum; ++ptId) {
        uint16_t diskId = diskByPt[ptId];
        CmDiskStatus diskStatus = disks[diskId].diskStatus;
        CmCopyState copyState = diskStatus == CM_DISK_NORMAL ? CM_COPY_RUNNING : CM_COPY_DOWN;
        CmPtState ptState = diskStatus == CM_DISK_NORMAL ? CM_PT_NORMAL : CM_PT_FAULT;
        std::vector<CmPtCopy> copys = { { STANDALONE_NODE_ID, diskId, copyState } };
        ptView.emplace(ptId, CmPtInfo(STANDALONE_PT_VERSION, ptId, ptState, STANDALONE_NODE_ID, diskId, copys));
    }

    LOG_INFO("Standalone view build success, nodeId:" << localNid.VNodeId() << ", ptNum:" << ptView.size() <<
        ", diskNum:" << diskNum << ".");
    return BIO_OK;
}

StandaloneView::~StandaloneView()
{
    Stop();
}

BResult StandaloneView::Start(uint32_t diskNum, FaultHandler handler)
{
    if (diskNum == 0 || handler == nullptr) {
        LOG_ERROR("Start standalone fault worker with invalid input, diskNum:" << diskNum << ".");
        return BIO_INVALID_PARAM;
    }

    {
        std::lock_guard<std::mutex> lock(mFaultMutex);
        if (mRunning) {
            return BIO_OK;
        }
        mDiskStates.assign(diskNum, DiskState::NORMAL);
        for (uint16_t diskId = 0; diskId < diskNum; ++diskId) {
            if (BdmGetDiskStatus(diskId) != BDM_DISK_STATE_NORMAL) {
                mDiskStates[diskId] = DiskState::FAULT_PENDING;
            }
        }
        mFaultHandler = std::move(handler);
        mRunning = true;
    }

    BdmRegisterDiskFaultHandler(HandleDiskFault, this);
    try {
        mFaultThread = std::thread(&StandaloneView::FaultWorker, this);
    } catch (const std::system_error &error) {
        BdmRegisterDiskFaultHandler(nullptr, nullptr);
        std::lock_guard<std::mutex> lock(mFaultMutex);
        mRunning = false;
        mFaultHandler = nullptr;
        LOG_ERROR("Start standalone fault worker failed, error:" << error.what() << ".");
        return BIO_ERR;
    }
    mFaultCv.notify_one();
    return BIO_OK;
}

void StandaloneView::Stop()
{
    BdmRegisterDiskFaultHandler(nullptr, nullptr);
    {
        std::lock_guard<std::mutex> lock(mFaultMutex);
        if (!mRunning) {
            return;
        }
        mRunning = false;
    }
    mFaultCv.notify_all();
    if (mFaultThread.joinable()) {
        mFaultThread.join();
    }
    std::lock_guard<std::mutex> lock(mFaultMutex);
    mFaultHandler = nullptr;
}

int32_t StandaloneView::HandleDiskFault(uint16_t diskId, void *context)
{
    if (context == nullptr) {
        return BIO_INVALID_PARAM;
    }
    return static_cast<StandaloneView *>(context)->ReportDiskFault(diskId);
}

int32_t StandaloneView::ReportDiskFault(uint16_t diskId)
{
    {
        std::lock_guard<std::mutex> lock(mFaultMutex);
        if (!mRunning) {
            return BIO_OK;
        }
        if (diskId >= mDiskStates.size()) {
            LOG_ERROR("Report invalid standalone fault disk, diskId:" << diskId << ", diskNum:" <<
                mDiskStates.size() << ".");
            return BIO_INVALID_PARAM;
        }
        if (mDiskStates[diskId] != DiskState::NORMAL) {
            LOG_INFO("Ignore repeated standalone disk fault, diskId:" << diskId << ".");
            return BIO_OK;
        }
        mDiskStates[diskId] = DiskState::FAULT_PENDING;
    }
    mFaultCv.notify_one();
    return BIO_OK;
}

bool StandaloneView::HasPendingFaultLocked() const
{
    return std::find(mDiskStates.begin(), mDiskStates.end(), DiskState::FAULT_PENDING) != mDiskStates.end();
}

void StandaloneView::FaultWorker()
{
    bool running = true;
    while (running) {
        std::vector<uint16_t> pendingDisks;
        FaultHandler handler;
        {
            std::unique_lock<std::mutex> lock(mFaultMutex);
            mFaultCv.wait(lock, [this]() { return !mRunning || HasPendingFaultLocked(); });
            running = mRunning;
            if (!running) {
                continue;
            }
            for (uint16_t diskId = 0; diskId < mDiskStates.size(); ++diskId) {
                if (mDiskStates[diskId] == DiskState::FAULT_PENDING) {
                    pendingDisks.push_back(diskId);
                }
            }
            handler = mFaultHandler;
        }

        bool needRetry = false;
        for (uint16_t diskId : pendingDisks) {
            BResult ret = handler == nullptr ? BIO_ERR : handler(diskId);
            std::lock_guard<std::mutex> lock(mFaultMutex);
            if (diskId >= mDiskStates.size() || mDiskStates[diskId] != DiskState::FAULT_PENDING) {
                // A rolled back add disk untracks the new disk while this worker
                // runs, and a retry can re-add the same id as a healthy disk, so
                // an id collected before the rollback must not be written back.
                LOG_INFO("Standalone fault disk is no longer pending, diskId:" << diskId << ", diskNum:" <<
                    mDiskStates.size() << ".");
                continue;
            }
            if (ret == BIO_OK) {
                mDiskStates[diskId] = DiskState::FAULT_HANDLED;
                LOG_INFO("Handle standalone disk fault success, diskId:" << diskId << ".");
            } else {
                needRetry = true;
                LOG_ERROR("Handle standalone disk fault failed, diskId:" << diskId << ", ret:" << ret << ".");
            }
        }
        if (needRetry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool StandaloneView::IsDiskFault(uint16_t diskId) const
{
    std::lock_guard<std::mutex> lock(mFaultMutex);
    return diskId < mDiskStates.size() && mDiskStates[diskId] != DiskState::NORMAL;
}

bool StandaloneView::IsDiskFaultPending(uint16_t diskId) const
{
    std::lock_guard<std::mutex> lock(mFaultMutex);
    return diskId < mDiskStates.size() && mDiskStates[diskId] == DiskState::FAULT_PENDING;
}

BResult StandaloneView::CheckDiskRecoverable(uint16_t diskId) const
{
    std::lock_guard<std::mutex> lock(mFaultMutex);
    if (diskId >= mDiskStates.size()) {
        LOG_ERROR("Check recoverable disk with invalid id, diskId:" << diskId << ", diskNum:" <<
            mDiskStates.size() << ".");
        return BIO_INVALID_PARAM;
    }
    if (mDiskStates[diskId] == DiskState::FAULT_PENDING) {
        LOG_INFO("Standalone disk fault is still being handled, diskId:" << diskId << ".");
        return BIO_INNER_RETRY;
    }
    return BIO_OK;
}

BResult StandaloneView::MarkDiskRecovered(uint16_t diskId)
{
    std::lock_guard<std::mutex> lock(mFaultMutex);
    if (diskId >= mDiskStates.size()) {
        LOG_ERROR("Recover standalone disk with invalid id, diskId:" << diskId << ", diskNum:" <<
            mDiskStates.size() << ".");
        return BIO_INVALID_PARAM;
    }
    if (mDiskStates[diskId] == DiskState::FAULT_PENDING) {
        LOG_INFO("Standalone disk fault is still being handled, diskId:" << diskId << ".");
        return BIO_INNER_RETRY;
    }
    mDiskStates[diskId] = DiskState::NORMAL;
    LOG_INFO("Mark standalone disk recovered, diskId:" << diskId << ".");
    return BIO_OK;
}

BResult StandaloneView::TrackDisk(uint16_t diskId)
{
    std::lock_guard<std::mutex> lock(mFaultMutex);
    if (diskId != mDiskStates.size()) {
        LOG_ERROR("Register standalone disk with non-contiguous id, diskId:" << diskId << ", diskNum:" <<
            mDiskStates.size() << ".");
        return BIO_INVALID_PARAM;
    }
    mDiskStates.emplace_back(DiskState::NORMAL);
    return BIO_OK;
}

BResult StandaloneView::UntrackDisk(uint16_t diskId)
{
    std::lock_guard<std::mutex> lock(mFaultMutex);
    if (diskId + 1 != mDiskStates.size()) {
        LOG_ERROR("Unregister standalone disk with non-last id, diskId:" << diskId << ", diskNum:" <<
            mDiskStates.size() << ".");
        return BIO_INVALID_PARAM;
    }
    mDiskStates.pop_back();
    return BIO_OK;
}

BResult StandaloneView::FailoverDisk(uint16_t failedDiskId, const std::vector<int64_t> &currentDiskCaps,
    const CmNodeId &localNid, NodeView &nodeView, PtView &ptView,
    std::vector<std::pair<uint16_t, uint64_t>> &changedPts)
{
    changedPts.clear();
    auto nodeIter = nodeView.find(localNid);
    if (nodeIter == nodeView.end()) {
        LOG_ERROR("Standalone local node is missing when handling disk fault, diskId:" << failedDiskId << ".");
        return BIO_ERR;
    }

    bool foundFailedDisk = false;
    std::vector<uint16_t> healthyDisks;
    for (auto &disk : nodeIter->second.disks) {
        if (disk.diskId == failedDiskId) {
            disk.diskStatus = CM_DISK_FAULT;
            foundFailedDisk = true;
        }
        if (disk.diskStatus == CM_DISK_NORMAL && !IsDiskFault(disk.diskId)) {
            healthyDisks.push_back(disk.diskId);
        }
    }
    if (!foundFailedDisk) {
        LOG_ERROR("Standalone fault disk is missing from node view, diskId:" << failedDiskId << ".");
        return BIO_INVALID_PARAM;
    }
    std::map<uint16_t, uint32_t> targetPtCount;
    if (!healthyDisks.empty()) {
        BResult ret = ComputePtTargetDistribution(healthyDisks, currentDiskCaps,
            static_cast<uint32_t>(ptView.size()), targetPtCount);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
    }
    LOG_INFO("Standalone pt view before disk fault rebuild, diskId:" << failedDiskId << ", ptView:" <<
        FormatPtView(ptView) << ".");

    std::map<uint16_t, uint32_t> assignedPtCount;
    for (uint16_t diskId : healthyDisks) {
        assignedPtCount.emplace(diskId, 0);
    }
    for (const auto &ptEntry : ptView) {
        auto countIter = assignedPtCount.find(ptEntry.second.masterDiskId);
        if (countIter != assignedPtCount.end()) {
            ++countIter->second;
        }
    }

    uint32_t changedPtCount = 0;
    for (auto &ptEntry : ptView) {
        CmPtInfo &pt = ptEntry.second;
        if (pt.masterDiskId != failedDiskId || pt.state == CM_PT_FAULT) {
            continue;
        }

        ++pt.version;
        ++changedPtCount;
        changedPts.emplace_back(ptEntry.first, pt.version);
        if (healthyDisks.empty()) {
            pt.state = CM_PT_FAULT;
            pt.copys.clear();
            pt.copys.push_back({ localNid.VNodeId(), failedDiskId, CM_COPY_DOWN });
            continue;
        }

        uint16_t targetDiskId = healthyDisks.front();
        uint32_t maxDeficit = 0;
        for (uint16_t diskId : healthyDisks) {
            uint32_t assigned = assignedPtCount[diskId];
            uint32_t target = targetPtCount[diskId];
            uint32_t deficit = target > assigned ? target - assigned : 0;
            if (deficit > maxDeficit || (deficit == maxDeficit && diskId < targetDiskId)) {
                maxDeficit = deficit;
                targetDiskId = diskId;
            }
        }
        if (UNLIKELY(maxDeficit == 0)) {
            LOG_ERROR("Standalone disk fault cannot reach capacity target, failedDiskId:" << failedDiskId <<
                ", ptId:" << pt.ptId << ".");
            return BIO_ERR;
        }
        ++assignedPtCount[targetDiskId];
        pt.state = CM_PT_NORMAL;
        pt.masterNodeId = localNid.VNodeId();
        pt.masterDiskId = targetDiskId;
        pt.copys.clear();
        pt.copys.push_back({ localNid.VNodeId(), targetDiskId, CM_COPY_RUNNING });
    }

    LOG_INFO("Standalone pt view after disk fault rebuild, diskId:" << failedDiskId << ", ptView:" <<
        FormatPtView(ptView) << ".");
    LOG_INFO("Rebuild standalone view for disk fault, diskId:" << failedDiskId << ", changedPtCount:" <<
        changedPtCount << ", healthyDiskCount:" << healthyDisks.size() << ".");
    return BIO_OK;
}

BResult StandaloneView::AddDisk(uint16_t diskId, int64_t diskCapacity,
    const std::vector<int64_t> &currentDiskCaps, const CmNodeId &localNid, NodeView &nodeView, PtView &ptView,
    std::vector<CmPtInfo> &changedPts)
{
    changedPts.clear();
    auto nodeIter = nodeView.find(localNid);
    if (nodeIter == nodeView.end() || ptView.empty() || diskCapacity <= 0) {
        LOG_ERROR("Invalid standalone add disk view, diskId:" << diskId << ", capacity:" << diskCapacity << ".");
        return BIO_INVALID_PARAM;
    }
    for (const auto &disk : nodeIter->second.disks) {
        if (disk.diskId == diskId) {
            return BIO_EXISTS;
        }
    }

    std::vector<int64_t> diskCaps = currentDiskCaps;
    if (diskId != diskCaps.size()) {
        LOG_ERROR("Standalone add disk id is not contiguous, diskId:" << diskId << ", diskCapNum:" <<
            diskCaps.size() << ".");
        return BIO_INVALID_PARAM;
    }
    diskCaps.emplace_back(diskCapacity);
    nodeIter->second.disks.push_back({ diskId, CM_DISK_NORMAL });

    std::vector<uint16_t> normalDisks;
    for (const auto &disk : nodeIter->second.disks) {
        if (disk.diskStatus == CM_DISK_NORMAL && (disk.diskId == diskId || !IsDiskFault(disk.diskId))) {
            normalDisks.push_back(disk.diskId);
        }
    }
    std::map<uint16_t, uint32_t> targetPtCount;
    BResult ret = ComputePtTargetDistribution(normalDisks, diskCaps, static_cast<uint32_t>(ptView.size()), targetPtCount);
    if (ret != BIO_OK) {
        return ret;
    }

    std::map<uint16_t, std::vector<uint16_t>> ptsByDisk;
    for (const auto &ptEntry : ptView) {
        ptsByDisk[ptEntry.second.masterDiskId].push_back(ptEntry.first);
    }
    uint32_t needMove = targetPtCount[diskId];
    std::vector<uint16_t> changedPtIds;
    for (const auto &diskPts : ptsByDisk) {
        if (diskPts.first == diskId || changedPtIds.size() >= needMove) {
            continue;
        }
        uint32_t target = targetPtCount.count(diskPts.first) == 0 ? 0 : targetPtCount[diskPts.first];
        uint32_t surplus = diskPts.second.size() > target ? diskPts.second.size() - target : 0;
        for (uint16_t ptId : diskPts.second) {
            if (surplus == 0 || changedPtIds.size() >= needMove) {
                break;
            }
            if (ptView[ptId].version == std::numeric_limits<uint64_t>::max()) {
                LOG_ERROR("Standalone pt version overflow, ptId:" << ptId << ".");
                return BIO_ERR;
            }
            changedPtIds.push_back(ptId);
            --surplus;
        }
    }
    if (changedPtIds.size() != needMove) {
        LOG_ERROR("Standalone add disk cannot reach target, diskId:" << diskId << ", target:" << needMove <<
            ", changed:" << changedPtIds.size() << ".");
        return BIO_ERR;
    }

    LOG_INFO("Standalone pt view before add disk rebuild, diskId:" << diskId << ", ptView:" <<
        FormatPtView(ptView) << ".");
    for (uint16_t ptId : changedPtIds) {
        CmPtInfo &pt = ptView[ptId];
        changedPts.push_back(pt);
        ++pt.version;
        pt.state = CM_PT_NORMAL;
        pt.masterNodeId = localNid.VNodeId();
        pt.masterDiskId = diskId;
        pt.copys.clear();
        pt.copys.push_back({ localNid.VNodeId(), diskId, CM_COPY_RUNNING });
    }
    LOG_INFO("Standalone pt view after add disk rebuild, diskId:" << diskId << ", ptView:" <<
        FormatPtView(ptView) << ".");
    LOG_INFO("Rebuild standalone view for add disk, diskId:" << diskId << ", changedPtCount:" <<
        changedPtIds.size() << ", targetPtCount:" << needMove << ".");
    return BIO_OK;
}

BResult StandaloneView::RejoinDisk(uint16_t diskId, const std::vector<int64_t> &currentDiskCaps,
    const CmNodeId &localNid, NodeView &nodeView, PtView &ptView, std::vector<CmPtInfo> &changedPts)
{
    changedPts.clear();
    auto nodeIter = nodeView.find(localNid);
    if (nodeIter == nodeView.end() || ptView.empty()) {
        LOG_ERROR("Invalid standalone rejoin disk view, diskId:" << diskId << ".");
        return BIO_INVALID_PARAM;
    }
    if (diskId >= currentDiskCaps.size() || currentDiskCaps[diskId] <= 0) {
        LOG_ERROR("Invalid standalone rejoin disk capacity, diskId:" << diskId << ", diskCapNum:" <<
            currentDiskCaps.size() << ".");
        return BIO_INVALID_PARAM;
    }

    bool foundDisk = false;
    for (auto &disk : nodeIter->second.disks) {
        if (disk.diskId != diskId) {
            continue;
        }
        foundDisk = true;
        if (disk.diskStatus == CM_DISK_NORMAL) {
            return BIO_EXISTS;
        }
        if (disk.diskStatus != CM_DISK_FAULT) {
            LOG_ERROR("Standalone rejoin disk with invalid status, diskId:" << diskId << ", status:" <<
                disk.diskStatus << ".");
            return BIO_INVALID_PARAM;
        }
        disk.diskStatus = CM_DISK_NORMAL;
    }
    if (!foundDisk) {
        LOG_ERROR("Standalone rejoin disk is missing from node view, diskId:" << diskId << ".");
        return BIO_INVALID_PARAM;
    }

    // A faulted disk owns no usable PTs: after failover its NORMAL PTs were
    // moved away, and the only entries that can still point at it are the
    // PT_FAULT copies left by an all-disk fault. Those are not repaired in
    // place; they are re-assigned below through the same migration path as
    // every other PT: version++, NORMAL, copy RUNNING.
    std::vector<uint16_t> normalDisks;
    for (const auto &disk : nodeIter->second.disks) {
        if (disk.diskStatus == CM_DISK_NORMAL && (disk.diskId == diskId || !IsDiskFault(disk.diskId))) {
            normalDisks.push_back(disk.diskId);
        }
    }

    // Reuse the add-disk PT assignment unchanged. Every PT participates in the
    // target calculation, and disks that are not in the healthy set have
    // target 0, so their PTs (including PT_FAULT left on another failed disk)
    // become donors and are repaired onto the rejoined disk. This keeps any
    // rejoin order convergent to the same capacity-based distribution.
    std::map<uint16_t, std::vector<uint16_t>> ptsByDisk;
    for (const auto &ptEntry : ptView) {
        ptsByDisk[ptEntry.second.masterDiskId].push_back(ptEntry.first);
    }

    std::map<uint16_t, uint32_t> targetPtCount;
    BResult ret = ComputePtTargetDistribution(normalDisks, currentDiskCaps, static_cast<uint32_t>(ptView.size()),
        targetPtCount);
    if (ret != BIO_OK) {
        return ret;
    }

    // Only NORMAL PTs already on this disk count as usable; PT_FAULT entries
    // whose masterDiskId still points at it are failed copies and must be
    // re-assigned below like any other PT.
    uint32_t currentPtCount = 0;
    for (uint16_t ptId : ptsByDisk[diskId]) {
        if (ptView[ptId].state == CM_PT_NORMAL) {
            ++currentPtCount;
        }
    }
    uint32_t needMove = targetPtCount[diskId] > currentPtCount ? targetPtCount[diskId] - currentPtCount : 0;
    std::vector<uint16_t> changedPtIds;
    for (const auto &diskPts : ptsByDisk) {
        if (changedPtIds.size() >= needMove) {
            break;
        }
        if (diskPts.first == diskId) {
            // Writable PTs on this disk stay; its failed PTs go through the
            // same reassignment path as donors on other disks.
            for (uint16_t ptId : diskPts.second) {
                if (changedPtIds.size() >= needMove) {
                    break;
                }
                if (ptView[ptId].state == CM_PT_NORMAL) {
                    continue;
                }
                if (ptView[ptId].version == std::numeric_limits<uint64_t>::max()) {
                    LOG_ERROR("Standalone pt version overflow, ptId:" << ptId << ".");
                    return BIO_ERR;
                }
                changedPtIds.push_back(ptId);
            }
            continue;
        }
        uint32_t target = targetPtCount.count(diskPts.first) == 0 ? 0 : targetPtCount[diskPts.first];
        uint32_t surplus = diskPts.second.size() > target ? diskPts.second.size() - target : 0;
        for (uint16_t ptId : diskPts.second) {
            if (surplus == 0 || changedPtIds.size() >= needMove) {
                break;
            }
            if (ptView[ptId].version == std::numeric_limits<uint64_t>::max()) {
                LOG_ERROR("Standalone pt version overflow, ptId:" << ptId << ".");
                return BIO_ERR;
            }
            changedPtIds.push_back(ptId);
            --surplus;
        }
    }
    if (changedPtIds.size() != needMove) {
        LOG_ERROR("Standalone rejoin disk cannot reach target, diskId:" << diskId << ", target:" << needMove <<
            ", changed:" << changedPtIds.size() << ".");
        return BIO_ERR;
    }

    LOG_INFO("Standalone pt view before rejoin disk rebuild, diskId:" << diskId << ", ptView:" <<
        FormatPtView(ptView) << ".");
    for (uint16_t ptId : changedPtIds) {
        CmPtInfo &pt = ptView[ptId];
        changedPts.push_back(pt);
        ++pt.version;
        pt.state = CM_PT_NORMAL;
        pt.masterNodeId = localNid.VNodeId();
        pt.masterDiskId = diskId;
        pt.copys.clear();
        pt.copys.push_back({ localNid.VNodeId(), diskId, CM_COPY_RUNNING });
    }
    LOG_INFO("Standalone pt view after rejoin disk rebuild, diskId:" << diskId << ", ptView:" <<
        FormatPtView(ptView) << ".");
    LOG_INFO("Rebuild standalone view for rejoin disk, diskId:" << diskId << ", changedPtCount:" <<
        changedPtIds.size() << ", currentPtCount:" << currentPtCount << ", targetPtCount:" << needMove << ".");
    return BIO_OK;
}
}
}
