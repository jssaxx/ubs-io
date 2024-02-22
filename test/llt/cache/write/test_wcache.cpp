/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <gtest/gtest.h>
#include <mockcpp/mokc.h>
#include <mockcpp/mockcpp.hpp>
#include <cstdint>
#include "bio_server.h"
#include "bio_mock.h"
#include "bio_config_instance.h"
#include "cache_slice_operator.h"
#include "wcache_manager.h"
#include "test_wcache.h"

using namespace ock::bio;

static WCacheManagerPtr g_wcacheManager = WCacheManager::Instance();

bool TestWCache::g_setup = false;

void TestWCache::SetUp()
{
    if (g_setup) {
        return;
    }

    g_setup = true;
    return;
}

void TestWCache::TearDown()
{
    return;
}

constexpr uint64_t g_ptId = 1;
constexpr Key g_key = const_cast<char *>("123123123");

static uint64_t g_cacheId = 0;
static WCacheSlicePtr g_wcacheSlice;
auto reader = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = sliceOperator.Copy(from, to);
    EXPECT_EQ(ret, BIO_OK);
    return ret;
};

auto wwriter = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = sliceOperator.Copy(from, to);
    EXPECT_EQ(ret, BIO_OK);
    return ret;
};

BResult GetSlice(uint64_t g_cacheId, uint64_t flowOffset, uint64_t length) {
    SliceKey sliceKey{g_cacheId, flowOffset, FLOW_MEMORY, length, 0};
    return g_wcacheManager->GetWCacheSlice(sliceKey, g_wcacheSlice);
}

TEST_F(TestWCache, test_cacheId_case_return_ok) {
    auto ret = g_wcacheManager->AllocateFlowId(g_ptId, g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_createcache_case_return_ok) {
    auto ret = g_wcacheManager->CreateWCache(g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_deletecache_case_return_ok) {
    auto ret = g_wcacheManager->DeleteWCache(g_ptId);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_getslice_case_return_ok) {
    auto ret = GetSlice(g_cacheId, 0, 1024);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_getslice_invalidcacheid_case_return_fail) {
    auto ret = GetSlice(NO_MAX_VALUE64, 0, 1024);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_getslice_invalidoffset_case_return_fail) {
    auto ret = GetSlice(g_cacheId, NO_MAX_VALUE64, 1024);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_getslice_invalidlength_case_return_fail) {
    auto ret = GetSlice(g_cacheId, 0, NO_MAX_VALUE64);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_getslice_noexistcacheid_case_return_fail) {
    auto ret = GetSlice(30, 0, 1024);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
}

TEST_F(TestWCache, test_put_case_return_ok) {
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(g_cacheId, 0, 0, 1024, addrVec);

    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = g_wcacheManager->Put(g_key, wcacheSlice, reader, attr);
    EXPECT_EQ(ret, BIO_OK);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_get_case_return_ok) {
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(g_ptId, 1024, addrVec);

    uint64_t realLen = 0;
    ret = g_wcacheManager->Get(g_key, 0, rcacheSlice, wwriter, realLen);
    EXPECT_EQ(ret, BIO_OK);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_stat_case_return_ok) {
    CacheObjStat objState;
    auto ret = g_wcacheManager->Stat(g_ptId, g_key, objState);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_delete_case_return_ok) {
    auto ret = g_wcacheManager->Delete(g_ptId, g_key);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_put_repeat_case_return_ok) {
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(g_cacheId, 1024, 1, 1024, addrVec);

    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = g_wcacheManager->Put(g_key, wcacheSlice, reader, attr);
    EXPECT_EQ(ret, BIO_OK);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_evict_case_return_ok) {
    auto ret = g_wcacheManager->Flush(g_ptId, 0);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_put_nullkey_case_return_fail) {
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    auto ret = g_wcacheManager->Put(nullptr, g_wcacheSlice, reader, attr);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_put_nullslice_case_return_fail) {
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    auto ret = g_wcacheManager->Put(g_key, nullptr, reader, attr);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_put_nullreader_case_return_fail) {
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    auto ret = g_wcacheManager->Put(g_key, g_wcacheSlice, nullptr, attr);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

