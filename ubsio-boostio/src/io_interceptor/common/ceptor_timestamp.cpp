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

#include <sys/time.h>
#include <cinttypes>
#include <ctime>
#include "securec.h"
#include "ceptor_timestamp.h"

namespace ock {
namespace interceptor {
TimeStamp::TimeStamp() : microSecondsSinceEpoch(0)
{}

TimeStamp::TimeStamp(int64_t microSeconds) : microSecondsSinceEpoch(microSeconds)
{}

std::string TimeStamp::ToString() const
{
    char buf[32] = {0};
    int64_t seconds = microSecondsSinceEpoch / microSecondPerSecond;
    int64_t microSeconds = microSecondsSinceEpoch % microSecondPerSecond;
    int32_t ret = snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "%" PRId64 ".%06" PRId64 "", seconds, microSeconds);
    if (ret < 0) {
        printf("snprintf_s failed.");
        return "";
    }
    return buf;
}

std::string TimeStamp::ToFormattedString() const
{
    char buf[64] = {0};
    time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / microSecondPerSecond);
    int microSeconds = static_cast<int>(microSecondsSinceEpoch % microSecondPerSecond);
    struct tm tmTime = {0};
    gmtime_r(&seconds, &tmTime);

    int32_t ret = snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "%4d%02d%02d %02d:%02d:%2d.%6d",
        static_cast<int>(tmTime.tm_year + 1900u), tmTime.tm_mon + 1, tmTime.tm_mday, tmTime.tm_hour, tmTime.tm_min,
        tmTime.tm_sec, microSeconds);
    if (ret < 0) {
        printf("snprintf_s failed.");
        return "";
    }
    return buf;
}

TimeStamp TimeStamp::Now()
{
    struct timeval tv = {0};
    gettimeofday(&tv, nullptr);
    int64_t seconds = tv.tv_sec;
    if ((INT64_MAX - tv.tv_usec) / microSecondPerSecond < seconds) {
        printf("the following multiplication operation will result a overflows");
        return TimeStamp();
    }
    return TimeStamp(seconds * microSecondPerSecond + tv.tv_usec);
}
}
}