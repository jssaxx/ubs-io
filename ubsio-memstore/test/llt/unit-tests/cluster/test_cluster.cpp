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

static int ZooWget(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx, char *buffer, int *bufferLen,
    struct Stat *stat)
{
    char *result = nullptr;
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int ret = strcpy_s(zkPath, CM_ZNODE_PATH_LEN, path);
    if (ret != 0) {
        return ZOK;
    }
    if (result = strstr(zkPath, CM_NODE_LIST_PATH)) {
        NodeInfoList *nodeChangeList = (NodeInfoList *)buffer;
        nodeChangeList->poolId = 0;
        nodeChangeList->nodeNum = 0;
        watcher(zh, ZOO_CREATED_EVENT, 1, path, watcherCtx);
    } else if (result = strstr(zkPath, CM_STATE_PATH)) {
        NodeStateList *nodeStatList = (NodeStateList *)buffer;
        nodeStatList->poolId = 0;
        nodeStatList->nodeNum = 0;
        watcher(zh, ZOO_CREATED_EVENT, 1, path, watcherCtx);
    } else if (result = strstr(zkPath, CM_PT_PATH)) {
        PtEntryList *ptList = (PtEntryList *)buffer;
        ptList->poolId = 0;
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
    } else if (result = strstr(zkPath, CM_MASTER_PATH)) {
        uint16_t *masterNodeId = (uint16_t *)buffer;
        *masterNodeId = 0;
        watcher(zh, ZOO_CREATED_EVENT, 1, path, watcherCtx);
    }
    return ZOK;
}

static int ZooDelete(zhandle_t *zh, const char *path, int version)
{
    return ZOK;
}

static std::vector<std::string> gZkGetChildrenFirst;
static int ZooWgetChildren(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx,
    struct String_vector *strings)
{
    auto it = std::find(gZkGetChildrenFirst.begin(), gZkGetChildrenFirst.end(), path);
    if (it != gZkGetChildrenFirst.end()) {
        strings->count = 0;
    } else {
        strings->count = 1;
        strings->data = static_cast<char **>(malloc(sizeof(char *)));
        uint16_t newLen = strlen(path) + NO_3;
        char *tmp = (char *)malloc(newLen);
        int ret = strcpy_s(tmp, newLen, path);
        if (ret != MMS_OK) {
            return MMS_ERR;
        }
        ret = strcat_s(tmp, newLen, "/0");
        if (ret != MMS_OK) {
            return MMS_ERR;
        }
        strings->data[0] = tmp;
        gZkGetChildrenFirst.push_back(path);
        free(tmp);
    }
    watcher(zh, ZOO_CREATED_EVENT, 1, path, watcherCtx);
    return ZOK;
}

static int ZooDeallocateStringVector(struct String_vector *strings)
{
    if (strings != nullptr) {
        if (strings->data != nullptr) {
            free(strings->data);
            strings->data = nullptr;
        }
    }
    return ZOK;
}

static int ZookeeperClose(zhandle_t *zh)
{
    return ZOK;
}

void TestCluster::Stub()
{
    MOCKER(CM_RegNodeListChangeNotifyHandle).stubs().will(invoke(CM_RegNodeListChangeNotifyHandle_Stub));
    MOCKER(CM_RegPtViewChangeOpHandle).stubs().will(invoke(CM_RegPtViewChangeOpHandle_Stub));
    MOCKER(CM_GetLocalNodeId).stubs().will(invoke(CM_GetLocalNodeId_Stub));
    MOCKER(CM_GetNodeInfo).stubs().will(invoke(CM_GetNodeInfo_Stub));
    MOCKER(CM_Init).stubs().will(invoke(CM_Init_Stub));

    MOCKER(zoo_set_debug_level).stubs().will(invoke(ZooSetDebugLevel));
    MOCKER(zookeeper_init).stubs().will(invoke(ZookeeperInit));
    MOCKER(zoo_state).stubs().will(invoke(ZooState));
    MOCKER(zoo_recv_timeout).stubs().will(invoke(ZooRecvTimeout));
    MOCKER(zoo_create).stubs().will(invoke(ZooCreate));
    MOCKER(zoo_recv_timeout).stubs().will(invoke(ZooRecvTimeout));
    MOCKER(zoo_get).stubs().will(invoke(ZooGet));
    MOCKER(zoo_exists).stubs().will(invoke(ZooExists));
    MOCKER(zoo_set).stubs().will(invoke(ZooSet));
    MOCKER(zoo_wget).stubs().will(invoke(ZooWget));
    MOCKER(zoo_delete).stubs().will(invoke(ZooDelete));
    MOCKER(zoo_wget_children).stubs().will(invoke(ZooWgetChildren));
    MOCKER(zookeeper_close).stubs().will(invoke(ZookeeperClose));
    MOCKER(deallocate_String_vector).stubs().will(invoke(ZooDeallocateStringVector));
}

TEST_F(TestCluster, test_cm_inner_init)
{
    LOG_INFO("test_cm_inner_init");
    CmOptions mOptions;
    mOptions.role = ROLE_TOGETHER;
    mOptions.zkIpMask = "127.0.0.1:2181";
    mOptions.groups.groupId = 0;
    mOptions.groups.replicaNum = NO_1;
    mOptions.groups.initialNodeNum = NO_1;
    mOptions.groups.maxNodeNum = NO_1;
    mOptions.groups.maxPtNum = NO_1;
    mOptions.hbTempTimeout = NO_30;
    mOptions.hbPermFaultTime = NO_60;

    PoolInfo pools;
    int32_t ret = strcpy_s(pools.poolName, POOL_NAME_LEN, "mms_tester");
    EXPECT_EQ(ret, 0);
    pools.poolId = mOptions.groups.groupId;
    pools.diskType = DISK_TYPE_DRAM;
    pools.redundanceNum = (mOptions.groups.replicaNum == NO_2) ? NO_2 : NO_3;
    pools.initialNodeNum = mOptions.groups.initialNodeNum;
    pools.maxNodeNum = mOptions.groups.maxNodeNum;
    pools.maxPtNum = mOptions.groups.maxPtNum;

    CmCfgInfo cfgInfo;
    cfgInfo.zkIpMask = const_cast<char *>(mOptions.zkIpMask.c_str());
    cfgInfo.ipStr = "127.0.0.1";
    cfgInfo.regTimeOut = mOptions.hbTempTimeout * NO_1000;
    cfgInfo.regPermTimeOut = mOptions.hbPermFaultTime * NO_1000;
    ret = CM_Init(CONFIG_ROLE_TOGETHER, &pools, 1, &cfgInfo);
    EXPECT_EQ(ret, CM_OK);
}

static void CmServerCancelNodeFaultStub()
{
    return;
}

static void CmServerMonitorInitMgrStub()
{
    return;
}

static int32_t CmServerMonitorLoadPoolStub()
{
    return CM_OK;
}


void TestCluster::CancelNodeStub()
{
    MOCKER(CmServerCancelNodeFault).stubs().will(invoke(CmServerCancelNodeFaultStub));
    MOCKER(CmServerMonitorInitMgr).stubs().will(invoke(CmServerMonitorInitMgrStub));
    MOCKER(CmServerMonitorLoadPool).stubs().will(invoke(CmServerMonitorLoadPoolStub));
}

static int32_t StubNodeChgHandler(NodeStateList *nodeList, void *ctx)
{
    return MMS_OK;
}

TEST_F(TestCluster, test_cm_node_info)
{
    LOG_INFO("test_cm_node_info");
    CmNodeInfo cmNodeInfo;
    cmNodeInfo.id = NO_1;
    cmNodeInfo.ip = "127.0.0.1";
    cmNodeInfo.port = 0;
    cmNodeInfo.status = CM_NODE_NORMAL;
    cmNodeInfo.numas.push_back(NO_1);
    std::string str = cmNodeInfo.ToString();
}

TEST_F(TestCluster, test_cm_pt_info)
{
    LOG_INFO("test_cm_pt_info");
    CmPtInfo cmPtInfo;
    cmPtInfo.version = 0;
    cmPtInfo.ptId = NO_1;
    cmPtInfo.state = CM_PT_NORMAL;
    cmPtInfo.masterNodeId = NO_1;
    cmPtInfo.resv[0] = NO_1;
    cmPtInfo.resv[1] = NO_2;
    CmPtCopy cmPtCopy;
    cmPtCopy.state = CM_COPY_RUNNING;
    cmPtCopy.nodeId = NO_1;
    cmPtInfo.copys.push_back(cmPtCopy);
    std::string str = cmPtInfo.ToString();
    CmPtInfo cmPtInfoCopy;
    cmPtInfoCopy.Clone(cmPtInfo);
}

TEST_F(TestCluster, test_cm_service_status)
{
    LOG_INFO("test_cm_service_status");
    auto cm = Cm::Instance();
    cm->GetServiceStatus();
}

TEST_F(TestCluster, test_get_node_info_return_err)
{
    LOG_INFO("test_get_node_info_return_err");
    auto cm = Cm::Instance();
    uint16_t nodeId = NO_1;
    CmNodeInfo cmNodeInfo;

    auto result = cm->GetNodeInfo(nodeId, cmNodeInfo);
    EXPECT_EQ(result, MMS_ERR);
}

TEST_F(TestCluster, test_check_is_online_return_err)
{
    LOG_INFO("test_check_is_online_return_err");
    auto cm = Cm::Instance();
    uint16_t nodeId = NO_1;
    std::string ip = "127.0.0.1";
    uint16_t port = 0;

    auto result = cm->CheckIsOnline(nodeId, ip, port);
    EXPECT_EQ(result, false);
}

TEST_F(TestCluster, test_get_node_info)
{
    LOG_INFO("test_get_node_info");
    auto cm = Cm::Instance();
    cm->GetNodeView();
}

TEST_F(TestCluster, test_get_pt_info)
{
    LOG_INFO("test_get_pt_info");
    auto cm = Cm::Instance();
    cm->GetPtView();
}

TEST_F(TestCluster, test_get_pt_info_return_err)
{
    LOG_INFO("test_get_pt_info_return_err");
    auto cm = Cm::Instance();
    uint16_t ptId = NO_1;
    CmPtInfo ptInfo;

    auto ret = cm->GetPtInfo(ptId, ptInfo);
    EXPECT_EQ(ret, MMS_ERR);
}

TEST_F(TestCluster, test_get_pt_info_not_normal)
{
    LOG_INFO("test_get_pt_info_not_normal");
    auto cm = Cm::Instance();
    uint16_t ptId = NO_1;
    uint64_t ptv = NO_1;
    uint16_t remoteIds[MAX_NODES_NUM];
    uint16_t remoteNum = NO_3;

    auto ret = cm->GetPtInfo(ptId, ptv, remoteIds, remoteNum);
    EXPECT_EQ(ret, MMS_OK);
}

TEST_F(TestCluster, test_update_pt_state)
{
    LOG_INFO("test_update_pt_state");
    auto cm = Cm::Instance();
    uint16_t ptId = NO_1;

    auto ret = cm->UpdatePtState(ptId);
    EXPECT_EQ(ret, MMS_ERR);
}

TEST_F(TestCluster, test_cm_set_disk_status_return_err)
{
    LOG_INFO("test_cm_set_disk_status_return_err");
    uint16_t poolId = 0;
    uint16_t diskId = NO_1;
    DiskState diskState = DISK_STATE_NORMAL;

    auto ret = CM_SetDiskStatus(poolId, diskId, diskState);
    EXPECT_EQ(ret, CM_ERR);
}

TEST_F(TestCluster, test_cm_set_pt_status_return_err)
{
    LOG_INFO("test_cm_set_pt_status_return_err");
    uint16_t poolId = 0;
    uint16_t ptNum = NO_16;
    PtFinish *ptFinish;

    auto ret = CM_SetPtFinishStatus(poolId, ptNum, ptFinish);
    EXPECT_EQ(ret, CM_ERR);
}

TEST_F(TestCluster, test_cm_client_init)
{
    LOG_INFO("test_cm_client_init");
    CM_ClientInit();
}

TEST_F(TestCluster, test_cm_get_node_info)
{
    LOG_INFO("test_cm_get_node_info");
    uint16_t poolId = NO_1;
    NodeInfo nodeInfo{};
    nodeInfo.nodeId = NO_1;

    auto ret = CM_GetNodeInfo(poolId, &nodeInfo);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_get_node_mr)
{
    LOG_INFO("test_cm_get_node_mr");
    uint16_t poolId = NO_1;
    NodeMetaBuff mr{};
    mr.nodeId = NO_1;

    auto ret = CM_GetNodeMr(poolId, &mr);
    EXPECT_EQ(ret, CM_ERR);
}

TEST_F(TestCluster, test_cm_get_local_node_id)
{
    LOG_INFO("test_cm_get_local_node_id");
    uint16_t poolId = NO_1;

    auto ret = CM_GetLocalNodeId(poolId);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_register_node_query_op_handle)
{
    LOG_INFO("test_cm_register_node_query_op_handle");
    uint16_t poolId = NO_1;
    LocalNodeQueryOpHandle *handle = NULL;

    auto ret = CM_RegLocalNodeQueryOpHandle(poolId, handle);
    EXPECT_EQ(ret, CM_ERR);
}

TEST_F(TestCluster, test_cm_write_data_info)
{
    LOG_INFO("test_cm_write_data_info");
    uint16_t poolId = NO_1;
    const char *key = "key";
    uint8_t buffer[128] = {0};
    void *value = buffer;
    uint32_t valLen = 0;

    auto ret = CM_WriteDataInfo(poolId, key, value, valLen);
    EXPECT_EQ(ret, CM_ERR);
}

TEST_F(TestCluster, test_cm_client_local_get_node_info)
{
    LOG_INFO("test_cm_client_local_get_node_info");
    uint16_t poolId = NO_1;
    NodeInfo nodeInfo;

    auto ret = CmClientLocalGetNodeInfo(poolId, &nodeInfo);
    EXPECT_EQ(ret, CM_ERR);
}

TEST_F(TestCluster, test_cm_client_local_get_node_id)
{
    LOG_INFO("test_cm_client_local_get_node_id");
    uint16_t poolId = NO_1;

    auto ret = CmClientLocalGetNodeId(poolId);
    EXPECT_EQ(ret, NODE_ID_INVALID);
}

TEST_F(TestCluster, test_cm_client_local_update_node_info)
{
    LOG_INFO("test_cm_client_local_update_node_info");
    uint16_t poolId = NO_1;
    NodeInfo nodeInfo = {};
    CmClientLocalUpdateNodeInfo(poolId, &nodeInfo);
}

TEST_F(TestCluster, test_cm_client_local_init)
{
    LOG_INFO("test_cm_client_local_init");
    CmClientLocalInit();
}

void *TestThreadFunc(void *arg)
{
    int *flag = (int *)arg;
    if (flag != nullptr) {
        *flag = 1;
    }
    return nullptr;
}

TEST_F(TestCluster, test_cm_client_schedule_add)
{
    LOG_INFO("test_cm_client_schedule_add");
    uint16_t poolId = NO_1;
    THREAD_CALL_BACK handle = TestThreadFunc;
    CmClientSchedueAdd(poolId, handle, nullptr);
}

TEST_F(TestCluster, test_cm_client_schedule_init)
{
    LOG_INFO("test_cm_client_schedule_init");
    auto ret = CmClientScheduleInit();
    EXPECT_EQ(ret, RETURN_OK);
    CmClientScheduleExit();
}

TEST_F(TestCluster, test_cm_get_node_time)
{
    LOG_INFO("test_cm_get_node_time");
    CmGetNanoTime();
}

TEST_F(TestCluster, test_cm_get_milli_time)
{
    LOG_INFO("test_cm_get_milli_time");
    CmGetMilliTime();
}

TEST_F(TestCluster, test_cm_get_seconds_time)
{
    LOG_INFO("test_cm_get_seconds_time");
    CmGetSecondsTime();
}

TEST_F(TestCluster, test_cm_config_get_timeout)
{
    LOG_INFO("test_cm_config_get_timeout");
    CmConfigGetTimeOut();
}

TEST_F(TestCluster, test_cm_config_get_perm_fault_timeout)
{
    LOG_INFO("test_cm_config_get_perm_fault_timeout");
    CmConfigGetPermFaultTimeOut();
}

TEST_F(TestCluster, test_cm_config_get_disk_perm_fault_timeout)
{
    LOG_INFO("test_cm_config_get_disk_perm_fault_timeout");
    CmConfigGetDiskPermFaultTimeOut();
}

TEST_F(TestCluster, test_cm_config_get_node_id)
{
    LOG_INFO("test_cm_config_get_node_id");
    CmConfigGetNodeId();
}

TEST_F(TestCluster, test_cm_config_get_ip_addr)
{
    LOG_INFO("test_cm_config_get_ip_addr");
    CmConfigGetIpv4AddrStr();
}

TEST_F(TestCluster, test_cm_config_get_zk_server_list)
{
    LOG_INFO("test_cm_config_get_zk_server_list");
    CmConfigGetZkServerList();
}

TEST_F(TestCluster, test_cm_config_has_cfg_pools)
{
    LOG_INFO("test_cm_config_has_cfg_pools");
    CmConfigHasCfgPoolS();
}

TEST_F(TestCluster, test_cm_config_init)
{
    LOG_INFO("test_cm_config_init");
    ConfigRole configRole = CONFIG_ROLE_CMM;
    PoolInfo pools;
    int32_t ret = strcpy_s(pools.poolName, POOL_NAME_LEN, "mms_tester");
    EXPECT_EQ(ret, 0);
    pools.poolId = NO_1;
    pools.diskType = DISK_TYPE_DRAM;
    pools.redundanceNum = NO_2;
    pools.initialNodeNum = NO_3;
    pools.maxNodeNum = NO_256;
    pools.maxPtNum = NO_8192;
    uint16_t num = NO_1;
    CmCfgInfo cfgInfo;
    char *zkMask = "127.0.0.1:2181";
    char *ipStr = "127.0.0.1";
    cfgInfo.zkIpMask = zkMask;
    cfgInfo.ipStr = ipStr;

    ret = CmConfigInit(configRole, &pools, num, &cfgInfo);
    EXPECT_EQ(ret, CM_OK);
}

void cm_module_test_exit(void)
{
    return;
}

int32_t cm_module_test(void)
{
    return CM_OK;
}

TEST_F(TestCluster, test_cm_init_modules)
{
    LOG_INFO("test_cm_init_modules");
    MODULE_DEFINE_S modules[] = {{ "cm_module_test", cm_module_test, cm_module_test_exit }};
    int32_t steps = 0;

    auto ret = initModules(modules, steps);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestCluster, test_cm_sleep)
{
    LOG_INFO("test_cm_sleep");
    unsigned int var = 1;
    CmSleep(var);

    const char *name = "cm_test";
    int flag = 0;
    void *data = &flag;
    THREAD_CALL_BACK handle = TestThreadFunc;

    auto ret = CmSwitchThreadAsync(name, data, handle);
    EXPECT_EQ(ret, RETURN_OK);
}

TEST_F(TestCluster, test_cm_thread_pool_create)
{
    LOG_INFO("test_cm_thread_pool_create");
    uint16_t threadNum = 1;
    uint16_t queueSize = 1;
    uint16_t flags = 0;
    const char *poolName = "cm_test";
    CM_THREAD_POOL_S *pool = CmThreadPoolCreate(threadNum, queueSize, flags, poolName);
    EXPECT_NE(pool, nullptr);
    EXPECT_EQ(CmThreadPoolDestroy(pool, THREAD_POOL_EXIT_IMMEDIATELY), RETURN_OK);
}

TEST_F(TestCluster, test_cm_thread_pool_add)
{
    LOG_INFO("test_cm_thread_pool_add");
    CM_THREAD_POOL_S *scheduleThread[1] = { NULL };

    auto ret = CmThreadPoolAdd(scheduleThread[0], TestThreadFunc, nullptr);
    EXPECT_EQ(ret, RETURN_ERROR);
}

TEST_F(TestCluster, test_cm_thread_pool_destroy)
{
    LOG_INFO("test_cm_thread_pool_destroy");
    CM_THREAD_POOL_S *scheduleThread[1] = { NULL };

    auto ret = CmThreadPoolDestroy(scheduleThread[0], THREAD_POOL_EXIT_IMMEDIATELY);
    EXPECT_EQ(ret, RETURN_ERROR);
}

TEST_F(TestCluster, test_cm_client_zk_get_node_id)
{
    LOG_INFO("test_cm_client_zk_get_node_id");
    uint16_t poolId = NO_1;
    const char *ipv4AddrStr = "127.0.0.1";
    uint16_t port = 0;
    uint16_t nodeId = NO_1;

    auto ret = CmClientZkGetNodeId(poolId, ipv4AddrStr, port, &nodeId);
    EXPECT_EQ(ret, CM_ERR);
}

TEST_F(TestCluster, test_cm_client_zk_record_node_info)
{
    LOG_INFO("test_cm_client_zk_record_node_info");
    uint16_t poolId = NO_1;
    NodeInfo nodeInfo;
    nodeInfo.nodeId = NO_1;
    nodeInfo.port = 0;
    nodeInfo.status = CM_NODE_NORMAL;

    auto ret = CmClientZkRecordNodeInfo(poolId, &nodeInfo);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_get_node_id_by_path)
{
    LOG_INFO("test_cm_client_zk_get_node_id_by_path");
    const char *path = "cm_test";
    const char *pre = "cm_test";

    auto ret = CmClientZkGetNodeIdByPath(path, pre);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestCluster, test_cm_client_zk_record_mr)
{
    LOG_INFO("test_cm_client_zk_record_mr");
    uint16_t poolId = NO_1;
    const char *testData = "test_mr";
    uint16_t dataLen = static_cast<uint16_t>(sizeof(testData));
    NodeMetaBuff *mr = (NodeMetaBuff *)std::malloc(sizeof(NodeMetaBuff) + dataLen);

    mr->nodeId = 3;
    mr->len = dataLen;
    memcpy_s(mr->buf, dataLen, testData, dataLen);

    int32_t ret = CmClientZkRecordMr(poolId, mr);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_record_local_session)
{
    LOG_INFO("test_cm_client_zk_record_local_session");
    uint16_t poolId = NO_1;

    auto ret = CmClientZkRecordLocalSession(poolId);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_get_node_session)
{
    LOG_INFO("test_cm_client_zk_get_node_session");
    uint16_t poolId = 0;
    uint16_t nodeId = 0;
    uint64_t sessionId = 1;

    auto ret = CmClientZkGetNodeSession(poolId, nodeId, &sessionId);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_get_mr)
{
    LOG_INFO("test_cm_client_zk_get_mr");
    uint16_t poolId = NO_1;
    const char *testData = "test_mr";
    uint16_t dataLen = static_cast<uint16_t>(sizeof(testData));
    NodeMetaBuff *mr = (NodeMetaBuff *)std::malloc(sizeof(NodeMetaBuff) + dataLen);

    mr->nodeId = 3;
    mr->len = dataLen;
    memcpy_s(mr->buf, dataLen, testData, dataLen);
    auto ret = CmClientZkGetMr(poolId, mr);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_get_node_info)
{
    LOG_INFO("test_cm_client_zk_get_node_info");
    uint16_t poolId = 0;
    NodeInfo nodeInfo;

    auto ret = CmClientZkGetNodeInfo(poolId, &nodeInfo);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_node_event_exist_check)
{
    LOG_INFO("test_cm_client_zk_node_event_exist_check");
    uint16_t poolId = 0;
    uint16_t nodeId = 0;

    auto ret = CmClientZkNodeEventExistCheck(poolId, nodeId);
    EXPECT_EQ(ret, CM_NOT_EXIST);
}

TEST_F(TestCluster, test_cm_client_zk_pt_event_exist_check)
{
    LOG_INFO("test_cm_client_zk_pt_event_exist_check");
    uint16_t poolId = 0;
    uint16_t nodeId = 0;

    auto ret = CmClientZkPtEventExistCheck(poolId, nodeId);
    EXPECT_EQ(ret, CM_NOT_EXIST);
}

TEST_F(TestCluster, test_cm_client_zk_record_node_event)
{
    LOG_INFO("test_cm_client_zk_record_node_event");
    CmNodeEvent cmNodeEvent;
    cmNodeEvent.nodeId = 0;
    cmNodeEvent.poolId = 0;
    cmNodeEvent.eventType = CM_EVENT_PT_FINISH;

    auto ret = CmClientZkRecordNodeEvent(&cmNodeEvent);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_record_pt_event)
{
    LOG_INFO("test_cm_client_zk_record_pt_event");
    CmPtEvent cmPtEvent;
    cmPtEvent.nodeId = 0;
    cmPtEvent.poolId = 0;
    cmPtEvent.ptNum = NO_16;

    auto ret = CmClientZkRecordPtEvent(&cmPtEvent);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_pt_entry_is_same)
{
    LOG_INFO("test_cm_client_zk_pt_entry_is_same");
    PtEntry elem1 {};
    PtEntry elem2 {};

    elem1.ptId = 1;
    elem1.state = 0;
    elem1.birthVersion = 100;
    elem1.masterNodeId = 2;
    elem1.masterDiskId = 3;
    elem1.copyNum = 2;

    elem2 = elem1;
    elem1.copyList[0].state = 0;
    elem1.copyList[0].nodeId = 10;
    elem1.copyList[0].diskId = 20;
    elem1.copyList[1].state = 0;
    elem1.copyList[1].nodeId = 11;
    elem1.copyList[1].diskId = 21;

    elem2.copyList[0] = elem1.copyList[0];
    elem2.copyList[1] = elem1.copyList[1];

    int32_t ret = CmClientZkPtEntryIsSame(&elem1, &elem2);
    EXPECT_EQ(TRUE, ret);
}

TEST_F(TestCluster, test_cm_client_zk_record_data_info)
{
    LOG_INFO("test_cm_client_zk_record_data_info");
    uint16_t poolId = 0;
    const char *key = "key";
    const char *testData = "test";
    uint32_t valLen = static_cast<uint32_t>(sizeof(testData));
    char buf[16] = {0};
    void *value = buf;
    memcpy_s(value, valLen, testData, valLen);

    auto ret = CmClientZkRecordDataInfo(poolId, key, value, valLen);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_client_zk_sub_data_info)
{
    LOG_INFO("test_cm_client_zk_sub_data_info");
    uint16_t poolId = 0;
    const char *key = "key";
    const char *testData = "test";
    uint32_t valLen = static_cast<uint32_t>(sizeof(testData));
    char buf[16] = {0};
    void *value = buf;
    memcpy_s(value, valLen, testData, valLen);

    auto ret = CmClientZkRecordDataInfo(poolId, key, value, valLen);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_server_zk_free_para_list)
{
    LOG_INFO("test_cm_server_zk_free_para_list");
    uint16_t poolId = 0;
    CmClientZkFreeParaList(poolId);
    CmClientZkInit();
    CmServerZkRecordMetaSession();
}

TEST_F(TestCluster, test_cm_server_zk_register_meta_node)
{
    LOG_INFO("test_cm_server_zk_register_meta_node");
    const char *ipv4 = "127.0.0.1";

    auto ret = CmServerZkRegisterMetaNode(ipv4);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_server_zk_record_pt_list)
{
    LOG_INFO("test_cm_server_zk_record_pt_list");
    PtEntryList *ptEntryList = (PtEntryList *)malloc(sizeof(PtEntry) * NO_5 + sizeof(PtEntryList));
    ptEntryList->poolId = 0;
    ptEntryList->ptNum = NO_16;
    ptEntryList->maxCopyNum = NO_3;
    ptEntryList->minCopyNum = NO_1;
    ptEntryList->globalVersion = NO_1;
    for (uint16_t index = 0; index < NO_5; index++) {
        ptEntryList->ptEntryList[index].birthVersion = 1;
        ptEntryList->ptEntryList[index].ptId = index;
        ptEntryList->ptEntryList[index].state = PT_STATE_NORMAL;
        ptEntryList->ptEntryList[index].masterNodeId = 0;
        ptEntryList->ptEntryList[index].masterDiskId = gNodeInfo.diskList.list[index].diskId;
        ptEntryList->ptEntryList[index].copyNum = 1;
        ptEntryList->ptEntryList[index].copyList[0].nodeId = 0;
        ptEntryList->ptEntryList[index].copyList[0].diskId = gNodeInfo.diskList.list[index].diskId;
        ptEntryList->ptEntryList[index].copyList[0].state = PT_COPY_STATE_RUNNING;
    }

    auto ret = CmServerZkRecordPtEntryList(ptEntryList);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_server_zk_record_node_list)
{
    LOG_INFO("test_cm_server_zk_record_node_list");
    NodeInfoList *nodeInfoList = (NodeInfoList *)malloc(sizeof(NodeInfo) * NO_3 + sizeof(nodeInfoList));
    nodeInfoList->poolId = 0;
    nodeInfoList->nodeNum = NO_3;
    for (uint16_t index = 0; index < NO_3; ++index) {
        nodeInfoList->nodeList[index].nodeId = index;
        nodeInfoList->nodeList[index].port = 0;
        nodeInfoList->nodeList[index].status = NODE_STATUS_OK;
    }

    auto ret = CmServerZkRecordNodeList(nodeInfoList);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_server_zk_record_state_list)
{
    LOG_INFO("test_cm_server_zk_record_state_list");
    NodeStateList *nodeStateList = (NodeStateList*)malloc(sizeof(NodeStateList) + sizeof(NodeStateInfo) * NO_3);
    nodeStateList->poolId = 0;
    nodeStateList->nodeNum = NO_3;
    nodeStateList->masterNodeId = 0;
    for (uint16_t i = 0; i < NO_3; ++i) {
        nodeStateList->nodeList[i].nodeId = i;
        nodeStateList->nodeList[i].state = NODE_STATE_UP;
        nodeStateList->nodeList[i].clusterState = NODE_CLUSTER_STATE_IN;
        nodeStateList->nodeList[i].diskNum = NO_3;
    }

    auto ret = CmServerZkRecordStateList(nodeStateList);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_server_zk_get_node_list)
{
    LOG_INFO("test_cm_server_zk_record_state_list");
    NodeInfoList *nodeInfoList = (NodeInfoList *)malloc(sizeof(NodeInfo) * NO_3 + sizeof(nodeInfoList));

    auto ret = CmServerZkGetNodeList(nodeInfoList);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_server_zk_get_state_list)
{
    LOG_INFO("test_cm_server_zk_get_state_list");
    NodeStateList *nodeStateList = (NodeStateList*)malloc(sizeof(NodeStateList) + sizeof(NodeStateInfo) * NO_3);

    auto ret = CmServerZkGetStateList(nodeStateList);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_server_zk_get_pt_list)
{
    LOG_INFO("test_cm_server_zk_get_pt_list");
    PtEntryList *ptEntryList = (PtEntryList *)malloc(sizeof(PtEntry) * NO_5 + sizeof(PtEntryList));

    auto ret = CmServerZkGetPtEntryList(ptEntryList);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_server_zk_get_node_info)
{
    LOG_INFO("test_cm_server_zk_get_node_info");
    uint16_t poolId = 0;
    NodeInfo nodeInfo;

    auto ret = CmServerZkGetNodeInfo(poolId, &nodeInfo);
    EXPECT_EQ(ret, CM_OK);
}


TEST_F(TestCluster, test_cm_server_zk_init)
{
    LOG_INFO("test_cm_server_zk_init");
    auto ret = CmServerZkInit();
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCluster, test_cm_zk_create)
{
    LOG_INFO("test_cm_zk_create");
    zhandle_t *zh;
    const char *path;
    const char *value;
    int valuelen;
    const struct ACL_vector *acl;
    int mode;
    char *pathBuffer;
    int pathBufferLen;

    auto ret = CmZkCreate(zh, path, value, valuelen, acl, mode, pathBuffer, pathBufferLen);
    EXPECT_EQ(ret, ZOK);
}

TEST_F(TestCluster, test_cm_zk_delete)
{
    LOG_INFO("test_cm_zk_delete");
    zhandle_t *zh;
    const char *path;
    int version;

    auto ret = CmZkDelete(zh, path, version);
    EXPECT_EQ(ret, ZOK);
}

TEST_F(TestCluster, test_cm_zk_get)
{
    LOG_INFO("test_cm_zk_get");
    zhandle_t *zh;
    const char *path;
    int watch;
    char buffer[128] = {0};
    int bufferLen = (int)sizeof(uint64_t);
    struct Stat *stat;

    auto ret = CmZkGet(zh, path, watch, buffer, &bufferLen, stat);
    EXPECT_EQ(ret, ZOK);
}

TEST_F(TestCluster, test_cm_zk_exists)
{
    LOG_INFO("test_cm_zk_exists");
    zhandle_t *zh;
    const char *path;
    int watch;
    struct Stat *stat;

    auto ret = CmZkExists(zh, path, watch, stat);
    EXPECT_EQ(ret, ZNONODE);
}

TEST_F(TestCluster, test_cm_zk_set)
{
    LOG_INFO("test_cm_zk_set");
    zhandle_t *zh;
    const char *path;
    char *buffer;
    int bufferLen;
    int version;

    auto ret = CmZkSet(zh, path, buffer, bufferLen, version);
    EXPECT_EQ(ret, ZOK);
}
