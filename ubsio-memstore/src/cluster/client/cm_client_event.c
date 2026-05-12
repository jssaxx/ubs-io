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

#include "cm_client_event.h"
#include "cm_client_schedule.h"
#include "cm_client_local.h"
#include "cm_zkadapter.h"
#include "cm_thread.h"
#include "cm_config.h"
#include "cm_comm.h"
#include "cm_log.h"

#define REPORT_TRY_DELAY_TIME (5000)

static void *CmClientNodeEventHandle(void *arg)
{
    CmNodeEvent *nodeEvent = (CmNodeEvent *)arg;
    int32_t ret;

    while (1) {
        if (CmClientZkNodeEventExistCheck(nodeEvent->poolId, nodeEvent->nodeId) != CM_NOT_EXIST) {
            CmSleep(REPORT_TRY_DELAY_TIME);
            continue;
        }

        ret = CmClientZkRecordNodeEvent(nodeEvent);
        if (ret != CM_OK) {
            CmSleep(REPORT_TRY_DELAY_TIME);
            continue;
        }
        break;
    }
    free(nodeEvent);
    return NULL;
}

static int32_t CmClientNodeEventReport(uint16_t poolId, uint16_t nodeId, uint16_t eventType)
{
    CmNodeEvent *nodeEvent = (CmNodeEvent *)malloc(sizeof(CmNodeEvent));
    if (nodeEvent == NULL) {
        CM_LOGERROR("Malloc nodeEvent buff failed, poolId(%u).", poolId);
        return CM_ERR;
    }

    nodeEvent->eventType = eventType;
    nodeEvent->poolId = poolId;
    nodeEvent->nodeId = nodeId;

    CmClientSchedueAdd(poolId, CmClientNodeEventHandle, (void *)nodeEvent);
    return CM_OK;
}

static void *CmClientPtEventHandle(void *arg)
{
    CmPtEvent *ptEvent = (CmPtEvent *)arg;
    int32_t ret;

    while (1) {
        if (CmClientZkPtEventExistCheck(ptEvent->poolId, ptEvent->nodeId) != CM_NOT_EXIST) {
            CmSleep(REPORT_TRY_DELAY_TIME);
            continue;
        }

        ret = CmClientZkRecordPtEvent(ptEvent);
        if (ret != CM_OK) {
            CmSleep(REPORT_TRY_DELAY_TIME);
            continue;
        }
        break;
    }
    free(ptEvent);
    return NULL;
}

static int32_t CmClientPtEventReport(uint16_t poolId, uint16_t nodeId, uint16_t eventType, uint16_t ptNum,
    PtFinish *eventList)
{
    CmPtEvent *ptEvent = (CmPtEvent *)malloc(sizeof(CmPtEvent) + sizeof(CmPtFinish) * ptNum);
    if (ptEvent == NULL) {
        CM_LOGERROR("Malloc ptEvent buff failed, poolId(%u).", poolId);
        return CM_ERR;
    }

    ptEvent->eventType = eventType;
    ptEvent->poolId = poolId;
    ptEvent->nodeId = nodeId;
    ptEvent->ptNum = ptNum;

    uint16_t index;
    for (index = 0; index < ptNum; index++) {
        ptEvent->ptList[index].birthVersion = eventList[index].birthVersion;
        ptEvent->ptList[index].ptId = eventList[index].ptId;
        ptEvent->ptList[index].resv = 0;
    }

    CmClientSchedueAdd(poolId, CmClientPtEventHandle, (void *)ptEvent);
    return CM_OK;
}

static int32_t CmClientEventCheck(uint16_t poolId, uint16_t eventType)
{
    PoolInfo *poolInfo = CmConfigGetPoolInfo(poolId);
    if (poolInfo == NULL) {
        CM_LOGERROR("Invalid poolId(%u).", poolId);
        return CM_ERR;
    }

    if ((poolInfo->redundanceNum == 0) && (eventType == CM_EVENT_PT_FINISH)) {
        CM_LOGERROR("Invalid eventType, poolId(%u).", poolId);
        return CM_ERR;
    }

    return CM_OK;
}

int32_t CM_SetDiskStatus(uint16_t poolId, uint16_t diskId, DiskState state)
{
    NodeInfo nodeInfo;

    int32_t ret = CmClientEventCheck(poolId, CM_EVENT_DISK);
    if (ret != CM_OK) {
        return ret;
    }

    ret = CmClientLocalGetNodeInfo(poolId, &nodeInfo);
    if (ret != CM_OK) {
        CM_LOGERROR("Get nodeInfo failed, ret(%d) nodeId(%u), poolId(%u).", ret, nodeInfo.nodeId, poolId);
        return ret;
    }

    DiskList *diskList = &nodeInfo.diskList;

    uint16_t index;
    int32_t isNeedUpdate = FALSE;
    for (index = 0; index < diskList->num; index++) {
        if (diskList->list[index].diskId != diskId) {
            continue;
        }
        if (diskList->list[index].state != state) {
            diskList->list[index].state = state;
            isNeedUpdate = TRUE;
        }
        break;
    }
    if (isNeedUpdate == FALSE) {
        return CM_OK;
    }

    CM_LOGINFO("Setdisk, poolId(%u) nodeId(%u) diskId(%u) state(%s).", poolId, nodeInfo.nodeId, diskId,
        CM_DISK_STATE(state));

    ret = CmClientZkRecordNodeInfo(poolId, &nodeInfo);
    if (ret != CM_OK) {
        CM_LOGERROR("Update nodeInfo failed, ret(%d) nodeId(%u), poolId(%u).", ret, nodeInfo.nodeId, poolId);
        return ret;
    }

    CmClientLocalUpdateNodeInfo(poolId, &nodeInfo);

    return CmClientNodeEventReport(poolId, nodeInfo.nodeId, CM_EVENT_DISK);
}

int32_t CM_SetPtFinishStatus(uint16_t poolId, uint16_t ptNum, PtFinish *eventList)
{
    int32_t ret = CmClientEventCheck(poolId, CM_EVENT_PT_FINISH);
    if (ret != CM_OK) {
        return ret;
    }

    size_t len = sizeof(CmNodeEvent) + sizeof(PtFinish) * ptNum;
    if (len > NODE_META_BUFF_LEN) {
        CM_LOGERROR("PtNum(%u) too much, poolId(%u).", ptNum, poolId);
        return CM_ERR;
    }
    uint16_t nodeId = CmClientLocalGetNodeId(poolId);
    CM_LOGINFO("Setptfinish, poolId(%u) nodeId(%u) ptNum(%u), pt id: %hu", poolId, nodeId, ptNum, eventList->ptId);
    return CmClientPtEventReport(poolId, nodeId, CM_EVENT_PT_FINISH, ptNum, eventList);
}

int32_t CmClientEventInit(void)
{
    CM_LOGINFO("Cm client event init succeed.");
    return CM_OK;
}

void CmClientEventExit(void)
{
    return;
}

