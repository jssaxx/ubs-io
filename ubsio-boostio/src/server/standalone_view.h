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

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
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
    using FaultHandler = std::function<BResult(uint16_t diskId)>;

    StandaloneView() = default;
    ~StandaloneView();

    // Build a single-node view.
    // localNid, nodeView, and ptView are all output parameters. The generated
    // PT ids are contiguous [0, ptNum), each PT has one local copy, and every
    // PT version starts from the standalone constant version. Configured cache
    // disks have already been narrowed to the current process' selected disks.
    static BResult Build(const BioConfigPtr &config, CmNodeId &localNid, NodeView &nodeView, PtView &ptView);

    BResult Start(uint32_t diskNum, FaultHandler handler);
    void Stop();

    bool IsDiskFault(uint16_t diskId) const;

    // True only while a reported fault has not been failed over yet. The fault
    // worker collects pending ids before it takes the server side update lock,
    // so a failover must confirm the fault is still pending: a rolled back add
    // disk may have untracked that id, and a retry may have re-added it as a
    // healthy disk.
    bool IsDiskFaultPending(uint16_t diskId) const;

    // Rollback helpers for add disk. TrackDisk only accepts the next contiguous
    // id and UntrackDisk only removes the last one, so a failed add disk must
    // untrack before it returns, otherwise the tracked disk count stays ahead of
    // the config disk list and every retry fails on the contiguous id check.
    BResult TrackDisk(uint16_t diskId);
    BResult UntrackDisk(uint16_t diskId);

    // Rejoin-safe fault-state helpers. Recoverable means NORMAL or
    // FAULT_HANDLED; a disk whose failover worker is still pending must be
    // retried later so a stale fault task cannot re-fault a recovered disk.
    BResult CheckDiskRecoverable(uint16_t diskId) const;
    BResult MarkDiskRecovered(uint16_t diskId);

    BResult FailoverDisk(uint16_t failedDiskId, const std::vector<int64_t> &currentDiskCaps,
        const CmNodeId &localNid, NodeView &nodeView, PtView &ptView,
        std::vector<std::pair<uint16_t, uint64_t>> &changedPts);

    BResult RejoinDisk(uint16_t diskId, const std::vector<int64_t> &currentDiskCaps, const CmNodeId &localNid,
        NodeView &nodeView, PtView &ptView, std::vector<CmPtInfo> &changedPts);

    BResult AddDisk(uint16_t diskId, int64_t diskCapacity, const std::vector<int64_t> &currentDiskCaps,
        const CmNodeId &localNid, NodeView &nodeView, PtView &ptView, std::vector<CmPtInfo> &changedPts);

private:
    enum class DiskState : uint8_t {
        NORMAL = 0,
        FAULT_PENDING,
        FAULT_HANDLED,
    };

    static int32_t HandleDiskFault(uint16_t diskId, void *context);
    int32_t ReportDiskFault(uint16_t diskId);
    void FaultWorker();
    bool HasPendingFaultLocked() const;

private:
    mutable std::mutex mFaultMutex;
    std::condition_variable mFaultCv;
    std::thread mFaultThread;
    std::vector<DiskState> mDiskStates;
    FaultHandler mFaultHandler;
    bool mRunning{ false };
};
}
}

#endif // STANDALONE_VIEW_H
