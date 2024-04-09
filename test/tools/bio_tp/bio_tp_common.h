/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#ifndef BIO_TP_COMMON_H
#define BIO_TP_COMMON_H

#ifdef __aarch64__
#include "tracepoint.h"
#else
#define BIO_LVOS_TRACEP_PARAM_SIZE     32UL
typedef struct
{
    char achParamData[BIO_LVOS_TRACEP_PARAM_SIZE]; /**<  自定义参数数据区。 */
} LVOS_TRACEP_PARAM_S;
#endif

namespace ock {
    namespace bio {
        namespace tp {
            class CommonTp {
            public:
                static void Register() noexcept;
                static void Deregister() noexcept;

            public:
                static void IntValueCallback(LVOS_TRACEP_PARAM_S *user, int *value, int set) noexcept;
                static void IntValueErrnoCallback(LVOS_TRACEP_PARAM_S *user, int *value, int set, int err) noexcept;
                static void LongValueCallback(LVOS_TRACEP_PARAM_S *user, long *value, long set) noexcept;
                static void LongValueErrnoCallback(LVOS_TRACEP_PARAM_S *user, long *value, long set, int err) noexcept;
                static void NoProcessCallback(LVOS_TRACEP_PARAM_S *user) noexcept;
            };
        }
    }
}

#endif // BIO_TP_COMMON_H
