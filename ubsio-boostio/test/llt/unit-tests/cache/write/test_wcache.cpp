/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE", 0, 1, userParam);
    auto ret = gWCacheManager->EvictNegotiateThread();
    BioHvsDeactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE");
    BioHvsDeactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_slave_send_negotiate_get_masternode_file)
{
    LOG_INFO("test_slave_send_negotiate_get_masternode_file");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "EVICT_NEGOTIATE_GET_MASTERNODE", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE", 0, 1, userParam);
    auto ret = gWCacheManager->EvictNegotiateThread();
    BioHvsDeactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE");
    BioHvsDeactiveTracePoint(0, "EVICT_NEGOTIATE_GET_MASTERNODE");
    BioHvsDeactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_slave_send_negotiate_get_vectory_empty)
{
    LOG_INFO("test_slave_send_negotiate_get_vectory_empty");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "EVICT_NEGOTIATE_VECTOR_EMPTY", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE", 0, 1, userParam);
    auto ret = gWCacheManager->EvictNegotiateThread();
    BioHvsDeactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_TRUE");
    BioHvsDeactiveTracePoint(0, "EVICT_NEGOTIATE_VECTOR_EMPTY");
    BioHvsDeactiveTracePoint(0, "WCACHE_NEGOTIATE_FLAG_CLEAR");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_SLAVE_NEGOTIATE_NO_JUDGE_MASTER");
    uint64_t slices[NO_3];
    slices[0] = 0;
    slices[NO_1] = NO_1;
    slices[NO_2] = NO_2;
    std::vector<bool> reslut;
    reslut.push_back(false);
    BioHvsActiveTracePoint(0, "NO_PROCESS_MASTER_NEGOTIATE_NO_EVICT", 0, 1, userParam);
    gWCacheManager->MasterEvictNegotiate(g_flowId, slices, reslut, NO_3);
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_MASTER_NEGOTIATE_NO_EVICT");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestWCache, test_get_evict_negotiate_info_case)
{
    LOG_INFO("test_get_evict_negotiate_info_case");
    auto ret = gWCacheManager->GetEvictNegotiateInfo();
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "WCACHE_STATE_NOT_NORMAL", 0, 1, userParam);
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_INNER_RETRY);
    BioHvsDeactiveTracePoint(0, "WCACHE_STATE_NOT_NORMAL");

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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "WCACHE_PUT_FAIL", 0, 1, userParam);
    ret = gWCacheManager->Put(key, wSlice, reader, attr, false);
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "WCACHE_PUT_FAIL");

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

    ret = gWCacheManager->Delete(G_PT_ID, key);
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
    sleep(NO_5);
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Get(key, 0, slicePtr, wWriter, realLen);
    EXPECT_EQ(ret, BIO_OK);
    BioHvsDeactiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL");

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

    ret = gWCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
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

    ret = gWCacheManager->Delete(G_PT_ID, key);
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_INIT_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().List(nullptr, 0, true, objs);
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "UNDERFS_INIT_FAIL");
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "WCACHE_ALLOC_FAIL", 0, 1, userParam);
    uint16_t ptId = 1;
    auto ret = Cache::Instance().CreateWCache(0, ptId, 0, 0, false);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    BioHvsDeactiveTracePoint(0, "WCACHE_ALLOC_FAIL");
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_CLEAR_OLD_CACHE", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_FLUSH", 0, 1, userParam);
    SyncDataRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() } };
    auto ret = MirrorServer::Instance()->SyncData(req);
    EXPECT_EQ(ret, BIO_INNER_ERR);
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_CLEAR_OLD_CACHE");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_FLUSH");
}

TEST_F(TestWCache, test_expired_flush_return_ok)
{
    LOG_INFO("test_expired_flush_return_ok");
    auto ret = Cache::Instance().ExpiredClear(G_PT_ID, G_PT_V);
    EXPECT_EQ(ret, BIO_OK);
    ret = gWCacheManager->ExpiredClear(G_PT_ID, G_PT_V);
}

TEST_F(TestWCache, test_wcache_destroy_case_return_ok)
{
    LOG_INFO("test_wcache_destroy_case_return_ok");
    BioTracepointParam userParam;
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_HANDLE_BROCK_FLOWID_FAIL", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_flowId);
    EXPECT_EQ(ret, BIO_OK);
    sleep(NO_5);
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    BioHvsDeactiveTracePoint(0, "WCACHE_HANDLE_BROCK_FLOWID_FAIL");
    BioHvsDeactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
}

TEST_F(TestWCache, test_wcache_destroy_flush_return_ok)
{
    LOG_INFO("test_wcache_destroy_flush_return_ok");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_HANDLE_BROCK_FLUSH", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_flowId);
    EXPECT_EQ(ret, BIO_OK);
    sleep(NO_5);
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
    BioHvsDeactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    BioHvsDeactiveTracePoint(0, "WCACHE_HANDLE_BROCK_FLUSH");
}

TEST_F(TestWCache, test_wcache_destroy_expire_return_ok)
{
    LOG_INFO("test_wcache_destroy_expire_return_ok");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "HANDLE_CACHE_BROKE_OK", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_HANDLE_BROCK_EXPIRED_CLEAR", 0, 1, userParam);
    auto ret = Cache::Instance().DestroyWCache(0, 0, 0, g_flowId);
    EXPECT_EQ(ret, BIO_OK);
    sleep(NO_5);
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_DESTROY_EVICT_THREAD");
    BioHvsDeactiveTracePoint(0, "HANDLE_CACHE_BROKE_OK");
    BioHvsDeactiveTracePoint(0, "WCACHE_HANDLE_BROCK_EXPIRED_CLEAR");
}

TEST_F(TestWCache, test_handle_proc_broken_err)
{
    LOG_INFO("test_handle_proc_broken_err");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "HANDLE_PROC_BROKEN_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().HandleProcBroken(NO_1024);
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "HANDLE_PROC_BROKEN_FAIL");
}

TEST_F(TestWCache, test_handle_proc_broken_expired_err)
{
    LOG_INFO("test_handle_proc_broken_expired_err");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, NO_10, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, NO_10, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR", 0, NO_10, userParam);
    BioHvsActiveTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, NO_10, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, NO_10, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_OK);
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
    BioHvsDeactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR");
    BioHvsDeactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
}

TEST_F(TestWCache, test_handle_proc_broken_flush_err)
{
    LOG_INFO("test_handle_proc_broken_flush_err");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, 1, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_OK);
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    BioHvsDeactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR");
    BioHvsDeactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
}

TEST_F(TestWCache, test_handle_proc_broken_role_err)
{
    LOG_INFO("test_handle_proc_broken_role_err");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_ROLE_ERR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "HANDLE_PROC_BROKE_OK", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE", 0, 1, userParam);
    auto ret = WCacheManager::Instance()->HandleProcBrokenHdl(0);
    EXPECT_EQ(ret, BIO_OK);
    BioHvsDeactiveTracePoint(0, "WCACHE_HANDLE_PROC_BROCK_ROLE_ERR");
    BioHvsDeactiveTracePoint(0, "HANDLE_PROC_BROKE_OK");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_CLEAR_PROC_CACHE");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_FLUSH");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_WCACHE_EXPIRED_CLEAR");
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
    BIO_TRACEP_PARAM_S userParam;
    BioHvsActiveTracePoint(0, "NO_PROCESS_UPGRADE_FLUSH", 0, 1, userParam);
    auto ret = gWCacheManager->ServiceUngradeFlush();
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_UPGRADE_FLUSH");
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
    BIO_TRACEP_PARAM_S userParam;
    BioHvsActiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_STATE_NORMAL", 0, 1, userParam);
    auto ret = GetSlice(g_flowId, 0, NO_1024);
    BioHvsDeactiveTracePoint(0, "WCACHE_STATE_NORMAL");
    BioHvsDeactiveTracePoint(0, "WCACHE_FLOW_OFFSET_FAIL");
    EXPECT_EQ(ret, BIO_ERR);
}

TEST_F(TestWCache, test_get_slice_wcache_hold_wait_err_return_fail)
{
    LOG_INFO("test_get_slice_wcache_hold_wait_err_return_fail");
    GetSliceRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, 1, 0, 1, NO_128 };
    BIO_TRACEP_PARAM_S userParam;
    BioHvsActiveTracePoint(0, "WCACHE_HOLD_WAIT_FAIL", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "WCACHE_STATE_NORMAL", 0, 1, userParam);
    auto ret = GetSlice(g_flowId, 0, NO_MAX_VALUE64-1);
    BioHvsDeactiveTracePoint(0, "WCACHE_STATE_NORMAL");
    BioHvsDeactiveTracePoint(0, "WCACHE_HOLD_WAIT_FAIL");
    EXPECT_EQ(ret, BIO_ERR);
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
    BIO_TRACEP_PARAM_S userParam;
    BioHvsActiveTracePoint(0, "WRITE_SLICE_NULL_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerPut(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    BioHvsDeactiveTracePoint(0, "WRITE_SLICE_NULL_FAIL");
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

    BIO_TRACEP_PARAM_S userParam;
    BioHvsActiveTracePoint(0, "FLOW_SEAL_ERR", 0, 1, userParam);
    WCacheTier wCacheTier;
    auto ret = wCacheTier.Seal();
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "FLOW_SEAL_ERR");
}

TEST_F(TestWCache, test_wcache_tier_dataflow_error)
{
    LOG_INFO("test_wcache_tier_error");

    BIO_TRACEP_PARAM_S userParam;
    BioHvsActiveTracePoint(0, "FLOW_SEAL_ERR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "FLOW_DATA_FLOW_ERR", 0, 1, userParam);
    WCacheTier wCacheTier;
    auto ret = wCacheTier.Seal();
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "FLOW_SEAL_ERR");
    BioHvsDeactiveTracePoint(0, "FLOW_DATA_FLOW_ERR");
}