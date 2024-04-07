/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include <cstdint>
#include "underfs.h"
#include "bio_server.h"
#include "bio_mock.h"
#include "bio_config_instance.h"
#include "cache_slice_operator.h"
#include "rcache_manager.h"
#include "bdm_core.h"
#include "test_rcache.h"

using namespace ock::bio;

static RCacheManagerPtr gRcacheManager = RCacheManager::Instance();

bool TestRCache::gSetup = false;

void TestRCache::SetUp()
{
    if (gSetup) {
        return;
    }

    gSetup = true;
    return;
}

void TestRCache::TearDown()
{
    return;
}

static CacheSliceOperator gSlicerOperator;

static constexpr uint64_t G_PT_ID = 1;
static constexpr uint64_t G_PT_V = 1;
static constexpr Key G_KEY = "123123key";
static constexpr char *G_VALUE = "test/read/cache/data";

static RCacheSlicePtr gRcacheSlice;

auto rwriter = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = gSlicerOperator.Copy(from, to);
    EXPECT_EQ(ret, BIO_OK);
    return ret;
};

TEST_F(TestRCache, test_rcache_create_ok)
{
    auto ret = gRcacheManager->CreateRCache(G_PT_ID, G_PT_V, 0);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_rcache_put_ok)
{
    LOG_INFO("run test_rcache_put");
    uint64_t len = strlen(G_VALUE) + 1;
    WCacheSlicePtr slicePtr = nullptr;

    auto ret = gRcacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = gRcacheManager->Put(G_PT_ID, G_KEY, slicePtr);
    EXPECT_EQ(ret, 0);

    uint64_t totalCap = 0;
    uint64_t usedCap = 0;
    BdmGetCapacity(0, &totalCap, &usedCap);
    LOG_INFO("total cap " << totalCap << " used cap " << usedCap);
    uint64_t needEvictData = len;
    uint64_t haveEvictData;
    gRcacheManager->GetRCacheInstanceByPtId(G_PT_ID)->EvictMemData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(needEvictData, haveEvictData);

    gRcacheManager->GetRCacheInstanceByPtId(G_PT_ID)->EvictDiskData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(needEvictData, haveEvictData);
}

TEST_F(TestRCache, test_rcache_get_ok)
{
    uint64_t len = strlen(G_VALUE) + 1;
    Key key1 = "123123key1";
    WCacheSlicePtr slicePtr = nullptr;
    RCacheSlicePtr readSlicePtr = nullptr;

    auto ret = gRcacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = gRcacheManager->Put(G_PT_ID, key1, slicePtr);
    EXPECT_EQ(ret, 0);

    FlowAddr flowAddr;
    flowAddr.chunkId = (uint64_t)malloc(len);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = len;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    readSlicePtr = MakeRef<RCacheSlice>(G_PT_ID, len, addrs, FLOW_MEMORY);
    EXPECT_NE(readSlicePtr, nullptr);
    uint64_t realLen;
    ret = gRcacheManager->Get(G_PT_ID, key1, 0ULL, readSlicePtr, rwriter, realLen);
    ret = memcmp(G_VALUE, reinterpret_cast<void *>(readSlicePtr->GetAddrs()[0].chunkId), len);
    free((void *)flowAddr.chunkId);
    EXPECT_EQ(ret, 0);

    ret = gRcacheManager->Delete(G_PT_ID, key1);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestRCache, test_rcache_load_ok)
{
    Key key2 = "123123key2";
    uint64_t len = strlen(G_VALUE) + 1;
    RCacheSlicePtr readSlicePtr = nullptr;

    UnderFs::Instance()->Put(key2, G_VALUE, len);

    uint64_t realLen;
    auto ret = gRcacheManager->Load(G_PT_ID, key2, 0, len, realLen);
    EXPECT_EQ(ret, 0);

    FlowAddr flowAddr;
    flowAddr.chunkId = (uint64_t)malloc(len);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = len;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    readSlicePtr = MakeRef<RCacheSlice>(G_PT_ID, len, addrs, FLOW_MEMORY);
    EXPECT_NE(readSlicePtr, nullptr);
    ret = gRcacheManager->Get(G_PT_ID, key2, 0ULL, readSlicePtr, rwriter, realLen);
    ret = memcmp(G_VALUE, reinterpret_cast<void *>(readSlicePtr->GetAddrs()[0].chunkId), len);
    free((void *)flowAddr.chunkId);
    EXPECT_EQ(ret, 0);

    ret = gRcacheManager->Delete(G_PT_ID, key2);
    UnderFs::Instance()->Delete(key2);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestRCache, test_rcache_delete_ok)
{
    Key key4 = "123123key3";
    uint64_t len = strlen(G_VALUE) + 1;
    WCacheSlicePtr slicePtr = nullptr;

    auto ret = gRcacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = gRcacheManager->Put(G_PT_ID, key4, slicePtr);
    EXPECT_EQ(ret, 0);

    ret = gRcacheManager->Delete(G_PT_ID, key4);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestRCache, test_rcache_expired_clear_ok)
{
    auto ret = gRcacheManager->ExpiredClear(G_PT_ID, 2);
    EXPECT_EQ(ret, 0);
}
