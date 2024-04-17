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

namespace ock {
namespace bio {
using LogFunc = std::function<void(int32_t level, const char *logBuf)>;

class BioClientLog {
public:
    enum class Level {
        LOG_LEVEL_DEBUG = 0,
        LOG_LEVEL_INFO = 1,
        LOG_LEVEL_WARN = 2,
        LOG_LEVEL_ERROR = 3,
        LOG_LEVEL_BUTT
    };

    BioClientLog() = default;
    ~BioClientLog()
    {
        func = nullptr;
    }

    int32_t Initialize(int32_t mode, int32_t level)
    {
        minLogLevel = level;
        if (mode == 1) {
            LoggerOptions options;
            options.minLogLevel = level;
            options.path = "/var/log/boostio/bio_sdk_" + std::to_string(getpid()) + ".log";
            auto logger = Logger::Instance(options);
            if (logger == nullptr) {
                std::cout << "Failed to create logger instance." << std::endl;
                return -1;
            }
            auto ret = logger->Init();
            if (ret != 0) {
                std::cout << "Failed to init log, ret:" << ret << ", log path:" << options.path << "." << std::endl;
                return -1;
            }
        }
        auto logFunc = [](int level, const char *message) { Logger::gInstance->Log(level + 1, message); };
        func = logFunc;
        return 0;
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

    static BioClientLog *Instance()
    {
        static auto *instance = new BioClientLog();
        return instance;
    }

private:
    LogFunc func = nullptr;
    int32_t minLogLevel = 1;
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

#define CLIENT_LOG_DEBUG(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_DEBUG), args)
#define CLIENT_LOG_INFO(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_INFO), args)
#define CLIENT_LOG_WARN(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_WARN), args)
#define CLIENT_LOG_ERROR(args) BASE_LOG(static_cast<int>(BioClientLog::Level::LOG_LEVEL_ERROR), args)
}
}
#endif