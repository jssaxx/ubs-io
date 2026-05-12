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

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>

#include "server/pt/cm_pt_calc.h"
#include "server/pt/cm_pt_calc_fixed.h"
#include "server/pt/cm_pt_calc_fixed_state.h"
#include "server/pt/cm_pt_store.h"

namespace {
constexpr uint16_t TEST_POOL_ID = 0;
constexpr uint16_t TEST_NODE_NUM = 4;
constexpr uint16_t TEST_PT_NUM = 8;
constexpr uint16_t TEST_COPY_NUM = 2;
constexpr uint16_t TEST_MIN_COPY_NUM = 1;

NodeInfoList *AllocNodeInfoList(uint16_t nodeNum)
{
    auto *list = static_cast<NodeInfoList *>(calloc(1, sizeof(NodeInfoList) + sizeof(NodeInfo) * nodeNum));
    list->poolId = TEST_POOL_ID;
    list->nodeNum = nodeNum;
    for (uint16_t i = 0; i < nodeNum; ++i) {
        NodeInfo *info = &list->nodeList[i];
        info->nodeId = i;
        info->status = NODE_STATUS_OK;
        info->diskList.num = 1;
        info->diskList.list[0].diskId = 0;
        info->diskList.list[0].state = DISK_STATE_NORMAL;
        info->netList.num = 1;
        info->netList.list[0].state = NET_STATE_NORMAL;
    }
    return list;
}

NodeStateList *AllocNodeStateList(uint16_t nodeNum)
{
    auto *list = static_cast<NodeStateList *>(calloc(1, sizeof(NodeStateList) + sizeof(NodeStateInfo) * nodeNum));
    list->poolId = TEST_POOL_ID;
    list->nodeNum = nodeNum;
    list->masterNodeId = 0;
    for (uint16_t i = 0; i < nodeNum; ++i) {
        NodeStateInfo *state = &list->nodeList[i];
        state->nodeId = i;
        state->state = NODE_STATE_UP;
        state->clusterState = NODE_CLUSTER_STATE_IN;
        state->diskNum = 1;
        state->diskList[0].diskId = 0;
        state->diskList[0].clusterState = DISK_CLUSTER_STATE_IN;
    }
    return list;
}

PtEntryList *AllocPtEntryList(uint16_t ptNum)
{
    auto *list = static_cast<PtEntryList *>(calloc(1, sizeof(PtEntryList) + sizeof(PtEntry) * ptNum));
    list->poolId = TEST_POOL_ID;
    list->ptNum = ptNum;
    list->maxCopyNum = TEST_COPY_NUM;
    list->minCopyNum = TEST_MIN_COPY_NUM;
    return list;
}

void FreePtFixtures(NodeInfoList *nodeList, NodeStateList *stateList, PtEntryList *ptList)
{
    free(nodeList);
    free(stateList);
    free(ptList);
}
}  // namespace

TEST(TestPtCalc, test_fixed_calculator_initial_rebalance_and_need_check)
{
    NodeInfoList *nodeList = AllocNodeInfoList(TEST_NODE_NUM);
    NodeStateList *stateList = AllocNodeStateList(TEST_NODE_NUM);
    PtEntryList *ptList = AllocPtEntryList(TEST_PT_NUM);
    Calculator calc = CreateViewCalculator(TEST_NODE_NUM, TEST_PT_NUM, TEST_COPY_NUM, TEST_MIN_COPY_NUM);
    ASSERT_NE(calc, nullptr);

    EXPECT_EQ(ViewCalculatorInitial(calc, nodeList, stateList, ptList), CM_OK);
    EXPECT_EQ(ptList->ptNum, TEST_PT_NUM);
    EXPECT_EQ(ptList->maxCopyNum, TEST_COPY_NUM);
    EXPECT_EQ(ptList->poolId, TEST_POOL_ID);
    for (uint16_t ptId = 0; ptId < TEST_PT_NUM; ++ptId) {
        EXPECT_EQ(ptList->ptEntryList[ptId].state, PT_STATE_NORMAL);
        EXPECT_EQ(ptList->ptEntryList[ptId].copyNum, TEST_COPY_NUM);
        EXPECT_NE(ptList->ptEntryList[ptId].masterNodeId, NODE_ID_INVALID);
    }

    EXPECT_EQ(ViewCalculatorNeedRebalance(calc, nodeList, stateList, ptList), 0);

    stateList->nodeList[1].clusterState = NODE_CLUSTER_STATE_OUT;
    EXPECT_EQ(ViewCalculatorNeedRebalance(calc, nodeList, stateList, ptList), 1);
    EXPECT_EQ(ViewCalculatorRebalance(calc, nodeList, stateList, ptList), CM_OK);

    DestoryViewCalculator(calc);
    FreePtFixtures(nodeList, stateList, ptList);
}

TEST(TestPtCalc, test_fixed_calculator_failure_paths)
{
    NodeInfoList *nodeList = AllocNodeInfoList(TEST_NODE_NUM);
    NodeStateList *stateList = AllocNodeStateList(TEST_NODE_NUM);
    PtEntryList *ptList = AllocPtEntryList(TEST_PT_NUM);
    Calculator calc = CreateViewCalculator(TEST_NODE_NUM, TEST_PT_NUM, TEST_COPY_NUM, TEST_MIN_COPY_NUM);
    ASSERT_NE(calc, nullptr);

    for (uint16_t i = 1; i < TEST_NODE_NUM; ++i) {
        stateList->nodeList[i].state = NODE_STATE_DOWN;
    }

    EXPECT_EQ(ViewCalculatorInitial(calc, nodeList, stateList, ptList), CM_ERR);
    EXPECT_EQ(ViewCalculatorNeedRebalance(calc, nodeList, stateList, ptList), 0);

    DestoryViewCalculator(calc);
    FreePtFixtures(nodeList, stateList, ptList);
}

TEST(TestPtCalc, test_update_node_state_and_finish)
{
    PtEntryList *ptList = AllocPtEntryList(2);
    ptList->globalVersion = 7;
    for (uint16_t ptId = 0; ptId < ptList->ptNum; ++ptId) {
        PtEntry *entry = &ptList->ptEntryList[ptId];
        entry->birthVersion = ptList->globalVersion;
        entry->ptId = ptId;
        entry->state = PT_STATE_NORMAL;
        entry->copyNum = TEST_COPY_NUM;
        entry->masterNodeId = 0;
        entry->masterDiskId = 0;
        entry->copyList[0] = {0, 0, PT_COPY_STATE_RUNNING, 0};
        entry->copyList[1] = {1, 0, PT_COPY_STATE_RUNNING, 0};
    }

    NodeInfo nodeInfo;
    (void)memset(&nodeInfo, 0, sizeof(nodeInfo));
    nodeInfo.diskList.num = 1;
    nodeInfo.diskList.list[0].diskId = 0;
    nodeInfo.diskList.list[0].state = DISK_STATE_NORMAL;
    nodeInfo.netList.num = 1;
    nodeInfo.netList.list[0].state = NET_STATE_NORMAL;

    int32_t changed = 0;
    ViewPtEntryListUpdateNodeState(1, NODE_STATE_DOWN, &nodeInfo, ptList, &changed);
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(ptList->ptEntryList[0].copyList[1].state, PT_COPY_STATE_DOWN);
    EXPECT_EQ(ptList->ptEntryList[0].state, PT_STATE_DEGRADE_LOSS1);

    changed = 0;
    ViewPtEntryListUpdateNodeState(1, NODE_STATE_UP, &nodeInfo, ptList, &changed);
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(ptList->ptEntryList[0].copyList[1].state, PT_COPY_STATE_RECOVERY);

    CmPtFinish finish[2] = {
        {ptList->ptEntryList[0].birthVersion, 0, 0},
        {ptList->ptEntryList[1].birthVersion, 1, 0},
    };
    int32_t finishChanged = 0;
    ViewPtEntryListUpdateNodeFinish(1, finish, 2, ptList, &finishChanged, TEST_NODE_NUM);
    EXPECT_EQ(finishChanged, 1);
    EXPECT_EQ(ptList->ptEntryList[0].copyList[1].state, PT_COPY_STATE_RUNNING);

    free(ptList);
}

TEST(TestPtCalc, test_pt_store_compare_and_ops)
{
    PtEntryList *ptList = AllocPtEntryList(2);
    ptList->globalVersion = 1;
    for (uint16_t ptId = 0; ptId < ptList->ptNum; ++ptId) {
        PtEntry *entry = &ptList->ptEntryList[ptId];
        entry->birthVersion = ptList->globalVersion;
        entry->ptId = ptId;
        entry->state = PT_STATE_NORMAL;
        entry->copyNum = TEST_COPY_NUM;
        entry->masterNodeId = 0;
        entry->masterDiskId = 0;
        entry->copyList[0] = {0, 0, PT_COPY_STATE_RUNNING, 0};
        entry->copyList[1] = {1, 0, PT_COPY_STATE_RUNNING, 0};
    }

    EXPECT_EQ(ViewStorePtEntryIsSame(&ptList->ptEntryList[0], &ptList->ptEntryList[0]), 1);
    PtEntry different = ptList->ptEntryList[0];
    different.copyList[1].state = PT_COPY_STATE_DOWN;
    EXPECT_EQ(ViewStorePtEntryIsSame(&ptList->ptEntryList[0], &different), 0);

    StoreOps *ops = CmPtStoreOpsGet();
    ASSERT_NE(ops, nullptr);
    Storer store = ops->createStorer(TEST_NODE_NUM, TEST_PT_NUM, TEST_COPY_NUM);
    ASSERT_NE(store, nullptr);

    EXPECT_EQ(ops->initial(store, ptList), CM_OK);
    EXPECT_EQ(ops->loadcheck(store, ptList), CM_OK);
    ptList->globalVersion++;
    ptList->ptEntryList[1].birthVersion = ptList->globalVersion;
    ptList->ptEntryList[1].copyList[1].state = PT_COPY_STATE_DOWN;
    EXPECT_EQ(ops->update(store, ptList), CM_OK);
    EXPECT_EQ(ops->update(store, ptList), CM_OK);

    ops->destoryStorer(store);
    free(ptList);
}
