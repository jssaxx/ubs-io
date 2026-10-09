/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"

#define private public
#include "wcache_manager.h"
#undef private

using namespace ock::bio;

TEST(TestWCacheFaultCleanup, cleanup_without_matching_flow_is_noop)
{
    WCacheManager manager;

    EXPECT_EQ(manager.CleanupFaultedDiskFlows(7), BIO_OK);
    EXPECT_TRUE(manager.mWCacheManager.empty());
    EXPECT_TRUE(manager.mReuseFlows.empty());
}

TEST(TestWCacheFaultCleanup, collect_and_unregister_include_all_historical_flows_on_failed_disk)
{
    constexpr uint16_t failedDiskId = 7;
    constexpr uint16_t healthyDiskId = 8;
    constexpr uint64_t historicalFlowId = 101;
    constexpr uint64_t currentFlowId = 102;
    constexpr uint64_t healthyFlowId = 103;
    WCacheManager manager;
    auto historicalFlow = MakeRef<WCache>(1, historicalFlowId, 3, 1, failedDiskId, false);
    auto currentFlow = MakeRef<WCache>(2, currentFlowId, 4, 9, failedDiskId, false);
    auto healthyFlow = MakeRef<WCache>(3, healthyFlowId, 5, 2, healthyDiskId, false);
    ASSERT_NE(historicalFlow, nullptr);
    ASSERT_NE(currentFlow, nullptr);
    ASSERT_NE(healthyFlow, nullptr);

    manager.mWCacheManager.emplace(historicalFlowId, historicalFlow);
    manager.mWCacheManager.emplace(currentFlowId, currentFlow);
    manager.mWCacheManager.emplace(healthyFlowId, healthyFlow);
    manager.mDestroyManager.emplace(historicalFlowId, 1);
    manager.mDestroyManager.emplace(currentFlowId, 1);
    manager.mDestroyManager.emplace(healthyFlowId, 1);
    manager.mRetryManager[WCACHE_MEMORY] = { historicalFlowId, healthyFlowId, currentFlowId };
    manager.mReuseFlows[3].insert(historicalFlowId);
    manager.mReuseFlows[4].insert(currentFlowId);
    manager.mReuseFlows[5].insert(healthyFlowId);

    std::unordered_map<uint16_t, std::list<WCachePtr>> faultedFlowsByPt;
    std::unordered_map<uint16_t, std::unordered_set<uint64_t>> flowIdsByPt;
    manager.CollectFaultedFlows(failedDiskId, faultedFlowsByPt, flowIdsByPt);

    ASSERT_EQ(faultedFlowsByPt.size(), 2);
    EXPECT_EQ(flowIdsByPt[3], std::unordered_set<uint64_t>({ historicalFlowId }));
    EXPECT_EQ(flowIdsByPt[4], std::unordered_set<uint64_t>({ currentFlowId }));
    EXPECT_TRUE(historicalFlow->IsStandaloneFault());
    EXPECT_TRUE(currentFlow->IsStandaloneFault());
    EXPECT_FALSE(historicalFlow->GetState());
    EXPECT_FALSE(currentFlow->GetState());
    EXPECT_FALSE(healthyFlow->IsStandaloneFault());
    EXPECT_TRUE(healthyFlow->GetState());

    for (const auto &entry : faultedFlowsByPt) {
        EXPECT_EQ(manager.UnregisterFaultedFlows(entry.first, entry.second), BIO_OK);
    }
    EXPECT_EQ(manager.mWCacheManager.size(), 1);
    EXPECT_EQ(manager.mWCacheManager[healthyFlowId], healthyFlow);
    EXPECT_EQ(manager.mDestroyManager.count(historicalFlowId), 0);
    EXPECT_EQ(manager.mDestroyManager.count(currentFlowId), 0);
    EXPECT_EQ(manager.mDestroyManager.count(healthyFlowId), 1);
    EXPECT_EQ(manager.mReuseFlows.count(3), 0);
    EXPECT_EQ(manager.mReuseFlows.count(4), 0);
    EXPECT_EQ(manager.mReuseFlows[5], std::unordered_set<uint64_t>({ healthyFlowId }));
    const auto &retryFlows = manager.mRetryManager[WCACHE_MEMORY];
    EXPECT_EQ(std::find(retryFlows.begin(), retryFlows.end(), historicalFlowId), retryFlows.end());
    EXPECT_EQ(std::find(retryFlows.begin(), retryFlows.end(), currentFlowId), retryFlows.end());
    EXPECT_NE(std::find(retryFlows.begin(), retryFlows.end(), healthyFlowId), retryFlows.end());
}
