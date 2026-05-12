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

#include "cm_pt_calc_fixed.h"
#include "cm_log.h"
#include "dlist.h"

#define REFER_NUM_DIFFERENCE (1)

typedef struct {
    DList node;
    uint16_t nodeId;
    uint16_t diskId;
    int64_t referNum;
} CalcElem;

typedef struct {
    uint16_t nodeNum;
    uint16_t ptNum;
    uint16_t copyNum;
    uint16_t minCopyNum;

    uint16_t diskNum;

    DList busy;
    DList free;
    CalcElem *buff;
} CalcCore;

Calculator CreateViewCalculator(uint16_t maxNodeNum, uint16_t maxPtNum, uint16_t copyNum, uint16_t minCopyNum)
{
    CalcCore *calc = (CalcCore *)malloc(sizeof(CalcCore));
    if (calc == NULL) {
        CM_LOGERROR("Malloc calculator buff failed.");
        return NULL;
    }

    uint16_t num = maxNodeNum * MAX_DISK_NUM;

    calc->buff = (CalcElem *)malloc(sizeof(CalcElem) * num);
    if (calc->buff == NULL) {
        CM_LOGERROR("Malloc calculator buff failed.");
        free(calc);
        return NULL;
    }

    D_INIT_LIST_HEAD(&calc->busy);
    D_INIT_LIST_HEAD(&calc->free);

    uint16_t index;
    for (index = 0; index < num; index++) {
        CalcElem *elem = &calc->buff[index];
        DListAddTail(&elem->node, &calc->free);
    }

    calc->nodeNum = maxNodeNum;
    calc->ptNum = maxPtNum;
    calc->copyNum = copyNum;
    calc->minCopyNum = minCopyNum;

    return (Calculator)calc;
}

void DestoryViewCalculator(Calculator calculator)
{
    CalcCore *calc = (CalcCore *)calculator;

    free(calc->buff);
    free(calc);
    return;
}

uint16_t ViewCalcNormNodeNum(NodeInfoList *nodeList, NodeStateList *stateList)
{
    uint16_t normNodeNum = 0;
    uint16_t nodeId, diskIndex;

    for (nodeId = 0; nodeId < stateList->nodeNum; nodeId++) {
        if (stateList->nodeList[nodeId].state != NODE_STATE_UP ||
            stateList->nodeList[nodeId].clusterState != NODE_CLUSTER_STATE_IN) {
            continue;
        }
        DiskList *diskList = &nodeList->nodeList[nodeId].diskList;
        NodeStateInfo *cmState = &stateList->nodeList[nodeId];
        for (diskIndex = 0; diskIndex < diskList->num; diskIndex++) {
            if (diskList->list[diskIndex].state == DISK_STATE_NORMAL &&
                cmState->diskList[diskIndex].clusterState == DISK_CLUSTER_STATE_IN) {
                normNodeNum++;
                break;
            }
        }
    }

    return normNodeNum;
}

void ViewCalcBuildBusyList(CalcCore *calc, NodeInfoList *nodeList, NodeStateList *stateList)
{
    uint16_t nodeId, diskIndex;

    for (nodeId = 0; nodeId < stateList->nodeNum; nodeId++) {
        if (stateList->nodeList[nodeId].state != NODE_STATE_UP ||
            stateList->nodeList[nodeId].clusterState != NODE_CLUSTER_STATE_IN) {
            continue;
        }
        DiskList *diskList = &nodeList->nodeList[nodeId].diskList;
        NodeStateInfo *cmState = &stateList->nodeList[nodeId];
        for (diskIndex = 0; diskIndex < diskList->num; diskIndex++) {
            if (diskList->list[diskIndex].state == DISK_STATE_NORMAL &&
                cmState->diskList[diskIndex].clusterState == DISK_CLUSTER_STATE_IN) {
                if (D_LIST_EMPTY(&calc->free)) {
                    CM_LOGERROR("Impossible.");
                    return;
                }
                DList *newNode = calc->free.next;
                DListDel(newNode);

                CalcElem *elem = D_LIST_ENTRY(newNode, CalcElem, node);
                elem->nodeId = nodeId;
                elem->diskId = 0; // 分区精确到节点级
                elem->referNum = 0;
                DListAddTail(&elem->node, &calc->busy);
                break; // 集群范围内单分区视图
            }
        }
        calc->diskNum = diskList->num;
    }
}

void ViewCalcDestroyBusyList(CalcCore *calc)
{
    CalcElem *elem;
    DList *pos, *next;
    D_LIST_FOR_EACH_SAFE(pos, next, &calc->busy)
    {
        elem = D_LIST_ENTRY(pos, CalcElem, node);
        DListDel(&elem->node);
        DListAdd(&elem->node, &calc->free);
    }
}

void ViewCalcFillUpdateBusyList(DList *head, uint16_t nodeId, uint16_t diskId, int32_t diff)
{
    CalcElem *elem, *elem1;
    CalcElem *find = NULL;
    DList *pos, *next;

    D_LIST_FOR_EACH_SAFE(pos, next, head)
    {
        elem = D_LIST_ENTRY(pos, CalcElem, node);
        if (elem->nodeId == nodeId && elem->diskId == diskId) {
            find = elem;
            break;
        }
    }

    if (find == NULL) {
        CM_LOGERROR("Impossible.");
        return;
    }

    DListDel(&find->node);
    find->referNum += diff;

    D_LIST_FOR_EACH_SAFE(pos, next, head)
    {
        elem = D_LIST_ENTRY(pos, CalcElem, node);
        if (elem->referNum <= find->referNum) {
            continue;
        }
        elem1 = D_LIST_ENTRY(elem->node.prev, CalcElem, node);
        DListAdd(&find->node, &elem1->node);
        return;
    }
    DListAddTail(&find->node, head);
    return;
}

int32_t ViewCalcCheckNodeIsNormal(NodeInfoList *nodeList, NodeStateList *stateList, uint16_t nodeId, uint16_t diskId)
{
    if (nodeId == NODE_ID_INVALID || diskId == DISK_ID_INVALID) {
        return FALSE;
    }

    if (stateList->nodeList[nodeId].state != NODE_STATE_UP ||
        stateList->nodeList[nodeId].clusterState != NODE_CLUSTER_STATE_IN) {
        return FALSE;
    }

    uint16_t diskIndex;

    DiskList *diskList = &nodeList->nodeList[nodeId].diskList;
    NodeStateInfo *cmState = &stateList->nodeList[nodeId];
    for (diskIndex = 0; diskIndex < diskList->num; diskIndex++) {
        if (diskList->list[diskIndex].diskId != diskId) {
            continue;
        }
        if (diskList->list[diskIndex].state == DISK_STATE_NORMAL &&
            cmState->diskList[diskIndex].clusterState == DISK_CLUSTER_STATE_IN) {
            return TRUE;
        }
        break;
    }

    return FALSE;
}

int32_t ViewCalcCheckNodeIsFault(NodeInfoList *nodeList, NodeStateList *stateList, uint16_t nodeId, uint16_t diskId)
{
    if (nodeId == NODE_ID_INVALID || diskId == DISK_ID_INVALID) {
        CM_LOGERROR("Impossible.");
        return TRUE;
    }

    if (stateList->nodeList[nodeId].clusterState == NODE_CLUSTER_STATE_OUT) {
        return TRUE;
    }

    uint16_t diskIndex;

    DiskList *diskList = &nodeList->nodeList[nodeId].diskList;
    NodeStateInfo *cmState = &stateList->nodeList[nodeId];
    for (diskIndex = 0; diskIndex < diskList->num; diskIndex++) {
        if (diskList->list[diskIndex].diskId != diskId) {
            continue;
        }
        if (cmState->diskList[diskIndex].clusterState == DISK_CLUSTER_STATE_OUT) {
            return TRUE;
        }
        break;
    }

    return FALSE;
}

void ViewCalcFillBusyList(CalcCore *calc, uint16_t copyIndex, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList)
{
    uint16_t ptId, nodeId, diskId;

    int32_t isNormal;

    for (ptId = 0; ptId < calc->ptNum; ptId++) {
        PtEntry *ptEntry = &ptEntryList->ptEntryList[ptId];
        if (ptEntry->copyList[copyIndex].state != PT_COPY_STATE_INIT &&
            ptEntry->copyList[copyIndex].state != PT_COPY_STATE_OUT) {
            nodeId = ptEntry->copyList[copyIndex].nodeId;
            diskId = ptEntry->copyList[copyIndex].diskId;
        } else {
            nodeId = ptEntry->copyList[copyIndex + calc->copyNum].nodeId;
            diskId = ptEntry->copyList[copyIndex + calc->copyNum].diskId;
        }
        isNormal = ViewCalcCheckNodeIsNormal(nodeList, stateList, nodeId, diskId);
        if (isNormal == TRUE) {
            ViewCalcFillUpdateBusyList(&calc->busy, nodeId, diskId, 1);
        }
    }

    return;
}

void ViewCalcResetBusyList(CalcCore *calc)
{
    CalcElem *elem;
    DList *pos, *next;

    D_LIST_FOR_EACH_SAFE(pos, next, &calc->busy)
    {
        elem = D_LIST_ENTRY(pos, CalcElem, node);
        elem->referNum = 0;
    }

    elem = D_LIST_ENTRY(calc->busy.next, CalcElem, node);
    DListDel(&elem->node);
    DListAddTail(&elem->node, &calc->busy);

    return;
}

void ViewCalcInitPtEntryList(CalcCore *calc, PtEntryList *ptEntryList)
{
    uint16_t ptId, copyIndex;

    ptEntryList->maxCopyNum = calc->copyNum;
    ptEntryList->minCopyNum = calc->minCopyNum;

    ptEntryList->ptNum = calc->ptNum;
    ptEntryList->globalVersion++;

    for (ptId = 0; ptId < calc->ptNum; ptId++) {
        ptEntryList->ptEntryList[ptId].ptId = ptId;
        ptEntryList->ptEntryList[ptId].state = PT_STATE_INIT;
        ptEntryList->ptEntryList[ptId].birthVersion = ptEntryList->globalVersion;
        ptEntryList->ptEntryList[ptId].masterNodeId = NODE_ID_INVALID;
        ptEntryList->ptEntryList[ptId].masterDiskId = DISK_ID_INVALID;
        ptEntryList->ptEntryList[ptId].copyNum = calc->copyNum;
        for (copyIndex = 0; copyIndex < MAX_COPY_NUM; copyIndex++) {
            ptEntryList->ptEntryList[ptId].copyList[copyIndex].nodeId = NODE_ID_INVALID;
            ptEntryList->ptEntryList[ptId].copyList[copyIndex].diskId = DISK_ID_INVALID;
            ptEntryList->ptEntryList[ptId].copyList[copyIndex].state = PT_COPY_STATE_INIT;
            ptEntryList->ptEntryList[ptId].copyList[copyIndex].resv = 0;
        }
    }
    return;
}

void ViewCalcInitPtEntryList1(CalcCore *calc, PtEntryList *ptEntryList, NodeInfoList *nodeList,
    NodeStateList *stateList)
{
    PtEntry *ptEntry;
    uint16_t ptId, copyIndex, index, nodeId, diskId, isFault;

    ptEntryList->globalVersion++;

    for (ptId = 0; ptId < ptEntryList->ptNum; ptId++) {
        ptEntry = &ptEntryList->ptEntryList[ptId];
        for (copyIndex = 0; copyIndex < calc->copyNum; copyIndex++) {
            if (ptEntry->copyList[copyIndex].state != PT_COPY_STATE_INIT &&
                ptEntry->copyList[copyIndex].state != PT_COPY_STATE_OUT) {
                nodeId = ptEntry->copyList[copyIndex].nodeId;
                diskId = ptEntry->copyList[copyIndex].diskId;
                index = copyIndex;
            } else {
                nodeId = ptEntry->copyList[copyIndex + calc->copyNum].nodeId;
                diskId = ptEntry->copyList[copyIndex + calc->copyNum].diskId;
                index = copyIndex + calc->copyNum;
            }
            isFault = ViewCalcCheckNodeIsFault(nodeList, stateList, nodeId, diskId);
            if (isFault == TRUE) { // 存在需要更新的副本，刷新pt版本号
                ptEntry->birthVersion = ptEntryList->globalVersion;
                ptEntry->copyList[index].nodeId = NODE_ID_INVALID;
                ptEntry->copyList[index].diskId = DISK_ID_INVALID;
                ptEntry->copyList[index].state = PT_COPY_STATE_OUT;
            }
        }
    }
}

static void ViewCalcBuildPrintf(uint16_t copyIndex, DList *head, const char *name)
{
    CalcElem *elem;
    DList *start;

    D_LIST_FOR_EACH(start, head)
    {
        elem = D_LIST_ENTRY(start, CalcElem, node);
        CM_LOGDEBUG("[%s]: copyIndex(%u) nodeId(%u) diskId(%u) referNum(%u).", name, copyIndex, elem->nodeId,
            elem->diskId, elem->referNum);
    }
}

int32_t ViewCalcBuildPtEntry(CalcCore *calc, uint16_t copyIndex, PtEntry *ptEntry, DList *head)
{
    CalcElem *elem;
    DList *pos, *next;

    uint16_t index;
    uint16_t isFound = FALSE;

    D_LIST_FOR_EACH_SAFE(pos, next, head)
    {
        elem = D_LIST_ENTRY(pos, CalcElem, node);
        for (index = 0; index < ptEntry->copyNum; index++) {
            if (ptEntry->copyList[index].state != PT_COPY_STATE_INIT &&
                ptEntry->copyList[index].state != PT_COPY_STATE_OUT &&
                elem->nodeId == ptEntry->copyList[index].nodeId) {
                isFound = TRUE;
                break;
            }
        }
        if (isFound == TRUE) {
            isFound = FALSE;
            continue;
        }

        if (ptEntry->copyList[copyIndex].state == PT_COPY_STATE_INIT) {
            ptEntry->copyList[copyIndex].nodeId = elem->nodeId;
            ptEntry->copyList[copyIndex].diskId = elem->diskId;
            ptEntry->copyList[copyIndex].state = PT_COPY_STATE_RUNNING;
        } else {
            ptEntry->copyList[copyIndex + calc->copyNum].nodeId = elem->nodeId;
            ptEntry->copyList[copyIndex + calc->copyNum].diskId = elem->diskId;
            ptEntry->copyList[copyIndex + calc->copyNum].state = PT_COPY_STATE_RECOVERY;

            ptEntry->copyNum = calc->copyNum * 2UL;
        }

        ViewCalcFillUpdateBusyList(head, elem->nodeId, elem->diskId, 1);
        return CM_OK;
    }

    CM_LOGERROR("Impossible.");
    return CM_ERR;
}

uint16_t ViewCalcPtEntryHasNodeId(PtEntry *ptEntry, uint16_t nodeId)
{
    uint16_t copyIndex;

    for (copyIndex = 0; copyIndex < ptEntry->copyNum; copyIndex++) {
        if (ptEntry->copyList[copyIndex].nodeId == nodeId) {
            return TRUE;
        }
    }
    return FALSE;
}

int32_t ViewCalcBuildPreCheck(CalcCore *calc, uint16_t *index, PtEntry *ptEntry, uint64_t globalVersion)
{
    if (ptEntry->birthVersion == globalVersion) {
        return CM_OK;
    }

    if (ptEntry->state != PT_STATE_NORMAL && ptEntry->state != PT_STATE_DEGRADE_LOSS1) {
        return CM_OK;
    }

    uint16_t nodeId, diskId;
    if (ptEntry->copyList[*index].state != PT_COPY_STATE_INIT && ptEntry->copyList[*index].state != PT_COPY_STATE_OUT) {
        nodeId = ptEntry->copyList[*index].nodeId;
        diskId = ptEntry->copyList[*index].diskId;
    } else {
        nodeId = ptEntry->copyList[*index + calc->copyNum].nodeId;
        diskId = ptEntry->copyList[*index + calc->copyNum].diskId;
        *index = *index + calc->copyNum;
    }
    if (ptEntry->masterNodeId == nodeId && ptEntry->masterDiskId == diskId) {
        return CM_OK;
    }
    if (nodeId == NODE_ID_INVALID || diskId == DISK_ID_INVALID) {
        CM_LOGERROR("Impossible.");
        return CM_OK;
    }
    return CM_ERR;
}

int32_t ViewCalcBuildPtEntry1(CalcCore *calc, uint16_t copyIndex, PtEntry *ptEntry, uint64_t globalVersion, DList *head)
{
    CalcElem *elem, *startElem;
    DList *pos, *start;

    DList *first = head->next;
    DList *last = head->prev;

    CalcElem *firstElem = D_LIST_ENTRY(first, CalcElem, node);
    CalcElem *lastElem = D_LIST_ENTRY(last, CalcElem, node);

    if (lastElem->referNum <= firstElem->referNum + REFER_NUM_DIFFERENCE) {
        return CM_OK;
    }

    uint16_t index = copyIndex;
    int32_t ret = ViewCalcBuildPreCheck(calc, &index, ptEntry, globalVersion);
    if (ret == CM_OK) {
        return CM_OK;
    }

    uint16_t nodeId = ptEntry->copyList[index].nodeId;
    uint16_t diskId = ptEntry->copyList[index].diskId;

    startElem = NULL;

    D_LIST_FOR_EACH(start, head)
    {
        startElem = D_LIST_ENTRY(start, CalcElem, node);
        if (startElem->nodeId == nodeId && startElem->diskId == diskId) {
            break;
        }
    }

    if ((startElem == NULL) || (startElem->referNum <= firstElem->referNum + REFER_NUM_DIFFERENCE)) {
        return CM_OK;
    }

    pos = head->next;
    while (pos != &startElem->node) {
        elem = D_LIST_ENTRY(pos, CalcElem, node);
        if (elem->referNum + REFER_NUM_DIFFERENCE >= startElem->referNum) {
            return CM_OK;
        }
        pos = pos->next;

        if (ViewCalcPtEntryHasNodeId(ptEntry, elem->nodeId) == TRUE) {
            continue;
        }

        ViewCalcFillUpdateBusyList(head, nodeId, diskId, -1);

        ptEntry->copyList[copyIndex].nodeId = NODE_ID_INVALID;
        ptEntry->copyList[copyIndex].diskId = DISK_ID_INVALID;
        ptEntry->copyList[copyIndex].state = PT_COPY_STATE_OUT;

        ptEntry->copyList[copyIndex + calc->copyNum].nodeId = elem->nodeId;
        ptEntry->copyList[copyIndex + calc->copyNum].diskId = elem->diskId;
        ptEntry->copyList[copyIndex + calc->copyNum].state = PT_COPY_STATE_RECOVERY;

        ptEntry->copyNum = calc->copyNum * 2UL;
        ptEntry->birthVersion = globalVersion;

        ViewCalcFillUpdateBusyList(head, elem->nodeId, elem->diskId, 1);
        return CM_OK;
    }

    CM_LOGINFO("No needed, ptId(%u).", ptEntry->ptId);
    return CM_OK;
}

int32_t ViewCalcBuildPtEntryList(CalcCore *calc, PtEntryList *ptEntryList)
{
    uint16_t copyIndex, ptId;
    int32_t ret;

    for (copyIndex = 0; copyIndex < calc->copyNum; copyIndex++) {
        for (ptId = 0; ptId < calc->ptNum; ptId++) {
            PtEntry *ptEntry = &ptEntryList->ptEntryList[ptId];
            if (ptEntry->copyList[copyIndex].state != PT_COPY_STATE_INIT &&
                ptEntry->copyList[copyIndex].state != PT_COPY_STATE_OUT) {
                CM_LOGERROR("Impossible.");
                continue;
            }
            ret = ViewCalcBuildPtEntry(calc, copyIndex, &ptEntryList->ptEntryList[ptId], &calc->busy);
            if (ret != CM_OK) {
                CM_LOGERROR("Build ptEntry failed, copyIndex(%u) poolId(%u) ptId(%u).", copyIndex, ptEntryList->poolId,
                    ptId);
                return ret;
            }
        }
        ViewCalcBuildPrintf(copyIndex, &calc->busy, "initial");
        ViewCalcResetBusyList(calc);
    }
    return CM_OK;
}

int32_t ViewCalcBuildPtEntryList1(CalcCore *calc, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList)
{
    uint16_t copyIndex, ptId;
    int32_t ret;

    for (copyIndex = 0; copyIndex < calc->copyNum; copyIndex++) {
        ViewCalcFillBusyList(calc, copyIndex, nodeList, stateList, ptEntryList);
        for (ptId = 0; ptId < calc->ptNum; ptId++) {
            PtEntry *ptEntry = &ptEntryList->ptEntryList[ptId];
            if (ptEntry->copyList[copyIndex].state != PT_COPY_STATE_INIT &&
                ptEntry->copyList[copyIndex].state != PT_COPY_STATE_OUT) {
                continue;
            }
            if (copyIndex + calc->copyNum < ptEntry->copyNum &&
                ptEntry->copyList[copyIndex + calc->copyNum].state != PT_COPY_STATE_INIT &&
                ptEntry->copyList[copyIndex + calc->copyNum].state != PT_COPY_STATE_OUT) {
                continue;
            }
            ret = ViewCalcBuildPtEntry(calc, copyIndex, &ptEntryList->ptEntryList[ptId], &calc->busy);
            if (ret != CM_OK) {
                CM_LOGERROR("Build ptEntry failed, copyIndex(%u) poolId(%u) ptId(%u).", copyIndex, ptEntryList->poolId,
                    ptId);
                return ret;
            }
        }
        for (ptId = 0; ptId < calc->ptNum; ptId++) {
            ret = ViewCalcBuildPtEntry1(calc, copyIndex, &ptEntryList->ptEntryList[ptId], ptEntryList->globalVersion,
                &calc->busy);
            if (ret != CM_OK) {
                CM_LOGERROR("Build ptEntry failed, copyIndex(%u) poolId(%u) ptId(%u).", copyIndex, ptEntryList->poolId,
                    ptId);
                return ret;
            }
        }
        ViewCalcBuildPrintf(copyIndex, &calc->busy, "rebalance");
        ViewCalcResetBusyList(calc);
    }
    return CM_OK;
}

void ViewCalcUpdatePtEntryList(PtEntryList *ptEntryList, CalcCore *calc)
{
    PtEntry *ptEntry;
    uint16_t ptId, copyIndex, normNum, isFirstRunning;

    for (ptId = 0; ptId < ptEntryList->ptNum; ptId++) {
        ptEntry = &ptEntryList->ptEntryList[ptId];
        normNum = 0;
        isFirstRunning = TRUE;
        for (copyIndex = 0; copyIndex < ptEntry->copyNum; copyIndex++) {
            if (ptEntry->copyList[copyIndex].state == PT_COPY_STATE_RUNNING) {
                normNum++;
            }
            if (ptEntry->copyList[copyIndex].state == PT_COPY_STATE_RUNNING && isFirstRunning == TRUE) {
                isFirstRunning = FALSE;
                ptEntry->masterNodeId = ptEntry->copyList[copyIndex].nodeId;
                ptEntry->masterDiskId = ptEntry->copyList[copyIndex].diskId;
            }
        }
        if (normNum != calc->copyNum) {
            CM_LOGERROR("Impossible.");
        }
        ptEntry->state = PT_STATE_NORMAL;
    }
}

void ViewCalcUpdatePtEntryList1(PtEntryList *ptEntryList, CalcCore *calc)
{
    PtEntry *ptEntry;
    uint16_t ptId, copyIndex, runningNum, firstIndex, masterIndex;

    for (ptId = 0; ptId < ptEntryList->ptNum; ptId++) {
        ptEntry = &ptEntryList->ptEntryList[ptId];
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
        if (runningNum < calc->minCopyNum) {
            ptEntry->state = PT_STATE_FAULT;
        } else if (runningNum == calc->copyNum) {
            ptEntry->state = PT_STATE_NORMAL;
        } else if (runningNum + 1 == calc->copyNum) {
            ptEntry->state = PT_STATE_DEGRADE_LOSS1;
        } else if (runningNum + 2 == calc->copyNum) {
            ptEntry->state = PT_STATE_DEGRADE_LOSS2;
        } else {
            CM_LOGERROR("Impossible.");
            ptEntry->state = PT_STATE_FAULT;
        }
    }
}

int32_t ViewCalculatorInitial(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList)
{
    CalcCore *calc = (CalcCore *)calculator;

    uint16_t normNodeNum = ViewCalcNormNodeNum(nodeList, stateList);
    if (normNodeNum < calc->copyNum) {
        CM_LOGWARN("Current normal nodeNum(%u) is not enough, config(%u).", normNodeNum, calc->copyNum);
        return CM_ERR;
    }

    ViewCalcBuildBusyList(calc, nodeList, stateList);

    ptEntryList->poolId = nodeList->poolId;

    ViewCalcInitPtEntryList(calc, ptEntryList);

    int32_t ret = ViewCalcBuildPtEntryList(calc, ptEntryList);
    if (ret != CM_OK) {
        CM_LOGERROR("Initial failed, poolId(%u).", nodeList->poolId);
        ViewCalcDestroyBusyList(calc);
        return ret;
    }
    ViewCalcDestroyBusyList(calc);

    ViewCalcUpdatePtEntryList(ptEntryList, calc);
    return CM_OK;
}

int32_t ViewCalculatorRebalance(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList)
{
    CalcCore *calc = (CalcCore *)calculator;

    uint16_t normNodeNum = ViewCalcNormNodeNum(nodeList, stateList);
    if (normNodeNum < calc->copyNum) {
        CM_LOGERROR("Current normal nodeNum(%u) is not enough, config(%u).", normNodeNum, calc->copyNum);
        return CM_ERR;
    }

    ViewCalcBuildBusyList(calc, nodeList, stateList);

    ViewCalcInitPtEntryList1(calc, ptEntryList, nodeList, stateList);

    int32_t ret = ViewCalcBuildPtEntryList1(calc, nodeList, stateList, ptEntryList);
    if (ret != CM_OK) {
        CM_LOGERROR("Rebalance failed, poolId(%u).", nodeList->poolId);
        ViewCalcDestroyBusyList(calc);
        return ret;
    }
    ViewCalcDestroyBusyList(calc);

    ViewCalcUpdatePtEntryList1(ptEntryList, calc);
    return CM_OK;
}

int32_t ViewCalcCheckPtEntryList(CalcCore *calc, PtEntryList *ptEntryList, NodeInfoList *nodeList,
    NodeStateList *stateList)
{
    PtEntry *ptEntry;
    uint16_t ptId, copyIndex, nodeId, diskId, isFault;

    for (ptId = 0; ptId < ptEntryList->ptNum; ptId++) {
        ptEntry = &ptEntryList->ptEntryList[ptId];
        for (copyIndex = 0; copyIndex < calc->copyNum; copyIndex++) {
            if (ptEntry->copyList[copyIndex].state != PT_COPY_STATE_INIT &&
                ptEntry->copyList[copyIndex].state != PT_COPY_STATE_OUT) {
                nodeId = ptEntry->copyList[copyIndex].nodeId;
                diskId = ptEntry->copyList[copyIndex].diskId;
            } else {
                nodeId = ptEntry->copyList[copyIndex + calc->copyNum].nodeId;
                diskId = ptEntry->copyList[copyIndex + calc->copyNum].diskId;
            }
            isFault = ViewCalcCheckNodeIsFault(nodeList, stateList, nodeId, diskId);
            if (isFault == TRUE) { // 存在需要更新的副本，刷新pt版本号
                return TRUE;
            }
        }
    }
    return FALSE;
}

int32_t ViewCalcCheckPtEntryList1(CalcCore *calc, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList)
{
    uint16_t ptId, copyIndex, nodeId, diskId;

    int32_t isNormal;
    uint16_t normPtNum = 0;
    for (ptId = 0; ptId < calc->ptNum; ptId++) {
        PtEntry *ptEntry = &ptEntryList->ptEntryList[ptId];
        if (ptEntry->state == PT_STATE_NORMAL) {
            normPtNum++;
        }
        for (copyIndex = 0; copyIndex < calc->copyNum; copyIndex++) {
            if (ptEntry->copyList[copyIndex].state != PT_COPY_STATE_INIT &&
                ptEntry->copyList[copyIndex].state != PT_COPY_STATE_OUT) {
                nodeId = ptEntry->copyList[copyIndex].nodeId;
                diskId = ptEntry->copyList[copyIndex].diskId;
            } else {
                nodeId = ptEntry->copyList[copyIndex + calc->copyNum].nodeId;
                diskId = ptEntry->copyList[copyIndex + calc->copyNum].diskId;
            }
            isNormal = ViewCalcCheckNodeIsNormal(nodeList, stateList, nodeId, diskId);
            if (isNormal == TRUE) {
                ViewCalcFillUpdateBusyList(&calc->busy, nodeId, diskId, 1);
            }
        }
    }

    CalcElem *elem;
    DList *pos, *next;

    D_LIST_FOR_EACH_SAFE(pos, next, &calc->busy)
    {
        elem = D_LIST_ENTRY(pos, CalcElem, node);
        if (elem->referNum == 0 && normPtNum != 0) {
            return TRUE;
        }
    }

    return FALSE;
}

int32_t ViewCalculatorNeedRebalance(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList)
{
    CalcCore *calc = (CalcCore *)calculator;

    if (ptEntryList->ptNum == 0) {
        return FALSE;
    }

    uint16_t normNodeNum = ViewCalcNormNodeNum(nodeList, stateList);
    if (normNodeNum < 1) {
        CM_LOGWARN("Current normal nodeNum(%u) is not enough, config(%u).", normNodeNum, calc->copyNum);
        return FALSE;
    }

    int32_t ret = ViewCalcCheckPtEntryList(calc, ptEntryList, nodeList, stateList);
    if (ret == TRUE) {
        return TRUE;
    }

    ViewCalcBuildBusyList(calc, nodeList, stateList);

    ret = ViewCalcCheckPtEntryList1(calc, nodeList, stateList, ptEntryList);
    ViewCalcDestroyBusyList(calc);
    return ret;
}

