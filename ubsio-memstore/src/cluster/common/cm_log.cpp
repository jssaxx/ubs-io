/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <cstdarg>
#include <cstdio>
#include "mms_log.h"
#include "cm_log.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void CmLogFunc(int logLevel, const char *funcName, int line, const char *fileName, const char *format, ...)
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
    va_end(argPtr);
    if (ret < 0) {
        MMS_LOG_INTERNAL(logLevel, fileName, line, funcName, "vsnprintf_s failed.");
        return;
    }

    MMS_LOG_INTERNAL(logLevel, fileName, line, funcName, dataBuf);
    return;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
