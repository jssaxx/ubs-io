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

#ifndef MEM_LOG_H
#define MEM_LOG_H

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
using MemLogFunc = void (*)(int32_t level, const char *logBuf);

class MemLog {
public:
   enum class Level {
       LOG_LEVEL_DEBUG = 0,
       LOG_LEVEL_INFO = 1,
       LOG_LEVEL_WARN = 2,
       LOG_LEVEL_ERROR = 3,
       LOG_LEVEL_BUTT
   };

   MemLog() = default;
   ~MemLog()
   {
       func = nullptr;
   }

   inline MemLogFunc GetLogFuncFunc(void)
   {
       return func;
   }

   inline void SetLogFuncFunc(MemLogFunc f)
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

   static MemLog *Instance()
   {
       static MemLog logger;
       return &logger;
   }

private:
   MemLogFunc func = nullptr;
   int32_t minLogLevel = 0;
};

#ifndef MEM_LOG_FILENAME
#define MEM_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#ifdef DEBUG_UT
#define MEM_BASE_LOG(level, args)
#else
#define MEM_BASE_LOG(level, args)                                                                         \
   do {                                                                                                  \
       if (((level) + 1) >= MemLog::Instance()->GetMinLogLevel()) {                                      \
           std::ostringstream oss;                                                                       \
           oss << "[" << MEM_LOG_FILENAME << ":" << __LINE__ << "]"                                      \
               << "[" << __FUNCTION__ << "] " << args;                                                   \
           MemLog::Instance()->Log(level + 1, oss);                                                      \
       }                                                                                                 \
   } while (0)
#endif

#define MEM_LOG_DEBUG(args) MEM_BASE_LOG(static_cast<int>(MemLog::Level::LOG_LEVEL_DEBUG), args)
#define MEM_LOG_INFO(args) MEM_BASE_LOG(static_cast<int>(MemLog::Level::LOG_LEVEL_INFO), args)
#define MEM_LOG_WARN(args) MEM_BASE_LOG(static_cast<int>(MemLog::Level::LOG_LEVEL_WARN), args)
#define MEM_LOG_ERROR(args) MEM_BASE_LOG(static_cast<int>(MemLog::Level::LOG_LEVEL_ERROR), args)
}
}
#endif
