/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <cstdint>
#include <libaio.h>
#include "bio_server.h"
#include "bio_server_c.h"
#include "bio_mock.h"
#include "bio_config_instance.h"
#include "cache_slice_operator.h"
#include "wcache_manager.h"
#include "bdm_core.h"
#include "flow_task_pool.h"
#include "flow_manager.h"
#include "tracepoint.h"
#include "cache_overload_ctrl.h"
#include "test_wcache.h"

using namespace ock::bio;

static WCacheManagerPtr gWCacheManager = WCacheManager::Instance();

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

static constexpr uint16_t G_PT_ID = 0;
static constexpr uint16_t G_PT_V = 1;
static uint64_t g_flowId = 0;
static FlowInstance *g_flowInst = nullptr;

static auto reader = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = sliceOperator.Copy(from, to);
    EXPECT_EQ(ret, BIO_OK);
    return ret;
};

static auto wWriter = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = sliceOperator.Copy(from, to);
    return ret;
};

static WCacheSlicePtr gWcacheSlice;
static BResult GetSlice(uint64_t flowId, uint64_t flowOffset, uint64_t length)
{
    SliceKey sliceKey(flowId, flowOffset, FLOW_MEMORY, length, 0);
    return gWCacheManager->GetWCacheSlice(sliceKey, gWcacheSlice);
}

TEST_F(TestWCache, test_create_flow_return_ok)
{
    LOG_INFO("test_create_flow_return_ok");
    bool isDegrade = false;
    uint64_t cacheId = 123;
    auto ret = Cache::Instance().AllocateFlowId(cacheId, G_PT_ID, G_PT_V, g_flowId);
    EXPECT_EQ(ret, BIO_OK);

    ret = Cache::Instance().CreateWCache(cacheId, G_PT_ID, G_PT_V, g_flowId, isDegrade);
    EXPECT_EQ(ret, BIO_OK);

    ret = Cache::Instance().CreateRCache(G_PT_ID, G_PT_V);
    EXPECT_EQ(ret, BIO_OK);

    g_flowInst = new FlowInstance(g_flowId, G_PT_V, isDegrade);
    EXPECT_NE(g_flowInst, nullptr);
}

TEST_F(TestWCache, test_get_slice_param_invalid)
{
    LOG_INFO("test_get_slice_param_invalid");
    SliceKey sliceKey(NO_MAX_VALUE64, 0, FLOW_MEMORY, NO_1024, 0);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    EXPECT_EQ(wSlice, nullptr);

    sliceKey.flowId = g_flowId;
    sliceKey.flowOffset = NO_MAX_VALUE64;
    sliceKey.length = 0;
    ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    EXPECT_EQ(wSlice, nullptr);

    sliceKey.flowOffset = 0;
    sliceKey.length = NO_MAX_VALUE64;
    ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    EXPECT_EQ(wSlice, nullptr);
}

TEST_F(TestWCache, test_put_case_return_ok)
{
    LOG_INFO("test_put_case_return_ok");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);

    Key key = "test_put_case_return_ok";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_slave_send_negotiate_case_return_ok)
{
    LOG_INFO("test_master_negotiate_case_return_ok");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE", 0, 1, userParam);
    auto ret = gWCacheManager->EvictNegotiateThread();
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_slave_send_negotiate_get_masternode_file)
{
    LOG_INFO("test_slave_send_negotiate_get_masternode_file");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "EVICT_NEGOTIATE_GET_MASTERNODE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE", 0, 1, userParam);
    auto ret = gWCacheManager->EvictNegotiateThread();
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE");
    LVOS_HVS_deactiveTracePoint(0, "EVICT_NEGOTIATE_GET_MASTERNODE");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_slave_send_negotiate_get_vectory_empty)
{
    LOG_INFO("test_slave_send_negotiate_get_vectory_empty");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "EVICT_NEGOTIATE_VECTOR_EMPTY", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE", 0, 1, userParam);
    auto ret = gWCacheManager->EvictNegotiateThread();
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE");
    LVOS_HVS_deactiveTracePoint(0, "EVICT_NEGOTIATE_VECTOR_EMPTY");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_put_state_not_normal_case_return_fail)
{
    LOG_INFO("test_put_state_not_normal_case_return_fail");
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wSlice = MakeRef<WCacheSlice>(g_flowId, 0, 1, NO_1024, addrVec);
    EXPECT_NE(wSlice, nullptr);

    Key key = "test_put_state_not_normal_case_return_fail";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_STATE_NOT_NORMAL", 0, 1, userParam);
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_INNER_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_STATE_NOT_NORMAL");

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_put_wcache_put_err_case_return_fail)
{
    LOG_INFO("test_put_wcache_put_err_case_return_fail");
    NetMrInfo bioMrInfo;
    auto ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    WCacheSlicePtr wSlice = MakeRef<WCacheSlice>(g_flowId, 0, 1, NO_1024, addrVec);
    EXPECT_NE(wSlice, nullptr);

    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    Key key = "test_put_wcache_put_err_case_return_fail";
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_PUT_FAIL", 0, 1, userParam);
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_PUT_FAIL");

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_put_repeat_case_return_ok)
{
    LOG_INFO("test_put_repeat_case_return_ok");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);

    Key key = "test_put_repeat_case_return_ok";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_put_nullkey_case_return_fail)
{
    LOG_INFO("test_put_nullkey_case_return_fail");
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    auto ret = gWCacheManager->Put(nullptr, gWcacheSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    Key key = "test_put_nullkey_case_return_fail";
    ret = gWCacheManager->Put(key, nullptr, reader, attr, false);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = gWCacheManager->Put(key, gWcacheSlice, nullptr, attr, false);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
}

TEST_F(TestWCache, test_put_degrate_case_return_ok)
{
    LOG_INFO("test_put_degrate_case_return_ok");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);

    gWCacheManager->SetDegradeState(wSlice, true);
    Key key = "test_put_degrate_case_return_ok";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, true);
    EXPECT_EQ(ret, BIO_OK);
    gWCacheManager->SetDegradeState(wSlice, false);

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
}

TEST_F(TestWCache, test_get_case_return_ok)
{
    LOG_INFO("test_get_case_return_ok");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_get_case_return_ok";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    NetMrInfo bioMrInfo;
    ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    uint64_t realLen = 0;
    ret = gWCacheManager->Get(key, 0, rcacheSlice, wWriter, realLen);
    EXPECT_EQ(ret, BIO_OK);

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_get_offset_over_case_return_err)
{
    LOG_INFO("test_get_offset_over_case_return_err");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_get_offset_over_case_return_err";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    NetMrInfo bioMrInfo;
    ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    uint64_t realLen = 0;
    ret = gWCacheManager->Get(key, NO_MAX_VALUE64, rcacheSlice, wWriter, realLen);
    EXPECT_EQ(ret, BIO_READ_EXCEED);

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_get_offset_err_case_return_err)
{
    LOG_INFO("test_get_offset_err_case_return_err");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_get_offset_err_case_return_err";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    NetMrInfo bioMrInfo;
    ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    uint64_t realLen = 0;
    ret = gWCacheManager->Get(key, NO_100, rcacheSlice, wWriter, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_cache_get_nullkey_case_return_err)
{
    LOG_INFO("test_cache_get_nullkey_case_return_err");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_cache_get_nullkey_case_return_err";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    NetMrInfo bioMrInfo2;
    ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo2);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo2 = { bioMrInfo2.address, static_cast<uint32_t>(bioMrInfo2.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo2) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    uint64_t realLen = 0;
    ret = gWCacheManager->Get(key, 0, rcacheSlice, wWriter, realLen);
    EXPECT_EQ(ret, BIO_OK);
    ret = gWCacheManager->Get(nullptr, 0, rcacheSlice, wWriter, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
    BioServer::Instance()->MemFree(mrInfo2.address);
}

TEST_F(TestWCache, test_cache_get_nullslice_case_return_err)
{
    LOG_INFO("test_cache_get_nullslice_case_return_err");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_cache_get_nullslice_case_return_err";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    NetMrInfo bioMrInfo2;
    ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo2);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo2 = { bioMrInfo2.address, static_cast<uint32_t>(bioMrInfo2.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo2) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    uint64_t realLen = 0;
    ret = gWCacheManager->Get(key, 0, rcacheSlice, wWriter, realLen);
    EXPECT_EQ(ret, BIO_OK);
    ret = gWCacheManager->Get(nullptr, 0, nullptr, wWriter, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
    BioServer::Instance()->MemFree(mrInfo2.address);
}

TEST_F(TestWCache, test_rcache_get_rcahceptr_notexist_case_return_fail)
{
    LOG_INFO("test_rcache_get_rcahceptr_notexist_case_return_fail");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_rcache_get_rcahceptr_notexist_case_return_fail";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    NetMrInfo bioMrInfo2;
    ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo2);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo2 = { bioMrInfo2.address, static_cast<uint32_t>(bioMrInfo2.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo2) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    uint64_t realLen = 0;
    ret = gWCacheManager->Get(key, 0, rcacheSlice, wWriter, realLen);
    EXPECT_EQ(ret, BIO_OK);
    ret = gWCacheManager->Get(nullptr, 0, nullptr, nullptr, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
    BioServer::Instance()->MemFree(mrInfo2.address);
}

TEST_F(TestWCache, test_rcache_get_flow_offset_err_case_return_fail)
{
    LOG_INFO("test_rcache_get_flow_offset_err_case_return_fail");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_rcache_get_flow_offset_err_case_return_fail";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    uint64_t realLen = 0;
    std::vector<FlowAddr> addrVec;
    RCacheSlicePtr slicePtr = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Get(key, 0, slicePtr, wWriter, realLen);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL");

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_cache_get_nullslicewriter_case_return_err)
{
    LOG_INFO("test_cache_get_nullslicewriter_case_return_err");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_cache_get_nullslicewriter_case_return_err";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    NetMrInfo bioMrInfo;
    ret = BioServer::Instance()->MemAlloc(NO_1024, bioMrInfo);
    EXPECT_EQ(ret, BIO_OK);
    MrInfo mrInfo = { bioMrInfo.address, static_cast<uint32_t>(bioMrInfo.size) };
    std::vector<FlowAddr> addrVec = { FlowAddr(mrInfo) };
    RCacheSlicePtr rcacheSlice = MakeRef<RCacheSlice>(G_PT_ID, NO_1024, addrVec);
    uint64_t realLen = 0;
    ret = gWCacheManager->Get(key, 0, rcacheSlice, nullptr, realLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    BioServer::Instance()->MemFree(mrInfo.address);
}

TEST_F(TestWCache, test_stat_case_return_ok)
{
    LOG_INFO("test_stat_case_return_ok");
    uint64_t length = NO_1024;
    uint64_t flowIndex = 0;
    uint64_t flowOffset = g_flowInst->AllocOffset(length, flowIndex);
    SliceKey sliceKey(g_flowId, flowOffset, FLOW_MEMORY, length, flowIndex);
    WCacheSlicePtr wSlice = nullptr;
    auto ret = gWCacheManager->GetWCacheSlice(sliceKey, wSlice);
    EXPECT_EQ(ret, BIO_OK);
    Key key = "test_stat_case_return_ok";
    CacheAttr attr = { 0, LOCAL_AFFINITY, WRITE_BACK };
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_OK);

    CacheObjStat objState;
    ret = gWCacheManager->Stat(G_PT_ID, key, objState);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_list_prefix_null_case_return_ok)
{
    LOG_INFO("test_list_prefix_null_case_return_ok");
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
    LOG_INFO("test_list_prefix_null_underfs_fail_case_return_ok");
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

TEST_F(TestWCache, test_delete_wcache_flowid_notexist_return_ok)
{
    LOG_INFO("test_delete_wcache_flowid_notexist_return_ok");
    uint64_t invalidFlowId = NO_1024;
    auto ret = gWCacheManager->DeleteWCache(invalidFlowId);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_wcache_alloc_case_return_fail)
{
    LOG_INFO("test_wcache_alloc_case_return_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_ALLOC_FAIL", 0, 1, userParam);
    uint16_t ptId = 1;
    auto ret = Cache::Instance().CreateWCache(0, ptId, 0, 0, false);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_ALLOC_FAIL");
}

TEST_F(TestWCache, test_cache_delete_keynull_case_return_err)
{
    LOG_INFO("test_cache_delete_keynull_case_return_err");
    Key key = nullptr;
    auto ret = Cache::Instance().Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
}

TEST_F(TestWCache, test_flush_return_err)
{
    LOG_INFO("test_flush_return_err");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CLEAR_OLD_CACHE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_FLUSH", 0, 1, userParam);
    SyncDataRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() } };
    auto ret = MirrorServer::Instance()->SyncData(req);
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CLEAR_OLD_CACHE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_FLUSH");
}

TEST_F(TestWCache, test_expired_flush_return_ok)
{
    LOG_INFO("test_expired_flush_return_ok");
    auto ret = Cache::Instance().ExpiredClear(G_PT_ID, G_PT_V);
    EXPECT_EQ(ret, BIO_OK);
    ret = gWCacheManager->ExpiredClear(G_PT_ID, G_PT_V);
}

TEST_F(TestWCache, test_handle_proc_broken_err)
{
    LOG_INFO("test_handle_proc_broken_err");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "HANDLE_PROC_BROKEN_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().HandleProcBroken(NO_1024);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_PROC_BROKEN_FAIL");
}

TEST_F(TestWCache, test_handle_proc_broken_expired_err)
{
    LOG_INFO("test_handle_proc_broken_expired_err");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, NO_10, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, NO_10, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
}

TEST_F(TestWCache, test_handle_proc_broken_flush_err)
{
    LOG_INFO("test_handle_proc_broken_flush_err");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, 1, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
}

TEST_F(TestWCache, test_handle_proc_broken_role_err)
{
    LOG_INFO("test_handle_proc_broken_role_err");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_ROLE_ERR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, 1, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_ROLE_ERR");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
}

TEST_F(TestWCache, test_handle_proc_broken_callback)
{
    LOG_INFO("test_handle_proc_broken_callback");
    LVOS_TRACEP_PARAM_S userParam;
    CmPtInfo ptEntry;
    auto ret = Cm::Instance()->GetPtInfo(0, ptEntry);
    WCachePtr wcache = MakeRef<WCache>(1, 1, 1, 1, 1, false);
    bool needDestroy = false;
    bool slaveResult[CLUSTER_SIZEMAX];
    std::fill(std::begin(slaveResult), std::end(slaveResult), true);
    LVOS_HVS_activeTracePoint(0, "SERVER_NET_ASYNC_CALL_FAIL", 0, 1, userParam);
    ret = WCacheManager::Instance()->SendProcBrokenSyncRequest(wcache, ptEntry, 1,
                                                               slaveResult, needDestroy);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_ASYNC_CALL_FAIL");
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestWCache, test_start_pool_threadnum_incorrect_return_fail)
{
    LOG_INFO("test_start_pool_threadnum_incorrect_return_fail");
    FlowTaskPoolPtr flowTaskPool = MakeRef<FlowTaskPool>("testflow");
    auto ret = flowTaskPool->Start(0, NO_16);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
}

TEST_F(TestWCache, test_start_pool_return_ok)
{
    LOG_INFO("test_start_pool_return_ok");
    FlowTaskPoolPtr flowTaskPool = MakeRef<FlowTaskPool>("testflow");
    auto ret = flowTaskPool->Start(NO_8, NO_16);
    EXPECT_EQ(ret, BIO_OK);
    ret = flowTaskPool->Start(NO_8, NO_16);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_service_update_return_retry)
{
    LOG_INFO("test_service_update_return_retry");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_UPGRADE_FLUSH", 0, 1, userParam);
    auto ret = gWCacheManager->ServiceUngradeFlush();
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_UPGRADE_FLUSH");
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
    *bucketId = g_flowId;
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

TEST_F(TestWCache, test_get_evict_offset_return_ok)
{
    LOG_INFO("test_get_evict_offset_return_ok");
    uint64_t flowOffset = 0;
    auto ret = Cache::Instance().GetEvictOffset(g_flowId, flowOffset);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
}

TEST_F(TestWCache, test_get_slice_wcache_flow_offset_err_return_fail)
{
    LOG_INFO("test_get_slice_wcache_flow_offset_err_return_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_STATE_NORMAL", 0, 1, userParam);
    auto ret = GetSlice(g_flowId, 0, NO_1024);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_STATE_NORMAL");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL");
    EXPECT_EQ(ret, BIO_ERR);
}

TEST_F(TestWCache, test_get_slice_wcache_hold_wait_err_return_fail)
{
    LOG_INFO("test_get_slice_wcache_hold_wait_err_return_fail");
    GetSliceRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, 1, 0, 1, NO_128 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_HOLD_WAIT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_STATE_NORMAL", 0, 1, userParam);
    auto ret = GetSlice(g_flowId, 0, NO_MAX_VALUE64-1);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_STATE_NORMAL");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HOLD_WAIT_FAIL");
    EXPECT_EQ(ret, BIO_ERR);
}

TEST_F(TestWCache, test_destroy_flow_ok)
{
    LOG_INFO("test_destroy_flow_ok");
    WCachePtr wCachePtr = MakeRef<WCache>(1, 1, 1, 1, 1, false);
    bool slaveResult[CLUSTER_SIZEMAX];
    std::fill(std::begin(slaveResult), std::end(slaveResult), false);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_DESTROY_LOCAL_FALSE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "BROCK_DESTROY_FLOW_OK", 0, 1, userParam);
    gWCacheManager->HandleProcBrokenDestroyFlow(wCachePtr, 0, slaveResult);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_DESTROY_LOCAL_FALSE");
    LVOS_HVS_deactiveTracePoint(0, "BROCK_DESTROY_FLOW_OK");
}

TEST_F(TestWCache, test_alloc_rcache_ok)
{
    LOG_INFO("test_alloc_rcache_ok");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "ALLOC_DEST_SLICE_NULL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RESOURCE_ENOUGH", 0, 1, userParam);
    WCachePtr wcache = MakeRef<WCache>(1, 1, 1, 1, 1, false);
    FlowAddr flowAddr;
    flowAddr.chunkId = 1;
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = NO_1024;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    WCacheSlicePtr srcSlice = MakeRef<WCacheSlice>(G_PT_ID, 0, 1, NO_1024, addrs);
    WCacheSlicePtr dstSlice = nullptr;

    bool isRcache = true;
    BResult ret = wcache->AllocRCacheResource(srcSlice, dstSlice, isRcache);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "ALLOC_DEST_SLICE_NULL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RESOURCE_ENOUGH");
}

TEST_F(TestWCache, test_bio_server_put_write_slice_null_reply_ok)
{
    LOG_INFO("test_bio_server_put_write_slice_null_reply_ok");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcdslice", KEY_MAX_SIZE);
    req.length = NO_128;
    req.flowId = 0;
    req.mrKey = 1;
    req.sliceLen = 0;
    req.ioStrategy = 0;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WRITE_SLICE_NULL_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerPut(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "WRITE_SLICE_NULL_FAIL");
}

TEST_F(TestWCache, test_bio_olc_low_water_level)
{
    LOG_INFO("test_bio_olc_low_water_level");
    uint64_t frontWriteBw = NO_1;
    uint64_t evict2DiskBw = NO_2;
    uint32_t proc = 0;
    auto ret = CacheOverloadCtrl::Instance().LowWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_1);

    frontWriteBw = NO_3;
    evict2DiskBw = NO_2;
    ret = CacheOverloadCtrl::Instance().LowWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_2);

    frontWriteBw = NO_3;
    evict2DiskBw = NO_1;
    ret = CacheOverloadCtrl::Instance().LowWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_2);

    frontWriteBw = NO_5;
    evict2DiskBw = NO_1;
    ret = CacheOverloadCtrl::Instance().LowWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_3);
}

TEST_F(TestWCache, test_bio_olc_mid_water_level)
{
    LOG_INFO("test_bio_olc_mid_water_level");
    uint64_t frontWriteBw = NO_1;
    uint64_t evict2DiskBw = NO_2;
    uint32_t proc = 0;
    auto ret = CacheOverloadCtrl::Instance().MidWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_1);

    frontWriteBw = NO_3;
    evict2DiskBw = NO_2;
    ret = CacheOverloadCtrl::Instance().MidWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_2);

    frontWriteBw = NO_3;
    evict2DiskBw = NO_1;
    ret = CacheOverloadCtrl::Instance().MidWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_2);

    frontWriteBw = NO_5;
    evict2DiskBw = NO_1;
    ret = CacheOverloadCtrl::Instance().MidWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_3);
}

TEST_F(TestWCache, test_bio_olc_high_water_level)
{
    LOG_INFO("test_bio_olc_high_water_level");
    uint64_t frontWriteBw = NO_1;
    uint64_t evict2DiskBw = NO_2;
    uint32_t proc = 0;
    auto ret = CacheOverloadCtrl::Instance().HighWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_1);

    frontWriteBw = NO_3;
    evict2DiskBw = NO_2;
    ret = CacheOverloadCtrl::Instance().HighWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_2);

    frontWriteBw = NO_3;
    evict2DiskBw = NO_1;
    ret = CacheOverloadCtrl::Instance().HighWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_2);

    frontWriteBw = NO_5;
    evict2DiskBw = NO_1;
    ret = CacheOverloadCtrl::Instance().HighWaterLevelQuota(frontWriteBw, evict2DiskBw, proc);
    EXPECT_EQ(proc, NO_2);
}

TEST_F(TestWCache, test_bio_olc_show)
{
    LOG_INFO("test_bio_olc_show");
    uint64_t vmVec = 0;
    uint64_t totalQuota = 0;
    uint64_t remainQuota = 0;
    std::unordered_map<QuotaHolder, uint64_t, QuotaHolderHash, QuotaHolderEqual> holders;
    CacheOverloadCtrl::Instance().Show(vmVec, totalQuota, remainQuota, holders);
    EXPECT_NE(vmVec, 0);
}

TEST_F(TestWCache, test_bio_olc_recycle)
{
    LOG_INFO("test_bio_olc_recycle");
    QuotaHolder holder = { NO_1, NO_1024 };
    auto holdMap = CacheOverloadCtrl::Instance().GetHolders();
    auto iter = holdMap->find(holder);
    if (iter == holdMap->end()) {
        holdMap->emplace(holder, NO_4194304);
    }

    CacheOverloadCtrl::Instance().RecycleQuota(holder);
    iter = holdMap->find(holder);
    EXPECT_EQ(iter, holdMap->end());
}

TEST_F(TestWCache, test_wcache_tier_metaflow_error)
{
    LOG_INFO("test_wcache_tier_error");

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "FLOW_SEAL_ERR", 0, 1, userParam);
    WCacheTier wCacheTier;
    auto ret = wCacheTier.Seal();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "FLOW_SEAL_ERR");
}

TEST_F(TestWCache, test_wcache_tier_dataflow_error)
{
    LOG_INFO("test_wcache_tier_error");

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "FLOW_SEAL_ERR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "FLOW_DATA_FLOW_ERR", 0, 1, userParam);
    WCacheTier wCacheTier;
    auto ret = wCacheTier.Seal();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "FLOW_SEAL_ERR");
    LVOS_HVS_deactiveTracePoint(0, "FLOW_DATA_FLOW_ERR");
}

TEST_F(TestWCache, test_wcache_destroy_case_return_ok)
{
    LOG_INFO("test_wcache_destroy_case_return_ok");
    LVOS_TRACEP_PARAM_S userParam;
    auto ret = Cache::Instance().DestroyWCache(0, G_PT_ID, G_PT_V, g_flowId);
    EXPECT_EQ(ret, BIO_OK);

    ret = Cache::Instance().DestroyRCache(G_PT_ID);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_wcache_destroy_flowid_unexist_return_ok)
{
    LOG_INFO("test_wcache_destroy_flowid_unexist_return_ok");
    auto ret = Cache::Instance().DestroyWCache(0, G_PT_ID, G_PT_V, NO_1024);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_wcache_destroy_flowid_err_return_ok)
{
    LOG_INFO("test_wcache_destroy_flowid_err_return_ok");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_BROCK_FLOWID_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_flowId);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_BROCK_FLOWID_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
}

TEST_F(TestWCache, test_wcache_destroy_flush_return_ok)
{
    LOG_INFO("test_wcache_destroy_flush_return_ok");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_BROCK_FLUSH", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_flowId);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_BROCK_FLUSH");
}

TEST_F(TestWCache, test_wcache_destroy_expire_return_ok)
{
    LOG_INFO("test_wcache_destroy_expire_return_ok");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_HANDLE_BROCK_EXPIRED_CLEAR", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_flowId);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
    LVOS_HVS_deactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_HANDLE_BROCK_EXPIRED_CLEAR");
}

TEST_F(TestWCache, test_handle_proc_brock)
{
    LOG_INFO("test_handle_proc_brock");
    std::list<WCache*> oldList;
    gWCacheManager->ScanProcCache(getpid(), oldList);
    auto ret = gWCacheManager->HandleProcBrokenImpl(getpid(), oldList);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_bio_cache_get_capacity)
{
    LOG_INFO("test_bio_cache_get_capacity");
    auto result = Cache::Instance().CreateWCache(1, 1, 1, 1, false);
    EXPECT_EQ(result, BIO_OK);

    auto capacity = gWCacheManager->GetWCache(1)->GetCapacity(WCACHE_DISK);
    EXPECT_NE(capacity, -1);

    auto virCapacity = gWCacheManager->GetWCache(1)->GetVirCapacity(WCACHE_DISK);
    EXPECT_NE(virCapacity, -1);

    auto evictOffset = gWCacheManager->GetWCache(1)->GetEvictOffset();
    EXPECT_NE(evictOffset, -1);

    auto truncateIndex = gWCacheManager->GetWCache(1)->GetTruncateIndex();
    EXPECT_NE(truncateIndex, -1);

    FlowPtr flowPtr = FlowManager::Instance()->CreateObject(FLOW_DATA, FLOW_DISK, 1, 1);
    auto ret = gWCacheManager->RecoverCache(flowPtr);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_to_string)
{
    LOG_INFO("test_to_string");
    // disk slice
    char *buffer = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec;
    addrVec.push_back(flowAddr);
    SlicePtr slice = MakeRef<Slice>(NO_1024, addrVec, FLOW_DISK);
    auto diskStr = slice->ToString();
    EXPECT_NE(diskStr.find(std::to_string(slice->GetFlowType())), std::string::npos);

    // memory slice
    char *buffer1 = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr1;
    flowAddr1.chunkId = reinterpret_cast<uint64_t>(buffer1);
    flowAddr1.chunkOffset = 0;
    flowAddr1.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec1;
    addrVec1.push_back(flowAddr1);
    SlicePtr memSlice = MakeRef<Slice>(NO_1024, addrVec1, FLOW_MEMORY);
    auto memStr = slice->ToString();
    EXPECT_NE(memStr.find(std::to_string(slice->GetFlowType())), std::string::npos);
    free(buffer);
    free(buffer1);
}

TEST_F(TestWCache, test_calculate_data_crc_disk_fail)
{
    LOG_INFO("test_calculate_data_crc_disk_fail");
    uint32_t crcValue = 0;
    uint64_t dataOffset = 0;
    uint64_t dataLength = NO_200;
    char *buffer = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec;
    addrVec.push_back(flowAddr);
    SlicePtr slice = MakeRef<Slice>(NO_1024, addrVec, FLOW_DISK);
    auto diskStr = slice->ToString();
    EXPECT_NE(diskStr.find(std::to_string(slice->GetFlowType())), std::string::npos);
    BResult result = slice->CalculateDataCrc(crcValue, dataOffset, dataLength);
    EXPECT_EQ(result, BIO_DISK_IOERR);
    free(buffer);
}

TEST_F(TestWCache, test_calculate_data_crc_ok)
{
    LOG_INFO("test_calculate_data_crc_ok");
    uint32_t crcValue = 0;
    uint64_t dataOffset = 0;
    uint64_t dataLength = NO_200;
    char *buffer = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec;
    addrVec.push_back(flowAddr);
    SlicePtr slice = MakeRef<Slice>(NO_1024, addrVec, FLOW_MEMORY);
    auto diskStr = slice->ToString();
    EXPECT_NE(diskStr.find(std::to_string(slice->GetFlowType())), std::string::npos);
    BResult result = slice->CalculateDataCrc(crcValue, dataOffset, dataLength);
    EXPECT_EQ(result, BIO_OK);
    free(buffer);
}

TEST_F(TestWCache, test_verify_data_crc_success)
{
    LOG_INFO("test_verify_data_crc_success");
    uint32_t crcValue = 0;
    uint64_t dataOffset = 0;
    uint64_t dataLength = NO_100;
    Slice testSlice;
    char *buffer = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec;
    addrVec.push_back(flowAddr);
    SlicePtr slice = MakeRef<Slice>(NO_1024, addrVec, FLOW_MEMORY);
    BResult result = slice->CalculateDataCrc(crcValue, dataOffset, dataLength);
    EXPECT_EQ(result, BIO_OK);
    result = slice->VerifyDataCrc(crcValue, dataOffset, dataLength, &testSlice);
    EXPECT_EQ(result, BIO_OK);
    free(buffer);
}

TEST_F(TestWCache, test_verify_data_crc_mismatch)
{
    LOG_INFO("test_verify_data_crc_mismatch");
    uint32_t originCrc = NO_1024;
    uint64_t dataOffset = 0;
    uint64_t dataLength = NO_100;
    Slice testSlice;
    char *buffer = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec;
    addrVec.push_back(flowAddr);
    SlicePtr slice = MakeRef<Slice>(NO_1024, addrVec, FLOW_MEMORY);
    BResult result = slice->VerifyDataCrc(originCrc, dataOffset, dataLength, &testSlice);
    EXPECT_EQ(result, BIO_CRC_ERR);
    free(buffer);
}

TEST_F(TestWCache, test_slice_ref_release)
{
    LOG_INFO("test_slice_ref_release");
    WCacheSlicePtr dataSlice = nullptr;
    auto sliceRef = MakeRef<WCacheSliceRef>(dataSlice);
    sliceRef->Release();
}

TEST_F(TestWCache, test_get_all_object_disk)
{
    LOG_INFO("test_get_all_object_disk");
    std::map<uint64_t, FlowPtr> flowMaps;
    auto ret = FlowManager::Instance()->GetAllObject(FLOW_DISK, flowMaps);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_get_all_object_mem)
{
    LOG_INFO("test_get_all_object_mem");
    std::map<uint64_t, FlowPtr> flowMaps;
    auto ret = FlowManager::Instance()->GetAllObject(FLOW_MEMORY, flowMaps);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_recover_chunk_success)
{
    LOG_INFO("test_recover_chunk_success");
    FlowRole flowRole = FLOW_DATA;
    FlowType flowType = FLOW_DISK;
    uint64_t flowId = NO_1;
    uint32_t mediaId = NO_1;
    uint64_t chunkSize = NO_4194304;
    uint64_t preLoadSize = NO_1024;
    Flow flow(flowRole, flowType, flowId, mediaId, chunkSize, preLoadSize);
    auto ret = flow.RecoverChunk(0, 0);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_recover_check_empty)
{
    LOG_INFO("test_recover_check_empty");
    FlowRole flowRole = FLOW_DATA;
    FlowType flowType = FLOW_DISK;
    uint64_t flowId = NO_1;
    uint32_t mediaId = NO_1;
    uint64_t chunkSize = NO_4194304;
    uint64_t preLoadSize = NO_1024;
    Flow flow(flowRole, flowType, flowId, mediaId, chunkSize, preLoadSize);
    auto ret = flow.RecoverCheck();
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_recover_check_return_ok)
{
    LOG_INFO("test_recover_check_return_ok");
    FlowRole flowRole = FLOW_DATA;
    FlowType flowType = FLOW_DISK;
    uint64_t flowId = NO_1;
    uint32_t mediaId = NO_1;
    uint64_t chunkSize = NO_4194304;
    uint64_t preLoadSize = NO_1024;
    Flow flow(flowRole, flowType, flowId, mediaId, chunkSize, preLoadSize);
    auto ret = flow.RecoverChunk(0, 0);
    EXPECT_EQ(ret, BIO_OK);
    ret = flow.RecoverCheck();
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, sync_flow_id)
{
    LOG_INFO("sync_flow_id");
    uint64_t flowId = NO_1;
    auto idAllocator = FlowIdAllocator::Instance();
    idAllocator->SyncFlowId(flowId);
}

TEST_F(TestWCache, recover_chunk_return_ok)
{
    LOG_INFO("recover_chunk_return_ok");
    uint32_t mediaId = NO_1;
    uint64_t chunkId = NO_1;
    uint64_t flowId = NO_1;
    uint64_t flowOffset = NO_1;
    auto flowManager = FlowManager::Instance();
    auto ret = flowManager->RecoverChunk(mediaId, chunkId, flowId, flowOffset);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_roll_back_offset)
{
    LOG_INFO("test_roll_back_offset");
    uint64_t flowId = NO_1;
    uint64_t version = 0;
    bool isDegrade = false;
    uint64_t len = NO_1024;
    FlowInstance flowInstance(flowId, version, isDegrade);
    flowInstance.RollbackOffset(len);
}