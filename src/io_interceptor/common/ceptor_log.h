/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 */

#ifndef CEPTOR_LOG_H
#define CEPTOR_LOG_H


namespace ock {
namespace interceptor {
using Logger = int (*)(const char*, ...);
enum class LogLevel {
    LOG_NULL = -1,
    LOG_CRIT = 0,
    LOG_ERROR = 1,
    LOG_WARN = 2,
    LOG_INFO = 3,
    LOG_DEBUG = 4,
};
void LogPrint(LogLevel logLevel, int line, const char* func, const char* format, ...);
}
}

#define LOGLEVEL ock::interceptor::LogLevel::LOG_NULL
#define INTERCEPTORLOG_ERROR(...) do { \
    if (LOGLEVEL >= ock::interceptor::LogLevel::LOG_ERROR) { \
        ock::interceptor::LogPrint(ock::interceptor::LogLevel::LOG_ERROR, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } \
} while (0)

#define INTERCEPTORLOG_WARN(...) do { \
    if (LOGLEVEL >= ock::interceptor::LogLevel::LOG_WARN) { \
        ock::interceptor::LogPrint(ock::interceptor::LogLevel::LOG_WARN, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } \
} while (0)

#define INTERCEPTORLOG_INFO(...) do { \
    if (LOGLEVEL >= ock::interceptor::LogLevel::LOG_INFO) { \
        ock::interceptor::LogPrint(ock::interceptor::LogLevel::LOG_INFO, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } \
} while (0)

#define INTERCEPTORLOG_DEBUG(...) do { \
    if (LOGLEVEL >= ock::interceptor::LogLevel::LOG_DEBUG) { \
        ock::interceptor::LogPrint(ock::interceptor::LogLevel::LOG_DEBUG, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } \
} while (0)

#define INTERCEPTORLOG_CTRI(...) do { \
    if (LOGLEVEL >= ock::interceptor::LogLevel::LOG_CRIT) { \
        ock::interceptor::LogPrint(ock::interceptor::LogLevel::LOG_CRIT, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } \
} while (0)

#endif // CEPTOR_LOG_H