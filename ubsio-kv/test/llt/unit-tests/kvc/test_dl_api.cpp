/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <array>
#include <cstring>
#include <vector>
#include <sys/uio.h>
#include "test_common.h"
#include "ubsio_kvc_err.h"
#include "ubsio_kvc_stream_manager.h"

using namespace ock::ubsio;

namespace {
void MetaCallback(void *context, const UbsioMetaEventC *events, uint32_t count)
{
    auto *calls = static_cast<uint32_t *>(context);
    *calls += count;
    EXPECT_EQ(events[0].type, UBSIO_META_RECOVER_C);
}

TEST_F(KvTest, BioSdkStubLoadsAndCoversAllDelegates)
{
    ASSERT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_OK);
    EXPECT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_OK);

    EXPECT_EQ(DlBioSdkApi::KvBioInit(-2), -1);
    EXPECT_EQ(DlBioSdkApi::KvBioInit(-1), UBSIO_KVC_OK);
    EXPECT_EQ(FakeBioGetStandaloneDevice(), 0U);

    FakeBioSetResult("BioCreateCache", RET_CACHE_EXISTS);
    EXPECT_EQ(DlBioSdkApi::KvBioInit(3), UBSIO_KVC_OK);
    EXPECT_EQ(FakeBioGetStandaloneDevice(), 3U);

    FakeBioSetResult("BioInitialize", RET_CACHE_ERROR);
    EXPECT_EQ(DlBioSdkApi::KvBioInit(0), -1);
    FakeBioSetResult("BioInitialize", RET_CACHE_OK);
    FakeBioSetResult("BioCreateCache", RET_CACHE_ERROR);
    EXPECT_EQ(DlBioSdkApi::KvBioInit(0), -1);
    FakeBioSetResult("BioCreateCache", RET_CACHE_OK);

    ObjLocation location{};
    EXPECT_EQ(DlBioSdkApi::CalcLocation(1, 7, &location), RET_CACHE_OK);
    EXPECT_EQ(location.location[0], 7U);

    char value[8]{};
    uint64_t realLength = 0;
    EXPECT_EQ(DlBioSdkApi::Get(1, "key", 0, sizeof(value), location, value, &realLength), RET_CACHE_OK);
    EXPECT_EQ(realLength, sizeof(value));
    EXPECT_EQ(value[0], 'G');
    EXPECT_EQ(DlBioSdkApi::Put(1, "key", value, sizeof(value), location), RET_CACHE_OK);

    ObjStat stat{};
    EXPECT_EQ(DlBioSdkApi::Stat(1, "key", location, &stat), RET_CACHE_OK);
    EXPECT_EQ(stat.size, 64U);

    const char *keys[] = {"a", "b"};
    uint64_t offsets[] = {0, 0};
    uint64_t lengths[] = {4, 8};
    ObjLocation locations[2]{};
    uintptr_t addresses[2]{};
    uint64_t realLengths[2]{};
    int32_t results[2]{};
    EXPECT_EQ(DlBioSdkApi::BatchGet(1, keys, 2, offsets, lengths, locations,
                                    addresses, realLengths, results), RET_CACHE_OK);
    EXPECT_NE(addresses[0], 0U);
    EXPECT_EQ(realLengths[1], 8U);

    bool exists[2]{};
    EXPECT_EQ(DlBioSdkApi::BatchExist(1, keys, locations, 2, exists), RET_CACHE_OK);
    EXPECT_TRUE(exists[0]);
    EXPECT_FALSE(exists[1]);
    EXPECT_EQ(DlBioSdkApi::BatchGetFree(1, addresses, 2), RET_CACHE_OK);
    EXPECT_EQ(addresses[0], 0U);

    EXPECT_EQ(DlBioSdkApi::Delete(1, "a", location), RET_CACHE_OK);

    FakeBioSetDiskInfo("/fake/disk0", 16, 32, RET_CACHE_OK);
    KeyAddrInfo infos[2]{};
    EXPECT_EQ(DlBioSdkApi::BatchGetKeyDiskAddr(1, keys, locations, 2, infos), RET_CACHE_OK);
    EXPECT_STREQ(infos[0].path, "/fake/disk0");
    EXPECT_EQ(infos[0].offset[0], 16U);

    uint32_t callbackCalls = 0;
    EXPECT_EQ(DlBioSdkApi::RegisterMetaEventCallback(MetaCallback, &callbackCalls), RET_CACHE_OK);
    EXPECT_EQ(callbackCalls, 1U);

    CacheResourcesDesc resource{};
    EXPECT_EQ(DlBioSdkApi::ShowLocalCacheResource(&resource), RET_CACHE_OK);
    EXPECT_EQ(resource.wCacheDiskCapacity, 1000U);

    const UbsioKvKeyInfo *items = nullptr;
    uint64_t count = 0;
    EXPECT_EQ(DlBioSdkApi::ScanKey(1, &items, &count), RET_CACHE_OK);
    EXPECT_EQ(count, 2U);
    ASSERT_NE(items, nullptr);
    EXPECT_STREQ(items[0].key, "key-0");
    DlBioSdkApi::FreeScanKeyResult(&items);
    EXPECT_EQ(items, nullptr);

    DlBioSdkApi::Exit();
    EXPECT_EQ(FakeBioGetCallCount("BioExit"), 1);
    DlBioSdkApi::CleanupLibrary();
    DlBioSdkApi::CleanupLibrary();
}

TEST_F(KvTest, AclStubCoversUnavailableLoadAndDelegates)
{
    EXPECT_EQ(ACLApi::AclrtGetDevice(nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtSetDevice(0), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtCreateStream(nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtCreateStreamWithConfig(nullptr, 0, 0), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtDestroyStream(nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtSynchronizeStream(nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtMalloc(nullptr, 1, 0), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtFree(nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtMallocHost(nullptr, 1), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtFreeHost(nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtMemcpy(nullptr, 0, nullptr, 0, 0), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtMemcpyAsync(nullptr, 0, nullptr, 0, 0, nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtMemcpyBatch(nullptr, nullptr, nullptr, nullptr, 0,
                                      nullptr, nullptr, 0, nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtMemcpy2d(nullptr, 0, nullptr, 0, 0, 0, 0), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtMemcpy2dAsync(nullptr, 0, nullptr, 0, 0, 0, 0, nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtMemset(nullptr, 0, 0, 0), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::RtGetDeviceInfo(0, 0, 0, nullptr), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::AclrtGetLogicDevIdByUserDevId(0, nullptr), UBSIO_KVC_ERR);

    auto ascendHome = AscendHome();
    ASSERT_FALSE(ascendHome.empty());
    unsetenv("ASCEND_HOME_PATH");
    EXPECT_EQ(ACLApi::LoadLibrary(), UBSIO_KVC_ERR);
    ASSERT_EQ(setenv("ASCEND_HOME_PATH", ascendHome.c_str(), 1), 0);

    ASSERT_EQ(ACLApi::LoadLibrary(), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::LoadLibrary(), UBSIO_KVC_OK);

    int32_t device = -1;
    EXPECT_EQ(ACLApi::AclrtGetDevice(&device), UBSIO_KVC_OK);
    EXPECT_EQ(device, -1);
    EXPECT_EQ(ACLApi::AclrtSetDevice(4), UBSIO_KVC_OK);
    EXPECT_EQ(FakeAclGetCurrentDevice(), 4);
    EXPECT_EQ(ACLApi::AclrtSetDevice(4), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::AclrtSetDevice(5, true), UBSIO_KVC_OK);

    void *stream = nullptr;
    EXPECT_EQ(ACLApi::AclrtCreateStream(&stream), UBSIO_KVC_OK);
    EXPECT_NE(stream, nullptr);
    EXPECT_EQ(ACLApi::AclrtCreateStreamWithConfig(&stream, 0, 3), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::AclrtSynchronizeStream(stream), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::AclrtDestroyStream(stream), UBSIO_KVC_OK);

    void *deviceMemory = nullptr;
    void *hostMemory = nullptr;
    EXPECT_EQ(ACLApi::AclrtMalloc(&deviceMemory, 32, 0), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::AclrtMallocHost(&hostMemory, 32), UBSIO_KVC_OK);
    std::array<char, 32> source{};
    source.fill('A');
    EXPECT_EQ(ACLApi::AclrtMemcpy(deviceMemory, 32, source.data(), source.size(), 0), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::AclrtMemcpyAsync(hostMemory, 32, deviceMemory, 32, 0, stream), UBSIO_KVC_OK);
    EXPECT_EQ(static_cast<char *>(hostMemory)[0], 'A');

    std::array<char, 16> matrix{};
    EXPECT_EQ(ACLApi::AclrtMemcpy2d(matrix.data(), 4, source.data(), 4, 4, 4, 0), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::AclrtMemcpy2dAsync(matrix.data(), 4, source.data(), 4, 4, 4, 0, stream), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::AclrtMemset(matrix.data(), matrix.size(), 1, matrix.size()), UBSIO_KVC_OK);

    void *dsts[] = {hostMemory};
    void *srcs[] = {deviceMemory};
    size_t maxSizes[] = {32};
    size_t copySizes[] = {32};
    AclrtMemcpyBatchAttr attrs{};
    size_t attrIndex = 0;
    size_t failIndex = 99;
    EXPECT_EQ(ACLApi::AclrtMemcpyBatch(dsts, maxSizes, srcs, copySizes, 1,
                                       &attrs, &attrIndex, 1, &failIndex), UBSIO_KVC_OK);

    int64_t info = 0;
    EXPECT_EQ(ACLApi::RtGetDeviceInfo(5, 0, 0, &info), UBSIO_KVC_OK);
    EXPECT_EQ(info, 1005);
    int32_t logicDevice = -1;
    EXPECT_EQ(ACLApi::AclrtGetLogicDevIdByUserDevId(6, &logicDevice), UBSIO_KVC_OK);
    EXPECT_EQ(logicDevice, 106);

    EXPECT_EQ(ACLApi::AclrtFree(deviceMemory), UBSIO_KVC_OK);
    EXPECT_EQ(ACLApi::AclrtFreeHost(hostMemory), UBSIO_KVC_OK);
    ACLApi::CleanupLibrary();
    ACLApi::CleanupLibrary();
}

TEST_F(KvTest, NdsStubCoversUnavailableAndDelegates)
{
    nds_fileid_t fileId{1, 0};
    char buffer[16]{};
    struct ::iovec vector{buffer, sizeof(buffer)};

    EXPECT_EQ(NdsApi::NdsInit(0), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsApi::NdsInitAsync(0), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsApi::NdsUninit(), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsApi::NdsOpen("x", 0), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsApi::NdsRegmem(fileId, buffer, sizeof(buffer)), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsApi::NdsUnregmem(fileId, buffer, sizeof(buffer)), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsApi::NdsRead(fileId, buffer, 0, sizeof(buffer), 0), -1);
    EXPECT_EQ(NdsApi::NdsReadvBatch(fileId, &vector, 1, 0), -1);

    ASSERT_EQ(NdsApi::LoadLibrary(), UBSIO_KVC_OK);
    EXPECT_EQ(NdsApi::LoadLibrary(), UBSIO_KVC_OK);
    EXPECT_EQ(NdsApi::NdsInit(0), 0);
    EXPECT_EQ(NdsApi::NdsInitAsync(0), 0);
    int fd = NdsApi::NdsOpen("/fake/disk0", 0);
    ASSERT_GE(fd, 0);
    fileId.fd = fd;
    EXPECT_EQ(NdsApi::NdsRegmem(fileId, buffer, sizeof(buffer)), 0);
    EXPECT_EQ(NdsApi::NdsRead(fileId, buffer, 0, sizeof(buffer), 0),
              static_cast<ssize_t>(sizeof(buffer)));
    EXPECT_EQ(buffer[0], 'N');
    EXPECT_EQ(NdsApi::NdsReadvBatch(fileId, &vector, 1, 0),
              static_cast<ssize_t>(sizeof(buffer)));
    EXPECT_EQ(buffer[0], 'R');
    EXPECT_EQ(NdsApi::NdsUnregmem(fileId, buffer, sizeof(buffer)), 0);
    EXPECT_EQ(NdsApi::NdsUninit(), 0);
    close(fd);

    NdsApi::CleanupLibrary();
    NdsApi::CleanupLibrary();
}
}
