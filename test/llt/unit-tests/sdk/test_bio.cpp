/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <map>
#include <mockcpp/mockcpp.hpp>
#include <semaphore.h>
#include "gtest/gtest.h"
#include "bio_c.h"
#include "bio_client.h"
#include "bio_client_net.h"
#include "bio_mock.h"
#include "message.h"
#include "bio_functions.h"
#include "cm.h"
#include "flow_instance.h"
#include "server/pt/cm_pt_calc_fixed_state.h"
#include "cm_comm.h"
#include "server/pt/cm_pt_calc_fixed.h"
#include "server/interceptor_server.h"
#include "tracepoint.h"
#include "test_bio.h"
#include "bio_client_agent.h"

using namespace ock::bio;

bool TestBio::gSetup = false;

static constexpr uint32_t G_TENANT_ID = 5;
static constexpr char *G_KEY = "456123keybio";
static constexpr uint64_t G_LENGTH = 1024;
static constexpr uint64_t G_INVALID_TENANT_ID = 0;
static constexpr uint32_t G_PT_TIMES = 512;

static ObjLocation g_Location{};

void TestBio::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestBio::TearDown()
{
    return;
}

TEST_F(TestBio, test_bio_create_cache)
{
    LOG_INFO("test_bio_create_cache");
    uint64_t tenantId = 12341UL;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_EXISTS);

    tenantId = 121UL;
    ret = BioCreateCache({ tenantId, AFFINITY_BUTT, strategy });
    EXPECT_EQ(ret, RET_CACHE_EPERM);
}

TEST_F(TestBio, test_bio_query_cache_instance)
{
    LOG_INFO("test_bio_query_cache_instance");
    uint64_t tenantId = 12343;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_OK);

    CacheDescriptor desc;
    ret = BioGetCache(tenantId, &desc);
    EXPECT_EQ(ret, RET_CACHE_OK);
    EXPECT_EQ(tenantId, desc.tenantId);
    EXPECT_EQ(affinity, desc.affinity);
    EXPECT_EQ(strategy, desc.strategy);

    auto cacheInstance = ock::bio::BioService::GetCache(tenantId);
    EXPECT_EQ(tenantId, cacheInstance.tenantId);
    EXPECT_EQ(affinity, cacheInstance.affinity);
    EXPECT_EQ(strategy, cacheInstance.strategy);

    ret = BioGetCache(tenantId, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioGetCache(G_INVALID_TENANT_ID, &desc);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_bio_destroy_cache)
{
    LOG_INFO("test_bio_destroy_cache");
    uint64_t tenantId = 12344;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_OK);

    CacheDescriptor desc;
    ret = BioGetCache(tenantId, &desc);
    EXPECT_EQ(ret, RET_CACHE_OK);
    EXPECT_EQ(tenantId, desc.tenantId);
    EXPECT_EQ(affinity, desc.affinity);
    EXPECT_EQ(strategy, desc.strategy);

    ret = BioDestroyCache(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioGetCache(tenantId, &desc);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    ret = BioDestroyCache(G_INVALID_TENANT_ID);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_list_cache)
{
    LOG_INFO("test_list_cache");
    uint64_t tenantId = 12345;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_OK);

    auto cacheList = ock::bio::BioService::ListCache();
    EXPECT_FALSE(cacheList.empty());
    for (auto &item : cacheList) {
        EXPECT_EQ(BioDestroyCache(item.tenantId), RET_CACHE_OK);
    }
    cacheList.clear();
    cacheList = ock::bio::BioService::ListCache();
    EXPECT_TRUE(cacheList.empty());
}

TEST_F(TestBio, test_bio_calc_location)
{
    LOG_INFO("test_bio_calc_location");
    CacheDescriptor desc = { G_TENANT_ID, LOCAL_AFFINITY, WRITE_BACK };
    auto ret = BioCreateCache(desc);
    EXPECT_EQ(ret, RET_CACHE_OK);

    uint32_t sliceId = 1;
    ret = BioCalcLocation(G_TENANT_ID, sliceId, &g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioCalcLocation(G_TENANT_ID, sliceId, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ObjLocation location;
    ret = BioCalcLocation(G_INVALID_TENANT_ID, sliceId, &location);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_bio_put)
{
    LOG_INFO("test_bio_put");
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *)value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(G_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioPut(G_TENANT_ID, "...abc", value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioPut(G_INVALID_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    ret = BioPut(G_TENANT_ID, nullptr, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, 1, userParam);
    ret = BioPut(G_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, 1, userParam);
    ret = BioPut(G_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    uint64_t tenantId = 187UL;
    static ObjLocation location{};
    CacheDescriptor desc = { tenantId, LOCAL_AFFINITY, WRITE_BACK };
    ret = BioCreateCache(desc);
    EXPECT_EQ(ret, RET_CACHE_OK);

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_PREPARE_FAIL", 0, 1, userParam);
    ret = BioPut(tenantId, "key187", "testvalue", 2UL, location);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_PREPARE_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY");
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_RSP_NUM_ERROR", 0, 1, userParam);
    ret = BioPut(tenantId, "key204", "testvalue204", 4UL, location);
    EXPECT_EQ(ret, RET_CACHE_ERROR);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_RSP_NUM_ERROR");
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME");

    ret = BioDestroyCache(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);

    fclose(fp);
}

TEST_F(TestBio, test_bio_put_diff_size_case)
{
    LOG_INFO("test_bio_put_diff_size_case");
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    uint64_t length = 6000UL;
    std::string value(length, ' ');
    EXPECT_EQ(fread((void *)value.c_str(), sizeof(char), length, fp), length);
    auto ret = BioPut(G_TENANT_ID, "4_8k", value.c_str(), length, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);

    uint64_t length1 = 10000UL;
    std::string value1(length1, ' ');
    EXPECT_EQ(fread((void *)value1.c_str(), sizeof(char), length1, fp), length1);
    ret = BioPut(G_TENANT_ID, "8_64k", value1.c_str(), length1, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);

    uint64_t length2 = 100000UL;
    std::string value2(length2, ' ');
    EXPECT_EQ(fread((void *)value2.c_str(), sizeof(char), length2, fp), length2);
    ret = BioPut(G_TENANT_ID, "64_128k", value2.c_str(), length2, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);

    uint64_t length3 = 160000UL;
    std::string value3(length3, ' ');
    EXPECT_EQ(fread((void *)value3.c_str(), sizeof(char), length3, fp), length3);
    ret = BioPut(G_TENANT_ID, "128_256k", value3.c_str(), length3, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);

    uint64_t length4 = 300000UL;
    std::string value4(length4, ' ');
    EXPECT_EQ(fread((void *)value4.c_str(), sizeof(char), length4, fp), length4);
    ret = BioPut(G_TENANT_ID, "256K_1M", value4.c_str(), length4, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);

    uint64_t length5 = 1500000UL;
    std::string value5(length5, ' ');
    EXPECT_EQ(fread((void *)value5.c_str(), sizeof(char), length5, fp), length5);
    ret = BioPut(G_TENANT_ID, "1M_2M", value5.c_str(), length5, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);

    uint64_t length6 = 3300000UL;
    std::string value6(length6, ' ');
    EXPECT_EQ(fread((void *)value6.c_str(), sizeof(char), length6, fp), length6);
    ret = BioPut(G_TENANT_ID, "2M_4M", value6.c_str(), length6, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    fclose(fp);
}

TEST_F(TestBio, test_bio_get)
{
    LOG_INFO("test_bio_get");
    char *value = new char[G_LENGTH];
    uint64_t realLen = 0;
    LVOS_TRACEP_PARAM_S userParam;

    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, value, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioGet(G_INVALID_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, G_PT_TIMES, userParam);
    ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_GET_RETRY", 0, 1, userParam);
    ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_GET_RETRY");

    LVOS_HVS_activeTracePoint(0, "SDK_CLIENT_GET_CEPH_STAT_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_GET_RETRY", 0, 1, userParam);
    ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_GET_RETRY");
    LVOS_HVS_deactiveTracePoint(0, "SDK_CLIENT_GET_CEPH_STAT_OK");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_GET_RETRY", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SDK_CLIENT_GET_CEPH_STAT_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SDK_CLIENT_GET_CEPH_STAT_SIZE", 0, 1, userParam);
    ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "SDK_CLIENT_GET_CEPH_STAT_SIZE");
    LVOS_HVS_deactiveTracePoint(0, "SDK_CLIENT_GET_CEPH_STAT_OK");
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_GET_RETRY");
    delete[] value;
}

TEST_F(TestBio, test_bio_get_external_stat)
{
    LOG_INFO("test_bio_get_external_stat");
    uint64_t realLen0 = 6000UL;
    char *value0 = new char[realLen0];
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "RCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NOT_EXIST", 0, 1, userParam);
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen0, g_Location, value0, &realLen0);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_NOT_EXIST");
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    delete[] value0;
}

TEST_F(TestBio, test_bio_get_external_rcache)
{
    LOG_INFO("test_bio_get_external_rcache");
    uint64_t realLen0 = 6000UL;
    char *value0 = new char[realLen0];
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_MODIFY_REALLENGTH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_NO_STAT", 0, 1, userParam);
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen0, g_Location, value0, &realLen0);
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_NO_STAT");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_MODIFY_REALLENGTH");
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    delete[] value0;
}

TEST_F(TestBio, test_bio_get_external_rcache_fail)
{
    LOG_INFO("test_bio_get_external_rcache_fail");
    uint64_t realLen0 = 6000UL;
    char *value0 = new char[realLen0];
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "GET_EXTERNAL_RCACHE_MALLOC_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_EXTERNAL_GETUNDERFS_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_MODIFY_REALLENGTH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_NO_STAT", 0, 1, userParam);
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen0, g_Location, value0, &realLen0);
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_NO_STAT");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_MODIFY_REALLENGTH");
    LVOS_HVS_deactiveTracePoint(0, "GET_EXTERNAL_GETUNDERFS_OK");
    LVOS_HVS_deactiveTracePoint(0, "GET_EXTERNAL_RCACHE_MALLOC_FAIL");
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value0;
}

TEST_F(TestBio, test_bio_get_external_rcache_underfs)
{
    LOG_INFO("test_bio_get_external_rcache_underfs");
    uint64_t realLen0 = 6000UL;
    char *value0 = new char[realLen0];
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "GET_EXTERNAL_GETUNDERFS_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_MODIFY_REALLENGTH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_NO_STAT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_EXTERBAL_OPEN_CRC", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_EXTERBAL_CRC_OK", 0, 1, userParam);
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen0, g_Location, value0, &realLen0);
    LVOS_HVS_deactiveTracePoint(0, "GET_EXTERBAL_CRC_OK");
    LVOS_HVS_deactiveTracePoint(0, "GET_EXTERBAL_OPEN_CRC");
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_NO_STAT");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_MODIFY_REALLENGTH");
    LVOS_HVS_deactiveTracePoint(0, "GET_EXTERNAL_GETUNDERFS_OK");
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value0;
}

TEST_F(TestBio, test_bio_get_external_malloc)
{
    LOG_INFO("test_bio_get_external_malloc");
    uint64_t realLen0 = 6000UL;
    char *value0 = new char[realLen0];
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "GET_EXTERNAL_GETUNDERFS_OK", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_MODIFY_REALLENGTH", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "WCACHE_NOT_EXIST", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_NO_STAT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "GET_UNDERFS_NOT_ENOUGHRESOURCE", 0, 1, userParam);
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen0, g_Location, value0, &realLen0);
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_NOT_ENOUGHRESOURCE");
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_NO_STAT");
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_NOT_EXIST");
    LVOS_HVS_deactiveTracePoint(0, "GET_UNDERFS_MODIFY_REALLENGTH");
    LVOS_HVS_deactiveTracePoint(0, "GET_EXTERNAL_GETUNDERFS_OK");
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value0;
}

TEST_F(TestBio, test_bio_get_diff_size)
{
    LOG_INFO("test_bio_get_diff_size");
    uint64_t realLen0 = 6000UL;
    char *value0 = new char[realLen0];
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen0, g_Location, value0, &realLen0);
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value0;

    uint64_t realLen = 10000UL;
    char *value = new char[realLen];
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen, g_Location, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value;

    uint64_t realLen1 = 100000UL;
    char *value1 = new char[realLen1];
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen1, g_Location, value1, &realLen1);
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value1;

    uint64_t realLen2 = 160000UL;
    char *value2 = new char[realLen2];
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen2, g_Location, value2, &realLen2);
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value2;

    uint64_t realLen3 = 300000UL;
    char *value3 = new char[realLen3];
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen3, g_Location, value3, &realLen3);
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value3;

    uint64_t realLen4 = 1500000UL;
    char *value4 = new char[realLen4];
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen4, g_Location, value4, &realLen4);
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value4;

    uint64_t realLen5 = 3300000UL;
    char *value5 = new char[realLen5];
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen5, g_Location, value5, &realLen5);
    EXPECT_EQ(ret, RET_CACHE_OK);
    delete[] value5;
}

TEST_F(TestBio, test_bio_get_case_return_fail)
{
    LOG_INFO("test_bio_get_case_return_fail");
    uint64_t realLen = 3300000UL;
    char *value = new char[realLen];
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, 0, g_Location, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    delete[] value;

    uint64_t realLen1 = 4194305UL;
    char *value1 = new char[realLen1];
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen1, g_Location, value1, &realLen1);
    EXPECT_EQ(ret, RET_CACHE_READ_EXCEED);
    delete[] value1;
}

TEST_F(TestBio, test_bio_list_all)
{
    LOG_INFO("test_bio_list_all");
    auto prefix = "456";
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(G_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_OK);
    EXPECT_EQ(objNum, 1);
    BioFreeListResources(objs, objNum);

    ret = BioListAll(G_TENANT_ID, prefix, &objs, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioListAll(G_INVALID_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    ret = BioListAll(G_TENANT_ID, nullptr, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioListAll(G_TENANT_ID, nullptr, nullptr, &objNum);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
}

TEST_F(TestBio, test_bio_stat)
{
    LOG_INFO("test_bio_stat");
    ObjStat keyStat;
    auto ret = BioStat(G_TENANT_ID, G_KEY, g_Location, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_OK);
    EXPECT_EQ(keyStat.size, G_LENGTH);

    ret = BioStat(G_TENANT_ID, nullptr, g_Location, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_ERROR);

    std::string invalidKey(KEY_MAX_SIZE + 1, ' ');
    ret = BioStat(G_TENANT_ID, invalidKey.c_str(), g_Location, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioStat(G_TENANT_ID, G_KEY, g_Location, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioStat(G_INVALID_TENANT_ID, G_KEY, g_Location, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CHECK_PT_FAIL", 0, 1, userParam);
    ret = BioStat(G_TENANT_ID, G_KEY, g_Location, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CHECK_PT_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_STAT_RECV_FAIL", 0, 1, userParam);
    ret = BioStat(G_TENANT_ID, G_KEY, g_Location, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_STAT_RECV_FAIL");
}

namespace {
struct LoadContext {
    sem_t sem;
    CResult result;
};
}

static void TestCallback(void *context, int32_t result)
{
    auto loadCtx = reinterpret_cast<LoadContext *>(context);
    loadCtx->result = static_cast<CResult>(result);
    sem_post(&(loadCtx->sem));
}

TEST_F(TestBio, test_bio_load)
{
    LOG_INFO("test_bio_load");
    LoadContext loadCtx;
    sem_init(&(loadCtx.sem), 0, 0);
    loadCtx.result = RET_CACHE_OK;
    auto ret = BioLoad(G_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioLoad(G_INVALID_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    ret = BioLoad(G_TENANT_ID, nullptr, 0, G_LENGTH, g_Location, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, 1, userParam);
    ret = BioLoad(G_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CHECK_PT_FAIL", 0, 1, userParam);
    ret = BioLoad(G_TENANT_ID, G_KEY, 0, G_LENGTH, g_Location, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CHECK_PT_FAIL");

    sem_wait(&(loadCtx.sem));
    sem_destroy(&(loadCtx.sem));
}

TEST_F(TestBio, test_bio_alloc_cache_space_return_fail)
{
    uint64_t objectId = 2UL;
    CacheSpaceDesc addressDesc;
    addressDesc.loc = { 0, 0 };
    addressDesc.allocLoc = 1;

    constexpr uint64_t tenantId = 10000UL;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioAllocCacheSpace(tenantId, objectId, NO_1024, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, 1, userParam);
    ret = BioAllocCacheSpace(tenantId, objectId, NO_1024, &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_QUERY_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, 1, userParam);
    ret = BioAllocCacheSpace(tenantId, objectId, NO_1024, &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_QUERY_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL", 0, 1, userParam);
    ret = BioAllocCacheSpace(tenantId, objectId, NO_1024, &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_ADDRNUM_INVALID", 0, 1, userParam);
    ret = BioAllocCacheSpace(tenantId, objectId, NO_1024, &addressDesc);
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_ADDRNUM_INVALID");

    ret = BioDestroyCache(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_put_copy_free)
{
    LOG_INFO("test_bio_put_copy_free");
    uint64_t objectId = 0;
    CacheSpaceDesc addressDesc;
    addressDesc.loc.location[0] = 0;
    addressDesc.allocLoc = 1;
    objectId = NO_10;

    auto ret = BioAllocCacheSpace(G_INVALID_TENANT_ID, objectId, NO_1024, &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    ret = BioPutWithCopyFree(G_INVALID_TENANT_ID, "putwithcopyfree", &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    constexpr uint64_t tenantId = 10001UL;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioPutWithCopyFree(tenantId, "putwithcopyfree1", nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioPutWithCopyFree(tenantId, nullptr, &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    objectId = 11UL;
    addressDesc.loc = { 0, 0 };
    ret = BioAllocCacheSpace(tenantId, objectId, NO_1024, &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_OK);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, 1, userParam);
    ret = BioPutWithCopyFree(tenantId, "putwithcopyfree2", &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    ret = BioPutWithCopyFree(tenantId, "putwithcopyfree3", &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL", 0, 1, userParam);
    ret = BioPutWithCopyFree(tenantId, "putwithcopyfree4", &addressDesc);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL");

    ret = BioDestroyCache(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_delete)
{
    LOG_INFO("test_bio_delete");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, 1, userParam);
    auto ret = BioDelete(G_TENANT_ID, G_KEY, g_Location);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    ret = BioDelete(G_TENANT_ID, nullptr, g_Location);
    EXPECT_EQ(ret, RET_CACHE_EPERM);

    ret = BioDelete(G_INVALID_TENANT_ID, G_KEY, g_Location);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_bio_update_return_ok)
{
    LOG_INFO("test_bio_update_return_ok");
    constexpr uint64_t tenantId = 1234UL;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioNotifyUpgradePrepare(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_UPGRADE_FLUSH", 0, 1, userParam);
    ret = BioCheckUpgradeReady(tenantId);
    EXPECT_EQ(ret, RET_CACHE_ERROR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_UPGRADE_FLUSH");

    ret = BioNotifyUpgradeFinish(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioDestroyCache(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

void TestBio::VNodeIdStub()
{
    MOCKER_CPP(&ock::bio::CmNodeId::VNodeId, uint16_t(*)()).stubs().will(returnValue(10));
}

void TestBio::GetPtVersionStub()
{
    MOCKER_CPP(&ock::bio::FlowInstance::Version, uint64_t(*)()).stubs().will(returnValue(10));
}

TEST_F(TestBio, test_list_remote_case_return_ok)
{
    LOG_INFO("test_list_remote_case_return_ok");
    TestBio::VNodeIdStub();
    auto prefix = "456";
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME", 0, 1, userParam);
    auto ret = BioListAll(G_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME");
    BioFreeListResources(objs, objNum);
}

TEST_F(TestBio, test_list_remote_remote_over_limit)
{
    LOG_INFO("test_list_remote_remote_over_limit");
    TestBio::VNodeIdStub();
    auto prefix = "456";
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "LISTALL_REMOTE_OVER_1000", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "LISTALL_REMOTE_RSP_OVER_LIMIT", 0, 1, userParam);
    auto ret = BioListAll(G_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "LISTALL_REMOTE_OVER_1000");
    LVOS_HVS_deactiveTracePoint(0, "LISTALL_REMOTE_RSP_OVER_LIMIT");
    BioFreeListResources(objs, objNum);
}

TEST_F(TestBio, test_bio_put_remote_case_return_fail)
{
    LOG_INFO("test_bio_put_remote_case_return_fail");
    TestBio::VNodeIdStub();
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *)value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME", 0, 1, userParam);
    auto ret = BioPut(G_TENANT_ID, "putremote", value.c_str(), G_LENGTH, g_Location);
    fclose(fp);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME");
}

TEST_F(TestBio, test_bio_put_remote_ptv_error_case_return_fail)
{
    LOG_INFO("test_bio_put_remote_ptv_error_case_return_fail");
    TestBio::VNodeIdStub();
    TestBio::GetPtVersionStub();
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *)value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME", 0, 1, userParam);
    auto ret = BioPut(G_TENANT_ID, "putremoteptverror", value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME");
    fclose(fp);
}

TEST_F(TestBio, test_bio_get_remote_case_return_fail)
{
    LOG_INFO("test_bio_get_remote_case_return_fail");
    TestBio::VNodeIdStub();
    char *value = new char[G_LENGTH];
    uint64_t realLen = G_LENGTH;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME", 0, 1, userParam);
    auto ret = BioGet(G_TENANT_ID, "getremote", 0, G_LENGTH, g_Location, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CLIENT_SET_RETRY_TIME");
    delete[] value;
}

TEST_F(TestBio, test_pt_entry_list_update_node_state_down)
{
    LOG_INFO("test_pt_entry_list_update_node_state_down");
    auto ptList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptList->poolId = 0;
    ptList->ptNum = NO_2;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < NO_2; diskIdx++) {
        ptList->ptEntryList[diskIdx].birthVersion = 1;
        ptList->ptEntryList[diskIdx].ptId = diskIdx;
        ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptList->ptEntryList[diskIdx].masterNodeId = 0;
        ptList->ptEntryList[diskIdx].masterDiskId = 0;
        ptList->ptEntryList[diskIdx].referNum = 0;
        ptList->ptEntryList[diskIdx].copyNum = 1;
        ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
    }
    auto pgChange = std::make_unique<int>(sizeof(int32_t));
    ViewPtEntryListUpdateNodeState(0, NODE_STATE_DOWN, nullptr, ptList, pgChange.get());
    free(ptList);
}

TEST_F(TestBio, test_pt_entry_list_update_node_state_up_down)
{
    LOG_INFO("test_pt_entry_list_update_node_state_up_down");
    auto ptList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptList->poolId = 0;
    ptList->ptNum = NO_2;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < NO_2; diskIdx++) {
        ptList->ptEntryList[diskIdx].birthVersion = 1;
        ptList->ptEntryList[diskIdx].ptId = diskIdx;
        ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptList->ptEntryList[diskIdx].masterNodeId = 0;
        ptList->ptEntryList[diskIdx].masterDiskId = 0;
        ptList->ptEntryList[diskIdx].referNum = 0;
        ptList->ptEntryList[diskIdx].copyNum = 1;
        ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_DOWN;
    }
    auto pgChange = std::make_unique<int>(sizeof(int32_t));
    NodeInfo nodeInfo;
    CM_GetNodeInfo(0, &nodeInfo);
    nodeInfo.diskList.list[0].state = DISK_STATE_NORMAL;
    nodeInfo.diskList.list[1].state = DISK_STATE_NORMAL;
    ViewPtEntryListUpdateNodeState(0, NODE_STATE_UP, &nodeInfo, ptList, pgChange.get());
    free(ptList);
}

TEST_F(TestBio, test_pt_entry_list_update_node_state_up_running)
{
    LOG_INFO("test_pt_entry_list_update_node_state_up_running");
    auto ptList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptList->poolId = 0;
    ptList->ptNum = NO_2;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < NO_2; diskIdx++) {
        ptList->ptEntryList[diskIdx].birthVersion = 1;
        ptList->ptEntryList[diskIdx].ptId = diskIdx;
        ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptList->ptEntryList[diskIdx].masterNodeId = 0;
        ptList->ptEntryList[diskIdx].masterDiskId = 0;
        ptList->ptEntryList[diskIdx].referNum = 0;
        ptList->ptEntryList[diskIdx].copyNum = 1;
        ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
    }
    auto pgChange = std::make_unique<int>(sizeof(int32_t));
    NodeInfo nodeInfo;
    CM_GetNodeInfo(0, &nodeInfo);
    nodeInfo.diskList.list[0].state = DISK_STATE_FAULT;
    nodeInfo.diskList.list[1].state = DISK_STATE_FAULT;
    ViewPtEntryListUpdateNodeState(0, NODE_STATE_UP, &nodeInfo, ptList, pgChange.get());
    free(ptList);
}

TEST_F(TestBio, test_pt_entry_list_update_node_state_up_recovery)
{
    LOG_INFO("test_pt_entry_list_update_node_state_up_recovery");
    auto ptList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptList->poolId = 0;
    ptList->ptNum = NO_2;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < NO_2; diskIdx++) {
        ptList->ptEntryList[diskIdx].birthVersion = 1;
        ptList->ptEntryList[diskIdx].ptId = diskIdx;
        ptList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptList->ptEntryList[diskIdx].masterNodeId = 0;
        ptList->ptEntryList[diskIdx].masterDiskId = 0;
        ptList->ptEntryList[diskIdx].referNum = 0;
        ptList->ptEntryList[diskIdx].copyNum = 1;
        ptList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RECOVERY;
    }
    auto pgChange = std::make_unique<int>(sizeof(int32_t));
    NodeInfo nodeInfo;
    CM_GetNodeInfo(0, &nodeInfo);
    nodeInfo.diskList.list[0].state = DISK_STATE_FAULT;
    nodeInfo.diskList.list[1].state = DISK_STATE_FAULT;
    ViewPtEntryListUpdateNodeState(0, NODE_STATE_UP, &nodeInfo, ptList, pgChange.get());
    free(ptList);
}

TEST_F(TestBio, test_pt_entry_list_update_node_finish)
{
    LOG_INFO("test_pt_entry_list_update_node_finish");
    auto ptEntryList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptEntryList->poolId = 0;
    ptEntryList->ptNum = NO_2;
    ptEntryList->maxCopyNum = 1;
    ptEntryList->minCopyNum = -1;
    ptEntryList->globalVersion = 1;
    ptEntryList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < NO_2; diskIdx++) {
        ptEntryList->ptEntryList[diskIdx].birthVersion = 1;
        ptEntryList->ptEntryList[diskIdx].ptId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptEntryList->ptEntryList[diskIdx].masterNodeId = 0;
        ptEntryList->ptEntryList[diskIdx].masterDiskId = 0;
        ptEntryList->ptEntryList[diskIdx].referNum = 0;
        ptEntryList->ptEntryList[diskIdx].copyNum = 1;
        ptEntryList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
        ptEntryList->ptEntryList[diskIdx].copyList[1].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[1].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].state = PT_COPY_STATE_RUNNING;
    }
    auto ptList = (::CmPtFinish *)malloc(sizeof(::CmPtFinish) * NO_2);
    ptList[0].birthVersion = 1;
    ptList[0].ptId = 1;
    ptList[1].birthVersion = 1;
    ptList[1].ptId = 1;
    auto pgChange = std::make_unique<int>(sizeof(int32_t));
    ViewPtEntryListUpdateNodeFinish(0, ptList, NO_2, ptEntryList, pgChange.get(), 1, 1);
    free(ptList);
    free(ptEntryList);
}

TEST_F(TestBio, test_create_view_caculator)
{
    LOG_INFO("test_create_view_caculator");
    Calculator calc = nullptr;
    calc = CreateViewCalculator(1, 1, 1, 1);
    EXPECT_EQ(0, BIO_OK);
    DestoryViewCalculator(calc);
}

TEST_F(TestBio, test_view_caculator_initial)
{
    LOG_INFO("test_view_caculator_initial");
    Calculator calculator = CreateViewCalculator(1, 1, 1, 1);
    auto notifyList = static_cast<NodeInfoList *>(malloc(sizeof(NodeInfoList) + sizeof(NodeInfo)));
    auto nodeInfo = (NodeInfo *)malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;
    notifyList->poolId = 0;
    notifyList->nodeNum = 0;
    notifyList->nodeList[0] = *nodeInfo;
    notifyList->nodeNum++;

    auto ptEntryList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptEntryList->poolId = 0;
    ptEntryList->ptNum = NO_2;
    ptEntryList->maxCopyNum = 1;
    ptEntryList->minCopyNum = 1;
    ptEntryList->globalVersion = 1;
    ptEntryList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < NO_2; diskIdx++) {
        ptEntryList->ptEntryList[diskIdx].birthVersion = 1;
        ptEntryList->ptEntryList[diskIdx].ptId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptEntryList->ptEntryList[diskIdx].masterNodeId = 0;
        ptEntryList->ptEntryList[diskIdx].masterDiskId = 0;
        ptEntryList->ptEntryList[diskIdx].referNum = 0;
        ptEntryList->ptEntryList[diskIdx].copyNum = 1;
        ptEntryList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
    }

    auto len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo));
    NodeDiskState nodeDiskState{ 0, DISK_CLUSTER_STATE_IN };
    auto nodeStateInfo = static_cast<NodeStateInfo *>(malloc(sizeof(NodeStateInfo) + sizeof(NodeDiskState)));
    nodeStateInfo->diskNum = 1;
    nodeStateInfo->nodeId = 0;
    nodeStateInfo->state = NODE_STATE_UP;
    nodeStateInfo->clusterState = DISK_CLUSTER_STATE_IN;
    nodeStateInfo->diskList[0] = nodeDiskState;
    auto *stateList = static_cast<NodeStateList *>(malloc(len));
    stateList->poolId = 0;
    stateList->nodeNum = 1;
    stateList->nodeList[0] = *nodeStateInfo;
    ViewCalculatorInitial(calculator, notifyList, stateList, ptEntryList);
    DestoryViewCalculator(calculator);
    free(stateList);
    free(nodeStateInfo);
    free(ptEntryList);
    free(nodeInfo);
    free(notifyList);
}

TEST_F(TestBio, test_caculator_need_rebalance)
{
    LOG_INFO("test_caculator_need_rebalance");
    Calculator calculator = CreateViewCalculator(1, 1, 1, 1);
    auto notifyList = static_cast<NodeInfoList *>(malloc(sizeof(NodeInfoList) + sizeof(NodeInfo)));
    auto nodeInfo = (NodeInfo *)malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;
    notifyList->poolId = 0;
    notifyList->nodeNum = 0;
    notifyList->nodeList[0] = *nodeInfo;
    notifyList->nodeNum++;

    auto ptEntryList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptEntryList->poolId = 0;
    ptEntryList->ptNum = NO_2;
    ptEntryList->maxCopyNum = 1;
    ptEntryList->minCopyNum = 1;
    ptEntryList->globalVersion = 1;
    ptEntryList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < ock::bio::NO_2; diskIdx++) {
        ptEntryList->ptEntryList[diskIdx].birthVersion = 1;
        ptEntryList->ptEntryList[diskIdx].ptId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptEntryList->ptEntryList[diskIdx].masterNodeId = 0;
        ptEntryList->ptEntryList[diskIdx].masterDiskId = 0;
        ptEntryList->ptEntryList[diskIdx].referNum = 0;
        ptEntryList->ptEntryList[diskIdx].copyNum = 1;
        ptEntryList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
        ptEntryList->ptEntryList[diskIdx].copyList[1].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[1].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].state = PT_COPY_STATE_RUNNING;
    }

    auto len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo));

    NodeDiskState nodeDiskState{ 0, DISK_CLUSTER_STATE_IN };
    auto nodeStateInfo = static_cast<NodeStateInfo *>(malloc(sizeof(NodeStateInfo) + sizeof(NodeDiskState)));
    nodeStateInfo->diskNum = 1;
    nodeStateInfo->nodeId = 0;
    nodeStateInfo->state = NODE_STATE_UP;
    nodeStateInfo->clusterState = DISK_CLUSTER_STATE_IN;
    nodeStateInfo->diskList[0] = nodeDiskState;
    auto *stateList = static_cast<NodeStateList *>(malloc(len));
    stateList->poolId = 0;
    stateList->nodeNum = 1;
    stateList->nodeList[0] = *nodeStateInfo;
    ViewCalculatorNeedRebalance(calculator, notifyList, stateList, ptEntryList);
    DestoryViewCalculator(calculator);
    free(stateList);
    free(nodeStateInfo);
    free(ptEntryList);
    free(nodeInfo);
    free(notifyList);
}

TEST_F(TestBio, test_caculator_rebalance)
{
    LOG_INFO("test_caculator_rebalance");
    Calculator calculator = CreateViewCalculator(1, 1, 1, 1);
    auto notifyList = static_cast<NodeInfoList *>(malloc(sizeof(NodeInfoList) + sizeof(NodeInfo)));
    auto nodeInfo = (NodeInfo *)malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;
    notifyList->poolId = 0;
    notifyList->nodeNum = 0;
    notifyList->nodeList[0] = *nodeInfo;
    notifyList->nodeNum++;

    auto ptEntryList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptEntryList->poolId = 0;
    ptEntryList->ptNum = NO_2;
    ptEntryList->maxCopyNum = 1;
    ptEntryList->minCopyNum = 1;
    ptEntryList->globalVersion = 1;
    ptEntryList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < NO_2; diskIdx++) {
        ptEntryList->ptEntryList[diskIdx].birthVersion = 1;
        ptEntryList->ptEntryList[diskIdx].ptId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptEntryList->ptEntryList[diskIdx].masterNodeId = 0;
        ptEntryList->ptEntryList[diskIdx].masterDiskId = 0;
        ptEntryList->ptEntryList[diskIdx].referNum = 0;
        ptEntryList->ptEntryList[diskIdx].copyNum = 1;
        ptEntryList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_RUNNING;
        ptEntryList->ptEntryList[diskIdx].copyList[1].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[1].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].state = PT_COPY_STATE_RUNNING;
    }

    auto len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo));
    NodeDiskState nodeDiskState{ 0, DISK_CLUSTER_STATE_IN };
    auto nodeStateInfo = static_cast<NodeStateInfo *>(malloc(sizeof(NodeStateInfo) + sizeof(NodeDiskState)));
    nodeStateInfo->diskNum = 1;
    nodeStateInfo->nodeId = 0;
    nodeStateInfo->state = NODE_STATE_UP;
    nodeStateInfo->clusterState = DISK_CLUSTER_STATE_IN;
    nodeStateInfo->diskList[0] = nodeDiskState;
    auto *stateList = static_cast<NodeStateList *>(malloc(len));
    stateList->poolId = 0;
    stateList->nodeNum = 1;
    stateList->nodeList[0] = *nodeStateInfo;
    ViewCalculatorRebalance(calculator, notifyList, stateList, ptEntryList);
    DestoryViewCalculator(calculator);
    free(stateList);
    free(nodeStateInfo);
    free(ptEntryList);
    free(nodeInfo);
    free(notifyList);
}

TEST_F(TestBio, test_caculator_rebalance_state_init)
{
    LOG_INFO("test_caculator_rebalance_state_init");
    Calculator calculator = CreateViewCalculator(1, 1, 1, 1);
    auto notifyList = static_cast<NodeInfoList *>(malloc(sizeof(NodeInfoList) + sizeof(NodeInfo)));
    auto nodeInfo = (NodeInfo *)malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;
    notifyList->poolId = 0;
    notifyList->nodeNum = 0;
    notifyList->nodeList[0] = *nodeInfo;
    notifyList->nodeNum++;

    auto ptEntryList = (PtEntryList *)malloc(sizeof(PtEntryList) + sizeof(PtEntry) * NO_2);
    ptEntryList->poolId = 0;
    ptEntryList->ptNum = NO_2;
    ptEntryList->maxCopyNum = 1;
    ptEntryList->minCopyNum = 1;
    ptEntryList->globalVersion = 1;
    ptEntryList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < NO_2; diskIdx++) {
        ptEntryList->ptEntryList[diskIdx].birthVersion = 1;
        ptEntryList->ptEntryList[diskIdx].ptId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].state = PT_STATE_NORMAL;
        ptEntryList->ptEntryList[diskIdx].masterNodeId = 0;
        ptEntryList->ptEntryList[diskIdx].masterDiskId = 0;
        ptEntryList->ptEntryList[diskIdx].referNum = 0;
        ptEntryList->ptEntryList[diskIdx].copyNum = 1;
        ptEntryList->ptEntryList[diskIdx].copyList[0].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[0].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_INIT;
        ptEntryList->ptEntryList[diskIdx].copyList[1].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[1].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].state = PT_COPY_STATE_INIT;
    }

    auto len = (int32_t)(sizeof(NodeStateList) + sizeof(NodeStateInfo));
    NodeDiskState nodeDiskState{ 0, DISK_CLUSTER_STATE_IN };
    auto nodeStateInfo = static_cast<NodeStateInfo *>(malloc(sizeof(NodeStateInfo) + sizeof(NodeDiskState)));
    nodeStateInfo->diskNum = 1;
    nodeStateInfo->nodeId = 0;
    nodeStateInfo->state = NODE_STATE_UP;
    nodeStateInfo->clusterState = DISK_CLUSTER_STATE_IN;
    nodeStateInfo->diskList[0] = nodeDiskState;
    auto *stateList = static_cast<NodeStateList *>(malloc(len));
    stateList->poolId = 0;
    stateList->nodeNum = 1;
    stateList->nodeList[0] = *nodeStateInfo;
    ViewCalculatorRebalance(calculator, notifyList, stateList, ptEntryList);
    DestoryViewCalculator(calculator);
    free(stateList);
    free(nodeStateInfo);
    free(ptEntryList);
    free(nodeInfo);
    free(notifyList);
}

static int ReadHookFunc(uint64_t inode, char *buff, uint64_t count, uint64_t offset, int *readLen)
{
    return BIO_OK;
}

static int WriteHookFunc(uint64_t inode, char *buff, uint64_t count, uint64_t offset, uint64_t fh)
{
    return BIO_OK;
}

static int WriteCopyFreeHookFunc(uint64_t inode, uint64_t offset, uint64_t count, CacheSpaceDesc *spaceInfo)
{
    return BIO_OK;
}

TEST_F(TestBio, test_juicefs_callback_read_case)
{
    LOG_INFO("test_juicefs_callback_read_case");
    BioRegisterInterceptorRead(nullptr);
    auto ret = BioReadHook(0, nullptr, 0, 0, nullptr);
    EXPECT_EQ(ret, RET_CACHE_ERROR);

    BioRegisterInterceptorRead(ReadHookFunc);
    ret = BioReadHook(0, nullptr, 0, 0, nullptr);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBio, test_juicefs_callback_write_case)
{
    LOG_INFO("test_juicefs_callback_write_case");
    BioRegisterInterceptorWrite(nullptr);
    auto ret = BioWriteHook(0, nullptr, 0, 0, 0);
    EXPECT_EQ(ret, RET_CACHE_ERROR);

    BioRegisterInterceptorWrite(WriteHookFunc);
    ret = BioWriteHook(0, nullptr, 0, 0, 0);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBio, test_juicefs_callback_write_copy_case)
{
    LOG_INFO("test_juicefs_callback_write_copy_case");
    BioRegisterInterceptorWriteCopyFree(nullptr);
    auto ret = BioWriteCopyFreeHook(0, 0, 0, nullptr);
    EXPECT_EQ(ret, RET_CACHE_ERROR);

    BioRegisterInterceptorWriteCopyFree(WriteCopyFreeHookFunc);
    ret = BioWriteCopyFreeHook(0, 0, 0, nullptr);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBio, test_bio_calculateLocation_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_calculateLocation_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    uint32_t sliceId = 1;
    auto ret = BioCalcLocation(G_TENANT_ID, sliceId, &g_Location);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);
}

TEST_F(TestBio, test_bio_put_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_put_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *)value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(G_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    fclose(fp);
    ock::bio::BioClient::Instance()->SetStartWorker(true);
}

TEST_F(TestBio, test_bio_get_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_get_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    uint64_t realLen = 6000UL;
    char *value = new char[realLen];
    ObjLocation locationInfo{ 0, 0 };
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen, locationInfo, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    delete[] value;
    ock::bio::BioClient::Instance()->SetStartWorker(true);
}

TEST_F(TestBio, test_bio_delete_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_delete_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    ObjLocation locationInfo{ 0, 0 };
    auto ret = BioDelete(G_TENANT_ID, G_KEY, locationInfo);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);
}

TEST_F(TestBio, test_bio_load_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_load_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    ObjLocation locationInfo{ 0, 0 };
    LoadContext loadCtx;
    sem_init(&(loadCtx.sem), 0, 1);
    loadCtx.result = RET_CACHE_OK;
    auto ret = BioLoad(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    sem_wait(&(loadCtx.sem));
    sem_destroy(&(loadCtx.sem));
    ock::bio::BioClient::Instance()->SetStartWorker(true);
}

TEST_F(TestBio, test_bio_list_all_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_list_all_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    auto prefix = "456";
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(G_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    BioFreeListResources(objs, objNum);
    ock::bio::BioClient::Instance()->SetStartWorker(true);
}

TEST_F(TestBio, test_bio_stat_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_stat_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    ObjLocation locationInfo{ 0, 0 };
    ObjStat keyStat;
    auto ret = BioStat(G_TENANT_ID, G_KEY, locationInfo, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);
}

TEST_F(TestBio, test_bio_allocspace_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_allocspace_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    static uint64_t objectId = 1;
    CacheSpaceDesc spaceDesc;
    spaceDesc.allocLoc = 1;
    auto ret = BioAllocCacheSpace(G_TENANT_ID, objectId++, ock::bio::NO_1024, &spaceDesc);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_SET_PT_ID_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_SELECT_PT_FAIL", 0, 1, userParam);
    ret = BioAllocCacheSpace(G_TENANT_ID, objectId, ock::bio::NO_1024, &spaceDesc);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_SELECT_PT_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_SET_PT_ID_FAIL");
}

TEST_F(TestBio, test_bio_putwithspace_not_ready_case_return_fail)
{
    LOG_INFO("test_bio_putwithspace_not_ready_case_return_fail");
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    CacheSpaceDesc spaceDesc;
    auto ret = BioPutWithCopyFree(G_TENANT_ID, "putwithspace", &spaceDesc);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);
}

TEST_F(TestBio, test_bio_update_return_fail)
{
    LOG_INFO("test_bio_update_return_fail");
    constexpr uint64_t tenantId = 123UL;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({ tenantId, affinity, strategy });
    EXPECT_EQ(ret, RET_CACHE_OK);

    ret = BioNotifyUpgradePrepare(G_INVALID_TENANT_ID);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    ock::bio::BioClient::Instance()->SetStartWorker(false);
    ret = BioNotifyUpgradePrepare(tenantId);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL", 0, 1, userParam);
    ret = BioNotifyUpgradePrepare(tenantId);
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL");

    ret = BioCheckUpgradeReady(G_INVALID_TENANT_ID);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    ock::bio::BioClient::Instance()->SetStartWorker(false);
    ret = BioCheckUpgradeReady(tenantId);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CHECK_UPDATE_RECV_FAIL", 0, 1, userParam);
    ret = BioCheckUpgradeReady(tenantId);
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CHECK_UPDATE_RECV_FAIL");

    ret = BioNotifyUpgradeFinish(G_INVALID_TENANT_ID);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);

    ock::bio::BioClient::Instance()->SetStartWorker(false);
    ret = BioNotifyUpgradeFinish(tenantId);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL", 0, 1, userParam);
    ret = BioNotifyUpgradeFinish(tenantId);
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL");

    ret = BioDestroyCache(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_convert_location)
{
    LOG_INFO("test_bio_convert_location");
    ObjLocation location{0, 0};
    ObjLocationDetail detailLoc;
    ock::bio::BioClient::Instance()->SetStartWorker(false);
    auto ret = BioConvertLocation(location, &detailLoc);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    ock::bio::BioClient::Instance()->SetStartWorker(true);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, 1, userParam);
    ret = BioConvertLocation(location, &detailLoc);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    ret = BioConvertLocation(location, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
}

TEST_F(TestBio, test_bio_qos_wake_force)
{
    LOG_INFO("test_bio_qos_wake_force");
    auto map = BioQos::Instance()->GetQuotaPtr()->GetIoQueueMap();
    uint16_t nodeSet = NO_50;
    auto iter = map->find(nodeSet);
    if (UNLIKELY(iter == map->end())) {
        map->emplace(nodeSet, IoHangQueue());
        iter = map->find(nodeSet);
    }

    auto taskFlag = BioQos::Instance()->GetQuotaPtr()->GetTaskRunFlag();
    auto iter2 = taskFlag->find(nodeSet);
    if (UNLIKELY(iter2 == taskFlag->end())) {
        taskFlag->emplace(nodeSet, true);
    }

    IoWaitEntry entry1("test_bio_qos_wake_force_1", NO_4194304);
    IoWaitEntry entry2("test_bio_qos_wake_force_2", NO_4194304);
    iter->second.Push(&entry1);
    iter->second.Push(&entry2);
    BioQos::Instance()->GetQuotaPtr()->WakeForce(nodeSet, false);
    EXPECT_EQ(entry1.Result(), BIO_INNER_RETRY);
    EXPECT_EQ(entry2.Result(), BIO_INNER_RETRY);
}

TEST_F(TestBio, test_bio_qos_rollback)
{
    LOG_INFO("test_bio_qos_rollback");
    CmPtInfo ptEntry;
    uint16_t ptId = 1;
    auto ret = BioClient::Instance()->GetMirror()->GetPtEntry(ptId, ptEntry);
    EXPECT_EQ(ret, BIO_OK);

    std::vector<uint16_t> successNodeVec = { 0 };
    std::vector<uint64_t> successQuotaVec = { NO_4194304 };
    BioQos::Instance()->GetQuotaPtr()->RollbackAllocQuotaReq(&ptEntry, successNodeVec, successQuotaVec);
}

TEST_F(TestBio, test_bio_qos_put_align_size)
{
    LOG_INFO("test_bio_qos_put_align_size");
    CmPtInfo ptEntry;
    uint16_t ptId = 1;
    auto ret = BioClient::Instance()->GetMirror()->GetPtEntry(ptId, ptEntry);
    EXPECT_EQ(ret, BIO_OK);

    BioClient::Instance()->GetMirror()->SetScene(SCENE_BIGDATA);

    uint64_t length = NO_1024 - NO_1;
    char *dataBuff = new char[length];
    ObjLocation location = { 1, 0 };
    MirrorClient::MirrorPut param = { { NO_1, LOCAL_AFFINITY, WRITE_BACK }, "test_bio_qos_put_align_size", dataBuff,
                                      length, location, 0 };
    bool isAllocMem = false;
    char *value = param.value;
    ret = BioClient::Instance()->GetMirror()->PutAlignSize(value, param, isAllocMem);
    EXPECT_EQ(ret, BIO_OK);

    if (isAllocMem) {
        free(param.value);
        param.value = value;
    }
    delete[] dataBuff;
    BioClient::Instance()->GetMirror()->SetScene(SCENE_NONE);
}

TEST_F(TestBio, test_check_get_underfs_config_resp)
{
    LOG_INFO("test_check_get_underfs_config_resp");
    GetUnderFsConfigResponse rsp;
    char *type = "hdfs";
    char *nameNode = "192.168.100.171:9000";
    char *path = "/hdfs";
    char *user = "ceph";
    char *cluster = "ceph";
    char *cfgPath = "/etc/conf";
    char *pool = "pool0";
    std::strcpy(rsp.underFsType, type);

    CephConfigResponse cephConfig;
    std::strcpy(cephConfig.cfgPath, cfgPath);
    std::strcpy(cephConfig.cluster, cluster);
    std::strcpy(cephConfig.user, user);
    std::strcpy(cephConfig.pool, pool);

    HdfsConfigResponse hdfsConfig;
    std::strcpy(hdfsConfig.workingPath, path);
    std::strcpy(hdfsConfig.nameNode, nameNode);

    rsp.cephConfig = cephConfig;
    rsp.hdfsConfig = hdfsConfig;

    auto ret = ock::bio::net::BioClientNet::Instance()->CheckGetUnderFsConfigResp(rsp);
    EXPECT_EQ(ret, true);
}

TEST_F(TestBio, test_get_underfs_config)
{
    LOG_INFO("test_get_underfs_config");
    BioConfig::UnderFsConfig config;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_CLIENT_GET_UNDERFS_CONFIG_PASS_SYNC_CALL", 0, 1, userParam);
    auto ret = ock::bio::net::BioClientNet::Instance()->GetUnderFsConfig(config);
    LVOS_HVS_deactiveTracePoint(0, "SDK_CLIENT_GET_UNDERFS_CONFIG_PASS_SYNC_CALL");
    EXPECT_EQ(ret, BIO_INNER_ERR);
}

TEST_F(TestBio, test_bio_client_net_shm_init)
{
    LOG_INFO("test_bio_client_net_shm_init");
    ShmInitResponse rsp{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    auto ret = ock::bio::net::BioClientNet::Instance()->CheckShmInitResp(rsp);
    EXPECT_EQ(ret, false);
}

TEST_F(TestBio, test_bio_client_agent_get_local_quota_info)
{
    LOG_INFO("test_bio_client_agent_get_local_quota_info");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_GET_LOCAL_QUOTA", 0, 1, userParam);
    bool enable = true;
    uint64_t load = 1;
    auto ret = ock::bio::agent::BioClientAgent::Instance()->GetLocalQuotaInfo(1, enable, load);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_GET_LOCAL_QUOTA");
}

TEST_F(TestBio, test_bio_client_agent_check_get_slice)
{
    LOG_INFO("test_bio_client_agent_check_get_slice");
    GetSliceResponse *rsp = (GetSliceResponse *) new char[sizeof(GetSliceResponse) + 128];
    rsp->addrNum = NO_20;
    auto ret = ock::bio::agent::BioClientAgent::Instance()->CheckGetSliceRsp(&rsp);
    EXPECT_EQ(ret, false);
    rsp->addrNum = NO_1;
    rsp->sliceLen = NO_1;
    ret = ock::bio::agent::BioClientAgent::Instance()->CheckGetSliceRsp(&rsp);
    SliceAddrDesc addr;
    addr.chunkLen = IO_SIZE_64M;
    rsp->addr[0] =addr;
    ret = ock::bio::agent::BioClientAgent::Instance()->CheckGetSliceRsp(&rsp);
    EXPECT_EQ(ret, false);
    addr.chunkLen = IO_SIZE_4M;
    rsp->addr[0] =addr;
    ret = ock::bio::agent::BioClientAgent::Instance()->CheckGetSliceRsp(&rsp);
    EXPECT_EQ(ret, true);
    delete[] rsp;
}

TEST_F(TestBio, test_bio_client_agent_check_update_local)
{
    LOG_INFO("test_bio_client_agent_check_update_local");
    CheckUpdateReadyRequest req;
    CheckUpdateReadyResponse rsp;
    auto ret = ock::bio::agent::BioClientAgent::Instance()->SendCheckUpdateReadyRequestLocal(req, rsp);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestBio, test_bio_client_get_shm_address)
{
    LOG_INFO("test_bio_client_get_shm_address");
    auto ret = ock::bio::net::BioClientNet::Instance()->GetShmAddress(0, NO_60) == nullptr ? BIO_OK : BIO_ERR;
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBio, test_bio_client_agent_prepare)
{
    LOG_INFO("test_bio_client_agent_prepare");
    CmPtInfo ptEntry;
    GetSliceResponse *rsp = nullptr;
    auto ret = ock::bio::agent::BioClientAgent::Instance()->SendPrepareResourceLocal(ptEntry, 1, 1, 1, 1, &rsp);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}