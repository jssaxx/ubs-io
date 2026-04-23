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

#ifndef UBSIO_KVC_LOG_H
#define UBSIO_KVC_LOG_H

#include <syscall.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <sys/time.h>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace ock {
namespace ubsio {

enum LogLevel : int {
    DEBUG_LEVEL = 0,
    INFO_LEVEL,
    WARN_LEVEL,
    ERROR_LEVEL,
    BUTT_LEVEL // no use
};

#ifndef LIKELY
#define LIKELY(x) (__builtin_expect(!!(x), 1) != 0)
#endif

#ifndef UNLIKELY
#define UNLIKELY(x) (__builtin_expect(!!(x), 0) != 0)
#endif

#define PID_TID " [" << getpid() << "-" << syscall(SYS_gettid) << "]"

constexpr int MICROSECOND_WIDTH = 6;
using ExternalLog = void (*)(int, const char *);
using ExternalAuditLog = void (*)(const char *);
class UbsioLog {
public:
    static UbsioLog &Instance()
    {
        static UbsioLog gLogger;
        return gLogger;
    }

    inline LogLevel GetLogLevel() const
    {
        return logLevel_;
    }

    inline int32_t SetLogLevel(LogLevel level)
    {
        if (level < DEBUG_LEVEL || level >= BUTT_LEVEL) {
            return -1;
        }
        logLevel_ = level;
        return 0;
    }

    inline void SetExternalLogFunction(ExternalLog func, bool forceUpdate = false)
    {
        if (logFunc_ == nullptr || forceUpdate) {
            logFunc_ = func;
        }
    }

    inline void Log(int level, const std::ostringstream &oss) const
    {
        if (level < logLevel_) {
            return;
        }

        if (logFunc_ != nullptr) {
            logFunc_(level, oss.str().c_str());
            return;
        }

        struct timeval tv{};
        char strTime[24];

        gettimeofday(&tv, nullptr);
        time_t timeStamp = tv.tv_sec;
        struct tm localTime{};
        if (strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", localtime_r(&timeStamp, &localTime)) != 0) {
            std::cout << strTime << std::setw(MICROSECOND_WIDTH) << std::setfill('0') << tv.tv_usec << " "
                      << LogLevelDesc(level) << PID_TID << oss.str() << std::endl;
        } else {
            std::cout << " Invalid time " << LogLevelDesc(level) << PID_TID << oss.str() << std::endl;
        }
    }

    inline void AuditLog(const std::ostringstream &oss) const
    {
        if (auditLogFunc_ != nullptr) {
            auditLogFunc_(oss.str().c_str());
            return;
        }
    }

    int32_t GetLogLevel(const std::string &logLevelDesc) const
    {
        for (uint32_t count = DEBUG_LEVEL; count < BUTT_LEVEL; count++) {
            if (logLevelDesc == logLevelDesc_[count]) {
                return count;
            }
        }
        // 没有匹配到日志级别，使用默认级别INFO
        return INFO_LEVEL;
    }

    UbsioLog(const UbsioLog &) = delete;
    UbsioLog(UbsioLog &&) = delete;
    UbsioLog &operator=(const UbsioLog &other) = delete;
    UbsioLog &operator=(UbsioLog &&) = delete;

    ~UbsioLog()
    {
        logFunc_ = nullptr;
        auditLogFunc_ = nullptr;
    }

private:
    UbsioLog() = default;

    const char *LogLevelDesc(const int level) const
    {
        if (UNLIKELY(level < DEBUG_LEVEL || level >= BUTT_LEVEL)) {
            return "invalid";
        }
        return logLevelDesc_[level];
    }

private:
    LogLevel logLevel_ = INFO_LEVEL;
    const char *logLevelDesc_[BUTT_LEVEL] = {"DEBUG", "INFO", "WARN", "ERROR"};
    ExternalLog logFunc_ = nullptr;
    ExternalAuditLog auditLogFunc_ = nullptr;
};

#define UBSIO_KVC_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define UBSIO_KVC_LOG_FORMAT "[UBSIO_KVC " << UBSIO_KVC_LOG_FILENAME << ":" << __LINE__ << " " << __FUNCTION__ << "] "
#define UBSIO_KVC_LOG_INTERNAL(level, msg)                            \
    do {                                                        \
        std::ostringstream oss;                                 \
        oss << UBSIO_KVC_LOG_FORMAT << msg;                           \
        ock::ubsio::UbsioLog::Instance().Log(level, oss);     \
    } while (0)

#define UBSIO_KVC_LOG_ERROR_WITH_ERRCODE(ARGS, ERRCODE)                           \
    do {                                                                    \
        std::ostringstream oss;                                             \
        oss << UBSIO_KVC_LOG_FORMAT << ARGS << ", error code " << ERRCODE;        \
        ock::ubsio::UbsioLog::Instance().Log(ERROR_LEVEL, oss);           \
    } while (0)

#define LOG_ERROR(msg) UBSIO_KVC_LOG_INTERNAL(ERROR_LEVEL, msg)
#define LOG_WARN(msg) UBSIO_KVC_LOG_INTERNAL(WARN_LEVEL, msg)
#define LOG_INFO(msg) UBSIO_KVC_LOG_INTERNAL(INFO_LEVEL, msg)
#define LOG_DEBUG(msg) UBSIO_KVC_LOG_INTERNAL(DEBUG_LEVEL, msg)

// if ARGS is false, print error
#define UBSIO_KVC_ASSERT_RETURN(ARGS, RET)             \
    do {                                         \
        if (__builtin_expect(!(ARGS), 0) != 0) { \
            LOG_ERROR("Assert " #ARGS);       \
            return RET;                          \
        }                                        \
    } while (0)

#define UBSIO_KVC_ASSERT_RET_VOID(ARGS)                \
    do {                                         \
        if (__builtin_expect(!(ARGS), 0) != 0) { \
            LOG_ERROR("Assert " #ARGS);       \
            return;                              \
        }                                        \
    } while (0)

#define UBSIO_KVC_ASSERT(ARGS)                         \
    do {                                         \
        if (__builtin_expect(!(ARGS), 0) != 0) { \
            LOG_ERROR("Assert " #ARGS);       \
        }                                        \
    } while (0)

#define UBSIO_KVC_RETURN_ERROR(result, msg)                     \
    do {                                                  \
        auto innerResult = (result);                      \
        if (UNLIKELY(innerResult != 0)) {                 \
            UBSIO_KVC_LOG_ERROR_WITH_ERRCODE(msg, innerResult); \
            return innerResult;                           \
        }                                                 \
    } while (0)

inline void SafeCloseFd(int32_t &fd)
{
    auto tmpFd = fd;
    if (UNLIKELY(tmpFd < 0)) {
        return;
    }
    if (__sync_bool_compare_and_swap(&fd, tmpFd, -1)) {
        close(tmpFd);
    }
}

inline void HcomPrint(int level, const char *msg)
{
    UBSIO_KVC_LOG_INTERNAL(level, msg);
}

} // namespace ubsio
} // namespace ock
#endif // UBSIO_KVC_LOG_H