/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"
#include "bio_types.h"
#include "cm.h"
#include "securec.h"
#include "zookeeper.h"
#include "cm_zkadapter.h"
#include "cm_config.h"
#include "common/cm_inner.h"
#include "client/cm_client_init.h"
#include "client/cm_client_local.h"
#include "server/pt/cm_pt_calc_fixed_state.h"
#include "server/pt/cm_pt_store.h"
#include "server/cm_server_init.h"
#include "server/cm_server_view.h"
#include "server/cm_server_monitor.h"
#include "test_cm.h"

using namespace ock::bio;
bool TestCm::gSetup = false;

void TestCm::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestCm::TearDown()
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

    uint16_t nodeNum = NO_2;
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
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < gNodeInfo.diskList.num; diskIdx++) {
        ptList->ptEntryList[diskIdx].birthVersion = 1;
        ptList->ptEntryList[diskIdx].ptId = diskIdx;
        ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptList->ptEntryList[diskIdx].masterNodeId = 0;
        ptList->ptEntryList[diskIdx].masterDiskId = gNodeInfo.diskList.list[diskIdx].diskId;
        ptList->ptEntryList[diskIdx].referNum = 0;
        ptList->ptEntryList[diskIdx].copyNum = 1;
        ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptList->ptEntryList[diskIdx].copyList[0].diskId = gNodeInfo.diskList.list[diskIdx].diskId;
        ptList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
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
        pool->type = DISK_TYPE_DRAM;
        pool->redundance = PT_REP_DOUBLE;
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
        ptList->changeVersion = 1;
        for (uint16_t diskIdx = 0; diskIdx < gNodeInfo.diskList.num; diskIdx++) {
            ptList->ptEntryList[diskIdx].birthVersion = 1;
            ptList->ptEntryList[diskIdx].ptId = diskIdx;
            ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
            ptList->ptEntryList[diskIdx].masterNodeId = 0;
            ptList->ptEntryList[diskIdx].masterDiskId = gNodeInfo.diskList.list[diskIdx].diskId;
            ptList->ptEntryList[diskIdx].referNum = 0;
            ptList->ptEntryList[diskIdx].copyNum = 1;
            ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
            ptList->ptEntryList[diskIdx].copyList[0].diskId = gNodeInfo.diskList.list[diskIdx].diskId;
            ptList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
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
        if (ret != BIO_OK) {
            return BIO_ERR;
        }
        ret = strcat_s(tmp, newLen, "/0");
        if (ret != BIO_OK) {
            return BIO_ERR;
        }
        strings->data[0] = tmp;
        gZkGetChildrenFirst.push_back(path);
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

void TestCm::Stub()
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

TEST_F(TestCm, test_cm_inner_init)
{
    LOG_INFO("test_cm_inner_init");
    CmOptions mOptions;
    mOptions.role = ROLE_TOGETHER;
    mOptions.zkIpMask = "127.0.0.1:2181";
    mOptions.groups.groupId = 0;
    mOptions.groups.replicaNum = NO_2;
    mOptions.groups.initialNodeNum = NO_3;
    mOptions.groups.maxNodeNum = NO_256;
    mOptions.groups.maxPtNum = NO_16;
    mOptions.hbTempTimeout = NO_30;
    mOptions.hbPermFaultTime = NO_60;

    PoolInfo pools;
    int32_t ret = strcpy_s(pools.poolName, POOL_NAME_LEN, "bio_tester");
    EXPECT_EQ(ret, 0);
    pools.poolId = mOptions.groups.groupId;
    pools.type = DISK_TYPE_DRAM;
    pools.redundance = (mOptions.groups.replicaNum == NO_2) ? PT_REP_DOUBLE : PT_REP_TRIPLE;
    pools.initialNodeNum = mOptions.groups.initialNodeNum;
    pools.maxNodeNum = mOptions.groups.maxNodeNum;
    pools.maxPtNum = mOptions.groups.maxPtNum;

    CmCfgInfo cfgInfo;
    cfgInfo.zkIpMask = const_cast<char *>(mOptions.zkIpMask.c_str());
    cfgInfo.ipStr = "127.0.0.1";
    cfgInfo.regTimeOut = mOptions.hbTempTimeout * NO_1000;
    cfgInfo.regPermTimeOut = mOptions.hbPermFaultTime * NO_1000;
    ret = CmConfigInit(CONFIG_ROLE_TOGETHER, &pools, 1, &cfgInfo);
    EXPECT_EQ(ret, CM_OK);

    ret = CmZkInit();
    EXPECT_EQ(ret, CM_OK);

    ret = CM_ClientInit();
    EXPECT_EQ(ret, CM_OK);

    ret = CM_ServerInit();
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_get_node_mr)
{
    LOG_INFO("test_cm_get_node_mr");
    NodeMetaBuff *mr = (NodeMetaBuff *) malloc(NODE_META_BUFF_LEN);
    mr->nodeId = UINT16_MAX;
    int32_t ret = CM_GetNodeMr(NO_512, mr);
    EXPECT_EQ(ret, CM_ERR);

    ret = CM_GetNodeMr(0, mr);
    EXPECT_EQ(ret, CM_ERR);

    mr->nodeId = 0;
    ret = CM_GetNodeMr(0, mr);
    EXPECT_EQ(ret, CM_OK);

    free(mr);
}

TEST_F(TestCm, test_cm_get_node_session)
{
    LOG_INFO("test_cm_get_node_session");
    uint64_t sessionId = 0;
    int32_t ret = CmClientZkGetNodeSession(0, 0, &sessionId);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_node_event_check)
{
    LOG_INFO("test_cm_node_event_check");
    uint16_t poolId = 0;
    uint16_t nodeId = 0;
    int32_t ret = CmClientZkNodeEventExistCheck(poolId, nodeId);
    EXPECT_EQ(ret, CM_NOT_EXIST);
}

TEST_F(TestCm, test_cm_pt_event_check)
{
    LOG_INFO("test_cm_pt_event_check");
    uint16_t poolId = 0;
    uint16_t nodeId = 0;
    int32_t ret = CmClientZkPtEventExistCheck(poolId, nodeId);
    EXPECT_EQ(ret, CM_NOT_EXIST);
}

TEST_F(TestCm, test_cm_record_node_event)
{
    LOG_INFO("test_cm_record_node_event");
    CmNodeEvent nodeEvent;
    nodeEvent.poolId = 0;
    nodeEvent.nodeId = 0;
    int32_t ret = CmClientZkRecordNodeEvent(&nodeEvent);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_record_pt_event)
{
    LOG_INFO("test_cm_record_pt_event");
    CmPtEvent ptEvent;
    ptEvent.poolId = 0;
    ptEvent.nodeId = 0;
    ptEvent.ptNum = 0;
    int32_t ret = CmClientZkRecordPtEvent(&ptEvent);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_report_disk_status_fault)
{
    LOG_INFO("test_cm_report_disk_status_fault");
    uint16_t diskId = 0;
    int32_t ret = CmReportDiskStatus(diskId, CM_DISK_FAULT);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_report_disk_status_normal)
{
    LOG_INFO("test_cm_report_disk_status_normal");
    uint16_t diskId = 0;
    int32_t ret = CmReportDiskStatus(diskId, CM_DISK_NORMAL);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_server_view_role_change_slave)
{
    LOG_INFO("test_cm_server_view_role_change_slave");
    int32_t ret = CmServerViewRoleChange(CM_SERVER_SLAVE);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_server_view_role_change_master)
{
    LOG_INFO("test_cm_server_view_role_change_master");
    int32_t ret = CmServerViewRoleChange(CM_SERVER_MASTER);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_server_view_role_change_master_ptnum_0)
{
    LOG_INFO("test_cm_server_view_role_change_master_ptnum_0");
    PoolInfo pools;
    int32_t ret = strcpy_s(pools.poolName, POOL_NAME_LEN, "bio_tester");
    EXPECT_EQ(ret, 0);
    pools.poolId = 0;
    pools.type = DISK_TYPE_DRAM;
    pools.redundance = PT_REP_DOUBLE;
    pools.initialNodeNum = NO_3;
    pools.maxNodeNum = NO_256;
    pools.maxPtNum = NO_16;

    auto nodeList = (NodeStateList *)malloc(sizeof(NodeStateList) + sizeof(NodeStateInfo) * NO_2);
    InitNodeList(&pools, nodeList, NO_2);
    gNodeChange.notifyNodeListChange(nodeList, gNodeChange.ctx);
    free(nodeList);

    auto ptList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * gNodeInfo.diskList.num);
    ptList->poolId = pools.poolId;
    ptList->ptNum = 0;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < gNodeInfo.diskList.num; diskIdx++) {
        ptList->ptEntryList[diskIdx].birthVersion = 1;
        ptList->ptEntryList[diskIdx].ptId = diskIdx;
        ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptList->ptEntryList[diskIdx].masterNodeId = 0;
        ptList->ptEntryList[diskIdx].masterDiskId = gNodeInfo.diskList.list[diskIdx].diskId;
        ptList->ptEntryList[diskIdx].referNum = 0;
        ptList->ptEntryList[diskIdx].copyNum = 1;
        ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptList->ptEntryList[diskIdx].copyList[0].diskId = gNodeInfo.diskList.list[diskIdx].diskId;
        ptList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
    }
    gPtChange.notifyPtListChange(ptList, gPtChange.ctx);
    ret = CmServerViewRoleChange(CM_SERVER_MASTER);
    EXPECT_EQ(ret, CM_OK);
    ptList->ptNum = gNodeInfo.diskList.num;
    gPtChange.notifyPtListChange(ptList, gPtChange.ctx);
    free(ptList);
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

TEST_F(TestCm, test_cm_server_view_change_case_return_ok)
{
    LOG_INFO("test_cm_server_view_change_case_return_ok");
    CmNodeIdList *nodeList = (CmNodeIdList *)malloc(sizeof(CmNodeIdList) + sizeof(uint16_t));
    nodeList->poolId = 0;
    nodeList->nodeNum = NO_256;
    nodeList->nodeList[0] = 0;
    int32_t ret = CmServerViewNodeListChange(nodeList);
    EXPECT_EQ(ret, CM_OK);
    free(nodeList);

    ret = CmServerMonitorInit();
    EXPECT_EQ(ret, CM_OK);
}

void TestCm::CancelNodeStub()
{
    MOCKER(CmServerCancelNodeFault).stubs().will(invoke(CmServerCancelNodeFaultStub));
    MOCKER(CmServerMonitorInitMgr).stubs().will(invoke(CmServerMonitorInitMgrStub));
    MOCKER(CmServerMonitorLoadPool).stubs().will(invoke(CmServerMonitorLoadPoolStub));
}

TEST_F(TestCm, test_cm_server_monitor_expire_case_return_ok)
{
    LOG_INFO("test_cm_server_monitor_expire_case_return_ok");
    TestCm::CancelNodeStub();
    CmNodeIdList *nodeList = (CmNodeIdList *)malloc(sizeof(CmNodeIdList) + sizeof(uint16_t));
    nodeList->poolId = 0;
    nodeList->nodeNum = NO_256;
    nodeList->nodeList[0] = 0;
    int32_t ret = CmServerViewNodeListChange(nodeList);
    EXPECT_EQ(ret, CM_OK);
    free(nodeList);

    ret = CmServerMonitorInit();
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_server_get_node_state_case_return_ok)
{
    LOG_INFO("test_cm_server_get_node_state_case_return_ok");
    NodeStateInfo state;
    state.nodeId = 0;
    int32_t ret = CmServerViewGetNodeState(0, &state);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_get_node_info)
{
    LOG_INFO("test_cm_get_node_info");
    NodeInfo nodeInfo;
    auto ret = CM_GetNodeInfo(0, &nodeInfo);
    EXPECT_EQ(ret, CM_OK);
}

static int32_t StubNodeChgHandler(NodeStateList *nodeList, void *ctx)
{
    return BIO_OK;
}

TEST_F(TestCm, test_cm_register_node_chg)
{
    LOG_INFO("test_cm_register_node_chg");
    NodeListChangeOpHandle handle = { StubNodeChgHandler, nullptr };
    int32_t ret = CM_RegNodeListChangeNotifyHandle(0, &handle);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_set_pt_finish)
{
    LOG_INFO("test_cm_set_pt_finish");
    PtFinish eventList = { 1, 1, 0 };
    int32_t ret = CM_SetPtFinishStatus(0, 1, &eventList);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_is_pt_same)
{
    LOG_INFO("test_cm_is_pt_same");
    PtEntry elem1 = { 1, 1, PT_STATE_NORMAL, 1, 1, 0, 2,
        {  { 1, 2, FALSE, PT_COPY_STATE_RUNNING },  { 1, 2, FALSE, PT_COPY_STATE_RUNNING } } };
    PtEntry elem2 = elem1;
    int32_t ret = ViewStorePtEntryIsSame(&elem1, &elem2);
    EXPECT_EQ(ret, TRUE);
}

TEST_F(TestCm, test_cm_get_node_id_by_path)
{
    LOG_INFO("test_cm_get_node_id_by_path");
    std::string path = "/zk/123";
    std::string pre = "/zk/";
    int32_t ret = CmClientZkGetNodeIdByPath(path.c_str(), pre.c_str());
    EXPECT_EQ(ret, 123U);
}

TEST_F(TestCm, test_cm_is_zk_pt_same)
{
    LOG_INFO("test_cm_is_zk_pt_same");
    PtEntry elem1 = { 1, 1, PT_STATE_NORMAL, 1, 1, 0, 2,
        {  { 1, 2, FALSE, PT_COPY_STATE_RUNNING },  { 1, 2, FALSE, PT_COPY_STATE_RUNNING } } };
    PtEntry elem2 = elem1;
    int32_t ret = CmClientZkPtEntryIsSame(&elem1, &elem2);
    EXPECT_EQ(ret, TRUE);
}