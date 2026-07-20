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

#include "bdm_common.h"
#include <cstdarg>
#include <cstdio>
#include "bio_log.h"
#include "securec.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void BdmLogFunc(int logLevel, const char *funcName, int line, const char *fileName, const char *format, ...)
{
    va_list argPtr;
    char dataBuf[BDM_LOG_BUF_LEN];
    int ret;

    ret = memset_s(dataBuf, sizeof(dataBuf), 0, sizeof(dataBuf));
    if (ret != 0) {
        return;
    }

    va_start(argPtr, format);
    ret = vsnprintf_s(dataBuf, BDM_LOG_BUF_LEN, sizeof(dataBuf) - 1, format, argPtr);
    if (ret < 0) {
        BIO_LOG_INTERNAL(logLevel, fileName, line, funcName, "vsnprintf_s failed.");
        va_end(argPtr);
        return;
    }
    va_end(argPtr);

    BIO_LOG_INTERNAL(logLevel, fileName, line, funcName, dataBuf);
    return;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
