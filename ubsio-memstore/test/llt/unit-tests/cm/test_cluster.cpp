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
#include <mockcpp/mockcpp.hpp>
#include <cstring>
#include "gtest/gtest.h"
#include "mms_types.h"
#include "cm.h"
#include "securec.h"
#include "zookeeper.h"
#include "cm_zkadapter.h"
#include "cm_config.h"
#include "cm_threadpool.h"
#include "common/cm_inner.h"
#include "common/cm_module.h"
#include "common/cm_thread.h"
#include "common/cm_threadpool.h"
#include "client/cm_client_init.h"
#include "client/cm_client_local.h"
#include "client/cm_client_event.h"
#include "client/cm_client_schedule.h"
#include "server/pt/cm_pt_calc_fixed_state.h"
#include "server/pt/cm_pt_store.h"
#include "server/cm_server_init.h"
#include "server/cm_server_view.h"
#include "server/cm_server_monitor.h"
#include "test_cluster.h"

using namespace ock::mms;
bool TestCluster::gSetup = false;

void TestCluster::SetUp()
{
   if (gSetup) {
       return;
   }
   gSetup = true;
   return;
}

void TestCluster::TearDown()
{
   return;
}

static LocalNodeQueryOpHandle gLocalQuery = { nullptr, nullptr, nullptr };

static int32_t CM_RegLocalNodeQueryOpHandle_Stub(uint16_t poolId, LocalNodeQueryOpHandle *handle)
{
   gLocalQuery.queryLocalNodeInfo = handle->queryLocalNodeInfo;
   gLocalQuery.queryLocalNodeMr = handle->queryLocalNodeMr;
   gLocalQuery.ctx = handle->ctx;
   return CM_OK;
}

static NodeListChangeOpHandle gNodeChange = { nullptr, nullptr };

static int32_t CM_RegNodeListChangeNotifyHandle_Stub(uint16_t poolId, NodeListChangeOpHandle *handle)
{
   gNodeChange.notifyNodeListChange = handle->notifyNodeListChange;
   gNodeChange.ctx = handle->ctx;
   return CM_OK;
}

static PtViewChangeOpHandle gPtChange = { nullptr, nullptr };

static int32_t CM_RegPtViewChangeOpHandle_Stub(uint16_t poolId, PtViewChangeOpHandle *handle)
{
   gPtChange.notifyPtListChange = handle->notifyPtListChange;
   gPtChange.ctx = handle->ctx;
   return CM_OK;
}

static uint16_t CM_GetLocalNodeId_Stub(uint16_t poolId)
{
   return 0;
}

static NodeInfo gNodeInfo;

static int32_t CM_GetNodeInfo_Stub(uint16_t poolId, NodeInfo *nodeInfo)
{
   uint16_t nodeId = nodeInfo->nodeId;
   memcpy_s(nodeInfo, sizeof(NodeInfo), &gNodeInfo, sizeof(NodeInfo));
   nodeInfo->nodeId = nodeId;
   return CM_OK;
}

static void InitNodeList(PoolInfo *pools, NodeStateList *nodeList, uint16_t nodeNum)
{
   nodeList->poolId = pools->poolId;
   nodeList->nodeNum = nodeNum;
   CmClientLocalGetNode(pools->poolId, &gNodeInfo);
   for (uint32_t nodeId = 0; nodeId < nodeNum; nodeId++) {
       nodeList->masterNodeId = 0;
       nodeList->nodeList[nodeId].sessionId = 0;
       nodeList->nodeList[nodeId].nodeId = nodeId;
       nodeList->nodeList[nodeId].state = NODE_STATE_UP;
       nodeList->nodeList[nodeId].clusterState = NODE_CLUSTER_STATE_IN;
       nodeList->nodeList[nodeId].diskNum = gNodeInfo.diskList.num;
       for (uint16_t diskIdx = 0; diskIdx < gNodeInfo.diskList.num; diskIdx++) {
           nodeList->nodeList[nodeId].diskList[diskIdx].diskId = gNodeInfo.diskList.list[diskIdx].diskId;
           nodeList->nodeList[nodeId].diskList[diskIdx].clusterState = DISK_CLUSTER_STATE_IN;
       }
   }
}

static int32_t CM_Init_Stub(ConfigRole role, PoolInfo *pools, uint16_t num, const CmCfgInfo *cfgInfo)
{
   if (pools == nullptr || num != 1) {
       return CM_ERR;
   }
   if (gNodeChange.notifyNodeListChange == nullptr || gPtChange.notifyPtListChange == nullptr) {
       return CM_ERR;
   }

   uint16_t nodeNum = NO_1;
   auto nodeList = (NodeStateList *)malloc(sizeof(NodeStateList) + sizeof(NodeStateInfo) * nodeNum);
   if (nodeList == nullptr) {
       return CM_ERR;
   }
   InitNodeList(pools, nodeList, nodeNum);
   gNodeChange.notifyNodeListChange(nodeList, gNodeChange.ctx);
   free(nodeList);

   auto ptList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * gNodeInfo.diskList.num);
   if (ptList == nullptr) {
       return CM_ERR;
   }
   ptList->poolId = pools->poolId;
   ptList->ptNum = gNodeInfo.diskList.num;
   ptList->maxCopyNum = 1;
   ptList->minCopyNum = 1;
   ptList->globalVersion = 1;
   for (uint16_t diskIdx = 0; diskIdx < gNodeInfo.diskList.num; diskIdx++) {
       ptList->ptEntryList[diskIdx].birthVersion = 1;
       ptList->ptEntryList[diskIdx].ptId = diskIdx;
       ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
       ptList->ptEntryList[diskIdx].masterNodeId = 0;
       ptList->ptEntryList[diskIdx].masterDiskId = gNodeInfo.diskList.list[diskIdx].diskId;
       ptList->ptEntryList[diskIdx].copyNum = 1;
       ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
       ptList->ptEntryList[diskIdx].copyList[0].diskId = gNodeInfo.diskList.list[diskIdx].diskId;
       ptList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
   }
   gPtChange.notifyPtListChange(ptList, gPtChange.ctx);
   free(ptList);
   return CM_OK;
}

static void ZooSetDebugLevel() {}

static zhandle_t *ZookeeperInit()
{
   return (zhandle_t *)"zHandle";
}

static int ZooState()
{
   return ZOO_CONNECTED_STATE;
}

static int ZooRecvTimeout()
{
   return (int)NO_10;
}

static int ZooCreate(zhandle_t *zh, const char *path, const char *value, int valuelen, const struct ACL_vector *acl,
                    int mode, char *pathBuffer, int pathBufferLen)
{
   return ZOK;
}

static int ZooGet(zhandle_t *zh, const char *path, int watch, char *buffer, int *bufferLen, struct Stat *stat)
{
   if (*bufferLen == (int)sizeof(uint16_t)) {
       if (strcmp(path, "/cm/meta/ip/127.0.0.1") == 0) {
           return ZNONODE;
       }
       uint32_t *nodeId = (uint32_t *)buffer;
       *nodeId = 0;
   } else if (*bufferLen == (int)sizeof(uint64_t)) {
       uint64_t *sessionId = (uint64_t *)buffer;
       *sessionId = 0;
   } else if (*bufferLen == NODE_META_BUFF_LEN) {
       NodeMetaBuff *mrBuff = (NodeMetaBuff *)buffer;
       mrBuff->nodeId = 0;
       mrBuff->len = 0;
   } else if (*bufferLen == (int)sizeof(PoolInfo)) {
       PoolInfo *pool = (PoolInfo *)buffer;
       (void)memset_s(pool, sizeof(PoolInfo), 0, sizeof(PoolInfo));
       pool->poolId = 0;
       pool->diskType = DISK_TYPE_DRAM;
       pool->redundanceNum = NO_2;
       pool->initialNodeNum = NO_3;
       pool->maxNodeNum = NO_256;
       pool->maxPtNum = NO_16;
   } else if (*bufferLen == (int)sizeof(NodeInfo)) {
       NodeInfo *nodeInfo = (NodeInfo *)buffer;
       uint16_t nodeId = nodeInfo->nodeId;
       memcpy_s(nodeInfo, sizeof(NodeInfo), &gNodeInfo, sizeof(NodeInfo));
       nodeInfo->nodeId = nodeId;
       return 0;
   } else if (*bufferLen == (int32_t)(sizeof(PtEntryList) + sizeof(PtEntry) * NO_16)) {
       PtEntryList *ptList = (PtEntryList *)buffer;
       ptList->ptNum = 0;
   } else if (*bufferLen == (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo) * NO_256)) {
       NodeStateList *stateList = (NodeStateList *)buffer;
       stateList->nodeList[0].state = NODE_STATE_UP;
       stateList->nodeList[0].clusterState = NODE_CLUSTER_STATE_IN;
   }
   return ZOK;
}

static int ZooExists(zhandle_t *zh, const char *path, int watch, struct Stat *stat)
{
   return ZNONODE;
}

static int ZooSet(zhandle_t *zh, const char *path, const char *buffer, int buflen, int version)
{
   return ZOK;
}
