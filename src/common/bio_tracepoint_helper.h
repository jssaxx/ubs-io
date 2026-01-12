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
