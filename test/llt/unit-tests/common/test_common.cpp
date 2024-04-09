/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "test_common.h"
#include "bio_str_util.h"
#include "bio_file_util.h"

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
    float value = 0.0f;
    auto ret = StrUtil::StrToFloat("123.5", value);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(value, 123.5f);
}

TEST_F(TestCommon, test_strstartwith_return_ok)
{
    auto ret = StrUtil::StartWith("123.5", "1");
    EXPECT_EQ(ret, true);
}
