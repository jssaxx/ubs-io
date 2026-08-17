/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <array>
#include <cstdlib>
#include <string>
#include <vector>
#include "test_common.h"
#include "ubsio_kvc_instance.h"

using namespace ock::ubsio;
using namespace ock::ubsio::nds;

namespace {
void ConfigureNdsEnvironment(bool useIoUring = false)
{
    ASSERT_EQ(setenv("UBSIO_BIO_DISKS", "/fake/disk0", 1), 0);
    if (useIoUring) {
        ASSERT_EQ(setenv("UBSIO_USE_IO_URING", "1", 1), 0);
    } else {
        unsetenv("UBSIO_USE_IO_URING");
    }
}

void StartNds(bool useIoUring = false)
{
    ConfigureNdsEnvironment(useIoUring);
    ASSERT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_OK);
    ASSERT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_OK);
    std::array<char, 32> registration{};
    ASSERT_EQ(NdsManager::Instance().RegisterMemory(registration.data(), registration.size()), UBSIO_KVC_OK);
}

TEST_F(KvTest, NdsInitializationValidatesConfigurationAndDependencies)
{
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_ERR);
    ASSERT_EQ(setenv("UBSIO_BIO_DISKS", "", 1), 0);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_ERR);

    ConfigureNdsEnvironment();
    EXPECT_EQ(NdsManager::Instance().Initialize(-1), UBSIO_KVC_ERR);

    FakeNdsSetResult("nds_init", -1);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_ERR);
    NdsManager::Instance().UnInitialize();
    FakeNdsReset();

    ConfigureNdsEnvironment();
    FakeNdsSetResult("nds_open", -1);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_ERR);
    NdsManager::Instance().UnInitialize();
    FakeNdsReset();

    ConfigureNdsEnvironment();
    ASSERT_EQ(setenv("UBSIO_NDS_READ_THREAD", "bad", 1), 0);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_ERR);
    NdsManager::Instance().UnInitialize();

    FakeNdsReset();
    ConfigureNdsEnvironment();
    ASSERT_EQ(setenv("UBSIO_NDS_READ_THREAD", "0", 1), 0);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_ERR);
    NdsManager::Instance().UnInitialize();

    FakeNdsReset();
    ConfigureNdsEnvironment();
    ASSERT_EQ(setenv("UBSIO_NDS_READ_THREAD", "257", 1), 0);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_ERR);
    NdsManager::Instance().UnInitialize();

    FakeNdsReset();
    ConfigureNdsEnvironment();
    ASSERT_EQ(setenv("UBSIO_NDS_READ_THREAD", "2", 1), 0);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_OK);
    EXPECT_EQ(NdsManager::Instance().Initialize(3), UBSIO_KVC_OK);
    NdsManager::Instance().UnInitialize();

    FakeNdsReset();
    ConfigureNdsEnvironment(true);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_OK);
    EXPECT_EQ(FakeNdsGetCallCount("nds_init_async"), 1);
    NdsManager::Instance().UnInitialize();
}

TEST_F(KvTest, NdsMemoryRegistrationCoversSuccessAndFailures)
{
    ConfigureNdsEnvironment();
    ASSERT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_OK);
    std::array<char, 32> buffer{};

    FakeNdsSetResult("nds_regmem", -1);
    EXPECT_EQ(NdsManager::Instance().RegisterMemory(buffer.data(), buffer.size()), UBSIO_KVC_ERR);
    FakeNdsSetResult("nds_regmem", 0);
    EXPECT_EQ(NdsManager::Instance().RegisterMemory(buffer.data(), buffer.size()), UBSIO_KVC_OK);

    FakeNdsSetResult("nds_unregmem", -1);
    EXPECT_EQ(NdsManager::Instance().UnRegisterMemory(buffer.data(), buffer.size()), UBSIO_KVC_ERR);
    FakeNdsSetResult("nds_unregmem", 0);
    EXPECT_EQ(NdsManager::Instance().UnRegisterMemory(buffer.data(), buffer.size()), UBSIO_KVC_OK);
}

TEST_F(KvTest, NdsDirectReadValidatesInputsAndBioResponses)
{
    std::array<char, 16> output{};
    std::vector<uintptr_t> buffers{reinterpret_cast<uintptr_t>(output.data())};
    std::vector<size_t> sizes{output.size()};

    EXPECT_EQ(NdsManager::Instance().DirectRead("key", buffers, sizes), UBSIO_KVC_NO_NDS);

    StartNds();
    EXPECT_EQ(NdsManager::Instance().DirectRead("", buffers, sizes), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsManager::Instance().DirectRead(std::string(257, 'x'), buffers, sizes), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", {}, {}), UBSIO_KVC_ERR);
    std::vector<uintptr_t> tooManyBuffers(256, reinterpret_cast<uintptr_t>(output.data()));
    std::vector<size_t> tooManySizes(256, 1);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", tooManyBuffers, tooManySizes), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", buffers, {}), UBSIO_KVC_ERR);

    FakeBioSetResult("BioCalcLocation", RET_CACHE_ERROR);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", buffers, sizes), UBSIO_KVC_ERR);
    FakeBioSetResult("BioCalcLocation", RET_CACHE_OK);

    FakeBioSetResult("BioBatchGetKeyDiskAddr", RET_CACHE_ERROR);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", buffers, sizes), UBSIO_KVC_ERR);
    FakeBioSetResult("BioBatchGetKeyDiskAddr", RET_CACHE_OK);

    FakeBioSetDiskInfo("/fake/disk0", 0, output.size(), RET_CACHE_ERROR);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", buffers, sizes), UBSIO_KVC_ERR);

    FakeBioSetDiskInfo("/wrong/disk", 0, output.size(), RET_CACHE_OK);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", buffers, sizes), UBSIO_KVC_ERR);

    FakeBioSetDiskInfo("/fake/disk0", 0, output.size(), RET_CACHE_OK);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", buffers, sizes), UBSIO_KVC_OK);
    EXPECT_EQ(output[0], 'N');

    FakeNdsSetResult("nds_read", -1);
    EXPECT_EQ(NdsManager::Instance().DirectRead("key", buffers, sizes), UBSIO_KVC_ERR);
}

TEST_F(KvTest, NdsBatchReadCoversThreadPoolAndKvcFastPath)
{
    StartNds();
    FakeBioSetDiskInfo("/fake/disk0", 0, 4, RET_CACHE_OK);

    std::array<char, 8> first{};
    std::array<char, 8> second{};
    const std::vector<std::string> keys{"first", "second"};
    const std::vector<std::vector<uintptr_t>> buffers{
        {reinterpret_cast<uintptr_t>(first.data())},
        {reinterpret_cast<uintptr_t>(second.data())},
    };
    const std::vector<std::vector<size_t>> sizes{{first.size()}, {second.size()}};

    EXPECT_EQ(NdsManager::Instance().BatchDirectRead({}, {}, {}), UBSIO_KVC_ERR);
    std::vector<std::string> tooManyKeys(513, "key");
    EXPECT_EQ(NdsManager::Instance().BatchDirectRead(tooManyKeys, {}, {}), UBSIO_KVC_ERR);

    FakeBioSetResult("BioCalcLocation", RET_CACHE_ERROR);
    EXPECT_EQ(NdsManager::Instance().BatchDirectRead(keys, buffers, sizes), UBSIO_KVC_ERR);
    FakeBioSetResult("BioCalcLocation", RET_CACHE_OK);

    FakeBioSetResult("BioBatchGetKeyDiskAddr", RET_CACHE_ERROR);
    EXPECT_EQ(NdsManager::Instance().BatchDirectRead(keys, buffers, sizes), UBSIO_KVC_ERR);
    FakeBioSetResult("BioBatchGetKeyDiskAddr", RET_CACHE_OK);

    FakeBioSetDiskInfo("/fake/disk0", 0, 4, RET_CACHE_OK);
    EXPECT_EQ(NdsManager::Instance().BatchDirectRead(keys, buffers, sizes), UBSIO_KVC_OK);
    EXPECT_EQ(first[0], 'N');
    EXPECT_EQ(first[4], 'N');

    FakeNdsSetResult("nds_read", -1);
    EXPECT_EQ(NdsManager::Instance().BatchDirectRead(keys, buffers, sizes), UBSIO_KVC_ERR);
    FakeNdsSetResult("nds_read", 4);

    int results[] = {-1, -1};
    auto mutableBuffers = buffers;
    EXPECT_EQ(KvcInstance::Instance().Read(keys, mutableBuffers, sizes, results), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], UBSIO_KVC_OK);
    EXPECT_EQ(FakeBioGetCallCount("BioBatchGet"), 0);
}

TEST_F(KvTest, NdsIoUringBatchReadUsesStubbedVectorIo)
{
    StartNds(true);
    FakeBioSetDiskInfo("/fake/disk0", 0, 4, RET_CACHE_OK);

    std::array<char, 8> output{};
    const std::vector<std::string> keys{"key"};
    const std::vector<std::vector<uintptr_t>> buffers{
        {reinterpret_cast<uintptr_t>(output.data())},
    };
    const std::vector<std::vector<size_t>> sizes{{output.size()}};

    EXPECT_EQ(NdsManager::Instance().BatchDirectRead(keys, buffers, sizes), UBSIO_KVC_OK);
    EXPECT_EQ(FakeNdsGetCallCount("nds_readv_batch"), 1);
    EXPECT_EQ(output[0], 'R');
}
}
