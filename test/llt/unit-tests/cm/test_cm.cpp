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
#include "cm_server_init.h"
#include "cm_client_init.h"
#include "cm_client_local.h"
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

static LocalNodeQueryOpHandle gLocalQuery = {nullptr, nullptr, nullptr};

int32_t CM_RegLocalNodeQueryOpHandle_Stub(uint16_t poolId, LocalNodeQueryOpHandle *handle)
{
    gLocalQuery.queryLocalNodeInfo = handle->queryLocalNodeInfo;
    gLocalQuery.queryLocalNodeMr = handle->queryLocalNodeMr;
    gLocalQuery.ctx = handle->ctx;
    return 0;
}

static NodeListChangeOpHandle gNodeChange = {nullptr, nullptr};

int32_t CM_RegNodeListChangeNotifyHandle_Stub(uint16_t poolId, NodeListChangeOpHandle *handle)
{
    gNodeChange.notifyNodeListChange = handle->notifyNodeListChange;
    gNodeChange.ctx = handle->ctx;
    return 0;
}

static PtViewChangeOpHandle gPtChange = {nullptr, nullptr};

int32_t CM_RegPtViewChangeOpHandle_Stub(uint16_t poolId, PtViewChangeOpHandle *handle)
{
    gPtChange.notifyPtListChange = handle->notifyPtListChange;
    gPtChange.ctx = handle->ctx;
    return 0;
}

uint16_t CM_GetLocalNodeId_Stub(uint16_t poolId)
{
    return 0;
}

static NodeInfo gNodeInfo;

int32_t CM_GetNodeInfo_Stub(uint16_t poolId, NodeInfo *nodeInfo)
{
    uint16_t nodeId = nodeInfo->nodeId;
    memcpy_s(nodeInfo, sizeof(NodeInfo), &gNodeInfo, sizeof(NodeInfo));
    nodeInfo->nodeId = nodeId;
    return 0;
}

void InitNodeList(PoolInfo *pools, NodeStateList *nodeList, uint16_t nodeNum)
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

constexpr int8_t ERR_2 = -2;
constexpr int8_t ERR_3 = -3;

int32_t CM_Init_Stub(ConfigRole role, PoolInfo *pools, uint16_t num, const CmCfgInfo *cfgInfo)
{
    LOG_INFO("call CM_Init_Stub");
    if (pools == nullptr || num != 1) {
        return -1;
    }
    if (gNodeChange.notifyNodeListChange == nullptr ||
        gPtChange.notifyPtListChange == nullptr) {
        return ERR_2;
    }
    uint16_t nodeNum = 2;
    auto nodeList = (NodeStateList *) malloc(sizeof(NodeStateList) + sizeof(NodeStateInfo) * nodeNum);
    if (nodeList == nullptr) {
        return ERR_3;
    }
    InitNodeList(pools, nodeList, nodeNum);
    gNodeChange.notifyNodeListChange(nodeList, gNodeChange.ctx);
    free(nodeList);

    auto ptList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * gNodeInfo.diskList.num);
    if (ptList == nullptr) {
        return ERR_3;
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
    return 0;
}

void ZooSetDebugLevel()
{
    LOG_INFO("call ZooSetDebugLevel");
}

zhandle_t* ZookeeperInit()
{
    LOG_INFO("call ZookeeperInit");
    return (zhandle_t *) "zHandle";
}

int ZooState()
{
    LOG_INFO("call ZooState");
    return ZOO_CONNECTED_STATE;
}

int ZooRecvTimeout()
{
    LOG_INFO("call ZooRecvTimeout");
    return (int)NO_10;
}

int ZooCreate(zhandle_t *zh, const char *path, const char *value,
              int valuelen, const struct ACL_vector *acl, int mode,
              char *pathBuffer, int pathBufferLen)
{
    LOG_INFO("call ZooCreate");

    return ZOK;
}

int ZooGet(zhandle_t *zh, const char *path, int watch, char *buffer, int *bufferLen, struct Stat *stat)
{
    LOG_INFO("call ZooGet");
    if (*bufferLen == (int)sizeof(uint16_t)) {
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
    }
    return ZOK;
}

int ZooExists(zhandle_t *zh, const char *path, int watch, struct Stat *stat)
{
    LOG_INFO("call ZooExists");
    return ZNONODE;
}

int ZooSet(zhandle_t *zh, const char *path, const char *buffer, int buflen, int version)
{
    LOG_INFO("call ZooSet");
    return ZOK;
}

int ZooWget(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx, char *buffer, int *bufferLen,
    struct Stat *stat)
{
    LOG_INFO("call ZooWget");
    char *result = nullptr;
    char zkPath[CM_ZNODE_PATH_LEN] = { 0 };
    int ret = strcpy_s(zkPath, CM_ZNODE_PATH_LEN, path);
    if (ret != 0) {
        return ZOK;
    }
    if (result = strstr(zkPath, CM_NODE_LIST_PATH)) {
        NodeInfoList *nodeChangeList = (NodeInfoList *)buffer;
        nodeChangeList->poolId = 0;
        nodeChangeList->nodeNum =0;
    } else if (result = strstr(zkPath, CM_STATE_PATH)) {
        NodeStateList *nodeStatList = (NodeStateList *)buffer;
        nodeStatList->poolId = 0;
        nodeStatList->nodeNum = 0;
    } else if (result = strstr(zkPath, CM_PT_PATH)) {
        PtEntryList *ptList = (PtEntryList *)buffer;
        ptList->poolId = 0;
        ptList->ptNum = 0;
    } else if (result = strstr(zkPath, CM_MASTER_PATH)) {
        uint16_t *masterNodeId = (uint16_t *)buffer;
        *masterNodeId = 0;
    }
    return ZOK;
}

int ZooDelete(zhandle_t *zh, const char *path, int version)
{
    LOG_INFO("call ZooDelete");
    return ZOK;
}

int ZooWgetChildren(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx,
    struct String_vector *strings)
{
    LOG_INFO("call ZooWgetChildren");
    return ZOK;
}
int ZooDeallocateStringVector(struct String_vector *strings)
{
    if (strings != nullptr) {
        if (strings->data != nullptr) {
            free(strings->data);
            strings->data = nullptr;
        }
    }
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
    MOCKER(deallocate_String_vector).stubs().will(invoke(ZooDeallocateStringVector));
}

TEST_F(TestCm, test_cm_initialize)
{
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

    int32_t ret;
    PoolInfo pools;
    ret = strcpy_s(pools.poolName, POOL_NAME_LEN, "bio");
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

TEST_F(TestCm, test_cm_get_mr)
{
    NodeMetaBuff *mr = (NodeMetaBuff *)malloc(NODE_META_BUFF_LEN);
    mr->nodeId = 0;

    int ret = CM_GetNodeMr(0, mr);
    EXPECT_EQ(ret, CM_OK);
    free(mr);
}

TEST_F(TestCm, test_cm_get_node_session)
{
    uint64_t sessionId = 0;
    int ret = CmClientZkGetNodeSession(0, 0, &sessionId);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_node_event_check)
{
    int ret = CmClientZkNodeEventExistCheck(0, 0);
    EXPECT_EQ(ret, CM_NOT_EXIST);
}

TEST_F(TestCm, test_cm_pt_event_check)
{
    int ret = CmClientZkPtEventExistCheck(0, 0);
    EXPECT_EQ(ret, CM_NOT_EXIST);
}

TEST_F(TestCm, test_cm_record_node_event)
{
    CmNodeEvent nodeEvent;
    nodeEvent.poolId = 0;
    nodeEvent.nodeId = 0;
    int ret = CmClientZkRecordNodeEvent(&nodeEvent);
    EXPECT_EQ(ret, CM_OK);
}

TEST_F(TestCm, test_cm_record_pt_event)
{
    CmPtEvent ptEvent;
    ptEvent.poolId = 0;
    ptEvent.nodeId = 0;
    ptEvent.ptNum = 0;
    int ret = CmClientZkRecordPtEvent(&ptEvent);
    EXPECT_EQ(ret, CM_OK);
}