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
#include <cstdint>
#include <vector>
#include "bdm_core.h"
#include "bio_log.h"
#include "standalone_view.h"

namespace ock {
namespace bio {
namespace {
constexpr uint16_t STANDALONE_NODE_ID = 0;
constexpr uint64_t STANDALONE_PT_VERSION = 1;

CmDiskStatus ToCmDiskStatus(uint16_t diskId)
{
    return BdmGetDiskStatus(diskId) == BDM_DISK_STATE_NORMAL ? CM_DISK_NORMAL : CM_DISK_FAULT;
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
    ptView.clear();
    for (uint16_t ptId = 0; ptId < ptNum; ++ptId) {
        uint16_t diskId = ptId % diskNum;
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
}
}
