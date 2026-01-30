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

#ifndef BIO_TRACEPOINT_CMD_H
#define BIO_TRACEPOINT_CMD_H

#include "tracepoint.h"
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define BIO_TRACEP_MAX_ACTIVE_PER_CMD  10
#define BIO_TRACEP_TRACE_PER_PAGE      40
#define INT_MAX ((int)(~0U>>1))
typedef struct
{
    TpUint32  uiOpts;
    TpUint32  uiPid;
    TpChar *szParam;
    TpChar szTracePoint[MAX_NAME_LEN];
    TpChar szHooks[BIO_MAX_HOOK_PER_TRACEP][MAX_NAME_LEN];
    TpInt32  iTracePointFlag;
    TpInt32  aiHookFlag[BIO_MAX_HOOK_PER_TRACEP];
} BIO_TRACEP_CMD_S;

typedef struct
{
    TpUint32 uiCurPid;
    TpUint32 uiNextPid;
    TpUint32 uiShowCount;
    TpInt32 iActive;
    BIO_TRACEP_CMD_S *pstParam;
    BioTracepoints  *pstTrace;
} BIO_TRACEP_WALK_S;

typedef struct
{
    TpUint32 uiShowCount;
    TpInt32 iActive;
    BIO_TRACEP_CMD_S *pstParam;
} BIO_TRACE_HOOK_WALK_S;

typedef struct
{
    uint32_t cmd;
    uint32_t pid;
    int32_t type;
    uint32_t timeAlive;
    BioTracepointParam userParam;
    char traceName[MAX_NAME_LEN];
} BIO_TRACEP_CMD_NEW_S;

void TracePointCmdInit(void);
void TracePointCmdExit(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif // BIO_TRACEPOINT_CMD_H