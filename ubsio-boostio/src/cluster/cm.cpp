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

#include "cm.h"
#include "securec.h"
#include "bio_trace.h"

namespace ock {
namespace bio {
constexpr uint32_t UNIT_SEC2MISEC = 1000;

BResult Cm::Initialize(const CmOptions &opt)
{
    if (mInited) {
        return BIO_OK;
    }
    mOptions = opt;
    mInited = true;
    return BIO_OK;
}

BResult Cm::Start()
{
    if (mStarted) {
        return BIO_OK;
    }
    PoolInfo pools;
    strcpy_s(pools.poolName, POOL_NAME_LEN, "bio");
    pools.poolId = mOptions.groups.groupId;
    pools.type = DISK_TYPE_DRAM;
    pools.redundance = (mOptions.groups.replicaNum == 2U) ? PT_REP_DOUBLE : PT_REP_TRIPLE;
    pools.initialNodeNum = mOptions.groups.initialNodeNum;
    pools.maxNodeNum = mOptions.groups.maxNodeNum;
    pools.maxPtNum = mOptions.groups.maxPtNum;

    CmCfgInfo cfgInfo;
    cfgInfo.zkIpMask = const_cast<char *>(mOptions.zkIpMask.c_str());
    cfgInfo.ipStr = const_cast<char *>(mNode.ip.c_str());
    cfgInfo.regTimeOut = mOptions.hbTempTimeout * UNIT_SEC2MISEC;
    cfgInfo.regPermTimeOut = mOptions.hbPermFaultTime * UNIT_SEC2MISEC;
    int ret = CM_Init(CONFIG_ROLE_TOGETHER, &pools, 1, &cfgInfo);
    if (ret != 0) {
        return BIO_ERR;
    }

    DataInfoChangeOpHandle handle;
    handle.notifyDataInfoChange = NotifyDataInfoChange;
    handle.ctx = reinterpret_cast<void *>(this);

    ret = CM_RegDataInfoHandle(pools.poolId, "service_state", &mIsUpgrade, sizeof(mIsUpgrade), &handle);
    if (ret != BIO_OK) {
        return ret;
    }

    mStarted = true;
    return BIO_OK;
}

void Cm::Stop()
{
    CM_Exit();
}

BResult Cm::ReportDiskStatus(uint16_t diskId, CmDiskStatus status)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    DiskState state = (status == CM_DISK_NORMAL) ? DISK_STATE_NORMAL : DISK_STATE_FAULT;
    int ret = CM_SetDiskStatus(mOptions.groups.groupId, diskId, state);
    if (ret != 0) {
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult Cm::AddNewDisk(uint16_t diskId, CmDiskStatus status, bool isNewDisk)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    uint16_t poolId = GetCmLocalNodeId().groupId;
    DiskState state = (status == CM_DISK_NORMAL) ? DISK_STATE_NORMAL : DISK_STATE_FAULT;

    auto ret = CM_UpdateNodeInfo(poolId, state, diskId, isNewDisk);
    if (ret != 0) {
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult Cm::ReportPtFinish(std::vector<CmPtFinish> &ptFinish)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    if (ptFinish.size() == 0) {
        return BIO_OK;
    }
    PtFinish *ptList = new (std::nothrow) PtFinish[ptFinish.size()];
    if (ptList == nullptr) {
        LOG_ERROR("Malloc ptList failed:" << ptFinish.size());
        return BIO_ERR;
    }
    uint16_t num = 0;
    for (uint32_t index = 0; index < ptFinish.size(); index++) {
        if (mPtInfos.find(ptFinish[index].ptId) == mPtInfos.end()) {
            continue;
        }
        uint64_t referNum = mPtInfos[ptFinish[index].ptId].referNum;
        if (ptFinish[index].version < referNum) {
            LOG_ERROR("Pt version incorrect, version " << ptFinish[index].version << " referNum " << referNum);
            delete[] ptList;
            return BIO_ERR;
        }
        ptList[num].birthVersion = ptFinish[index].version - referNum;
        ptList[num].ptId = ptFinish[index].ptId;
        num++;
    }
    int ret = CM_SetPtFinishStatus(mOptions.groups.groupId, num, ptList);
    if (ret != 0) {
        delete[] ptList;
        return BIO_ERR;
    }
    delete[] ptList;
    return BIO_OK;
}

BResult Cm::RegisterNode(CmNodeInfo &node)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    mNode = node;

    LocalNodeQueryOpHandle handle;
    handle.queryLocalNodeInfo = QueryLocalNodeInfo;
    handle.queryLocalNodeMr = NULL;
    handle.ctx = reinterpret_cast<void *>(this);

    int ret = CM_RegLocalNodeQueryOpHandle(mOptions.groups.groupId, &handle);
    if (ret != BIO_OK) {
        return ret;
    }
    return BIO_OK;
}

BResult Cm::RegisterNodeHandler(const CmNodeHandler &nodeHandler)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    mNodeHandler = nodeHandler;

    NodeListChangeOpHandle handle;
    handle.notifyNodeListChange = NotifyNodeListChange;
    handle.ctx = reinterpret_cast<void *>(this);

    int ret = CM_RegNodeListChangeNotifyHandle(mOptions.groups.groupId, &handle);
    if (ret != BIO_OK) {
        return ret;
    }
    return BIO_OK;
}

BResult Cm::RegisterPtHandler(const CmPtHandler &ptHandler)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    mPtHandler = ptHandler;

    PtViewChangeOpHandle handle;
    handle.notifyPtListChange = NotifyPtListChange;
    handle.ctx = reinterpret_cast<void *>(this);

    int ret = CM_RegPtViewChangeOpHandle(mOptions.groups.groupId, &handle);
    if (ret != BIO_OK) {
        return ret;
    }
    return BIO_OK;
}

BResult Cm::ReportServiceState(bool isUngrade)
{
    int ret = CM_WriteDataInfo(mOptions.groups.groupId, "service_state", &isUngrade, sizeof(isUngrade));
    if (ret != 0) {
        return BIO_ERR;
    }
    return BIO_OK;
}

int32_t Cm::QueryLocalNodeInfo(NodeInfo *nodeInfo, void *ctx)
{
    Cm *cm = static_cast<Cm *>(ctx);
    WriteLocker<ReadWriteLock> lock(&cm->mLock);

    nodeInfo->port = cm->mNode.port;
    nodeInfo->status = NODE_STATUS_OK;
    auto ret = strcpy_s(nodeInfo->ipv4AddrStr, IP_ADDR_LEN, cm->mNode.ip.c_str());
    if (ret != 0) {
        return -1;
    }
    nodeInfo->diskList.num = cm->mNode.disks.size();
    nodeInfo->diskList.type = DISK_TYPE_DRAM;
    for (uint16_t index = 0; index < nodeInfo->diskList.num; index++) {
        nodeInfo->diskList.list[index].diskId = cm->mNode.disks[index].diskId;
        if (cm->mNode.disks[index].diskStatus == CM_DISK_NORMAL) {
            nodeInfo->diskList.list[index].state = DISK_STATE_NORMAL;
        } else {
            nodeInfo->diskList.list[index].state = DISK_STATE_FAULT;
        }
    }
    nodeInfo->netList.num = 0;
    return 0;
}

int32_t Cm::NotifyNodeListChange(NodeStateList *nodeList, void *ctx)
{
    Cm *cm = static_cast<Cm *>(ctx);
    WriteLocker<ReadWriteLock> lock(&cm->mLock);

    if (cm->mNodeId.whole == NO_MAX_VALUE32) {
        cm->mNodeId.groupId = cm->mOptions.groups.groupId;
        cm->mNodeId.nodeId = CM_GetLocalNodeId(cm->mOptions.groups.groupId);
    }

    for (uint16_t index = 0; index < nodeList->nodeNum; index++) {
        if (nodeList->nodeList[index].state == NODE_STATE_INVALID) {
            continue;
        }

        NodeInfo info;
        info.nodeId = nodeList->nodeList[index].nodeId;
        int ret = CM_GetNodeInfo(nodeList->poolId, &info);
        if (ret != 0) {
            LOG_ERROR("Impossible, get node failed, nodeId:" << info.nodeId);
            continue;
        }

        CmNodeId id;
        CmNodeInfo node;
        CmDiskInfo disk;

        id.groupId = nodeList->poolId;
        id.nodeId = nodeList->nodeList[index].nodeId;
        node.id = id;
        node.ip = info.ipv4AddrStr;
        node.port = info.port;
        node.status = (nodeList->nodeList[index].state == NODE_STATE_UP) ? CM_NODE_NORMAL : CM_NODE_FAULT;

        for (uint16_t idx = 0; idx < info.diskList.num; idx++) {
            disk.diskId = info.diskList.list[idx].diskId;
            disk.diskStatus = (info.diskList.list[idx].state == DISK_STATE_NORMAL) ? CM_DISK_NORMAL : CM_DISK_FAULT;
            node.disks.push_back(disk);
        }
        cm->mNodeInfos[id] = node;
        LOG_INFO("NodeInfo, index:" << index << ", node " << node.id.ToString());
    }
    BIO_TRACE_START(CM_TRACE_NOTIFY_NODEVIEW);
    cm->mNodeHandler(cm->mNodeInfos);
    BIO_TRACE_END(CM_TRACE_NOTIFY_NODEVIEW, 0);
    return 0;
}

void Cm::FillPtInfo(PtEntryList *ptList, uint16_t index, CmPtInfo &pt, const CmPtState ptState[])
{
    pt.version = ptList->ptEntryList[index].birthVersion + ptList->ptEntryList[index].referNum;
    pt.referNum = ptList->ptEntryList[index].referNum;
    pt.ptId = ptList->ptEntryList[index].ptId;
    pt.state = ptState[ptList->ptEntryList[index].state];
    pt.masterNodeId = ptList->ptEntryList[index].masterNodeId;
    pt.masterDiskId = ptList->ptEntryList[index].masterDiskId;
}

int32_t Cm::FillPtCopyList(PtEntryList *ptList, uint16_t index, CmPtInfo &pt, const CmCopyState ptState[])
{
    for (uint16_t idx = 0; idx < ptList->maxCopyNum; idx++) {
        uint16_t vIdx = idx;
        if (vIdx < PT_MAX_COPY_INDEX && (ptList->ptEntryList[index].copyList[vIdx].state == PT_COPY_STATE_INIT ||
                                         ptList->ptEntryList[index].copyList[vIdx].state == PT_COPY_STATE_OUT)) {
            vIdx = idx + ptList->maxCopyNum;
        }
        if (vIdx < PT_MAX_COPY_INDEX && (ptList->ptEntryList[index].copyList[vIdx].state == PT_COPY_STATE_INIT ||
                                         ptList->ptEntryList[index].copyList[vIdx].state == PT_COPY_STATE_OUT)) {
            LOG_ERROR("Impossible, ptId:" << ptList->ptEntryList[index].ptId);
            return -1;
        }

        if (vIdx >= PT_MAX_COPY_INDEX) {
            LOG_ERROR("Impossible copy num : " << vIdx);
            return -1;
        }
        CmPtCopy copy;
        copy.nodeId = ptList->ptEntryList[index].copyList[vIdx].nodeId;
        copy.diskId = ptList->ptEntryList[index].copyList[vIdx].diskId;
        copy.state = ptState[ptList->ptEntryList[index].copyList[vIdx].state];
        pt.copys.push_back(copy);
    }
    return 0;
}

int32_t Cm::NotifyPtListChange(PtEntryList *ptList, void *ctx)
{
    Cm *cm = static_cast<Cm *>(ctx);
    WriteLocker<ReadWriteLock> lock(&cm->mLock);
    cm->mStatus = CM_NORMAL;

    static CmPtState ptState[PT_STATE_BUTT] = {
        CM_PT_INIT,
        CM_PT_NORMAL,
        CM_PT_DEGRADE_LOSS1,
        CM_PT_DEGRADE_LOSS2,
        CM_PT_FAULT,
        CM_PT_BYPASS,
    };

    static CmCopyState copyState[PT_COPY_STATE_BUTT] = {
        CM_COPY_INIT,
        CM_COPY_RUNNING,
        CM_COPY_DOWN,
        CM_COPY_OUT,
        CM_COPY_RECOVERY,
    };

    for (uint16_t index = 0; index < ptList->ptNum; index++) {
        if (ptList->ptEntryList[index].state == PT_STATE_INIT || ptList->ptEntryList[index].state == PT_STATE_BUTT) {
            continue;
        }

        CmPtInfo pt;
        FillPtInfo(ptList, index, pt, ptState);
        int32_t ret = FillPtCopyList(ptList, index, pt, copyState);
        if (ret != 0) {
            LOG_ERROR("Fill pt copy list failed, ret :" << ret);
            return ret;
        }

        cm->mPtInfos[pt.ptId] = CmPtInfo();
        cm->mPtInfos[pt.ptId].Clone(pt);
    }
    cm->ScanPtListAffinity();
    BIO_TRACE_START(CM_TRACE_NOTIFY_PTVIEW);
    cm->mPtHandler(cm->mPtInfos);
    BIO_TRACE_END(CM_TRACE_NOTIFY_PTVIEW, 0);
    return 0;
}

int32_t Cm::NotifyDataInfoChange(const char *key, void *value, uint32_t valLen, void *ctx)
{
    Cm *cm = static_cast<Cm *>(ctx);
    cm->mIsUpgrade = *(reinterpret_cast<bool *>(value));
    std::string state = cm->mIsUpgrade ? "true" : "false";
    LOG_INFO("Service state, isUpgrade:" << state);
    return 0;
}

void Cm::ScanPtListAffinity()
{
    mLocals.clear();
    for (auto it = mPtInfos.begin(); it != mPtInfos.end(); ++it) {
        for (auto ite = it->second.copys.begin(); ite != it->second.copys.end(); ++ite) {
            if (ite->nodeId == mNodeId.nodeId) {
                mLocals.push_back(it->first);
                break;
            }
        }
    }
    return;
}
}
}

/* ******************************** CM api implementation in C language ********************* */
using namespace ock::bio;
int32_t CmReportDiskStatus(uint16_t diskId, CmDiskStatus status)
{
    return Cm::Instance()->ReportDiskStatus(diskId, status);
}

int32_t CmAddNewDisk(uint16_t diskId, CmDiskStatus status, bool isNewDisk)
{
    return Cm::Instance()->AddNewDisk(diskId, status, isNewDisk);
}