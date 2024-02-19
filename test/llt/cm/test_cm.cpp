//
// Created by root on 12/11/23.
//

#include "gtest/gtest.h"
#include <mockcpp/mokc.h>
#include <mockcpp/mockcpp.hpp>
#include "test_cm.h"
#include "cm.h"

bool TestCm::g_setup = false;

void TestCm::SetUp()
{
    if (g_setup) {
        return;
    }
    // XXX
    g_setup = true;
    return;
}

void TestCm::TearDown()
{
    return;
}

static LocalNodeQueryOpHandle g_localQuery = { nullptr, nullptr, nullptr };
int32_t CM_RegLocalNodeQueryOpHandle_Stub(uint16_t poolId, LocalNodeQueryOpHandle *handle)
{
    g_localQuery.queryLocalNodeInfo = handle->queryLocalNodeInfo;
    g_localQuery.queryLocalNodeMr = handle->queryLocalNodeMr;
    g_localQuery.ctx = handle->ctx;
    return 0;
}

static NodeListChangeOpHandle g_nodeChange = { nullptr, nullptr };
int32_t CM_RegNodeListChangeNotifyHandle_Stub(uint16_t poolId, NodeListChangeOpHandle *handle)
{
    g_nodeChange.notifyNodeListChange = handle->notifyNodeListChange;
    g_nodeChange.ctx = handle->ctx;
    return 0;
}

static PtViewChangeOpHandle g_ptChange = { nullptr, nullptr };
int32_t CM_RegPtViewChangeOpHandle_Stub(uint16_t poolId, PtViewChangeOpHandle *handle)
{
    g_ptChange.notifyPtListChange = handle->notifyPtListChange;
    g_ptChange.ctx = handle->ctx;
    return 0;
}

uint16_t CM_GetLocalNodeId_Stub(uint16_t poolId)
{
    return 0;
}

static NodeInfo g_nodeInfo;
int32_t CM_GetNodeInfo_Stub(uint16_t poolId, NodeInfo *nodeInfo)
{
    uint16_t nodeId = nodeInfo->nodeId;
    memcpy(nodeInfo, &g_nodeInfo, sizeof(NodeInfo));
    nodeInfo->nodeId = nodeId;
    return 0;
}

int32_t CM_Init_Stub(ConfigRole role, PoolInfo *pools, uint16_t num, const CmCfgInfo *cfgInfo)
{
    if (pools == nullptr || num != 1) {
        return -1;
    }
    if (g_localQuery.queryLocalNodeInfo == nullptr ||
        g_nodeChange.notifyNodeListChange == nullptr ||
        g_ptChange.notifyPtListChange == nullptr) {
        return -2;
    }
    uint16_t nodeNum = 2;
    NodeStateList *nodeList = (NodeStateList *)malloc(sizeof(NodeStateList) + sizeof(NodeStateInfo) * nodeNum);
    if (nodeList == nullptr) {
        return -3;
    }
    nodeList->poolId = pools->poolId;
    nodeList->nodeNum = nodeNum;
    g_localQuery.queryLocalNodeInfo(&g_nodeInfo, g_localQuery.ctx);
    for (uint32_t nodeId = 0; nodeId < nodeNum; nodeId++) {
        nodeList->masterNodeId = 0;
        nodeList->nodeList[nodeId].sessionId = 0;
        nodeList->nodeList[nodeId].nodeId = nodeId;
        nodeList->nodeList[nodeId].state = NODE_STATE_UP;
        nodeList->nodeList[nodeId].clusterState = NODE_CLUSTER_STATE_IN;
        nodeList->nodeList[nodeId].diskNum = g_nodeInfo.diskList.num;
        for (uint16_t diskIdx = 0; diskIdx < g_nodeInfo.diskList.num; diskIdx++) {
            nodeList->nodeList[nodeId].diskList[diskIdx].diskId = g_nodeInfo.diskList.list[diskIdx].diskId;
            nodeList->nodeList[nodeId].diskList[diskIdx].clusterState = DISK_CLUSTER_STATE_IN;
        }
    }
    g_nodeChange.notifyNodeListChange(nodeList, g_nodeChange.ctx);
    free(nodeList);

    PtEntryList *ptList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * g_nodeInfo.diskList.num);
    if (ptList == nullptr) {
        return -3;
    }
    ptList->poolId = pools->poolId;
    ptList->ptNum = g_nodeInfo.diskList.num;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < g_nodeInfo.diskList.num; diskIdx++) {
        ptList->ptEntryList[diskIdx].birthVersion = 1;
        ptList->ptEntryList[diskIdx].ptId = diskIdx;
        ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptList->ptEntryList[diskIdx].masterNodeId = 0;
        ptList->ptEntryList[diskIdx].masterDiskId = g_nodeInfo.diskList.list[diskIdx].diskId;
        ptList->ptEntryList[diskIdx].referNum = 0;
        ptList->ptEntryList[diskIdx].copyNum = 1;
        ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptList->ptEntryList[diskIdx].copyList[0].diskId = g_nodeInfo.diskList.list[diskIdx].diskId;
        ptList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
    }
    g_ptChange.notifyPtListChange(ptList, g_ptChange.ctx);
    free(ptList);
    return 0;
}

void TestCm::Stub()
{
    MOCKER(CM_RegLocalNodeQueryOpHandle).stubs().will(invoke(CM_RegLocalNodeQueryOpHandle_Stub));
    MOCKER(CM_RegNodeListChangeNotifyHandle).stubs().will(invoke(CM_RegNodeListChangeNotifyHandle_Stub));
    MOCKER(CM_RegPtViewChangeOpHandle).stubs().will(invoke(CM_RegPtViewChangeOpHandle_Stub));
    MOCKER(CM_GetLocalNodeId).stubs().will(invoke(CM_GetLocalNodeId_Stub));
    MOCKER(CM_GetNodeInfo).stubs().will(invoke(CM_GetNodeInfo_Stub));
    MOCKER(CM_Init).stubs().will(invoke(CM_Init_Stub));
}

TEST_F(TestCm, test_cm_initialize) {}