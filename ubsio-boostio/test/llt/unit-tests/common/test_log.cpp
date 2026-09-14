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

#include <string>

#include "bio_log.h"
#include "gtest/gtest.h"

TEST(TestBioLog, init_error_screen_outputs_error_before_logger_initialization)
{
    ock::bio::Logger::Destroy();
    ock::bio::Logger::SetInitErrorScreenEnabled(true);

    testing::internal::CaptureStderr();
    LOG_INFO("initialization info");
    LOG_ERROR("initialization failure");
    const std::string output = testing::internal::GetCapturedStderr();

    ock::bio::Logger::SetInitErrorScreenEnabled(false);
    EXPECT_EQ(output.find("initialization info"), std::string::npos);
    EXPECT_NE(output.find("initialization failure"), std::string::npos);
}

TEST(TestBioLog, init_error_screen_is_disabled_by_default)
{
    ock::bio::Logger::Destroy();
    ock::bio::Logger::SetInitErrorScreenEnabled(false);

    testing::internal::CaptureStderr();
    LOG_ERROR("hidden initialization failure");
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(output.empty()) << output;
}
