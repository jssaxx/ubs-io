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

#include "cm_server_monitor.h"
#include "cm_comm.h"
#include "cm_config.h"
#include "cm_log.h"
#include "cm_server_schedule.h"
#include "cm_thread.h"

#define MONITOR_THREAD_NAME "cm_server_monitor"

#define MONITOR_PERM_FAULT_TIME (CmConfigGetPermFaultTimeOut() / 1000)

#define MONITOR_DISK_PERM_FAULT_TIME (CmConfigGetDiskPermFaultTimeOut() / 1000)

#define MONITOR_INTERAL_TIME (5000)

typedef enum{
    RECORD_STATE_NORMAL = 0,
    RECORD_STATE_FAULT = 1,
} RecordState;

typedef struct {
    RecordState state;
    uint16_t id;
    uint64_t times;
} RecordInfo;

typedef struct {
    uint16_t used;
    uint16_t nodeId;
    RecordInfo node;
    uint16_t diskNum;
    RecordInfo diskList[DISK_LIST_NUM];
} NodeRecord;

typedef struct {
    uint16_t poolId;
    uint16_t num;
    NodeRecord list[];
} NodeRecordList;

typedef struct {
    CM_RWLOCK_T lock;
    uint16_t used;
    PoolInfo *pool;
    uint16_t activeNum;
    uint16_t idle;
    NodeRecordList *nodeList;
} PoolRecord;

typedef struct {
    CM_RWLOCK_T lock;
    PoolRecord list[MAX_POOL_NUM];
    CM_SEM_S sem;
    CmServerMonitorExpiredHandle handle;
} FaultMonitor;

static FaultMonitor g_faultMonitor;

int g_monitorWatchFlag = 1;

void CmServerMonitorRegisterHandle(CmServerMonitorExpiredHandle handle)
{
    g_faultMonitor.handle = handle;
}

void CmServerListenNodeFault(uint16_t poolId, uint16_t nodeId)
{
    PoolRecord *record = &g_faultMonitor.list[poolId];
    NodeRecordList *nodeList = record->nodeList;

    CM_RWLOCK_WRLOCK(&record->lock);
    if (nodeList->list[nodeId].used == FALSE) {
        CM_LOGINFO("Listen, poolId(%u) nodeId(%u).", poolId, nodeId);
        nodeList->list[nodeId].used = TRUE;
        nodeList->list[nodeId].node.state = RECORD_STATE_FAULT;
        nodeList->list[nodeId].node.id = nodeId;
        nodeList->list[nodeId].node.times = CmGetSecondsTime();
        record->activeNum++;
    } else {
        if (nodeList->list[nodeId].node.state == RECORD_STATE_FAULT) {
            CM_RWLOCK_UNLOCK(&record->lock);
            return;
        }
        CM_LOGINFO("Listen, poolId(%u) nodeId(%u).", poolId, nodeId);
        nodeList->list[nodeId].node.state = RECORD_STATE_FAULT;
        nodeList->list[nodeId].node.id = nodeId;
        nodeList->list[nodeId].node.times = CmGetSecondsTime();

        nodeList->list[nodeId].diskNum = 0;
    }
    CM_RWLOCK_UNLOCK(&record->lock);

    CM_SEM_UP(&g_faultMonitor.sem);
    return;
}

void CmServerListenDiskFault(uint16_t poolId, uint16_t nodeId, uint16_t diskId)
{
    PoolRecord *record = &g_faultMonitor.list[poolId];
    NodeRecordList *nodeList = record->nodeList;

    CM_RWLOCK_WRLOCK(&record->lock);
    if (nodeList->list[nodeId].used == FALSE) {
        CM_LOGINFO("Listen, poolId(%u) nodeId(%u) diskId(%u).", poolId, nodeId, diskId);
        nodeList->list[nodeId].used = TRUE;
        nodeList->list[nodeId].diskList[nodeList->list[nodeId].diskNum].state = RECORD_STATE_FAULT;
        nodeList->list[nodeId].diskList[nodeList->list[nodeId].diskNum].id = diskId;
        nodeList->list[nodeId].diskList[nodeList->list[nodeId].diskNum].times = CmGetSecondsTime();
        nodeList->list[nodeId].diskNum++;
        record->activeNum++;
    } else {
        if (nodeList->list[nodeId].node.state == RECORD_STATE_FAULT) {
            CM_RWLOCK_UNLOCK(&record->lock);
            return;
        }
        uint16_t index;
        for (index = 0; index < nodeList->list[nodeId].diskNum; index++) {
            if (nodeList->list[nodeId].diskList[index].id == diskId) {
                CM_RWLOCK_UNLOCK(&record->lock);
                return;
            }
        }
        CM_LOGINFO("Listen, poolId(%u) nodeId(%u) diskId(%u).", poolId, nodeId, diskId);
        nodeList->list[nodeId].diskList[nodeList->list[nodeId].diskNum].state = RECORD_STATE_FAULT;
        nodeList->list[nodeId].diskList[nodeList->list[nodeId].diskNum].id = diskId;
        nodeList->list[nodeId].diskList[nodeList->list[nodeId].diskNum].times = CmGetSecondsTime();
        nodeList->list[nodeId].diskNum++;
    }
    CM_RWLOCK_UNLOCK(&record->lock);

    CM_SEM_UP(&g_faultMonitor.sem);
    return;
}

void CmServerCancelNodeFault(uint16_t poolId, uint16_t nodeId)
{
    PoolRecord *record = &g_faultMonitor.list[poolId];
    NodeRecordList *nodeList = record->nodeList;

    CM_RWLOCK_WRLOCK(&record->lock);
    if (nodeList->list[nodeId].used == FALSE) {
        CM_RWLOCK_UNLOCK(&record->lock);
        return;
    } else {
        CM_LOGINFO("Cancel, poolId(%u) nodeId(%u).", poolId, nodeId);
        nodeList->list[nodeId].used = FALSE;
        nodeList->list[nodeId].node.state = RECORD_STATE_NORMAL;
        nodeList->list[nodeId].node.id = nodeId;
        nodeList->list[nodeId].node.times = 0;

        nodeList->list[nodeId].diskNum = 0;
        record->activeNum--;
    }
    CM_RWLOCK_UNLOCK(&record->lock);

    return;
}

void CmServerCancelDiskFault(uint16_t poolId, uint16_t nodeId, uint16_t diskId)
{
    PoolRecord *record = &g_faultMonitor.list[poolId];
    NodeRecordList *nodeList = record->nodeList;

    CM_RWLOCK_WRLOCK(&record->lock);
    if (nodeList->list[nodeId].used == FALSE) {
        CM_RWLOCK_UNLOCK(&record->lock);
        return;
    } else {
        if (nodeList->list[nodeId].node.state == RECORD_STATE_FAULT) {
            CM_RWLOCK_UNLOCK(&record->lock);
            return;
        }
        uint16_t index;
        uint16_t diskIndex;
        diskIndex = nodeList->list[nodeId].diskNum;
        for (index = 0; index < nodeList->list[nodeId].diskNum; index++) {
            if (nodeList->list[nodeId].diskList[index].id == diskId) {
                diskIndex = index;
                CM_LOGINFO("Cancel, poolId(%u) nodeId(%u) diskId(%u).", poolId, nodeId, diskId);
                continue;
            }
            if (diskIndex != nodeList->list[nodeId].diskNum) {
                nodeList->list[nodeId].diskList[diskIndex] = nodeList->list[nodeId].diskList[index];
                diskIndex++;
            }
        }
        nodeList->list[nodeId].diskNum = diskIndex;

        if (nodeList->list[nodeId].diskNum == 0) {
            nodeList->list[nodeId].used = FALSE;
            record->activeNum--;
        }
    }
    CM_RWLOCK_UNLOCK(&record->lock);

    return;
}

static void CmServerMonitorResetNode(uint16_t poolId, uint16_t nodeId)
{
    NodeRecord *nodeInfo = &g_faultMonitor.list[poolId].nodeList->list[nodeId];

    nodeInfo->used = FALSE;
    nodeInfo->nodeId = nodeId;

    nodeInfo->node.state = RECORD_STATE_NORMAL;
    nodeInfo->node.id = nodeId;
    nodeInfo->node.times = 0;

    nodeInfo->diskNum = 0;

    return;
}

void CmServerMonitorPoolExpiredUpdate(PoolRecord *record, uint64_t curTimes)
{
    NodeRecordList *nodeList = record->nodeList;
    uint16_t nodeId;
    uint16_t index;

    CM_RWLOCK_WRLOCK(&record->lock);

    for (nodeId = 0; nodeId < nodeList->num; nodeId++) {
        if (nodeList->list[nodeId].used == FALSE) {
            continue;
        }
        if (nodeList->list[nodeId].node.state == RECORD_STATE_FAULT) {
            if (nodeList->list[nodeId].node.times + MONITOR_PERM_FAULT_TIME <= curTimes) {
                CmServerMonitorResetNode(record->pool->poolId, nodeId);
                record->activeNum--;
                continue;
            }
            continue;
        }
        if (nodeList->list[nodeId].diskNum == 0) {
            continue;
        }
        uint16_t list[DISK_LIST_NUM] = {0};
        uint16_t num = 0;
        for (index = 0; index < nodeList->list[nodeId].diskNum; index++) {
            if (nodeList->list[nodeId].diskList[index].times + MONITOR_DISK_PERM_FAULT_TIME <= curTimes) {
                continue;
            }
            list[num++] = index;
        }
        if (num == 0) {
            CmServerMonitorResetNode(record->pool->poolId, nodeId);
            record->activeNum--;
            continue;
        }
        for (index = 0; index < num; index++) {
            nodeList->list[nodeId].diskList[index] = nodeList->list[nodeId].diskList[list[index]];
        }
        nodeList->list[nodeId].diskNum = num;
    }

    CM_RWLOCK_UNLOCK(&record->lock);

    return;
}

void *CmServerMonitorPoolExpiredHandle(void *ctx)
{
    PoolRecord *record = (PoolRecord *)ctx;
    NodeRecordList *nodeList = record->nodeList;
    uint16_t nodeId;
    uint16_t index;

    uint64_t curTimes = CmGetSecondsTime();

    CM_RWLOCK_WRLOCK(&record->lock);

    for (nodeId = 0; nodeId < nodeList->num; nodeId++) {
        if (nodeList->list[nodeId].used == FALSE) {
            continue;
        }
        if (nodeList->list[nodeId].node.state == RECORD_STATE_FAULT) {
            if (nodeList->list[nodeId].node.times + MONITOR_PERM_FAULT_TIME <= curTimes) {
                g_faultMonitor.handle.expiredNodeSet(record->pool->poolId, nodeId);
                continue;
            }
            continue;
        }
        for (index = 0; index < nodeList->list[nodeId].diskNum; index++) {
            if (nodeList->list[nodeId].diskList[index].times + MONITOR_DISK_PERM_FAULT_TIME <= curTimes) {
                g_faultMonitor.handle.expiredDiskSet(record->pool->poolId, nodeId,
                                                     nodeList->list[nodeId].diskList[index].id);
                continue;
            }
        }
    }

    CM_RWLOCK_UNLOCK(&record->lock);

    int32_t ret = g_faultMonitor.handle.expiredCommit(record->pool->poolId);
    if (ret != CM_OK) {
        CM_LOGWARN("Expired pool handle failed, continue, poolId(%u).", record->pool->poolId);
        record->idle = TRUE;
        return NULL;
    }

    CmServerMonitorPoolExpiredUpdate(record, curTimes);

    record->idle = TRUE;

    return NULL;
}

uint16_t CmServerMonitorPoolIsExpired(uint16_t poolId)
{
    PoolRecord *record = &g_faultMonitor.list[poolId];
    NodeRecordList *nodeList = record->nodeList;
    uint16_t nodeId;
    uint16_t index;

    uint64_t curTimes = CmGetSecondsTime();

    for (nodeId = 0; nodeId < nodeList->num; nodeId++) {
        if (nodeList->list[nodeId].used == FALSE) {
            continue;
        }
        if (nodeList->list[nodeId].node.state == RECORD_STATE_FAULT) {
            if (nodeList->list[nodeId].node.times + MONITOR_PERM_FAULT_TIME <= curTimes) {
                return TRUE;
            }
            continue;
        }
        for (index = 0; index < nodeList->list[nodeId].diskNum; index++) {
            if (nodeList->list[nodeId].diskList[index].state == RECORD_STATE_FAULT) {
                if (nodeList->list[nodeId].diskList[index].times + MONITOR_DISK_PERM_FAULT_TIME <= curTimes) {
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

void *CmServerMonitorDetectThread(void *ctx)
{
    UNREFERENCE_PARAM(ctx);

    while (1) {
        uint16_t isContinue = FALSE;
        uint16_t isExpired = FALSE;

        uint16_t poolId;

        if (g_monitorWatchFlag != 1) {
            break;
        }

        for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
            PoolRecord *record = &g_faultMonitor.list[poolId];
            if (record->used == FALSE || record->activeNum == 0) {
                continue;
            }
            isContinue = TRUE;
            CM_RWLOCK_RDLOCK(&record->lock);
            if (record->idle == FALSE) {
                CM_RWLOCK_UNLOCK(&record->lock); // current has generated task;
                continue;
            }
            isExpired = CmServerMonitorPoolIsExpired(poolId);
            if (isExpired == TRUE) {
                record->idle = FALSE;
            }
            CM_RWLOCK_UNLOCK(&record->lock);
            if (isExpired == TRUE) {
                CmServerSchedueAdd(poolId, CmServerMonitorPoolExpiredHandle, (void *)record);
            }
        }

        if (isContinue == FALSE) {
            CM_SEM_DOWN(&g_faultMonitor.sem);
        }

        CmSleep(MONITOR_INTERAL_TIME);
    }

    return NULL;
}

static void CmServerMonitorResetPool(uint16_t poolId)
{
    PoolRecord *record = &g_faultMonitor.list[poolId];
    PoolInfo *pool = record->pool;
    uint16_t nodeId;

    NodeRecordList *nodeList = record->nodeList;

    CM_RWLOCK_WRLOCK(&record->lock);

    record->activeNum = 0;
    record->idle = TRUE;

    nodeList->poolId = pool->poolId;
    nodeList->num = pool->maxNodeNum;

    for (nodeId = 0; nodeId < pool->maxNodeNum; nodeId++) {
        nodeList->list[nodeId].used = FALSE;
        nodeList->list[nodeId].nodeId = nodeId;

        nodeList->list[nodeId].node.state = RECORD_STATE_NORMAL;
        nodeList->list[nodeId].node.id = nodeId;
        nodeList->list[nodeId].node.times = 0;

        nodeList->list[nodeId].diskNum = 0;
    }

    CM_RWLOCK_UNLOCK(&record->lock);
    return;
}

int32_t CmServerMonitorLoadPool(uint16_t poolId)
{
    PoolRecord *record = &g_faultMonitor.list[poolId];
    PoolInfo *pool = record->pool;

    NodeRecordList *nodeList = (NodeRecordList *)malloc(sizeof(NodeRecord) * pool->maxNodeNum + sizeof(NodeRecordList));
    if (nodeList == NULL) {
        CM_LOGERROR("Malloc nodeList buff failed.");
        return CM_ERR;
    }

    record->used = TRUE;
    record->nodeList = nodeList;

    CmServerMonitorResetPool(poolId);

    return CM_OK;
}

void CmServerMonitorInitMgr(void)
{
    uint16_t poolId;

    CM_RWLOCK_INIT(&g_faultMonitor.lock, NULL);

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        CM_RWLOCK_INIT(&g_faultMonitor.list[poolId].lock, NULL);
        g_faultMonitor.list[poolId].used = FALSE;
        g_faultMonitor.list[poolId].pool = CmConfigGetPoolInfo(poolId);
        g_faultMonitor.list[poolId].activeNum = 0;
        g_faultMonitor.list[poolId].idle = TRUE;
        g_faultMonitor.list[poolId].nodeList = NULL;
    }
}

void CmServerMonitorReset(void)
{
    uint16_t poolId;

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        if (g_faultMonitor.list[poolId].used == FALSE) {
            continue;
        }
        CmServerMonitorResetPool(poolId);
    }
    return;
}

int32_t CmServerMonitorInit(void)
{
    uint16_t poolId;
    int32_t ret;

    CmServerMonitorInitMgr();

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        PoolInfo *pool = g_faultMonitor.list[poolId].pool;
        if (pool == NULL) {
            continue;
        }
        ret = CmServerMonitorLoadPool(poolId);
        if (ret != CM_OK) {
            CM_LOGERROR("Load pool failed, ret(%d).", ret);
            CmServerMonitorExit();
            return ret;
        }
    }

    CM_SEM_INIT(&g_faultMonitor.sem, 0);
    ret = CmSwitchThreadAsync(MONITOR_THREAD_NAME, NULL, CmServerMonitorDetectThread);
    if (ret != CM_OK) {
        CM_LOGERROR("Create detect thread failed, ret(%d).", ret);
        CmServerMonitorExit();
        return ret;
    }

    CM_LOGINFO("Cm server monitor init succeed.");
    return CM_OK;
}

void CmServerMonitorExit(void)
{
    uint16_t poolId;
    g_monitorWatchFlag = 0;
    CM_RWLOCK_WRLOCK(&g_faultMonitor.lock);
    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        if (g_faultMonitor.list[poolId].used == FALSE) {
            continue;
        }
        g_faultMonitor.list[poolId].used = FALSE;
        if (g_faultMonitor.list[poolId].nodeList != NULL) {
            free(g_faultMonitor.list[poolId].nodeList);
            g_faultMonitor.list[poolId].nodeList = NULL;
        }
        CM_RWLOCK_DESTROY(&g_faultMonitor.list[poolId].lock);
    }
    CM_RWLOCK_UNLOCK(&g_faultMonitor.lock);
    CM_RWLOCK_DESTROY(&g_faultMonitor.lock);
    CM_LOGINFO("Cm server monitor exit succeed.");
    return;
}
