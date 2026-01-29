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

#include "ceptor_log.h"

#include <cstdarg>
#include <mutex>
#include <cstdio>
#include <unistd.h>
#include <sys/prctl.h>
#include <unordered_map>
#include <sys/syscall.h>

#include "ceptor_timestamp.h"
#include "securec.h"

namespace ock {
namespace interceptor {
static std::mutex g_loggerMtx;
Logger g_logger = printf;
const int LOG_BUF_MAX_LEN = 512;

static const char* g_map[] = {"CRIT", "ERROR", "WARN", "INFO", "DEBUG"};

void LogPrint(LogLevel logLevel, int line, const char *func, const char *format, ...)
{
    va_list argPtr;
    char headBuff[LOG_BUF_MAX_LEN];
    char dataBuff[LOG_BUF_MAX_LEN];
    if (snprintf_s(headBuff, sizeof(headBuff), sizeof(headBuff) - 1,
        "[%s:%d][%s][%ld][%s]", func, line, g_map[static_cast<int>(logLevel)], syscall(SYS_gettid),
        TimeStamp::Now().ToFormattedString().data()) < 0) {
        return;
    }

    va_start(argPtr, format);
    int ret = vsnprintf_s(dataBuff, sizeof(dataBuff), sizeof(dataBuff) - 1, format, argPtr);
    if (ret < 0) {      // 缓冲区不足，出现截断时，丢弃并打印错误信息
        std::lock_guard<std::mutex> lock(g_loggerMtx);
        g_logger("%s %s\n", headBuff, "vsnprintf failed.");
        va_end(argPtr);
        return;
    }
    va_end(argPtr);
    std::lock_guard<std::mutex> lock(g_loggerMtx);
    g_logger("%s %s\n", headBuff, dataBuff);
}
}
}
