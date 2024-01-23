/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_LOG_H
#define BOOSTIO_BIO_LOG_H

#include <cstdint>
#include <cstring>
#include <sstream>
#include <sys/time.h>
#include <utility>

#include "spdlog/common.h"
#include "spdlog/spdlog.h"

namespace ock {
namespace bio {
#ifndef __BIO_LOG_FILENAME__
#define __BIO_LOG_FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define SPDLOG_LEVEL_TRACE 0
#define SPDLOG_LEVEL_DEBUG 1
#define SPDLOG_LEVEL_INFO 2
#define SPDLOG_LEVEL_WARN 3
#define SPDLOG_LEVEL_ERROR 4
#define SPDLOG_LEVEL_CRITICAL 5
#define SPDLOG_LEVEL_OFF 6

/* for default logger */
#define BIO_LOG_INTERNAL(level, file, line, msg)                                   \
    do {                                                                           \
        if (ock::bio::Logger::gInstance->IsHigherLevel(static_cast<int>(level))) { \
            std::ostringstream oss;                                                \
            oss.str("");                                                           \
            oss.clear();                                                           \
            oss << "[" << file << ":" << line << "] " << msg;                      \
            ock::bio::Logger::gInstance->Log(level, oss.str());                    \
        }                                                                          \
    } while (0)

#define LOG_CRITICAL(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_CRITICAL, __BIO_LOG_FILENAME__, __LINE__, msg)
#define LOG_ERROR(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_ERROR, __BIO_LOG_FILENAME__, __LINE__, msg)
#define LOG_WARN(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_WARN, __BIO_LOG_FILENAME__, __LINE__, msg)
#define LOG_INFO(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_INFO, __BIO_LOG_FILENAME__, __LINE__, msg)
#define LOG_DEBUG(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_DEBUG, __BIO_LOG_FILENAME__, __LINE__, msg)
#define LOG_TRACE(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_TRACE, __BIO_LOG_FILENAME__, __LINE__, msg)

#define ChkTrueNot(ARGS, RET)                     \
    do {                                          \
        if (__builtin_expect(!(ARGS), 0) != 0) {  \
            LOG_ERROR("Check Failed: " << #ARGS); \
            return RET;                           \
        }                                         \
    } while (0)

#define ChkTrueExNot(ARGS)                        \
    do {                                          \
        if (__builtin_expect(!(ARGS), 0) != 0) {  \
            LOG_ERROR("Check Failed: " << #ARGS); \
            return;                               \
        }                                         \
    } while (0)

#define ChkNot(ARGS)                              \
    do {                                          \
        if (__builtin_expect(!(ARGS), 0) != 0) {  \
            LOG_ERROR("Check Failed: " << #ARGS); \
        }                                         \
    } while (0)

#define ChkTrue(ARGS, RET, MSG)                                  \
    do {                                                         \
        if (__builtin_expect(!(ARGS), 0) != 0) {                 \
            LOG_ERROR("Check Failed: " << #ARGS << ", " << MSG); \
            return RET;                                          \
        }                                                        \
    } while (0)

#define ChkTrueEx(ARGS, MSG)                                     \
    do {                                                         \
        if (__builtin_expect(!(ARGS), 0) != 0) {                 \
            LOG_ERROR("Check Failed: " << #ARGS << ", " << MSG); \
            return;                                              \
        }                                                        \
    } while (0)

struct LoggerOptions {
    int32_t minLogLevel = 0;
    uint32_t rotationFileSizeInMB = 20;
    uint32_t rotationFileCount = 10;
    std::string path;
};

class Logger {
public:
    static Logger *Instance(const LoggerOptions &options);
    static void Destroy();

    static int32_t ChangeLogLevel(int32_t logLevel);

public:
    explicit Logger(LoggerOptions options) : mOptions(std::move(options)) {}
    ~Logger() = default;

    int32_t Init();
    void Exit();

    int32_t Log(int level, const std::string &message) const;
    void Flush();

    inline bool IsHigherLevel(int nowLevel) const
    {
        return nowLevel >= mOptions.minLogLevel;
    }

    static Logger *gInstance;

private:
    static bool ValidateParams(const LoggerOptions &options);
    static void LogToStdErr(const std::ostringstream &oss);

    static bool ValidateLogLevel(int32_t logLevel)
    {
        return logLevel <= SPDLOG_LEVEL_OFF && logLevel >= SPDLOG_LEVEL_TRACE;
    }

private:
    std::shared_ptr<spdlog::logger> mSpdLogger; /* spd logger for normal log */
    LoggerOptions mOptions;

private:
    static std::mutex gMutex;
};
}
}

#endif // BOOSTIO_BIO_LOG_H