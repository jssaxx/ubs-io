/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <atomic>
#include <sched.h>
#include <stdexcept>
#include <unistd.h>
#include "test_common.h"
#include "ubsio_kvc_execution.h"

using namespace ock::ubsio;

namespace {
TEST_F(KvTest, ExecutorRunsTasksAndHandlesLifecycle)
{
    EXPECT_EQ(ExecutorService::Create(0).Get(), nullptr);
    EXPECT_EQ(ExecutorService::Create(ES_MAX_THR_NUM + 1).Get(), nullptr);

    auto executor = ExecutorService::Create(2, 8);
    ASSERT_NE(executor.Get(), nullptr);
    executor->SetThreadName("kvut");
    executor->SetCpuSetStartIndex(-1);
    EXPECT_TRUE(executor->Start());
    EXPECT_TRUE(executor->Start());

    RunnablePtr empty;
    EXPECT_FALSE(executor->Execute(empty));

    Runnable emptyRunnable;
    emptyRunnable.Run();

    std::atomic<int> calls{0};
    ASSERT_TRUE(executor->Execute([&calls]() { ++calls; }));
    ASSERT_TRUE(executor->Execute([&calls]() {
        ++calls;
        throw std::runtime_error("expected test error");
    }));
    ASSERT_TRUE(executor->Execute([&calls]() {
        ++calls;
        throw 7;
    }));
    ASSERT_TRUE(executor->Execute([&calls]() { ++calls; }));
    for (int retry = 0; retry < 10000 && calls.load() != 4; ++retry) {
        usleep(10);
    }
    EXPECT_EQ(calls.load(), 4);

    executor->Stop();
    executor->Stop();

    auto affinityExecutor = ExecutorService::Create(1, 2);
    ASSERT_NE(affinityExecutor.Get(), nullptr);
    auto cpuId = sched_getcpu();
    ASSERT_GE(cpuId, 0);
    affinityExecutor->SetCpuSetStartIndex(static_cast<int16_t>(cpuId));
    EXPECT_TRUE(affinityExecutor->Start());

    std::atomic<int> affinityCalls{0};
    ASSERT_TRUE(affinityExecutor->Execute([&affinityCalls]() { ++affinityCalls; }));
    for (int retry = 0; retry < 10000 && affinityCalls.load() == 0; ++retry) {
        usleep(10);
    }
    EXPECT_EQ(affinityCalls.load(), 1);
    affinityExecutor->Stop();
}
}
