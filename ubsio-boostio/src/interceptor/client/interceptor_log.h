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

#ifndef INTERCEPTOR_LOG_H
#define INTERCEPTOR_LOG_H

#include <cstdio>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <iostream>
#include <sstream>
#include <string>

namespace ock {
namespace bio {
typedef void (*LogFunc)(int32_t level, const char *logBuf);

class InterceptorLog {
public:
    enum class Level {
        LOG_LEVEL_DEBUG = 0,
        LOG_LEVEL_INFO = 1,
        LOG_LEVEL_WARN = 2,
        LOG_LEVEL_ERROR = 3,
        LOG_LEVEL_BUTT
    };

    InterceptorLog() = default;
    ~InterceptorLog()
    {
        func = nullptr;
    }

    void Initialize(Level level)
    {
        minLogLevel = level;
    }

    inline Level GetMinLogLevel() const
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

    static InterceptorLog *Instance()
    {
        static auto *instance = new InterceptorLog();
        return instance;
    }

private:
    LogFunc func = nullptr;
    Level minLogLevel = Level::LOG_LEVEL_ERROR;
};

#ifndef INTERCEPTOR_LOG_FILENAME
#define INTERCEPTOR_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif
#define INTERCEPTOR_LOG(level, args)                                                                                \
    do {                                                                                                            \
        if ((level) >= InterceptorLog::Instance()->GetMinLogLevel()) {                                              \
            std::ostringstream oss;                                                                                 \
            oss << "[INTERCEPTOR " << __FUNCTION__ << ":" << INTERCEPTOR_LOG_FILENAME << ":" << __LINE__ << "] " << \
                args;                                                                                               \
            InterceptorLog::Instance()->Log(level, oss);                                                            \
        }                                                                                                           \
    } while (0)

#define CLOG_DEBUG(args) INTERCEPTOR_LOG(static_cast<int>(InterceptorLog::Level::LOG_LEVEL_DEBUG), args)
#define CLOG_INFO(args) INTERCEPTOR_LOG(static_cast<int>(InterceptorLog::Level::LOG_LEVEL_INFO), args)
#define CLOG_WARN(args) INTERCEPTOR_LOG(static_cast<int>(InterceptorLog::Level::LOG_LEVEL_WARN), args)
#define CLOG_ERROR(args) INTERCEPTOR_LOG(static_cast<int>(InterceptorLog::Level::LOG_LEVEL_ERROR), args)
}
}
#endif