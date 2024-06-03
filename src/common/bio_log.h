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
#ifndef BIO_LOG_FILENAME
#define BIO_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define SPDLOG_LEVEL_TRACE 0
#define SPDLOG_LEVEL_DEBUG 1
#define SPDLOG_LEVEL_INFO 2
#define SPDLOG_LEVEL_WARN 3
#define SPDLOG_LEVEL_ERROR 4
#define SPDLOG_LEVEL_CRITICAL 5
#define SPDLOG_LEVEL_OFF 6

#define BIO_LOG_RESET_LEVEL(level)                         \
    do {                                                   \
        ock::bio::Logger::gInstance->ResetLogLevel(level); \
    } while (0)

/* for default logger */
#define BIO_LOG_INTERNAL(level, file, line, func, msg)                             \
    do {                                                                           \
        if (ock::bio::Logger::gInstance != nullptr &&                              \
            ock::bio::Logger::gInstance->IsHigherLevel(static_cast<int>(level))) { \
            std::ostringstream oss;                                                \
            oss.str("");                                                           \
            oss.clear();                                                           \
            oss << "[" << file << ":" << line << "]"                               \
                << "[" << func << "] " << msg;                                     \
            ock::bio::Logger::gInstance->Log(level, oss.str());                    \
        }                                                                          \
    } while (0)

#define LOG_CRITICAL(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_CRITICAL, BIO_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_ERROR(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_ERROR, BIO_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_WARN(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_WARN, BIO_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_INFO(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_INFO, BIO_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_DEBUG(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_DEBUG, BIO_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_TRACE(msg) BIO_LOG_INTERNAL(SPDLOG_LEVEL_TRACE, BIO_LOG_FILENAME, __LINE__, __FUNCTION__, msg)

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

#define ChkTrue(ARGS, RET, MSG)                                  \
    do {                                                         \
        if (__builtin_expect(!(ARGS), 0) != 0) {                 \
            LOG_ERROR("Check Failed: " << #ARGS << ", " << MSG); \
            return RET;                                          \
        }                                                        \
    } while (0)

struct LoggerOptions {
    uint8_t logType = 0;
    int32_t minLogLevel = 0;
    uint32_t rotationFileSizeInMB = 50;
    uint32_t rotationFileCount = 20;
    std::string path;
};

class Logger {
public:
    static Logger *Instance(const LoggerOptions &options);
    static void Destroy();

    explicit Logger(LoggerOptions options) : mOptions(std::move(options)) {}

    int32_t Init();
    void Exit();

    int32_t Log(int level, const std::string &message) const;

    void ResetLogLevel(int32_t logLevel);

    inline bool IsHigherLevel(int nowLevel) const
    {
        return nowLevel >= mOptions.minLogLevel;
    }

    static Logger *gInstance;

private:
    static bool ValidateParams(const LoggerOptions &options);
    static void LogToStdErr(const std::ostringstream &oss);

private:
    std::shared_ptr<spdlog::logger> mSpdLogger; /* spd logger for normal log */
    LoggerOptions mOptions;

private:
    static std::mutex gMutex;
    static bool gInited;
};
}
}

#endif // BOOSTIO_BIO_LOG_H