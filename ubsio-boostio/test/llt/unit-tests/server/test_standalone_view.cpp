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

#include <chrono>
#include <condition_variable>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

#define private public
#include "standalone_view.h"
#undef private

#include "bdm_core.h"

using namespace ock::bio;

namespace {
constexpr int64_t TEST_DISK_CAP = 1024 * 1024 * 1024;

class DiskStatusGuard {
public:
    explicit DiskStatusGuard(std::vector<uint32_t> diskIdsParam) : diskIds(std::move(diskIdsParam))
    {
        for (uint32_t diskId : diskIds) {
            oldStatus.emplace_back(BdmGetDiskStatus(diskId));
        }
    }

    ~DiskStatusGuard()
    {
        for (size_t index = 0; index < diskIds.size(); index++) {
            BdmSetDiskUsedStatus(diskIds[index], oldStatus[index] == BDM_DISK_STATE_NORMAL);
        }
    }

    std::vector<uint32_t> diskIds;
    std::vector<BdmDiskState> oldStatus;
};

BioConfigPtr MakeViewConfig(uint32_t diskNum, int32_t ptNum)
{
    auto config = MakeRef<BioConfig>();
    config->mCmConfig.ptNum = ptNum;
    config->mCmConfig.groupId = 0;
    config->mNetConfig.dataIp = "127.0.0.1";
    config->mNetConfig.dataPort = 7300;
    for (uint32_t diskId = 0; diskId < diskNum; diskId++) {
        config->mDaemonConfig.diskList.emplace_back("disk" + std::to_string(diskId));
        config->mDaemonConfig.diskCaps.emplace_back(TEST_DISK_CAP);
    }
    return config;
}

void ExpectPtOnDisk(const StandaloneView::PtView &ptView, uint16_t ptId, uint16_t diskId, CmPtState state,
    CmCopyState copyState)
{
    auto iter = ptView.find(ptId);
    ASSERT_NE(iter, ptView.end());
    EXPECT_EQ(iter->second.ptId, ptId);
    EXPECT_EQ(iter->second.masterNodeId, 0);
    EXPECT_EQ(iter->second.masterDiskId, diskId);
    EXPECT_EQ(iter->second.state, state);
    ASSERT_EQ(iter->second.copys.size(), 1);
    EXPECT_EQ(iter->second.copys[0].nodeId, 0);
    EXPECT_EQ(iter->second.copys[0].diskId, diskId);
    EXPECT_EQ(iter->second.copys[0].state, copyState);
}

uint32_t CountPtsOnDisk(const StandaloneView::PtView &ptView, uint16_t diskId)
{
    return static_cast<uint32_t>(std::count_if(ptView.begin(), ptView.end(), [diskId](const auto &entry) {
        return entry.second.masterDiskId == diskId;
    }));
}
}

TEST(TestStandaloneView, build_rejects_null_and_empty_disk_config)
{
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    BioConfigPtr nullConfig;

    EXPECT_EQ(StandaloneView::Build(nullConfig, localNid, nodeView, ptView), BIO_INVALID_PARAM);

    auto emptyDiskConfig = MakeViewConfig(0, 4);
    EXPECT_EQ(StandaloneView::Build(emptyDiskConfig, localNid, nodeView, ptView), BIO_INVALID_PARAM);
}

TEST(TestStandaloneView, build_creates_single_node_view_and_ordered_pt_view)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);

    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 5);

    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    EXPECT_EQ(localNid.VNodeId(), 0);
    ASSERT_EQ(nodeView.size(), 1);
    auto nodeIter = nodeView.find(localNid);
    ASSERT_NE(nodeIter, nodeView.end());
    EXPECT_EQ(nodeIter->second.id.VNodeId(), 0);
    EXPECT_EQ(nodeIter->second.ip, "127.0.0.1");
    ASSERT_EQ(nodeIter->second.disks.size(), 2);
    EXPECT_EQ(nodeIter->second.disks[0].diskStatus, CM_DISK_NORMAL);
    EXPECT_EQ(nodeIter->second.disks[1].diskStatus, CM_DISK_NORMAL);

    ASSERT_EQ(ptView.size(), 5);
    ExpectPtOnDisk(ptView, 0, 0, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 1, 0, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 2, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 3, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 4, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
}

TEST(TestStandaloneView, build_assigns_pt_count_by_disk_capacity)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);

    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 16);
    config->mDaemonConfig.diskCaps[1] = TEST_DISK_CAP * 2;

    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    std::vector<uint32_t> ptCountByDisk(2, 0);
    for (const auto &item : ptView) {
        ASSERT_LT(item.second.masterDiskId, ptCountByDisk.size());
        ptCountByDisk[item.second.masterDiskId]++;
    }
    EXPECT_EQ(ptCountByDisk[0], 5);
    EXPECT_EQ(ptCountByDisk[1], 11);
    ExpectPtOnDisk(ptView, 0, 0, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 4, 0, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 5, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 15, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
}

TEST(TestStandaloneView, build_without_disk_cache_uses_virtual_disk)
{
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(0, 3);
    config->mDaemonConfig.hasDiskCache = false;

    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    EXPECT_EQ(localNid.VNodeId(), 0);
    ASSERT_EQ(nodeView.size(), 1);
    auto nodeIter = nodeView.find(localNid);
    ASSERT_NE(nodeIter, nodeView.end());
    ASSERT_EQ(nodeIter->second.disks.size(), 1);
    EXPECT_EQ(nodeIter->second.disks[0].diskId, 0);
    EXPECT_EQ(nodeIter->second.disks[0].diskStatus, CM_DISK_NORMAL);

    ASSERT_EQ(ptView.size(), 3);
    for (uint16_t ptId = 0; ptId < 3; ptId++) {
        ExpectPtOnDisk(ptView, ptId, 0, CM_PT_NORMAL, CM_COPY_RUNNING);
    }
}

TEST(TestStandaloneView, build_excludes_fault_disk_from_pt_assignment)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);

    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(3, 16);
    config->mDaemonConfig.diskCaps[0] = TEST_DISK_CAP;
    config->mDaemonConfig.diskCaps[1] = TEST_DISK_CAP * 2;
    config->mDaemonConfig.diskCaps[2] = TEST_DISK_CAP * 8;

    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    ASSERT_EQ(nodeView.size(), 1);
    auto nodeIter = nodeView.find(localNid);
    ASSERT_NE(nodeIter, nodeView.end());
    ASSERT_EQ(nodeIter->second.disks.size(), 3);
    EXPECT_EQ(nodeIter->second.disks[0].diskStatus, CM_DISK_NORMAL);
    EXPECT_EQ(nodeIter->second.disks[1].diskStatus, CM_DISK_NORMAL);
    EXPECT_EQ(nodeIter->second.disks[2].diskStatus, CM_DISK_FAULT);

    ASSERT_EQ(ptView.size(), 16);
    for (uint16_t ptId = 0; ptId < 5; ++ptId) {
        ExpectPtOnDisk(ptView, ptId, 0, CM_PT_NORMAL, CM_COPY_RUNNING);
    }
    for (uint16_t ptId = 5; ptId < 16; ++ptId) {
        ExpectPtOnDisk(ptView, ptId, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
    }
}

TEST(TestStandaloneView, build_preserves_normal_disk_id_when_first_disk_is_fault)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, false);
    BdmSetDiskUsedStatus(1, true);

    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 4);

    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    ASSERT_EQ(nodeView.size(), 1);
    auto nodeIter = nodeView.find(localNid);
    ASSERT_NE(nodeIter, nodeView.end());
    ASSERT_EQ(nodeIter->second.disks.size(), 2);
    EXPECT_EQ(nodeIter->second.disks[0].diskStatus, CM_DISK_FAULT);
    EXPECT_EQ(nodeIter->second.disks[1].diskStatus, CM_DISK_NORMAL);

    ASSERT_EQ(ptView.size(), 4);
    for (uint16_t ptId = 0; ptId < 4; ++ptId) {
        ExpectPtOnDisk(ptView, ptId, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
    }
}

TEST(TestStandaloneView, failover_assigns_changed_pts_by_healthy_disk_capacity)
{
    CmNodeId localNid(0, 0);
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(3, 16);
    config->mDaemonConfig.diskCaps[0] = TEST_DISK_CAP * 4;
    config->mDaemonConfig.diskCaps[1] = TEST_DISK_CAP;
    config->mDaemonConfig.diskCaps[2] = TEST_DISK_CAP * 3;
    std::vector<CmDiskInfo> disks = { { 0, CM_DISK_NORMAL }, { 1, CM_DISK_NORMAL }, { 2, CM_DISK_NORMAL } };
    nodeView.emplace(localNid, CmNodeInfo(localNid, "127.0.0.1", 7300, CM_NODE_NORMAL, disks));
    for (uint16_t ptId = 0; ptId < 16; ++ptId) {
        uint16_t diskId = ptId < 8 ? 0 : (ptId < 10 ? 1 : 2);
        std::vector<CmPtCopy> copys = { { localNid.VNodeId(), diskId, CM_COPY_RUNNING } };
        ptView.emplace(ptId, CmPtInfo(1, ptId, CM_PT_NORMAL, localNid.VNodeId(), diskId, copys));
    }

    StandaloneView standaloneView;
    std::vector<std::pair<uint16_t, uint64_t>> changedPts;
    ASSERT_EQ(standaloneView.FailoverDisk(2, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView, changedPts),
        BIO_OK);

    std::vector<uint32_t> ptCountByDisk(3, 0);
    for (const auto &item : ptView) {
        ASSERT_LT(item.second.masterDiskId, ptCountByDisk.size());
        ptCountByDisk[item.second.masterDiskId]++;
    }
    EXPECT_EQ(ptCountByDisk[0], 13);
    EXPECT_EQ(ptCountByDisk[1], 3);
    EXPECT_EQ(ptCountByDisk[2], 0);
    EXPECT_EQ(changedPts.size(), 6);
}

TEST(TestStandaloneView, track_disk_requires_contiguous_id_and_allows_retry_after_rollback)
{
    StandaloneView standaloneView;
    standaloneView.mDiskStates.assign(2, StandaloneView::DiskState::NORMAL);

    EXPECT_EQ(standaloneView.TrackDisk(3), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.TrackDisk(2), BIO_OK);
    EXPECT_EQ(standaloneView.mDiskStates.size(), 3);

    // Only the last id can be untracked, so a failed add disk has to undo the
    // disk it just tracked before it returns.
    EXPECT_EQ(standaloneView.UntrackDisk(1), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.UntrackDisk(2), BIO_OK);
    EXPECT_EQ(standaloneView.mDiskStates.size(), 2);

    // The retry after a rolled back add disk gets the same id again instead of
    // failing forever on the contiguous id check.
    EXPECT_EQ(standaloneView.TrackDisk(2), BIO_OK);
    EXPECT_EQ(standaloneView.mDiskStates.size(), 3);
    EXPECT_FALSE(standaloneView.IsDiskFault(2));
}

TEST(TestStandaloneView, fault_worker_drops_fault_of_disk_untracked_by_rollback)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);

    StandaloneView standaloneView;
    std::mutex handledMutex;
    std::condition_variable handledCv;
    std::vector<uint16_t> handledDisks;
    auto handler = [&](uint16_t diskId) {
        // Emulate an add disk rollback that untracks the new disk after the
        // fault worker has already collected its id.
        EXPECT_EQ(standaloneView.UntrackDisk(diskId), BIO_OK);
        std::lock_guard<std::mutex> lock(handledMutex);
        handledDisks.emplace_back(diskId);
        handledCv.notify_all();
        return BIO_OK;
    };
    ASSERT_EQ(standaloneView.Start(2, handler), BIO_OK);
    ASSERT_EQ(standaloneView.TrackDisk(2), BIO_OK);
    ASSERT_EQ(standaloneView.ReportDiskFault(2), BIO_OK);

    {
        std::unique_lock<std::mutex> lock(handledMutex);
        ASSERT_TRUE(handledCv.wait_for(lock, std::chrono::seconds(5), [&]() { return !handledDisks.empty(); }));
        EXPECT_EQ(handledDisks.size(), 1);
        EXPECT_EQ(handledDisks[0], 2);
    }

    standaloneView.Stop();
    // The untracked id must not be written back, and the disks that stay
    // tracked must keep their normal state.
    ASSERT_EQ(standaloneView.mDiskStates.size(), 2);
    EXPECT_EQ(standaloneView.mDiskStates[0], StandaloneView::DiskState::NORMAL);
    EXPECT_EQ(standaloneView.mDiskStates[1], StandaloneView::DiskState::NORMAL);
}

TEST(TestStandaloneView, build_validates_capacity_and_fault_topology)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;

    auto incompleteCaps = MakeViewConfig(2, 4);
    incompleteCaps->mDaemonConfig.diskCaps.pop_back();
    EXPECT_EQ(StandaloneView::Build(incompleteCaps, localNid, nodeView, ptView), BIO_INVALID_PARAM);

    auto invalidCap = MakeViewConfig(2, 4);
    invalidCap->mDaemonConfig.diskCaps[1] = 0;
    EXPECT_EQ(StandaloneView::Build(invalidCap, localNid, nodeView, ptView), BIO_INVALID_PARAM);

    BdmSetDiskUsedStatus(0, false);
    BdmSetDiskUsedStatus(1, false);
    auto allFault = MakeViewConfig(2, 4);
    EXPECT_EQ(StandaloneView::Build(allFault, localNid, nodeView, ptView), BIO_ERR);
}

TEST(TestStandaloneView, build_clamps_zero_pt_count_and_rejects_oversized_count)
{
    DiskStatusGuard guard({ 0 });
    BdmSetDiskUsedStatus(0, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;

    auto zeroPt = MakeViewConfig(1, 0);
    ASSERT_EQ(StandaloneView::Build(zeroPt, localNid, nodeView, ptView), BIO_OK);
    ASSERT_EQ(ptView.size(), 1);
    ExpectPtOnDisk(ptView, 0, 0, CM_PT_NORMAL, CM_COPY_RUNNING);

    auto oversizedPt = MakeViewConfig(1, static_cast<int32_t>(UINT16_MAX) + 1);
    EXPECT_EQ(StandaloneView::Build(oversizedPt, localNid, nodeView, ptView), BIO_INVALID_PARAM);
}

TEST(TestStandaloneView, start_and_fault_callback_validate_inputs)
{
    StandaloneView standaloneView;
    auto handler = [](uint16_t) { return BIO_OK; };

    EXPECT_EQ(standaloneView.Start(0, handler), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.Start(1, nullptr), BIO_INVALID_PARAM);
    EXPECT_EQ(StandaloneView::HandleDiskFault(0, nullptr), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.ReportDiskFault(0), BIO_OK);
    EXPECT_FALSE(standaloneView.IsDiskFault(0));

    standaloneView.Stop();
    standaloneView.Stop();
}

TEST(TestStandaloneView, report_disk_fault_validates_id_and_deduplicates)
{
    StandaloneView standaloneView;
    standaloneView.mRunning = true;
    standaloneView.mDiskStates.assign(1, StandaloneView::DiskState::NORMAL);

    EXPECT_EQ(standaloneView.ReportDiskFault(1), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.ReportDiskFault(0), BIO_OK);
    EXPECT_TRUE(standaloneView.IsDiskFault(0));
    EXPECT_TRUE(standaloneView.IsDiskFaultPending(0));
    EXPECT_EQ(standaloneView.ReportDiskFault(0), BIO_OK);
    EXPECT_EQ(standaloneView.mDiskStates[0], StandaloneView::DiskState::FAULT_PENDING);

    standaloneView.mRunning = false;
}

TEST(TestStandaloneView, fault_worker_retries_failed_handler)
{
    DiskStatusGuard guard({ 0 });
    BdmSetDiskUsedStatus(0, true);
    StandaloneView standaloneView;
    std::atomic<uint32_t> attempts{ 0 };
    std::mutex handledMutex;
    std::condition_variable handledCv;
    auto handler = [&](uint16_t diskId) {
        EXPECT_EQ(diskId, 0);
        uint32_t current = ++attempts;
        if (current == 1) {
            return BIO_ERR;
        }
        handledCv.notify_all();
        return BIO_OK;
    };
    ASSERT_EQ(standaloneView.Start(1, handler), BIO_OK);
    ASSERT_EQ(standaloneView.ReportDiskFault(0), BIO_OK);

    {
        std::unique_lock<std::mutex> lock(handledMutex);
        ASSERT_TRUE(handledCv.wait_for(lock, std::chrono::seconds(5), [&]() { return attempts.load() >= 2; }));
    }
    for (uint32_t retry = 0; retry < 100 && standaloneView.IsDiskFaultPending(0); ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(attempts.load(), 2);
    EXPECT_TRUE(standaloneView.IsDiskFault(0));
    EXPECT_FALSE(standaloneView.IsDiskFaultPending(0));
    standaloneView.Stop();
}

TEST(TestStandaloneView, disk_recovery_helpers_cover_all_states)
{
    StandaloneView standaloneView;
    standaloneView.mDiskStates = { StandaloneView::DiskState::NORMAL, StandaloneView::DiskState::FAULT_PENDING,
        StandaloneView::DiskState::FAULT_HANDLED };

    EXPECT_EQ(standaloneView.CheckDiskRecoverable(3), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.CheckDiskRecoverable(0), BIO_OK);
    EXPECT_EQ(standaloneView.CheckDiskRecoverable(1), BIO_INNER_RETRY);
    EXPECT_EQ(standaloneView.CheckDiskRecoverable(2), BIO_OK);
    EXPECT_EQ(standaloneView.MarkDiskRecovered(3), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.MarkDiskRecovered(1), BIO_INNER_RETRY);
    EXPECT_EQ(standaloneView.MarkDiskRecovered(2), BIO_OK);
    EXPECT_FALSE(standaloneView.IsDiskFault(2));
}

TEST(TestStandaloneView, failover_validates_node_and_disk_presence)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 4);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    StandaloneView standaloneView;
    std::vector<std::pair<uint16_t, uint64_t>> changedPts;
    CmNodeId missingNid(0, 9);
    EXPECT_EQ(standaloneView.FailoverDisk(0, config->mDaemonConfig.diskCaps, missingNid, nodeView, ptView,
        changedPts), BIO_ERR);
    EXPECT_EQ(standaloneView.FailoverDisk(9, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_INVALID_PARAM);
    EXPECT_TRUE(changedPts.empty());
}

TEST(TestStandaloneView, failover_without_healthy_disk_marks_pts_fault)
{
    DiskStatusGuard guard({ 0 });
    BdmSetDiskUsedStatus(0, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(1, 3);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    StandaloneView standaloneView;
    std::vector<std::pair<uint16_t, uint64_t>> changedPts;
    ASSERT_EQ(standaloneView.FailoverDisk(0, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_OK);
    ASSERT_EQ(changedPts.size(), 3);
    for (uint16_t ptId = 0; ptId < 3; ++ptId) {
        ExpectPtOnDisk(ptView, ptId, 0, CM_PT_FAULT, CM_COPY_DOWN);
        EXPECT_EQ(ptView[ptId].version, 2);
    }
}

TEST(TestStandaloneView, failover_skips_preexisting_fault_pt)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 4);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);
    ptView[0].state = CM_PT_FAULT;
    ptView[0].copys[0].state = CM_COPY_DOWN;

    StandaloneView standaloneView;
    std::vector<std::pair<uint16_t, uint64_t>> changedPts;
    ASSERT_EQ(standaloneView.FailoverDisk(0, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_OK);
    ASSERT_EQ(changedPts.size(), 1);
    EXPECT_EQ(changedPts[0].first, 1);
    ExpectPtOnDisk(ptView, 0, 0, CM_PT_FAULT, CM_COPY_DOWN);
    ExpectPtOnDisk(ptView, 1, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
}

TEST(TestStandaloneView, failover_rejects_invalid_healthy_capacity)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 4);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);
    config->mDaemonConfig.diskCaps[1] = 0;

    StandaloneView standaloneView;
    std::vector<std::pair<uint16_t, uint64_t>> changedPts;
    EXPECT_EQ(standaloneView.FailoverDisk(0, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_INVALID_PARAM);
    EXPECT_TRUE(changedPts.empty());
}

TEST(TestStandaloneView, add_disk_validates_view_and_capacity)
{
    DiskStatusGuard guard({ 0 });
    BdmSetDiskUsedStatus(0, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(1, 4);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    StandaloneView standaloneView;
    std::vector<CmPtInfo> changedPts;
    CmNodeId missingNid(0, 9);
    EXPECT_EQ(standaloneView.AddDisk(1, TEST_DISK_CAP, config->mDaemonConfig.diskCaps, missingNid, nodeView,
        ptView, changedPts), BIO_INVALID_PARAM);
    StandaloneView::PtView emptyPtView;
    EXPECT_EQ(standaloneView.AddDisk(1, TEST_DISK_CAP, config->mDaemonConfig.diskCaps, localNid, nodeView,
        emptyPtView, changedPts), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.AddDisk(1, 0, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_INVALID_PARAM);
}

TEST(TestStandaloneView, add_disk_rejects_duplicate_and_noncontiguous_id)
{
    DiskStatusGuard guard({ 0 });
    BdmSetDiskUsedStatus(0, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(1, 4);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    StandaloneView standaloneView;
    std::vector<CmPtInfo> changedPts;
    EXPECT_EQ(standaloneView.AddDisk(0, TEST_DISK_CAP, config->mDaemonConfig.diskCaps, localNid, nodeView,
        ptView, changedPts), BIO_EXISTS);
    EXPECT_EQ(standaloneView.AddDisk(2, TEST_DISK_CAP, config->mDaemonConfig.diskCaps, localNid, nodeView,
        ptView, changedPts), BIO_INVALID_PARAM);
}

TEST(TestStandaloneView, add_disk_moves_target_share_and_preserves_old_snapshots)
{
    DiskStatusGuard guard({ 0 });
    BdmSetDiskUsedStatus(0, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(1, 6);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    StandaloneView standaloneView;
    std::vector<CmPtInfo> changedPts;
    ASSERT_EQ(standaloneView.AddDisk(1, TEST_DISK_CAP, config->mDaemonConfig.diskCaps, localNid, nodeView,
        ptView, changedPts), BIO_OK);
    EXPECT_EQ(CountPtsOnDisk(ptView, 0), 3);
    EXPECT_EQ(CountPtsOnDisk(ptView, 1), 3);
    ASSERT_EQ(changedPts.size(), 3);
    for (const auto &oldPt : changedPts) {
        EXPECT_EQ(oldPt.masterDiskId, 0);
        EXPECT_EQ(oldPt.version, 1);
        EXPECT_EQ(ptView[oldPt.ptId].masterDiskId, 1);
        EXPECT_EQ(ptView[oldPt.ptId].version, 2);
    }
}

TEST(TestStandaloneView, add_disk_rejects_pt_version_overflow)
{
    DiskStatusGuard guard({ 0 });
    BdmSetDiskUsedStatus(0, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(1, 2);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);
    ptView[0].version = std::numeric_limits<uint64_t>::max();

    StandaloneView standaloneView;
    std::vector<CmPtInfo> changedPts;
    EXPECT_EQ(standaloneView.AddDisk(1, TEST_DISK_CAP, config->mDaemonConfig.diskCaps, localNid, nodeView,
        ptView, changedPts), BIO_ERR);
    EXPECT_TRUE(changedPts.empty());
}

TEST(TestStandaloneView, rejoin_disk_validates_input_and_current_state)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 4);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    StandaloneView standaloneView;
    std::vector<CmPtInfo> changedPts;
    CmNodeId missingNid(0, 9);
    EXPECT_EQ(standaloneView.RejoinDisk(1, config->mDaemonConfig.diskCaps, missingNid, nodeView, ptView,
        changedPts), BIO_INVALID_PARAM);
    StandaloneView::PtView emptyPtView;
    EXPECT_EQ(standaloneView.RejoinDisk(1, config->mDaemonConfig.diskCaps, localNid, nodeView, emptyPtView,
        changedPts), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.RejoinDisk(2, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_INVALID_PARAM);
    EXPECT_EQ(standaloneView.RejoinDisk(0, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_EXISTS);

    auto invalidStatusView = nodeView;
    invalidStatusView[localNid].disks[1].diskStatus = static_cast<CmDiskStatus>(2);
    EXPECT_EQ(standaloneView.RejoinDisk(1, config->mDaemonConfig.diskCaps, localNid, invalidStatusView, ptView,
        changedPts), BIO_INVALID_PARAM);
}

TEST(TestStandaloneView, rejoin_disk_restores_fault_pts_to_capacity_target)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 4);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);
    nodeView[localNid].disks[1].diskStatus = CM_DISK_FAULT;
    for (auto &entry : ptView) {
        if (entry.second.masterDiskId == 1) {
            entry.second.state = CM_PT_FAULT;
            entry.second.copys[0].state = CM_COPY_DOWN;
        }
    }

    StandaloneView standaloneView;
    std::vector<CmPtInfo> changedPts;
    ASSERT_EQ(standaloneView.RejoinDisk(1, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_OK);
    EXPECT_EQ(nodeView[localNid].disks[1].diskStatus, CM_DISK_NORMAL);
    EXPECT_EQ(CountPtsOnDisk(ptView, 0), 2);
    EXPECT_EQ(CountPtsOnDisk(ptView, 1), 2);
    ASSERT_EQ(changedPts.size(), 2);
    for (const auto &oldPt : changedPts) {
        EXPECT_EQ(oldPt.state, CM_PT_FAULT);
        ExpectPtOnDisk(ptView, oldPt.ptId, 1, CM_PT_NORMAL, CM_COPY_RUNNING);
        EXPECT_EQ(ptView[oldPt.ptId].version, oldPt.version + 1);
    }
}

TEST(TestStandaloneView, rejoin_disk_rejects_pt_version_overflow)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, true);
    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 2);
    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);
    nodeView[localNid].disks[1].diskStatus = CM_DISK_FAULT;
    ptView[1].state = CM_PT_FAULT;
    ptView[1].copys[0].state = CM_COPY_DOWN;
    ptView[1].version = std::numeric_limits<uint64_t>::max();

    StandaloneView standaloneView;
    std::vector<CmPtInfo> changedPts;
    EXPECT_EQ(standaloneView.RejoinDisk(1, config->mDaemonConfig.diskCaps, localNid, nodeView, ptView,
        changedPts), BIO_ERR);
    EXPECT_TRUE(changedPts.empty());
}
