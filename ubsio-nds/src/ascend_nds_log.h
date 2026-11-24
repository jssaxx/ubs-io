/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 */

#ifndef ASCEND_NDS_LOG_H
#define ASCEND_NDS_LOG_H

#include <cstdio>
#include <cstdarg>

namespace ascend_nds {

inline void LogErr(const char *fmt, ...) noexcept
{
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[ascend_nds][error] ");
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

inline void LogWarn(const char *fmt, ...) noexcept
{
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[ascend_nds][warn] ");
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

inline void LogInfo(const char *fmt, ...) noexcept
{
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[ascend_nds][info] ");
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

}  // namespace ascend_nds

#endif /* ASCEND_NDS_LOG_H */
