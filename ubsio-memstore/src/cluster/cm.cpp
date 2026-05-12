/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
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

namespace ock {
namespace mms {
constexpr uint32_t UNIT_SEC2MISEC = 1000;

BResult Cm::Initialize(const CmOptions &opt)
{
    if (mInited) {
        return MMS_OK;
    }
    mOptions = opt;
    mInited = true;
    return MMS_OK;
}

BResult Cm::Start()
{
    if (mStarted) {
        return MMS_OK;
    }
    PoolInfo pools;
    strcpy_s(pools.poolName, POOL_NAME_LEN, "mms");
    pools.poolId = mOptions.groups.groupId;
    pools.diskType = DISK_TYPE_DRAM;
    pools.diskNum = static_cast<uint16_t>(mNode.numas.size());
    pools.redundanceNum = mOptions.groups.replicaNum;
    pools.initialNodeNum = mOptions.groups.initialNodeNum;
    pools.maxNodeNum = mOptions.groups.maxNodeNum;
    pools.maxPtNum = mOptions.groups.maxPtNum;

    CmCfgInfo cfgInfo;
    cfgInfo.zkIpMask = const_cast<char *>(mOptions.zkIpMask.c_str());
    cfgInfo.ipStr = const_cast<char *>(mNode.ip.c_str());
    cfgInfo.nodeId = mOptions.nodeId;
    cfgInfo.regTimeOut = mOptions.hbTempTimeout * UNIT_SEC2MISEC;
    cfgInfo.regPermTimeOut = mOptions.hbPermFaultTime * UNIT_SEC2MISEC;
    int ret = CM_Init(CONFIG_ROLE_TOGETHER, &pools, 1, &cfgInfo);
    if (ret != 0) {
        return MMS_ERR;
    }

    mStarted = true;
    return MMS_OK;
}

void Cm::Stop()
{
    CM_Exit();
}

BResult Cm::ReportPtFinish(uint16_t ptId, uint64_t birthVersion)
{
    WriteLocker<ReadWriteLock> lock(&mPLock);
    PtFinish ptFinish;
    ptFinish.birthVersion = birthVersion;
    ptFinish.ptId = ptId;
    int ret = CM_SetPtFinishStatus(mOptions.groups.groupId, NO_1, &ptFinish);
    if (ret != CM_OK) {
        return MMS_ERR;
    }
    return MMS_OK;
}

BResult Cm::RegisterNode(CmNodeInfo &node)
{
    WriteLocker<ReadWriteLock> lock(&mNLock);
    mNode = node;

    LocalNodeQueryOpHandle handle;
    handle.queryLocalNodeInfo = QueryLocalNodeInfo;
    handle.queryLocalNodeMr = NULL;
    handle.ctx = reinterpret_cast<void *>(this);

    int ret = CM_RegLocalNodeQueryOpHandle(mOptions.groups.groupId, &handle);
    if (ret != MMS_OK) {
        return ret;
    }
    return MMS_OK;
}

BResult Cm::RegisterNodeHandler(const CmNodeHandler &nodeHandler)
{
    WriteLocker<ReadWriteLock> lock(&mNLock);
    mNodeHandler = nodeHandler;

    NodeListChangeOpHandle handle;
    handle.notifyNodeListChange = NotifyNodeListChange;
    handle.ctx = reinterpret_cast<void *>(this);

    int ret = CM_RegNodeListChangeNotifyHandle(mOptions.groups.groupId, &handle);
    if (ret != MMS_OK) {
        return ret;
    }
    return MMS_OK;
}

BResult Cm::RegisterPtHandler(const CmPtHandler &ptHandler)
{
    WriteLocker<ReadWriteLock> lock(&mPLock);
    mPtHandler = ptHandler;

    PtViewChangeOpHandle handle;
    handle.notifyPtListChange = NotifyPtListChange;
    handle.ctx = reinterpret_cast<void *>(this);

    int ret = CM_RegPtViewChangeOpHandle(mOptions.groups.groupId, &handle);
    if (ret != MMS_OK) {
        return ret;
    }
    return MMS_OK;
}

BResult Cm::RegisterPtMigrateHandler(const CmPtMigrateHandler &ptMigrateHandler)
{
    WriteLocker<ReadWriteLock> lock(&mPLock);
    mPtMigrateHandler = ptMigrateHandler;

    return MMS_OK;
}

int32_t Cm::QueryLocalNodeInfo(NodeInfo *nodeInfo, void *ctx)
{
    Cm *cm = static_cast<Cm *>(ctx);
    WriteLocker<ReadWriteLock> lock(&cm->mNLock);

    nodeInfo->port = cm->mNode.port;
    nodeInfo->multiPort = cm->mNode.multiPort;
    nodeInfo->status = NODE_STATUS_OK;
    auto ret = strcpy_s(nodeInfo->ipv4AddrStr, IP_ADDR_LEN, cm->mNode.ip.c_str());
    if (ret != 0) {
        return MMS_ERR;
    }
    nodeInfo->diskList.num = cm->mNode.numas.size();
    nodeInfo->diskList.type = DISK_TYPE_DRAM;
    for (uint16_t index = 0; index < nodeInfo->diskList.num; index++) {
        nodeInfo->diskList.list[index].diskId = cm->mNode.numas[index];
        nodeInfo->diskList.list[index].state = DISK_STATE_NORMAL;
    }
    return MMS_OK;
}

int32_t Cm::NotifyNodeListChange(NodeStateList *nodeList, void *ctx)
{
    Cm *cm = static_cast<Cm *>(ctx);
    WriteLocker<ReadWriteLock> lock(&cm->mNLock);

    if (cm->mNodeId == NODE_ID_INVALID) {
        cm->mNodeId = CM_GetLocalNodeId(cm->mOptions.groups.groupId);
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

        CmNodeInfo node;
        node.id = info.nodeId;
        node.ip = info.ipv4AddrStr;
        node.port = info.port;
        node.multiPort = info.multiPort;
        node.status = (nodeList->nodeList[index].state == NODE_STATE_UP) ? CM_NODE_NORMAL : CM_NODE_FAULT;

        for (uint16_t idx = 0; idx < info.diskList.num; idx++) {
            node.numas.push_back(info.diskList.list[idx].diskId);
        }

        cm->mNodeInfos[node.id] = node;
        LOG_INFO("NodeList Change, index:" << index << ", nodeId:" << node.id);
    }
    cm->mMasterNode = nodeList->masterNodeId;
    cm->mNodeHandler(cm->mNodeInfos);
    return 0;
}

int32_t Cm::NotifyPtListChange(PtEntryList *ptList, void *ctx)
{
    Cm *cm = static_cast<Cm *>(ctx);
    WriteLocker<ReadWriteLock> lock(&cm->mPLock);

    static CmCopyState s_copystate[PT_COPY_STATE_BUTT] = {
        CM_COPY_INIT,
        CM_COPY_RUNNING,
        CM_COPY_DOWN,
        CM_COPY_OUT,
        CM_COPY_RECOVERY,
    };

    CmServiceStatus status = CM_NORMAL;
    for (uint16_t index = 0; index < ptList->ptNum; index++) {
        if (ptList->ptEntryList[index].state == PT_STATE_INIT || ptList->ptEntryList[index].state == PT_STATE_BUTT) {
            continue;
        }

        CmPtInfo ptInfo;
        CmPtCopy copy;

        ptInfo.version = ptList->ptEntryList[index].birthVersion;
        ptInfo.ptId = ptList->ptEntryList[index].ptId;
        ptInfo.state = CM_PT_NORMAL;
        ptInfo.masterNodeId = ptList->ptEntryList[index].masterNodeId;

        for (uint16_t idx = 0; idx < ptList->maxCopyNum; idx++) {
            copy.nodeId = ptList->ptEntryList[index].copyList[idx].nodeId;
            copy.state = s_copystate[ptList->ptEntryList[index].copyList[idx].state];
            ptInfo.copys.push_back(copy);
        }
        LOG_INFO("Batch pt num: " << ptList->ptNum << ", Pt id: " << ptInfo.ptId << ", master node:"
        << ptInfo.masterNodeId << ", cm nodeId:" << cm->mNodeId);
        if (ptInfo.masterNodeId == cm->mNodeId) {
            if (cm->mPtInfos.find(ptInfo.ptId) != cm->mPtInfos.end() &&
                cm->mPtInfos[ptInfo.ptId].masterNodeId != ptInfo.masterNodeId) {
                ptInfo.state = CM_PT_MST_MIGRATE;  // 写前日志回放等待完成
                cm->mPtMigrateHandler(ptInfo.ptId);
            }
        }

        if (ptInfo.ptId == cm->mNodeId) {
            cm->mPtInfo = ptInfo;
        }

        cm->mPtInfos[ptInfo.ptId] = ptInfo;
    }

    for (auto& pt :cm->mPtInfos) {
        for (uint16_t idx = 0; idx < ptList->maxCopyNum; idx++) {
            if (pt.second.copys[idx].nodeId == cm->mNodeId && pt.second.copys[idx].state != CM_COPY_RUNNING) {
                status = CM_ABNORMAL;
            }
        }
    }
    cm->mStatus = status;
    cm->mPtHandler(cm->mPtInfos, (status == CM_NORMAL));
    return 0;
}
}
}

