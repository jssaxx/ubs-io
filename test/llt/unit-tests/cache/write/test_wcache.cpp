/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <cstdint>
#include <libaio.h>
#include "bio_server.h"
#include "bio_mock.h"
#include "bio_config_instance.h"
#include "cache_slice_operator.h"
#include "wcache_manager.h"
#include "bdm_core.h"
#include "flow_task_pool.h"
#include "flow_manager.h"
#include "tracepoint.h"
#include "test_wcache.h"

using namespace ock::bio;

static WCacheManagerPtr gWcacheManager = WCacheManager::Instance();

bool TestWCache::gSetup = false;

void TestWCache::SetUp()
{
    if (gSetup) {
        return;
    }

    gSetup = true;
    return;
}

void TestWCache::TearDown()
{
    return;
}

static constexpr uint16_t G_PT_ID = 1;
static constexpr uint16_t G_PT_V = 1;
static constexpr Key G_KEY = "123123123";

static uint64_t g_cacheId = 0;
static WCacheSlicePtr gWcacheSlice;

static auto reader = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = sliceOperator.Copy(from, to);
    EXPECT_EQ(ret, BIO_OK);
    return ret;
};

static auto wwriter = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = sliceOperator.Copy(from, to);
    return ret;
};

static BResult GetSlice(uint64_t g_cacheId, uint64_t flowOffset, uint64_t length)
{
    SliceKey sliceKey{ g_cacheId, flowOffset, FLOW_MEMORY, length, 0 };
    return gWcacheManager->GetWCacheSlice(sliceKey, gWcacheSlice);
}

TEST_F(TestWCache, test_cacheId_case_return_ok)
{
    auto ret = gWcacheManager->AllocateFlowId(G_PT_ID, G_PT_V, g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_createcache_case_return_ok)
{
    auto ret = gWcacheManager->CreateWCache(0, g_cacheId, 0, 0, 0, false);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_mirror_server_get_slice)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetSliceRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, g_cacheId, 0, 0, 128 };
    auto ret = mirror->MirrorServerGetSlice(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_mirror_server_get_slice_alloc_fail)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetSliceRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, g_cacheId, 0, 0, 128 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "GET_SLICE_ALLOC_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerGetSlice(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "GET_SLICE_ALLOC_FAIL");
}

TEST_F(TestWCache, test_getslice_case_return_ok)
{
    auto ret = GetSlice(g_cacheId, 0, NO_1024);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_getslice_invalidcacheid_case_return_fail)
{
    auto ret = GetSlice(NO_MAX_VALUE64, 0, NO_1024);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_getslice_invalidoffset_case_return_fail)
{
    auto ret = GetSlice(g_cacheId, NO_MAX_VALUE64, NO_1024);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_getslice_invalidlength_case_return_fail)
{
    auto ret = GetSlice(g_cacheId, 0, NO_MAX_VALUE64);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_getslice_noexistcacheid_case_return_fail)
{
    auto ret = GetSlice(NO_30, 0, NO_1024);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
}

TEST_F(TestWCache, test_put_case_return_ok)
{
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(g_cacheId, 0, 0, NO_1024, addrVec);

    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWcacheManager->Put(G_KEY, wcacheSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_put_state_not_normal_case_return_fail)
{
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(g_cacheId, 0, 1, NO_1024, addrVec);

    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_STATE_NOT_NORMAL", 0, 1, userParam);
    ret = gWcacheManager->Put(G_KEY, wcacheSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_STATE_NOT_NORMAL");

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_put_wcache_put_err_case_return_fail)
{
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(g_cacheId, 0, 1, NO_1024, addrVec);

    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_PUT", 0, 1, userParam);
    ret = gWcacheManager->Put(G_KEY, wcacheSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_PUT");

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_delete_wcache_flowid_notexist_return_ok)
{
    auto ret = gWcacheManager->DeleteWCache(NO_1024);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_delete_wcache_memory_tier_destroy_err_return_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MEMORY_WCACHE_TIER_DESTROY_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_ERASE", 0, 1, userParam);
    auto ret = gWcacheManager->DeleteWCache(g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "MEMORY_WCACHE_TIER_DESTROY_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_ERASE");
}

TEST_F(TestWCache, test_delete_wcache_disk_tier_destroy_err_return_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MEMORY_WCACHE_TIER_DESTROY_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MEMORY_WCACHE_TIER_DESTROY_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "DISK_WCACHE_TIER_DESTROY_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_ERASE", 0, 1, userParam);
    auto ret = gWcacheManager->DeleteWCache(g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "MEMORY_WCACHE_TIER_DESTROY_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MEMORY_WCACHE_TIER_DESTROY_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "DISK_WCACHE_TIER_DESTROY_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_ERASE");
}

TEST_F(TestWCache, test_get_case_return_ok)
{
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);

    uint64_t realLen = 0;
    ret = gWcacheManager->Get(G_KEY, 0, rcacheSlice, wwriter, realLen);
    EXPECT_EQ(ret, BIO_OK);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_get_offset_over_case_return_err)
{
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);

    uint64_t realLen = 0;
    ret = gWcacheManager->Get(G_KEY, NO_MAX_VALUE64, rcacheSlice, wwriter, realLen);
    EXPECT_EQ(ret, BIO_READ_EXCEED);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_get_offset_err_case_return_err)
{
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);

    uint64_t realLen = 0;
    ret = gWcacheManager->Get(G_KEY, NO_100, rcacheSlice, wwriter, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_stat_case_return_ok)
{
    CacheObjStat objState;
    auto ret = gWcacheManager->Stat(G_PT_ID, G_KEY, objState);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_cache_delete_keynull_case_return_err)
{
    auto ret = Cache::Instance().Delete(G_PT_ID, nullptr);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_cache_delete_underfs_err_case_return_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_DELETE_FLOWID_ERR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_MANAGER_DELETE_ERR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "UNDERFS_DELETE_ERR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_DELETE", 0, 1, userParam);
    auto ret = Cache::Instance().Delete(G_PT_ID, G_KEY);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_DELETE_FLOWID_ERR");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_MANAGER_DELETE_ERR");
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_DELETE_ERR");
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_DELETE");

    LVOS_HVS_activeTracePoint(0, "WCACHE_GET_META_SLICE_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Delete(G_PT_ID, G_KEY);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_GET_META_SLICE_FAIL");

    LVOS_HVS_activeTracePoint(0, "CACHE_DELETE_RCACHE_MANAGER_ERR", 0, 1, userParam);
    ret = Cache::Instance().Delete(G_PT_ID, "notexists");
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "CACHE_DELETE_RCACHE_MANAGER_ERR");
}

TEST_F(TestWCache, test_flush_return_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CLEAR_OLD_CACHE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_FLUSH", 0, 1, userParam);
    SyncDataRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() } };
    auto ret = MirrorServer::Instance()->SyncData(req);
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CLEAR_OLD_CACHE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_FLUSH");
}

TEST_F(TestWCache, test_delete_case_return_ok)
{
    auto ret = gWcacheManager->Delete(G_PT_ID, G_KEY);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_put_repeat_case_return_ok)
{
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(g_cacheId, NO_1024, 1, NO_1024, addrVec);

    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWcacheManager->Put(G_KEY, wcacheSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_put_nullkey_case_return_fail)
{
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    auto ret = gWcacheManager->Put(nullptr, gWcacheSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_put_nullslice_case_return_fail)
{
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    auto ret = gWcacheManager->Put(G_KEY, nullptr, reader, attr, false);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_put_nullreader_case_return_fail)
{
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    auto ret = gWcacheManager->Put(G_KEY, gWcacheSlice, nullptr, attr, false);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

static std::vector<FlowAddr> addrVec;
TEST_F(TestWCache, test_put_degrate_case_return_ok)
{
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);

    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(g_cacheId, NO_4096, NO_4, NO_1024, addrVec);

    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    gWcacheManager->SetDegradeState(wcacheSlice, true);
    ret = gWcacheManager->Put("degrate", wcacheSlice, reader, attr, true);
    EXPECT_EQ(ret, BIO_OK);
    gWcacheManager->SetDegradeState(wcacheSlice, false);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_wcache_alloc_case_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_ALLOC_FAIL", 0, 1, userParam);
    uint64_t ptId = 0;
    auto ret = Cache::Instance().CreateWCache(0, ptId, 0, 0, false);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_ALLOC_FAIL");
}

TEST_F(TestWCache, test_wcache_destroy_case_return_ok)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_DESTROY_WCACHE", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_DESTROY_WCACHE");
}

TEST_F(TestWCache, test_wcache_destroy_flowid_unexist_return_ok)
{
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, NO_1024);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_wcache_destroy_flowid_err_return_ok)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_BROCK_FLOWID_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
    sleep(NO_5);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_BROCK_FLOWID_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
}

TEST_F(TestWCache, test_wcache_destroy_flush_return_ok)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_BROCK_FLUSH", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
    sleep(NO_5);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_BROCK_FLUSH");
}

TEST_F(TestWCache, test_wcache_destroy_expire_return_ok)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_BROCK_EXPIRED_CLEAR", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_cacheId);
    EXPECT_EQ(ret, BIO_OK);
    sleep(NO_5);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_BROCK_EXPIRED_CLEAR");
}

TEST_F(TestWCache, test_handle_proc_broken_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "HANDLE_PROC_BROKEN_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().HandleProcBroken(NO_1024);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_PROC_BROKEN_FAIL");
}

TEST_F(TestWCache, test_handle_proc_broken_expired_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_VALIDATE", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, NO_10, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_VALIDATE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
}

TEST_F(TestWCache, test_handle_proc_broken_flush_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_VALIDATE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, 1, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_VALIDATE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
}

TEST_F(TestWCache, test_handle_proc_broken_role_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_VALIDATE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_ROLE_ERR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, 1, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_VALIDATE");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_ROLE_ERR");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
}

TEST_F(TestWCache, test_list_prefix_null_case_return_ok)
{
    std::unordered_map<std::string, CacheObjStat> objs;
    CacheObjStat stat;
    for (int i = 0; i < NO_1024; i++) {
        objs.emplace("test" + std::to_string(i), stat);
    }
    auto ret = Cache::Instance().List(nullptr, 0, true, objs);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_list_prefix_null_underfs_fail_case_return_ok)
{
    std::unordered_map<std::string, CacheObjStat> objs;
    CacheObjStat stat;
    for (int i = 0; i < NO_10; i++) {
        objs.emplace("test" + std::to_string(i), stat);
    }
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_INIT_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().List(nullptr, 0, true, objs);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_INIT_FAIL");
}

TEST_F(TestWCache, test_cache_get_nullkey_case_return_err)
{
    uint64_t realLen = 0;
    BResult ret = Cache::Instance().Get(nullptr, 0, nullptr, nullptr, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_cache_get_nullslicewriter_case_return_err)
{
    uint64_t realLen = 0;
    BResult ret = Cache::Instance().Get(G_KEY, 0, nullptr, nullptr, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_cache_get_nullslice_case_return_err)
{
    uint64_t realLen = 0;
    RCacheSlicePtr slicePtr = MakeRef<RCacheSlice>(NO_100, NO_1024, addrVec);
    BResult ret = Cache::Instance().Get(G_KEY, 0, slicePtr, nullptr, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_wcache_get_case_return_ok)
{
    uint64_t realLen = 0;
    RCacheSlicePtr slicePtr = MakeRef<RCacheSlice>(NO_100, NO_1024, addrVec);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_GET_OK", 0, 1, userParam);
    BResult ret = Cache::Instance().Get(G_KEY, 0, slicePtr, nullptr, realLen);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_GET_OK");
}

TEST_F(TestWCache, test_rcache_get_rcahceptr_notexist_case_return_fail)
{
    uint64_t realLen = 0;
    RCacheSlicePtr slicePtr = MakeRef<RCacheSlice>(NO_100, NO_1024, addrVec);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_GET_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NOT_EXIST", 0, 1, userParam);
    BResult ret = Cache::Instance().Get(G_KEY, 0, slicePtr, nullptr, realLen);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_GET_OK");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NOT_EXIST");
}

TEST_F(TestWCache, test_rcache_get_flow_offset_err_case_return_fail)
{
    uint64_t realLen = 0;
    RCacheSlicePtr slicePtr = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    sleep(NO_5);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL_RESET", 0, 1, userParam);
    BResult ret = Cache::Instance().Get(G_KEY, 0, slicePtr, wwriter, realLen);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL_RESET");
}

TEST_F(TestWCache, test_start_pool_threadnum_incorrect_return_fail)
{
    FlowTaskPoolPtr flowTaskPool = MakeRef<FlowTaskPool>("testflow");
    auto ret = flowTaskPool->Start(0, NO_16);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
}

TEST_F(TestWCache, test_start_pool_return_ok)
{
    FlowTaskPoolPtr flowTaskPool = MakeRef<FlowTaskPool>("testflow");
    auto ret = flowTaskPool->Start(NO_8, NO_16);
    EXPECT_EQ(ret, BIO_OK);
    ret = flowTaskPool->Start(NO_8, NO_16);
    EXPECT_EQ(ret, BIO_OK);
}

void TestWCache::Stub()
{
    MOCKER_CPP(&WCache::IsEmptyEvict, bool (*)()).stubs().will(returnValue(false));
}

static int32_t BdmGetNextUsedChunkIdStub(uint32_t bdmId, uint64_t *chunkId, uint64_t *chunkSize, uint64_t *bucketId,
    uint64_t *bucketOffset)
{
    *chunkId = 0;
    *chunkSize = NO_4194304;
    *bucketId = g_cacheId;
    *bucketOffset = 0;
    return BIO_OK;
}

void TestWCache::RecoverStub()
{
    MOCKER(BdmGetNextUsedChunkId).stubs().will(invoke(BdmGetNextUsedChunkIdStub));
    MOCKER_CPP(&FlowTaskPool::Start, BResult(*)(uint32_t coreThreadNum, uint32_t queueSize))
        .stubs()
        .will(returnValue(BIO_OK));
}

TEST_F(TestWCache, test_flowmanager_recover_case_return_fail)
{
    TestWCache::RecoverStub();
    auto ret = FlowManager::Instance()->Init();
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_get_evict_offset_return_ok)
{
    uint64_t  flowOffset = 0;
    auto ret = Cache::Instance().GetEvictOffset(g_cacheId, flowOffset);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_get_slice_wcache_flow_offset_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_STATE_NORMAL", 0, 1, userParam);
    auto ret = GetSlice(g_cacheId, 0, NO_1024);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_STATE_NORMAL");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL_RESET");
    EXPECT_EQ(ret, BIO_ERR);
}

TEST_F(TestWCache, test_get_slice_wcache_hold_wait_err_return_fail)
{
    GetSliceRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, 1, 0, 1, NO_128 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_HOLD_WAIT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_STATE_NORMAL", 0, 1, userParam);
    auto ret = GetSlice(g_cacheId, 0, NO_1024);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_STATE_NORMAL");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HOLD_WAIT_FAIL");
    EXPECT_EQ(ret, BIO_ERR);
}

TEST_F(TestWCache, test_bio_server_put_write_slice_null_reply_ok)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcdslice", KEY_MAX_SIZE);
    req.length = NO_128;
    req.flowId = g_cacheId;
    req.mrKey = 1;
    req.sliceLen = 0;
    req.ioStratege = 0;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WRITE_SLICE_NULL_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerPut(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "WRITE_SLICE_NULL_FAIL");
}