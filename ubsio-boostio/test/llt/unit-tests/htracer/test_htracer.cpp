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

#include "test_htracer.h"
#include <unistd.h>
#include <iostream>
#include <mockcpp/mockcpp.hpp>
#include "bio_log.h"
#include "htracer.h"
#include "securec.h"
#include "service//htracer_service.h"
#include "tracepoint.h"

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

    ClearTraceInfo();
}
