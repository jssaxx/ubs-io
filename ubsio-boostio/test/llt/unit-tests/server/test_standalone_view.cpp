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

TEST(TestStandaloneView, build_creates_single_node_view_and_round_robin_pt_view)
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
    for (uint16_t ptId = 0; ptId < 5; ptId++) {
        ExpectPtOnDisk(ptView, ptId, ptId % 2, CM_PT_NORMAL, CM_COPY_RUNNING);
    }
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

TEST(TestStandaloneView, build_marks_pt_fault_when_target_disk_is_fault)
{
    DiskStatusGuard guard({ 0, 1 });
    BdmSetDiskUsedStatus(0, true);
    BdmSetDiskUsedStatus(1, false);

    CmNodeId localNid;
    StandaloneView::NodeView nodeView;
    StandaloneView::PtView ptView;
    auto config = MakeViewConfig(2, 4);

    ASSERT_EQ(StandaloneView::Build(config, localNid, nodeView, ptView), BIO_OK);

    ASSERT_EQ(nodeView.size(), 1);
    auto nodeIter = nodeView.find(localNid);
    ASSERT_NE(nodeIter, nodeView.end());
    ASSERT_EQ(nodeIter->second.disks.size(), 2);
    EXPECT_EQ(nodeIter->second.disks[0].diskStatus, CM_DISK_NORMAL);
    EXPECT_EQ(nodeIter->second.disks[1].diskStatus, CM_DISK_FAULT);

    ExpectPtOnDisk(ptView, 0, 0, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 1, 1, CM_PT_FAULT, CM_COPY_DOWN);
    ExpectPtOnDisk(ptView, 2, 0, CM_PT_NORMAL, CM_COPY_RUNNING);
    ExpectPtOnDisk(ptView, 3, 1, CM_PT_FAULT, CM_COPY_DOWN);
}
