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

#include "cm_pt_calc_fixed_state.h"
#include "cm_log.h"

void ViewPtUpdateCopyState(PtEntry *ptEntry, uint16_t copyIndex, uint16_t minCopyNum)
{
    uint16_t index, normNum;

    normNum = 0;

    for (index = 0; index < ptEntry->copyNum; index++) {
        if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING) {
            normNum++;
        }
    }

    if (normNum >= 1) {
        ptEntry->copyList[copyIndex].state = PT_COPY_STATE_RECOVERY;
        return;
    }

    ptEntry->copyList[copyIndex].state = PT_COPY_STATE_RUNNING;
    return;
}

void ViewPtEntryUpdatePtState(PtEntry *ptEntry, PtEntryList *ptList)
{
    uint16_t copyIndex, runningNum, firstIndex, masterIndex;

    runningNum = 0;
    firstIndex = masterIndex = INVALID_VALUE16;
    for (copyIndex = 0; copyIndex < ptEntry->copyNum; copyIndex++) {
        if (ptEntry->copyList[copyIndex].state != PT_COPY_STATE_RUNNING) {
            continue;
        }
        runningNum++;
        if (firstIndex == INVALID_VALUE16) {
            firstIndex = copyIndex;
        }
        if (ptEntry->copyList[copyIndex].nodeId == ptEntry->masterNodeId &&
            ptEntry->copyList[copyIndex].diskId == ptEntry->masterDiskId) {
            masterIndex = copyIndex;
        }
    }
    if (masterIndex == INVALID_VALUE16 && firstIndex != INVALID_VALUE16) { // 发生切主
        ptEntry->masterNodeId = ptEntry->copyList[firstIndex].nodeId;
        ptEntry->masterDiskId = ptEntry->copyList[firstIndex].diskId;
    }
    if (runningNum < ptList->minCopyNum) {
        ptEntry->state = PT_STATE_FAULT;
    } else if (runningNum == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_NORMAL;
    } else if (runningNum + 1 == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_DEGRADE_LOSS1;
    } else if (runningNum + 2 == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_DEGRADE_LOSS2;
    } else {
        CM_LOGERROR("Impossible.");
        ptEntry->state = PT_STATE_FAULT;
    }
}

void ViewPtEntryListUpdateNodeDown(uint16_t nodeId, PtEntryList *ptList, int32_t *pgChange)
{
    PtEntry *ptEntry;
    uint16_t ptId, index;

    for (ptId = 0; ptId < ptList->ptNum; ptId++) {
        ptEntry = &ptList->ptEntryList[ptId];
        for (index = 0; index < ptEntry->copyNum; index++) {
            if (ptEntry->copyList[index].nodeId != nodeId) {
                continue;
            }
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING) {
                ptEntry->copyList[index].state = PT_COPY_STATE_DOWN;
                *pgChange = TRUE;
                ptEntry->birthVersion++;
                ViewPtEntryUpdatePtState(ptEntry, ptList);
                break;
            }
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RECOVERY) {
                ptEntry->copyList[index].state = PT_COPY_STATE_DOWN;
                *pgChange = TRUE;
                ptEntry->birthVersion++;
                break;
            }
        }
    }
}

int32_t ViewPtEntryCheckNetFault(NodeInfo *nodeInfo)
{
    NetList *netList = &nodeInfo->netList;

    if (netList->num == 0) {
        return FALSE; // 不需要监控IOs网卡
    }

    NetState state = netList->list[0].state;
    uint16_t index;

    for (index = 1; index < netList->num; index++) {
        if (netList->list[index].state != state) {
            return FALSE; // 存在可用IO网卡
        }
    }

    if (state == NET_STATE_FAULT) {
        return TRUE; // 启动IO网卡故障监控
    }

    return FALSE;
}

int32_t ViewPtEntryCheckDiskFault(NodeInfo *nodeInfo, uint16_t diskId)
{
    DiskList *diskList = &nodeInfo->diskList;

    if (diskList->num == 0) {
        CM_LOGERROR("Impossible.");
        return FALSE;
    }

    uint16_t index;

    for (index = 0; index < diskList->num; index++) {
        if (diskList->list[index].diskId != diskId) {
            continue;
        }
        if (diskList->list[index].state == DISK_STATE_FAULT) {
            return TRUE;
        } else {
            return FALSE;
        }
    }

    CM_LOGERROR("Impossible.");
    return FALSE;
}

void ViewPtEntryListUpdateNodeUp(uint16_t nodeId, NodeInfo *info, PtEntryList *ptList, int32_t *pgChange)
{
    PtEntry *ptEntry;
    uint16_t ptId, index;

    int32_t netfault = ViewPtEntryCheckNetFault(info);
    if (netfault == TRUE) {
        ViewPtEntryListUpdateNodeDown(nodeId, ptList, pgChange);
        return;
    }

    for (ptId = 0; ptId < ptList->ptNum; ptId++) {
        ptEntry = &ptList->ptEntryList[ptId];
        for (index = 0; index < ptEntry->copyNum; index++) {
            if (ptEntry->copyList[index].nodeId != nodeId) {
                continue;
            }
            int32_t diskfault = ViewPtEntryCheckDiskFault(info, ptEntry->copyList[index].diskId);
            if (ptEntry->copyList[index].state == PT_COPY_STATE_DOWN && diskfault == FALSE) {
                *pgChange = TRUE;
                ptEntry->birthVersion++;
                ViewPtUpdateCopyState(ptEntry, index, ptList->minCopyNum);
                ViewPtEntryUpdatePtState(ptEntry, ptList);
                break;
            }
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING && diskfault == TRUE) {
                ptEntry->copyList[index].state = PT_COPY_STATE_DOWN;
                *pgChange = TRUE;
                ptEntry->birthVersion++;
                ViewPtEntryUpdatePtState(ptEntry, ptList);
                break;
            }
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RECOVERY && diskfault == TRUE) {
                ptEntry->copyList[index].state = PT_COPY_STATE_DOWN;
                *pgChange = TRUE;
                ptEntry->birthVersion++;
                break;
            }
        }
    }
}

void ViewPtEntryListUpdateNodeState(uint16_t nodeId, NodeState state, NodeInfo *info, PtEntryList *ptList,
    int32_t *pgChange)
{
    if (state == NODE_STATE_DOWN) {
        ViewPtEntryListUpdateNodeDown(nodeId, ptList, pgChange);
    } else if (state == NODE_STATE_UP) {
        ViewPtEntryListUpdateNodeUp(nodeId, info, ptList, pgChange);
    } else {
        CM_LOGERROR("Impossible.");
    }
    if (*pgChange == TRUE) {
        ptList->globalVersion++;
    }
}

static void ViewPtUpdateCopyState1(PtEntry *ptEntry, PtEntryList *ptList)
{
    uint16_t copyIndex;
    uint16_t runningNum = 0;

    for (copyIndex = 0; copyIndex < ptList->maxCopyNum; copyIndex++) {
        if (ptEntry->copyList[copyIndex].state == PT_COPY_STATE_RUNNING) {
            runningNum++;
            continue;
        }
    }

    if (runningNum < ptList->minCopyNum) {
        ptEntry->state = PT_STATE_FAULT;
    } else if (runningNum == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_NORMAL;
        ptEntry->copyNum = ptList->maxCopyNum;
    } else if (runningNum + 1 == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_DEGRADE_LOSS1;
    } else if (runningNum + 2 == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_DEGRADE_LOSS2;
    } else {
        CM_LOGERROR("Impossible.");
        ptEntry->state = PT_STATE_FAULT;
    }

    uint16_t firstIndex, masterIndex;

    firstIndex = masterIndex = INVALID_VALUE16;
    for (copyIndex = 0; copyIndex < ptEntry->copyNum; copyIndex++) {
        if (ptEntry->copyList[copyIndex].state != PT_COPY_STATE_RUNNING) {
            continue;
        }
        if (firstIndex == INVALID_VALUE16) {
            firstIndex = copyIndex;
        }
        if (ptEntry->copyList[copyIndex].nodeId == ptEntry->masterNodeId &&
            ptEntry->copyList[copyIndex].diskId == ptEntry->masterDiskId) {
            masterIndex = copyIndex;
        }
    }
    if (firstIndex != INVALID_VALUE16 && firstIndex != masterIndex) { // 发生切主
        ptEntry->masterNodeId = ptEntry->copyList[firstIndex].nodeId;
        ptEntry->masterDiskId = ptEntry->copyList[firstIndex].diskId;
    }

    return;
}

void ViewPtEntryListUpdateNodeFinish(uint16_t nodeId, CmPtFinish *ptList, uint16_t ptNum, PtEntryList *ptEntryList,
    int32_t *ptChange, uint16_t nodeNum)
{
    PtEntry *ptEntry = NULL;
    uint16_t index, copyIndex;

    for (index = 0; index < ptNum; index++) {
        if (ptList[index].ptId >= ptEntryList->ptNum) {
            CM_LOGERROR("Invalid, poolId(%u) ptId(%u).", ptEntryList->poolId, ptList[index].ptId);
            continue;
        }
        ptEntry = &ptEntryList->ptEntryList[ptList[index].ptId];
        if (ptEntry->birthVersion != ptList[index].birthVersion) {
            CM_LOGDEBUG("pt id: %u, birthVersion not match old(%u) new(%u).", ptList[index].ptId, ptEntry->birthVersion,
                        ptList[index].birthVersion);
            continue;
        }
        for (copyIndex = 0; copyIndex < ptEntry->copyNum; copyIndex++) {
            CM_LOGDEBUG("try to update pt copy status, pt id: %u, copy node id: %u copy status: %u, update node id: %u",
                        ptList[index].ptId, ptEntry->copyList[copyIndex].nodeId, ptEntry->copyList[copyIndex].state,
                        nodeId);
            if (ptEntry->copyList[copyIndex].nodeId == nodeId &&
                ptEntry->copyList[copyIndex].state == PT_COPY_STATE_RECOVERY) {
                CM_LOGDEBUG("start update pt copy status, pt id: %u.", ptList[index].ptId);
                ptEntry->copyList[copyIndex].state = PT_COPY_STATE_RUNNING;
                *ptChange = TRUE;
                ViewPtUpdateCopyState1(ptEntry, ptEntryList);
                break;
            }
        }
    }
    if (*ptChange == TRUE) {
        ptEntryList->globalVersion++;
    }
}

