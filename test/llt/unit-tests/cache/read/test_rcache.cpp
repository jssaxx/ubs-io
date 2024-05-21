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
#include "tracepoint.h"
#include "flow_manager.h"
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

static auto rwriter = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
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
    uint64_t len = strlen(G_VALUE) + 1;
    WCacheSlicePtr slicePtr = nullptr;

    auto ret = gRcacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, BIO_OK);

    ret = gRcacheManager->Put(G_PT_ID, G_KEY, slicePtr);
    EXPECT_EQ(ret, BIO_OK);

    uint64_t totalCap = 0;
    uint64_t usedCap = 0;
    BdmGetCapacity(0, &totalCap, &usedCap);
    uint64_t needEvictData = len;
    uint64_t haveEvictData;
    gRcacheManager->GetRCacheInstanceByPtId(G_PT_ID)->EvictMemData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(needEvictData, haveEvictData);

    gRcacheManager->GetRCacheInstanceByPtId(G_PT_ID)->EvictDiskData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(needEvictData, haveEvictData);
}

TEST_F(TestRCache, test_bio_server_expire_clear_ptid_err)
{
    MirrorServerCrbPtr crb = BioServer::Instance()->GetMirrorCrb();
    CmPtInfo ptInfo;
    ptInfo.ptId = NO_128;
    ptInfo.version = 1;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR", 0, NO_2, userParam);
    auto ret = crb->JobExpiredClear(ptInfo);
    EXPECT_EQ(ret, BIO_ERR);

    LVOS_HVS_activeTracePoint(0, "WCACHE_EXPIRED_CLEAR_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_INDEX_TABLE_FAIL", 0, 1, userParam);
    ret = crb->JobExpiredClear(ptInfo);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_EXPIRED_CLEAR_OK");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_INDEX_TABLE_FAIL");

    ptInfo.ptId = G_PT_ID;
    ptInfo.version = NO_2;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_STOP_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_EVICT_ERR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_EVICT_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_RELEASE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_DESTROY_INDEX", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "CACHE_EXPIRED_ERR", 0, NO_2, userParam);
    ret = crb->JobExpiredClear(ptInfo);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_STOP_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_EVICT_ERR");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_EVICT_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_RELEASE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_DESTROY_INDEX");
    LVOS_HVS_deactiveTracePoint(0, "CACHE_EXPIRED_ERR");
}

TEST_F(TestRCache, test_rcache_put_ptid_unexist_return_fail)
{
    uint64_t len = strlen(G_VALUE) + 1;
    WCacheSlicePtr slicePtr = nullptr;

    auto ret = gRcacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = gRcacheManager->Put(NO_10, G_KEY, slicePtr);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
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

TEST_F(TestRCache, test_cache_extra_create_rcache_get_err)
{
    auto ret = Cache::Instance().ExtraCreateRCache(NO_128, NO_10);
    EXPECT_EQ(ret, ock::bio::BIO_ERR);
}

TEST_F(TestRCache, test_cache_extra_create_rcache_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_FIND", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_ALLOC_OBJ_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().ExtraCreateRCache(G_PT_ID, G_PT_V);
    EXPECT_EQ(ret, ock::bio::BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_FIND");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_ALLOC_OBJ_FAIL");
}

TEST_F(TestRCache, test_cache_recover_return_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, ock::bio::BIO_NOT_READY);
    LVOS_HVS_deactiveTracePoint(0, "CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL");
}

TEST_F(TestRCache, test_flowmanager_init_task_pool_exeception_return_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "ALLOC_TASK_POOL_FAIL", 0, NO_3, userParam);
    LVOS_HVS_activeTracePoint(0, "ALLOC_TASK_POOL_FAIL_RESET", 0, NO_3, userParam);
    auto ret = FlowManager::Instance()->Init();
    EXPECT_EQ(ret, ock::bio::BIO_ERR);

    LVOS_HVS_activeTracePoint(0, "ALLOC_TASK_POOL_FAIL_RESET_OUTER", 0, NO_2, userParam);
    LVOS_HVS_activeTracePoint(0, "ALLOC_DISK_TASK_POOL_FAIL", 0, NO_2, userParam);
    LVOS_HVS_activeTracePoint(0, "ALLOC_DISK_TASK_POOL_FAIL_RESET", 0, 1, userParam);
    ret = FlowManager::Instance()->Init();
    EXPECT_EQ(ret, ock::bio::BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "ALLOC_DISK_TASK_POOL_FAIL_RESET");

    LVOS_HVS_activeTracePoint(0, "ALLOC_DISK_TASK_POOL_FAIL_RESET_OUTER", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_EXECUTOR_POOL_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MEXECSERVICE_START_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "EXECUTOR_SERVICE_STOP_TASK_NULL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "EXECUTOR_SERVICE_MSERVICE_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_EXECUTOR_SERVICE_JOIN", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "EXECUTOR_SERVICE_MSERVICE_FAIL_RESET", 0, 1, userParam);
    ret = FlowManager::Instance()->Init();
    EXPECT_EQ(ret, ock::bio::BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "ALLOC_TASK_POOL_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "ALLOC_TASK_POOL_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "ALLOC_TASK_POOL_FAIL_RESET_OUTER");
    LVOS_HVS_deactiveTracePoint(0, "ALLOC_DISK_TASK_POOL_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "ALLOC_DISK_TASK_POOL_FAIL_RESET_OUTER");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_EXECUTOR_POOL_START");
    LVOS_HVS_deactiveTracePoint(0, "MEXECSERVICE_START_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "EXECUTOR_SERVICE_STOP_TASK_NULL");
    LVOS_HVS_deactiveTracePoint(0, "EXECUTOR_SERVICE_MSERVICE_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_EXECUTOR_SERVICE_JOIN");
    LVOS_HVS_deactiveTracePoint(0, "EXECUTOR_SERVICE_MSERVICE_FAIL_RESET");
}

TEST_F(TestRCache, test_cache_init_rcache_init_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "RCACHE_MANAGER_INIT_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().Init();
    EXPECT_EQ(ret, ock::bio::BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_MANAGER_INIT_FAIL");
}

TEST_F(TestRCache, test_cache_init_wcache_init_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "RCACHE_MANAGER_INIT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_MANAGER_INIT_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_MANAGER_INIT_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().Init();
    EXPECT_EQ(ret, ock::bio::BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_MANAGER_INIT_FAIL");
}

TEST_F(TestRCache, test_rcache_slice)
{
    uint32_t length = NO_1024;
    char *buffer = static_cast<char*>(malloc(length));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = length;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    RCacheSlicePtr readSlicePtr = MakeRef<RCacheSlice>(G_PT_ID, length, addrs, FLOW_MEMORY);
    readSlicePtr->GetSerializeLen();

    auto ret = readSlicePtr->Serialize(nullptr, length);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = readSlicePtr->Deserialize(nullptr, length);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    char* sliceBuf = static_cast<char *>(malloc(length));
    ret = readSlicePtr->Serialize(sliceBuf, length);
    EXPECT_EQ(ret, BIO_OK);

    ret = readSlicePtr->Deserialize(sliceBuf, length);
    EXPECT_EQ(ret, BIO_OK);

    ret = readSlicePtr->Deserialize(sliceBuf, 0);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    free(sliceBuf);
    free(buffer);
}

TEST_F(TestRCache, test_wcache_slice)
{
    uint32_t length = NO_1024;
    char *buffer = static_cast<char*>(malloc(length));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = length;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(G_PT_ID, 0, 1, NO_1024, addrs);

    auto ret = wcacheSlice->Serialize(nullptr, length);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = wcacheSlice->Deserialize(nullptr, length);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    char* sliceBuf = static_cast<char *>(malloc(length));
    ret = wcacheSlice->Deserialize(sliceBuf, 0);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    free(sliceBuf);
    free(buffer);
}

TEST_F(TestRCache, test_cache_recover_err)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "CACHE_RECOVER_TYPE_FAIL", 0, NO_24, userParam);
    LVOS_HVS_activeTracePoint(0, "CACHE_RECOVER_TYPE_INNER_FAIL", 0, NO_24, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CACHE_RECOVER", 0, NO_24, userParam);
    auto ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, ock::bio::BIO_OK);

    LVOS_HVS_activeTracePoint(0, "CACHE_RECOVER_CACHE_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, ock::bio::BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "CACHE_RECOVER_TYPE_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "CACHE_RECOVER_TYPE_INNER_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CACHE_RECOVER");
    LVOS_HVS_deactiveTracePoint(0, "CACHE_RECOVER_CACHE_FAIL");

    LVOS_HVS_activeTracePoint(0, "WCACHE_TIER_ALLOC_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, ock::bio::BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_TIER_ALLOC_FAIL");

    LVOS_HVS_activeTracePoint(0, "WCACHE_TIER_TYPE_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, ock::bio::BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_TIER_TYPE_FAIL");

    LVOS_HVS_activeTracePoint(0, "RECOVER_CACHE_FLOWID_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, ock::bio::BIO_NOT_EXISTS);
    LVOS_HVS_deactiveTracePoint(0, "RECOVER_CACHE_FLOWID_FAIL");
}
