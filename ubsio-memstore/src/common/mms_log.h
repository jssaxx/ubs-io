/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef MMSCORE_MMS_LOG_H
#define MMSCORE_MMS_LOG_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/time.h>
#include <utility>

namespace ock {
namespace mms {

constexpr uint8_t LOG_TYPE_STDOUT = 0;
constexpr uint8_t LOG_TYPE_STDERR = 1;
constexpr uint8_t LOG_TYPE_FILE = 2;

#ifndef MMS_LOG_FILENAME
#define MMS_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define MMSLOG_LEVEL_TRACE 0
#define MMSLOG_LEVEL_DEBUG 1
#define MMSLOG_LEVEL_INFO 2
#define MMSLOG_LEVEL_WARN 3
#define MMSLOG_LEVEL_ERROR 4
#define MMSLOG_LEVEL_CRITICAL 5
#define MMSLOG_LEVEL_OFF 6

#define MMS_LOG_RESET_LEVEL(level)                         \
    do {                                                   \
        ock::mms::Logger::gInstance->ResetLogLevel(level); \
    } while (0)

#ifdef DEBUG_UT
#define MMS_LOG_INTERNAL(level, file, line, func, msg)
#else
/* for default logger */
#define MMS_LOG_INTERNAL(level, file, line, func, msg)                             \
    do {                                                                           \
        if (ock::mms::Logger::gInstance != nullptr &&                              \
            ock::mms::Logger::gInstance->IsHigherLevel(static_cast<int>(level))) { \
            std::ostringstream oss;                                                \
            oss.str("");                                                           \
            oss.clear();                                                           \
            oss << "[" << file << ":" << line << "]"                               \
                << "[" << func << "] " << msg;                                     \
            ock::mms::Logger::gInstance->Log(level, oss.str());                    \
        }                                                                          \
    } while (0)
#endif

#define LOG_CRITICAL(msg) MMS_LOG_INTERNAL(MMSLOG_LEVEL_CRITICAL, MMS_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_ERROR(msg) MMS_LOG_INTERNAL(MMSLOG_LEVEL_ERROR, MMS_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_WARN(msg) MMS_LOG_INTERNAL(MMSLOG_LEVEL_WARN, MMS_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_INFO(msg) MMS_LOG_INTERNAL(MMSLOG_LEVEL_INFO, MMS_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_DEBUG(msg) MMS_LOG_INTERNAL(MMSLOG_LEVEL_DEBUG, MMS_LOG_FILENAME, __LINE__, __FUNCTION__, msg)
#define LOG_TRACE(msg) MMS_LOG_INTERNAL(MMSLOG_LEVEL_TRACE, MMS_LOG_FILENAME, __LINE__, __FUNCTION__, msg)

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

#define ChkTrueVoid(ARGS, MSG)                                   \
    do {                                                         \
        if (__builtin_expect(!(ARGS), 0) != 0) {                 \
            LOG_ERROR("Check Failed: " << #ARGS << ", " << MSG); \
            return;                                              \
        }                                                        \
    } while (0)

struct LoggerOptions {
    uint8_t logType = 0;
    int32_t minLogLevel = 0;
    uint32_t rotationFileSizeInMB = 50;
    uint32_t rotationFileCount = 100;
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
    static const char *LevelToString(int level);
    static std::string FormatLogLine(int level, const std::string &message);
    int32_t OpenLogFile();
    void RotateLogFile();
    bool NeedRotate() const;
    std::string GetRotatedFileName(uint32_t index) const;

private:
    mutable std::mutex mLogMutex;
    mutable std::ofstream mLogFile;
    LoggerOptions mOptions;

private:
    static std::mutex gMutex;
    static bool gInited;
};
}
}

#endif // MMSCORE_MMS_LOG_H
