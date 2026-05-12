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

#ifndef CACHE_LOG_H
#define CACHE_LOG_H

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
using CacheLogFunc = void (*)(int32_t level, const char *logBuf);

class CacheLog {
public:
    enum class Level {
        LOG_LEVEL_DEBUG = 0,
        LOG_LEVEL_INFO = 1,
        LOG_LEVEL_WARN = 2,
        LOG_LEVEL_ERROR = 3,
        LOG_LEVEL_BUTT
    };

    CacheLog() = default;
    ~CacheLog()
    {
        func = nullptr;
    }

    inline CacheLogFunc GetLogFuncFunc(void)
    {
        return func;
    }

    inline void SetLogFuncFunc(CacheLogFunc f)
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

    static CacheLog *Instance()
    {
        static CacheLog logger;
        return &logger;
    }

private:
    CacheLogFunc func = nullptr;
    int32_t minLogLevel = 0;
};

#ifndef CACHE_LOG_FILENAME
#define CACHE_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#ifdef DEBUG_UT
#define CACHE_BASE_LOG(level, args)
#else
#define CACHE_BASE_LOG(level, args)                                                                         \
    do {                                                                                                  \
        if (((level) + 1) >= CacheLog::Instance()->GetMinLogLevel()) {                                      \
            std::ostringstream oss;                                                                       \
            oss << "[" << CACHE_LOG_FILENAME << ":" << __LINE__ << "]"                                      \
                << "[" << __FUNCTION__ << "] " << args;                                                   \
            CacheLog::Instance()->Log(level + 1, oss);                                                      \
        }                                                                                                 \
    } while (0)
#endif

#define CACHE_LOG_DEBUG(args) CACHE_BASE_LOG(static_cast<int>(CacheLog::Level::LOG_LEVEL_DEBUG), args)
#define CACHE_LOG_INFO(args) CACHE_BASE_LOG(static_cast<int>(CacheLog::Level::LOG_LEVEL_INFO), args)
#define CACHE_LOG_WARN(args) CACHE_BASE_LOG(static_cast<int>(CacheLog::Level::LOG_LEVEL_WARN), args)
#define CACHE_LOG_ERROR(args) CACHE_BASE_LOG(static_cast<int>(CacheLog::Level::LOG_LEVEL_ERROR), args)

#define CheckTrue(ARGS, RET, MSG)                                        \
    do {                                                               \
        if (__builtin_expect(!(ARGS), 0) != 0) {                         \
            CACHE_LOG_ERROR("Check Failed: " << #ARGS << ", " << MSG); \
            return RET;                                                  \
        }                                                              \
    } while (0)
}
}
#endif
