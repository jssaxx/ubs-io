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

#ifndef BIO_TRACEPOINT_COMMON_H
#define BIO_TRACEPOINT_COMMON_H

#include <stdint.h>
#include "tracepoint_type.h"

typedef int TpInt32;
#define RETURN_OK                   (0)
#define RETURN_ERROR                (-1)
#define DP_FAIL                     (-1)

#define FALSE false
#define TRUE true

#ifndef UNREFERENCE_PARAM
#define UNREFERENCE_PARAM(para)     ((void)(para))
#endif

#ifndef ARRAY_LEN
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef LIKELY
#define LIKELY(x) __builtin_expect((x), 1)
#endif

#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect((x), 0)
#endif

#define EXPORT_SYMBOL(x)
#define module_init(x)
#define module_exit(x)
#define MODULE_AUTHOR(x)
#define MODULE_LICENSE(x)

#define MODULE_ID(x)                static TpUint32 MY_PID = (x)

#ifndef INVALID_PID
#define INVALID_PID 0
#endif

#ifndef MAX_PID_NUM
#define MAX_PID_NUM 1024
#endif

#define BIO_LOG_INFO(...)
#define BIO_LOG_WARNING(...)
#define PRINT_LIMIT(...)

TpUint64 DpaxTimeGetnanosec(void);
TpUint64 DpaxTimeGetmillisec(void);
TpInt32 DpaxTimeCharpos(char *pstr, char chr, TpInt32 *sipos);
void DpaxFilterNewlinesymbol(char *pstr);
char *DpaxReadLine(TpInt32 siFd, void *pBuf, TpInt32 siMaxSize);

#define BIO_GET_MILLISECOND                         DpaxTimeGetmillisec
#define BIO_GET_NANOSECOND                          DpaxTimeGetnanosec
#define BIO_GET_CHAR_POS                            DpaxTimeCharpos
#define BIO_FILTER_NEWLINE_SYMBOL                   DpaxFilterNewlinesymbol
#define BIO_READ_LINE(siFd, pBuf, siMaxSize)	    DpaxReadLine(siFd, pBuf, siMaxSize)

#endif // BIO_TRACEPOINT_COMMON_H