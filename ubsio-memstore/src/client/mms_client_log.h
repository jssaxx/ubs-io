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

#ifndef MMS_CLIENT_LOG_H
#define MMS_CLIENT_LOG_H

#include <cstdio>
#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include "mms_ref.h"
#include "mms_log.h"
#include "mms_types.h"
#include "mms_file_util.h"

namespace ock {
namespace mms {
using LogFunc = std::function<void(int32_t level, const char *logBuf)>;
class MmsClientLog;
using MmsClientLogPtr = Ref<MmsClientLog>;
class MmsClientLog {
public:
    enum class Level {
        LOG_LEVEL_TRACE = 0,
        LOG_LEVEL_DEBUG = 1,
        LOG_LEVEL_INFO = 2,
        LOG_LEVEL_WARN = 3,
        LOG_LEVEL_ERROR = 4,
        LOG_LEVEL_BUTT
    };

    MmsClientLog() = default;
    ~MmsClientLog()
    {
        mFunc = nullptr;
    }

    int32_t Initialize(int32_t level, uint8_t logType, std::string logFilePath)
    {
        mMinLevel = level;
        LoggerOptions options;
        options.logType = (uint8_t)logType;
        options.minLogLevel = level;

        if (options.logType == NO_2) { // file
            auto logDir = logFilePath;
            bool result = FileUtil::CanonicalPath(logDir);
            if (!result) {
                std::cout << "Failed to check log dir." << std::endl;
                return -1;
            }
            options.path = logDir + "/mms_client.log";
        }

        mLogger = Logger::Instance(options);
        if (mLogger == nullptr) {
            std::cout << "Failed to create logger instance." << std::endl;
            return -1;
        }
        int32_t ret = mLogger->Init();
        if (ret != 0) {
            std::cout << "Failed to init log, ret:" << ret << "." << std::endl;
            return -1;
        }
        auto logFunc = [](int level, const char *message) { Logger::gInstance->Log(level, message); };
        mFunc = logFunc;
        return 0;
    }

    void Exit(void)
    {
        Logger::Destroy();
    }

    inline int32_t GetMinLogLevel() const
    {
        return mMinLevel;
    }

    inline void Log(int level, const std::string &message)
    {
        if (mFunc != nullptr) {
            mFunc(level, message.c_str());
        } else {
            struct timeval tv {};
            char strTime[24];
            gettimeofday(&tv, nullptr);
            strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", localtime(&tv.tv_sec));
            std::cout << strTime << tv.tv_usec << " " << level << " " << message << std::endl;
        }
    }

    void ResetLogLevel(int32_t level)
    {
        mMinLevel = level;
        if (mLogger != nullptr) {
            mLogger->ResetLogLevel(level);
        }
    }

    static MmsClientLogPtr Instance()
    {
        static auto instance = MakeRef<MmsClientLog>();
        return instance;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    LogFunc mFunc = nullptr;
    int32_t mMinLevel = 1;
    Logger *mLogger = nullptr;

    DEFINE_REF_COUNT_VARIABLE;
};

#ifndef MMS_CLIENT_LOG_FILENAME
#define MMS_CLIENT_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#ifdef DEBUG_UT
#define BASE_LOG(level, args)
#else
#define BASE_LOG(level, args)                                                                                    \
    do {                                                                                                         \
        if ((level) >= MmsClientLog::Instance()->GetMinLogLevel()) {                                             \
            std::ostringstream oss;                                                                              \
            oss << "[" << MMS_CLIENT_LOG_FILENAME << ":" << __LINE__ << "]"                                      \
                << "[" << __FUNCTION__ << "] " << args;                                                          \
            MmsClientLog::Instance()->Log(level, oss.str());                                                     \
        }                                                                                                        \
    } while (0)
#endif

#define CLIENT_LOG_TRACE(args) BASE_LOG(static_cast<int>(MmsClientLog::Level::LOG_LEVEL_TRACE), args)
#define CLIENT_LOG_DEBUG(args) BASE_LOG(static_cast<int>(MmsClientLog::Level::LOG_LEVEL_DEBUG), args)
#define CLIENT_LOG_INFO(args) BASE_LOG(static_cast<int>(MmsClientLog::Level::LOG_LEVEL_INFO), args)
#define CLIENT_LOG_WARN(args) BASE_LOG(static_cast<int>(MmsClientLog::Level::LOG_LEVEL_WARN), args)
#define CLIENT_LOG_ERROR(args) BASE_LOG(static_cast<int>(MmsClientLog::Level::LOG_LEVEL_ERROR), args)
}
}
#endif
