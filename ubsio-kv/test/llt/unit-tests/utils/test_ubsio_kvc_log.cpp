/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <atomic>
#include <sstream>
#include <unistd.h>
#include "test_common.h"
#include "ubsio_kvc_log.h"

using namespace ock::ubsio;

namespace {
std::atomic<int> gLogCalls{0};
std::atomic<int> gAlternateLogCalls{0};

void ExternalLogger(int, const char *)
{
    ++gLogCalls;
}

void AlternateLogger(int, const char *)
{
    ++gAlternateLogCalls;
}

TEST_F(KvTest, LoggerAndFileDescriptorHelpersAreSafe)
{
    auto &logger = UbsioLog::Instance();
    EXPECT_EQ(logger.SetLogLevel(DEBUG_LEVEL), 0);
    EXPECT_EQ(logger.SetLogLevel(BUTT_LEVEL), -1);
    EXPECT_EQ(logger.SetLogLevel(static_cast<LogLevel>(-1)), -1);
    EXPECT_EQ(logger.GetLogLevel("DEBUG"), DEBUG_LEVEL);
    EXPECT_EQ(logger.GetLogLevel("INFO"), INFO_LEVEL);
    EXPECT_EQ(logger.GetLogLevel("WARN"), WARN_LEVEL);
    EXPECT_EQ(logger.GetLogLevel("ERROR"), ERROR_LEVEL);
    EXPECT_EQ(logger.GetLogLevel("missing"), INFO_LEVEL);

    gLogCalls = 0;
    gAlternateLogCalls = 0;
    std::ostringstream message;
    message << "unit-test";

    logger.SetExternalLogFunction(nullptr, true);
    logger.Log(INFO_LEVEL, message);
    logger.Log(BUTT_LEVEL, message);
    logger.AuditLog(message);

    logger.SetExternalLogFunction(AlternateLogger);
    logger.Log(INFO_LEVEL, message);
    EXPECT_EQ(gAlternateLogCalls.load(), 1);
    logger.SetExternalLogFunction(ExternalLogger);
    logger.Log(INFO_LEVEL, message);
    EXPECT_EQ(gAlternateLogCalls.load(), 2);
    EXPECT_EQ(gLogCalls.load(), 0);

    logger.SetExternalLogFunction(ExternalLogger, true);
    logger.Log(INFO_LEVEL, message);
    EXPECT_EQ(gLogCalls.load(), 1);
    logger.SetLogLevel(ERROR_LEVEL);
    logger.Log(DEBUG_LEVEL, message);
    EXPECT_EQ(gLogCalls.load(), 1);
    logger.SetLogLevel(INFO_LEVEL);
    HcomPrint(INFO_LEVEL, "hcom-unit-test");
    EXPECT_EQ(gLogCalls.load(), 2);

    int descriptors[2]{};
    ASSERT_EQ(pipe(descriptors), 0);
    SafeCloseFd(descriptors[0]);
    EXPECT_EQ(descriptors[0], -1);
    SafeCloseFd(descriptors[0]);
    close(descriptors[1]);
}
}
