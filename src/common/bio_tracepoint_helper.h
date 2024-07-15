/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#ifndef BIO_TRACEPOINT_HELPER_H
#define BIO_TRACEPOINT_HELPER_H

#ifdef USE_DEBUG_TP_TOOLS
#include "bio_tracepoint.h"
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

#endif // BIO_TRACEPOINT_HELPER_H
