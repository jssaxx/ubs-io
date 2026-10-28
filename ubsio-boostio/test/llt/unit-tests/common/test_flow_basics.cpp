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
#include <list>
#include <map>
#include <memory>
#include <thread>
#include <vector>
#include "mockcpp/ApiHook.h"
#include "gtest/gtest.h"
#include "bio_log.h"
#include "bio_types.h"

#define private public
#include "bio_lock.h"
#undef private

#include "cache_overload_ctrl.h"

#define private public
#include "flow.h"
#include "flow_manager.h"
#include "flow_task_pool.h"
#undef private

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

TEST(TestFlowBasics, RejectedPreloadWakesWaiterAndAllowsAnotherAttempt)
{
    auto manager = FlowManager::Instance();
    FlowTaskPoolPtr previous;
    {
        std::lock_guard<std::mutex> lock(manager->mMutex);
        previous = manager->mTaskPool[FLOW_MEMORY];
        manager->mTaskPool[FLOW_MEMORY] = nullptr; // An unavailable pool rejects preload submission.
    }
    auto flow = MakeRef<Flow>(FLOW_DATA, FLOW_MEMORY, 7, 0, 4096, 0);
    auto attempt = [&]() {
        std::vector<FlowAddr> addrs;
        return flow->GetAddrByOffset(0, 4096, addrs);
    };
    auto result = std::async(std::launch::async, attempt);
    auto status = result.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(status, std::future_status::ready);
    if (status != std::future_status::ready) {
        flow->HoldClean(NO_MAX_VALUE64, BIO_INNER_RETRY, false); // Rescue a regressed implementation.
    }
    EXPECT_EQ(result.get(), BIO_INNER_RETRY);
    EXPECT_TRUE(flow->mHoldList.empty());
    EXPECT_FALSE(flow->mPreLoadFlag);
    if (status == std::future_status::ready) {
        EXPECT_EQ(attempt(), BIO_INNER_RETRY);
    }
    {
        std::lock_guard<std::mutex> lock(manager->mMutex);
        manager->mTaskPool[FLOW_MEMORY] = previous;
    }
}

namespace {
constexpr uint64_t FLOW_TEST_CHUNK = 4096;

struct CompletionUnlock {
    Flow *flow;
    uint64_t offset;
    std::promise<void> reached;
    std::shared_future<void> resume;
    bool observed{ false };
    bool idleBeforeUnlock{ false };
};

thread_local CompletionUnlock *gCompletionUnlock = nullptr;

void ObservePreloadUnlock(ReadWriteLock *lock)
{
    auto probe = gCompletionUnlock;
    bool completion = probe != nullptr && lock == &probe->flow->mLock && !probe->observed &&
        probe->flow->mPreLoadOffset >= probe->offset;
    if (completion) {
        probe->observed = true;
        probe->idleBeforeUnlock = !probe->flow->mPreLoadFlag;
    }
    pthread_rwlock_unlock(&lock->mLock);
    if (completion) {
        probe->reached.set_value();
        probe->resume.wait();
    }
}

class TestFlowPreloadHandoff : public testing::Test {
protected:
    void SetUp() override
    {
        manager = FlowManager::Instance();
        previousAllocator = FlowManager::mDiskAllocator;
        previousCacheType = FlowManager::mGetCacheType;
        previousUsed = FlowManager::mUsedSize[FLOW_WCACHE][FLOW_DISK][0].load();
        {
            std::lock_guard<std::mutex> guard(manager->mMutex);
            previousPool = manager->mTaskPool[FLOW_DISK];
        }
        pool = MakeRef<FlowTaskPool>("flow-handoff");
        ASSERT_NE(pool, nullptr);
        ASSERT_EQ(pool->Start(1, 16), BIO_OK);
        {
            std::lock_guard<std::mutex> guard(manager->mMutex);
            manager->mTaskPool[FLOW_DISK] = pool;
        }
        DiskAllocator allocator;
        allocator.alloc = [this](uint32_t, uint64_t, uint64_t, uint64_t, uint64_t *chunkId) {
            *chunkId = ++allocations;
            return onAllocate ? onAllocate() : BIO_OK;
        };
        allocator.free = [](uint32_t, uint64_t, uint64_t) {};
        FlowManager::RegisterDiskAllocator(allocator);
        FlowManager::RegisterGetCacheType([](uint64_t) { return FLOW_WCACHE; });
        flow = MakeRef<Flow>(FLOW_DATA, FLOW_DISK, 7, 0, FLOW_TEST_CHUNK, 2 * FLOW_TEST_CHUNK);
        flow->mPreLoadFlag = true; // The test starts the initial preload worker explicitly.
    }

    void TearDown() override
    {
        if (pool != nullptr) {
            pool->Stop();
        }
        {
            std::lock_guard<std::mutex> guard(manager->mMutex);
            manager->mTaskPool[FLOW_DISK] = previousPool;
        }
        FlowManager::RegisterDiskAllocator(previousAllocator);
        FlowManager::RegisterGetCacheType(previousCacheType);
        FlowManager::mUsedSize[FLOW_WCACHE][FLOW_DISK][0] = previousUsed;
    }

    bool WaitForState(size_t waiterCount, bool preloading)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                ReadLocker<ReadWriteLock> guard(&flow->mLock);
                if (flow->mHoldList.size() == waiterCount && flow->mPreLoadFlag == preloading) {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    void CheckResult(std::future<BResult> &result, BResult expected)
    {
        auto status = result.wait_for(std::chrono::seconds(2));
        EXPECT_EQ(status, std::future_status::ready);
        if (status != std::future_status::ready) {
            flow->HoldClean(NO_MAX_VALUE64, BIO_INNER_RETRY, false);
        }
        EXPECT_EQ(result.get(), expected);
    }

    FlowManagerPtr manager;
    FlowPtr flow;
    FlowTaskPoolPtr pool;
    FlowTaskPoolPtr previousPool;
    DiskAllocator previousAllocator;
    GetCacheType previousCacheType;
    uint64_t previousUsed{ 0 };
    std::atomic<uint64_t> allocations{ 0 };
    std::function<BResult()> onAllocate;
};
}

TEST_F(TestFlowPreloadHandoff, CompletionPublishesIdleBeforeUnlockAndNextRequestProgresses)
{
    std::promise<void> resume;
    CompletionUnlock probe{flow.Get(), 2 * FLOW_TEST_CHUNK, {}, resume.get_future().share()};
    auto reached = probe.reached.get_future();
    auto unlock = reinterpret_cast<void (*)(ReadWriteLock *)>(&ReadWriteLock::UnLock);
    // Scope the hook locally so unrelated global LLT mocks remain installed.
    mockcpp::ApiHook hook(reinterpret_cast<const void *>(unlock),
        reinterpret_cast<const void *>(ObservePreloadUnlock));
    auto worker = std::async(std::launch::async, [&]() {
        gCompletionUnlock = &probe;
        flow->PreLoadHandle();
        gCompletionUnlock = nullptr;
    });
    auto status = reached.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(status, std::future_status::ready);
    if (status != std::future_status::ready) {
        resume.set_value();
        worker.get();
        pool->Stop();
        return;
    }
    EXPECT_TRUE(probe.idleBeforeUnlock);

    // The old worker is still paused after unlocking. The new request must be
    // able to submit its own preload work instead of relying on that worker.
    auto request = std::async(std::launch::async, [&]() {
        return flow->ValidateAndPreloadRange(0, 4 * FLOW_TEST_CHUNK);
    });
    CheckResult(request, BIO_OK);
    resume.set_value();
    worker.get();
    pool->Stop(); // Quiesce the replacement worker before removing the unlock hook.
}

TEST_F(TestFlowPreloadHandoff, RequestsQueuedBeforeCompletionAreCoveredByCurrentWorker)
{
    std::promise<void> allocating;
    std::promise<void> resume;
    auto allocationStarted = allocating.get_future();
    auto resumeAllocation = resume.get_future().share();
    onAllocate = [&]() {
        if (allocations == 1) {
            allocating.set_value();
            resumeAllocation.wait();
        }
        return BIO_OK;
    };
    auto worker = std::async(std::launch::async, [&]() { flow->PreLoadHandle(); });
    auto status = allocationStarted.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(status, std::future_status::ready);
    auto first = std::async(std::launch::async, [&]() {
        return flow->ValidateAndPreloadRange(0, 4 * FLOW_TEST_CHUNK);
    });
    auto second = std::async(std::launch::async, [&]() {
        return flow->ValidateAndPreloadRange(0, 6 * FLOW_TEST_CHUNK);
    });
    EXPECT_TRUE(WaitForState(2, true));
    resume.set_value();
    worker.get();
    CheckResult(first, BIO_OK);
    CheckResult(second, BIO_OK);
    EXPECT_EQ(flow->mPreLoadOffset.load(), 8 * FLOW_TEST_CHUNK);
    EXPECT_FALSE(flow->mPreLoadFlag);
    EXPECT_TRUE(flow->mHoldList.empty());
}

TEST_F(TestFlowPreloadHandoff, AllocationFailureWakesAllQueuedRequests)
{
    onAllocate = []() { return BIO_ERR; };
    auto first = std::async(std::launch::async, [&]() {
        return flow->ValidateAndPreloadRange(0, 4 * FLOW_TEST_CHUNK);
    });
    auto second = std::async(std::launch::async, [&]() {
        return flow->ValidateAndPreloadRange(0, 6 * FLOW_TEST_CHUNK);
    });
    EXPECT_TRUE(WaitForState(2, true));
    flow->PreLoadHandle();
    CheckResult(first, BIO_INNER_RETRY);
    CheckResult(second, BIO_INNER_RETRY);
    EXPECT_EQ(flow->mPreLoadOffset.load(), 0U);
    EXPECT_TRUE(WaitForState(0, false));
}
