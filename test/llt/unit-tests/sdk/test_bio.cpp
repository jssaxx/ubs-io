/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include <semaphore.h>
#include "gtest/gtest.h"
#include "bio_c.h"
#include "bio_client_net.h"
#include "bio_mock.h"
#include "test_bio.h"

using namespace ock::bio;

bool TestBio::gSetup = false;

constexpr uint32_t G_TENANT_ID = 5;
constexpr char *G_KEY = "456123keybio";
constexpr uint64_t G_LENGTH = 1024;

ObjLocation g_Location{};

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

TEST_F(TestBio, test_bio_initialize_case_return_ok)
{
    auto ret = BioInitialize(WorkerMode::CONVERGENCE);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_create_cache_case_return_ok)
{
    CacheDescriptor desc = { G_TENANT_ID, AffinityStrategy::LOCAL_AFFINITY, WriteStrategy::WRITE_BACK };
    auto ret = BioCreateCache(desc);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_get_cache_case_return_ok)
{
    CacheDescriptor desc;
    auto ret = BioGetCache(G_TENANT_ID, &desc);
    EXPECT_EQ(ret, RET_CACHE_OK);
    EXPECT_EQ(G_TENANT_ID, desc.tenantId);
}


TEST_F(TestBio, test_bio_calc_location_case_return_ok)
{
    uint32_t sliceId = 1;
    auto ret = BioCalcLocation(G_TENANT_ID, sliceId, &g_Location);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_put_case_return_ok)
{
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *)value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(G_TENANT_ID, G_KEY, value.c_str(), G_LENGTH, g_Location);
    fclose(fp);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_put_keynull_case_return_fail)
{
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *)value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(G_TENANT_ID, nullptr, value.c_str(), G_LENGTH, g_Location);
    fclose(fp);
    EXPECT_EQ(ret, RET_CACHE_EPERM);
}

TEST_F(TestBio, test_bio_put_tenantid_unexist_case_return_fail)
{
    FILE *fp = fopen("./bio_test", "r");
    EXPECT_NE(fp, nullptr);
    std::string value(G_LENGTH, ' ');
    EXPECT_EQ(fread((void *)value.c_str(), sizeof(char), G_LENGTH, fp), G_LENGTH);
    auto ret = BioPut(2, nullptr, value.c_str(), G_LENGTH, g_Location);
    fclose(fp);
    EXPECT_EQ(ret, RET_CACHE_NOT_FOUND);
}

TEST_F(TestBio, test_bio_get_case_return_ok)
{
    (void)system("touch bio_get_file");
    FILE *fp = fopen("./bio_get_file", "w");
    EXPECT_NE(fp, nullptr);
    char *value = new char[G_LENGTH];
    ObjLocation locationInfo{ 0, 0 };
    uint64_t realLen = G_LENGTH;
    auto ret = BioGet(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, value, &realLen);
    EXPECT_EQ(fwrite(value, sizeof(char), realLen, fp), realLen);
    delete[] value;
    fclose(fp);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_list_all_case_return_ok)
{
    auto prefix = "456";
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(G_TENANT_ID, prefix, &objs, &objNum);
    EXPECT_EQ(objNum, 1);
    free(objs);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_stat_case_return_ok)
{
    ObjLocation locationInfo{ 0, 0 };
    ObjStat keyStat;
    auto ret = BioStat(G_TENANT_ID, G_KEY, locationInfo, &keyStat);
    EXPECT_EQ(keyStat.size, G_LENGTH);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

struct LoadContext {
    sem_t sem;
    CResult result;
};

static void TestCallback(void *context, int32_t result)
{
    auto loadCtx = reinterpret_cast<LoadContext *>(context);
    loadCtx->result = static_cast<CResult>(result);
    sem_post(&(loadCtx->sem));
}

TEST_F(TestBio, test_bio_load_case_return_ok)
{
    ObjLocation locationInfo{ 0, 0 };
    LoadContext loadCtx;
    sem_init(&(loadCtx.sem), 0, 0);
    loadCtx.result = RET_CACHE_OK;
    auto ret = BioLoad(G_TENANT_ID, G_KEY, 0, G_LENGTH, locationInfo, TestCallback, &loadCtx);
    EXPECT_EQ(ret, RET_CACHE_OK);
    sem_wait(&(loadCtx.sem));
    sem_destroy(&(loadCtx.sem));
}

TEST_F(TestBio, test_bio_delete_case_return_ok)
{
    ObjLocation locationInfo{ 0, 0 };
    auto ret = BioDelete(G_TENANT_ID, G_KEY, locationInfo);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_destroy_cache_case_return_ok)
{
    auto ret = BioDestroyCache(G_TENANT_ID);
    EXPECT_EQ(ret, RET_CACHE_OK);
}

TEST_F(TestBio, test_bio_exit_case_return_ok)
{
    BioExit();
}