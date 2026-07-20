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

#include "cm_pt_calc_fixed_state.h"
#include "cm_log.h"

#define LOSS_NUM_TWO (2)
void ViewPtUpdateCopyKeepAlive(PtEntry *ptEntry, uint16_t minCopyNum)
{
    uint16_t index;

    uint16_t normNum = 0;
    for (index = 0; index < ptEntry->copyNum; index++) {
        if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING) {
            normNum++;
        }
    }

    if (normNum == minCopyNum) {
        for (index = 0; index < ptEntry->copyNum; index++) {
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING) {
                ptEntry->copyList[index].keepAlive = TRUE;
            }
        }
    }

    if (normNum > minCopyNum) {
        for (index = 0; index < ptEntry->copyNum; index++) {
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING) {
                ptEntry->copyList[index].keepAlive = FALSE;
            }
        }
    }
}

void ViewPtUpdateCopyState(PtEntry *ptEntry, uint16_t copyIndex, uint16_t minCopyNum)
{
    uint16_t index;
    uint16_t normNum;

    normNum = 0;

    for (index = 0; index < ptEntry->copyNum; index++) {
        if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING) {
            normNum++;
        }
    }

    if (normNum >= minCopyNum) {
        ptEntry->copyList[copyIndex].state = PT_COPY_STATE_RECOVERY;
        return;
    }

    if (ptEntry->copyList[copyIndex].keepAlive == TRUE) {
        ptEntry->copyList[copyIndex].state = PT_COPY_STATE_RUNNING;
        return; // 不满足，最低正常副本要求，且keepAlive标识为最后掉线副本，强制置NORMAL
    } else {
        ptEntry->copyList[copyIndex].state = PT_COPY_STATE_RECOVERY;
        return; // 不满足，最低正常副本要求，且keepAlive标识非最后掉线副本，置RECOVERY
    }
}

void ViewPtEntryUpdatePtState(PtEntry *ptEntry, PtEntryList *ptList)
{
    uint16_t copyIndex;
    uint16_t runningNum;
    uint16_t firstIndex;
    uint16_t masterIndex;

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
    if (masterIndex == INVALID_VALUE16 && firstIndex != INVALID_VALUE16) {
        ptEntry->masterNodeId = ptEntry->copyList[firstIndex].nodeId;
        ptEntry->masterDiskId = ptEntry->copyList[firstIndex].diskId;
    }
    if (runningNum < ptList->minCopyNum) {
        ptEntry->state = PT_STATE_FAULT;
    } else if (runningNum == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_NORMAL;
    } else if (runningNum + 1 == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_DEGRADE_LOSS1;
    } else if (runningNum + LOSS_NUM_TWO == ptList->maxCopyNum) {
        ptEntry->state = PT_STATE_DEGRADE_LOSS2;
    } else {
        CM_LOGERROR("Impossible.");
        ptEntry->state = PT_STATE_FAULT;
    }
}

void ViewPtEntryListUpdateNodeDown(uint16_t nodeId, PtEntryList *ptList, int32_t *pgChange)
{
    PtEntry *ptEntry;
    uint16_t ptId;
    uint16_t index;

    for (ptId = 0; ptId < ptList->ptNum; ptId++) {
        ptEntry = &ptList->ptEntryList[ptId];
        for (index = 0; index < ptEntry->copyNum; index++) {
            if (ptEntry->copyList[index].nodeId != nodeId) {
                continue;
            }
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING) {
                ptEntry->copyList[index].state = PT_COPY_STATE_DOWN;
                *pgChange = TRUE;
                ptEntry->referNum++;
                ViewPtUpdateCopyKeepAlive(ptEntry, ptList->minCopyNum);
                ViewPtEntryUpdatePtState(ptEntry, ptList);
                break;
            }
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RECOVERY) {
                ptEntry->copyList[index].state = PT_COPY_STATE_DOWN;
                *pgChange = TRUE;
                ptEntry->referNum++;
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
    uint16_t ptId;
    uint16_t index;

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
                ptEntry->referNum++;
                ViewPtUpdateCopyState(ptEntry, index, ptList->minCopyNum);
                ViewPtEntryUpdatePtState(ptEntry, ptList);
                break;
            }
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING && diskfault == TRUE) {
                ptEntry->copyList[index].state = PT_COPY_STATE_DOWN;
                *pgChange = TRUE;
                ptEntry->referNum++;
                ViewPtUpdateCopyKeepAlive(ptEntry, ptList->minCopyNum);
                ViewPtEntryUpdatePtState(ptEntry, ptList);
                break;
            }
            if (ptEntry->copyList[index].state == PT_COPY_STATE_RECOVERY && diskfault == TRUE) {
                ptEntry->copyList[index].state = PT_COPY_STATE_DOWN;
                *pgChange = TRUE;
                ptEntry->referNum++;
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
}

static int32_t ViewPtEntryTrim(PtEntry *ptEntry, uint16_t copyNum, uint16_t ptNum, uint16_t *masterList,
                               uint16_t nodeId, uint16_t validNum)
{
    uint16_t copyIndex;
    uint16_t statNum = 0;

    for (copyIndex = 0; copyIndex < ptEntry->copyNum; copyIndex++) {
        if (ptEntry->copyList[copyIndex].state == PT_COPY_STATE_RUNNING) {
            statNum++;
        }
    }
    if (statNum != copyNum) {
        return FALSE;
    }

    for (copyIndex = 0; copyIndex < copyNum; copyIndex++) {
        if (ptEntry->copyList[copyIndex].state == PT_COPY_STATE_RUNNING) {
            continue;
        }
        if (ptEntry->copyList[copyIndex + copyNum].state == PT_COPY_STATE_RUNNING) {
            ptEntry->copyList[copyIndex] = ptEntry->copyList[copyIndex + copyNum];
            ptEntry->copyList[copyIndex + copyNum].nodeId = NODE_ID_INVALID;
            ptEntry->copyList[copyIndex + copyNum].diskId = DISK_ID_INVALID;
            ptEntry->copyList[copyIndex + copyNum].keepAlive = FALSE;
            ptEntry->copyList[copyIndex + copyNum].state = PT_COPY_STATE_OUT;
            continue;
        }
        CM_LOGERROR("Impossible.");
    }
    ptEntry->state = PT_STATE_NORMAL;
    ptEntry->copyNum = copyNum;

    if (validNum == 0) {
        return TRUE;
    }

    uint16_t nodeIndex = INVALID_VALUE16;
    for (copyIndex = 0; copyIndex < copyNum; copyIndex++) {
        if (ptEntry->copyList[copyIndex].nodeId == nodeId) {
            nodeIndex = copyIndex;
        }
    }

    if ((nodeIndex != INVALID_VALUE16) && (masterList[nodeId] < (ptNum / validNum)) &&
        (masterList[ptEntry->masterNodeId] > (ptNum / validNum))) {
        masterList[ptEntry->masterNodeId]--;
        ptEntry->masterNodeId = ptEntry->copyList[nodeIndex].nodeId;
        ptEntry->masterDiskId = ptEntry->copyList[nodeIndex].diskId;
        ptEntry->referNum++;
        masterList[nodeId]++;
    }

    return TRUE;
}

int32_t ViewPtSatisfiedCopyNum(PtEntry *ptEntry, uint16_t minCopyNum)
{
    uint16_t index;

    uint16_t normNum = 0;
    for (index = 0; index < ptEntry->copyNum; index++) {
        if (ptEntry->copyList[index].state == PT_COPY_STATE_RUNNING) {
            normNum++;
        }
    }

    if (normNum < minCopyNum) {
        return FALSE;
    }

    return TRUE;
}

uint16_t *GenMasterList(PtEntryList *ptEntryList, uint16_t nodeNum)
{
    size_t len = sizeof(uint16_t) * nodeNum;
    uint16_t *masterList = (uint16_t *)malloc(len);
    if (masterList == NULL) {
        CM_LOGERROR("Malloc fail, len(%u).", len);
        return NULL;
    }
    memset_s(masterList, len, 0, len);

    uint16_t index;
    for (index = 0; index < ptEntryList->ptNum; index++) {
        if (ptEntryList->ptEntryList[index].masterNodeId < nodeNum) {
            masterList[ptEntryList->ptEntryList[index].masterNodeId] += 1UL;
        }
    }

    return masterList;
}

void ViewPtEntryListUpdateNodeFinish(uint16_t nodeId, CmPtFinish *ptList, uint16_t ptNum, PtEntryList *ptEntryList,
                                     int32_t *ptChange, uint16_t nodeNum, uint16_t validNum)
{
    PtEntry *ptEntry = NULL;
    uint16_t index;
    uint16_t copyIndex;

    uint16_t *masterList = GenMasterList(ptEntryList, nodeNum);
    if (masterList == NULL) {
        return;
    }

    for (index = 0; index < ptNum; index++) {
        if (ptList[index].ptId >= ptEntryList->ptNum) {
            CM_LOGERROR("Invalid, poolId(%u) ptId(%u).", ptEntryList->poolId, ptList[index].ptId);
            continue;
        }
        ptEntry = &ptEntryList->ptEntryList[ptList[index].ptId];
        if (ptEntry->birthVersion != ptList[index].birthVersion) {
            continue;
        }
        if (ViewPtSatisfiedCopyNum(ptEntry, ptEntryList->minCopyNum) == FALSE) {
            CM_LOGWARN("Invalid, poolId(%u) ptId(%u), not satisfy min copynum.", ptEntryList->poolId,
                       ptList[index].ptId);
            continue;
        }
        for (copyIndex = 0; copyIndex < ptEntry->copyNum; copyIndex++) {
            if (ptEntry->copyList[copyIndex].nodeId == nodeId &&
                ptEntry->copyList[copyIndex].state == PT_COPY_STATE_RECOVERY) {
                *ptChange = TRUE;
                ptEntry->copyList[copyIndex].state = PT_COPY_STATE_RUNNING;
                ViewPtUpdateCopyKeepAlive(ptEntry, ptEntryList->minCopyNum);
                int32_t isUpdate =
                    ViewPtEntryTrim(ptEntry, ptEntryList->maxCopyNum, ptEntryList->ptNum, masterList, nodeId, validNum);
                if (isUpdate == FALSE) {
                    ViewPtEntryUpdatePtState(ptEntry, ptEntryList);
                }
                break;
            }
        }
    }

    free(masterList);
}
