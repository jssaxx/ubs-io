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

#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>

#include "cli.h"
#include "dpax_list.h"
#include "tracepoint_common.h"
#include "tracepoint.h"
#include "tracepoint_pub.h"
#include "tracepoint_cmd.h"

#define BIO_TRACEP_MAX_ACTIVE_PER_CMD  10

#define BIO_TRACEP_TRACE_PER_PAGE      40
#define SHOWTP_COUNT_NEED_FLUSH         80
#define BIO_TRACEP_CONDITION_FOR_RESET_PID (60 * 1000)
#define BIO_TRACEP_PARAM_BUFF_OFFSET   3

MODULE_ID(PID_DEBUG);

static TpUint32 g_uiLastPid = 0;
static TpUint64 g_ullLastShowTime = 0;
static pthread_mutex_t g_initLock = PTHREAD_MUTEX_INITIALIZER;/*lint !e708*/
extern BioHtS *g_TPHt;

static void BIO_GetoptInit(void)
{
#ifndef WIN32
    optind = 0;
#endif
}

static TpInt32 BIO_GetShowParam(TpInt32 v_iArgc, TpChar *v_szArgv[], BIO_TRACEP_CMD_S *v_pstParam)
{
    TpInt32 iRet = 0;

    CLI_OPT_S stOpt = {0};

    memset(v_pstParam, 0, sizeof(BIO_TRACEP_CMD_S));

    BIO_GetoptInit();
    while ((iRet = CLI_GetOpt(v_iArgc, v_szArgv, "p:m:n", &stOpt)) != -1) {
        if ('n' == iRet) {
            CLI_SetOpt(&v_pstParam->uiOpts, 'n');
        } else if ('m' == iRet) {
            if (CLI_TestOpt(v_pstParam->uiOpts, 'm')) {
                CLI_Print("Error %s: too many option -- m\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            CLI_SetOpt(&v_pstParam->uiOpts, 'm');
            v_pstParam->uiPid = (TpUint32) atoll(stOpt.szOptArg);
            if ((v_pstParam->uiPid == 0) || (v_pstParam->uiPid >= MAX_PID_NUM)) {
                CLI_Print("Error %s: invalid pid -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }
        } else if ('p' == iRet) {
            if (CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
                CLI_Print("Error %s: too many option -- p\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            CLI_SetOpt(&v_pstParam->uiOpts, 'p');
            v_pstParam->szParam = stOpt.szOptArg;
        } else if (CLI_RET_UNKNOWN_ARG == iRet) {
            CLI_ShowUsageAndErrMsg("Error %s: invalid argument -- %s", v_szArgv[0], stOpt.szOptArg);
            return RETURN_ERROR;
        } else {
            CLI_ShowUsageAndErrMsg("%s", stOpt.szErrMsg);
            return RETURN_ERROR;
        }
    }

    return RETURN_OK;
}

#define TRACP_NAME_END(ch)  ((ch) == '\0' || (ch) == ':')

static TpInt32 BIO_PraseHooksLine(TpChar *v_szCommand, TpChar *v_szHookStr, BIO_TRACEP_CMD_S *v_pstParam)
{
    TpUint32 uiIndex, uiIndex2;
    TpChar *pchCur = v_szHookStr;

    uiIndex2 = 0;

    while ((*pchCur != '\0') && (uiIndex2 < BIO_MAX_HOOK_PER_TRACEP)) {
        for (uiIndex = 0;
             !TRACP_NAME_END(pchCur[uiIndex]) && (uiIndex < ARRAY_LEN(v_pstParam->szHooks[uiIndex2]) - 1);
             uiIndex++) {
            v_pstParam->szHooks[uiIndex2][uiIndex] = pchCur[uiIndex];
        }

        if (0 == strcmp(v_pstParam->szHooks[uiIndex2], "*")) {
            strcpy(v_pstParam->szHooks[0], "*");
            v_pstParam->szHooks[1][0] = '\0';
            return RETURN_OK;
        }

        if (pchCur[uiIndex] == '\0') {
            return RETURN_OK;
        }

        if (pchCur[uiIndex] != ':') {
            CLI_Print("Error %s: hook too long in argument -- %s\n", v_szCommand, v_pstParam->szParam);
            return RETURN_ERROR;
        }

        pchCur += (uiIndex + 1);
        uiIndex2++;

        while ((*pchCur == ':') && (*pchCur != '\0')) {
            pchCur++;
        }
    }

    if (uiIndex2 >= BIO_MAX_HOOK_PER_TRACEP) {
        CLI_Print("Error %s: too many hooks in argument -- %s\n", v_szCommand, v_pstParam->szParam);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

static TpInt32 BIO_PraseOneTracePointLine(TpChar *v_szCommand, BIO_TRACEP_CMD_S *v_pstParam)
{
    TpUint32 uiIndex;
    TpChar *pchCur = v_pstParam->szParam;

    for (uiIndex = 0;
         !TRACP_NAME_END(pchCur[uiIndex]) && (uiIndex < ARRAY_LEN(v_pstParam->szTracePoint) - 1);
         uiIndex++) {
        v_pstParam->szTracePoint[uiIndex] = pchCur[uiIndex];
    }

    if (pchCur[uiIndex] == '\0') {
        return RETURN_OK;
    }

    if (pchCur[uiIndex] == '*') {
        v_pstParam->szTracePoint[uiIndex] = pchCur[uiIndex];
        uiIndex++;
    }

    if ((pchCur[uiIndex] != ':') && (pchCur[uiIndex] != '\0')) {
        CLI_Print("Error %s: tracepoint too long in argument -- %s\n", v_szCommand, v_pstParam->szParam);
        return RETURN_ERROR;
    }

    pchCur += (uiIndex + 1);
    return BIO_PraseHooksLine(v_szCommand, pchCur, v_pstParam);
}

static TpInt32 BIO_PraseShowParam(TpChar *v_szCommand, BIO_TRACEP_CMD_S *v_pstParam)
{
    TpInt32 iRet;

    if (CLI_TestOpt(v_pstParam->uiOpts, 'n')) {
        if (CLI_TestOpt(v_pstParam->uiOpts, 'm') || CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
            CLI_Print("Error %s: must be used alone option -- n\n", v_szCommand);
            return RETURN_ERROR;
        }
    }

    if (!CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
        return RETURN_OK;
    }

    iRet = BIO_PraseOneTracePointLine(v_szCommand, v_pstParam);
    if (RETURN_OK != iRet) {
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

static void BIO_ShowOneTracePointList(TpInt32 v_iShowHeader, BioTracepoints *v_pstTrace)
{
    if (v_iShowHeader != FALSE) {
        CLI_Print("<PID>  <State>  <TracePoint>        <Description>\n");
    }

    CLI_Print("%5u  %-8s %c%-18s %s\n", v_pstTrace->uiPid,
              v_pstTrace->iActive == BIO_TRACEP_STAT_ACTIVE ? "active" : "deactive",
              v_pstTrace->iDbgOnly ? '*' : ' ', v_pstTrace->szName, v_pstTrace->szDesc);
}

static TpInt32 BIO_ShowPidTraceListProc(BioTracepoints *v_pstTrace, void *v_pParam)
{
    BIO_TRACEP_WALK_S *pstWalk = v_pParam;

    if (v_pstTrace->uiPid == pstWalk->uiCurPid) {
        if (pstWalk->uiShowCount == 0) {
            BIO_ShowOneTracePointList(TRUE, v_pstTrace);
        } else {
            BIO_ShowOneTracePointList(FALSE, v_pstTrace);
        }
        pstWalk->uiShowCount++;
    }

    return RETURN_OK;
}

static TpInt32 BIO_ShowNextProc(BioTracepoints *v_pstTrace, void *v_pParam)
{
    BIO_TRACEP_WALK_S *pstWalk = v_pParam;

    (void) BIO_ShowPidTraceListProc(v_pstTrace, v_pParam);

    if (v_pstTrace->uiPid > pstWalk->uiCurPid) {
        if (pstWalk->uiNextPid == pstWalk->uiCurPid) {
            pstWalk->uiNextPid = v_pstTrace->uiPid;
        } else if (v_pstTrace->uiPid < pstWalk->uiNextPid) {
            pstWalk->uiNextPid = v_pstTrace->uiPid;
        }
    }

    return RETURN_OK;
}

static void BIO_ShowNextPage(BIO_TRACEP_CMD_S *v_pstParam)
{
    BIO_TRACEP_WALK_S stWalk;

    if ((BIO_GET_MILLISECOND() - g_ullLastShowTime) > (BIO_TRACEP_CONDITION_FOR_RESET_PID)) {
        g_uiLastPid = 0;
    }

    g_ullLastShowTime = BIO_GET_MILLISECOND();

    stWalk.uiCurPid = g_uiLastPid;
    stWalk.uiNextPid = stWalk.uiCurPid;
    stWalk.uiShowCount = 0;
    stWalk.pstParam = v_pstParam;

    BioWalkTracePoint(BIO_ShowNextProc, &stWalk);

    while ((stWalk.uiNextPid > stWalk.uiCurPid)
           && (stWalk.uiShowCount < BIO_TRACEP_TRACE_PER_PAGE)) {
        stWalk.uiCurPid = stWalk.uiNextPid;
        BioWalkTracePoint(BIO_ShowNextProc, &stWalk);
    }

    g_uiLastPid = stWalk.uiCurPid + 1;
    if (stWalk.uiNextPid != stWalk.uiCurPid) {
        g_uiLastPid = stWalk.uiNextPid;
        CLI_Print("Type `showtpold -n` to show next page.\n");
    } else if (stWalk.uiShowCount == 0) {
        CLI_Print("Error showtpold: No more tracepoint, `showtp` to show the first page.\n");
    } else {
        CLI_Print("<END>\n");
    }
}

static void BIO_ShowPidTraceList(BIO_TRACEP_CMD_S *v_pstParam)
{
    BIO_TRACEP_WALK_S stWalk;

    stWalk.uiCurPid = v_pstParam->uiPid;
    stWalk.uiShowCount = 0;
    stWalk.pstParam = v_pstParam;

    BioWalkTracePoint(BIO_ShowPidTraceListProc, &stWalk);

    if (0 == stWalk.uiShowCount) {
        CLI_Print("Error showtpold: No tracepoint for module <%d>\n", stWalk.uiCurPid);
    }
}

static TpInt32 BIO_ShowTraceMatchProc(BioTracepoints *v_pstTrace, void *v_pParam)
{
    BIO_TRACEP_WALK_S *pstWalk = v_pParam;

    if ((0 == strncmp(v_pstTrace->szName, pstWalk->pstParam->szTracePoint, (size_t) pstWalk->uiNextPid))
        && ((0 == pstWalk->uiCurPid) || (pstWalk->uiCurPid == v_pstTrace->uiPid))) {
        if (pstWalk->uiShowCount == 0) {
            BIO_ShowOneTracePointList(TRUE, v_pstTrace);
        } else {
            BIO_ShowOneTracePointList(FALSE, v_pstTrace);
        }
        pstWalk->uiShowCount++;
    }

    return RETURN_OK;
}

static void BIO_ShowTraceMatch(BIO_TRACEP_CMD_S *v_pstParam)
{
    BIO_TRACEP_WALK_S stWalk;

    stWalk.uiCurPid = v_pstParam->uiPid;
    stWalk.pstParam = v_pstParam;
    stWalk.uiShowCount = 0;
    stWalk.uiNextPid = (TpUint32) strlen(v_pstParam->szTracePoint);

    BioWalkTracePoint(BIO_ShowTraceMatchProc, &stWalk);

    if (0 == stWalk.uiShowCount) {
        if (0 == stWalk.uiCurPid) {
            CLI_Print("Error showtpold: No tracepoint match `%s`\n", v_pstParam->szTracePoint);
        } else {
            CLI_Print("Error showtpold: No tracepoint match `%s`, module <%d>\n",
                      v_pstParam->szTracePoint, stWalk.uiCurPid);
        }
    }
}

static TpInt32 BIO_FindOneTraceProc(BioTracepoints *v_pstTrace, void *v_pParam)
{
    BIO_TRACEP_WALK_S *pstWalk = v_pParam;

    if ((0 == strcmp(v_pstTrace->szName, pstWalk->pstParam->szTracePoint))
        && ((0 == pstWalk->uiCurPid) || (pstWalk->uiCurPid == v_pstTrace->uiPid))) {
        pstWalk->pstTrace = v_pstTrace;
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

static TpInt32 BIO_ShowHooksProc(BioTracepointHook *v_pstHook, void *v_pvParam)
{
    BIO_TRACE_HOOK_WALK_S *pstWalkHook = v_pvParam;

    if (0 == pstWalkHook->uiShowCount) {
        CLI_Print("Hooks      :\n    <State>  <Hook>            <Description>\n");
    }

    CLI_Print("    %-8s %c%-16s %s\n", v_pstHook->iActive == BIO_TRACEP_STAT_ACTIVE ? "active" : "deactive",
              v_pstHook->iDbgOnly ? '*' : ' ', v_pstHook->szName, v_pstHook->szDesc);

    pstWalkHook->uiShowCount++;

    return RETURN_OK;
}

static void BIO_ShowOneTrace(BIO_TRACEP_CMD_S *v_pstParam)
{
    BIO_TRACEP_WALK_S stWalk;
    BIO_TRACE_HOOK_WALK_S stWalkHook;
    TpChar szParamBuf[BIO_TRACEP_PARAM_SIZE * BIO_TRACEP_PARAM_BUFF_OFFSET + 1];
    TpUint32 uiIndex;

    stWalk.uiCurPid = v_pstParam->uiPid;
    stWalk.pstTrace = NULL;
    stWalk.pstParam = v_pstParam;

    BioWalkTracePoint(BIO_FindOneTraceProc, &stWalk);

    if (NULL == stWalk.pstTrace) {
        CLI_Print("Error showtpold: tracepoint `%s` not found.\n", v_pstParam->szTracePoint);
        return;
    }

    if (FALSE != stWalk.pstTrace->iDbgOnly) {
        CLI_Print("Name       : *%s\n", stWalk.pstTrace->szName);
    } else {
        CLI_Print("Name       : %s\n", stWalk.pstTrace->szName);
    }
    CLI_Print("State      : %s\n", stWalk.pstTrace->iActive == BIO_TRACEP_STAT_ACTIVE ? "active" : "deactive");
    CLI_Print("Module     : <%d>\n", stWalk.pstTrace->uiPid);
    CLI_Print("Desctiption: %s\n", stWalk.pstTrace->szDesc);
    for (uiIndex = 0; uiIndex < BIO_TRACEP_PARAM_SIZE; uiIndex++) {
        snprintf(&szParamBuf[uiIndex * 3], 4UL, "%02x ",
                   (unsigned char) stWalk.pstTrace->stParam.tpParamData[uiIndex]);
    }
    CLI_Print("Param Value: %s(HEX)\n", szParamBuf);

    stWalkHook.uiShowCount = 0;
    BioWalkTraceHook(stWalk.pstTrace, BIO_ShowHooksProc, &stWalkHook);

    if (0 == stWalkHook.uiShowCount) {
        CLI_Print("Hooks      : <None>\n");
    }
}

static void BIO_ShowSpecialTrace(BIO_TRACEP_CMD_S *v_pstParam)
{
    TpUint32 uiLen;

    uiLen = (TpUint32) strlen(v_pstParam->szTracePoint);

    if ((('*' == v_pstParam->szTracePoint[sizeof(v_pstParam->szTracePoint) - 1])
         || ('*' == v_pstParam->szTracePoint[uiLen - 1]))) {
        v_pstParam->szTracePoint[sizeof(v_pstParam->szTracePoint) - 1] = '\0';
        if (uiLen < sizeof(v_pstParam->szTracePoint)) {
            v_pstParam->szTracePoint[uiLen - 1] = '\0';
        }

        BIO_ShowTraceMatch(v_pstParam);
        return;
    }

    BIO_ShowOneTrace(v_pstParam);
}

static void BIO_ExeShowTrace(BIO_TRACEP_CMD_S *v_pstParam)
{
    if (0 == v_pstParam->uiOpts) {
        g_uiLastPid = 0;
        BIO_ShowNextPage(v_pstParam);
        return;
    }

    if (CLI_TestOpt(v_pstParam->uiOpts, 'n')) {
        BIO_ShowNextPage(v_pstParam);
        return;
    }

    if (CLI_TestOpt(v_pstParam->uiOpts, 'm') && !CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
        BIO_ShowPidTraceList(v_pstParam);
        return;
    }

    BIO_ShowSpecialTrace(v_pstParam);

    return;
}

static void BIO_DoShowTracePointCmd(TpInt32 v_iArgc, TpChar *v_szArgv[])
{
    TpInt32 iRet;
    BIO_TRACEP_CMD_S *pstParam = NULL;

    pstParam = malloc((TpUint32) sizeof(BIO_TRACEP_CMD_S));
    if (NULL == pstParam) {
        CLI_Print("Error %s: Out of memory\n", v_szArgv[0]);
        return;
    }

    iRet = BIO_GetShowParam(v_iArgc, v_szArgv, pstParam);
    if (RETURN_OK != iRet) {
        free(pstParam);
        return;
    }

    iRet = BIO_PraseShowParam(v_szArgv[0], pstParam);
    if (RETURN_OK != iRet) {
        free(pstParam);
        return;
    }

    BIO_ExeShowTrace(pstParam);

    free(pstParam);
    return;
}

static TpInt32 BIO_GetActiveParam(TpInt32 v_iArgc, TpChar *v_szArgv[], BIO_TRACEP_CMD_S *v_pstParam)
{
    TpInt32 iRet = 0;

    CLI_OPT_S stOpt = {0};

    TpUint32 uiIndex = 0;

    memset(v_pstParam, 0, sizeof(BIO_TRACEP_CMD_S) * BIO_TRACEP_MAX_ACTIVE_PER_CMD);

    BIO_GetoptInit();
    while ((iRet = CLI_GetOpt(v_iArgc, v_szArgv, "p:m:", &stOpt)) != -1) {
        if ('m' == iRet) {
            if (CLI_TestOpt(v_pstParam->uiOpts, 'm')) {
                CLI_Print("Error %s: too many option -- m\n", v_szArgv[0]);
                return RETURN_ERROR;
            }
            CLI_SetOpt(&v_pstParam->uiOpts, 'm');
            v_pstParam->uiPid = (TpUint32) atoll(stOpt.szOptArg);
            if ((v_pstParam->uiPid == 0) || (v_pstParam->uiPid >= MAX_PID_NUM)) {
                CLI_Print("Error %s: invalid pid -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }
        } else if ('p' == iRet) {
            CLI_SetOpt(&v_pstParam->uiOpts, 'p');
            if (uiIndex < BIO_TRACEP_MAX_ACTIVE_PER_CMD) {
                v_pstParam[uiIndex].szParam = stOpt.szOptArg;
                uiIndex++;
            } else {
                CLI_Print("Error %s: too many option -- p\n", v_szArgv[0]);
            }
        } else if (CLI_RET_UNKNOWN_ARG == iRet) {
            CLI_ShowUsageAndErrMsg("Error %s: invalid argument -- %s", v_szArgv[0], stOpt.szOptArg);
            return RETURN_ERROR;
        } else {
            CLI_ShowUsageAndErrMsg("%s", stOpt.szErrMsg);
            return RETURN_ERROR;
        }
    }

    return RETURN_OK;
}

static TpInt32 BIO_PraseActiveParam(TpChar *v_szCommand, BIO_TRACEP_CMD_S *v_pstParam)
{
    TpInt32 iRet;
    TpInt32 uiIndex;

    if (!CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
        CLI_Print("Error %s: missing required option -- p\n", v_szCommand);
        return RETURN_ERROR;
    }

    for (uiIndex = 0; uiIndex < BIO_TRACEP_MAX_ACTIVE_PER_CMD; uiIndex++) {
        if (NULL == v_pstParam[uiIndex].szParam) {
            return RETURN_OK;
        }

        iRet = BIO_PraseOneTracePointLine(v_szCommand, v_pstParam + uiIndex);
        if (RETURN_OK != iRet) {
            return RETURN_ERROR;
        }
    }

    return RETURN_OK;
}

static TpInt32 BIO_ActiveHooksProc(BioTracepointHook *v_pstHook, void *v_pvParam)
{
    BIO_TRACE_HOOK_WALK_S *pstWalk = v_pvParam;
    TpUint32 uiIndex;

    if (0 == strcmp(pstWalk->pstParam->szHooks[0], "*")) {
        v_pstHook->iActive = pstWalk->iActive;
        pstWalk->pstParam->aiHookFlag[0] = TRUE;
        return RETURN_OK;
    }

    for (uiIndex = 0; uiIndex < BIO_MAX_HOOK_PER_TRACEP && pstWalk->pstParam->szHooks[uiIndex][0] != '\0'; uiIndex++) {
        if (0 == strcmp(pstWalk->pstParam->szHooks[uiIndex], v_pstHook->szName)) {
            v_pstHook->iActive = pstWalk->iActive;
            pstWalk->pstParam->aiHookFlag[uiIndex] = TRUE;
            return RETURN_OK;
        }
    }

    return RETURN_OK;
}

static TpInt32 BIO_ActiveTraceProc(BioTracepoints *v_pstTrace, void *v_pParam)
{
    TpUint32 uiIndex;
    BIO_TRACEP_WALK_S *pstWalk = v_pParam;
    BIO_TRACE_HOOK_WALK_S stHookWalk;

    if ((0 != pstWalk->uiCurPid) && (v_pstTrace->uiPid != pstWalk->uiCurPid)) {
        return RETURN_OK;
    }

    for (uiIndex = 0; uiIndex < BIO_TRACEP_MAX_ACTIVE_PER_CMD; uiIndex++) {
        if ('\0' == pstWalk->pstParam[uiIndex].szTracePoint[0]) {
            return RETURN_OK;
        }

        if (0 != strcmp(pstWalk->pstParam[uiIndex].szTracePoint, v_pstTrace->szName)) {
            continue;
        }

        if ('\0' == pstWalk->pstParam[uiIndex].szHooks[0][0]) {
            v_pstTrace->iActive = pstWalk->iActive;
            pstWalk->pstParam[uiIndex].iTracePointFlag = TRUE;
            continue;
        }

        stHookWalk.iActive = pstWalk->iActive;
        stHookWalk.pstParam = &pstWalk->pstParam[uiIndex];
        BioWalkTraceHook(v_pstTrace, BIO_ActiveHooksProc, &stHookWalk);
    }

    return RETURN_OK;
}

static void BIO_ShowActiveHookResult(BIO_TRACEP_CMD_S *v_pstParam, TpInt32 v_iFlag)
{
    TpUint32 uiCount, uiHookCount;
    TpUint32 uiIndex, uiHookIndex;

    uiCount = 0;
    for (uiIndex = 0; uiIndex < BIO_TRACEP_MAX_ACTIVE_PER_CMD; uiIndex++) {
        if ('\0' == v_pstParam[uiIndex].szTracePoint[0]) {
            break;
        }

        if ('\0' == v_pstParam[uiIndex].szHooks[0][0]) {
            continue;
        }

        uiHookCount = 0;

        for (uiHookIndex = 0; uiHookIndex < ARRAY_LEN(v_pstParam[uiIndex].szHooks); uiHookIndex++) {
            if ('\0' == v_pstParam[uiIndex].szHooks[uiHookIndex][0]) {
                break;
            }

            if (v_pstParam[uiIndex].aiHookFlag[uiHookIndex] != v_iFlag) {
                continue;
            }

            if (uiHookCount > 0) {
                CLI_Print(":%s", v_pstParam[uiIndex].szHooks[uiHookIndex]);
            } else if (uiCount > 0) {
                CLI_Print("                 %s:%s", v_pstParam[uiIndex].szTracePoint,
                          v_pstParam[uiIndex].szHooks[uiHookIndex]);
            } else {
                CLI_Print("    Hook       : %s:%s", v_pstParam[uiIndex].szTracePoint,
                          v_pstParam[uiIndex].szHooks[uiHookIndex]);
            }

            uiHookCount++;
        }

        if (uiHookCount > 0) {
            CLI_Print("\n");
            uiCount++;
        }
    }

    if (0 == uiCount) {
        CLI_Print("    Hook       : <None>\n");
    }
}

static void BIO_ShowActiveTraceResult(BIO_TRACEP_CMD_S *v_pstParam, TpInt32 v_iFlag)
{
    TpUint32 uiCount;
    TpUint32 uiIndex;

    uiCount = 0;
    for (uiIndex = 0; uiIndex < BIO_TRACEP_MAX_ACTIVE_PER_CMD; uiIndex++) {
        if ('\0' == v_pstParam[uiIndex].szTracePoint[0]) {
            break;
        }

        if ('\0' != v_pstParam[uiIndex].szHooks[0][0]) {
            continue;
        }

        if (v_pstParam[uiIndex].iTracePointFlag != v_iFlag) {
            continue;
        }

        if (0 == uiCount) {
            CLI_Print("    Tracepoint : %s\n", v_pstParam[uiIndex].szTracePoint);
        } else {
            CLI_Print("                 %s\n", v_pstParam[uiIndex].szTracePoint);
        }

        uiCount++;
    }

    if (0 == uiCount) {
        CLI_Print("    Tracepoint : <None>\n");
    }
}

static void BIO_ShowActiveResult(TpChar *v_pszCommand, BIO_TRACEP_CMD_S *v_pstParam)
{
    CLI_Print("%s result:\n  Success:\n", v_pszCommand);
    BIO_ShowActiveTraceResult(v_pstParam, TRUE);
    BIO_ShowActiveHookResult(v_pstParam, TRUE);

    CLI_Print("  Failure:\n");
    BIO_ShowActiveTraceResult(v_pstParam, FALSE);
    BIO_ShowActiveHookResult(v_pstParam, FALSE);
}

static void BIO_ExeActiveTrace(TpChar *v_pszCommand, BIO_TRACEP_CMD_S *v_pstParam)
{
    BIO_TRACEP_WALK_S stWalk;

    memset(&stWalk, 0, sizeof(stWalk));

    stWalk.uiCurPid = v_pstParam->uiPid;
    stWalk.pstParam = v_pstParam;
    stWalk.iActive = BIO_TRACEP_STAT_ACTIVE;
    if (0 == strcmp(v_pszCommand, "deactivetpold")) {
        stWalk.iActive = BIO_TRACEP_STAT_DEACTIVE;
    }

    BioWalkTracePoint(BIO_ActiveTraceProc, &stWalk);

    BIO_ShowActiveResult(v_pszCommand, v_pstParam);
}

static void BIO_DoTracePointCmd(TpInt32 v_iArgc, TpChar *v_szArgv[])
{
    TpInt32 iRet;
    BIO_TRACEP_CMD_S *pstParam = NULL;

    pstParam = malloc((TpUint32) sizeof(BIO_TRACEP_CMD_S) * BIO_TRACEP_MAX_ACTIVE_PER_CMD);
    if (NULL == pstParam) {
        CLI_Print("Error %s: Out of memory\n", v_szArgv[0]);
        return;
    }

    iRet = BIO_GetActiveParam(v_iArgc, v_szArgv, pstParam);
    if (RETURN_OK != iRet) {
        free(pstParam);
        return;
    }

    iRet = BIO_PraseActiveParam(v_szArgv[0], pstParam);
    if (RETURN_OK != iRet) {
        free(pstParam);
        return;
    }

    BIO_ExeActiveTrace(v_szArgv[0], pstParam);

    free(pstParam);
    return;
}

static void BIO_ShowTracePointHelpInfo(TpChar *v_szCommand, TpInt32 iShowDetail)
{
    const TpChar *pszSHelp =
            "SYNOPSIS\n\
        %s\n\
\n\
DESCRIPTION\n\
        show tracepoint informations.\n\
\n\
OPTIONS\n\
        [-n]\n\
            show next page tracepoint, if this option was given, all other \n\
            options was ignored, no argument means show the first page.\n\
        [-m pid]\n\
            module pid number.\n\
        [-p tracepoint[*]]\n\
            tracepoint information, [*] means show tracepoint list match\n\
            `tracepoint`.\n";

    const TpChar *pszAHelp =
            "SYNOPSIS\n\
        %s %s\n\
\n\
DESCRIPTION\n\
        active tracepoints and hooks.\n\
\n\
OPTIONS\n\
        [-m pid]\n\
            module pid number.\n\
        [-p tracepoint[:*][:hook[:hook]]]\n\
            tracepoint information, this option can be repeated for 1~10 times.\n\
            [:*] means all hooks in tracepoint.\n";

    const TpChar *pszSUsage = "showtpold [-n] [-m pid] [-p tracepoint[*]]";
    const TpChar *pszAUsage = "[-m pid] [-p tracepoint[:[*][hook[:hook]]]]";
    const TpChar *pszAUsage2 = " %s [-m pid] [-p tracepoint[:[*][hook[:hook]]]]";

    if (0 == strcmp(v_szCommand, "showtpold")) {
        if (TRUE == iShowDetail) {
            CLI_Print(pszSHelp, pszSUsage);
        } else {
            CLI_Print("%s", pszSUsage);
        }
    } else {
        if (TRUE == iShowDetail) {
            CLI_Print(pszAHelp, v_szCommand, pszAUsage);
        } else {
            CLI_Print(pszAUsage2, v_szCommand);
        }
    }
}

void showOneTP(BioTracepointNews *tracepoint)
{
    /*lint -e785*/
    char *types[BIO_TP_TYPE_BUTT] = {"callback", "reset", "pause"};
    /*lint +e785*/

    CLI_Print("Name       : %s\n", tracepoint->szName);
    CLI_Print("State      : %s\n", (BIO_TRACEP_STAT_ACTIVE == tracepoint->iActive) ? "active" : "inactive");
    if (tracepoint->type >= BIO_TP_TYPE_BUTT) {
        CLI_Print("Type       : NA\n");
    } else {
        CLI_Print("Type       : %s\n", types[tracepoint->type]);
    }

    CLI_Print("Module     : <%d>\n", tracepoint->uiPid);
    CLI_Print("Desctiption: %s\n", tracepoint->szDesc);
    CLI_Print("User Param : %s\n", tracepoint->stParam.tpParamData);
    if (BIO_TRACEP_STAT_ACTIVE == tracepoint->iActive) {
        CLI_Print("Time Called: %d\n", tracepoint->timeCalled);
    }
}

void showTPForTravel(BioHashItemS *hashdata, void *param, TpUint32 showTpCount)
{
    BioTpHashS *hashTPData = NULL;
    BIO_TRACEP_CMD_NEW_S *command = NULL;

    if ((NULL == hashdata) || (NULL == param)) {
        return;
    }

    hashTPData = (BioTpHashS *) (void *) hashdata;
    command = (BIO_TRACEP_CMD_NEW_S *) param;

    if (((PID_OSP_NULL != command->pid) && (command->pid != hashTPData->stTP.uiPid))
        || ((BIO_TRACEP_STAT_DELETED != command->type) && (command->type != hashTPData->stTP.iActive))) {
        return;
    }

    if ((showTpCount % SHOWTP_COUNT_NEED_FLUSH) == 0) {
        CLI_Print(NULL);
    }

    CLI_PrintBuf("%5u  %-8s %-18s %s\n", hashTPData->stTP.uiPid,
                 (BIO_TRACEP_STAT_ACTIVE == hashTPData->stTP.iActive) ? "active" : "inactive",
                 hashTPData->stTP.szName, hashTPData->stTP.szDesc);

    return;
}

static int32_t getShowParamNew(int32_t v_iArgc, char *v_szArgv[], BIO_TRACEP_CMD_NEW_S *command)
{
    int32_t iRet = 0;
    bool isNameFound = FALSE;
    bool isTypeFound = FALSE;
    bool isPidFound = FALSE;
    CLI_OPT_S stOpt = {0};

    memset(&stOpt, 0, sizeof(CLI_OPT_S));

    command->pid = PID_OSP_NULL;
    command->type = BIO_TRACEP_STAT_DELETED;

    BIO_GetoptInit();
    while ((iRet = CLI_GetOpt(v_iArgc, v_szArgv, "p:m:t:", &stOpt)) != -1) {
        if ((int32_t) 'p' == iRet) {
            if (TRUE == (int32_t) isNameFound) {
                CLI_Print("Error %s: too many argument (p)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            strcpy(&command->traceName[0], stOpt.szOptArg);
            command->traceName[MAX_NAME_LEN - 1] = '\0';

            isNameFound = TRUE;
        } else if ((int32_t) 'm' == iRet) {
            if (TRUE == (int32_t) isPidFound) {
                CLI_Print("Error %s: too many argument (m)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            command->pid = (uint32_t) atoll(stOpt.szOptArg);
            if ((0 == command->pid) || (command->pid >= MAX_PID_NUM)) {
                CLI_Print("Error %s: invalid pid -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }

            isPidFound = TRUE;
        } else if ((int32_t) 't' == iRet) {
            if (TRUE == (int32_t) isTypeFound) {
                CLI_Print("Error %s: too many argument (t)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            if (0 == strcmp(stOpt.szOptArg, "active")) {
                command->type = BIO_TRACEP_STAT_ACTIVE;
            } else if (0 == strcmp(stOpt.szOptArg, "inactive")) {
                command->type = BIO_TRACEP_STAT_DEACTIVE;
            } else {
                CLI_Print("Error %s: invalid type -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }

            isTypeFound = TRUE;
        } else if (CLI_RET_UNKNOWN_ARG == iRet) {
            CLI_ShowUsageAndErrMsg("Error %s: invalid argument -- %s", v_szArgv[0], stOpt.szOptArg);
            return RETURN_ERROR;
        } else {
            CLI_ShowUsageAndErrMsg("%s", stOpt.szErrMsg);
            return RETURN_ERROR;
        }
    }

    return RETURN_OK;
}

static void doTracePointShowCmdNew(int32_t v_iArgc, char *v_szArgv[])
{
    BioTracepointNews *tracepoint = NULL;
    BIO_TRACEP_CMD_NEW_S command;

    memset(&command, 0, sizeof(BIO_TRACEP_CMD_NEW_S));

    if (RETURN_OK != getShowParamNew(v_iArgc, v_szArgv, &command)) {
        return;
    }

    if (0 != strnlen(command.traceName, (size_t) MAX_NAME_LEN)) {
        (void) BioHvsGetTracePoint(PID_OSP_NULL, command.traceName, &tracepoint);
        if (NULL == tracepoint) {
            CLI_Print("Error %s: Cannot find tracepoint %s.\n", v_szArgv[0], command.traceName);
            return;
        }

        showOneTP(tracepoint);
    } else {
        CLI_Print("<PID>  <State>  <TracePoint>        <Description>\n");
        (void) BioTpShowTpAll(g_TPHt, showTPForTravel, (void *) &command);
    }

    return;
}

static int32_t getTypeFromCmd(const char *cmd, int32_t *type)
{
    if (0 == strcmp(cmd, "callback")) {
        *type = BIO_TP_TYPE_CALLBACK;
    } else if (0 == strcmp(cmd, "reset")) {
        *type = BIO_TP_TYPE_RESET;
    } else if (0 == strcmp(cmd, "pause")) {
        *type = BIO_TP_TYPE_PAUSE;
    } else if (0 == strcmp(cmd, "abort")) {
        *type = BIO_TP_TYPE_ABORT;
    } else {
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

static int32_t parseActiveParam(int32_t v_iArgc, char *v_szArgv[], BIO_TRACEP_CMD_NEW_S *command,
                                bool paramRecord[])
{
    int32_t iRet = 0;
    CLI_OPT_S stOpt = {0};

    memset(&stOpt, 0, sizeof(CLI_OPT_S));

    BIO_GetoptInit();
    while ((iRet = CLI_GetOpt(v_iArgc, v_szArgv, "p:t:a:u:", &stOpt)) != -1) {
        if ((int32_t) 'p' == iRet) {
            if (TRUE == paramRecord[BIO_TP_INIT_ACTIVE_NAME]) {
                CLI_Print("Error %s: too many argument (p)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            strcpy(&command->traceName[0], stOpt.szOptArg);
            paramRecord[BIO_TP_INIT_ACTIVE_NAME] = TRUE;
        } else if ((int32_t) 't' == iRet) {
            if (TRUE == paramRecord[BIO_TP_INIT_ACTIVE_TYPE]) {
                CLI_Print("Error %s: too many argument (t)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            if (RETURN_OK != getTypeFromCmd(stOpt.szOptArg, &(command->type))) {
                CLI_Print("Error %s: invalid type -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }

            paramRecord[BIO_TP_INIT_ACTIVE_TYPE] = TRUE;
        } else if ((int32_t) 'a' == iRet) {
            if (TRUE == paramRecord[BIO_TP_INIT_ACTIVE_ALIVE]) {
                CLI_Print("Error %s: too many argument (a)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            command->timeAlive = (uint32_t) atoll(stOpt.szOptArg);
            if (0 == command->timeAlive) {
                CLI_Print("Error %s: invalid time of alive -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }

            paramRecord[BIO_TP_INIT_ACTIVE_ALIVE] = TRUE;
        } else if ((int32_t) 'u' == iRet) {
            if (TRUE == paramRecord[BIO_TP_INIT_ACTIVE_PARAM]) {
                CLI_Print("Error %s: too many argument (u)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            strcpy(&command->userParam.tpParamData[0], stOpt.szOptArg);
            command->userParam.tpParamData[BIO_TRACEP_PARAM_SIZE - 1] = '\0';

            paramRecord[BIO_TP_INIT_ACTIVE_PARAM] = TRUE;
        } else if (CLI_RET_UNKNOWN_ARG == iRet) {
            CLI_ShowUsageAndErrMsg("Error %s: invalid argument -- %s", v_szArgv[0], stOpt.szOptArg);
            return RETURN_ERROR;
        } else {
            CLI_ShowUsageAndErrMsg("%s", stOpt.szErrMsg);
            return RETURN_ERROR;
        }
    }

    return RETURN_OK;
}

static int32_t getActiveParamNew(int32_t v_iArgc, char *v_szArgv[], BIO_TRACEP_CMD_NEW_S *command)
{
    uint32_t interval = 0;
    bool paramRecord[BIO_TP_INIT_ACTIVE_BUTT] = {FALSE, FALSE, FALSE, FALSE};

    if (0 == strcmp(v_szArgv[0], "activetp")) {
        command->cmd = BIO_TRACEP_STAT_ACTIVE;
    } else if (0 == strcmp(v_szArgv[0], "deactivetp")) {
        command->cmd = BIO_TRACEP_STAT_DEACTIVE;
    }

    command->pid = PID_OSP_NULL;
    command->type = BIO_TP_TYPE_BUTT;
    command->timeAlive = INT_MAX;

    if (RETURN_OK != parseActiveParam(v_iArgc, v_szArgv, command, paramRecord)) {
        return RETURN_ERROR;
    }

    if (FALSE == paramRecord[BIO_TP_INIT_ACTIVE_NAME]) {
        CLI_ShowUsageAndErrMsg("Error %s: lack of argument (p)", v_szArgv[0]);
        return RETURN_ERROR;
    }

    if ((FALSE == paramRecord[BIO_TP_INIT_ACTIVE_TYPE]) &&
        (BIO_TRACEP_STAT_ACTIVE == command->cmd)) {
        command->type = BIO_TP_TYPE_CALLBACK;
    }

    if (BIO_TP_TYPE_PAUSE == command->type) {
        /*lint -e661 -e831*/
        if (FALSE == paramRecord[BIO_TP_INIT_ACTIVE_PARAM]) {
            strcpy(&command->userParam.tpParamData[0], "10000");
        } else { /*lint +e661 +e831*/
            interval = (uint32_t) atoll(command->userParam.tpParamData);
            if (0 == interval) {
                CLI_Print("Error %s: time of pause must be less and equal than 20 and not be zero.\n", v_szArgv[0]);
                return RETURN_ERROR;
            }
        }
    }

    return RETURN_OK;
}

static void doTracePointActiveCmdNew(int32_t v_iArgc, char *v_szArgv[])
{
    int32_t ret = RETURN_ERROR;
    BIO_TRACEP_CMD_NEW_S command;

    memset(&command, 0, sizeof(BIO_TRACEP_CMD_NEW_S));

    if (RETURN_OK != getActiveParamNew(v_iArgc, v_szArgv, &command)) {
        return;
    }

    if (BIO_TRACEP_STAT_ACTIVE == command.cmd) {
        ret = BioHvsActiveTracePoint(command.pid, command.traceName, command.type, command.timeAlive,
                                        command.userParam);
        if (RETURN_OK != ret) {
            if (BIO_TP_RETURN_CALLBACK_NULL == ret) {
                CLI_Print("Error %s: callback function is NULL.\n", v_szArgv[0]);
            } else if (BIO_TP_RETURN_NOT_FOUND == ret) {
                CLI_Print("Error %s: tracepint %s not found.\n", v_szArgv[0], command.traceName);
            } else {
                CLI_Print("Error %s: failed to active tracepoint %s.\n", v_szArgv[0], command.traceName);
            }
        }
    } else if (BIO_TRACEP_STAT_DEACTIVE == command.cmd) {
        if (0 == strcmp(command.traceName, "all")) {
            ret = BioHvsDeactiveTracePointAll();
            if (RETURN_OK != ret) {
                CLI_Print("Error %s: failed to deactive all the tracepoints.\n", v_szArgv[0]);
            }
        } else {
            ret = BioHvsDeactiveTracePoint(command.pid, command.traceName);
            if (RETURN_OK != ret) {
                if (BIO_TP_RETURN_NOT_FOUND == ret) {
                    CLI_Print("Error %s: tracepint %s not found.\n", v_szArgv[0], command.traceName);
                } else {
                    CLI_Print("Error %s: failed to deactive tracepoint %s.\n", v_szArgv[0], command.traceName);
                }
            }
        }
    }

    return;
}

static void showTracePointHelpInfo(char *v_szCommand, int32_t iShowDetail)
{
    const char *pszSHelp =
            "SYNOPSIS\n\
        %s\n\
\n\
DESCRIPTION\n\
        show tracepoint informations.\n\
\n\
OPTIONS\n\
        [-m pid]\n\
            show the tracepoints whose module ids equal to pid.\n\
        [-p tpname]\n\
            show the tracepoint whose name equals to tpname.\n\
        [-t type]\n\
            type can be one of following: active|inactive;\n\
            show the tracepoints which are active or inactive.\n";

    const char *pszAHelp =
            "SYNOPSIS\n\
        %s %s\n\
\n\
DESCRIPTION\n\
        active tracepoints.\n\
\n\
OPTIONS\n\
        [-p tpname]\n\
            active the tracepoint whose name equals to tpname.\n\
        [-t type]\n\
            tracepoint type, it can be one of following: callback|reset|pause, and the default is callback.\n\
        [-a alivenum]\n\
            after alivenum times of execution, the tracepoint is deactived automatically.\n\
        [-u userparam]\n\
            userparam you want to send to the tracepoint.\n";

    const char *pszDAHelp =
            "SYNOPSIS\n\
        %s %s\n\
\n\
DESCRIPTION\n\
        deactive tracepoints.\n\
\n\
OPTIONS\n\
        [-p tpname]\n\
            deactive the tracepoint whose name equals to tpname;\n\
            if tpname is all, then deactive all the tracepoints\n";

    const char *pszSUsage = "showtp [-m pid] [-p tpname] [-t type]";
    const char *pszAUsage = "-p tpname [-t type] [-a alivenum] [-u userparam]";
    const char *pszAUsage2 = " %s -p tpname [-t type] [-a alivenum] [-u userparam]";
    const char *pszDAUsage = "-p tpname";
    const char *pszDAUsage2 = " %s -p tpname";

    if (0 == strcmp(v_szCommand, "showtp")) {
        if (TRUE == iShowDetail) {
            CLI_Print(pszSHelp, pszSUsage);
        } else {
            CLI_Print("%s", pszSUsage);
        }
    } else if (0 == strcmp(v_szCommand, "activetp")) {
        if (TRUE == iShowDetail) {
            CLI_Print(pszAHelp, v_szCommand, pszAUsage);
        } else {
            CLI_Print(pszAUsage2, v_szCommand);
        }
    } else {
        if (TRUE == iShowDetail) {
            CLI_Print(pszDAHelp, v_szCommand, pszDAUsage);
        } else {
            CLI_Print(pszDAUsage2, v_szCommand);
        }
    }
}

void BioRegTracePointCmd(void)
{
    CLI_CMD_S stSCmdOld = {
            "showtpold",
            "show tracepoint informations",
            BIO_DoShowTracePointCmd,
            BIO_ShowTracePointHelpInfo,
    };
    CLI_CMD_S stACmdOld = {
            "activetpold",
            "active tracepoints and hooks",
            BIO_DoTracePointCmd,
            BIO_ShowTracePointHelpInfo,
    };
    CLI_CMD_S stDCmdOld = {
            "deactivetpold",
            "deactive tracepoints and hooks",
            BIO_DoTracePointCmd,
            BIO_ShowTracePointHelpInfo,
    };

    CLI_CMD_S stSCmd = {
            "showtp",
            "show tracepoint informations",
            doTracePointShowCmdNew,
            showTracePointHelpInfo,
    };

    CLI_CMD_S stACmd = {
            "activetp",
            "active tracepoints",
            doTracePointActiveCmdNew,
            showTracePointHelpInfo,
    };

    CLI_CMD_S stDCmd = {
            "deactivetp",
            "deactive tracepoints",
            doTracePointActiveCmdNew,
            showTracePointHelpInfo,
    };

    (void) CLI_RegCmd(&stSCmdOld);
    (void) CLI_RegCmd(&stACmdOld);
    (void) CLI_RegCmd(&stDCmdOld);
    (void) CLI_RegCmd(&stSCmd);
    (void) CLI_RegCmd(&stACmd);
    (void) CLI_RegCmd(&stDCmd);
}

TpInt32 BIO_IsTracePointInited(void)
{
    if (NULL != g_TPHt) {
        return RETURN_OK;
    }

    return RETURN_ERROR;
}

void TracePointCmdInit(void)
{
    pthread_mutex_lock(&g_initLock);
    if (RETURN_OK == BIO_IsTracePointInited()) {
        pthread_mutex_unlock(&g_initLock);
        return;
    }

    BioRegTracePointCmd();

    BioHvsInitTracePoint();
    pthread_mutex_unlock(&g_initLock);
}

void TracePointCmdExit(void)
{
    BioHvsExitTracePoint();
}

void DpaxReboot(unsigned long resetReason, unsigned int pid, char *message)
{
    fprintf(stderr, "##### call reboot with reason(%lu) pid(%u) message(%s) #####\n", resetReason, pid, message);
    fflush(stderr);
}