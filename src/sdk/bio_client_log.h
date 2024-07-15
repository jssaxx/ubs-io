/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2023. All rights reserved.
 */

#ifndef BIO_CLIENT_LOG_H
#define BIO_CLIENT_LOG_H

#include <cstdio>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include "bio_log.h"
#include "bio_tracepoint_helper.h"
#include "bio_file_util.h"

namespace ock {
namespace bio {
using LogFunc = std::function<void(int32_t level, const char *logBuf)>;

class BioClientLog {
public:
    enum class Level {
        LOG_LEVEL_TRACE = 0,
        LOG_LEVEL_DEBUG = 1,
        LOG_LEVEL_INFO = 2,
        LOG_LEVEL_WARN = 3,
        LOG_LEVEL_ERROR = 4,
        LOG_LEVEL_BUTT
    };

    BioClientLog() = default;
    ~BioClientLog()
    {
        func = nullptr;
    }

    int32_t Initialize(int32_t mode, int32_t level, uint8_t logType, std::string logFilePath)
    {
        minLogLevel = level;
        mMode = mode;
        if (mode == 1) {
            LoggerOptions options;
            options.logType = (uint8_t)logType;
            options.minLogLevel = level;

            if (options.logType == 1) { // file
                auto logDir = logFilePath;
                bool result = FileUtil::CanonicalPath(logDir);
                if (!result) {
                    std::cout << "Failed to check log dir." << std::endl;
                    return -1;
                }
                options.path = logDir + "/bio_sdk_" + std::to_string(getpid()) + ".log";
            }

            LVOS_TP_START(SDK_BIO_LOG_CREAT_FAIL, &mLogger, nullptr);
            mLogger = Logger::Instance(options);
            LVOS_TP_END;
            if (mLogger == nullptr) {
                std::cout << "Failed to create logger instance." << std::endl;
                return -1;
            }
            int32_t ret = -1;
            LVOS_TP_START(SDK_BIO_LOG_INIT_FAIL, &ret, -1);
            ret = mLogger->Init();
            LVOS_TP_END;
            if (ret != 0) {
                std::cout << "Failed to init log, ret:" << ret << "." << std::endl;
                return -1;
            }
        }
        auto logFunc = [](int level, const char *message) { Logger::gInstance->Log(level, message); };
        func = logFunc;
        return 0;
    }

    void Exit(int32_t mode)
    {
        if (mode == 1) {
            Logger::Destroy();
        }
    }

    inline int32_t GetMinLogLevel() const
    {
        return minLogLevel;
    }

    inline void Log(int level, const std::ostringstream &oss)
    {
        if (func != nullptr) {
            func(level, oss.str().c_str());
        } else {
            struct timeval tv {};
            char strTime[24];
            gettimeofday(&tv, nullptr);
            strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", localtime(&tv.tv_sec));
            std::cout << strTime << tv.tv_usec << " " << level << " " << oss.str() << std::endl;
        }
    }

    void ResetLogLevel(int32_t level)
    {
        minLogLevel = level;
        if (mMode == 1) {
            if (mLogger != nullptr) {
                mLogger->ResetLogLevel(level);
            }
        }
    }

    static BioClientLog *Instance()
    {
        static auto *instance = new BioClientLog();
        return instance;
    }

private:
    LogFunc func = nullptr;
    int32_t minLogLevel = 1;
    int32_t mMode = 0;
    Logger *mLogger = nullptr;
};

#ifndef BIO_CLIENT_LOG_FILENAME
#define BIO_CLIENT_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif
#define BASE_LOG(level, args)                                                                                    \
    do {                                                                                                         \
        if ((level) >= BioClientLog::Instance()->GetMinLogLevel()) {                                             \
            std::ostringstream oss;                                                                              \
            oss << "[SDK " << __FUNCTION__ << ":" << BIO_CLIENT_LOG_FILENAME << ":" << __LINE__ << "] " << args; \
            BioClientLog::Instance()->Log(level, oss);                                                           \
        }                                                                                                        \
    } while (0)

#define CLIENT_LOG_TRACE(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_TRACE), args)
#define CLIENT_LOG_DEBUG(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_DEBUG), args)
#define CLIENT_LOG_INFO(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_INFO), args)
#define CLIENT_LOG_WARN(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_WARN), args)
#define CLIENT_LOG_ERROR(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_ERROR), args)
}
}
#endif