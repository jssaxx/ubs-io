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

#include <climits>
#include <string>
#include "gtest/gtest.h"
#include "bio_file_util.h"
#include "bio_config_validator.h"

using namespace ock::bio;

namespace {
class PassthroughValidator : public Validator {
public:
    PassthroughValidator() : Validator("passthrough") {}
    bool Initialize() override
    {
        return true;
    }
};
}

TEST(TestConfigValidator, BaseAndStringValidators)
{
    PassthroughValidator base;
    EXPECT_TRUE(base.Initialize());
    EXPECT_TRUE(base.Validate(std::string("value")));
    EXPECT_TRUE(base.Validate(1));
    EXPECT_TRUE(base.Validate(1.0F));
    EXPECT_TRUE(base.Validate(1L));
    EXPECT_TRUE(base.ErrorMessage().empty());

    VStrRange invalidRange("range", 5, 5);
    EXPECT_FALSE(invalidRange.Initialize());
    VStrRange range("range", 0, 10);
    EXPECT_TRUE(range.Initialize());
    EXPECT_FALSE(range.Validate("1"));
    EXPECT_FALSE(range.Validate("~1"));
    EXPECT_FALSE(range.Validate("1~"));
    EXPECT_FALSE(range.Validate("-1~1"));
    EXPECT_FALSE(range.Validate("1~-1"));
    EXPECT_FALSE(range.Validate("x~1"));
    EXPECT_FALSE(range.Validate("1~x"));
    EXPECT_FALSE(range.Validate("0~11"));
    EXPECT_TRUE(range.Validate("0~10"));
    VStrRange emptyRange("range", 0, 10, true);
    EXPECT_TRUE(emptyRange.Validate(""));
    EXPECT_NE(VStrRange::Create("range", 0, 10).Get(), nullptr);

    VStrEnum emptyEnum("enum", "");
    EXPECT_FALSE(emptyEnum.Initialize());
    VStrEnum enumValidator("enum", "tcp||rdma");
    EXPECT_TRUE(enumValidator.Initialize());
    EXPECT_FALSE(enumValidator.Validate("tcp||rdma"));
    EXPECT_FALSE(enumValidator.Validate("udp"));
    EXPECT_TRUE(enumValidator.Validate("tcp"));
    EXPECT_NE(VStrEnum::Create("enum", "a||b").Get(), nullptr);

    VStrNotNull notNull("required");
    EXPECT_TRUE(notNull.Initialize());
    EXPECT_FALSE(notNull.Validate(""));
    EXPECT_TRUE(notNull.Validate("x"));
    EXPECT_NE(VStrNotNull::Create("required").Get(), nullptr);
}

TEST(TestConfigValidator, NumericValidators)
{
    VIntRange invalidInt("int", 2, 1);
    EXPECT_FALSE(invalidInt.Initialize());
    VIntRange intRange("int", 1, 3);
    EXPECT_TRUE(intRange.Initialize());
    EXPECT_FALSE(intRange.Validate(0));
    EXPECT_FALSE(intRange.Validate(4));
    EXPECT_TRUE(intRange.Validate(1));
    EXPECT_TRUE(intRange.Validate(3));
    VIntRange openIntRange("int", 1, INT32_MAX);
    EXPECT_FALSE(openIntRange.Validate(0));
    EXPECT_NE(VIntRange::Create("int", 1, 3).Get(), nullptr);

    VLongRange invalidLong("long", 3, 2);
    EXPECT_FALSE(invalidLong.Initialize());
    VLongRange longRange("long", 2, 2);
    EXPECT_TRUE(longRange.Initialize());
    EXPECT_FALSE(longRange.Validate(1L));
    EXPECT_FALSE(longRange.Validate(3L));
    EXPECT_TRUE(longRange.Validate(2L));
    VLongRange openLongRange("long", 1, LONG_MAX);
    EXPECT_FALSE(openLongRange.Validate(0L));
    EXPECT_NE(VLongRange::Create("long", 1, 3).Get(), nullptr);
}

TEST(TestConfigValidator, StringArrayValidator)
{
    VStrArray invalid("array", 2, 1, 2);
    EXPECT_FALSE(invalid.Initialize());
    VStrArray array("array", 1, 3, 2);
    EXPECT_TRUE(array.Initialize());
    EXPECT_FALSE(array.Validate("1"));
    EXPECT_FALSE(array.Validate("1,x"));
    EXPECT_FALSE(array.Validate("1,4"));
    EXPECT_TRUE(array.Validate("1,3"));
    VStrArray empty("array", 1, 3, 2, true);
    EXPECT_TRUE(empty.Validate(""));
    EXPECT_NE(VStrArray::Create("array", 1, 3, 2).Get(), nullptr);
}

TEST(TestConfigValidator, Ipv4AndMaskValidators)
{
    VIpv4Validator ipv4("ip", true);
    EXPECT_TRUE(ipv4.Initialize());
    EXPECT_TRUE(ipv4.Validate(""));
    EXPECT_FALSE(ipv4.Validate("127.0.0"));
    EXPECT_FALSE(ipv4.Validate("127.0.x.1"));
    EXPECT_FALSE(ipv4.Validate("127.0.-1.1"));
    EXPECT_FALSE(ipv4.Validate("127.0.256.1"));
    EXPECT_TRUE(ipv4.Validate("127.0.0.1"));
    EXPECT_NE(VIpv4Validator::Create("ip").Get(), nullptr);

    VIpv4MaskValidator mask("mask", true);
    EXPECT_TRUE(mask.Initialize());
    EXPECT_TRUE(mask.Validate(""));
    EXPECT_FALSE(mask.Validate("127.0.0.1"));
    EXPECT_FALSE(mask.Validate("127.0.0.1/x"));
    EXPECT_FALSE(mask.Validate("127.0.0.1/-1"));
    EXPECT_FALSE(mask.Validate("127.0.0.1/33"));
    EXPECT_FALSE(mask.Validate("127.0.0/24"));
    EXPECT_FALSE(mask.Validate("127.0.x.1/24"));
    EXPECT_FALSE(mask.Validate("127.0.256.1/24"));
    EXPECT_TRUE(mask.Validate("127.0.0.1/32"));
    EXPECT_NE(VIpv4MaskValidator::Create("mask").Get(), nullptr);
}

TEST(TestConfigValidator, Ipv4PortValidators)
{
    VIpv4PortValidator port("port", true);
    EXPECT_TRUE(port.Initialize());
    EXPECT_TRUE(port.Validate(""));
    EXPECT_FALSE(port.Validate("127.0.0.1"));
    EXPECT_FALSE(port.Validate("127.0.0.1:x"));
    EXPECT_FALSE(port.Validate("127.0.0.1:-1"));
    EXPECT_FALSE(port.Validate("127.0.0.1:65536"));
    EXPECT_FALSE(port.Validate("127.0.0:80"));
    EXPECT_FALSE(port.Validate("127.0.x.1:80"));
    EXPECT_FALSE(port.Validate("127.0.256.1:80"));
    EXPECT_TRUE(port.Validate("127.0.0.1:65535"));
    EXPECT_NE(VIpv4PortValidator::Create("port").Get(), nullptr);

    VIpv4PortListValidator list("list");
    EXPECT_TRUE(list.Initialize());
    EXPECT_FALSE(list.Validate(""));
    EXPECT_FALSE(list.Validate("127.0.0.1:80,invalid"));
    EXPECT_TRUE(list.Validate("127.0.0.1:80,10.0.0.1:443"));
    EXPECT_NE(VIpv4PortListValidator::Create("list").Get(), nullptr);
}

TEST(TestConfigValidator, PermissionRatioAndBooleanValidators)
{
    VFilePermissionValidator permission("permission", false);
    EXPECT_TRUE(permission.Initialize());
    EXPECT_FALSE(permission.Validate(""));
    EXPECT_FALSE(permission.Validate("77"));
    EXPECT_FALSE(permission.Validate("7777"));
    EXPECT_FALSE(permission.Validate("711"));
    EXPECT_FALSE(permission.Validate("077"));
    EXPECT_TRUE(permission.Validate("750"));
    VFilePermissionValidator ordered("permission", true);
    EXPECT_FALSE(ordered.Validate("475"));
    EXPECT_FALSE(ordered.Validate("745"));
    EXPECT_TRUE(ordered.Validate("754"));
    EXPECT_NE(VFilePermissionValidator::Create("permission", true).Get(), nullptr);

    VStrRatio ratio("ratio");
    EXPECT_TRUE(ratio.Initialize());
    EXPECT_FALSE(ratio.Validate(""));
    EXPECT_FALSE(ratio.Validate("4"));
    EXPECT_FALSE(ratio.Validate("x:6"));
    EXPECT_FALSE(ratio.Validate("-1:11"));
    EXPECT_FALSE(ratio.Validate("4:5"));
    EXPECT_TRUE(ratio.Validate("4:6"));
    EXPECT_NE(VStrRatio::Create("ratio").Get(), nullptr);

    VStrBoolRange boolean("boolean");
    EXPECT_TRUE(boolean.Initialize());
    EXPECT_FALSE(boolean.Validate("1"));
    EXPECT_TRUE(boolean.Validate("true"));
    EXPECT_TRUE(boolean.Validate("false"));
    EXPECT_NE(VStrBoolRange::Create("boolean").Get(), nullptr);
}

TEST(TestConfigValidator, PathAndCephPoolValidators)
{
    VStrRealPath path("path");
    EXPECT_TRUE(path.Initialize());
    EXPECT_FALSE(path.Validate(""));
    EXPECT_FALSE(path.Validate("/path/that/does/not/exist/boostio"));
    EXPECT_TRUE(path.Validate("."));
    EXPECT_NE(VStrRealPath::Create("path").Get(), nullptr);

    VStrCephPool pool("pool");
    EXPECT_TRUE(pool.Initialize());
    EXPECT_FALSE(pool.Validate(""));
    EXPECT_FALSE(pool.Validate("pool"));
    EXPECT_FALSE(pool.Validate("x:name"));
    EXPECT_FALSE(pool.Validate("-1:name"));
    EXPECT_TRUE(pool.Validate("0:first,2:second"));
    EXPECT_NE(VStrCephPool::Create("pool").Get(), nullptr);
}
