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

#ifndef CM_ZKADAPTER_H
#define CM_ZKADAPTER_H

#include "cm_zk_api_dl.h"
#include "cm_log.h"
#include "cm_comm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#if DESC("zk client")
typedef int32_t (*nodeChangeNotifyFp)(NodeInfoList *nodeList);
typedef int32_t (*stateChangeNotifyFp)(NodeStateList *stateList);
typedef int32_t (*masterChangeNotifyFp)(uint16_t poolId, uint16_t nodeId);
typedef int32_t (*ptChangeNotifyFp)(PtEntryList *ptList);

int32_t CmClientZkGetNodeId(uint16_t poolId, const char *ipv4AddrStr, uint16_t port, uint16_t *nodeId);

int32_t CmClientZkRecordNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

int32_t CmClientZkRegisterNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

int32_t CmClientZkRecordMr(uint16_t poolId, NodeMetaBuff *mr);

int32_t CmClientZkRecordLocalSession(uint16_t poolId);

int32_t CmClientZkGetNodeSession(uint16_t poolId, uint16_t nodeId, uint64_t *sessionId);

int32_t CmClientZkGetMr(uint16_t poolId, NodeMetaBuff *mr);

int32_t CmClientZkGetNodeState(uint16_t poolId, uint16_t nodeId, NodeStateInfo *cmState);

int32_t CmClientZkGetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

int32_t CmClientZkNodeEventExistCheck(uint16_t poolId, uint16_t nodeId);

int32_t CmClientZkPtEventExistCheck(uint16_t poolId, uint16_t nodeId);

int32_t CmClientZkRecordNodeEvent(CmNodeEvent *nodeEvent);

int32_t CmClientZkRecordPtEvent(CmPtEvent *ptEvent);

int32_t CmClientZkSubStateListChange(uint16_t poolId, stateChangeNotifyFp notifyFp);

int32_t CmClientZkSubPtListChange(uint16_t poolId, ptChangeNotifyFp notifyFp);

int32_t CmClientZkRecordDataInfo(uint16_t poolId, const char *key, void *value, uint32_t valLen);

int32_t CmClientZkSubDataInfoChange(uint16_t poolId, const char *key, void *value, uint32_t valLen,
    DataInfoChangeOpHandle *handle);

uint16_t CmClientZkGetNodeIdByPath(const char *path, const char *pre);

int32_t CmClientZkPtEntryIsSame(PtEntry *elem1, PtEntry *elem2);
#endif
#if DESC("zk server")
typedef int32_t (*ZkNotifyMetaRoleFp)(CmServerRole serverRole);
typedef int32_t (*ZkNotifyNodeListFp)(CmNodeIdList *nodeList);
typedef int32_t (*ZkNotifyNodeEventFp)(CmNodeEvent *nodeEvent);
typedef void (*ZkNotifyPtEventFp)(CmPtEvent *ptEvent);
typedef int32_t (*ZkCommitPtEventFp)(uint16_t poolId);

int32_t CmServerZkRegisterMetaNode(const char *ipv4AddrStr);

int32_t CmServerZkRecordMetaSession(void);

int32_t CmServerZkRecordPtEntryList(PtEntryList *ptEntryList);

int32_t CmServerZkRecordNodeList(NodeInfoList *nodeList);

int32_t CmServerZkRecordStateList(NodeStateList *stateList);

int32_t CmServerZkGetNodeList(NodeInfoList *nodeList);

int32_t CmServerZkGetStateList(NodeStateList *stateList);

int32_t CmServerZkGetPtEntryList(PtEntryList *ptEntryList);

int32_t CmServerZkGetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

int32_t CmServerZkSubRoleChange(ZkNotifyMetaRoleFp notifyFp);

int32_t CmServerZkSubNodeListChange(uint16_t poolId, ZkNotifyNodeListFp notifyFp);

int32_t CmServerZkSubNodeEvent(uint16_t poolId, ZkNotifyNodeEventFp notifyFp);

int32_t CmServerZkSubPtEvent(uint16_t poolId, ZkNotifyPtEventFp notifyFp, ZkCommitPtEventFp commitFp);
#endif
#if DESC("zk base handle")
#define CM_ROOT "/mms"

#define CM_META "/mms/meta"

#define CM_POOL "/mms/pool"

#define CM_LISTCHECK_PATH "listcheck" // 持久节点，用于白名单检查IP列表信息

#define CM_IP_PATH "ip" // 持久节点，用于保存节点基本信息

#define CM_NODE_PATH "node" // 持久节点，用于节点发现，FOUND BY ZK_SERVER

#define CM_MASTER_PATH "master" // 临时节点，用于竞争主

#define CM_PT_PATH "pt" // 永久节点，用于记录pt视图

#define CM_CAPACITY_PATH "capacity" // 持久节点，用于容量统计

#define CM_MR_PATH "mr" // 持久节点，用于保存节点链路mr信息

#define CM_CHILD_PATH "child" // 持久节点，用于保存孩子列表信息

#define CM_NODE_LIST_PATH "node_list" // 持久节点，用于保存节点基础信息列表

#define CM_NODE_INFO_PATH "node_info" // 持久节点，用于保存节点基础信息

#define CM_NODE_EVENT_PATH "node_event" // 持久节点，用于保存节点事件上报信息

#define CM_PT_EVENT_PATH "pt_event" // 持久节点，用于保存PT副本恢复完成事件上报信息

#define CM_PT_FINISH_PATH "pt_finish" // 持久节点，用于保存PT视图处理完成事件上报信息

#define CM_STATE_PATH "state" // 持久节点，用于保存节点健康状态信息

#define CM_NODEID_GENERATE_PATH "nodeid_generator" // 持久节点，用于生成nodeid
#define CM_NODEID_GENERATE_CHILD_PATH "nodeid_generator/"

#define CM_DATA_INFO_PATH "data_info" // 持久节点，用于保存用户写入数据

#define CM_ZNODE_PATH_LEN (128UL)

#define CM_ZK_TIMEOUT (30000UL)

#define CM_ZK_TRY_INTERAL (1000UL)

#define CM_ZK_NODE_DETECT_TIME (60000UL)

#define CM_ZK_TRY_CONNECT_TIME (30000UL)

#define CM_ZK_RADIX_10 10UL

int CmZkCreate(zhandle_t *zh, const char *path, const char *value, int valuelen, const struct ACL_vector *acl, int mode,
    char *pathBuffer, int pathBufferLen);

int CmZkDelete(zhandle_t *zh, const char *path, int version);

int CmZkGet(zhandle_t *zh, const char *path, int watch, char *buffer, int *bufferLen, struct Stat *stat);

int CmZkExists(zhandle_t *zh, const char *path, int watch, struct Stat *stat);

int CmZkSet(zhandle_t *zh, const char *path, const char *buffer, int buflen, int version);

int CmZkWget(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx, char *buffer, int *bufferLen,
    struct Stat *stat);

int CmZkWgetChildren(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx,
    struct String_vector *strings);
#endif
#if DESC("zk initial")
int CmClientZkInit(void);

int CmServerZkInit(void);

int CmZkInit(void);

void CmClientZkFreeParaList(uint16_t poolId);
#endif
#if defined(__cplusplus)
}
#endif

#endif
