/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include <semaphore.h>
#include "gtest/gtest.h"
#include "bio_c.h"
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
    // valid case
    uint64_t tenantId = 12341;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({tenantId, affinity, strategy});
    EXPECT_EQ(ret, RET_CACHE_OK);
    // repeat creat
    ret = BioCreateCache({tenantId, affinity, strategy});
    EXPECT_EQ(ret, RET_CACHE_EXISTS);
    // invalid case
    ret = BioCreateCache({G_INVALID_TENANT_ID, affinity, strategy});
    EXPECT_EQ(ret, RET_CACHE_EPERM);
}

TEST_F(TestBio, test_bio_get_cache)
{
    // valid case
    uint64_t tenantId = 12343;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({tenantId, affinity, strategy});
    EXPECT_EQ(ret, RET_CACHE_OK);
    // get cache instance
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
    // get instance with invalid parameters
    ret = BioGetCache(tenantId, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // get a non-existent instance
    ret = BioGetCache(G_INVALID_TENANT_ID, &desc);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_bio_destroy_cache)
{
    // valid case
    uint64_t tenantId = 12344;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({tenantId, affinity, strategy});
    EXPECT_EQ(ret, RET_CACHE_OK);
    // get cache instance
    CacheDescriptor desc;
    ret = BioGetCache(tenantId, &desc);
    EXPECT_EQ(ret, RET_CACHE_OK);
    EXPECT_EQ(tenantId, desc.tenantId);
    EXPECT_EQ(affinity, desc.affinity);
    EXPECT_EQ(strategy, desc.strategy);
    // destroy cache instance
    ret = BioDestroyCache(tenantId);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // get after destroy
    desc = {};
    ret = BioGetCache(tenantId, &desc);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    // destroy a non-existent instance
    ret = BioDestroyCache(G_INVALID_TENANT_ID);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_list_cache)
{
    uint64_t tenantId = 12345;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    auto ret = BioCreateCache({tenantId, affinity, strategy});
    EXPECT_EQ(ret, RET_CACHE_OK);
    auto cacheList = ock::bio::BioService::ListCache();
    EXPECT_FALSE(cacheList.empty());
    // get list after delete all instance
    for (auto &item: cacheList) {
        EXPECT_EQ(BioDestroyCache(item.tenantId), RET_CACHE_OK);
    }
    cacheList.clear();
    cacheList = ock::bio::BioService::ListCache();
    EXPECT_TRUE(cacheList.empty());
}

TEST_F(TestBio, test_bio_calc_location)
{
    CacheDescriptor desc = {G_TENANT_ID, AffinityStrategy::LOCAL_AFFINITY, WriteStrategy::WRITE_BACK};
    auto ret = BioCreateCache(desc);
    EXPECT_EQ(ret, RET_CACHE_OK);
    uint32_t sliceId = 1;
    ret = BioCalcLocation(G_TENANT_ID, sliceId, &g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // null location
    ret = BioCalcLocation(G_TENANT_ID, sliceId, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // invalid tenantId
    ObjLocation location;
    ret = BioCalcLocation(G_INVALID_TENANT_ID, sliceId, &location);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_bio_put)
{
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *) value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(G_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // invalid tenantId
    ret = BioPut(G_INVALID_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    // invalid key
    ret = BioPut(G_TENANT_ID, nullptr, value.c_str(), G_LENGTH, g_Location);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    fclose(fp);
}

TEST_F(TestBio, test_bio_get)
{
    (void) system("touch bio_get_file");
    FILE *fp = fopen("./bio_get_file", "w");
    EXPECT_NE(fp, nullptr);
    char *value = new char[G_LENGTH];
    ObjLocation locationInfo{0, 0};
    uint64_t realLen = G_LENGTH;
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, value, &realLen);
    EXPECT_EQ(fwrite(value, sizeof(char), realLen, fp), realLen);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // null realLen
    ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, value, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // invalid tenantId
    ret = BioGet(G_INVALID_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    // error branches
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, G_PT_TIMES, userParam);
    ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");
    delete[] value;
    fclose(fp);
}

TEST_F(TestBio, test_bio_put_diff_size_case_return_ok)
{
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    int length = 6000;
    std::string value(length, ' ');
    EXPECT_EQ(fread((void *) value.c_str(), sizeof(char), length, fp), length);
    auto ret = BioPut(G_TENANT_ID, "4_8k", value.c_str(), length, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    int length1 = 10000;
    std::string value1(length1, ' ');
    EXPECT_EQ(fread((void *) value1.c_str(), sizeof(char), length1, fp), length1);
    ret = BioPut(G_TENANT_ID, "8_64k", value1.c_str(), length1, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    int length2 = 100000;
    std::string value2(length2, ' ');
    EXPECT_EQ(fread((void *) value2.c_str(), sizeof(char), length2, fp), length2);
    ret = BioPut(G_TENANT_ID, "64_128k", value2.c_str(), length2, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    int length3 = 160000;
    std::string value3(length3, ' ');
    EXPECT_EQ(fread((void *) value3.c_str(), sizeof(char), length3, fp), length3);
    ret = BioPut(G_TENANT_ID, "128_256k", value3.c_str(), length3, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    int length4 = 300000;
    std::string value4(length4, ' ');
    EXPECT_EQ(fread((void *) value4.c_str(), sizeof(char), length4, fp), length4);
    ret = BioPut(G_TENANT_ID, "256K_1M", value4.c_str(), length4, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    int length5 = 1500000;
    std::string value5(length5, ' ');
    EXPECT_EQ(fread((void *) value5.c_str(), sizeof(char), length5, fp), length5);
    ret = BioPut(G_TENANT_ID, "1M_2M", value5.c_str(), length5, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    int length6 = 3300000;
    std::string value6(length6, ' ');
    EXPECT_EQ(fread((void *) value6.c_str(), sizeof(char), length6, fp), length6);
    ret = BioPut(G_TENANT_ID, "2M_4M", value6.c_str(), length6, g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
    fclose(fp);
}

TEST_F(TestBio, test_bio_get_diff_size)
{
    uint64_t realLen0 = 6000;
    char *value0 = new char[realLen0];
    ObjLocation locationInfo0{0, 0};
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen0, locationInfo0, value0, &realLen0);
    delete[] value0;
    EXPECT_EQ(ret, RET_CACHE_OK);
    uint64_t realLen = 10000;
    char *value = new char[realLen];
    ObjLocation locationInfo{0, 0};
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen, locationInfo, value, &realLen);
    delete[] value;
    EXPECT_EQ(ret, RET_CACHE_OK);
    uint64_t realLen1 = 100000;
    char *value1 = new char[realLen1];
    ObjLocation locationInfo1{0, 0};
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen1, locationInfo1, value1, &realLen1);
    delete[] value1;
    EXPECT_EQ(ret, RET_CACHE_OK);
    uint64_t realLen2 = 160000;
    char *value2 = new char[realLen2];
    ObjLocation locationInfo2{0, 0};
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen2, locationInfo2, value2, &realLen2);
    delete[] value2;
    EXPECT_EQ(ret, RET_CACHE_OK);
    uint64_t realLen3 = 300000;
    char *value3 = new char[realLen3];
    ObjLocation locationInfo3{0, 0};
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen3, locationInfo3, value3, &realLen3);
    delete[] value3;
    EXPECT_EQ(ret, RET_CACHE_OK);
    uint64_t realLen4 = 1500000;
    char *value4 = new char[realLen4];
    ObjLocation locationInfo4{0, 0};
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen4, locationInfo4, value4, &realLen4);
    delete[] value4;
    EXPECT_EQ(ret, RET_CACHE_OK);
    uint64_t realLen5 = 3300000;
    char *value5 = new char[realLen5];
    ObjLocation locationInfo5{0, 0};
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen5, locationInfo5, value5, &realLen5);
    delete[] value5;
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_get_case_return_fail)
{
    // invalid length
    uint64_t realLen = 3300000;
    char *value = new char[realLen];
    ObjLocation locationInfo6{0, 0};
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, 0, locationInfo6, value, &realLen);
    delete[] value;
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // length exceed limit
    uint64_t realLen1 = 4194305;
    char *value1 = new char[realLen1];
    ObjLocation locationInfo7{0, 0};
    ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen1, locationInfo7, value1, &realLen1);
    delete[] value1;
    EXPECT_EQ(ret, RET_CACHE_READ_EXCEED);
}

TEST_F(TestBio, test_bio_list_all)
{
    auto prefix = "456";
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(G_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_OK);
    EXPECT_EQ(objNum, 1);
    // null objNum
    ret = BioListAll(G_TENANT_ID, prefix, &objs, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // invalid tenantId
    ret = BioListAll(G_INVALID_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    // null prefix
    ret = BioListAll(G_TENANT_ID, nullptr, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
}

TEST_F(TestBio, test_bio_stat)
{
    ObjLocation locationInfo{0, 0};
    ObjStat keyStat;
    auto ret = BioStat(G_TENANT_ID, G_KEY, locationInfo, &keyStat);
    EXPECT_EQ(keyStat.size, G_LENGTH);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // invalid key
    ret = BioStat(G_TENANT_ID, nullptr, locationInfo, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_ERROR);
    std::string invalidKey(KEY_MAX_SIZE + 1, ' ');
    ret = BioStat(G_TENANT_ID, invalidKey.c_str(), locationInfo, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // null stat
    ret = BioStat(G_TENANT_ID, G_KEY, locationInfo, nullptr);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // nonexistent tenantId
    ret = BioStat(G_INVALID_TENANT_ID, G_KEY, locationInfo, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    // error branches
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CHECK_PT_FAIL", 0, G_PT_TIMES, userParam);
    ret = BioStat(G_TENANT_ID, G_KEY, locationInfo, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_PT_FAULT);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CHECK_PT_FAIL");
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
    ObjLocation locationInfo{0, 0};
    LoadContext loadCtx;
    sem_init(&(loadCtx.sem), 0, 0);
    loadCtx.result = RET_CACHE_OK;
    auto ret = BioLoad(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // invalid tenantId
    ret = BioLoad(G_INVALID_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    // invalid load parameter
    ret = BioLoad(G_TENANT_ID, nullptr, 0, G_LENGTH, locationInfo, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // error branches
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, G_PT_TIMES, userParam);
    ret = BioLoad(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");
    sem_wait(&(loadCtx.sem));
    sem_destroy(&(loadCtx.sem));
}

TEST_F(TestBio, test_bio_delete)
{
    ObjLocation locationInfo{0, 0};
    auto ret = BioDelete(G_TENANT_ID, G_KEY, locationInfo);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // get after delete
    char *value = new char[G_LENGTH];
    uint64_t realLen = G_LENGTH;
    ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, value, &realLen);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    // invalid key
    ret = BioDelete(G_TENANT_ID, nullptr, locationInfo);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // delete a non-existent cache
    ret = BioDelete(G_INVALID_TENANT_ID, G_KEY, locationInfo);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

CacheSpaceInfo addressInfo;

TEST_F(TestBio, test_bio_allocspace)
{
    static uint64_t objectId = 1;
    addressInfo.allocLoc = 0;
    auto ret = BioAllocSpace(G_TENANT_ID, objectId++, ock::bio::NO_1024, &addressInfo);
    EXPECT_EQ(ret, RET_CACHE_OK);
    addressInfo.allocLoc = 1;
    ret = BioAllocSpace(G_TENANT_ID, objectId++, ock::bio::NO_1024, &addressInfo);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // nonexistent tenantId
    ret = BioAllocSpace(G_INVALID_TENANT_ID, objectId++, ock::bio::NO_1024, &addressInfo);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_bio_putwithspace_case)
{
    auto ret = BioPutWithSpace(G_TENANT_ID, "putwithspace", &addressInfo);
    EXPECT_EQ(ret, RET_CACHE_OK);
    // invalid key
    ret = BioPutWithSpace(G_TENANT_ID, nullptr, &addressInfo);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
    // nonexistent tenantId
    ret = BioPutWithSpace(G_INVALID_TENANT_ID, "putwithspace1", &addressInfo);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
    // error branches
    constexpr uint64_t tenantId = 10001;
    CacheSpaceInfo addressInfo1;
    AffinityStrategy affinity = LOCAL_AFFINITY;
    WriteStrategy strategy = WRITE_BACK;
    ret = BioCreateCache({tenantId, affinity, strategy});
    EXPECT_EQ(ret, RET_CACHE_OK);
    static uint64_t objectId = 11;
    addressInfo.allocLoc = 0;
    ret = BioAllocSpace(tenantId, objectId, ock::bio::NO_1024, &addressInfo1);
    EXPECT_EQ(ret, RET_CACHE_OK);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL", 0, G_PT_TIMES, userParam);
    ret = BioPutWithSpace(tenantId, "putwithspace2", &addressInfo1);
    EXPECT_EQ(ret, RET_CACHE_PT_FAULT);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PT_VIEW_FIND_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_CHECK_PT_FAIL", 0, G_PT_TIMES, userParam);
    ret = BioPutWithSpace(tenantId, "putwithspace3", &addressInfo1);
    EXPECT_EQ(ret, RET_CACHE_PT_FAULT);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_CHECK_PT_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_PUT_MEMORY_FAIL", 0, G_PT_TIMES, userParam);
    ret = BioPutWithSpace(tenantId, "putwithspace4", &addressInfo1);
    EXPECT_EQ(ret, RET_CACHE_ERROR);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_PUT_MEMORY_FAIL");

    LVOS_HVS_activeTracePoint(0, "SDK_MIRROR_SEND_PUT_FAIL", 0, G_PT_TIMES, userParam);
    ret = BioPutWithSpace(tenantId, "putwithspace5", &addressInfo1);
    EXPECT_EQ(ret, RET_CACHE_ERROR);
    LVOS_HVS_deactiveTracePoint(0, "SDK_MIRROR_SEND_PUT_FAIL");
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
    TestBio::VNodeIdStub();
    auto prefix = "456";
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(G_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_OK);
    BioFreeListResources(objs, objNum);
}

TEST_F(TestBio, test_bio_put_remote_case_return_fail)
{
    TestBio::VNodeIdStub();
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *) value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(G_TENANT_ID, "putremote", value.c_str(), G_LENGTH, g_Location);
    fclose(fp);
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
}

TEST_F(TestBio, test_bio_put_remote_ptv_error_case_return_fail)
{
    TestBio::VNodeIdStub();
    TestBio::GetPtVersionStub();
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *) value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(G_TENANT_ID, "putremoteptverror", value.c_str(), G_LENGTH, g_Location);
    fclose(fp);
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
}

TEST_F(TestBio, test_bio_get_remote_case_return_fail)
{
    TestBio::VNodeIdStub();
    char *value = new char[G_LENGTH];
    ObjLocation locationInfo{0, 0};
    uint64_t realLen = G_LENGTH;
    auto ret = BioGet(G_TENANT_ID, "getremote", 0, G_LENGTH, locationInfo, value, &realLen);
    delete[] value;
    EXPECT_EQ(ret, RET_CACHE_NEED_RETRY);
}

TEST_F(TestBio, test_pt_entry_list_update_node_state_down)
{
    auto ptList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * 2);

    ptList->poolId = 0;
    ptList->ptNum = ock::bio::NO_2;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < ock::bio::NO_2; diskIdx++) {
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
    EXPECT_EQ(0, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_pt_entry_list_update_node_state_up_down)
{
    auto ptList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * ock::bio::NO_2);

    ptList->poolId = 0;
    ptList->ptNum = ock::bio::NO_2;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < ock::bio::NO_2; diskIdx++) {
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
    auto nodeInfo = (NodeInfo *) malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;
    nodeInfo->diskList.list[1].state = DISK_STATE_NORMAL;
    ViewPtEntryListUpdateNodeState(0, NODE_STATE_UP, nodeInfo, ptList, pgChange.get());
    EXPECT_EQ(0, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_pt_entry_list_update_node_state_up_running)
{
    auto ptList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * 2);

    ptList->poolId = 0;
    ptList->ptNum = ock::bio::NO_2;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < ock::bio::NO_2; diskIdx++) {
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
    auto nodeInfo = (NodeInfo *) malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_FAULT;
    nodeInfo->diskList.list[1].state = DISK_STATE_FAULT;
    ViewPtEntryListUpdateNodeState(0, NODE_STATE_UP, nodeInfo, ptList, pgChange.get());
    EXPECT_EQ(0, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_pt_entry_list_update_node_state_up_recovery)
{
    auto ptList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * ock::bio::NO_2);

    ptList->poolId = 0;
    ptList->ptNum = ock::bio::NO_2;
    ptList->maxCopyNum = 1;
    ptList->minCopyNum = 1;
    ptList->globalVersion = 1;
    ptList->changeVersion = 1;
    for (uint16_t diskIdx = 0; diskIdx < ock::bio::NO_2; diskIdx++) {
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
    auto nodeInfo = (NodeInfo *) malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_FAULT;
    nodeInfo->diskList.list[1].state = DISK_STATE_FAULT;
    ViewPtEntryListUpdateNodeState(0, NODE_STATE_UP, nodeInfo, ptList, pgChange.get());
    EXPECT_EQ(0, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_pt_entry_list_update_node_finish)
{
    auto ptEntryList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * ock::bio::NO_2);

    ptEntryList->poolId = 0;
    ptEntryList->ptNum = ock::bio::NO_2;
    ptEntryList->maxCopyNum = 1;
    ptEntryList->minCopyNum = -1;
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
    auto ptList = (CmPtFinish *) malloc(sizeof(CmPtFinish) * ock::bio::NO_2);
    ptList[0].birthVersion = 1;
    ptList[0].ptId = 1;
    ptList[1].birthVersion = 1;
    ptList[1].ptId = 1;
    auto pgChange = std::make_unique<int>(sizeof(int32_t));
    ViewPtEntryListUpdateNodeFinish(0, ptList, ock::bio::NO_2, ptEntryList, pgChange.get(), 1, 1);
    EXPECT_EQ(0, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_create_view_caculator)
{
    CreateViewCalculator(1, 1, 1, 1);
    EXPECT_EQ(0, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_view_caculator_initial)
{
    Calculator calculator = CreateViewCalculator(1, 1, 1, 1);
    auto notifyList = static_cast<NodeInfoList *>(malloc(sizeof(NodeInfoList) + sizeof(NodeInfo)));

    auto nodeInfo = (NodeInfo *) malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;

    notifyList->poolId = 0;
    notifyList->nodeNum = 0;
    notifyList->nodeList[0] = *nodeInfo;
    notifyList->nodeNum++;

    auto ptEntryList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * ock::bio::NO_2);

    ptEntryList->poolId = 0;
    ptEntryList->ptNum = ock::bio::NO_2;
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
    }

    auto len = (int32_t) (sizeof(NodeStateList) + sizeof(NodeStateInfo));

    NodeDiskState nodeDiskState{0, DISK_CLUSTER_STATE_IN};
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
    EXPECT_EQ(0, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_caculator_need_rebalance)
{
    Calculator calculator = CreateViewCalculator(1, 1, 1, 1);
    auto notifyList = static_cast<NodeInfoList *>(malloc(sizeof(NodeInfoList) + sizeof(NodeInfo)));

    auto nodeInfo = (NodeInfo *) malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;

    notifyList->poolId = 0;
    notifyList->nodeNum = 0;
    notifyList->nodeList[0] = *nodeInfo;
    notifyList->nodeNum++;

    auto ptEntryList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * ock::bio::NO_2);

    ptEntryList->poolId = 0;
    ptEntryList->ptNum = ock::bio::NO_2;
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

    auto len = (int32_t) (sizeof(NodeStateList) + sizeof(NodeStateInfo));

    NodeDiskState nodeDiskState{0, DISK_CLUSTER_STATE_IN};
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
}

TEST_F(TestBio, test_caculator_rebalance)
{
    Calculator calculator = CreateViewCalculator(1, 1, 1, 1);
    auto notifyList = static_cast<NodeInfoList *>(malloc(sizeof(NodeInfoList) + sizeof(NodeInfo)));

    auto nodeInfo = (NodeInfo *) malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;

    notifyList->poolId = 0;
    notifyList->nodeNum = 0;
    notifyList->nodeList[0] = *nodeInfo;
    notifyList->nodeNum++;

    auto ptEntryList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * ock::bio::NO_2);

    ptEntryList->poolId = 0;
    ptEntryList->ptNum = ock::bio::NO_2;
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

    auto len = (int32_t) (sizeof(NodeStateList) + sizeof(NodeStateInfo));

    NodeDiskState nodeDiskState{0, DISK_CLUSTER_STATE_IN};
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
}

TEST_F(TestBio, test_caculator_rebalance_state_init)
{
    Calculator calculator = CreateViewCalculator(1, 1, 1, 1);
    auto notifyList = static_cast<NodeInfoList *>(malloc(sizeof(NodeInfoList) + sizeof(NodeInfo)));

    auto nodeInfo = (NodeInfo *) malloc(sizeof(NodeInfo));
    CM_GetNodeInfo(0, nodeInfo);
    nodeInfo->diskList.list[0].state = DISK_STATE_NORMAL;

    notifyList->poolId = 0;
    notifyList->nodeNum = 0;
    notifyList->nodeList[0] = *nodeInfo;
    notifyList->nodeNum++;

    auto ptEntryList = (PtEntryList *) malloc(sizeof(PtEntryList) + sizeof(PtEntry) * ock::bio::NO_2);

    ptEntryList->poolId = 0;
    ptEntryList->ptNum = ock::bio::NO_2;
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
        ptEntryList->ptEntryList[diskIdx].copyList[0].state = PT_COPY_STATE_INIT;
        ptEntryList->ptEntryList[diskIdx].copyList[1].nodeId = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].diskId = diskIdx;
        ptEntryList->ptEntryList[diskIdx].copyList[1].keepAlive = 0;
        ptEntryList->ptEntryList[diskIdx].copyList[1].state = PT_COPY_STATE_INIT;
    }

    auto len = (int32_t) (sizeof(NodeStateList) + sizeof(NodeStateInfo));

    NodeDiskState nodeDiskState{0, DISK_CLUSTER_STATE_IN};
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
}

uint64_t ReadHookFunc(uint64_t inode, char *buff, uint64_t count, uint64_t offset, int *readLen)
{
    return ock::bio::BIO_OK;
}

uint64_t WriteHookFunc(uint64_t inode, char *buff, uint64_t count, uint64_t offset, uint64_t fh)
{
    return ock::bio::BIO_OK;
}

uint64_t WriteCopyFreeHookFunc(uint64_t inode, uint64_t offset, uint64_t count, CacheSpaceInfo *spaceInfo)
{
    return ock::bio::BIO_OK;
}

TEST_F(TestBio, test_juicefs_callback_read_case_return_ok)
{
    BioRegisterJuiceFSRead(ReadHookFunc);
    auto ret = BioReadHook(0, nullptr, 0, 0, nullptr);
    EXPECT_EQ(ret, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_juicefs_callback_write_case_return_ok)
{
    BioRegisterJuiceFSWrite(WriteHookFunc);
    auto ret = BioWriteHook(0, nullptr, 0, 0, 0);
    EXPECT_EQ(ret, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_juicefs_callback_write_copy_case_return_ok)
{
    BioRegisterJuiceFSWriteCopyFree(WriteCopyFreeHookFunc);
    auto ret = BioWriteCopyFreeHook(0, 0, 0, nullptr);
    EXPECT_EQ(ret, ock::bio::BIO_OK);
}

TEST_F(TestBio, test_bio_initialize_stratege_case_return_fail)
{
    BioExit();
    auto ret = BioInitialize(WorkerMode::SEPARATES);
    EXPECT_EQ(ret, ock::bio::BIO_INNER_ERR);
}

TEST_F(TestBio, test_bio_initialize_dlopen_fail_case_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "DLOPEN_SERVERSO_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "DLOPEN_SERVERSO_FAIL_RESET", 0, 1, userParam);
    auto ret = BioInitialize(WorkerMode::CONVERGENCE);
    EXPECT_EQ(ret, ock::bio::BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "DLOPEN_SERVERSO_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "DLOPEN_SERVERSO_FAIL_RESET");
}

TEST_F(TestBio, test_bio_calculateLocation_not_ready_case_return_fail)
{
    BioExit();
    uint32_t sliceId = 1;
    auto ret = BioCalcLocation(G_TENANT_ID, sliceId, &g_Location);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
}

TEST_F(TestBio, test_bio_put_not_ready_case_return_fail)
{
    BioExit();
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *) value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(G_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    fclose(fp);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
}

TEST_F(TestBio, test_bio_get_not_ready_case_return_fail)
{
    BioExit();
    uint64_t realLen = 6000;
    char *value = new char[realLen];
    ObjLocation locationInfo{0, 0};
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, realLen, locationInfo, value, &realLen);
    delete[] value;
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
}

TEST_F(TestBio, test_bio_delete_not_ready_case_return_fail)
{
    BioExit();
    ObjLocation locationInfo{0, 0};
    auto ret = BioDelete(G_TENANT_ID, G_KEY, locationInfo);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
}

TEST_F(TestBio, test_bio_load_not_ready_case_return_fail)
{
    BioExit();
    ObjLocation locationInfo{0, 0};
    LoadContext loadCtx;
    sem_init(&(loadCtx.sem), 0, 1);
    loadCtx.result = RET_CACHE_OK;
    auto ret = BioLoad(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    sem_wait(&(loadCtx.sem));
    sem_destroy(&(loadCtx.sem));
}

TEST_F(TestBio, test_bio_list_all_not_ready_case_return_fail)
{
    BioExit();
    auto prefix = "456";
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(G_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
    BioFreeListResources(objs, objNum);
}

TEST_F(TestBio, test_bio_stat_not_ready_case_return_fail)
{
    BioExit();
    ObjLocation locationInfo{0, 0};
    ObjStat keyStat;
    auto ret = BioStat(G_TENANT_ID, G_KEY, locationInfo, &keyStat);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
}

TEST_F(TestBio, test_bio_allocspace_not_ready_case_return_fail)
{
    BioExit();
    static uint64_t objectId = 1;
    addressInfo.allocLoc = 1;
    auto ret = BioAllocSpace(G_TENANT_ID, objectId++, ock::bio::NO_1024, &addressInfo);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
}

TEST_F(TestBio, test_bio_putwithspace_not_ready_case_return_fail)
{
    BioExit();
    static uint64_t objectId = 1;
    auto ret = BioPutWithSpace(G_TENANT_ID, "putwithspace", &addressInfo);
    EXPECT_EQ(ret, RET_CACHE_NOT_READY);
}