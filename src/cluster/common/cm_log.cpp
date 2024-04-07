/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include <cstdarg>
#include <cstdio>
#include "bio_log.h"
#include "cm_log.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void CM_Ulog(int logLevel, const char *funcName, int line, const char *fileName, const char *format, ...)
{
    va_list argPtr;
    char dataBuf[CM_LOG_BUF_LEN];
    int ret;

    ret = memset_s(dataBuf, sizeof(dataBuf), 0, sizeof(dataBuf));
    if (ret != 0) {
        return;
    }

    va_start(argPtr, format);
    ret = vsnprintf_s(dataBuf, CM_LOG_BUF_LEN, sizeof(dataBuf) - 1, format, argPtr);
    if (ret < 0) {
        BIO_LOG_INTERNAL(logLevel, fileName, line, funcName, "vsnprintf_s failed.");
        return;
    }
    va_end(argPtr);

    BIO_LOG_INTERNAL(logLevel, fileName, line, funcName, dataBuf);
    return;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
