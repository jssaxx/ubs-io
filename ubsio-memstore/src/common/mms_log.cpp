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

#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <new>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "mms_log.h"

namespace ock {
namespace mms {
Logger *Logger::gInstance = nullptr;
std::mutex Logger::gMutex;
bool Logger::gInited = false;

constexpr int MIN_LOG_LEVEL_MAX = 5;
constexpr int SIZE_MB_SHIFT = 20;
constexpr auto ROTATION_FILE_SIZE_MAX_MB = 100UL; // 100MB
constexpr auto ROTATION_FILE_SIZE_MIN_MB = 2;     // 2MB
constexpr int ROTATION_FILE_COUNT_MAX = 100;
constexpr mode_t LOG_FILE_MODE = S_IRUSR | S_IWUSR | S_IRGRP;
constexpr mode_t ROTATED_LOG_FILE_MODE = S_IRUSR | S_IRGRP;

#define MMS_LOG_STD_ERR(msg)      \
    do {                          \
        std::ostringstream oss;   \
        oss.str("");              \
        oss.clear();              \
        oss << msg;               \
        Logger::LogToStdErr(oss); \
    } while (0)

void Logger::LogToStdErr(const std::ostringstream &oss)
{
    struct timeval tv {};
    char strTime[24];

    gettimeofday(&tv, nullptr);
    if (strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", localtime(&tv.tv_sec)) != 0) {
        std::cout << strTime << tv.tv_usec << " info " << syscall(SYS_gettid) << " " << oss.str() << std::endl;
    } else {
        std::cout << " Invalid time info " << syscall(SYS_gettid) << " " << oss.str() << std::endl;
    }
}

bool Logger::ValidateParams(const LoggerOptions &options)
{
    /* for normal log */
    if (options.minLogLevel < 0 || options.minLogLevel > MIN_LOG_LEVEL_MAX) {
        MMS_LOG_STD_ERR("Invalid min log level for logger, which should be 0,1,2,3,4,5");
        return false;
    }

    if (options.logType != LOG_TYPE_FILE) {
        return true;
    }

    if (options.path.empty()) {
        MMS_LOG_STD_ERR("Invalid path for logger, which is empty");
        return false;
    }

    if (options.rotationFileSizeInMB > ROTATION_FILE_SIZE_MAX_MB ||
        options.rotationFileSizeInMB < ROTATION_FILE_SIZE_MIN_MB) {
        MMS_LOG_STD_ERR("Invalid max file size for logger, which should be between 2MB to 100MB");
        return false;
    }

    if (options.rotationFileCount > ROTATION_FILE_COUNT_MAX || options.rotationFileCount < 1) {
        MMS_LOG_STD_ERR("Invalid max file count for logger, which should be less than 100");
        return false;
    }

    return true;
}

const char *Logger::LevelToString(int level)
{
    switch (level) {
        case MMSLOG_LEVEL_TRACE:
            return "trace";
        case MMSLOG_LEVEL_DEBUG:
            return "debug";
        case MMSLOG_LEVEL_INFO:
            return "info";
        case MMSLOG_LEVEL_WARN:
            return "warning";
        case MMSLOG_LEVEL_ERROR:
            return "error";
        case MMSLOG_LEVEL_CRITICAL:
            return "critical";
        default:
            return "unknown";
    }
}

std::string Logger::FormatLogLine(int level, const std::string &message)
{
    struct timeval tv {};
    struct tm timeInfo {};
    char strTime[32] = {0};

    gettimeofday(&tv, nullptr);
    localtime_r(&tv.tv_sec, &timeInfo);
    if (strftime(strTime, sizeof(strTime), "%Y-%m-%d %H:%M:%S", &timeInfo) == 0) {
        strTime[0] = '\0';
    }

    std::ostringstream oss;
    oss << strTime << "." << std::setw(6) << std::setfill('0') << tv.tv_usec << " " << syscall(SYS_gettid) << " "
        << LevelToString(level) << " " << message;
    return oss.str();
}

std::string Logger::GetRotatedFileName(uint32_t index) const
{
    return mOptions.path + "." + std::to_string(index);
}

bool Logger::NeedRotate() const
{
    struct stat st {};
    if (stat(mOptions.path.c_str(), &st) != 0) {
        return false;
    }
    return static_cast<uint64_t>(st.st_size) >= (static_cast<uint64_t>(mOptions.rotationFileSizeInMB) << SIZE_MB_SHIFT);
}

void Logger::RotateLogFile()
{
    if (mOptions.logType != LOG_TYPE_FILE || !NeedRotate()) {
        return;
    }

    if (mLogFile.is_open()) {
        mLogFile.close();
        chmod(mOptions.path.c_str(), ROTATED_LOG_FILE_MODE);
    }

    std::remove(GetRotatedFileName(mOptions.rotationFileCount).c_str());
    for (uint32_t index = mOptions.rotationFileCount; index > 1; --index) {
        std::rename(GetRotatedFileName(index - 1).c_str(), GetRotatedFileName(index).c_str());
    }
    std::rename(mOptions.path.c_str(), GetRotatedFileName(1).c_str());
    chmod(GetRotatedFileName(1).c_str(), ROTATED_LOG_FILE_MODE);
}

int32_t Logger::OpenLogFile()
{
    mLogFile.open(mOptions.path, std::ios::out | std::ios::app);
    if (!mLogFile.is_open()) {
        MMS_LOG_STD_ERR("Failed to open log file, path:" << mOptions.path);
        return -1L;
    }
    chmod(mOptions.path.c_str(), LOG_FILE_MODE);
    return 0;
}

Logger *Logger::Instance(const LoggerOptions &options)
{
    std::lock_guard<std::mutex> guard(gMutex);
    /* already created */
    if (gInstance != nullptr) {
        return gInstance;
    }

    /* create */
    if (!ValidateParams(options)) {
        return nullptr;
    }
    gInstance = new (std::nothrow) Logger(options);
    if (gInstance == nullptr) {
        MMS_LOG_STD_ERR("Failed to new Logger object, probably out of memory");
        return nullptr;
    }

    return gInstance;
}

void Logger::Destroy()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gInstance != nullptr) {
        gInstance->Exit();
        delete gInstance;
        gInstance = nullptr;
    }
}

int32_t Logger::Init()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gInited) {
        return 0;
    }
    if (mOptions.logType == LOG_TYPE_FILE && OpenLogFile() != 0) {
        return -1L;
    }

    Log(MMSLOG_LEVEL_INFO, "Log started at [" + std::string(LevelToString(mOptions.minLogLevel)) + "] level");
    Log(MMSLOG_LEVEL_INFO, "Log default format: yyyy-mm-dd hh:mm:ss.uuuuuu threadid loglevel msg");
    gInited = true;
    return 0;
}

void Logger::Exit()
{
    std::lock_guard<std::mutex> guard(mLogMutex);
    if (mLogFile.is_open()) {
        mLogFile.flush();
        mLogFile.close();
        chmod(mOptions.path.c_str(), ROTATED_LOG_FILE_MODE);
    }
    gInited = false;
}

int32_t Logger::Log(int level, const std::string &message) const
{
    if (level < 0 || level > 5) { // 5
        return -3L;
    }

    const std::string line = FormatLogLine(level, message);
    std::lock_guard<std::mutex> guard(mLogMutex);
    if (mOptions.logType == LOG_TYPE_STDOUT) {
        std::cout << line << std::endl;
    } else if (mOptions.logType == LOG_TYPE_STDERR) {
        std::cerr << line << std::endl;
    } else if (mOptions.logType == LOG_TYPE_FILE) {
        const_cast<Logger *>(this)->RotateLogFile();
        if (!mLogFile.is_open() && const_cast<Logger *>(this)->OpenLogFile() != 0) {
            return -2L;
        }
        mLogFile << line << std::endl;
        if (level >= MMSLOG_LEVEL_ERROR) {
            mLogFile.flush();
        }
    } else {
        return -2L;
    }
    return 0L;
}

void Logger::ResetLogLevel(int32_t logLevel)
{
    mOptions.minLogLevel = logLevel;
}
}
}
