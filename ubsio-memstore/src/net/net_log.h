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

#ifndef NET_LOG_H
#define NET_LOG_H

#include <cstdio>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <iostream>
#include <sstream>
#include <string>

namespace ock {
namespace mms {
using NetLogFunc = void (*)(int32_t level, const char *logBuf);

class NetLog {
public:
    enum class Level {
        LOG_LEVEL_DEBUG = 0,
        LOG_LEVEL_INFO = 1,
        LOG_LEVEL_WARN = 2,
        LOG_LEVEL_ERROR = 3,
        LOG_LEVEL_BUTT
    };

    NetLog() = default;
    ~NetLog()
    {
        func = nullptr;
    }

    inline NetLogFunc GetLogFuncFunc(void)
    {
        return func;
    }

    inline void SetLogFuncFunc(NetLogFunc f)
    {
        func = f;
    }

    inline void SetMinLogLevel(int32_t level)
    {
        minLogLevel = level;
    }

    inline int32_t GetMinLogLevel(void)
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

    static NetLog *Instance()
    {
        static NetLog logger;
        return &logger;
    }

private:
    NetLogFunc func = nullptr;
    int32_t minLogLevel = 0;
};

#ifndef NET_LOG_FILENAME
#define NET_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#ifdef DEBUG_UT
#define NET_BASE_LOG(level, args)
#else
#define NET_BASE_LOG(level, args)                                                                         \
    do {                                                                                                  \
        if (((level) + 1) >= NetLog::Instance()->GetMinLogLevel()) {                                      \
            std::ostringstream oss;                                                                       \
            oss << "[" << NET_LOG_FILENAME << ":" << __LINE__ << "]"                                      \
                << "[" << __FUNCTION__ << "] " << args;                                                   \
            NetLog::Instance()->Log(level + 1, oss);                                                      \
        }                                                                                                 \
    } while (0)
#endif

#define NET_LOG_DEBUG(args) NET_BASE_LOG(static_cast<int>(NetLog::Level::LOG_LEVEL_DEBUG), args)
#define NET_LOG_INFO(args) NET_BASE_LOG(static_cast<int>(NetLog::Level::LOG_LEVEL_INFO), args)
#define NET_LOG_WARN(args) NET_BASE_LOG(static_cast<int>(NetLog::Level::LOG_LEVEL_WARN), args)
#define NET_LOG_ERROR(args) NET_BASE_LOG(static_cast<int>(NetLog::Level::LOG_LEVEL_ERROR), args)
}
}
#endif
