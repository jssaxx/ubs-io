/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <cmath>
#include <set>
#include <string>
#include <vector>
#include "test_common.h"
#include "ubsio_kvc_str_util.h"

using namespace ock::ubsio;

namespace {
TEST_F(KvTest, StringUtilitiesCoverValidAndInvalidInputs)
{
    EXPECT_TRUE(StrUtil::StartWith("ubsio-kv", "ubsio"));
    EXPECT_FALSE(StrUtil::StartWith("ubsio-kv", "kv"));
    EXPECT_TRUE(StrUtil::EndWith("ubsio-kv", "kv"));
    EXPECT_FALSE(StrUtil::EndWith("ubsio-kv", "io"));

    long integer = 0;
    EXPECT_TRUE(StrUtil::StrToLong("42", integer));
    EXPECT_EQ(integer, 42);
    EXPECT_TRUE(StrUtil::StrToLong("0", integer));
    EXPECT_FALSE(StrUtil::StrToLong("invalid", integer));
    EXPECT_FALSE(StrUtil::StrToLong("12x", integer));
    EXPECT_FALSE(StrUtil::StrToLong("999999999999999999999999999999999", integer));

    float number = 0;
    EXPECT_TRUE(StrUtil::StrToFloat("1.25", number));
    EXPECT_FLOAT_EQ(number, 1.25F);
    EXPECT_TRUE(StrUtil::StrToFloat("0.0", number));
    EXPECT_FALSE(StrUtil::StrToFloat("invalid", number));
    EXPECT_FALSE(StrUtil::StrToFloat("1.2x", number));
    EXPECT_FALSE(StrUtil::StrToFloat("0", number));
    EXPECT_TRUE(StrUtil::StrToFloat("1e9999", number));
    EXPECT_TRUE(std::isinf(number));

    std::vector<std::string> parts;
    StrUtil::Split("a,b,c", ",", parts);
    ASSERT_EQ(parts.size(), 3U);
    EXPECT_EQ(parts[1], "b");
    parts.clear();
    StrUtil::Split("a,", ",", parts);
    ASSERT_EQ(parts.size(), 1U);

    std::set<std::string> uniqueParts;
    StrUtil::Split("a,b,a", ",", uniqueParts);
    EXPECT_EQ(uniqueParts.size(), 2U);

    std::string text = "  value  ";
    StrUtil::StrTrim(text);
    EXPECT_EQ(text, "value");
    text.clear();
    StrUtil::StrTrim(text);
    EXPECT_TRUE(text.empty());

    text = "a::b::";
    StrUtil::Replace(text, "::", "/");
    EXPECT_EQ(text, "a/b");

    parts.clear();
    StrUtil::Split("single", ",", parts);
    ASSERT_EQ(parts.size(), 1U);
    EXPECT_EQ(parts[0], "single");
    parts.clear();
    StrUtil::Split("", ",", parts);
    EXPECT_TRUE(parts.empty());

    uniqueParts.clear();
    StrUtil::Split("a,", ",", uniqueParts);
    EXPECT_EQ(uniqueParts.size(), 1U);

    text = "   ";
    StrUtil::StrTrim(text);
    EXPECT_TRUE(text.empty());
    text = "unchanged";
    StrUtil::Replace(text, "missing", "replacement");
    EXPECT_EQ(text, "unchanged");
}
}
