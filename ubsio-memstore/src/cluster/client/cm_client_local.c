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

#include "cm_client_local.h"
#include "cm_zkadapter.h"
#include "cm_config.h"
#include "cm_log.h"
#include "securec.h"

static char g_mrBuff[NODE_META_BUFF_LEN];

static LocalNodeQueryOpHandle g_localOp[MAX_POOL_NUM] = { 0 };

static NodeInfo g_localNodeInfo[MAX_POOL_NUM] = { 0 };

static NodeListChangeOpHandle g_subNodeChange[MAX_POOL_NUM] = { 0 };

static PtViewChangeOpHandle g_subPtChange[MAX_POOL_NUM] = { 0 };

DATA_INIT CmClientInitLocalOp(void)
{
    uint16_t index;

    for (index = 0; index < MAX_POOL_NUM; index++) {
        g_localOp[index].queryLocalNodeInfo = NULL;
        g_localOp[index].queryLocalNodeMr = NULL;
    }
}

int32_t CM_GetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo)
{
    PoolInfo *pool = CmConfigGetPoolInfo(poolId);

    if (poolId >= MAX_POOL_NUM || pool == NULL || nodeInfo == NULL) {
        CM_LOGERROR("Invalid param, poolId(%u).", poolId);
        return CM_ERR;
    }

    int32_t ret = CmClientZkGetNodeInfo(poolId, nodeInfo);
    if (ret != CM_OK) {
        CM_LOGERROR("Get nodeInfo failed, ret(%d) nodeId(%u), poolId(%u).", ret, nodeInfo->nodeId, poolId);
        return ret;
    }

    return CM_OK;
}

int32_t CM_GetNodeMr(uint16_t poolId, NodeMetaBuff *mr)
{
    PoolInfo *pool = CmConfigGetPoolInfo(poolId);

    if (poolId >= MAX_POOL_NUM || pool == NULL || mr == NULL) {
        CM_LOGERROR("Invalid param, poolId(%u).", poolId);
        return CM_ERR;
    }

    if (mr->nodeId >= pool->maxNodeNum) {
        CM_LOGERROR("Invalid nodeId(%u), poolId(%u).", mr->nodeId, poolId);
        return CM_ERR;
    }

    int32_t ret = CmClientZkGetMr(poolId, mr);
    if (ret != CM_OK) {
        CM_LOGERROR("Get mr failed, ret(%d) nodeId(%u) poolId(%u).", ret, mr->nodeId, poolId);
        return ret;
    }
    return CM_OK;
}

uint16_t CM_GetLocalNodeId(uint16_t poolId)
{
    return CmClientLocalGetNodeId(poolId);
}

int32_t CM_RegLocalNodeQueryOpHandle(uint16_t poolId, LocalNodeQueryOpHandle *handle)
{
    if (poolId >= MAX_POOL_NUM || handle == NULL) {
        CM_LOGERROR("Invalid poolId(%u).", poolId);
        return CM_ERR;
    }

    if (g_localOp[poolId].queryLocalNodeInfo == NULL && handle->queryLocalNodeInfo != NULL) {
        CM_LOGINFO("Register query nodeInfo succeed, poolId(%u).", poolId);
        g_localOp[poolId].queryLocalNodeInfo = handle->queryLocalNodeInfo;
    }

    if (g_localOp[poolId].queryLocalNodeMr == NULL && handle->queryLocalNodeMr != NULL) {
        CM_LOGINFO("Register query mr succeed, poolId(%u).", poolId);
        g_localOp[poolId].queryLocalNodeMr = handle->queryLocalNodeMr;
    }

    g_localOp[poolId].ctx = handle->ctx;

    return CM_OK;
}

int32_t CM_RegNodeListChangeNotifyHandle(uint16_t poolId, NodeListChangeOpHandle *handle)
{
    if (poolId >= MAX_POOL_NUM || handle == NULL) {
        CM_LOGERROR("Invalid param, poolId(%u).", poolId);
        return CM_ERR;
    }

    if (g_subNodeChange[poolId].notifyNodeListChange == NULL) {
        CM_LOGINFO("Register nodelist change notify succeed, poolId(%u).", poolId);
        g_subNodeChange[poolId] = *handle;
    }

    return CM_OK;
}

int32_t CM_RegPtViewChangeOpHandle(uint16_t poolId, PtViewChangeOpHandle *handle)
{
    if (poolId >= MAX_POOL_NUM || handle == NULL) {
        CM_LOGERROR("Invalid param, poolId(%u).", poolId);
        return CM_ERR;
    }

    if (g_subPtChange[poolId].notifyPtListChange == NULL) {
        CM_LOGINFO("Register ptview change notify succeed, poolId(%u).", poolId);
        g_subPtChange[poolId] = *handle;
    }

    return CM_OK;
}

int32_t CM_WriteDataInfo(uint16_t poolId, const char *key, void *value, uint32_t valLen)
{
    if (poolId >= MAX_POOL_NUM || key == NULL || value == NULL || valLen == 0) {
        CM_LOGERROR("Invalid poolId(%u).", poolId);
        return CM_ERR;
    }

    int32_t ret = CmClientZkRecordDataInfo(poolId, key, value, valLen);
    if (ret != CM_OK) {
        CM_LOGERROR("Write data failed, ret(%d) poolId(%u).", ret, poolId);
        return ret;
    }

    return CM_OK;
}

int32_t CM_RegDataInfoHandle(uint16_t poolId, const char *key, void *value, uint32_t valLen,
    DataInfoChangeOpHandle *handle)
{
    if (poolId >= MAX_POOL_NUM || key == NULL || handle == NULL) {
        CM_LOGERROR("Invalid poolId(%u).", poolId);
        return CM_ERR;
    }

    int32_t ret = CmClientZkSubDataInfoChange(poolId, key, value, valLen, handle);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub data failed, ret(%d) poolId(%u).", ret, poolId);
        return ret;
    }

    return CM_OK;
}

int32_t CmClientLocalGetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo)
{
    if (g_localOp[poolId].queryLocalNodeInfo == NULL) {
        CM_LOGERROR("NodeInfo, not exist, poolId(%u).", poolId);
        return CM_ERR;
    }

    *nodeInfo = g_localNodeInfo[poolId];
    return CM_OK;
}

uint16_t CmClientLocalGetNodeId(uint16_t poolId)
{
    if (g_localOp[poolId].queryLocalNodeInfo == NULL) {
        CM_LOGERROR("NodeInfo, not exist, poolId(%u).", poolId);
        return NODE_ID_INVALID;
    }

    return g_localNodeInfo[poolId].nodeId;
}

void CmClientLocalUpdateNodeInfo(uint16_t poolId, NodeInfo *nodeInfo)
{
    if (g_localOp[poolId].queryLocalNodeInfo == NULL) {
        CM_LOGERROR("NodeInfo, not exist, poolId(%u).", poolId);
        return;
    }

    g_localNodeInfo[poolId] = *nodeInfo;
    return;
}

int32_t CmClientLocalGetNode(uint16_t poolId, NodeInfo *nodeInfo)
{
    int32_t ret;

    if (g_localOp[poolId].queryLocalNodeInfo != NULL) {
        ret = g_localOp[poolId].queryLocalNodeInfo(nodeInfo, g_localOp[poolId].ctx);
        if (ret != CM_OK) {
            CM_LOGERROR("Query nodeInfo failed, poolId(%u) ret(%d).", poolId, ret);
            return CM_ERR;
        }
    } else {
        CM_LOGERROR("Not register nodeInfo, poolId(%u).", poolId);
        return CM_ERR;
    }

    if (nodeInfo->diskList.num > MAX_DISK_NUM || nodeInfo->netList.num > MAX_NET_NUM) {
        CM_LOGERROR("Invalid nodeInfo, poolId(%u) disk num(%d) net num(%d).", poolId, nodeInfo->diskList.num,
            nodeInfo->netList.num);
        return CM_ERR;
    }

    return CM_OK;
}

static int32_t CmClientLocalCheckDisk(DiskList *diskList, DiskList *diskListBak)
{
    uint16_t i, j;

    if (diskList->num != diskListBak->num) {
        CM_LOGERROR("DiskNum not same, pre(%u) now(%u).", diskListBak->num, diskList->num);
        return CM_ERR;
    }

    for (i = 0; i < diskListBak->num; i++) {
        for (j = 0; j < diskList->num; j++) {
            if (diskListBak->list[i].diskId == diskList->list[j].diskId) {
                break;
            }
        }
        if (j == diskList->num) {
            CM_LOGERROR("DiskId(%u) not found.", diskListBak->list[i].diskId);
            return CM_ERR;
        }
    }
    return CM_OK;
}

static int32_t CmClientLocalCheckNode(uint16_t poolId, PoolInfo *poolInfo, NodeInfo *nodeInfo)
{
    NodeInfo record;
    int32_t ret;

    if (nodeInfo->diskList.type != poolInfo->diskType) {
        CM_LOGERROR("Check disk media failed, node(%u) config(%u).", nodeInfo->diskList.type, poolInfo->diskType);
        return CM_ERR;
    }

    if (nodeInfo->diskList.num != poolInfo->diskNum) {
        CM_LOGERROR("Check disk num failed, node(%u) config(%u).", nodeInfo->diskList.num, poolInfo->diskNum);
        return CM_ERR;
    }

    ret = CmClientZkGetNodeId(poolId, nodeInfo->ipv4AddrStr, nodeInfo->port, &record.nodeId);
    if (ret != CM_OK && ret != CM_NOT_EXIST) {
        CM_LOGERROR("Get nodeId failed, poolId(%u) ipv4AddrStr(%s) ret(%d).", poolId, nodeInfo->ipv4AddrStr, ret);
        return CM_ERR;
    }

    if (ret == CM_NOT_EXIST) {
        CM_LOGINFO("First register, poolId(%u) ipv4AddrStr(%s) port(%u).", poolId, nodeInfo->ipv4AddrStr,
            nodeInfo->port);
        return CM_OK; // no needed.
    }

    ret = CmClientZkGetNodeInfo(poolId, &record);
    if (ret != CM_OK && ret != CM_NOT_EXIST) {
        CM_LOGERROR("Get NodeInfo failed, poolId(%u).", poolId);
        return CM_ERR;
    }

    if (ret == CM_NOT_EXIST) {
        return CM_OK; // no needed.
    }

    NodeStateInfo cmState;
    ret = CmClientZkGetNodeState(poolId, record.nodeId, &cmState);
    if (ret != CM_OK) {
        CM_LOGERROR("Get nodeState failed, poolId(%u) nodeId(%u) ret(%d).", poolId, nodeInfo->nodeId, ret);
        return ret;
    }

    if (cmState.clusterState == NODE_CLUSTER_STATE_INVALID || cmState.clusterState == NODE_CLUSTER_STATE_OUT) {
        return CM_OK; // no needed.
    }

    ret = CmClientLocalCheckDisk(&nodeInfo->diskList, &record.diskList);
    if (ret != CM_OK) {
        CM_LOGERROR("Check diskList failed, ret(%d) poolId(%u).", ret, poolId);
        return CM_ERR;
    }

    return CM_OK;
}

static int32_t CmClientLocalRegisterNode(uint16_t poolId)
{
    NodeInfo *nodeInfo = &g_localNodeInfo[poolId];
    int32_t ret;

    PoolInfo *poolInfo = CmConfigGetPoolInfo(poolId);
    if (poolInfo == NULL) {
        CM_LOGERROR("Get poolInfo failed, poolId(%u).", poolId);
        return CM_ERR;
    }

    ret = CmClientLocalGetNode(poolId, nodeInfo);
    if (ret != CM_OK) {
        CM_LOGERROR("Get nodeInfo failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    ret = CmClientLocalCheckNode(poolId, poolInfo, nodeInfo);
    if (ret != CM_OK) {
        CM_LOGERROR("Prepare nodeInfo failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    CM_LOGINFO("Register, poolId(%u) diskNum(%u).", poolId, nodeInfo->diskList.num);
    ret = CmClientZkRegisterNodeInfo(poolId, nodeInfo);
    if (ret != CM_OK) {
        CM_LOGERROR("Register nodeInfo failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    return CM_OK;
}

static int32_t CmClientLocalGetMr(uint16_t poolId, NodeMetaBuff *mr)
{
    if (g_localOp[poolId].queryLocalNodeMr == NULL) {
        CM_LOGINFO("Not register, poolId(%u).", poolId);
        mr->len = 0;
        return CM_OK;
    }

    int32_t ret = g_localOp[poolId].queryLocalNodeMr(mr, g_localOp[poolId].ctx);
    if (ret != CM_OK) {
        CM_LOGERROR("Query local mr failed, poolId(%u) ret(%d).", poolId, ret);
        return CM_ERR;
    }

    return CM_OK;
}

static int32_t CmClientLocalRegisterMr(uint16_t poolId)
{
    int32_t ret;

    NodeInfo *nodeInfo = &g_localNodeInfo[poolId];

    NodeMetaBuff *mr = (NodeMetaBuff *)g_mrBuff;
    ret = CmClientLocalGetMr(poolId, mr);
    if (ret != CM_OK) {
        CM_LOGERROR("Get local mr failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }
    mr->nodeId = nodeInfo->nodeId;
    ret = CmClientZkRecordMr(poolId, mr);
    if (ret != CM_OK) {
        CM_LOGERROR("Record mr failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    return CM_OK;
}

static int32_t CmClientNodeListChangeFp(NodeStateList *changeList)
{
    uint16_t poolId = changeList->poolId;
    PoolInfo *pool = CmConfigGetPoolInfo(poolId);

    if (poolId >= MAX_POOL_NUM || pool == NULL) {
        CM_LOGWARN("Invalid, poolId(%u).", poolId);
        return CM_OK;
    }

    static const char *nstate[NODE_STATE_BUTT + 1] = {
        "invalid",
        "up",
        "down",
        "butt",
    };

    static const char *cstate[NODE_CLUSTER_STATE_BUTT + 1] = {
        "invalid",
        "out",
        "in",
        "butt",
    };

    uint16_t index;
    for (index = 0; index < changeList->nodeNum; index++) {
        NodeStateInfo *changeInfo = &changeList->nodeList[index];
        CM_LOGINFO("NodeChange: poolId(%u) nodeId(%u) state(%s-%s) session(%lu).", poolId, changeInfo->nodeId,
            nstate[changeInfo->state], cstate[changeInfo->clusterState], changeInfo->sessionId);
    }

    if (g_subNodeChange[poolId].notifyNodeListChange != NULL) {
        g_subNodeChange[poolId].notifyNodeListChange(changeList, g_subNodeChange[poolId].ctx);
    }

    return CM_OK;
}

static int32_t CmClientPtListChangeFp(PtEntryList *changeList)
{
    static const char *ptstate[PT_STATE_BUTT + 1] = {
        "init",
        "norm",
        "loss1",
        "loss2",
        "fault",
        "butt",
    };

    uint16_t poolId = changeList->poolId;
    PoolInfo *pool = CmConfigGetPoolInfo(poolId);

    if (poolId >= MAX_POOL_NUM || pool == NULL) {
        CM_LOGWARN("Invalid, poolId(%u).", poolId);
        return CM_OK;
    }

    uint16_t index;
    for (index = 0; index < changeList->ptNum; index++) {
        PtEntry *ptEntry = &changeList->ptEntryList[index];
        CM_LOGDEBUG("ptChange: poolId(%u) ptId(%u) state(%s) version(%lu).", poolId, ptEntry->ptId,
            ptstate[ptEntry->state], ptEntry->birthVersion);
    }

    if (g_subPtChange[poolId].notifyPtListChange != NULL) {
        g_subPtChange[poolId].notifyPtListChange(changeList, g_subPtChange[poolId].ctx);
    }

    return CM_OK;
}

static int32_t CmClientLocalRegister(uint16_t poolId, PoolInfo *pool)
{
    NodeInfo *nodeInfo = &g_localNodeInfo[poolId];
    int32_t ret;

    ret = CmClientLocalRegisterNode(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Register nodeInfo failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    ret = CmClientLocalRegisterMr(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Register mr failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    ret = CmClientZkRecordLocalSession(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Record session failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    ret = CmClientZkSubStateListChange(poolId, CmClientNodeListChangeFp);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub nodeList change failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    if (pool->redundanceNum != 0) {
        ret = CmClientZkSubPtListChange(poolId, CmClientPtListChangeFp);
        if (ret != CM_OK) {
            CM_LOGERROR("Sub ptList change failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
    }

    CM_LOGINFO("Register succeed, poolId(%u) nodeId(%u) ip(%s).", poolId, nodeInfo->nodeId, nodeInfo->ipv4AddrStr);
    return CM_OK;
}

int32_t CmClientLocalInit(void)
{
    uint16_t poolId;
    int32_t ret;

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        PoolInfo *pool = CmConfigGetPoolInfo(poolId);
        if (pool == NULL) {
            continue;
        }
        ret = CmClientLocalRegister(poolId, pool);
        if (ret != CM_OK) {
            CM_LOGERROR("Register local failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
    }

    CM_LOGINFO("Cm client local init succeed.");
    return CM_OK;
}

void CmClientLocalExit(void)
{
    return;
}

