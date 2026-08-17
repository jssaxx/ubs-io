/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <array>
#include <cstdlib>
#include <string>
#include <vector>
#include "kv_operation.h"
#include "test_common.h"
#include "ubsio_kvc.h"
#include "ubsio_kvc_instance.h"
#include "ubsio_kvc_stream_manager.h"

using namespace ock::ubsio;

namespace {
void RestoreAscendHome(const std::string &path)
{
    ASSERT_FALSE(path.empty());
    ASSERT_EQ(setenv("ASCEND_HOME_PATH", path.c_str(), 1), 0);
}

TEST_F(KvTest, KvcInitializationCoversDeviceAndFailurePaths)
{
    EXPECT_EQ(UbsioKvCacheInit(-1), UBSIO_KVC_OK);
    EXPECT_EQ(FakeBioGetStandaloneDevice(), 0U);
    EXPECT_EQ(UbsioKvCacheInit(-1), UBSIO_KVC_OK);
    UbsioKvCacheExit();

    FakeBioReset();
    FakeBioSetResult("BioInitialize", RET_CACHE_ERROR);
    EXPECT_EQ(UbsioKvCacheInit(-1), UBSIO_KVC_ERR);
    UbsioKvCacheExit();

    FakeBioReset();
    FakeBioSetResult("BioCreateCache", RET_CACHE_ERROR);
    EXPECT_EQ(UbsioKvCacheInit(-1), UBSIO_KVC_ERR);
    UbsioKvCacheExit();

    FakeBioReset();
    FakeBioSetResult("BioCreateCache", RET_CACHE_EXISTS);
    EXPECT_EQ(UbsioKvCacheInit(-1), UBSIO_KVC_OK);
    UbsioKvCacheExit();

    const auto ascendHome = AscendHome();
    unsetenv("ASCEND_HOME_PATH");
    EXPECT_EQ(UbsioKvCacheInit(0), UBSIO_KVC_ERR);
    UbsioKvCacheExit();
    RestoreAscendHome(ascendHome);

    FakeBioReset();
    FakeAclReset();
    FakeAclSetResult("aclrtGetLogicDevIdByUserDevId", 1);
    EXPECT_EQ(UbsioKvCacheInit(0), UBSIO_KVC_ERR);
    UbsioKvCacheExit();
    ACLApi::CleanupLibrary();

    FakeBioReset();
    FakeAclReset();
    FakeAclSetResult("aclrtSetDevice", 1);
    EXPECT_EQ(UbsioKvCacheInit(0), UBSIO_KVC_ERR);
    UbsioKvCacheExit();
    ACLApi::CleanupLibrary();

    FakeBioReset();
    FakeAclReset();
    FakeAclSetResult("aclrtCreateStreamWithConfig", 1);
    EXPECT_EQ(UbsioKvCacheInit(0), UBSIO_KVC_ERR);
    UbsioKvCacheExit();
    ACLApi::CleanupLibrary();

    FakeBioReset();
    FakeAclReset();
    EXPECT_EQ(UbsioKvCacheInit(5), UBSIO_KVC_OK);
    EXPECT_EQ(FakeBioGetStandaloneDevice(), 105U);
    EXPECT_NE(KvcStreamManager::GetAclStream(), nullptr);
    EXPECT_EQ(KvcStreamManager::InitAclStream(5), UBSIO_KVC_OK);
    UbsioKvCacheExit();
    EXPECT_EQ(KvcStreamManager::GetAclStream(), nullptr);
    KvcStreamManager::DestroyAclStream();
}

TEST_F(KvTest, KvcInstanceFallsBackToStubbedBioAndAcl)
{
    ASSERT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_OK);
    ASSERT_EQ(ACLApi::LoadLibrary(), UBSIO_KVC_OK);
    ASSERT_EQ(KvcStreamManager::InitAclStream(0), UBSIO_KVC_OK);
    ASSERT_EQ(KvcInstance::Instance().Initialize(0), UBSIO_KVC_OK);
    ASSERT_EQ(KvcInstance::Instance().Initialize(1), UBSIO_KVC_OK);

    const std::vector<std::string> keys{"first", "second"};
    std::array<char, 8> first{};
    std::array<char, 8> second{};
    std::vector<std::vector<uintptr_t>> addresses{
        {reinterpret_cast<uintptr_t>(first.data())},
        {reinterpret_cast<uintptr_t>(second.data())},
    };
    const std::vector<std::vector<size_t>> lengths{{first.size()}, {second.size()}};
    int results[] = {-1, -1};

    EXPECT_EQ(KvcInstance::Instance().Read(keys, addresses, lengths, results), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], UBSIO_KVC_OK);
    EXPECT_EQ(first[0], 'B');
    EXPECT_EQ(second[0], 'B');
    KvcInstance::Instance().UnInitialize();
    EXPECT_GE(FakeBioGetCallCount("BioBatchGetFree"), 1);

    ASSERT_EQ(KvcInstance::Instance().Initialize(0), UBSIO_KVC_OK);
    FakeBioSetBatchItemResult(RET_CACHE_NOT_FOUND);
    first.fill(0);
    EXPECT_EQ(KvcInstance::Instance().Read(keys, addresses, lengths, results), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], RET_CACHE_NOT_FOUND);
    EXPECT_EQ(first[0], 0);
    KvcInstance::Instance().UnInitialize();

    ASSERT_EQ(KvcInstance::Instance().Initialize(0), UBSIO_KVC_OK);
    FakeBioSetBatchItemResult(RET_CACHE_OK);
    FakeAclSetResult("aclrtMemcpyAsync", 1);
    EXPECT_EQ(KvcInstance::Instance().Read(keys, addresses, lengths, results), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], UBSIO_KVC_ERR);
    KvcInstance::Instance().UnInitialize();
    FakeAclSetResult("aclrtMemcpyAsync", 0);

    ASSERT_EQ(KvcInstance::Instance().Initialize(0), UBSIO_KVC_OK);
    FakeAclSetResult("aclrtSynchronizeStream", 1);
    EXPECT_EQ(KvcInstance::Instance().Read(keys, addresses, lengths, results), UBSIO_KVC_ERR);
    KvcInstance::Instance().UnInitialize();
    FakeAclSetResult("aclrtSynchronizeStream", 0);

    ASSERT_EQ(KvcInstance::Instance().Initialize(0), UBSIO_KVC_OK);
    KvcStreamManager::DestroyAclStream();
    EXPECT_EQ(KvcInstance::Instance().Read(keys, addresses, lengths, results), UBSIO_KVC_ERR);
    KvcInstance::Instance().UnInitialize();
}

TEST_F(KvTest, KvcInstanceSynchronousFreeAndBatchFailures)
{
    ASSERT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_OK);
    ASSERT_EQ(ACLApi::LoadLibrary(), UBSIO_KVC_OK);
    ASSERT_EQ(KvcStreamManager::InitAclStream(0), UBSIO_KVC_OK);

    const std::vector<std::string> keys{"one"};
    std::array<char, 4> output{};
    std::vector<std::vector<uintptr_t>> addresses{
        {reinterpret_cast<uintptr_t>(output.data())},
    };
    const std::vector<std::vector<size_t>> lengths{{output.size()}};
    int results[] = {-1};

    EXPECT_EQ(KvcInstance::Instance().Read(keys, addresses, lengths, results), UBSIO_KVC_OK);
    EXPECT_EQ(FakeBioGetCallCount("BioBatchGetFree"), 1);

    FakeBioSetResult("BioBatchGet", RET_CACHE_ERROR);
    EXPECT_EQ(KvcInstance::Instance().Read(keys, addresses, lengths, results), UBSIO_KVC_ERR);
    FakeBioSetResult("BioBatchGet", RET_CACHE_OK);

    KvcStreamManager::DestroyAclStream();
    EXPECT_EQ(KvcInstance::Instance().Read(keys, addresses, lengths, results), UBSIO_KVC_ERR);
}

TEST_F(KvTest, KvOperationLifecycleAndBatchErrorResults)
{
    ASSERT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_OK);
    auto *operation = KvOperation::Instance();
    ASSERT_NE(operation, nullptr);

    EXPECT_EQ(operation->Initialize("ignored"), UBSIO_KVC_OK);
    EXPECT_EQ(operation->Initialize("ignored"), UBSIO_KVC_OK);
    EXPECT_EQ(operation->InitKvExecutor(), UBSIO_KVC_OK);
    EXPECT_EQ(operation->InitKvExecutor(), UBSIO_KVC_OK);

    std::array<char, 4> value{};
    std::vector<std::string> keys{"a", "b"};
    std::vector<void *> values{value.data(), value.data()};
    std::vector<size_t> lengths{value.size(), value.size()};
    std::vector<int> results(2, -1);

    FakeBioSetResult("BioPut", RET_CACHE_ERROR);
    EXPECT_EQ(operation->BatchKvPutData(keys, values, lengths, results), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], RET_CACHE_ERROR);

    FakeBioSetResult("BioDelete", RET_CACHE_ERROR);
    EXPECT_EQ(operation->BatchKvDeleteKey(keys, results), UBSIO_KVC_OK);
    EXPECT_EQ(results[1], RET_CACHE_ERROR);

    FakeBioSetResult("BioStat", RET_CACHE_ERROR);
    std::vector<uint32_t> outputLengths(2, 0);
    EXPECT_EQ(operation->BatchGetLengthKey(keys, outputLengths, results), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], UBSIO_KVC_ERR);

    operation->UnInitialize();
    operation->UnInitialize();
}
}
