/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include <unistd.h>
#include <iostream>
#include "htracer.h"
#include "service//htracer_service.h"
#include "bio_log.h"
#include "securec.h"
#include "tracepoint.h"
#include "test_htracer.h"

using namespace ock::bio;
using namespace ock::htracer;

bool TestHtracer::gSetup = false;

static std::string g_stubDumpDir;

void TestHtracer::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestHtracer::TearDown()
{
    return;
}

int32_t TestHtracer::HTracerInitMock(const std::string &dumpDir)
{
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        LOG_INFO("Current worker path: " << cwd << ".");
    } else {
        return RET_ERR;
    }
    std::string mission("/htracer/");
    strncpy_s(cwd + strlen(cwd), strlen(mission.c_str()) + 1, mission.c_str(), strlen(mission.c_str()) + 1);
    auto &service = HTracerService::GetInstance();

    g_stubDumpDir = cwd;
    if (service.StartUp(g_stubDumpDir) != RET_OK) {
        return RET_ERR;
    }
    return RET_OK;
}

void TestHtracer::Stub() noexcept {}

TEST_F(TestHtracer, test_get_trace_info_ok)
{
    LOG_INFO("test_get_trace_info_ok");
    auto info = GetTraceInfo();
    EXPECT_NE(info, "");
}
