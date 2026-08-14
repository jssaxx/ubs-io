/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <array>
#include <cstring>
#include <string>
#include <vector>
#include "kv_operation.h"
#include "test_common.h"
#include "ubsio_kvc.h"
#include "ubsio_kvc_err.h"

using namespace ock::ubsio;

namespace {
constexpr uint32_t TOO_MANY_KEYS = (16U << 10U) + 1U;

void EventCounter(void *context, const UbsioMetaEventC *, uint32_t count)
{
    *static_cast<uint32_t *>(context) += count;
}

void StartOperationExecutor()
{
    ASSERT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_OK);
    ASSERT_NE(KvOperation::Instance(), nullptr);
    ASSERT_EQ(KvOperation::Instance()->InitKvExecutor(), UBSIO_KVC_OK);
}

TEST_F(KvTest, PublicApiRejectsInvalidArguments)
{
    std::string longKey(256, 'x');
    char data[8]{};
    void *buffer = data;
    const char *key = "key";
    const char *emptyKey = "";
    const char *nullKey = nullptr;
    size_t length = sizeof(data);
    int result = 0;
    bool exists = false;

    EXPECT_EQ(UbsioGetResourceInfo(nullptr), UBSIO_KVC_INVALID_PARAM);
    const UbsioKvKeyInfo *scanItems = nullptr;
    uint64_t scanCount = 0;
    EXPECT_EQ(UbsioKvCacheScanKey(nullptr, &scanCount), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheScanKey(&scanItems, nullptr), UBSIO_KVC_INVALID_PARAM);
    UbsioKvCacheFreeScanKeyResult(nullptr);
    UbsioKvCacheFreeScanKeyResult(&scanItems);

    EXPECT_EQ(UbsioKvCachePut(nullptr, data, sizeof(data), 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCachePut(key, nullptr, sizeof(data), 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCachePut(key, data, 0, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCachePut("", data, sizeof(data), 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCachePut(longKey.c_str(), data, sizeof(data), 0), UBSIO_KVC_INVALID_PARAM);

    const char *keys[] = {key};
    void *buffers[] = {buffer};
    size_t lengths[] = {length};
    int results[] = {result};

    EXPECT_EQ(UbsioKvCacheBatchPut(nullptr, 1, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchPut(keys, 1, nullptr, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchPut(keys, 1, buffers, nullptr, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchPut(keys, 1, buffers, lengths, nullptr, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchPut(keys, 0, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchPut(keys, TOO_MANY_KEYS, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);

    const char *badKeys[] = {nullKey};
    EXPECT_EQ(UbsioKvCacheBatchPut(badKeys, 1, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    const char *emptyKeys[] = {emptyKey};
    EXPECT_EQ(UbsioKvCacheBatchPut(emptyKeys, 1, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    void *nullBuffers[] = {nullptr};
    EXPECT_EQ(UbsioKvCacheBatchPut(keys, 1, nullBuffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    size_t zeroLengths[] = {0};
    EXPECT_EQ(UbsioKvCacheBatchPut(keys, 1, buffers, zeroLengths, results, 0), UBSIO_KVC_INVALID_PARAM);

    EXPECT_EQ(UbsioKvCacheGet(nullptr, data, sizeof(data), 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheGet(key, nullptr, sizeof(data), 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheGet(key, data, 0, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheGet("", data, sizeof(data), 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheGet(longKey.c_str(), data, sizeof(data), 0), UBSIO_KVC_INVALID_PARAM);

    EXPECT_FALSE(UbsioKvCacheExist(nullptr, 0));
    EXPECT_FALSE(UbsioKvCacheExist("", 0));
    EXPECT_FALSE(UbsioKvCacheExist(longKey.c_str(), 0));
    EXPECT_EQ(UbsioKvCacheDelete(nullptr, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheDelete("", 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheDelete(longKey.c_str(), 0), UBSIO_KVC_INVALID_PARAM);

    EXPECT_EQ(UbsioKvCacheBatchDelete(nullptr, 1, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchDelete(keys, 1, nullptr, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchDelete(keys, 0, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchDelete(badKeys, 1, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchDelete(emptyKeys, 1, results, 0), UBSIO_KVC_INVALID_PARAM);

    EXPECT_EQ(UbsioKvCacheGetLength(nullptr, &length, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheGetLength(key, nullptr, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheGetLength("", &length, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheGetLength(longKey.c_str(), &length, 0), UBSIO_KVC_INVALID_PARAM);

    EXPECT_EQ(UbsioKvCacheBatchGetLength(nullptr, 1, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetLength(keys, 1, nullptr, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetLength(keys, 1, lengths, nullptr, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetLength(keys, 0, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetLength(badKeys, 1, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetLength(emptyKeys, 1, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);

    EXPECT_EQ(UbsioKvCacheBatchGet(nullptr, 1, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGet(keys, 1, nullptr, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGet(keys, 1, buffers, nullptr, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGet(keys, 1, buffers, lengths, nullptr, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGet(keys, 0, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGet(badKeys, 1, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGet(emptyKeys, 1, buffers, lengths, results, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGet(keys, 1, buffers, zeroLengths, results, 0), UBSIO_KVC_INVALID_PARAM);

    EXPECT_EQ(UbsioKvCacheBatchExist(nullptr, 1, &exists, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchExist(keys, 1, nullptr, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchExist(keys, 0, &exists, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchExist(badKeys, 1, &exists, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchExist(emptyKeys, 1, &exists, 0), UBSIO_KVC_INVALID_PARAM);

    EXPECT_EQ(UbsioKvCacheBatchFree(nullptr, 1), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchFree(buffers, 0), UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchFree(buffers, TOO_MANY_KEYS), UBSIO_KVC_INVALID_PARAM);

    void *layerBuffer = data;
    void **bufferRows[] = {&layerBuffer};
    size_t layerLength = sizeof(data);
    size_t *lengthRows[] = {&layerLength};
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(nullptr, 1, bufferRows, lengthRows, 1, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 1, nullptr, lengthRows, 1, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 1, bufferRows, nullptr, 1, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 1, bufferRows, lengthRows, 1, 1, nullptr, 0),
              UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 0, bufferRows, lengthRows, 0, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 1, bufferRows, lengthRows, 0, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 1, bufferRows, lengthRows, 1, 1025, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(badKeys, 1, bufferRows, lengthRows, 1, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(emptyKeys, 1, bufferRows, lengthRows, 1, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);

    layerLength = 0;
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 1, bufferRows, lengthRows, 1, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    layerLength = (2ULL * 1024ULL * 1024ULL * 1024ULL) + 1ULL;
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 1, bufferRows, lengthRows, 1, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
    layerLength = sizeof(data);
    layerBuffer = nullptr;
    EXPECT_EQ(UbsioKvCacheBatchGetDirect(keys, 1, bufferRows, lengthRows, 1, 1, results, 0),
              UBSIO_KVC_INVALID_PARAM);
}

TEST_F(KvTest, PublicApiHappyPathsUseOnlyBioStub)
{
    StartOperationExecutor();

    UbsioResourceInfo info{};
    FakeBioSetResourceDiskCount(20);
    EXPECT_EQ(UbsioGetResourceInfo(&info), UBSIO_KVC_OK);
    EXPECT_EQ(info.diskCap, 1000U);
    EXPECT_EQ(info.diskNum, UBSIO_RESOURCE_MAX_DISK_NUM);
    EXPECT_EQ(info.faultDiskNum, 8U);
    EXPECT_STREQ(info.disks[0].path, "/fake/disk0");

    uint32_t callbackCount = 0;
    EXPECT_EQ(UbsioKvCacheRegisterMetaEventCallback(EventCounter, &callbackCount), UBSIO_KVC_OK);
    EXPECT_EQ(callbackCount, 1U);

    const UbsioKvKeyInfo *items = nullptr;
    uint64_t count = 0;
    EXPECT_EQ(UbsioKvCacheScanKey(&items, &count), UBSIO_KVC_OK);
    ASSERT_EQ(count, 2U);
    EXPECT_STREQ(items[1].key, "key-1");
    UbsioKvCacheFreeScanKeyResult(&items);
    EXPECT_EQ(items, nullptr);

    std::array<char, 16> data{};
    EXPECT_EQ(UbsioKvCachePut("alpha", data.data(), data.size(), 99), UBSIO_KVC_OK);
    EXPECT_EQ(UbsioKvCacheGet("alpha", data.data(), data.size(), 99), UBSIO_KVC_OK);
    EXPECT_EQ(data[0], 'G');
    EXPECT_TRUE(UbsioKvCacheExist("alpha", 0));
    EXPECT_EQ(UbsioKvCacheDelete("alpha", 0), UBSIO_KVC_OK);

    size_t objectLength = 0;
    EXPECT_EQ(UbsioKvCacheGetLength("alpha", &objectLength, 0), UBSIO_KVC_OK);
    EXPECT_EQ(objectLength, 64U);

    const char *keys[] = {"alpha", "beta"};
    void *putBuffers[] = {data.data(), data.data()};
    size_t lengths[] = {8, 16};
    int results[] = {-1, -1};
    EXPECT_EQ(UbsioKvCacheBatchPut(keys, 2, putBuffers, lengths, results, 0), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], UBSIO_KVC_OK);
    EXPECT_EQ(results[1], UBSIO_KVC_OK);

    void *getBuffers[] = {nullptr, nullptr};
    EXPECT_EQ(UbsioKvCacheBatchGet(keys, 2, getBuffers, lengths, results, 0), UBSIO_KVC_OK);
    EXPECT_NE(getBuffers[0], nullptr);
    EXPECT_EQ(results[0], UBSIO_KVC_OK);
    EXPECT_EQ(UbsioKvCacheBatchFree(getBuffers, 2), UBSIO_KVC_OK);
    EXPECT_EQ(getBuffers[0], nullptr);

    bool exists[] = {false, false};
    EXPECT_EQ(UbsioKvCacheBatchExist(keys, 2, exists, 0), UBSIO_KVC_OK);
    EXPECT_TRUE(exists[0]);
    EXPECT_FALSE(exists[1]);

    EXPECT_EQ(UbsioKvCacheBatchDelete(keys, 2, results, 0), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], UBSIO_KVC_OK);

    size_t outputLengths[] = {0, 0};
    EXPECT_EQ(UbsioKvCacheBatchGetLength(keys, 2, outputLengths, results, 0), UBSIO_KVC_OK);
    EXPECT_EQ(outputLengths[0], 64U);
    EXPECT_EQ(results[1], UBSIO_KVC_OK);
}

TEST_F(KvTest, PublicApiMapsBioFailures)
{
    StartOperationExecutor();

    UbsioResourceInfo info{};
    FakeBioSetResult("BioShowLocalCacheResource", RET_CACHE_ERROR);
    EXPECT_EQ(UbsioGetResourceInfo(&info), UBSIO_KVC_ERR);
    FakeBioSetResult("BioShowLocalCacheResource", RET_CACHE_OK);

    FakeBioSetResult("BioRegisterMetaEventCallback", RET_CACHE_ERROR);
    EXPECT_EQ(UbsioKvCacheRegisterMetaEventCallback(nullptr, nullptr), UBSIO_KVC_ERR);
    FakeBioSetResult("BioRegisterMetaEventCallback", RET_CACHE_OK);

    const UbsioKvKeyInfo *items = nullptr;
    uint64_t count = 0;
    const std::array<std::pair<int, int>, 7> scanMappings{{
        {RET_CACHE_EPERM, UBSIO_KVC_INVALID_PARAM},
        {RET_CACHE_NOT_READY, UBSIO_KVC_EAGAIN},
        {RET_CACHE_BUSY, UBSIO_KVC_EAGAIN},
        {RET_CACHE_NEED_RETRY, UBSIO_KVC_EAGAIN},
        {RET_CACHE_NO_SPACE, UBSIO_KVC_ALLOC_FAIL},
        {RET_CACHE_ERROR, UBSIO_KVC_BIO_ERR},
        {RET_CACHE_OK, UBSIO_KVC_OK},
    }};
    for (const auto &mapping : scanMappings) {
        FakeBioSetResult("BioScanKey", mapping.first);
        FakeBioSetScanCount(0);
        EXPECT_EQ(UbsioKvCacheScanKey(&items, &count), mapping.second);
    }

    char data[8]{};
    FakeBioSetResult("BioCalcLocation", RET_CACHE_ERROR);
    EXPECT_EQ(UbsioKvCachePut("key", data, sizeof(data), 0), UBSIO_KVC_ERR);
    EXPECT_EQ(UbsioKvCacheGet("key", data, sizeof(data), 0), UBSIO_KVC_ERR);
    EXPECT_FALSE(UbsioKvCacheExist("key", 0));
    EXPECT_EQ(UbsioKvCacheDelete("key", 0), UBSIO_KVC_ERR);
    size_t length = 0;
    EXPECT_EQ(UbsioKvCacheGetLength("key", &length, 0), UBSIO_KVC_ERR);

    const char *keys[] = {"key"};
    void *buffers[] = {nullptr};
    size_t lengths[] = {8};
    int results[] = {-1};
    bool exists[] = {false};

    EXPECT_EQ(UbsioKvCacheBatchGet(keys, 1, buffers, lengths, results, 0), UBSIO_KVC_ERR);
    EXPECT_EQ(UbsioKvCacheBatchExist(keys, 1, exists, 0), UBSIO_KVC_ERR);

    FakeBioSetResult("BioCalcLocation", RET_CACHE_OK);
    FakeBioSetResult("BioBatchGet", RET_CACHE_ERROR);
    EXPECT_EQ(UbsioKvCacheBatchGet(keys, 1, buffers, lengths, results, 0), UBSIO_KVC_ERR);
    FakeBioSetResult("BioBatchExist", RET_CACHE_ERROR);
    EXPECT_EQ(UbsioKvCacheBatchExist(keys, 1, exists, 0), UBSIO_KVC_ERR);

    void *freeBuffers[] = {nullptr};
    FakeBioSetResult("BioBatchGetFree", RET_CACHE_ERROR);
    EXPECT_EQ(UbsioKvCacheBatchFree(freeBuffers, 1), UBSIO_KVC_ERR);

    FakeBioSetResult("BioStat", RET_CACHE_ERROR);
    EXPECT_FALSE(UbsioKvCacheExist("key", 0));
    EXPECT_EQ(UbsioKvCacheGetLength("key", &length, 0), UBSIO_KVC_ERR);
    FakeBioSetResult("BioStat", RET_CACHE_OK);
    FakeBioSetStatSize(0);
    EXPECT_FALSE(UbsioKvCacheExist("key", 0));
    EXPECT_EQ(UbsioKvCacheGetLength("key", &length, 0), UBSIO_KVC_ERR);
}
}
