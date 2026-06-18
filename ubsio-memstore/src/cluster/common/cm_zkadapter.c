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

#include "cm_zkadapter.h"
#include "cm_client_schedule.h"
#include "cm_server_schedule.h"
#include "cm_config.h"
#include "cm_thread.h"
#include "cm_comm.h"
#include "cm_log.h"

static zhandle_t *g_zh = NULL;

static const int UNWATCH_ZNODE = 0;
static const int UNCHECK_VERSION = -1;

#define USER_DATA_MAX_LEN 4096

const int ZOO_CONNECTED_STATE = 3;
const int ZOO_CONNECTING_STATE = 1;
const int ZOO_ASSOCIATING_STATE = 2;
const int ZOO_EXPIRED_SESSION_STATE = -112;
const int ZOO_AUTH_FAILED_STATE = 999;

const int ZOO_CREATED_EVENT = 1;
const int ZOO_DELETED_EVENT = 2;
const int ZOO_CHANGED_EVENT = 3;
const int ZOO_CHILD_EVENT = 4;
const int ZOO_SESSION_EVENT = 5;
const int ZOO_NOTWATCHING_EVENT = -1;

const int ZOO_EPHEMERAL = 1;
const int ZOO_SEQUENCE = 2;

static struct ACL OPEN_ACL_UNSAFE_ACL[] = {{0x1f, {"world", "anyone"}}};
struct ACL_vector ZOO_OPEN_ACL_UNSAFE = {1, OPEN_ACL_UNSAFE_ACL};

#if DESC("zk client")
typedef struct {
    cm_rwlock_t lock;
    uint16_t used;
    PoolInfo *pool;
    NodeStateInfo local;
    NodeStateInfo master;
    NodeStateList *stateList;
    NodeInfoList *nodeList;
    PtEntryList *ptList;
    nodeChangeNotifyFp nodeChange;
    stateChangeNotifyFp stateChange;
    masterChangeNotifyFp masterChange;
    ptChangeNotifyFp ptChange;
    char userKey[USER_DATA_MAX_LEN];
    char userValue[USER_DATA_MAX_LEN];
    DataInfoChangeOpHandle userHandle;
} ZkRestoreC;

typedef struct {
    ZkRestoreC restore[MAX_POOL_NUM];
} CmClientZkMgr;

static CmClientZkMgr g_cZkMgr;

int32_t CmClientZkGetNodeId(uint16_t poolId, const char *ipv4AddrStr, uint16_t port, uint16_t *nodeId)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    uint16_t cfgNodeId = CmConfigGetNodeId();

    int32_t len = (int32_t)sizeof(uint16_t);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%s:%u", CM_POOL, poolId, CM_IP_PATH, ipv4AddrStr, port);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)nodeId, &len, NULL);
    if (ret != ZOK && ret != ZNONODE) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    if (ret == ZNONODE) {
        return CM_NOT_EXIST;
    }

    if (cfgNodeId != NODE_ID_INVALID && cfgNodeId != *nodeId) {
        CM_LOGERROR("Recover ip(%s) not matched nodeId(%u).", ipv4AddrStr, cfgNodeId);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmClientZkRecordNodeInfo(uint16_t poolId, NodeInfo *nodeInfo)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_NODE_INFO_PATH, nodeInfo->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    int len = (int)sizeof(NodeInfo);

    ret = CmZkExists(g_zh, zkPath, UNWATCH_ZNODE, NULL);
    if (ret != ZOK) {
        if (ret == ZNONODE) {
            ret = CmZkCreate(g_zh, zkPath, (char *)nodeInfo, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
            if (ret != ZOK) {
                CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
                return CM_ERR;
            }
            return CM_OK;
        } else {
            CM_LOGERROR("Check znode(%s) exists failed, ret(%d).", zkPath, ret);
            return CM_ERR;
        }
    }

    ret = CmZkSet(g_zh, zkPath, (char *)nodeInfo, len, UNCHECK_VERSION);
    if (ret != ZOK) {
        CM_LOGERROR("Set znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    return CM_OK;
}

static int32_t CmClientZkRecordNodeId(uint16_t poolId, NodeInfo *nodeInfo)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%s:%u", CM_POOL, poolId, CM_IP_PATH, nodeInfo->ipv4AddrStr,
        nodeInfo->port);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    int len = (int)sizeof(uint16_t);

    ret = CmZkExists(g_zh, zkPath, UNWATCH_ZNODE, NULL);
    if (ret != ZOK) {
        if (ret == ZNONODE) {
            ret = CmZkCreate(g_zh, zkPath, (char *)&nodeInfo->nodeId, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
            if (ret != ZOK) {
                CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
                return CM_ERR;
            }
            return CM_OK;
        } else {
            CM_LOGERROR("Check znode(%s) exists failed, ret(%d).", zkPath, ret);
            return CM_ERR;
        }
    }

    ret = CmZkSet(g_zh, zkPath, (char *)&nodeInfo->nodeId, len, UNCHECK_VERSION);
    if (ret != ZOK) {
        CM_LOGERROR("Set znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    return CM_OK;
}

static int32_t CmClientZkAtoi(const char *str)
{
    int32_t num = 0;
    while (str != NULL && str[0] >= '0' && str[0] <= '9') {
        num = num * CM_ZK_RADIX_10 + (str[0] - '0');
        str++;
    }
    return num;
}

uint16_t CmClientZkGetNodeIdByPath(const char *path, const char *pre)
{
    size_t step = strlen(pre);
    const char *str = &path[step];

    return (uint16_t)CmClientZkAtoi(str);
}

int32_t CmClientZkGenNodeIdV2(uint16_t poolId, NodeInfo *nodeInfo)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t retLen = (int32_t)sizeof(uint16_t);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%s:%u", CM_POOL, poolId, CM_IP_PATH, nodeInfo->ipv4AddrStr,
        nodeInfo->port);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    CM_LOGINFO("call zk get begin");
    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)&nodeInfo->nodeId, &retLen, NULL);
    CM_LOGINFO("call zk get end");
    if (ret == ZOK) {
        CM_LOGINFO("Recover ip(%s) matched nodeId(%u).", nodeInfo->ipv4AddrStr, nodeInfo->nodeId);
        return CmClientZkRecordNodeId(poolId, nodeInfo);
    } else if (ret == ZNONODE) {
        // 第一次启动：通过ZK生成全局唯一nodeid
        char zkPathT[CM_ZNODE_PATH_LEN] = {0};
        char zkPathB[CM_ZNODE_PATH_LEN] = {0};
        int32_t BLen = CM_ZNODE_PATH_LEN;
        ret = sprintf_s(zkPathT, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_NODEID_GENERATE_CHILD_PATH);
        if (ret < 0) {
            CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
            return CM_ERR;
        }
        ret = CmZkCreate(g_zh, zkPathT, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL | ZOO_SEQUENCE, zkPathB, BLen);
        if (ret != ZOK) {
            CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPathT, ret);
            return CM_ERR;
        }
        nodeInfo->nodeId = CmClientZkGetNodeIdByPath(zkPathB, zkPathT);
        ret = CmZkDelete(g_zh, zkPathB, UNCHECK_VERSION);
        if (ret != (int32_t)CM_OK) {
            CM_LOGERROR("Delete znode(%s) failed, ret(%d).", zkPathB, ret);
            return CM_ERR;
        }
        return CmClientZkRecordNodeId(poolId, nodeInfo);
    } else {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
}

int32_t CmClientZkGenNodeId(uint16_t poolId, NodeInfo *nodeInfo)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    uint16_t cfgNodeId = CmConfigGetNodeId();
    if (cfgNodeId == NODE_ID_INVALID) {
        return CmClientZkGenNodeIdV2(poolId, nodeInfo);
    }

    int32_t retLen = (int32_t)sizeof(uint16_t);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%s:%u", CM_POOL, poolId, CM_IP_PATH, nodeInfo->ipv4AddrStr,
        nodeInfo->port);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)&nodeInfo->nodeId, &retLen, NULL);
    if (ret == ZOK) {
        if (nodeInfo->nodeId == cfgNodeId) {
            CM_LOGINFO("Recover ip(%s) matched nodeId(%u).", nodeInfo->ipv4AddrStr, cfgNodeId);
            return CmClientZkRecordNodeId(poolId, nodeInfo);
        } else {
            CM_LOGERROR("Recover ip(%s) not matched nodeId(%u).", nodeInfo->ipv4AddrStr, cfgNodeId);
            return CM_ERR;
        }
    } else if (ret == ZNONODE) {
        nodeInfo->nodeId = cfgNodeId;
        return CmClientZkRecordNodeId(poolId, nodeInfo);
    } else {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
}

static int32_t CmClientZkCheckNodeIdExist(uint16_t poolId, uint16_t nodeId)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_NODE_PATH, nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    uint16_t cnt = 0;
    do {
        ret = CmZkExists(g_zh, zkPath, UNWATCH_ZNODE, NULL);
        if (ret == ZNONODE) {
            CM_LOGINFO("Check local znode(%s) not exist.", zkPath);
            return CM_OK;
        }
        CM_LOGWARN("Local znode exist, waiting, retry(%u ms).", cnt);
        CmSleep(CM_ZK_TRY_INTERAL);
        cnt += CM_ZK_TRY_INTERAL;
    } while (cnt < CM_ZK_NODE_DETECT_TIME);

    CM_LOGERROR("Local znode exist, not wait, retry(%u ms).", cnt);
    return CM_ERR;
}

int32_t CmClientZkRegisterNodeInfo(uint16_t poolId, NodeInfo *nodeInfo)
{
    int32_t ret;

    ret = CmClientZkGenNodeId(poolId, nodeInfo);
    if (ret != CM_OK) {
        CM_LOGERROR("Gen nodeId failed, poolId(%u) ip(%s) ret(%d).", poolId, nodeInfo->ipv4AddrStr, ret);
        return ret;
    }

    ret = CmClientZkRecordNodeInfo(poolId, nodeInfo);
    if (ret != CM_OK) {
        CM_LOGERROR("Record nodeInfo failed, poolId(%u) ip(%s) ret(%d).", poolId, nodeInfo->ipv4AddrStr, ret);
        return ret;
    }

    ret = CmClientZkCheckNodeIdExist(poolId, nodeInfo->nodeId);
    if (ret != CM_OK) {
        CM_LOGERROR("Check local node failed, poolId(%u) ip(%s) ret(%d).", poolId, nodeInfo->ipv4AddrStr, ret);
        return ret;
    }

    if (nodeInfo->nodeId >= g_cZkMgr.restore[poolId].pool->maxNodeNum) {
        CM_LOGERROR("Check node num failed, poolId(%u) max(%d) local(%d).", poolId,
            g_cZkMgr.restore[poolId].pool->maxNodeNum, nodeInfo->nodeId);
        return CM_ERR;
    }

    g_cZkMgr.restore[poolId].local.sessionId = CmGetMilliTime();
    g_cZkMgr.restore[poolId].local.nodeId = nodeInfo->nodeId;
    g_cZkMgr.restore[poolId].local.state = NODE_STATE_INVALID;
    g_cZkMgr.restore[poolId].local.clusterState = NODE_CLUSTER_STATE_INVALID;

    return CM_OK;
}

int32_t CmClientZkRecordMr(uint16_t poolId, NodeMetaBuff *mr)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeMetaBuff) + mr->len);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_MR_PATH, mr->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkExists(g_zh, zkPath, UNWATCH_ZNODE, NULL);
    if (ret != ZOK) {
        if (ret == ZNONODE) {
            ret = CmZkCreate(g_zh, zkPath, (char *)mr, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
            if (ret != ZOK) {
                CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
                return CM_ERR;
            }
            return CM_OK;
        } else {
            CM_LOGERROR("Check znode(%s) exists failed, ret(%d).", zkPath, ret);
            return CM_ERR;
        }
    }

    ret = CmZkSet(g_zh, zkPath, (char *)mr, len, UNCHECK_VERSION);
    if (ret != ZOK) {
        CM_LOGERROR("Set znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    return CM_OK;
}

int32_t CmClientZkRecordLocalSession(uint16_t poolId)
{
    NodeStateInfo *local = &g_cZkMgr.restore[poolId].local;

    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_NODE_PATH, local->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    uint64_t *value = &local->sessionId;
    ret = CmZkCreate(g_zh, zkPath, (char *)value, (int)sizeof(uint64_t), &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL, NULL, 0);
    if (ret != ZOK) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmClientZkGetNodeSession(uint16_t poolId, uint16_t nodeId, uint64_t *sessionId)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int len = (int)sizeof(uint64_t);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_NODE_PATH, nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)sessionId, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmClientZkGetMr(uint16_t poolId, NodeMetaBuff *mr)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = NODE_META_BUFF_LEN;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_MR_PATH, mr->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)mr, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmClientZkGetNodeState(uint16_t poolId, uint16_t nodeId, NodeStateInfo *cmState)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo) * g_cZkMgr.restore[poolId].pool->maxNodeNum);

    NodeStateList *stateList = malloc(len);
    if (stateList == NULL) {
        CM_LOGERROR("Alloc stateList buff failed, poolId(%u).", poolId);
        return CM_ERR;
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_STATE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        free(stateList);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)stateList, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        free(stateList);
        return CM_ERR;
    }

    *cmState = stateList->nodeList[nodeId];
    free(stateList);
    return CM_OK;
}

int32_t CmClientZkGetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)sizeof(NodeInfo);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_NODE_INFO_PATH, nodeInfo->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)nodeInfo, &len, NULL);
    if (ret != ZOK && ret != ZNONODE) {
        CM_LOGINFO("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    if (ret == ZNONODE) {
        return CM_NOT_EXIST;
    }
    return CM_OK;
}

int32_t CmClientZkNodeEventExistCheck(uint16_t poolId, uint16_t nodeId)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_NODE_EVENT_PATH, nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkExists(g_zh, zkPath, UNWATCH_ZNODE, NULL);
    if (ret != ZOK && ret != ZNONODE) {
        CM_LOGERROR("Check znode(%s) exist failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    if (ret == ZNONODE) {
        return CM_NOT_EXIST;
    }
    return CM_OK;
}

int32_t CmClientZkPtEventExistCheck(uint16_t poolId, uint16_t nodeId)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_PT_EVENT_PATH, nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkExists(g_zh, zkPath, UNWATCH_ZNODE, NULL);
    if (ret != ZOK && ret != ZNONODE) {
        CM_LOGERROR("Check znode(%s) exist failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    if (ret == ZNONODE) {
        return CM_NOT_EXIST;
    }
    return CM_OK;
}

int32_t CmClientZkRecordNodeEvent(CmNodeEvent *nodeEvent)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(CmNodeEvent));

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, nodeEvent->poolId, CM_NODE_EVENT_PATH,
        nodeEvent->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkCreate(g_zh, zkPath, (char *)nodeEvent, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmClientZkRecordPtEvent(CmPtEvent *ptEvent)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(CmPtEvent) + sizeof(CmPtFinish) * ptEvent->ptNum);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, ptEvent->poolId, CM_PT_EVENT_PATH,
        ptEvent->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkCreate(g_zh, zkPath, (char *)ptEvent, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

static void CmClientZkUpdateNodeList(NodeInfoList *changeList)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[changeList->poolId];
    NodeInfoList *nodeList = restore->nodeList;
    uint16_t nodeId;

    NodeInfoList *notifyList = malloc(sizeof(NodeInfoList) + sizeof(NodeInfo) * restore->pool->maxNodeNum);
    if (notifyList == NULL) {
        CM_LOGERROR("Alloc nodeList buff failed, poolId(%u).", restore->pool->poolId);
        return;
    }
    notifyList->poolId = changeList->poolId;
    notifyList->nodeNum = 0;

    cm_rwlock_wrlock(&restore->lock);
    nodeList->poolId = changeList->poolId;
    nodeList->nodeNum = changeList->nodeNum;
    for (nodeId = 0; nodeId < nodeList->nodeNum; nodeId++) {
        if (memcmp(&nodeList->nodeList[nodeId], &changeList->nodeList[nodeId], sizeof(NodeInfo)) != 0) {
            nodeList->nodeList[nodeId] = changeList->nodeList[nodeId];
            notifyList->nodeList[notifyList->nodeNum] = changeList->nodeList[nodeId];
            notifyList->nodeNum++;
        }
    }
    cm_rwlock_unlock(&restore->lock);
    if (restore->nodeChange != NULL) {
        restore->nodeChange(notifyList);
    }
    free(notifyList);
    return;
}

static int32_t CmClientZkSubNodeList(uint16_t poolId);

static void CmClientZkSubNodeListWatch(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    ZkRestoreC *restore = (ZkRestoreC *)watcherCtx;
    int32_t ret;

    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(state);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGINFO("evtype(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_CHANGED_EVENT) {
        ret = CmClientZkSubNodeList(restore->pool->poolId);
        if (ret != CM_OK) {
            CM_LOGERROR("Sub nodeList v2 failed, ret(%d).", ret);
        }
        return;
    }

    return;
}

static int32_t CmClientZkSubNodeList(uint16_t poolId)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[poolId];
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeInfoList) + sizeof(NodeInfo) * restore->pool->maxNodeNum);

    NodeInfoList *nodeList = malloc(len);
    if (nodeList == NULL) {
        CM_LOGERROR("Alloc nodeList buff failed, poolId(%u).", restore->pool->poolId);
        return CM_ERR;
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_NODE_LIST_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkWget(g_zh, zkPath, CmClientZkSubNodeListWatch, restore, (char *)nodeList, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    CmClientZkUpdateNodeList(nodeList);
    free(nodeList);
    return CM_OK;
}

static void CmClientZkUpdateStateList(NodeStateList *changeList)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[changeList->poolId];
    NodeStateList *stateList = restore->stateList;
    uint16_t nodeId;
    int32_t ret;

    NodeStateList *notifyList = malloc(sizeof(NodeStateList) + sizeof(NodeStateInfo) * restore->pool->maxNodeNum);
    if (notifyList == NULL) {
        CM_LOGERROR("Alloc stateList buff failed, poolId(%u).", restore->pool->poolId);
        return;
    }
    notifyList->poolId = changeList->poolId;
    notifyList->nodeNum = 0;
    notifyList->masterNodeId = changeList->masterNodeId;

    uint16_t masterNodeId = NODE_ID_INVALID;
    cm_rwlock_wrlock(&restore->lock);
    stateList->poolId = changeList->poolId;
    stateList->nodeNum = changeList->nodeNum;
    if (stateList->masterNodeId != changeList->masterNodeId) {
        stateList->masterNodeId = masterNodeId = changeList->masterNodeId;
    }
    for (nodeId = 0; nodeId < stateList->nodeNum; nodeId++) {
        if ((stateList->nodeList[nodeId].state != NODE_STATE_UP) &&
            (changeList->nodeList[nodeId].state == NODE_STATE_UP)) {
            ret = CmClientZkGetNodeSession(changeList->poolId, nodeId, &changeList->nodeList[nodeId].sessionId);
            if (ret != CM_OK) {
                cm_rwlock_unlock(&restore->lock);
                free(notifyList);
                return;
            }
        } else {
            changeList->nodeList[nodeId].sessionId = stateList->nodeList[nodeId].sessionId; // 补齐
        }
        if (memcmp(&stateList->nodeList[nodeId], &changeList->nodeList[nodeId], sizeof(NodeStateInfo)) != 0) {
            stateList->nodeList[nodeId] = changeList->nodeList[nodeId];
            notifyList->nodeList[notifyList->nodeNum] = stateList->nodeList[nodeId];
            notifyList->nodeNum++;
        }
    }
    cm_rwlock_unlock(&restore->lock);
    if (restore->masterChange != NULL && masterNodeId != NODE_ID_INVALID) {
        restore->masterChange(changeList->poolId, masterNodeId);
    }
    if (notifyList->nodeNum != 0) {
        restore->stateChange(notifyList);
    }
    free(notifyList);
    return;
}

static int32_t CmClientZkSubStateList(uint16_t poolId);

static void CmClientZkSubStateListWatch(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    ZkRestoreC *restore = (ZkRestoreC *)watcherCtx;
    int32_t ret;

    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(state);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGINFO("evtype(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_CHANGED_EVENT) {
        ret = CmClientZkSubStateList(restore->pool->poolId);
        if (ret != CM_OK) {
            CM_LOGERROR("Sub stateList v2 failed, ret(%d).", ret);
        }
        return;
    }

    return;
}

static int32_t CmClientZkSubStateList(uint16_t poolId)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[poolId];
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo) * restore->pool->maxNodeNum);

    NodeStateList *stateList = malloc(len);
    if (stateList == NULL) {
        CM_LOGERROR("Alloc stateList buff failed, poolId(%u).", restore->pool->poolId);
        return CM_ERR;
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_STATE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkWget(g_zh, zkPath, CmClientZkSubStateListWatch, restore, (char *)stateList, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    CmClientZkUpdateStateList(stateList);
    free(stateList);
    return CM_OK;
}

int32_t CmClientZkSubStateListChange(uint16_t poolId, stateChangeNotifyFp notifyFp)
{
    int32_t ret;

    CM_LOGINFO("First, sub state change, poolId(%u).", poolId);

    g_cZkMgr.restore[poolId].stateChange = notifyFp;

    ret = CmClientZkSubNodeList(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub nodeList v2 failed, poolId(%u) ret(%d).", poolId, ret);
        return CM_ERR;
    }
    ret = CmClientZkSubStateList(poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub nodeList v2 failed, poolId(%u) ret(%d).", poolId, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmClientZkPtEntryIsSame(PtEntry *elem1, PtEntry *elem2)
{
    if (elem1->ptId != elem2->ptId || elem1->state != elem2->state || elem1->birthVersion != elem2->birthVersion ||
        elem1->masterNodeId != elem2->masterNodeId || elem1->masterDiskId != elem2->masterDiskId ||
        elem1->copyNum != elem2->copyNum) {
        return FALSE;
    }

    uint16_t index;
    for (index = 0; index < elem1->copyNum; index++) {
        if (elem1->copyList[index].state != elem2->copyList[index].state ||
            elem1->copyList[index].nodeId != elem2->copyList[index].nodeId ||
            elem1->copyList[index].diskId != elem2->copyList[index].diskId) {
            return FALSE;
        }
    }
    return TRUE;
}

static void CmClientZkUpdatePtList(PtEntryList *changeList)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[changeList->poolId];
    PtEntryList *ptList = restore->ptList;
    uint16_t ptId;

    if (changeList->ptNum == 0) {
        return;
    }

    PtEntryList *notifyList = (PtEntryList *)malloc(sizeof(PtEntry) * restore->pool->maxPtNum + sizeof(PtEntryList));
    if (notifyList == NULL) {
        CM_LOGERROR("Alloc ptList buff failed, poolId(%u).", restore->pool->poolId);
        return;
    }
    notifyList->poolId = changeList->poolId;
    notifyList->ptNum = 0;
    notifyList->maxCopyNum = changeList->maxCopyNum;
    notifyList->minCopyNum = changeList->minCopyNum;
    notifyList->globalVersion = changeList->globalVersion;

    CM_LOGDEBUG("Update: poolId(%u) global verson(%lu).", changeList->poolId, changeList->globalVersion);
    cm_rwlock_wrlock(&restore->lock);
    ptList->poolId = changeList->poolId;
    ptList->ptNum = changeList->ptNum;
    ptList->maxCopyNum = changeList->maxCopyNum;
    ptList->minCopyNum = changeList->minCopyNum;
    ptList->globalVersion = changeList->globalVersion;
    for (ptId = 0; ptId < ptList->ptNum; ptId++) {
        if (CmClientZkPtEntryIsSame(&ptList->ptEntryList[ptId], &changeList->ptEntryList[ptId]) == FALSE) {
            ptList->ptEntryList[ptId] = changeList->ptEntryList[ptId];
            notifyList->ptEntryList[notifyList->ptNum] = changeList->ptEntryList[ptId];
            notifyList->ptNum++;
        }
    }
    cm_rwlock_unlock(&restore->lock);
    if (notifyList->ptNum != 0) {
        restore->ptChange(notifyList);
    }
    free(notifyList);
    return;
}

static void CmClientZkPtWatchFunc(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    ZkRestoreC *restore = (ZkRestoreC *)watcherCtx;
    int32_t ret;

    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(state);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGDEBUG("evtype(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_CHANGED_EVENT) {
        ret = CmClientZkSubPtListChange(restore->pool->poolId, restore->ptChange);
        if (ret != CM_OK) {
            CM_LOGERROR("Sub pt failed, ret(%d).", ret);
        }
        return;
    }

    return;
}

int32_t CmClientZkSubPtListChange(uint16_t poolId, ptChangeNotifyFp notifyFp)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[poolId];
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_PT_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    CM_LOGDEBUG("First, sub pt change, poolId(%u).", poolId);

    restore->ptChange = notifyFp;

    int32_t len = (int32_t)(sizeof(PtEntry) * restore->pool->maxPtNum + sizeof(PtEntryList));

    PtEntryList *ptList = (PtEntryList *)malloc(len);
    if (ptList == NULL) {
        CM_LOGERROR("Alloc ptList buff failed, poolId(%u).", restore->pool->poolId);
        return CM_ERR;
    }

    ret = CmZkWget(g_zh, zkPath, CmClientZkPtWatchFunc, restore, (char *)ptList, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        free(ptList);
        return CM_ERR;
    }
    CmClientZkUpdatePtList(ptList);
    free(ptList);
    return CM_OK;
}

int32_t CmClientZkRecordDataInfo(uint16_t poolId, const char *key, void *value, uint32_t valLen)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)valLen;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%s", CM_POOL, poolId, CM_DATA_INFO_PATH, key);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkExists(g_zh, zkPath, UNWATCH_ZNODE, NULL);
    if (ret != ZOK) {
        if (ret == ZNONODE) {
            ret = CmZkCreate(g_zh, zkPath, (char *)value, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
            if (ret != ZOK) {
                CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
                return CM_ERR;
            }
            return CM_OK;
        } else {
            CM_LOGERROR("Check znode(%s) exists failed, ret(%d).", zkPath, ret);
            return CM_ERR;
        }
    }

    ret = CmZkSet(g_zh, zkPath, (char *)value, len, UNCHECK_VERSION);
    if (ret != ZOK) {
        CM_LOGERROR("Set znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    return CM_OK;
}

static int32_t CmClientZkSubDataHandle(uint16_t poolId);

static void CmClientZkSubDataChangeWatch(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    ZkRestoreC *restore = (ZkRestoreC *)watcherCtx;
    int32_t ret;

    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(state);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGINFO("evtype(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_CHANGED_EVENT) {
        ret = CmClientZkSubDataHandle(restore->pool->poolId);
        if (ret != CM_OK) {
            CM_LOGERROR("Sub datainfo change failed, ret(%d).", ret);
        }
    }

    return;
}

static int32_t CmClientZkSubDataHandle(uint16_t poolId)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[poolId];
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;
    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%s", CM_POOL, restore->pool->poolId,
        CM_DATA_INFO_PATH, restore->userKey);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    
    int len = USER_DATA_MAX_LEN;
    ret = CmZkWget(g_zh, zkPath, CmClientZkSubDataChangeWatch, restore, (char *)restore->userValue, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    
    restore->userHandle.notifyDataInfoChange(restore->userKey, restore->userValue, len, restore->userHandle.ctx);
    return CM_OK;
}

int32_t CmClientZkSubDataInfoChange(uint16_t poolId, const char *key, void *value, uint32_t valLen,
    DataInfoChangeOpHandle *handle)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[poolId];
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    if (strlen(key) >= USER_DATA_MAX_LEN || valLen > USER_DATA_MAX_LEN) {
        CM_LOGERROR("Invalid, key len(%u) val len(%u).", strlen(key), valLen);
        return CM_ERR;
    }

    ret = memcpy_s(restore->userKey, USER_DATA_MAX_LEN, key, strlen(key) + 1uL);
    if (ret != CM_OK) {
        CM_LOGERROR("Memory copy failed, ret(%d).", ret);
        return CM_ERR;
    }
    restore->userHandle = *handle;

    int32_t len = (int32_t)valLen;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%s", CM_POOL, poolId, CM_DATA_INFO_PATH, restore->userKey);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkCreate(g_zh, zkPath, (char *)value, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != (int32_t)ZOK && ret != (int32_t)ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    ret = CmZkWget(g_zh, zkPath, CmClientZkSubDataChangeWatch, restore, (char *)value, &len, NULL);
    if (ret != (int32_t)ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    handle->notifyDataInfoChange(key, value, len, handle->ctx);
    return CM_OK;
}

static void CmClientZkInitMgr(void)
{
    uint16_t poolId;

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        cm_rwlock_init(&g_cZkMgr.restore[poolId].lock, NULL);
        g_cZkMgr.restore[poolId].used = FALSE;
        g_cZkMgr.restore[poolId].pool = CmConfigGetPoolInfo(poolId);
        g_cZkMgr.restore[poolId].local.sessionId = 0;
        g_cZkMgr.restore[poolId].local.nodeId = NODE_ID_INVALID;
        g_cZkMgr.restore[poolId].local.state = NODE_STATE_INVALID;
        g_cZkMgr.restore[poolId].local.clusterState = NODE_CLUSTER_STATE_INVALID;
        g_cZkMgr.restore[poolId].master.sessionId = 0UL;
        g_cZkMgr.restore[poolId].master.nodeId = NODE_ID_INVALID;
        g_cZkMgr.restore[poolId].master.state = NODE_STATE_INVALID;
        g_cZkMgr.restore[poolId].master.clusterState = NODE_CLUSTER_STATE_INVALID;
        g_cZkMgr.restore[poolId].stateList = NULL;
        g_cZkMgr.restore[poolId].ptList = NULL;
        g_cZkMgr.restore[poolId].nodeChange = NULL;
        g_cZkMgr.restore[poolId].stateChange = NULL;
        g_cZkMgr.restore[poolId].ptChange = NULL;
        g_cZkMgr.restore[poolId].masterChange = NULL;
        g_cZkMgr.restore[poolId].userHandle.notifyDataInfoChange = NULL;
    }
}

void CmClientZkFreeParaList(uint16_t poolId)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[poolId];

    if (restore->stateList != NULL) {
        free(restore->stateList);
        restore->stateList = NULL;
    }

    if (restore->nodeList != NULL) {
        free(restore->nodeList);
        restore->nodeList = NULL;
    }

    if (restore->ptList != NULL) {
        free(restore->ptList);
        restore->ptList = NULL;
    }
}

static int32_t CmClientZkCreatePoolImpl(uint16_t poolId)
{
    ZkRestoreC *restore = &g_cZkMgr.restore[poolId];
    PoolInfo *pool = restore->pool;

    uint16_t index;

    if (pool->maxNodeNum != 0) {
        restore->stateList = (NodeStateList *)malloc(sizeof(NodeStateInfo) * pool->maxNodeNum + sizeof(NodeStateList));
        restore->nodeList = (NodeInfoList *)malloc(sizeof(NodeInfo) * pool->maxNodeNum + sizeof(NodeInfoList));
        if (restore->stateList == NULL || restore->nodeList == NULL) {
            CM_LOGERROR("Alloc stateList buff failed, poolId(%u).", pool->poolId);
            CmClientZkFreeParaList(poolId);
            return CM_ERR;
        }
        restore->stateList->poolId = poolId;
        restore->stateList->masterNodeId = NODE_ID_INVALID;
        restore->stateList->nodeNum = pool->maxNodeNum;
        for (index = 0; index < pool->maxNodeNum; index++) {
            restore->stateList->nodeList[index].sessionId = 0;
            restore->stateList->nodeList[index].nodeId = index;
            restore->stateList->nodeList[index].state = NODE_STATE_INVALID;
            restore->stateList->nodeList[index].clusterState = NODE_CLUSTER_STATE_INVALID;
            restore->stateList->nodeList[index].diskNum = 0;
            size_t dlen = sizeof(NodeDiskState) * MAX_DISK_NUM;
            memset_s(restore->stateList->nodeList[index].diskList, dlen, 0, dlen);
        }
    }
    if (pool->maxPtNum != 0) {
        restore->ptList = (PtEntryList *)malloc(sizeof(PtEntry) * pool->maxPtNum + sizeof(PtEntryList));
        if (restore->ptList == NULL) {
            CM_LOGERROR("Alloc ptList buff failed, poolId(%u).", pool->poolId);
            CmClientZkFreeParaList(poolId);
            return CM_ERR;
        }
        restore->ptList->poolId = poolId;
        restore->ptList->ptNum = pool->maxPtNum;
        for (index = 0; index < pool->maxPtNum; index++) {
            restore->ptList->ptEntryList[index].ptId = index;
            restore->ptList->ptEntryList[index].state = PT_STATE_INIT;
        }
    }

    g_cZkMgr.restore[poolId].used = TRUE;
    return CM_OK;
}

static int32_t CmClientZkCreatePool(void)
{
    uint16_t poolId;
    int32_t ret;

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        PoolInfo *pool = g_cZkMgr.restore[poolId].pool;
        if (pool == NULL) {
            continue;
        }

        ret = CmClientZkCreatePoolImpl(poolId);
        if (ret != CM_OK) {
            CM_LOGERROR("Create pool failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
    }

    return CM_OK;
}

int CmClientZkInit(void)
{
    int32_t ret;

    CmClientZkInitMgr();

    ret = CmClientZkCreatePool();
    if (ret != CM_OK) {
        return ret;
    }

    CM_LOGINFO("Cm client zkadapter init succeed.");
    return CM_OK;
}
#endif
#if DESC("zk server")
typedef struct {
    PoolInfo *pool;
    ZkNotifyNodeListFp nodeChange;
    uint16_t nodeIdle;
    uint16_t ptIdle;
    ZkNotifyNodeEventFp nodeEvent;
    ZkNotifyPtEventFp ptEvent;
    ZkCommitPtEventFp ptCommit;
} ZkRestoreS;

typedef struct {
    CmServerRole role;
    uint16_t localId;
    uint16_t masterId;
    ZkRestoreS restore[MAX_POOL_NUM];
    ZkNotifyMetaRoleFp roleChange;
} CmServerZkMgr;

static CmServerZkMgr g_sZkMgr;

static void CmServerZkInitMgr(void)
{
    g_sZkMgr.role = CM_SERVER_SLAVE;

    g_sZkMgr.localId = NODE_ID_INVALID;
    g_sZkMgr.masterId = NODE_ID_INVALID;

    uint16_t poolId;
    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        g_sZkMgr.restore[poolId].pool = NULL;
        g_sZkMgr.restore[poolId].nodeChange = NULL;
        g_sZkMgr.restore[poolId].nodeIdle = TRUE;
        g_sZkMgr.restore[poolId].ptIdle = TRUE;
        g_sZkMgr.restore[poolId].nodeEvent = NULL;
        g_sZkMgr.restore[poolId].ptEvent = NULL;
        g_sZkMgr.restore[poolId].ptCommit = NULL;
    }

    g_sZkMgr.roleChange = NULL;
}

static int32_t CmServerZkRecordMetaNodeId(const char *ipv4AddrStr)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%s/%s", CM_META, CM_IP_PATH, ipv4AddrStr);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkCreate(g_zh, zkPath, (char *)&g_sZkMgr.localId, (int)sizeof(uint16_t), &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

static int32_t CmServerZkAtoi(const char *str)
{
    int32_t num = 0;
    while (str != NULL && str[0] >= '0' && str[0] <= '9') {
        num = num * CM_ZK_RADIX_10 + (str[0] - '0');
        str++;
    }
    return num;
}

static uint16_t CmServerZkGetNodeIdByPath(const char *path, const char *pre)
{
    size_t step = strlen(pre);
    const char *str = &path[step];

    return (uint16_t)CmServerZkAtoi(str);
}

static int32_t CmServerZkGenMetaNodeIdV2(const char *ipv4AddrStr)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    uint16_t nodeId;
    int32_t retLen = (int32_t)sizeof(uint16_t);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%s/%s", CM_META, CM_IP_PATH, ipv4AddrStr);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)&nodeId, &retLen, NULL);
    if (ret == ZOK) {
        CM_LOGINFO("Recover ip(%s) matched nodeId(%u).", ipv4AddrStr, nodeId);
        g_sZkMgr.localId = nodeId;
        return CM_OK;
    } else if (ret == ZNONODE) {
        // 第一次启动：通过ZK生成全局唯一nodeid
        char zkPathT[CM_ZNODE_PATH_LEN] = {0};
        char zkPathB[CM_ZNODE_PATH_LEN] = {0};
        int32_t BLen = CM_ZNODE_PATH_LEN;
        ret = sprintf_s(zkPathT, CM_ZNODE_PATH_LEN, "%s/%s", CM_META, CM_NODEID_GENERATE_CHILD_PATH);
        if (ret < 0) {
            CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
            return CM_ERR;
        }
        ret = CmZkCreate(g_zh, zkPathT, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL | ZOO_SEQUENCE, zkPathB, BLen);
        if (ret != ZOK) {
            CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPathT, ret);
            return CM_ERR;
        }
        g_sZkMgr.localId = CmServerZkGetNodeIdByPath(zkPathB, zkPathT);
        ret = CmZkDelete(g_zh, zkPathB, UNCHECK_VERSION);
        if (ret != (int32_t)CM_OK) {
            CM_LOGERROR("Delete znode(%s) failed, ret(%d).", zkPathB, ret);
            return CM_ERR;
        }
        return CmServerZkRecordMetaNodeId(ipv4AddrStr);
    } else {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
}

static int32_t CmServerZkGenMetaNodeId(const char *ipv4AddrStr)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    uint16_t cfgNodeId = CmConfigGetNodeId();
    if (cfgNodeId == NODE_ID_INVALID) {
        return CmServerZkGenMetaNodeIdV2(ipv4AddrStr);
    }

    uint16_t retNodeId;
    int32_t retLen = (int32_t)sizeof(uint16_t);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%s/%s", CM_META, CM_IP_PATH, ipv4AddrStr);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)&retNodeId, &retLen, NULL);
    if (ret == ZOK) {
        if (retNodeId == cfgNodeId) {
            CM_LOGINFO("Recover ip(%s) matched nodeId(%u).", ipv4AddrStr, cfgNodeId);
            g_sZkMgr.localId = cfgNodeId;
            return CM_OK;
        } else {
            CM_LOGERROR("Recover ip(%s) not matched nodeId(%u).", ipv4AddrStr, cfgNodeId);
            return CM_ERR;
        }
    } else if (ret == ZNONODE) {
        g_sZkMgr.localId = cfgNodeId;
        return CmServerZkRecordMetaNodeId(ipv4AddrStr);
    } else {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
}

static int32_t CmServerZkCheckMetaNodeExist(void)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%s/%u", CM_META, CM_NODE_PATH, g_sZkMgr.localId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    uint16_t cnt = 0;
    do {
        ret = CmZkExists(g_zh, zkPath, UNWATCH_ZNODE, NULL);
        if (ret == ZNONODE) {
            CM_LOGINFO("Check local znode(%s) not exist.", zkPath);
            return CM_OK;
        }
        CM_LOGWARN("Local znode exist, waiting, retry(%u ms).", cnt);
        CmSleep(CM_ZK_TRY_INTERAL);
        cnt += CM_ZK_TRY_INTERAL;
    } while (cnt < CM_ZK_NODE_DETECT_TIME);

    CM_LOGERROR("Local znode exist, not wait, retry(%u ms).", cnt);
    return CM_ERR;
}

int32_t CmServerZkRegisterMetaNode(const char *ipv4AddrStr)
{
    int32_t ret;

    ret = CmServerZkGenMetaNodeId(ipv4AddrStr);
    if (ret != CM_OK) {
        CM_LOGERROR("Gen nodeId failed, local ip(%s) ret(%d).", ipv4AddrStr, ret);
        return ret;
    }

    ret = CmServerZkCheckMetaNodeExist();
    if (ret != CM_OK) {
        CM_LOGERROR("Check local node failed, local ip(%s) ret(%d).", ipv4AddrStr, ret);
        return ret;
    }

    return CM_OK;
}

int32_t CmServerZkRecordMetaSession(void)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%s/%u", CM_META, CM_NODE_PATH, g_sZkMgr.localId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkCreate(g_zh, zkPath, (char *)&g_sZkMgr.localId, (int)sizeof(uint16_t), &ZOO_OPEN_ACL_UNSAFE,
        ZOO_EPHEMERAL, NULL, 0);
    if (ret != ZOK) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmServerZkRecordPtEntryList(PtEntryList *ptEntryList)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    CM_LOGDEBUG("Record: poolId(%u) global version(%lu).", ptEntryList->poolId, ptEntryList->globalVersion);

    int32_t len = (int32_t)(sizeof(PtEntryList) + sizeof(PtEntry) * ptEntryList->ptNum);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, ptEntryList->poolId, CM_PT_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkSet(g_zh, zkPath, (char *)ptEntryList, len, UNCHECK_VERSION);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Set znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmServerZkRecordNodeList(NodeInfoList *nodeList)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeInfoList) + sizeof(NodeInfo) * nodeList->nodeNum);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, nodeList->poolId, CM_NODE_LIST_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkSet(g_zh, zkPath, (char *)nodeList, len, UNCHECK_VERSION);
    if (ret != ZOK) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmServerZkRecordStateList(NodeStateList *stateList)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo) * stateList->nodeNum);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, stateList->poolId, CM_STATE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkSet(g_zh, zkPath, (char *)stateList, len, UNCHECK_VERSION);
    if (ret != ZOK) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmServerZkGetNodeList(NodeInfoList *nodeList)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeInfoList) + sizeof(NodeInfo) * nodeList->nodeNum);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, nodeList->poolId, CM_NODE_LIST_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)nodeList, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmServerZkGetStateList(NodeStateList *stateList)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo) * stateList->nodeNum);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, stateList->poolId, CM_STATE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)stateList, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmServerZkGetPtEntryList(PtEntryList *ptEntryList)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)(sizeof(PtEntryList) + sizeof(PtEntry) * ptEntryList->ptNum);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, ptEntryList->poolId, CM_PT_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)ptEntryList, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmServerZkGetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t retLen = (int32_t)sizeof(NodeInfo);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_NODE_INFO_PATH, nodeInfo->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)nodeInfo, &retLen, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

static void CmServerZkRoleWatchFunc(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    int32_t ret;

    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(state);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGINFO("Event type(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_DELETED_EVENT) {
        CM_LOGWARN("Master discontact, retry select role.");
        ret = CmServerZkSubRoleChange(g_sZkMgr.roleChange);
        if (ret != CM_OK) {
            CM_LOGERROR("Select role failed, ret(%d).", ret);
        }
        return;
    }
    return;
}

int32_t CmServerZkSubRoleChange(ZkNotifyMetaRoleFp notifyFp)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%s", CM_META, CM_MASTER_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    ret = CmZkCreate(g_zh, zkPath, (char *)&g_sZkMgr.localId, sizeof(uint16_t), &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
        NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    if (g_sZkMgr.roleChange == NULL) {
        CM_LOGINFO("First, sub role change.");
        g_sZkMgr.roleChange = notifyFp;
    }

    int32_t len = (int32_t)sizeof(uint16_t);

    ret = CmZkWget(g_zh, zkPath, CmServerZkRoleWatchFunc, NULL, (char *)&g_sZkMgr.masterId, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    if (g_sZkMgr.localId == g_sZkMgr.masterId) {
        CM_LOGWARN("Role select, local is master, master nodeId(%u).", g_sZkMgr.masterId);
        g_sZkMgr.role = CM_SERVER_MASTER;
    } else {
        CM_LOGWARN("Role select, local is slave, master nodeId(%u).", g_sZkMgr.masterId);
        g_sZkMgr.role = CM_SERVER_SLAVE;
    }

    notifyFp(g_sZkMgr.role); // 本地角色变更通知
    return CM_OK;
}

static void *CmServerZkSubNodeListHandle(void *ctx)
{
    CmNodeIdList *nodeList = (CmNodeIdList *)ctx;

    if (g_sZkMgr.restore[nodeList->poolId].nodeChange != NULL) {
        g_sZkMgr.restore[nodeList->poolId].nodeChange(nodeList);
    }

    free(nodeList);
    return NULL;
}

static void CmServerZkSubNodeListWatch(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    ZkRestoreS *restore = (ZkRestoreS *)watcherCtx;
    PoolInfo *pool = restore->pool;
    int32_t ret;

    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(state);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGINFO("evtype(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_CHILD_EVENT) {
        ret = CmServerZkSubNodeListChange(pool->poolId, restore->nodeChange);
        if (ret != CM_OK) {
            CM_LOGERROR("Sub node change failed, ret(%d).", ret);
        }
        return;
    }

    return;
}

int32_t CmServerZkSubNodeListChange(uint16_t poolId, ZkNotifyNodeListFp notifyFp)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    if (g_sZkMgr.role != CM_SERVER_MASTER) {
        return CM_OK;
    }

    if (g_sZkMgr.restore[poolId].nodeChange == NULL) {
        CM_LOGINFO("First, sub node change, poolId(%u).", poolId);
        g_sZkMgr.restore[poolId].nodeChange = notifyFp;
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_NODE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    struct String_vector retStrings = { 0 };
    ret = CmZkWgetChildren(g_zh, zkPath, CmServerZkSubNodeListWatch, (void *)&g_sZkMgr.restore[poolId], &retStrings);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    CmNodeIdList *nodeList =
        (CmNodeIdList *)malloc(sizeof(CmNodeIdList) + sizeof(uint16_t) * (uint16_t)retStrings.count);
    if (nodeList == NULL) {
        CM_LOGERROR("Malloc nodeList buff failed, count(%d).", retStrings.count);
        return CM_ERR;
    }
    nodeList->poolId = poolId;
    nodeList->nodeNum = (uint16_t)retStrings.count;
    uint16_t index;
    for (index = 0; index < nodeList->nodeNum; index++) {
        nodeList->nodeList[index] = (uint16_t)CmServerZkAtoi(retStrings.data[index]);
    }
    CmServerSchedueAdd(poolId, CmServerZkSubNodeListHandle, (void *)nodeList);
    DeallocateStringVector(&retStrings);
    return CM_OK;
}

static int32_t CmServerZkGetNodeEvent(CmNodeEvent *event)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = (int32_t)sizeof(CmNodeEvent);

    ret =
        sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, event->poolId, CM_NODE_EVENT_PATH, event->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)event, &len, NULL);
    if (ret != ZOK && ret != ZNONODE) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    if (ret == ZNONODE) {
        CM_LOGINFO("Get znode(%s), not exist.", zkPath);
        return CM_NOT_EXIST;
    }
    return CM_OK;
}

static int32_t CmServerZkDeleteNodeEvent(uint16_t poolId, uint16_t nodeId)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_NODE_EVENT_PATH, nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkDelete(g_zh, zkPath, UNCHECK_VERSION);
    if (ret != ZOK && ret != ZNONODE) {
        CM_LOGERROR("Delete znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

static void *CmServerZkSubNodeEventHandle(void *ctx)
{
    CmNodeIdList *nodeList = (CmNodeIdList *)ctx;
    CmNodeEvent event;

    uint16_t index;
    int32_t ret;
    for (index = 0; index < nodeList->nodeNum; index++) {
        event.poolId = nodeList->poolId;
        event.nodeId = nodeList->nodeList[index];
        ret = CmServerZkGetNodeEvent(&event);
        if (ret != CM_OK) {
            CM_LOGERROR("Get node event failed, ret(%d) nodeId(%u) poolId(%u).", ret, event.nodeId, event.poolId);
            free(nodeList);
            return NULL;
        }
        ret = g_sZkMgr.restore[nodeList->poolId].nodeEvent(&event);
        if (ret != CM_OK) {
            CM_LOGERROR("Handle node event failed, ret(%d) nodeId(%u) poolId(%u).", ret, event.nodeId, event.poolId);
            free(nodeList);
            return NULL;
        }
        ret = CmServerZkDeleteNodeEvent(event.poolId, event.nodeId);
        if (ret != CM_OK) {
            CM_LOGERROR("Delete node event failed, ret(%d) nodeId(%u) poolId(%u).", ret, event.nodeId, event.poolId);
            free(nodeList);
            return NULL;
        }
    }

    g_sZkMgr.restore[nodeList->poolId].nodeIdle = TRUE;

    ret = CmServerZkSubNodeEvent(nodeList->poolId, g_sZkMgr.restore[nodeList->poolId].nodeEvent);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub node event failed, ret(%d) poolId(%u).", ret, nodeList->poolId);
        free(nodeList);
        return NULL;
    }

    free(nodeList);
    return NULL;
}

static void CmServerZkSubNodeEventWatch(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    ZkRestoreS *restore = (ZkRestoreS *)watcherCtx;
    PoolInfo *pool = restore->pool;
    int32_t ret;

    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(state);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGINFO("evtype(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_CHILD_EVENT) {
        if (restore->nodeIdle == FALSE) {
            return;
        }
        ret = CmServerZkSubNodeEvent(pool->poolId, restore->nodeEvent);
        if (ret != CM_OK) {
            CM_LOGERROR("Sub node event failed, ret(%d).", ret);
        }
        return;
    }

    return;
}

int32_t CmServerZkSubNodeEvent(uint16_t poolId, ZkNotifyNodeEventFp notifyFp)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    if (g_sZkMgr.role != CM_SERVER_MASTER) {
        return CM_OK;
    }

    if (g_sZkMgr.restore[poolId].nodeEvent == NULL) {
        CM_LOGINFO("First, sub node event, poolId(%u).", poolId);
        g_sZkMgr.restore[poolId].nodeEvent = notifyFp;
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_NODE_EVENT_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    struct String_vector retStrings = { 0 };
    ret = CmZkWgetChildren(g_zh, zkPath, CmServerZkSubNodeEventWatch, (void *)&g_sZkMgr.restore[poolId], &retStrings);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    if (retStrings.count == 0) {
        DeallocateStringVector(&retStrings);
        return CM_OK;
    } else {
        g_sZkMgr.restore[poolId].nodeIdle = FALSE;
    }

    CmNodeIdList *nodeList =
        (CmNodeIdList *)malloc(sizeof(CmNodeIdList) + sizeof(uint16_t) * (uint16_t)retStrings.count);
    if (nodeList == NULL) {
        CM_LOGERROR("Malloc nodeList buff failed, count(%d).", retStrings.count);
        return CM_ERR;
    }
    nodeList->poolId = poolId;
    nodeList->nodeNum = (uint16_t)retStrings.count;
    uint16_t index;
    for (index = 0; index < nodeList->nodeNum; index++) {
        nodeList->nodeList[index] = (uint16_t)CmServerZkAtoi(retStrings.data[index]);
    }
    CmServerSchedueAdd(poolId, CmServerZkSubNodeEventHandle, (void *)nodeList);
    DeallocateStringVector(&retStrings);
    return CM_OK;
}

static int32_t CmServerZkGetPtEvent(CmPtEvent *event)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    int32_t len = NODE_META_BUFF_LEN;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, event->poolId, CM_PT_EVENT_PATH, event->nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)event, &len, NULL);
    if (ret != ZOK && ret != ZNONODE) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    if (ret == ZNONODE) {
        CM_LOGINFO("Get znode(%s), not exist.", zkPath);
        return CM_NOT_EXIST;
    }
    return CM_OK;
}

static int32_t CmServerZkDeletePtEvent(uint16_t poolId, uint16_t nodeId)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s/%u", CM_POOL, poolId, CM_PT_EVENT_PATH, nodeId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkDelete(g_zh, zkPath, UNCHECK_VERSION);
    if (ret != ZOK && ret != ZNONODE) {
        CM_LOGERROR("Delete znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CM_OK;
}

static void *CmServerZkSubPtEventHandle(void *ctx)
{
    CmNodeIdList *nodeList = (CmNodeIdList *)ctx;

    CmPtEvent *event = (CmPtEvent *)malloc(NODE_META_BUFF_LEN);
    if (event == NULL) {
        CM_LOGERROR("Malloc ptEvent buff failed, poolId(%u).", nodeList->poolId);
        free(nodeList);
        return NULL;
    }

    uint16_t index;
    int32_t ret;
    for (index = 0; index < nodeList->nodeNum; index++) {
        event->poolId = nodeList->poolId;
        event->nodeId = nodeList->nodeList[index];
        ret = CmServerZkGetPtEvent(event);
        if (ret != CM_OK) {
            CM_LOGERROR("Get pt event failed, ret(%d) poolId(%u).", ret, nodeList->poolId);
            free(event);
            free(nodeList);
            return NULL;
        }
        g_sZkMgr.restore[nodeList->poolId].ptEvent(event);
    }

    ret = g_sZkMgr.restore[nodeList->poolId].ptCommit(nodeList->poolId);
    if (ret != CM_OK) {
        CM_LOGERROR("Batch commit pt event failed, ret(%d) poolId(%u).", ret, nodeList->poolId);
        free(event);
        free(nodeList);
        return NULL;
    }

    free(event);

    for (index = 0; index < nodeList->nodeNum; index++) {
        ret = CmServerZkDeletePtEvent(nodeList->poolId, nodeList->nodeList[index]);
        if (ret != CM_OK) {
            CM_LOGERROR("Delete pt event failed, ret(%d) poolId(%u).", ret, nodeList->poolId);
            free(nodeList);
            return NULL;
        }
    }

    g_sZkMgr.restore[nodeList->poolId].ptIdle = TRUE;

    ret = CmServerZkSubPtEvent(nodeList->poolId, g_sZkMgr.restore[nodeList->poolId].ptEvent,
        g_sZkMgr.restore[nodeList->poolId].ptCommit);
    if (ret != CM_OK) {
        CM_LOGERROR("Sub pt event failed, ret(%d) poolId(%u).", ret, nodeList->poolId);
        free(nodeList);
        return NULL;
    }

    free(nodeList);
    return NULL;
}

static void CmServerZkSubPtEventWatch(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    ZkRestoreS *restore = (ZkRestoreS *)watcherCtx;
    PoolInfo *pool = restore->pool;
    int32_t ret;

    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(state);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGINFO("evtype(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_CHILD_EVENT) {
        if (restore->ptIdle == FALSE) {
            return;
        }
        ret = CmServerZkSubPtEvent(pool->poolId, restore->ptEvent, restore->ptCommit);
        if (ret != CM_OK) {
            CM_LOGERROR("Sub pt event failed, ret(%d).", ret);
        }
        return;
    }

    return;
}

int32_t CmServerZkSubPtEvent(uint16_t poolId, ZkNotifyPtEventFp notifyFp, ZkCommitPtEventFp commitFp)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    if (g_sZkMgr.role != CM_SERVER_MASTER) {
        return CM_OK;
    }

    if (g_sZkMgr.restore[poolId].pool->redundanceNum == 0) {
        return CM_OK;
    }

    if (g_sZkMgr.restore[poolId].ptEvent == NULL) {
        CM_LOGINFO("First, sub pt event, poolId(%u).", poolId);
        g_sZkMgr.restore[poolId].ptEvent = notifyFp;
        g_sZkMgr.restore[poolId].ptCommit = commitFp;
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_PT_EVENT_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }

    struct String_vector retStrings = { 0 };
    ret = CmZkWgetChildren(g_zh, zkPath, CmServerZkSubPtEventWatch, (void *)&g_sZkMgr.restore[poolId], &retStrings);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    if (retStrings.count == 0) {
        DeallocateStringVector(&retStrings);
        return CM_OK;
    } else {
        g_sZkMgr.restore[poolId].ptIdle = FALSE;
    }

    CmNodeIdList *nodeList =
        (CmNodeIdList *)malloc(sizeof(CmNodeIdList) + sizeof(uint16_t) * (uint16_t)retStrings.count);
    if (nodeList == NULL) {
        CM_LOGERROR("Malloc nodeList buff failed, count(%d).", retStrings.count);
        return CM_ERR;
    }
    nodeList->poolId = poolId;
    nodeList->nodeNum = (uint16_t)retStrings.count;
    uint16_t index;
    for (index = 0; index < nodeList->nodeNum; index++) {
        nodeList->nodeList[index] = (uint16_t)CmServerZkAtoi(retStrings.data[index]);
    }
    CmServerSchedueAdd(poolId, CmServerZkSubPtEventHandle, (void *)nodeList);
    DeallocateStringVector(&retStrings);
    return CM_OK;
}

static int32_t CmServerZkCreatePool(void)
{
    uint16_t poolId;

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        PoolInfo *pool = CmConfigGetPoolInfo(poolId);
        if (pool == NULL) {
            continue;
        }
        g_sZkMgr.restore[poolId].pool = pool;
    }

    return CM_OK;
}

int CmServerZkInit(void)
{
    int32_t ret;

    CmServerZkInitMgr();

    const char *ipv4Addr = CmConfigGetIpv4AddrStr();
    ret = CmServerZkRegisterMetaNode(ipv4Addr);
    if (ret != CM_OK) {
        CM_LOGERROR("Register meta node failed, ret(%d) ip(%s).", ret, ipv4Addr);
        return ret;
    }

    ret = CmServerZkRecordMetaSession();
    if (ret != CM_OK) {
        CM_LOGERROR("Record meta session failed, ret(%d) nid(%u).", ret, g_sZkMgr.localId);
        return ret;
    }

    ret = CmServerZkCreatePool();
    if (ret != CM_OK) {
        return ret;
    }

    CM_LOGINFO("Cm server zkadapter init succeed.");
    return CM_OK;
}
#endif
#if DESC("zk base handle")
#define CM_ZK_MAX_TRY_NUM (3)

int CmZkCreate(zhandle_t *zh, const char *path, const char *value, int valuelen, const struct ACL_vector *acl, int mode,
    char *pathBuffer, int pathBufferLen)
{
    int ret;

    uint16_t cnt = 0;
    do {
        ret = ZooCreate(zh, path, value, valuelen, acl, mode, pathBuffer, pathBufferLen);
        if ((ret != ZOK) && (ret != ZNODEEXISTS) && (ret != ZSESSIONEXPIRED)) {
            CM_LOGWARN("Create znode(%s) failed, ret(%d).", path, ret);
        }
        cnt++;
    } while ((ret != ZOK) && (ret != ZNODEEXISTS) && (ret != ZSESSIONEXPIRED) && (cnt < CM_ZK_MAX_TRY_NUM));

    return ret;
}

int CmZkDelete(zhandle_t *zh, const char *path, int version)
{
    int ret;

    uint16_t cnt = 0;
    do {
        ret = ZooDelete(zh, path, version);
        if ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED)) {
            CM_LOGWARN("Delete znode(%s) failed, ret(%d).", path, ret);
        }
        cnt++;
    } while ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED) && (cnt < CM_ZK_MAX_TRY_NUM));

    return ret;
}

int CmZkGet(zhandle_t *zh, const char *path, int watch, char *buffer, int *bufferLen, struct Stat *stat)
{
    int ret;

    uint16_t cnt = 0;
    do {
        ret = ZooGet(zh, path, watch, buffer, bufferLen, stat);
        if ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED)) {
            CM_LOGWARN("Get znode(%s) failed, ret(%d).", path, ret);
        }
        cnt++;
    } while ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED) && (cnt < CM_ZK_MAX_TRY_NUM));

    return ret;
}

int CmZkExists(zhandle_t *zh, const char *path, int watch, struct Stat *stat)
{
    int ret;

    uint16_t cnt = 0;
    do {
        ret = ZooExists(zh, path, watch, stat);
        if ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED)) {
            CM_LOGWARN("Exists znode(%s) failed, ret(%d).", path, ret);
        }
        cnt++;
    } while ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED) && (cnt < CM_ZK_MAX_TRY_NUM));

    return ret;
}

int CmZkSet(zhandle_t *zh, const char *path, const char *buffer, int buflen, int version)
{
    int ret;

    uint16_t cnt = 0;
    do {
        ret = ZooSet(zh, path, buffer, buflen, version);
        if ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED)) {
            CM_LOGWARN("Exists znode(%s) failed, ret(%d).", path, ret);
        }
        cnt++;
    } while ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED) && (cnt < CM_ZK_MAX_TRY_NUM));

    return ret;
}

int CmZkWget(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx, char *buffer, int *bufferLen,
    struct Stat *stat)
{
    int ret;

    uint16_t cnt = 0;
    do {
        ret = ZooWget(zh, path, watcher, watcherCtx, buffer, bufferLen, stat);
        if ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED)) {
            CM_LOGWARN("Wget znode(%s) failed, ret(%d).", path, ret);
        }
        cnt++;
    } while ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED) && (cnt < CM_ZK_MAX_TRY_NUM));

    return ret;
}

int CmZkWgetChildren(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx,
    struct String_vector *strings)
{
    int ret;

    uint16_t cnt = 0;
    do {
        ret = ZooWgetChildren(zh, path, watcher, watcherCtx, strings);
        if ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED)) {
            CM_LOGWARN("Wget children znode(%s) failed, ret(%d).", path, ret);
        }
        cnt++;
    } while ((ret != ZOK) && (ret != ZNONODE) && (ret != ZSESSIONEXPIRED) && (cnt < CM_ZK_MAX_TRY_NUM));

    return ret;
}
#endif
#if DESC("zk initial")
static int32_t CmZkConnect(void);

static void CmZkClientRestore(void)
{
    int32_t ret;

    for (uint16_t poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        if (g_cZkMgr.restore[poolId].used == FALSE) {
            continue;
        }
        if (g_cZkMgr.restore[poolId].local.nodeId != NODE_ID_INVALID) {
            ret = CmClientZkRecordLocalSession(poolId);
            if (ret != CM_OK) {
                continue;
            }
        }
        if (g_cZkMgr.restore[poolId].stateChange != NULL) {
            ret = CmClientZkSubStateListChange(poolId, g_cZkMgr.restore[poolId].stateChange);
            if (ret != CM_OK) {
                continue;
            }
        }
        if (g_cZkMgr.restore[poolId].ptChange != NULL) {
            ret = CmClientZkSubPtListChange(poolId, g_cZkMgr.restore[poolId].ptChange);
            if (ret != CM_OK) {
                continue;
            }
        }
        if (g_cZkMgr.restore[poolId].userHandle.notifyDataInfoChange != NULL) {
            ret = CmClientZkSubDataHandle(poolId);
            if (ret != CM_OK) {
                continue;
            }
        }
    }
}

static void CmZkServerRestore(void)
{
    int32_t ret;

    ret = CmServerZkRecordMetaSession();
    if (ret != CM_OK) {
        CM_LOGWARN("Record meta session failed, ret(%d) nid(%u).", ret, g_sZkMgr.localId);
    }
    
    ret = CmServerZkSubRoleChange(g_sZkMgr.roleChange); // 恢复订阅
    if (ret != CM_OK) {
        return;
    }
}

static void CmZkRestore(void)
{
    int32_t ret;

    do {
        ret = ZookeeperClose(g_zh);
        if (ret != CM_OK) {
            CM_LOGWARN("Zookeeper close failed, ret(%d).", ret);
        }
        ret = CmZkConnect();
    } while (ret != CM_OK);

    if (CmConfigHasCfgPoolC() == TRUE) {
        CmZkClientRestore();
    }

    if (CmConfigHasCfgPoolS() == TRUE) {
        CmZkServerRestore();
    }
    return;
}

static void CmZkWatchFunc(zhandle_t *zh, int evtype, int state, const char *path, void *watcherCtx)
{
    UNREFERENCE_PARAM(zh);
    UNREFERENCE_PARAM(path);
    UNREFERENCE_PARAM(watcherCtx);

    CM_LOGINFO("evtype(%d) state(%d) path(%s).", evtype, state, path);

    if (evtype == ZOO_SESSION_EVENT && state == ZOO_EXPIRED_SESSION_STATE) {
        if (CmConfigHasCfgPoolS() == TRUE && g_sZkMgr.role == CM_SERVER_MASTER) {
            CM_LOGWARN("Local role, master to slave.");
            g_sZkMgr.role = CM_SERVER_SLAVE;
            if (g_sZkMgr.roleChange != NULL) {
                g_sZkMgr.roleChange(CM_SERVER_SLAVE);
            }
        }
        CmZkRestore();
        return;
    }
    return;
}

static int32_t CmZkConnect(void)
{
    const char *zkServerIp = CmConfigGetZkServerList();
    int timeOut = (int)CmConfigGetTimeOut();

    ZooSetDebugLevel(ZOO_LOG_LEVEL_ERROR);
    g_zh = ZookeeperInit(zkServerIp, CmZkWatchFunc, timeOut, NULL, NULL, 0);
    if (g_zh == NULL) {
        CM_LOGERROR("Connect zookeeper failed, zkserver(%s).", zkServerIp);
        return CM_ERR;
    }

    uint16_t cnt = 0;
    while (ZooState(g_zh) != ZOO_CONNECTED_STATE) {
        CM_LOGWARN("Waiting for zookeeper connected, retry(%u ms).", cnt);
        if (cnt >= CM_ZK_TRY_CONNECT_TIME) {
            CM_LOGERROR("Connect zookeeper failed, zkserver(%s).", zkServerIp);
            return CM_ERR;
        }
        CmSleep(CM_ZK_TRY_INTERAL);
        cnt += CM_ZK_TRY_INTERAL;
    }

    int32_t timeout = ZooRecvTimeout(g_zh);
    CM_LOGINFO("Connect zookeeper succeed, zkserver(%s) timeout(%d ms).", zkServerIp, timeout);
    return CM_OK;
}

static int32_t CmZkCreateMetaDir(void)
{
    char path[CM_ZNODE_PATH_LEN] = {0};
    int32_t ret;

    ret = CmZkCreate(g_zh, CM_ROOT, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed(%d).", CM_ROOT, ret);
        return CM_ERR;
    }

    ret = CmZkCreate(g_zh, CM_META, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed(%d).", CM_META, ret);
        return CM_ERR;
    }
    ret = sprintf_s(path, CM_ZNODE_PATH_LEN, "%s/%s", CM_META, CM_NODE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, path, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", path, ret);
        return CM_ERR;
    }
    ret = sprintf_s(path, CM_ZNODE_PATH_LEN, "%s/%s", CM_META, CM_IP_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, path, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", path, ret);
        return CM_ERR;
    }
    ret = sprintf_s(path, CM_ZNODE_PATH_LEN, "%s/%s", CM_META, CM_NODEID_GENERATE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, path, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", path, ret);
        return CM_ERR;
    }
    return CM_OK;
}

int32_t CmZkComparePoolDir(PoolInfo *pool, PoolInfo *record)
{
    if (strncmp(pool->poolName, record->poolName, POOL_NAME_LEN) != 0) {
        CM_LOGERROR("Cfg poolName conflict, input(%s) record(%s).", pool->poolName, record->poolName);
        return CM_ERR;
    }
    if (pool->poolId != record->poolId) {
        CM_LOGERROR("Cfg poolId conflict, input(%u) record(%u).", pool->poolId, record->poolId);
        return CM_ERR;
    }
    if (pool->diskType != record->diskType) {
        CM_LOGERROR("Cfg diskType conflict, input(%u) record(%u).", pool->diskType, record->diskType);
        return CM_ERR;
    }
    if (pool->diskNum != record->diskNum) {
        CM_LOGERROR("Cfg diskNum conflict, input(%u) record(%u).", pool->diskNum, record->diskNum);
        return CM_ERR;
    }
    if (pool->redundanceNum != record->redundanceNum) {
        CM_LOGERROR("Cfg redundance conflict, input(%u) record(%u).", pool->redundanceNum, record->redundanceNum);
        return CM_ERR;
    }
    if (pool->initialNodeNum != record->initialNodeNum) {
        CM_LOGERROR("Cfg initialNodeNum conflict, input(%u) record(%u).", pool->initialNodeNum, record->initialNodeNum);
        return CM_ERR;
    }
    if (pool->maxNodeNum != record->maxNodeNum) {
        CM_LOGERROR("Cfg nodeNum conflict, input(%u) record(%u).", pool->maxNodeNum, record->maxNodeNum);
        return CM_ERR;
    }
    if (pool->maxPtNum != record->maxPtNum) {
        CM_LOGERROR("Cfg ptNum conflict, input(%u) record(%u).", pool->maxPtNum, record->maxPtNum);
        return CM_ERR;
    }
    return CM_OK;
}

static int32_t CmZkCheckPoolDir(PoolInfo *pool)
{
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int32_t ret;

    ret = CmZkCreate(g_zh, CM_POOL, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", CM_POOL, ret);
        return CM_ERR;
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u", CM_POOL, pool->poolId);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, (char *)pool, (int)sizeof(PoolInfo), &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    PoolInfo record;
    int32_t len = (int32_t)sizeof(PoolInfo);
    ret = CmZkGet(g_zh, zkPath, UNWATCH_ZNODE, (char *)&record, &len, NULL);
    if (ret != ZOK) {
        CM_LOGERROR("Get znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CmZkComparePoolDir(pool, &record);
}

static int32_t CmZkCreatePoolDir4(PoolInfo *pool)
{
    char zkPath[CM_ZNODE_PATH_LEN] = {0};
    int32_t ret;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, pool->poolId, CM_DATA_INFO_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    if (pool->redundanceNum == 0) {
        return CM_OK;
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, pool->poolId, CM_PT_EVENT_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    PtEntryList *ptEntryList = (PtEntryList *)malloc(sizeof(PtEntryList));
    if (ptEntryList == NULL) {
        return CM_ERR;
    }
    ptEntryList->poolId = pool->poolId;
    ptEntryList->ptNum = 0;
    ptEntryList->globalVersion = 0;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, pool->poolId, CM_PT_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        free(ptEntryList);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, (char *)ptEntryList, (int)sizeof(PtEntryList), &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        free(ptEntryList);
        return CM_ERR;
    }
    free(ptEntryList);
    return CM_OK;
}

static int32_t CmZkCreatePoolDir3(PoolInfo *pool)
{
    char zkPath[CM_ZNODE_PATH_LEN] = {0};
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeInfoList) + sizeof(NodeInfo) * pool->maxNodeNum);
    NodeInfoList *nodeList = (NodeInfoList *)malloc(len);
    if (nodeList == NULL) {
        return CM_ERR;
    }
    nodeList->poolId = pool->poolId;
    nodeList->nodeNum = pool->maxNodeNum;

    int32_t nlen = (int32_t)(sizeof(NodeInfo) * pool->maxNodeNum);
    memset_s(nodeList->nodeList, nlen, 0, nlen);

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, pool->poolId, CM_NODE_LIST_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        free(nodeList);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, (char *)nodeList, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        free(nodeList);
        return CM_ERR;
    }
    free(nodeList);

    return CmZkCreatePoolDir4(pool);
}

static int32_t CmZkCreatePoolDir2(PoolInfo *pool)
{
    char zkPath[CM_ZNODE_PATH_LEN] = {0};
    int32_t ret;

    int32_t len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo) * pool->maxNodeNum);
    NodeStateList *stateList = (NodeStateList *)malloc(len);
    if (stateList == NULL) {
        return CM_ERR;
    }
    stateList->poolId = pool->poolId;
    stateList->nodeNum = pool->maxNodeNum;
    stateList->masterNodeId = NODE_ID_INVALID;
    stateList->resv = 0;
    uint16_t nodeId;
    for (nodeId = 0; nodeId < pool->maxNodeNum; nodeId++) {
        stateList->nodeList[nodeId].nodeId = nodeId;
        stateList->nodeList[nodeId].state = NODE_STATE_INVALID;
        stateList->nodeList[nodeId].clusterState = NODE_STATE_INVALID;
        stateList->nodeList[nodeId].diskNum = 0;
        size_t dlen = sizeof(NodeDiskState) * MAX_DISK_NUM;
        memset_s(stateList->nodeList[nodeId].diskList, dlen, 0, dlen);
    }

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, pool->poolId, CM_STATE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        free(stateList);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, (char *)stateList, len, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        free(stateList);
        return CM_ERR;
    }
    free(stateList);

    return CmZkCreatePoolDir3(pool);
}

static int32_t CmZkCreatePoolDir1(PoolInfo *pool)
{
    char zkPath[CM_ZNODE_PATH_LEN] = {0};
    int32_t ret;

    uint16_t poolId = pool->poolId;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_CHILD_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_MR_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_NODE_EVENT_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_NODE_INFO_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    return CmZkCreatePoolDir2(pool);
}

static int32_t CmZkCreatePoolDir(PoolInfo *pool)
{
    char zkPath[CM_ZNODE_PATH_LEN] = {0};
    int32_t ret;

    uint16_t poolId = pool->poolId;

    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_IP_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_NODE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_CAPACITY_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }
    ret = sprintf_s(zkPath, CM_ZNODE_PATH_LEN, "%s/%u/%s", CM_POOL, poolId, CM_NODEID_GENERATE_PATH);
    if (ret < 0) {
        CM_LOGERROR("Sprintf_s path failed, ret(%d).", ret);
        return CM_ERR;
    }
    ret = CmZkCreate(g_zh, zkPath, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        CM_LOGERROR("Create znode(%s) failed, ret(%d).", zkPath, ret);
        return CM_ERR;
    }

    return CmZkCreatePoolDir1(pool);
}

static int32_t CmZkCreateDataPoolDir(void)
{
    uint16_t poolId;
    int32_t ret;

    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        PoolInfo *pool = CmConfigGetPoolInfo(poolId);
        if (pool == NULL) {
            continue;
        }

        ret = CmZkCheckPoolDir(pool);
        if (ret != CM_OK) {
            CM_LOGERROR("Check pool failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
        ret = CmZkCreatePoolDir(pool);
        if (ret != CM_OK) {
            CM_LOGERROR("Create pool dir failed, poolId(%u) ret(%d).", poolId, ret);
            return ret;
        }
    }

    return CM_OK;
}

int CmZkInit(void)
{
    int ret;

    ret = ZookeeperApiLoad();
    if (ret != CM_OK) {
        CM_LOGERROR("Failed to load zookeeper library");
        return ret;
    }

    ret = CmZkConnect();
    if (ret != CM_OK) {
        return ret;
    }

    ret = CmZkCreateMetaDir();
    if (ret != CM_OK) {
        return ret;
    }

    ret = CmZkCreateDataPoolDir();
    if (ret != CM_OK) {
        return ret;
    }

    return CM_OK;
}
#endif
