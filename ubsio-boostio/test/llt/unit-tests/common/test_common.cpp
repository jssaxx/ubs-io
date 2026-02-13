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

#include "test_common.h"
#include "bio_str_util.h"
#include "bio_functions.h"
#include "message.h"
#include "bio_file_util.h"
#include "bio_log.h"

using namespace ock::bio;

bool TestCommon::gSetup = false;

void TestCommon::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestCommon::TearDown()
{
    return;
}

TEST_F(TestCommon, test_strtofloat_return_ok)
{
    LOG_INFO("test_strtofloat_return_ok");
    float value = 0.0f;
    auto ret = StrUtil::StrToFloat("123.5", value);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(value, 123.5f);
}

TEST_F(TestCommon, test_strstartwith_return_ok)
{
    LOG_INFO("test_strstartwith_return_ok");
    auto ret = StrUtil::StartWith("123.5", "1");
    EXPECT_EQ(ret, true);
}

TEST_F(TestCommon, test_copy_key_fail)
{
    LOG_INFO("test_copy_key_fail");
    CopyKey(nullptr, nullptr, KEY_MAX_SIZE);

    auto ret = StrUtil::StartWith("123.5", "1");
    EXPECT_EQ(ret, true);
}