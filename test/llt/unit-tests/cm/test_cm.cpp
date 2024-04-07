/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"
#include "cm.h"
#include "securec.h"
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

int32_t CM_RegLocalNodeQueryOpHandle_Stub(uint16_t poolId, LocalNodeQueryOpHandle *handle)
{
    gLocalQuery.queryLocalNodeInfo = handle->queryLocalNodeInfo;
    gLocalQuery.queryLocalNodeMr = handle->queryLocalNodeMr;
    gLocalQuery.ctx = handle->ctx;
    return 0;
}

static NodeListChangeOpHandle gNodeChange = { nullptr, nullptr };

int32_t CM_RegNodeListChangeNotifyHandle_Stub(uint16_t poolId, NodeListChangeOpHandle *handle)
{
    gNodeChange.notifyNodeListChange = handle->notifyNodeListChange;
    gNodeChange.ctx = handle->ctx;
    return 0;
}

static PtViewChangeOpHandle gPtChange = { nullptr, nullptr };

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
    gLocalQuery.queryLocalNodeInfo(&gNodeInfo, gLocalQuery.ctx);
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

int32_t CM_Init_Stub(ConfigRole role, PoolInfo *pools, uint16_t num, const CmCfgInfo *cfgInfo)
{
    constexpr int8_t ERR_2 = -2;
    constexpr int8_t ERR_3 = -3;

    if (pools == nullptr || num != 1) {
        return -1;
    }
    if (gLocalQuery.queryLocalNodeInfo == nullptr || gNodeChange.notifyNodeListChange == nullptr ||
        gPtChange.notifyPtListChange == nullptr) {
        return ERR_2;
    }
    uint16_t nodeNum = 2U;
    auto nodeList = (NodeStateList *)malloc(sizeof(NodeStateList) + sizeof(NodeStateInfo) * nodeNum);
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