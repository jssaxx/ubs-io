/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */

#include <iostream>
#include "spdlog/sinks/rotating_file_sink.h"
#include "bio_log.h"

namespace ock {
namespace bio {
Logger *Logger::gInstance = nullptr;
std::mutex Logger::gMutex;
bool Logger::gInited = false;

constexpr int MIN_LOG_LEVEL_MAX = 5;
constexpr int SIZE_MB_SHIFT = 20;
constexpr auto ROTATION_FILE_SIZE_MAX_MB = 100UL;                                   // 100MB
constexpr auto ROTATION_FILE_SIZE_MAX = ROTATION_FILE_SIZE_MAX_MB << SIZE_MB_SHIFT; // 100MB
constexpr auto ROTATION_FILE_SIZE_MIN_MB = 2;                                       // 2MB
constexpr auto ROTATION_FILE_SIZE_MIN = ROTATION_FILE_SIZE_MIN_MB << SIZE_MB_SHIFT; // 2MB
constexpr int ROTATION_FILE_COUNT_MAX = 50;

#define BIO_LOG_STD_ERR(msg)      \
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
        BIO_LOG_STD_ERR("Invalid min log level for logger, which should be 0,1,2,3,4,5");
        return false;
    }

    if (options.path.empty()) {
        BIO_LOG_STD_ERR("Invalid path for logger, which is empty");
        return false;
    }

    if (options.rotationFileSizeInMB > ROTATION_FILE_SIZE_MAX_MB ||
        options.rotationFileSizeInMB < ROTATION_FILE_SIZE_MIN_MB) {
        BIO_LOG_STD_ERR("Invalid max file size for logger, which should be between 2MB to 100MB");
        return false;
    }

    if (options.rotationFileCount > ROTATION_FILE_COUNT_MAX || options.rotationFileCount < 1) {
        BIO_LOG_STD_ERR("Invalid max file count for logger, which should be less than 50");
        return false;
    }

    return true;
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
        BIO_LOG_STD_ERR("Failed to new Logger object, probably out of memory");
        return nullptr;
    }

    return gInstance;
}

void Logger::Destroy()
{
    std::lock_guard<std::mutex> guard(gMutex);

    /* un-initialize and delete logger */
    if (gInstance != nullptr) {
        gInstance->Exit();
        delete gInstance;
        gInstance = nullptr;
    }
}

int32_t Logger::ChangeLogLevel(int32_t newLogLevel)
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gInstance == nullptr) {
        BIO_LOG_STD_ERR("Failed to change logger level as it is not created");
        return -1L;
    }

    /* validate level */
    if (!ValidateLogLevel(newLogLevel)) {
        return -1L;
    }

    /* check if level is the same */
    if (gInstance->mOptions.minLogLevel == newLogLevel) {
        return 0;
    }

    /* set level of hlog */
    gInstance->mOptions.minLogLevel = newLogLevel;

    /* set spd log level */
    if (gInstance->mSpdLogger != nullptr) {
        gInstance->mSpdLogger->set_level(static_cast<spdlog::level::level_enum>(newLogLevel));
    }

    return 0;
}

int32_t Logger::Init()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gInited) {
        return 0;
    }
    try {
        std::string logName = std::string("ns:0").append(";log:normal");
        mSpdLogger = spdlog::rotating_logger_mt(logName, mOptions.path, mOptions.rotationFileSizeInMB << SIZE_MB_SHIFT,
            mOptions.rotationFileCount);
        mSpdLogger->set_pattern("%v");
        mSpdLogger->info("", "");
        mSpdLogger->set_pattern("%Y-%m-%d %H:%M:%S.%f %t %v");
        mSpdLogger->info("Log started at [{}] level",
            spdlog::level::to_string_view(static_cast<spdlog::level::level_enum>(mOptions.minLogLevel)).data());
        mSpdLogger->info("Log default format: yyyy-mm-dd hh:mm:ss.uuuuuu threadid loglevel msg");
        mSpdLogger->set_pattern("%Y-%m-%d %H:%M:%S.%f %t %l %v");
        spdlog::flush_every(std::chrono::seconds(1));
        mSpdLogger->set_level(static_cast<spdlog::level::level_enum>(mOptions.minLogLevel));
        mSpdLogger->flush_on(spdlog::level::err);
    } catch (const spdlog::spdlog_ex &ex) {
        mSpdLogger = nullptr;
        BIO_LOG_STD_ERR("Failed to create log: " << ex.what());
        return -1L;
    }
    gInited = true;
    return 0;
}

void Logger::Exit()
{
    if (mSpdLogger != nullptr) {
        mSpdLogger->flush();
        mSpdLogger = nullptr;
    }
}

int32_t Logger::Log(int level, const std::string &message) const
{
    if (mSpdLogger == nullptr) {
        return -2L;
    }

    if (level < 0 || level > 5) { // 5
        return -3L;
    }

    mSpdLogger->log(static_cast<spdlog::level::level_enum>(level), "{}", message);
    return 0L;
}

void Logger::Flush()
{
    if (mSpdLogger != nullptr) {
        mSpdLogger->flush();
    }
}

void Logger::ResetLogLevel(int32_t logLevel)
{
    mOptions.minLogLevel = logLevel;
    if (mSpdLogger != nullptr) {
        mSpdLogger->set_level(static_cast<spdlog::level::level_enum>(mOptions.minLogLevel));
    }
}
}
}
