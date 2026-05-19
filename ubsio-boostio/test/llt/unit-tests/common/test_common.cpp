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

#include <cstdlib>

#include "bio_file_util.h"
#include "bio_functions.h"
#include "bio_log.h"
#include "bio_str_util.h"
#include "interceptor_fd.h"
#include "message.h"

using namespace ock::bio;

namespace {
constexpr int TEST_FD = 10;
constexpr uint64_t TEST_INODE = 20;
constexpr size_t TEST_PREFETCH_VALUE = 3;
}

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

TEST_F(TestCommon, test_remote_read_window_cache_contains)
{
    LOG_INFO("test_remote_read_window_cache_contains");
    RemoteReadWindowCache cache{};
    cache.active = true;
    cache.fileOffset = INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;
    cache.dataLen = INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;
    cache.addrLen = cache.dataLen;

    EXPECT_TRUE(cache.Contains(cache.fileOffset, IO_SIZE_64K));
    EXPECT_TRUE(cache.Contains(cache.fileOffset + cache.dataLen - IO_SIZE_4K, IO_SIZE_4K));
    EXPECT_FALSE(cache.Contains(cache.fileOffset - 1, IO_SIZE_4K));
    EXPECT_FALSE(cache.Contains(cache.fileOffset + cache.dataLen - IO_SIZE_4K, IO_SIZE_4K + 1));
    cache.addrLen = cache.dataLen - 1;
    EXPECT_FALSE(cache.Contains(cache.fileOffset, IO_SIZE_4K));
}

TEST_F(TestCommon, test_remote_read_prefetch_env_parse)
{
    LOG_INFO("test_remote_read_prefetch_env_parse");
    unsetenv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST");
    EXPECT_EQ(ParseRemoteReadPrefetchEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", REMOTE_READ_PREFETCH_DEPTH_DEFAULT),
        REMOTE_READ_PREFETCH_DEPTH_DEFAULT);
    EXPECT_FALSE(ParseRemoteReadPrefetchBoolEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_BOOL_TEST", false));

    setenv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", "bad", 1);
    EXPECT_EQ(ParseRemoteReadPrefetchEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", REMOTE_READ_PREFETCH_DEPTH_DEFAULT),
        REMOTE_READ_PREFETCH_DEPTH_DEFAULT);

    setenv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", "-1", 1);
    EXPECT_EQ(ParseRemoteReadPrefetchEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", REMOTE_READ_PREFETCH_DEPTH_DEFAULT),
        REMOTE_READ_PREFETCH_DEPTH_DEFAULT);

    setenv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", "3", 1);
    EXPECT_EQ(ParseRemoteReadPrefetchEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", REMOTE_READ_PREFETCH_DEPTH_DEFAULT),
        TEST_PREFETCH_VALUE);

    setenv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", "99", 1);
    EXPECT_EQ(ParseRemoteReadPrefetchEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST", REMOTE_READ_PREFETCH_DEPTH_DEFAULT),
        REMOTE_READ_PREFETCH_LIMIT_MAX);
    unsetenv("INTERCEPTOR_REMOTE_READ_PREFETCH_TEST");

    setenv("INTERCEPTOR_REMOTE_READ_PREFETCH_BOOL_TEST", "true", 1);
    EXPECT_TRUE(ParseRemoteReadPrefetchBoolEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_BOOL_TEST", false));
    setenv("INTERCEPTOR_REMOTE_READ_PREFETCH_BOOL_TEST", "0", 1);
    EXPECT_FALSE(ParseRemoteReadPrefetchBoolEnv("INTERCEPTOR_REMOTE_READ_PREFETCH_BOOL_TEST", false));
    unsetenv("INTERCEPTOR_REMOTE_READ_PREFETCH_BOOL_TEST");
}

TEST_F(TestCommon, test_remote_read_prefetch_inflight_control)
{
    LOG_INFO("test_remote_read_prefetch_inflight_control");
    OpenFile file(TEST_FD, TEST_INODE);
    uint64_t generation = 0;
    uint64_t first = INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;
    uint64_t second = first + INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;

    EXPECT_TRUE(file.TryStartRemoteReadPrefetch(first, generation, 1));
    EXPECT_EQ(generation, 0U);
    EXPECT_FALSE(file.TryStartRemoteReadPrefetch(first, generation, 1));
    EXPECT_FALSE(file.TryStartRemoteReadPrefetch(second, generation, 1));

    file.FinishRemoteReadPrefetch(first, 0);
    EXPECT_TRUE(file.TryStartRemoteReadPrefetch(second, generation, 1));
    EXPECT_TRUE(file.IsRemoteReadPrefetchInFlight(second, IO_SIZE_64K));

    file.MarkRemoteReadEof(second);
    EXPECT_FALSE(file.TryStartRemoteReadPrefetch(second, generation, 1));
}

TEST_F(TestCommon, test_remote_read_large_io_sequential_state)
{
    LOG_INFO("test_remote_read_large_io_sequential_state");
    OpenFile file(TEST_FD, TEST_INODE);
    uint64_t first = 0;
    uint64_t second = INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;
    uint64_t third = second + INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;

    EXPECT_FALSE(file.IsRemoteReadSequential(first));
    EXPECT_FALSE(file.RecordRemoteRead(first, INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE));
    EXPECT_TRUE(file.IsRemoteReadSequential(second));
    EXPECT_TRUE(file.RecordRemoteRead(second, INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE));
    EXPECT_TRUE(file.IsRemoteReadSequential(third));

    EXPECT_FALSE(file.RecordRemoteRead(third + INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE,
        INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE));
    EXPECT_FALSE(file.IsRemoteReadSequential(third));
}
