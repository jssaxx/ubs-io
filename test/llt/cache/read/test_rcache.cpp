/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <gtest/gtest.h>
#include <mockcpp/mokc.h>
#include <mockcpp/mockcpp.hpp>
#include <cstdint>
#include "underfs.h"
#include "bio_server.h"
#include "bio_mock.h"
#include "bio_config_instance.h"
#include "cache_slice_operator.h"
#include "rcache_manager.h"
#include "test_rcache.h"

using namespace ock::bio;

static RCacheManagerPtr g_rcacheManager = RCacheManager::Instance();

bool TestRCache::g_setup = false;

void TestRCache::SetUp()
{
    if (g_setup) {
        return;
    }

    g_setup = true;
    return;
}

void TestRCache::TearDown()
{
    return;
}

static CacheSliceOperator g_slicerOperator;

constexpr uint64_t g_ptId = 1;
constexpr Key g_key = const_cast<char *>("123123key");
constexpr char *g_value = "test/read/cache/data";

static RCacheSlicePtr g_rcacheSlice;

auto rwriter = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = g_slicerOperator.Copy(from, to);
    EXPECT_EQ(ret, BIO_OK);
    return ret;
};

TEST_F(TestRCache, test_rcache_create_ok) {
    auto ret = g_rcacheManager->CreateRCache(g_ptId, 0, 0);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_rcache_put_ok)
{
    uint64_t len = strlen(g_value) + 1;
    WCacheSlicePtr slicePtr = nullptr;

    auto ret = g_rcacheManager->AllocResources(g_ptId, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = g_slicerOperator.Copy(g_value, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = g_rcacheManager->Put(g_ptId, g_key, slicePtr);
    EXPECT_EQ(ret, 0);

    uint64_t needEvictData = len;
    uint64_t haveEvictData;
    g_rcacheManager->GetRCacheInstanceByPtId(g_ptId)->EvictMemData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(needEvictData, haveEvictData);

    g_rcacheManager->GetRCacheInstanceByPtId(g_ptId)->EvictDiskData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(needEvictData, haveEvictData);
}

TEST_F(TestRCache, test_rcache_get_ok)
{
    uint64_t len = strlen(g_value) + 1;
    char *key1 = "123123key1";
    WCacheSlicePtr slicePtr = nullptr;
    RCacheSlicePtr readSlicePtr = nullptr;

    auto ret = g_rcacheManager->AllocResources(g_ptId, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = g_slicerOperator.Copy(g_value, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = g_rcacheManager->Put(g_ptId, key1, slicePtr);
    EXPECT_EQ(ret, 0);

    FlowAddr flowAddr;
    flowAddr.chunkId = (uint64_t)malloc(len);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = len;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    readSlicePtr = MakeRef<RCacheSlice>(g_ptId, len, addrs, FLOW_MEMORY);
    EXPECT_NE(readSlicePtr, nullptr);
    uint64_t realLen;
    ret = g_rcacheManager->Get(g_ptId, key1, 0ULL, readSlicePtr, rwriter, realLen);
    ret = memcmp(g_value, reinterpret_cast<void *>(readSlicePtr->GetAddrs()[0].chunkId), len);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestRCache, test_rcache_load_ok)
{
    char *key2 = "123123key2";
    uint64_t len = strlen(g_value) + 1;
    RCacheSlicePtr readSlicePtr = nullptr;

    UnderFs::Instance()->Put(key2, g_value, len);

    uint64_t realLen;
    auto ret = g_rcacheManager->Load(g_ptId, key2, 0, len, realLen);
    EXPECT_EQ(ret, 0);

    FlowAddr flowAddr;
    flowAddr.chunkId = (uint64_t)malloc(len);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = len;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    readSlicePtr = MakeRef<RCacheSlice>(g_ptId, len, addrs, FLOW_MEMORY);
    EXPECT_NE(readSlicePtr, nullptr);
    ret = g_rcacheManager->Get(g_ptId, key2, 0ULL, readSlicePtr, rwriter, realLen);
    ret = memcmp(g_value, reinterpret_cast<void *>(readSlicePtr->GetAddrs()[0].chunkId), len);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestRCache, test_rcache_delete_ok)
{
    char *key4 = "123123key3";
    uint64_t len = strlen(g_value) + 1;
    WCacheSlicePtr slicePtr = nullptr;

    auto ret = g_rcacheManager->AllocResources(g_ptId, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = g_slicerOperator.Copy(g_value, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = g_rcacheManager->Put(g_ptId, key4, slicePtr);
    EXPECT_EQ(ret, 0);

    ret = g_rcacheManager->Delete(g_ptId, key4);
    EXPECT_EQ(ret, 0);
}
