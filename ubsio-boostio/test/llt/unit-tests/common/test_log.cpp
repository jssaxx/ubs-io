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

#include <regex>
#include <string>

#include "bio_log.h"
#include "gtest/gtest.h"

namespace {
class TestBioLog : public testing::Test {
protected:
    void SetUp() override
    {
        ock::bio::Logger::SetInitErrorScreenEnabled(false);
        ock::bio::Logger::Destroy();
    }

    void TearDown() override
    {
        ock::bio::Logger::SetInitErrorScreenEnabled(false);
        ock::bio::Logger::Destroy();
    }
};
}

TEST_F(TestBioLog, stderr_error_has_standard_context)
{
    testing::internal::CaptureStderr();
    BIO_LOG_STD_ERR("bootstrap failure");
    const std::string output = testing::internal::GetCapturedStderr();

    const std::regex expected(
        R"(^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{6} [0-9]+ error \[test_log\.cpp:[0-9]+\]\[TestBody\] bootstrap failure\n$)");
    EXPECT_TRUE(std::regex_match(output, expected)) << output;
}

TEST_F(TestBioLog, init_error_screen_outputs_error_before_logger_initialization)
{
    ock::bio::Logger::SetInitErrorScreenEnabled(true);

    testing::internal::CaptureStderr();
    LOG_INFO("initialization info");
    LOG_ERROR("initialization failure");
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_EQ(output.find("initialization info"), std::string::npos);
    EXPECT_NE(output.find("initialization failure"), std::string::npos);
}

TEST_F(TestBioLog, init_error_screen_outputs_error_when_file_logger_initialization_fails)
{
    ock::bio::LoggerOptions options;
    options.logType = 1;
    options.path = "/path/that/does/not/exist/ubsio.log";
    auto *logger = ock::bio::Logger::Instance(options);
    ASSERT_NE(logger, nullptr);

    testing::internal::CaptureStderr();
    const int32_t initResult = logger->Init();
    testing::internal::GetCapturedStderr();
    ASSERT_NE(initResult, 0);

    ock::bio::Logger::SetInitErrorScreenEnabled(true);
    testing::internal::CaptureStderr();
    LOG_ERROR("logger backend initialization failure");
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_NE(output.find("logger backend initialization failure"), std::string::npos) << output;
}

TEST_F(TestBioLog, init_error_screen_suppresses_error_when_disabled)
{

    testing::internal::CaptureStderr();
    LOG_ERROR("hidden initialization failure");
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(output.empty()) << output;
}
