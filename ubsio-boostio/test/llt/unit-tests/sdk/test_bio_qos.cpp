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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>
#include <pthread.h>
#include "gtest/gtest.h"
#include "mockcpp/ApiHook.h"

#define private public
#define protected public
#include "flow.h"
#include "bio_client.h"
#include "bio_client_agent.h"
#include "bio_config_instance.h"
#include "bio_qos.h"
#include "cache_overload_ctrl.h"
#undef private
#undef protected

using namespace ock::bio;

namespace {
void IgnoreSignal(int) {}
std::atomic<uint64_t> observedVersion{0};
std::atomic<uint16_t> observedPt{0};
thread_local bool failPutRequestAllocation = false;

void *AllocatePutRequestForTest(std::size_t length, const std::nothrow_t &) noexcept
{
    if (failPutRequestAllocation && length == sizeof(PutRequest)) {
        return nullptr;
    }
    return ::operator new(length, std::nothrow);
}

int32_t RecordQuotaRequest(AllocQuotaRequest *req, AllocQuotaResponse *rsp)
{
    observedVersion = req->comm.ptv;
    observedPt = req->comm.ptId;
    rsp->exceptQuota = 128 * 1024 * 1024;
    return BIO_OK;
}

int32_t ThrowQuotaRequest(AllocQuotaRequest *, AllocQuotaResponse *)
{
    throw std::bad_alloc();
}

class TestBioQos : public testing::Test {
protected:
    void SetUp() override
    {
        client = MakeRef<MirrorClient>(STANDALONE);
        client->mBioQos = MakeRef<BioQos>();
        quota = MakeRef<BioQuota>(0, 0);
        quota->mEnable = true;
        client->mBioQos->mQuota = quota;
        pt.ptId = 3;
        pt.version = 7;
        pt.copys = {{0, 0, CM_COPY_RUNNING}};
        quota->mQuotaMgr[0] = credit;
        ASSERT_EQ(client->Insert(pt.ptId), BIO_OK); // Unready flow fails before allocating any memory.
    }

    void Reserve()
    {
        const uint64_t before = quota->mQuotaMgr.at(0);
        ASSERT_EQ(quota->AllocQuota("quota-test", &pt, size, Monotonic::TimeSec()), BIO_OK);
        EXPECT_EQ(quota->mQuotaMgr.at(0), before - size);
    }

    const uint64_t credit = 128 * 1024 * 1024;
    const uint64_t size = 4096;
    MirrorClientPtr client;
    BioQuotaPtr quota;
    CmPtInfo pt;
};

TEST_F(TestBioQos, early_flow_failure_returns_quota_for_each_write_entry)
{
    MirrorClient::MirrorPut param{{0, LOCAL_AFFINITY, WRITE_BACK},
        const_cast<char *>("quota-test"), nullptr, size, {}, 0};
    Reserve();
    EXPECT_EQ(client->PutImpl(param, pt.ptId, pt), BIO_INNER_RETRY);
    EXPECT_EQ(quota->mQuotaMgr.at(0), credit);
    Reserve();
    EXPECT_EQ(client->AsyncPutImpl(param, pt.ptId, pt, nullptr, nullptr), BIO_INNER_RETRY);
    EXPECT_EQ(quota->mQuotaMgr.at(0), credit);
    Reserve();
    CacheSpaceDesc space{};
    EXPECT_EQ(client->AllocSpaceImpl(pt.ptId, pt, param, space), BIO_INNER_RETRY);
    EXPECT_EQ(quota->mQuotaMgr.at(0), credit);
}

TEST_F(TestBioQos, unavailable_copy_keeps_quota_after_flow_offset_allocation)
{
    MirrorClient::MirrorPut param{{0, LOCAL_AFFINITY, WRITE_BACK},
        const_cast<char *>("quota-test"), nullptr, size, {}, 0};
    MirrorClient::UpdateParams update{pt.ptId, pt.version, 99, false, 0, 0};
    ASSERT_EQ(client->Update(update), BIO_OK);
    // Earlier suites may stub Version(); use the same value as AllocPutOffset.
    pt.version = client->Query(pt.ptId)->Version();
    pt.copys[0].state = CM_COPY_DOWN;
    for (bool memFromServer : {false, true}) {
        Reserve();
        ASSERT_EQ(client->AllocPutOffset(pt.ptId, pt.version, size, param.flowId,
            param.flowOffset, param.flowIndex), BIO_OK);
        const uint64_t expected = quota->mQuotaMgr.at(0);
        PutRequest req{};
        req.memFromServer = memFromServer;
        EXPECT_EQ(client->SendPutRequestImpl(pt, param, &req), BIO_CHECK_PT_FAIL);
        EXPECT_EQ(quota->mQuotaMgr.at(0), expected);

        Reserve();
        ASSERT_EQ(client->AllocPutOffset(pt.ptId, pt.version, size, param.flowId,
            param.flowOffset, param.flowIndex), BIO_OK);
        auto *asyncReq = reinterpret_cast<PutRequest *>(new uint8_t[sizeof(PutRequest)]());
        asyncReq->memFromServer = memFromServer;
        EXPECT_EQ(client->SendAsyncPutRequestImpl(pt, param, asyncReq, nullptr, nullptr), BIO_CHECK_PT_FAIL);
        EXPECT_EQ(quota->mQuotaMgr.at(0), expected - size);
    }
    EXPECT_EQ(client->Query(pt.ptId)->GetOffset(), 4 * size);
    EXPECT_EQ(quota->mQuotaMgr.at(0), credit - 4 * size);
}

TEST_F(TestBioQos, failed_preparation_keeps_quota_when_a_later_range_is_already_allocated)
{
    MirrorClient::UpdateParams update{pt.ptId, pt.version, 99, false, 0, 0};
    ASSERT_EQ(client->Update(update), BIO_OK);
    pt.version = client->Query(pt.ptId)->Version();
    MirrorClient::MirrorPut first{{0, LOCAL_AFFINITY, WRITE_BACK},
        const_cast<char *>("quota-first"), nullptr, size, {}, 0};
    MirrorClient::MirrorPut second = first;
    Reserve();
    ASSERT_EQ(client->AllocPutOffset(pt.ptId, pt.version, size, first.flowId,
        first.flowOffset, first.flowIndex), BIO_OK);
    Reserve();
    ASSERT_EQ(client->AllocPutOffset(pt.ptId, pt.version, size, second.flowId,
        second.flowOffset, second.flowIndex), BIO_OK);
    ASSERT_EQ(first.flowOffset, 0U);
    ASSERT_EQ(second.flowOffset, size);

    // Model the later write's allocated extent with test-owned memory chunks.
    std::vector<uint8_t> chunks(2 * size);
    Flow physicalFlow(FLOW_DATA, FLOW_MEMORY, first.flowId, 0, size, 0);
    ASSERT_EQ(physicalFlow.RecoverChunk(0, reinterpret_cast<uintptr_t>(chunks.data())), BIO_OK);
    ASSERT_EQ(physicalFlow.RecoverChunk(size, reinterpret_cast<uintptr_t>(chunks.data() + size)), BIO_OK);
    ASSERT_EQ(physicalFlow.RecoverCheck(), BIO_OK);

    PutRequest *req = nullptr;
    BResult ret;
    {
        auto allocate = static_cast<void *(*)(std::size_t, const std::nothrow_t &)>(&::operator new[]);
        mockcpp::ApiHook hook(reinterpret_cast<const void *>(allocate),
            reinterpret_cast<const void *>(AllocatePutRequestForTest));
        failPutRequestAllocation = true;
        ret = client->PrepareFromClient(pt, first, req);
        failPutRequestAllocation = false;
    }
    delete[] reinterpret_cast<uint8_t *>(req);

    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    EXPECT_EQ(quota->mQuotaMgr.at(0), credit - 2 * size);
    EXPECT_EQ(client->Query(pt.ptId)->GetOffset(), 2 * size);
    EXPECT_EQ(physicalFlow.GetAllocatedLen(), 2 * size);
    std::vector<FlowAddr> addrs;
    EXPECT_EQ(physicalFlow.GetExistingAddrByOffset(first.flowOffset, size, addrs), BIO_OK);
}

TEST_F(TestBioQos, queued_preload_owns_a_pt_snapshot)
{
    quota->mPreloadSize = credit;
    quota->mQuotaAllocExecutor = ExecutorService::Create(1, 16);
    ASSERT_NE(quota->mQuotaAllocExecutor, nullptr);
    ASSERT_TRUE(quota->mQuotaAllocExecutor->Start());
    std::promise<void> entered, release, finished;
    auto released = release.get_future().share();
    ASSERT_TRUE(quota->mQuotaAllocExecutor->Execute([&]() { entered.set_value(); released.wait(); }));
    entered.get_future().wait();

    auto bioClient = BioClient::Instance();
    auto agent = agent::BioClientAgent::Instance();
    auto oldMirror = bioClient->mMirror;
    auto oldMode = agent->mMode;
    auto oldAlloc = agent->allocQuotaOp;
    bioClient->mMirror = client;
    client->mLocalNid = CmNodeId(0, 0);
    // Earlier tests may stub VNodeId(); use the same local ID as the preload routing code.
    pt.copys[0].nodeId = client->GetLocalNodeInfo().VNodeId();
    agent->mMode = STANDALONE;
    agent->allocQuotaOp = RecordQuotaRequest;
    observedPt = 0;
    observedVersion = 0;

    quota->ExecutePreloadTask(&pt, 0);
    pt.ptId = 42;
    pt.version = 99;
    pt.copys.clear();
    EXPECT_TRUE(quota->mQuotaAllocExecutor->Execute([&]() { finished.set_value(); }));
    release.set_value();
    EXPECT_EQ(finished.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
    quota->mQuotaAllocExecutor->Stop();
    agent->allocQuotaOp = oldAlloc;
    agent->mMode = oldMode;
    bioClient->mMirror = oldMirror;

    EXPECT_EQ(observedPt.load(), 3U);
    EXPECT_EQ(observedVersion.load(), 7U);
}

TEST_F(TestBioQos, preload_exception_logs_stage_and_request_identity)
{
    quota->mPreloadSize = credit;
    quota->mQuotaAllocExecutor = ExecutorService::Create(1, 4);
    ASSERT_NE(quota->mQuotaAllocExecutor, nullptr);
    ASSERT_TRUE(quota->mQuotaAllocExecutor->Start());
    auto bioClient = BioClient::Instance();
    auto agent = agent::BioClientAgent::Instance();
    auto logger = BioClientLog::Instance();
    auto oldMirror = bioClient->mMirror;
    auto oldMode = agent->mMode;
    auto oldAlloc = agent->allocQuotaOp;
    auto oldLog = logger->func;
    auto oldLevel = logger->minLogLevel;
    bioClient->mMirror = client;
    pt.copys[0].nodeId = client->GetLocalNodeInfo().VNodeId();
    agent->mMode = STANDALONE;
    agent->allocQuotaOp = ThrowQuotaRequest;
    std::mutex logMutex;
    std::string message;
    logger->minLogLevel = static_cast<int32_t>(BioClientLog::Level::LOG_LEVEL_ERROR);
    logger->func = [&](int32_t, const char *text) {
        std::lock_guard<std::mutex> lock(logMutex);
        message += text;
    };

    quota->ExecutePreloadTask(&pt, 0);
    std::promise<void> finished;
    EXPECT_TRUE(quota->mQuotaAllocExecutor->Execute([&]() { finished.set_value(); }));
    EXPECT_EQ(finished.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
    quota->mQuotaAllocExecutor->Stop();
    logger->func = oldLog;
    logger->minLogLevel = oldLevel;
    agent->allocQuotaOp = oldAlloc;
    agent->mMode = oldMode;
    bioClient->mMirror = oldMirror;

    EXPECT_NE(message.find("QoS preload exception, stage:run, nodeSet:0"), std::string::npos);
    EXPECT_NE(message.find("ptId:3, ptVersion:7, preloadSize:134217728"), std::string::npos);
    EXPECT_NE(message.find("std::bad_alloc"), std::string::npos);
}

namespace {
struct PreloadAllocationFailure {
    uint32_t skip;
    bool triggered{false};
};

thread_local PreloadAllocationFailure *preloadAllocationFailure = nullptr;
thread_local bool unknownSubmitException = false;

bool FailPreloadAllocation()
{
    auto failure = preloadAllocationFailure;
    if (failure != nullptr && !failure->triggered) {
        if (failure->skip == 0) {
            failure->triggered = true;
            return true;
        }
        --failure->skip;
    }
    return false;
}

using TaskFlags = std::unordered_map<uint16_t, bool>;
using CopyAllocator = std::allocator<CmPtCopy>;

std::pair<TaskFlags::iterator, bool> InsertPreloadFlag(TaskFlags *flags, uint64_t &nodeSet, bool &&running)
{
    if (FailPreloadAllocation()) {
        throw std::bad_alloc();
    }
    return flags->insert({static_cast<uint16_t>(nodeSet), running});
}

CmPtCopy *AllocatePreloadCopies(CopyAllocator &, std::size_t count)
{
    if (FailPreloadAllocation()) {
        throw std::bad_alloc();
    }
    return static_cast<CmPtCopy *>(::operator new(count * sizeof(CmPtCopy)));
}

bool ThrowPreloadSubmit(ExecutorService *, const std::function<void()> &)
{
    if (unknownSubmitException) {
        throw 1;
    }
    throw std::bad_alloc();
}

class TestBioQosSubmit : public TestBioQos {
protected:
    void SetUp() override
    {
        TestBioQos::SetUp();
        auto bioClient = BioClient::Instance();
        auto agent = agent::BioClientAgent::Instance();
        auto logger = BioClientLog::Instance();
        oldMirror = bioClient->mMirror;
        oldMode = agent->mMode;
        oldAlloc = agent->allocQuotaOp;
        oldLog = logger->func;
        oldLevel = logger->minLogLevel;
        oldTrace = ock::htracer::HtracerManager::mEnable;
        bioClient->mMirror = client;
        agent->mMode = STANDALONE;
        agent->allocQuotaOp = RecordQuotaRequest;
        logger->minLogLevel = static_cast<int32_t>(BioClientLog::Level::LOG_LEVEL_ERROR);
        ock::htracer::HtracerManager::mEnable = false;
        pt.copys[0].nodeId = client->GetLocalNodeInfo().VNodeId();
        quota->mQuotaMgr[0] = 0;
        quota->mPreloadSize = credit;
        quota->mIoQueueMap.emplace(0, IoHangQueue());
        quota->mIoQueueMap.at(0).Push(&waiting);
        quota->mQuotaAllocExecutor = ExecutorService::Create(1, 16);
        ASSERT_NE(quota->mQuotaAllocExecutor, nullptr);
        ASSERT_TRUE(quota->mQuotaAllocExecutor->Start());
    }

    void TearDown() override
    {
        if (quota->mQuotaAllocExecutor != nullptr) {
            quota->mQuotaAllocExecutor->Stop();
        }
        BioClient::Instance()->mMirror = oldMirror;
        auto agent = agent::BioClientAgent::Instance();
        agent->mMode = oldMode;
        agent->allocQuotaOp = oldAlloc;
        auto logger = BioClientLog::Instance();
        logger->func = oldLog;
        logger->minLogLevel = oldLevel;
        ock::htracer::HtracerManager::mEnable = oldTrace;
    }

    void CheckResult(std::future<BResult> &result, BResult expected)
    {
        auto status = result.wait_for(std::chrono::seconds(3));
        EXPECT_EQ(status, std::future_status::ready);
        if (status != std::future_status::ready) {
            quota->WakeForce(0, false); // Rescue a waiter if the implementation regresses.
        }
        try {
            EXPECT_EQ(result.get(), expected);
        } catch (...) {
            ADD_FAILURE() << "Quota submission exception escaped AllocQuota";
            // An escaped exception has already destroyed the caller's entry; never dereference it.
            quota->mIoQueueMap.at(0).mTaskList.clear();
        }
    }

    void CheckCleaned()
    {
        EXPECT_TRUE(quota->mIoQueueMap.at(0).Empty());
        auto task = quota->mTaskRunFlag.find(0);
        EXPECT_TRUE(task == quota->mTaskRunFlag.end() || !task->second);
        EXPECT_TRUE(waiting.mReady);
        EXPECT_EQ(waiting.Result(), BIO_INNER_RETRY);
    }

    void CheckNextRequest()
    {
        auto next = std::async(std::launch::async, [&]() {
            return quota->AllocQuota("next", &pt, size, Monotonic::TimeSec());
        });
        CheckResult(next, BIO_OK);
        EXPECT_EQ(quota->mQuotaMgr.at(0), credit - size);
        EXPECT_TRUE(quota->mIoQueueMap.at(0).Empty());
        EXPECT_FALSE(quota->mTaskRunFlag.at(0));
    }

    void CheckAllocationFailure(bool insertFlag, uint32_t skip)
    {
        PreloadAllocationFailure failure{skip};
        // Hook only the QoS container operations, not global allocation inside the C++ runtime.
        const void *target;
        const void *replacement;
        if (insertFlag) {
            auto insert = &TaskFlags::emplace<uint64_t &, bool>;
            target = reinterpret_cast<const void *>(insert);
            replacement = reinterpret_cast<const void *>(InsertPreloadFlag);
        } else {
            auto allocate = static_cast<CmPtCopy *(*)(CopyAllocator &, std::size_t)>(
                &std::allocator_traits<CopyAllocator>::allocate);
            target = reinterpret_cast<const void *>(allocate);
            replacement = reinterpret_cast<const void *>(AllocatePreloadCopies);
        }
        {
            mockcpp::ApiHook hook(target, replacement);
            auto result = std::async(std::launch::async, [&]() {
                preloadAllocationFailure = &failure;
                try {
                    auto ret = quota->AllocQuota("submit", &pt, size, Monotonic::TimeSec());
                    preloadAllocationFailure = nullptr;
                    return ret;
                } catch (...) {
                    preloadAllocationFailure = nullptr;
                    throw;
                }
            });
            CheckResult(result, BIO_INNER_RETRY);
        }
        EXPECT_TRUE(failure.triggered);
        CheckCleaned();
        CheckNextRequest();
    }

    IoWaitEntry waiting{"waiting", 4096};
    MirrorClientPtr oldMirror;
    WorkerMode oldMode;
    agent::BioClientAgent::AllocQuotaFuncPtr oldAlloc;
    LogFunc oldLog;
    int32_t oldLevel;
    bool oldTrace;
};
}

TEST_F(TestBioQosSubmit, task_flag_allocation_failure_wakes_waiters_without_a_flag)
{
    CheckAllocationFailure(true, 0);
}

TEST_F(TestBioQosSubmit, snapshot_copy_failure_wakes_waiters_and_allows_retry)
{
    quota->mTaskRunFlag.emplace(0, false);
    CheckAllocationFailure(false, 0);
}

TEST_F(TestBioQosSubmit, closure_copy_failure_wakes_waiters_and_allows_retry)
{
    quota->mTaskRunFlag.emplace(0, false);
    CheckAllocationFailure(false, 1);
}

TEST_F(TestBioQosSubmit, logging_failure_does_not_escape_submission_recovery)
{
    std::atomic<uint32_t> logFailures{0};
    BioClientLog::Instance()->func = [&](int32_t, const char *) {
        ++logFailures;
        throw std::bad_alloc();
    };
    CheckAllocationFailure(true, 0);
    BioClientLog::Instance()->func = oldLog;
    EXPECT_GT(logFailures.load(), 0U);
}

TEST_F(TestBioQosSubmit, executor_submit_exceptions_wake_waiters_and_allow_retry)
{
    for (bool unknown : {false, true}) {
        SCOPED_TRACE(unknown);
        quota->mQuotaMgr[0] = 0;
        if (unknown) {
            waiting.mReady = false;
            quota->mIoQueueMap.at(0).Push(&waiting);
        }
        {
            auto execute = static_cast<bool (ExecutorService::*)(const std::function<void()> &)>(
                &ExecutorService::Execute);
            mockcpp::ApiHook hook(reinterpret_cast<const void *>(execute),
                reinterpret_cast<const void *>(ThrowPreloadSubmit));
            auto result = std::async(std::launch::async, [&]() {
                unknownSubmitException = unknown;
                return quota->AllocQuota("submit", &pt, size, Monotonic::TimeSec());
            });
            CheckResult(result, BIO_INNER_RETRY);
        }
        CheckCleaned();
        CheckNextRequest();
    }
}

TEST(TestBioQosWait, wake_before_wait_and_signal_do_not_release_an_ungranted_entry)
{
    IoWaitEntry ready("ready", 1);
    ready.Wake(BIO_INNER_RETRY);
    ready.Wait();
    EXPECT_EQ(ready.Result(), BIO_INNER_RETRY);

    struct sigaction action{}, previous{};
    action.sa_handler = IgnoreSignal;
    sigemptyset(&action.sa_mask);
    ASSERT_EQ(sigaction(SIGUSR1, &action, &previous), 0);
    IoWaitEntry waiting("waiting", 1);
    std::promise<void> entered;
    std::promise<BResult> completed;
    auto result = completed.get_future();
    std::thread waiter([&]() {
        entered.set_value();
        waiting.Wait();
        completed.set_value(waiting.Result());
    });
    entered.get_future().wait();
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    EXPECT_EQ(pthread_kill(waiter.native_handle(), SIGUSR1), 0);
    waiting.mCondition.notify_one(); // A notification without a grant must not release the caller.
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    waiting.Wake(BIO_QUOTA_NOT_ENOUGH);
    waiter.join();
    EXPECT_EQ(result.get(), BIO_QUOTA_NOT_ENOUGH);
    EXPECT_EQ(sigaction(SIGUSR1, &previous, nullptr), 0);
}

TEST(TestBioQosConfig, clamps_effective_eviction_watermark_below_quota_watermark)
{
    auto config = MakeRef<BioConfig>();
    config->LoadDefaultConf();
    ConfigurationPtr conf = config.Get();
    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.mem.size_in_gb", "0"));
    config->mDaemonConfig.enableQos = true;
    EXPECT_EQ(config->AutoConfigDaemonCache(conf), BIO_OK);
    EXPECT_EQ(config->mDaemonConfig.qosWaterLevel, 88U);
    EXPECT_EQ(config->mDaemonConfig.wcacheMemEvictLevel, 83U);

    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cache.qos.water_level", "100"));
    EXPECT_EQ(config->AutoConfigDaemonCache(conf), BIO_OK);
    EXPECT_EQ(config->mDaemonConfig.qosWaterLevel, 90U);

    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cache.qos.water_level", "80"));
    EXPECT_EQ(config->AutoConfigDaemonCache(conf), BIO_OK);
    EXPECT_EQ(config->mDaemonConfig.wcacheMemEvictLevel, 75U);
    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.disk.path", "/unused-test-device"));
    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cache.qos.water_level", "95"));
    for (const auto &level : {"0", "90", "98"}) {
        ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.wcache.evict_water_level", level));
        EXPECT_EQ(config->AutoConfigDaemonCache(conf), BIO_OK);
        EXPECT_EQ(config->mDaemonConfig.wcacheMemEvictLevel, std::string(level) == "0" ? 0U : 85U);
    }
    for (const auto &level : {"1", "5"}) {
        ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cache.qos.water_level", level));
        EXPECT_EQ(config->AutoConfigDaemonCache(conf), BIO_OK);
        EXPECT_EQ(config->mDaemonConfig.wcacheMemEvictLevel, 0U);
    }
    config->mDaemonConfig.enableQos = false;
    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.wcache.evict_water_level", "100"));
    EXPECT_EQ(config->AutoConfigDaemonCache(conf), BIO_OK);
    EXPECT_EQ(config->mDaemonConfig.wcacheMemEvictLevel, 100U);

    for (const auto &level : {"0", "101"}) {
        ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cache.qos.water_level", level));
        auto errors = conf->Validate();
        EXPECT_NE(std::find_if(errors.begin(), errors.end(), [](const std::string &error) {
            return error.find("ubsio.cache.qos.water_level") != std::string::npos;
        }), errors.end());
    }
}

TEST(TestBioQosConfig, standalone_budget_uses_memory_pt_count_and_segment)
{
    struct BudgetCase {
        uint32_t memoryGiB;
        uint32_t ptCount;
        uint32_t segmentMiB;
        uint32_t writeRatio;
        uint32_t quotaLevel;
        uint64_t evictLevel;
    };
    const BudgetCase cases[] = {
        {1, 16, 4, 10, 73, 58},
        {2, 16, 4, 10, 86, 78},
        {4, 16, 4, 10, 90, 85},
        {8, 16, 4, 10, 90, 85},
        {1, 32, 4, 10, 48, 33},
        {2, 64, 4, 10, 48, 40},
        {8, 128, 4, 10, 73, 68},
        {32, 128, 4, 10, 90, 85},
        {200, 16, 4, 10, 90, 85},
        {2, 16, 4, 5, 73, 58},
        {1, 16, 1, 10, 90, 75},
    };
    auto config = MakeRef<BioConfig>();
    config->LoadDefaultConf();
    config->SetStandaloneMode(true);
    ConfigurationPtr conf = config.Get();
    // Exercise the capacity-derived ceiling independently of the default.
    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cache.qos.water_level", "95"));
    for (const auto &item : cases) {
        SCOPED_TRACE(::testing::Message() << "memoryGiB=" << item.memoryGiB << ", ptCount=" << item.ptCount <<
            ", segmentMiB=" << item.segmentMiB << ", writeRatio=" << item.writeRatio);
        ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cm.pts_count", std::to_string(item.ptCount)));
        auto &daemon = config->mDaemonConfig;
        daemon.memCap = static_cast<uint64_t>(item.memoryGiB) * 1024 * 1024 * 1024;
        daemon.segment = item.segmentMiB * 1024 * 1024;
        daemon.memWriteRatio = item.writeRatio;
        daemon.wcacheMemEvictLevel = 100;
        ASSERT_EQ(config->AutoConfigQos(conf), BIO_OK);
        EXPECT_EQ(daemon.qosWaterLevel, item.quotaLevel);
        EXPECT_EQ(daemon.wcacheMemEvictLevel, item.evictLevel);
    }
}

TEST(TestBioQosConfig, standalone_budget_preserves_lower_limits_and_clamps_runtime_changes)
{
    auto config = MakeRef<BioConfig>();
    config->LoadDefaultConf();
    config->SetStandaloneMode(true);
    ConfigurationPtr conf = config.Get();
    auto &daemon = config->mDaemonConfig;
    daemon.memCap = 1024UL * 1024 * 1024;
    daemon.memWriteRatio = 10;
    for (uint64_t level : {0UL, 20UL, 90UL, 100UL}) {
        daemon.wcacheMemEvictLevel = level;
        ASSERT_EQ(config->AutoConfigQos(conf), BIO_OK);
        EXPECT_EQ(daemon.wcacheMemEvictLevel, std::min<uint64_t>(level, 58));
    }
    EXPECT_EQ(config->ModifyConfigEvictWaterLevel(0, 100), 58U);
    EXPECT_EQ(daemon.wcacheMemEvictLevel, 58U);
    EXPECT_EQ(config->ModifyConfigEvictWaterLevel(0, 20), 58U);
    EXPECT_EQ(daemon.wcacheMemEvictLevel, 20U);
    config->ModifyConfigEvictWaterLevel(1, 99);
    EXPECT_EQ(daemon.wcacheDiskEvictLevel, 99U);

    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cache.qos.water_level", "60"));
    daemon.wcacheMemEvictLevel = 100;
    ASSERT_EQ(config->AutoConfigQos(conf), BIO_OK);
    EXPECT_EQ(daemon.qosWaterLevel, 60U);
    EXPECT_EQ(daemon.wcacheMemEvictLevel, 45U);

    daemon.enableQos = false;
    daemon.memCap = 0;
    daemon.wcacheMemEvictLevel = 90;
    ASSERT_EQ(config->AutoConfigQos(conf), BIO_OK);
    EXPECT_EQ(daemon.wcacheMemEvictLevel, 90U);
    config->ModifyConfigEvictWaterLevel(0, 100);
    EXPECT_EQ(daemon.wcacheMemEvictLevel, 100U);
}

TEST(TestBioQosConfig, standalone_rejects_capacity_and_refill_budgets_that_cannot_fit)
{
    auto config = MakeRef<BioConfig>();
    config->LoadDefaultConf();
    config->SetStandaloneMode(true);
    ConfigurationPtr conf = config.Get();
    auto &daemon = config->mDaemonConfig;
    daemon.memWriteRatio = 10;
    daemon.memCap = 0;
    EXPECT_EQ(config->AutoConfigQos(conf), BIO_INVALID_PARAM);
    const std::pair<uint64_t, uint32_t> invalidBudgets[] = {{1, 64}, {1, 128}, {2, 128}};
    for (const auto &budget : invalidBudgets) {
        daemon.memCap = budget.first * 1024 * 1024 * 1024;
        ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cm.pts_count", std::to_string(budget.second)));
        EXPECT_EQ(config->AutoConfigQos(conf), BIO_INVALID_PARAM);
    }
    daemon.memCap = 1024UL * 1024 * 1024;
    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cm.pts_count", "16"));
    daemon.segment = 16 * 1024 * 1024;
    EXPECT_EQ(config->AutoConfigQos(conf), BIO_INVALID_PARAM);
    daemon.segment = 4 * 1024 * 1024;
    ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cache.qos.water_level", "5"));
    EXPECT_EQ(config->AutoConfigQos(conf), BIO_INVALID_PARAM);
}

TEST(TestBioQosConfig, standalone_watermarks_leave_capacity_and_refill_headroom_from_1_to_200_gib)
{
    auto config = MakeRef<BioConfig>();
    config->LoadDefaultConf();
    config->SetStandaloneMode(true);
    ConfigurationPtr conf = config.Get();
    auto &daemon = config->mDaemonConfig;
    daemon.memWriteRatio = 10;
    const uint64_t segment = daemon.segment;
    const uint64_t refill = (128UL + 16) * 1024 * 1024 + segment;
    for (uint64_t gib = 1; gib <= 200; ++gib) {
        for (uint32_t pts : {16U, 32U, 64U, 128U}) {
            // These three combinations cannot fit the fixed per-PT reserve; covered above.
            if ((gib == 1 && pts >= 64) || (gib == 2 && pts == 128)) {
                continue;
            }
            SCOPED_TRACE(::testing::Message() << "memoryGiB=" << gib << ", ptCount=" << pts);
            ASSERT_TRUE(conf->SetWithTypeAutoConvert("ubsio.cm.pts_count", std::to_string(pts)));
            daemon.memCap = gib * 1024 * 1024 * 1024;
            daemon.wcacheMemEvictLevel = 100;
            ASSERT_EQ(config->AutoConfigQos(conf), BIO_OK);
            const uint64_t quota = daemon.memCap / 100 * daemon.qosWaterLevel;
            const uint64_t evict = daemon.memCap * daemon.wcacheMemEvictLevel / 100;
            const uint64_t reserved = (4UL * pts + 1) * segment + (daemon.memCap + 99) / 100;
            EXPECT_LE(daemon.qosWaterLevel, 88U);
            EXPECT_LE(quota + reserved, daemon.memCap);
            EXPECT_GE(quota - evict, refill);
            EXPECT_LE(daemon.wcacheMemEvictLevel + 5, daemon.qosWaterLevel);
        }
    }
}

TEST(TestBioQosAccounting, rejected_holder_update_does_not_consume_server_quota)
{
    CacheOverloadCtrl control;
    QuotaHolder holder{0, 0};
    control.mWriteQuota = 4096;
    control.mHolders[holder] = UINT64_MAX - 1;
    uint64_t expected = 0;
    EXPECT_EQ(control.AllocQuota(holder, 2, expected), BIO_QUOTA_NOT_ENOUGH);
    EXPECT_EQ(control.mWriteQuota, 4096U);
    EXPECT_EQ(control.mHolders.at(holder), UINT64_MAX - 1);
}
}
