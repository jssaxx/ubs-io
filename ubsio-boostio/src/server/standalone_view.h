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

#ifndef STANDALONE_VIEW_H
#define STANDALONE_VIEW_H

#include <map>
#include "bio_config_instance.h"
#include "cm.h"

namespace ock {
namespace bio {
// Builds the synthetic CM views used by STANDALONE mode.
// In standalone deployment there is no CM/Zookeeper process to publish node
// and PT metadata. This helper keeps that responsibility isolated from
// BioServer: it reads the local daemon config and BDM disk state, then creates
// a one-node NodeView and a one-copy PtView. BioServer narrows the daemon disk
// config to the current standalone device's selected disks before this helper runs.
// Client and server both read this same view through the normal
// GetNodeView/GetPtView exported functions.
class StandaloneView {
public:
    using NodeView = std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp>;
    using PtView = std::map<uint16_t, CmPtInfo>;

    // Build a single-node view.
    // localNid, nodeView, and ptView are all output parameters. The generated
    // PT ids are contiguous [0, ptNum), each PT has one local copy, and every
    // PT version starts from the standalone constant version. Configured cache
    // disks have already been narrowed to the current process' selected disks.
    static BResult Build(const BioConfigPtr &config, CmNodeId &localNid, NodeView &nodeView, PtView &ptView);
};
}
}

#endif // STANDALONE_VIEW_H
