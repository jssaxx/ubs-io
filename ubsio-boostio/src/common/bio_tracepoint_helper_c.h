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

#ifndef BIO_TRACEPOINT_HELPER_C_H
#define BIO_TRACEPOINT_HELPER_C_H

#ifdef USE_DEBUG_TP_TOOLS

#ifdef __aarch64__
#include "tracepoint.h"
#endif

#ifndef __aarch64__
#define BIO_TP_START(name, ...)
#define BIO_TP_NOPARAM_START(...)
#define BIO_TP_END
#endif

#else
#define BIO_TP_START(name, ...)
#define BIO_TP_NOPARAM_START(...)
#define BIO_TP_END
#endif

#endif // BIO_TRACEPOINT_HELPER_C_H