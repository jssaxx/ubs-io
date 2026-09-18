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

#include <chrono>
#include <future>
#include <map>
#include <vector>
#include "gtest/gtest.h"
#include "flow.h"
#include "flow_manager.h"
#include "flow_task_pool.h"

using namespace ock::bio;

TEST(TestFlowBasics, UninitializedManagerRejectsOperations)
{
    FlowManager manager;
    std::map<uint64_t, FlowPtr> objects;
    std::function<void()> task = []() {};

    EXPECT_EQ(manager.CreateObject(FLOW_DATA, FLOW_MEMORY, 1, 0), nullptr);
    EXPECT_EQ(manager.DestroyObject(FLOW_MEMORY, 1), BIO_NOT_READY);
    EXPECT_EQ(manager.AbandonObject(1), BIO_NOT_READY);
    EXPECT_EQ(manager.GetAllObject(FLOW_MEMORY, objects), BIO_NOT_READY);
    EXPECT_EQ(manager.PreLoadObject(FLOW_MEMORY, task), BIO_NOT_READY);
    EXPECT_TRUE(objects.empty());
}

TEST(TestFlowBasics, EmptyFlowValidatesRangesAndSeal)
{
    Flow flow(FLOW_DATA, FLOW_MEMORY, 7, 0, 4096, 0);
    std::vector<FlowAddr> addrs;

    EXPECT_EQ(flow.GetFlowId(), 7U);
    EXPECT_EQ(flow.GetFlowType(), FLOW_MEMORY);
    EXPECT_EQ(flow.GetAllocatedLen(), 0U);
    EXPECT_EQ(flow.TruncateOffset(1), BIO_ERR);
    EXPECT_EQ(flow.ValidateAndPreloadRange(UINT64_MAX, 2), BIO_ERR);
    EXPECT_EQ(flow.BuildFlowAddrs(0, 1, addrs), BIO_INNER_ERR);
    EXPECT_TRUE(addrs.empty());
    EXPECT_EQ(flow.GetAddrByOffset(0, 0, addrs), BIO_OK);
    EXPECT_EQ(flow.GetValidLen(), 0U);
    EXPECT_EQ(flow.Seal(), BIO_OK);
    EXPECT_EQ(flow.Seal(), BIO_OK);
    EXPECT_EQ(flow.TruncateOffset(0), BIO_OK);
}

TEST(TestFlowBasics, TaskPoolExecutesAndStopsIdempotently)
{
    FlowTaskPool pool("unit");
    ASSERT_EQ(pool.Start(1, 16), BIO_OK);
    EXPECT_EQ(pool.Start(1, 16), BIO_OK);

    std::promise<int> completed;
    auto future = completed.get_future();
    FlowTaskHandler task = [&completed]() { completed.set_value(42); };
    ASSERT_EQ(pool.AddTask(task), BIO_OK);
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(future.get(), 42);

    pool.Stop();
    pool.Stop();
}
