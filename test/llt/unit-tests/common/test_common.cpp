/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "test_common.h"
#include <mockcpp/mockcpp.hpp>
#include "securec.h"
#include "bio_str_util.h"
#include "bio_file_util.h"
#include "bio_log.h"
#include "bio_mock.h"
#include "bio_cryptor_helper.h"

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

void TestCommon::Stub()
{
    MOCKER_CPP(&FileUtil::GetDiskCapacity, int64_t(*)(std::string & diskPath)).stubs().will(returnValue(1073741824));
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

TEST_F(TestCommon, test_decrypt_return_fail)
{
    LOG_INFO("test_decrypt_return_fail");
    BioCryptorHelper *mbioCryptorHelper = new (std::nothrow) BioCryptorHelper("bio.log", "bio.log");
    std::string path = "bio.log";
    std::pair<char *, int> result;
    auto ret = mbioCryptorHelper->Decrypt(1, path, result);
    EXPECT_EQ(ret, -1);

    path = "bio1.log";
    ret = mbioCryptorHelper->Decrypt(1, path, result);
    EXPECT_EQ(ret, -1);

    path = "libbio_sdk.so";
    ret = mbioCryptorHelper->Decrypt(1, path, result);
    EXPECT_EQ(ret, -1);
}
