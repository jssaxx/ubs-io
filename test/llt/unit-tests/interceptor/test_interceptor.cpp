/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include <unistd.h>
#include <iostream>
#include "bio_log.h"
#include "bio_types.h"
#include "securec.h"
#include "tracepoint.h"
#include "interceptor_server.h"
#include "test_interceptor.h"

using namespace ock::bio;
using namespace ock::htracer;

bool TestInterceptor::gSetup = false;

static std::string g_stubDumpDir;

void TestInterceptor::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestInterceptor::TearDown()
{
    return;
}

TEST_F(TestInterceptor, test_bio_server_interceptor_read)
{
    LOG_INFO("test_bio_server_interceptor_read");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorPreadIn req;
    req.nbytes = NO_100;
    auto ret = instance.InterceptorServerRead(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestInterceptor, test_bio_server_interceptor_read_alloc_failed)
{
    LOG_INFO("test_bio_server_interceptor_read_alloc_failed");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorPreadIn req;
    req.nbytes = NO_100;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "INTERCEPTOR_SERVER_MALLOC_FAIL", 0, 1, userParam);
    auto ret = instance.InterceptorServerRead(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "INTERCEPTOR_SERVER_MALLOC_FAIL");

    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestInterceptor, test_bio_server_interceptor_read_bio_read_hook_failed)
{
    LOG_INFO("test_bio_server_interceptor_read_bio_read_hook_failed");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorPreadIn req;
    req.nbytes = NO_100;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "INTERCEPTOR_SERVER_BIO_READ_HOOK_FAIL", 0, 1, userParam);
    auto ret = instance.InterceptorServerRead(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "INTERCEPTOR_SERVER_BIO_READ_HOOK_FAIL");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestInterceptor, test_bio_server_interceptor_write)
{
    LOG_INFO("test_bio_server_interceptor_write");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorPwriteIn req;
    req.nbytes = NO_100;
    auto ret = instance.InterceptorServerWrite(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestInterceptor, test_bio_server_interceptor_write_fail)
{
    LOG_INFO("test_bio_server_interceptor_write_fail");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorPwriteIn req;
    req.nbytes = NO_100;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "INTERCEPTOR_SERVER_CHANGE_LEN", 0, 1, userParam);
    auto ret = instance.InterceptorServerWrite(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "INTERCEPTOR_SERVER_CHANGE_LEN");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestInterceptor, test_bio_server_interceptor_alloc_page)
{
    LOG_INFO("test_bio_server_interceptor_alloc_page");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorAllocPageReq req;
    req.length = NO_4194304 + NO_1048576;
    auto ret = instance.InterceptorServerAllocPage(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestInterceptor, test_bio_server_interceptor_alloc_page_fail)
{
    LOG_INFO("test_bio_server_interceptor_alloc_page_fail");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorAllocPageReq req;
    req.length = NO_100;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "INTERCEPTOR_SERVER_ALLOC_CACHE_SPACE_FAIL", 0, 1, userParam);
    auto ret = instance.InterceptorServerAllocPage(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "INTERCEPTOR_SERVER_ALLOC_CACHE_SPACE_FAIL");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestInterceptor, test_bio_server_interceptor_large_write)
{
    LOG_INFO("test_bio_server_interceptor_large_write");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorLargePwriteIn req;
    req.nbytes = NO_100;
    auto ret = instance.InterceptorServerLargeWrite(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestInterceptor, test_bio_server_interceptor_large_write_fail)
{
LOG_INFO("test_bio_server_interceptor_large_write_fail");
    auto instance = InterceptorServer::GetInstance();
    ServiceContext ctx;
    InterceptorLargePwriteIn req;
    req.nbytes = NO_100;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "INTERCEPTOR_SERVER_DATA_CHANGE_LEN", 0, 1, userParam);
    auto ret = instance.InterceptorServerLargeWrite(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "INTERCEPTOR_SERVER_DATA_CHANGE_LEN");
    EXPECT_EQ(ret, BIO_OK);
}