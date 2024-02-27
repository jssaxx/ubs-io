/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#ifndef HYPERIO_COMMON_TRACEPOINT_COMMON_H
#define HYPERIO_COMMON_TRACEPOINT_COMMON_H

#include <stdint.h>

#include "tracepoint_type.h"

typedef int s32;
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

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

#define EXPORT_SYMBOL(x)
#define module_init(x)
#define module_exit(x)
#define MODULE_AUTHOR(x)
#define MODULE_LICENSE(x)

#define MODULE_ID(x)                static OSP_U32 MY_PID = (x)

#ifndef INVALID_PID
#define INVALID_PID 0
#endif

#ifndef MAX_PID_NUM
#define MAX_PID_NUM 1024
#endif

#define DBG_LogInfo(...)
#define DBG_LogWarning(...)
#define PRINT_LIMIT(...)

u64 dpax_time_getnanosec(void);
u64 dpax_time_getmillisec(void);
s32 dpax_get_charpos(char *pstr, char chr, s32 *sipos);
void dpax_filter_newlinesymbol(char *pstr);
char *dpax_readline(s32 siFd, void *pBuf, s32 siMaxSize);

#define LVOS_GetMilliSecond                         dpax_time_getmillisec
#define LVOS_GetNanoSecond                          dpax_time_getnanosec
#define getCharPos                                  dpax_get_charpos
#define LVOS_getCharPos                             dpax_get_charpos
#define LVOS_filterNewlineSymbol                    dpax_filter_newlinesymbol
#define LVOS_readline(siFd, pBuf, siMaxSize)	    dpax_readline(siFd, pBuf, siMaxSize)

#endif // HYPERIO_COMMON_TRACEPOINT_COMMON_H
