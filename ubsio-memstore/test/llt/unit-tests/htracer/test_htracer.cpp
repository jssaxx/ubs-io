/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <string>
#include "tracer.h"
#include "mms_log.h"
#include "test_htracer.h"

using namespace ock::mms;

bool TestHtracer::gSetup = false;

void TestHtracer::SetUp()
{
    if (gSetup) {
        return;
    }
    ock::tracemark::TraceMark::Init();
    ock::tracemark::TraceMark::SetEnable(true);
    gSetup = true;
}

void TestHtracer::TearDown()
{
    ock::tracemark::TraceMark::ClearTrace();
}

void TestHtracer::Stub() noexcept
{
}

TEST_F(TestHtracer, test_get_trace_info_ok)
{
    LOG_INFO("test_get_trace_info_ok");
    constexpr int32_t traceId = ock::tracemark::GetTraceId(0, 33);
    ock::tracemark::TraceMark::MarkBegin(traceId, "TRACE_MARK_TEST");
    ock::tracemark::TraceMark::MarkEnd(traceId, 1000, 0);

    auto info = ock::tracemark::TraceMark::GetTraceInfo();
    EXPECT_NE(info.find("TRACE_MARK_TEST"), std::string::npos);
    EXPECT_NE(info.find("1.000"), std::string::npos);
}

TEST_F(TestHtracer, test_clear_trace)
{
    LOG_INFO("test_clear_trace");
    constexpr int32_t traceId = ock::tracemark::GetTraceId(0, 34);
    ock::tracemark::TraceMark::MarkBegin(traceId, "TRACE_MARK_CLEAR");
    ock::tracemark::TraceMark::MarkEnd(traceId, 2000, 0);
    ock::tracemark::TraceMark::ClearTrace();

    auto info = ock::tracemark::TraceMark::GetTraceInfo();
    EXPECT_NE(info.find("TRACE_MARK_CLEAR"), std::string::npos);
    EXPECT_NE(info.find("\t0"), std::string::npos);
}
