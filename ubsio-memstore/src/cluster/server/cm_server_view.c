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

#include "cm_server_view.h"
#include "cm_server_schedule.h"
#include "cm_server_monitor.h"
#include "cm_zkadapter.h"
#include "cm_pt_calc.h"
#include "cm_pt_store.h"
#include "cm_config.h"
#include "cm_comm.h"
#include "cm_log.h"
#include "cm_thread.h"
#include "securec.h"

#define CM_SERVER_PT_INITIAL "cm_server_pt_initial"

#define CM_SERVER_PT_INITIAL_DELAY_TIME (60)
#define CM_SERVER_PT_TRY_INTERAL (5000)

typedef struct {
    cm_rwlock_t lock;
    int32_t used;
    int32_t nodeChange;
    int32_t stateChange;
    int32_t ptChange;
    int32_t isInitialOk;
    int32_t isInitialDoing;
    PoolInfo *pool;
    NodeInfoList *nodeList;
    NodeStateList *stateList;
    PtEntryList *ptEntryList;
    Calculator *calculator;
    CalcOps *calcOps;
    Storer *storer;
    StoreOps *storeOps;
} CmServerPool;

typedef struct {
    cm_rwlock_t lock;
    CmServerRole role;
    CmServerPool list[MAX_POOL_NUM];
} CmServerPoolMgr;

static CmServerPoolMgr g_sPoolMgr;

static void CmServerViewResetPool(uint16_t poolId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;

    spool->stateChange = FALSE;
    spool->ptChange = FALSE;
    spool->isInitialOk = FALSE;
    spool->isInitialDoing = FALSE;

    uint16_t nodeId;

    spool->nodeList->poolId = pool->poolId;
    spool->nodeList->nodeNum = pool->maxNodeNum;

    int32_t nlen = (int32_t)(sizeof(NodeInfo) * pool->maxNodeNum);
    memset_s(spool->nodeList->nodeList, nlen, 0, nlen);

    spool->stateList->poolId = pool->poolId;
    spool->stateList->nodeNum = pool->maxNodeNum;
    for (nodeId = 0; nodeId < pool->maxNodeNum; nodeId++) {
        spool->stateList->nodeList[nodeId].state = NODE_STATE_INVALID;
        spool->stateList->nodeList[nodeId].clusterState = NODE_CLUSTER_STATE_INVALID;
        spool->stateList->nodeList[nodeId].diskNum = 0;
        size_t dlen = sizeof(NodeDiskState) * MAX_DISK_NUM;
        memset_s(spool->stateList->nodeList[nodeId].diskList, dlen, 0, dlen);
    }
}

static const char *g_nstate[NODE_STATE_BUTT + 1] = {
    "invalid",
    "up",
    "down",
    "butt",
};

static const char *g_cstate[NODE_CLUSTER_STATE_BUTT + 1] = {
    "invalid",
    "out",
    "in",
    "butt",
};

static int32_t CmServerViewUpdatePtEntryList(uint16_t poolId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;
    int32_t ret = CM_OK;

    if (pool->redundanceNum != 0) {
        int32_t isNeed =
            spool->calcOps->needRebalance(spool->calculator, spool->nodeList, spool->stateList, spool->ptEntryList);
        if (isNeed == TRUE) {
            ret =
                spool->calcOps->viewRebalance(spool->calculator, spool->nodeList, spool->stateList, spool->ptEntryList);
            if (ret != CM_OK) {
                CM_LOGERROR("Rebalance failed, poolId(%u) ret(%d).", pool->poolId, ret);
                return ret;
            }
            spool->ptChange = TRUE;
        }
    }

    if (spool->ptChange == TRUE) {
        ret = spool->storeOps->update(spool->storer, spool->ptEntryList);
        if (ret != CM_OK) {
            CM_LOGERROR("Record ptEntryList failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
        spool->ptChange = FALSE;
    }

    return ret;
}

static void CmServerViewExpiredNodeSet(uint16_t poolId, uint16_t nodeId)
{
    NodeStateInfo *cmState = &g_sPoolMgr.list[poolId].stateList->nodeList[nodeId];

    if (cmState->clusterState == NODE_CLUSTER_STATE_OUT) {
        return;
    }

    cmState->clusterState = NODE_CLUSTER_STATE_OUT;
    g_sPoolMgr.list[poolId].stateChange = TRUE;

    CM_LOGINFO("Expired, poolId(%u) nodeId(%u) state(%s-%s).", poolId, nodeId, g_nstate[cmState->state],
        g_cstate[cmState->clusterState]);
    return;
}

static void CmServerViewExpiredDiskSet(uint16_t poolId, uint16_t nodeId, uint16_t diskId)
{
    NodeStateInfo *cmState = &g_sPoolMgr.list[poolId].stateList->nodeList[nodeId];
    uint16_t index;

    for (index = 0; index < cmState->diskNum; index++) {
        if (cmState->diskList[index].diskId == diskId) {
            if (cmState->diskList[index].clusterState == DISK_CLUSTER_STATE_OUT) {
                return;
            } else {
                cmState->diskList[index].clusterState = DISK_CLUSTER_STATE_OUT;
                g_sPoolMgr.list[poolId].stateChange = TRUE;

                CM_LOGINFO("Expired, poolId(%u) nodeId(%u) state(%s-%s) diskId(%u) disk state(%s).", poolId, nodeId,
                    g_nstate[cmState->state], g_cstate[cmState->clusterState], diskId, CM_DISK_STATE(DISK_STATE_FAULT));
                return;
            }
        }
    }

    CM_LOGERROR("Impossible.");
    return;
}

static int32_t CmServerViewExpiredCommit(uint16_t poolId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;
    int32_t ret;

    if (spool->stateChange == FALSE) {
        return CM_OK;
    }

    ret = CmServerZkRecordStateList(spool->stateList);
    if (ret != CM_OK) {
        CM_LOGERROR("Record stateList failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    spool->stateChange = FALSE;

    ret = CmServerViewUpdatePtEntryList(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Update ptEntryList failed, poolId(%u) ret(%d).", pool->poolId, ret);
        return ret;
    }

    return CM_OK;
}

static void CmServerViewRegisterExpiredHandle(void)
{
    CmServerMonitorExpiredHandle handle;

    handle.ExpiredNodeSet = CmServerViewExpiredNodeSet;
    handle.ExpiredDiskSet = CmServerViewExpiredDiskSet;
    handle.ExpiredCommit = CmServerViewExpiredCommit;

    CmServerMonitorRegisterHandle(handle);
    return;
}

static void CmServerViewCheckDiskFault(uint16_t poolId, NodeInfo *nodeInfo, NodeStateInfo *nodeState)
{
    DiskList *diskList = &nodeInfo->diskList;

    if (diskList->num == 0) {
        return; // 不需要监控磁盘故障
    }

    uint16_t index;

    for (index = 0; index < diskList->num; index++) {
        if (diskList->list[index].state == DISK_STATE_FAULT &&
            nodeState->diskList[index].clusterState == DISK_CLUSTER_STATE_IN) {
            CmServerListenDiskFault(poolId, nodeInfo->nodeId, diskList->list[index].diskId);
        } else {
            CmServerCancelDiskFault(poolId, nodeInfo->nodeId, diskList->list[index].diskId);
        }
    }

    return;
}

static void CmServerViewUpdateMasterNodeId(uint16_t poolId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    NodeInfoList *nodeList = spool->nodeList;
    NodeStateList *stateList = spool->stateList;

    uint16_t mstId = stateList->masterNodeId;
    if (mstId != NODE_ID_INVALID && stateList->nodeList[mstId].state == NODE_STATE_UP &&
        stateList->nodeList[mstId].clusterState == NODE_CLUSTER_STATE_IN &&
        nodeList->nodeList[mstId].status == NODE_STATUS_OK) {
        return;
    }

    uint16_t nodeId;
    for (nodeId = 0; nodeId < stateList->nodeNum; nodeId++) {
        if (stateList->nodeList[nodeId].state != NODE_STATE_UP ||
            stateList->nodeList[nodeId].clusterState != NODE_CLUSTER_STATE_IN ||
            nodeList->nodeList[nodeId].status != NODE_STATUS_OK) {
            continue;
        }
        stateList->masterNodeId = nodeId;
        spool->stateChange = TRUE;
        return;
    }
    return;
}

static int32_t CmServerViewBuildNode(uint16_t poolId, uint16_t nodeId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;
    NodeInfo *nodeInfo = &spool->nodeList->nodeList[nodeId];
    NodeStateInfo *nodeState = &spool->stateList->nodeList[nodeId];
    int32_t ret;

    if (nodeState->state == NODE_STATE_INVALID) {
        return CM_OK;
    }

    NodeInfo cache;
    cache.nodeId = nodeId;
    ret = CmServerZkGetNodeInfo(poolId, &cache);
    if (ret != CM_OK) {
        CM_LOGERROR("Get nodeInfo failed, poolId(%u) nodeId(%u) ret(%d).", poolId, nodeId, ret);
        return ret;
    }

    if (memcmp(nodeInfo, &cache, sizeof(NodeInfo)) != 0) {
        memcpy_s(nodeInfo, sizeof(NodeInfo), &cache, sizeof(NodeInfo));
        spool->nodeChange = TRUE;
    }

    CmServerViewUpdateMasterNodeId(poolId);

    if (nodeState->state == NODE_STATE_DOWN && nodeState->clusterState == NODE_CLUSTER_STATE_IN) {
        CmServerListenNodeFault(poolId, nodeInfo->nodeId); // 恢复节点故障监控
        if (pool->redundanceNum != 0) {
            spool->calcOps->updateState(nodeId, NODE_STATE_DOWN, nodeInfo, spool->ptEntryList,
                &spool->ptChange); // 更新PT副本状态
        }
        return CM_OK;
    }

    if (nodeState->state == NODE_STATE_UP && nodeState->clusterState == NODE_CLUSTER_STATE_IN) {
        CmServerViewCheckDiskFault(poolId, nodeInfo, nodeState); // 扫描节点磁盘故障监控
        if (pool->redundanceNum != 0) {
            spool->calcOps->updateState(nodeId, NODE_STATE_UP, nodeInfo, spool->ptEntryList,
                &spool->ptChange); // 更新PT副本状态
        }
        return CM_OK;
    }

    return CM_OK;
}

int32_t CmServerViewBuildNodeList(uint16_t poolId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;
    int32_t ret;

    CmServerViewResetPool(poolId);

    ret = CmServerZkGetNodeList(spool->nodeList);
    if (ret != CM_OK) {
        CM_LOGERROR("Get stateList failed, ret(%d) poolId(%u).", ret, pool->poolId);
        return ret;
    }

    ret = CmServerZkGetStateList(spool->stateList);
    if (ret != CM_OK) {
        CM_LOGERROR("Get stateList failed, ret(%d) poolId(%u).", ret, pool->poolId);
        return ret;
    }

    uint16_t nodeId;
    for (nodeId = 0; nodeId < spool->stateList->nodeNum; nodeId++) {
        ret = CmServerViewBuildNode(pool->poolId, nodeId);
        if (ret != CM_OK) {
            CM_LOGERROR("Build node failed, poolId(%u) nodeId(%u) ret(%d).", pool->poolId, nodeId, ret);
            return ret;
        }
    }

    if (spool->nodeChange == TRUE) {
        ret = CmServerZkRecordNodeList(spool->nodeList);
        if (ret != CM_OK) {
            CM_LOGERROR("Update nodeList failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
        spool->nodeChange = FALSE;
    }

    if (spool->stateChange == TRUE) {
        ret = CmServerZkRecordStateList(spool->stateList);
        if (ret != CM_OK) {
            CM_LOGERROR("Record stateList failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
        spool->stateChange = FALSE;
    }

    ret = CmServerViewUpdatePtEntryList(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Update ptEntryList failed, poolId(%u) ret(%d).", pool->poolId, ret);
        return ret;
    }

    return CM_OK;
}

uint16_t CmServerViewFindNodeIndex(uint16_t *nodeList, uint16_t nodeNum, uint16_t nodeId)
{
    uint16_t index;

    for (index = 0; index < nodeNum; index++) {
        if (nodeList[index] == nodeId) {
            return index;
        }
    }
    return nodeNum;
}

static int32_t CmServerViewNodeOffline(uint16_t poolId, uint16_t nodeId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;
    NodeInfo *nodeInfo = &spool->nodeList->nodeList[nodeId];
    NodeStateInfo *nodeState = &spool->stateList->nodeList[nodeId];

    if (nodeState->state == NODE_STATE_INVALID || nodeState->state == NODE_STATE_DOWN) {
        return CM_OK;
    }

    spool->stateChange = TRUE;

    nodeState->state = NODE_STATE_DOWN;
    CmServerViewUpdateMasterNodeId(poolId);

    CM_LOGINFO("Node offline, poolId(%u) nodeId(%u) state(%s-%s).", poolId, nodeId, g_nstate[nodeState->state],
        g_cstate[nodeState->clusterState]);

    if (nodeState->clusterState == NODE_CLUSTER_STATE_IN) {
        CmServerListenNodeFault(poolId, nodeId); // 加入节点故障监控
        if (pool->redundanceNum != 0) {
            spool->calcOps->updateState(nodeId, NODE_STATE_DOWN, nodeInfo, spool->ptEntryList,
                &spool->ptChange); // 更新PT副本状态
        }
    }

    return CM_OK;
}

static void CmServerViewOnlineUpdateState(NodeInfo *nodeInfo, NodeStateInfo *nodeState)
{
    uint16_t index;

    nodeState->state = NODE_STATE_UP;
    if (nodeState->clusterState == NODE_CLUSTER_STATE_INVALID || nodeState->clusterState == NODE_CLUSTER_STATE_OUT) {
        for (index = 0; index < nodeInfo->diskList.num; index++) {
            nodeState->diskList[index].diskId = nodeInfo->diskList.list[index].diskId;
            nodeState->diskList[index].clusterState = DISK_CLUSTER_STATE_IN;
        }
        nodeState->diskNum = nodeInfo->diskList.num;
        nodeState->clusterState = NODE_CLUSTER_STATE_IN;
    }

    for (index = 0; index < nodeState->diskNum; index++) {
        if (nodeInfo->diskList.list[index].state == DISK_STATE_NORMAL) {
            nodeState->diskList[index].clusterState = DISK_CLUSTER_STATE_IN;
        } else {
            nodeState->diskList[index].clusterState = DISK_CLUSTER_STATE_OUT;
        }
    }

    return;
}

int32_t CmServerViewNodeOnline(uint16_t poolId, uint16_t nodeId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;
    NodeInfo *nodeInfo = &spool->nodeList->nodeList[nodeId];
    NodeStateInfo *nodeState = &spool->stateList->nodeList[nodeId];
    int32_t ret;

    if (nodeState->state == NODE_STATE_UP) {
        return CM_OK;
    }

    spool->stateChange = TRUE;

    CmServerCancelNodeFault(poolId, nodeId);

    NodeInfo cache;
    cache.nodeId = nodeId;
    ret = CmServerZkGetNodeInfo(poolId, &cache);
    if (ret != CM_OK) {
        CM_LOGERROR("Get nodeInfo failed, poolId(%u) nodeId(%u) ret(%d).", poolId, nodeId, ret);
        return ret;
    }

    if (memcmp(nodeInfo, &cache, sizeof(NodeInfo)) != 0) {
        memcpy_s(nodeInfo, sizeof(NodeInfo), &cache, sizeof(NodeInfo));
        spool->nodeChange = TRUE;
    }

    CmServerViewOnlineUpdateState(nodeInfo, nodeState);
    CmServerViewUpdateMasterNodeId(poolId);

    CM_LOGINFO("Node online, poolId(%u) nodeId(%u) state(%s-%s).", poolId, nodeId, g_nstate[nodeState->state],
        g_cstate[nodeState->clusterState]);

    if (nodeState->clusterState == NODE_CLUSTER_STATE_IN) {
        CmServerViewCheckDiskFault(poolId, nodeInfo, nodeState); // 扫描节点磁盘故障监控
        if (pool->redundanceNum != 0) {
            spool->calcOps->updateState(nodeId, NODE_STATE_UP, nodeInfo, spool->ptEntryList,
                &spool->ptChange); // 更新PT副本状态
        }
        return CM_OK;
    }

    return CM_OK;
}

int32_t CmServerViewNodeListChange(CmNodeIdList *watchList)
{
    CmServerPool *spool = &g_sPoolMgr.list[watchList->poolId];
    PoolInfo *pool = spool->pool;
    NodeInfoList *nodeList = spool->nodeList;

    if (g_sPoolMgr.role != CM_SERVER_MASTER) {
        CM_LOGWARN("Local role to slave, req abort.");
        return CM_OK;
    }

    uint16_t nodeId, nodeIndex;
    int32_t ret;

    cm_rwlock_wrlock(&spool->lock);
    for (nodeId = 0; nodeId < nodeList->nodeNum; nodeId++) {
        nodeIndex = CmServerViewFindNodeIndex(watchList->nodeList, watchList->nodeNum, nodeId);
        if (nodeIndex == watchList->nodeNum) {
            ret = CmServerViewNodeOffline(watchList->poolId, nodeId);
        } else {
            ret = CmServerViewNodeOnline(watchList->poolId, nodeId);
        }
        if (ret != CM_OK) {
            cm_rwlock_unlock(&spool->lock);
            CM_LOGERROR("Node deal failed, poolId(%u) nodeId(%u) ret(%d).", watchList->poolId, nodeId, ret);
            return ret;
        }
    }
    cm_rwlock_unlock(&spool->lock);

    if (spool->nodeChange == TRUE) {
        ret = CmServerZkRecordNodeList(spool->nodeList);
        if (ret != CM_OK) {
            CM_LOGERROR("Update nodeList failed, poolId(%u) ret(%d).", watchList->poolId, ret);
            return ret;
        }
        spool->nodeChange = FALSE;
    }

    if (spool->stateChange == TRUE) {
        ret = CmServerZkRecordStateList(spool->stateList);
        if (ret != CM_OK) {
            CM_LOGERROR("Record stateList failed, poolId(%u) ret(%d).", watchList->poolId, ret);
            return ret;
        }
        spool->stateChange = FALSE;
    }

    ret = CmServerViewUpdatePtEntryList(watchList->poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Update ptEntryList failed, poolId(%u) ret(%d).", pool->poolId, ret);
        return ret;
    }

    return CM_OK;
}

int32_t CmServerViewNodeEvent(CmNodeEvent *nodeEvent)
{
    if (g_sPoolMgr.role != CM_SERVER_MASTER) {
        CM_LOGWARN("Local role to slave, req abort.");
        return CM_OK;
    }

    int32_t ret = CmServerViewBuildNode(nodeEvent->poolId, nodeEvent->nodeId);
    if (ret != CM_OK) {
        CM_LOGERROR("Build node failed, poolId(%u) nodeId(%u) ret(%d).", nodeEvent->poolId, nodeEvent->nodeId, ret);
        return ret;
    }

    CmServerPool *spool = &g_sPoolMgr.list[nodeEvent->poolId];
    if (spool->nodeChange == TRUE) {
        ret = CmServerZkRecordNodeList(spool->nodeList);
        if (ret != CM_OK) {
            CM_LOGERROR("Update nodeList failed, poolId(%u) ret(%d).", nodeEvent->poolId, ret);
            return ret;
        }
        spool->nodeChange = FALSE;
    }

    if (spool->stateChange == TRUE) {
        ret = CmServerZkRecordStateList(spool->stateList);
        if (ret != CM_OK) {
            CM_LOGERROR("Record stateList failed, poolId(%u) ret(%d).", nodeEvent->poolId, ret);
            return ret;
        }
        spool->stateChange = FALSE;
    }

    ret = CmServerViewUpdatePtEntryList(nodeEvent->poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Update ptEntryList failed, poolId(%u) ret(%d).", nodeEvent->poolId, ret);
        return ret;
    }

    return CM_OK;
}

void CmServerViewPtEvent(CmPtEvent *ptEvent)
{
    CmServerPool *spool = &g_sPoolMgr.list[ptEvent->poolId];
    PoolInfo *pool = spool->pool;

    if (g_sPoolMgr.role != CM_SERVER_MASTER) {
        CM_LOGWARN("Local role to slave, req abort.");
        return;
    }

    if (pool->redundanceNum == 0) {
        CM_LOGERROR("Impossible, poolId(%u).", ptEvent->poolId);
        return;
    }

    spool->calcOps->updateFinish(ptEvent->nodeId, ptEvent->ptList, ptEvent->ptNum, spool->ptEntryList, &spool->ptChange,
        pool->maxNodeNum);
    return;
}

int32_t CmServerViewPtEventCommit(uint16_t poolId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];

    if (spool->ptChange == TRUE) {
        int32_t ret = spool->storeOps->update(spool->storer, spool->ptEntryList);
        if (ret != CM_OK) {
            CM_LOGERROR("Record ptEntryList failed, ret(%d) poolId(%u).", ret, poolId);
            return ret;
        }
        spool->ptChange = FALSE;
    }
    return CM_OK;
}

static void *CmServerViewInitialThread(void *ctx)
{
    CmServerPool *spool = (CmServerPool *)ctx;
    PoolInfo *pool = spool->pool;

    int32_t ret;

    ret = spool->calcOps->viewInitial(spool->calculator, spool->nodeList, spool->stateList, spool->ptEntryList);
    if (ret != CM_OK) {
        CM_LOGWARN("Initial ptEntryList failed, poolId(%u) ret(%d).", pool->poolId, ret);
        spool->isInitialDoing = FALSE;
        return NULL;
    }

    ret = spool->storeOps->initial(spool->storer, spool->ptEntryList);
    if (ret != CM_OK) {
        CM_LOGWARN("Record ptEntryList failed, poolId(%u) ret(%d).", pool->poolId, ret);
        spool->isInitialDoing = FALSE;
        return NULL;
    }

    spool->isInitialOk = TRUE;
    spool->isInitialDoing = FALSE;

    return NULL;
}

static void *CmServerViewInitialDetectThread(void *ctx)
{
    CmServerPool *spool = (CmServerPool *)ctx;
    PoolInfo *pool = spool->pool;

    uint64_t times = CmGetSecondsTime();

    uint16_t copyNum = pool->redundanceNum;

    do {
        uint16_t nodeId;
        uint16_t okNum = 0;

        if (g_sPoolMgr.role != CM_SERVER_MASTER) {
            CM_LOGINFO("Stop detect, poolId(%u).", pool->poolId);
            break;
        }

        if (spool->isInitialDoing == TRUE) {
            CmSleep(CM_SERVER_PT_TRY_INTERAL);
            continue;
        }

        if (spool->isInitialOk == TRUE) {
            break;
        }

        cm_rwlock_rdlock(&spool->lock);
        for (nodeId = 0; nodeId < spool->stateList->nodeNum; nodeId++) {
            NodeStateInfo *stateInfo = &spool->stateList->nodeList[nodeId];
            if (stateInfo->state == NODE_STATE_UP && stateInfo->clusterState == NODE_CLUSTER_STATE_IN) {
                okNum++;
            }
        }
        cm_rwlock_unlock(&spool->lock);

        uint64_t curTimes = CmGetSecondsTime();

        CM_LOGINFO("Current poolId(%u) normNum(%u).", pool->poolId, okNum);

        if (okNum >= pool->initialNodeNum ||
            ((okNum >= copyNum) && (curTimes > (times + CM_SERVER_PT_INITIAL_DELAY_TIME)))) {
            spool->isInitialDoing = TRUE;
            CmServerSchedueAdd(pool->poolId, CmServerViewInitialThread, ctx);
        }

        CmSleep(CM_SERVER_PT_TRY_INTERAL);
    } while (1);

    return NULL;
}

int32_t CmServerViewBuildPtEntryList(uint16_t poolId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;
    int32_t ret;

    if (pool->redundanceNum == 0) {
        return CM_OK;
    }

    spool->ptEntryList->poolId = pool->poolId;
    spool->ptEntryList->ptNum = pool->maxPtNum;

    ret = CmServerZkGetPtEntryList(spool->ptEntryList);
    if (ret != CM_OK) {
        CM_LOGERROR("Get ptEntryList failed, poolId(%u) ret(%d).", pool->poolId, ret);
        return ret;
    }

    if (spool->ptEntryList->ptNum == 0) {
        ret = CmSwitchThreadAsync(CM_SERVER_PT_INITIAL, (void *)spool, CmServerViewInitialDetectThread);
        if (ret != CM_OK) {
            CM_LOGERROR("Create detect thread failed, ret(%d).", ret);
            return ret;
        }
        return CM_OK;
    }

    ret = spool->storeOps->loadcheck(spool->storer, spool->ptEntryList);
    if (ret != CM_OK) {
        CM_LOGERROR("Check ptEntryList failed, poolId(%u) ret(%d).", pool->poolId, ret);
        return ret;
    }
    return CM_OK;
}

int32_t CmServerViewBuildPool(uint16_t poolId)
{
    int32_t ret;

    ret = CmServerViewBuildPtEntryList(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Build ptEntryList failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    ret = CmServerViewBuildNodeList(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Build nodeList failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    ret = CmServerZkSubNodeListChange(poolId, CmServerViewNodeListChange);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub node change failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    ret = CmServerZkSubNodeEvent(poolId, CmServerViewNodeEvent);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub node event failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    ret = CmServerZkSubPtEvent(poolId, CmServerViewPtEvent, CmServerViewPtEventCommit);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub pt event failed, poolId(%u) ret(%d).", poolId, ret);
        return ret;
    }

    return CM_OK;
}

int32_t CmServerViewToMaster(void)
{
    uint16_t poolId;
    int32_t ret;

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        CmServerPool *spool = &g_sPoolMgr.list[poolId];
        if (spool->pool == NULL) {
            continue;
        }
        ret = CmServerViewBuildPool(poolId);
        if (ret != CM_OK) {
            CM_LOGERROR("Build pool failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
    }
    return CM_OK;
}

int32_t CmServerViewToSlave(void)
{
    CmServerMonitorReset();

    return CM_OK;
}

int32_t CmServerViewRoleChange(CmServerRole role)
{
    g_sPoolMgr.role = role;

    if (role == CM_SERVER_MASTER) {
        return CmServerViewToMaster();
    } else {
        return CmServerViewToSlave();
    }
}

int32_t CmServerViewInitPool(uint16_t poolId)
{
    CmServerPool *spool = &g_sPoolMgr.list[poolId];
    PoolInfo *pool = spool->pool;
    size_t len;

    spool->used = TRUE;

    spool->nodeList = (NodeInfoList *)malloc(sizeof(NodeInfo) * pool->maxNodeNum + sizeof(NodeInfoList));
    spool->stateList = (NodeStateList *)malloc(sizeof(NodeStateInfo) * pool->maxNodeNum + sizeof(NodeStateList));
    if (spool->nodeList == NULL || spool->stateList == NULL) {
        CM_LOGERROR("Malloc nodeList or stateList buff failed.");
        return CM_ERR;
    }
    len = sizeof(NodeInfo) * pool->maxNodeNum + sizeof(NodeInfoList);
    memset_s(spool->nodeList, len, 0, len);
    len = sizeof(NodeStateInfo) * pool->maxNodeNum + sizeof(NodeStateList);
    memset_s(spool->stateList, len, 0, len);

    if (pool->redundanceNum == 0) {
        return CM_OK;
    }

    spool->ptEntryList = (PtEntryList *)malloc(sizeof(PtEntry) * pool->maxPtNum + sizeof(PtEntryList));
    if (spool->ptEntryList == NULL) {
        CM_LOGERROR("Malloc ptEntryList buff failed.");
        return CM_ERR;
    }
    len = sizeof(PtEntry) * pool->maxPtNum + sizeof(PtEntryList);
    memset_s(spool->ptEntryList, len, 0, len);

    uint16_t copyNum = pool->redundanceNum;
    uint16_t minCopyNum = pool->redundanceNum - 1;

    spool->calcOps = CmPtCalcOpsGet(PT_NUM_LIMIT_FIXED);
    spool->calculator = spool->calcOps->createCalculator(pool->maxNodeNum, pool->maxPtNum, copyNum, minCopyNum);
    if (spool->calculator == NULL) {
        CM_LOGERROR("Create calculator failed, poolId(%u).", pool->poolId);
        return CM_ERR;
    }

    spool->storeOps = CmPtStoreOpsGet();
    spool->storer = spool->storeOps->createStorer(pool->maxNodeNum, pool->maxPtNum, copyNum);
    if (spool->storer == NULL) {
        CM_LOGERROR("Create storer failed, poolId(%u).", pool->poolId);
        return CM_ERR;
    }

    return CM_OK;
}

void CmServerViewReset(void)
{
    uint16_t poolId;

    cm_rwlock_init(&g_sPoolMgr.lock, NULL);

    g_sPoolMgr.role = CM_SERVER_SLAVE;

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        cm_rwlock_init(&g_sPoolMgr.list[poolId].lock, NULL);
        g_sPoolMgr.list[poolId].used = FALSE;
        g_sPoolMgr.list[poolId].nodeChange = FALSE;
        g_sPoolMgr.list[poolId].stateChange = FALSE;
        g_sPoolMgr.list[poolId].ptChange = FALSE;
        g_sPoolMgr.list[poolId].pool = CmConfigGetPoolInfo(poolId);
        g_sPoolMgr.list[poolId].nodeList = NULL;
        g_sPoolMgr.list[poolId].stateList = NULL;
        g_sPoolMgr.list[poolId].ptEntryList = NULL;
        g_sPoolMgr.list[poolId].calculator = NULL;
        g_sPoolMgr.list[poolId].calcOps = NULL;
        g_sPoolMgr.list[poolId].storer = NULL;
        g_sPoolMgr.list[poolId].storeOps = NULL;
    }
}

int32_t CmServerViewGetNodeState(uint16_t poolId, NodeStateInfo *state)
{
    if (poolId >= MAX_POOL_NUM || state == NULL) {
        CM_LOGERROR("Invalid poolId(%u).", poolId);
        return CM_ERR;
    }
    if (g_sPoolMgr.list[poolId].used == FALSE || state->nodeId >= g_sPoolMgr.list[poolId].pool->maxNodeNum) {
        CM_LOGERROR("Invalid poolId(%u) nodeId(%u).", poolId, state->nodeId);
        return CM_ERR;
    }
    if (g_sPoolMgr.list[poolId].stateList == NULL) {
        CM_LOGERROR("Invalid poolId(%u).", poolId);
        return CM_ERR;
    }
    *state = g_sPoolMgr.list[poolId].stateList->nodeList[state->nodeId];
    return CM_OK;
}

int32_t CmServerViewInit(void)
{
    uint16_t poolId;
    int32_t ret;

    CmServerViewReset();

    CmServerViewRegisterExpiredHandle();

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        CmServerPool *spool = &g_sPoolMgr.list[poolId];
        if (spool->pool == NULL) {
            continue;
        }
        ret = CmServerViewInitPool(poolId);
        if (ret != CM_OK) {
            CM_LOGERROR("Init pool failed, poolId(%u) ret(%d).", poolId, ret);
            CmServerViewExit();
            return ret;
        }
    }

    ret = CmServerZkSubRoleChange(CmServerViewRoleChange);
    if (ret != CM_OK) {
        CM_LOGERROR("Select role failed, ret(%d).", ret);
        CmServerViewExit();
        return ret;
    }

    CM_LOGINFO("Cm server view init succeed.");
    return CM_OK;
}

void CmServerViewExit(void)
{
    uint16_t poolId;

    cm_rwlock_wrlock(&g_sPoolMgr.lock);
    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        if (g_sPoolMgr.list[poolId].used == FALSE) {
            continue;
        }
        g_sPoolMgr.list[poolId].used = FALSE;
        g_sPoolMgr.list[poolId].stateChange = FALSE;
        g_sPoolMgr.list[poolId].ptChange = FALSE;
        if (g_sPoolMgr.list[poolId].nodeList != NULL) {
            free(g_sPoolMgr.list[poolId].nodeList);
            g_sPoolMgr.list[poolId].nodeList = NULL;
        }
        if (g_sPoolMgr.list[poolId].stateList != NULL) {
            free(g_sPoolMgr.list[poolId].stateList);
            g_sPoolMgr.list[poolId].stateList = NULL;
        }
        if (g_sPoolMgr.list[poolId].ptEntryList != NULL) {
            free(g_sPoolMgr.list[poolId].ptEntryList);
            g_sPoolMgr.list[poolId].ptEntryList = NULL;
        }
        if (g_sPoolMgr.list[poolId].calculator != NULL) {
            g_sPoolMgr.list[poolId].calcOps->destoryCalculator(g_sPoolMgr.list[poolId].calculator);
            g_sPoolMgr.list[poolId].calculator = NULL;
        }
        if (g_sPoolMgr.list[poolId].storer != NULL) {
            g_sPoolMgr.list[poolId].storeOps->destoryStorer(g_sPoolMgr.list[poolId].storer);
            g_sPoolMgr.list[poolId].storer = NULL;
        }
        cm_rwlock_destroy(&g_sPoolMgr.list[poolId].lock);
    }
    cm_rwlock_unlock(&g_sPoolMgr.lock);
    cm_rwlock_destroy(&g_sPoolMgr.lock);
    CM_LOGINFO("Cm server view exit succeed.");
    return;
}

