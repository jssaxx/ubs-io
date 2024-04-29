/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#ifndef BIO_TRACEPOINT_HELPER_C_H
#define BIO_TRACEPOINT_HELPER_C_H

#ifdef USE_DEBUG_TOOLS

#ifdef __aarch64__
#include "tracepoint.h"
#endif

#ifndef __aarch64__
#define LVOS_TP_START(name, ...)
#define LVOS_TP_NOPARAM_START(...)
#define LVOS_TP_END
#endif

#else
#define LVOS_TP_START(name, ...)
#define LVOS_TP_NOPARAM_START(...)
#define LVOS_TP_END
#endif

#endif // BIO_TRACEPOINT_HELPER_C_H