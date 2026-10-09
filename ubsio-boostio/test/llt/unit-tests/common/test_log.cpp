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

TEST(TestBioLog, stderr_error_has_standard_context)
{
    testing::internal::CaptureStderr();
    BIO_LOG_STD_ERR("bootstrap failure");
    const std::string output = testing::internal::GetCapturedStderr();

    const std::regex expected(
        R"(^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{6} [0-9]+ error \[test_log\.cpp:[0-9]+\]\[TestBody\] bootstrap failure\n$)");
    EXPECT_TRUE(std::regex_match(output, expected)) << output;
}
