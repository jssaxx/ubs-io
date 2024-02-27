/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2010-2020. All rights reserved.
 * Description: TracePoint调试命令实现
 * Create: 2010-06-26
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

#define LVOS_TRACEP_MAX_ACTIVE_PER_CMD  10

#define LVOS_TRACEP_TRACE_PER_PAGE      40
#define SHOWTP_COUNT_NEED_FLUSH         80
#define LVOS_TRACEP_CONDITION_FOR_RESET_PID (60 * 1000)
#define LVOS_TRACEP_PARAM_BUFF_OFFSET   3

MODULE_ID(PID_DEBUG);


/* 显示所有tracepoint时分页显示的暂时保存的值 */
static OSP_U32 g_uiLastPid = 0;
static OSP_U64 g_ullLastShowTime = 0;
static pthread_mutex_t g_initLock = PTHREAD_MUTEX_INITIALIZER;/*lint !e708*/
extern LVOS_HT_S *g_TPHt;

/*****************************************************************************
* 功    能: 重新初始化getopt函数内部数据
* 输出参数: 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_GetoptInit(void)
{
#ifndef WIN32
    optind = 0;
#endif
}

/*****************************************************************************
* 功    能: 获取showtp命令的参数
* 输出参数: 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_GetShowParam(OSP_S32 v_iArgc, OSP_CHAR *v_szArgv[], LVOS_TRACEP_CMD_S *v_pstParam)
{
    OSP_S32 iRet = 0;

    CLI_OPT_S stOpt = {0};

    memset(v_pstParam, 0, sizeof(LVOS_TRACEP_CMD_S));

    LVOS_GetoptInit();
    while ((iRet = CLI_GetOpt(v_iArgc, v_szArgv, "p:m:n", &stOpt)) != -1) {
        if ('n' == iRet) {
            CLI_SetOpt(&v_pstParam->uiOpts, 'n');
        } else if ('m' == iRet) {
            if (CLI_TestOpt(v_pstParam->uiOpts, 'm')) {
                CLI_Print("Error %s: too many option -- m\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            CLI_SetOpt(&v_pstParam->uiOpts, 'm');
            v_pstParam->uiPid = (OSP_U32) atoll(stOpt.szOptArg);
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

/* 判断是否是trace结束的标志 */
#define TRACP_NAME_END(ch)  ((ch) == '\0' || (ch) == ':')

/*****************************************************************************
* 功    能: 分析输入行内容的所有hook
* 输出参数: 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_PraseHooksLine(OSP_CHAR *v_szCommand, OSP_CHAR *v_szHookStr, LVOS_TRACEP_CMD_S *v_pstParam)
{
    OSP_U32 uiIndex, uiIndex2;
    OSP_CHAR *pchCur = v_szHookStr;

    uiIndex2 = 0;

    while ((*pchCur != '\0') && (uiIndex2 < LVOS_MAX_HOOK_PER_TRACEP)) {
        for (uiIndex = 0;
             !TRACP_NAME_END(pchCur[uiIndex]) && (uiIndex < ARRAY_LEN(v_pstParam->szHooks[uiIndex2]) - 1);
             uiIndex++) {
            v_pstParam->szHooks[uiIndex2][uiIndex] = pchCur[uiIndex];
        }

        if (0 == strcmp(v_pstParam->szHooks[uiIndex2], "*")) {
            /* 任何一个hook为 * 则只保留 * ,其他的hook均忽略 */
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

        pchCur += (uiIndex + 1); /* 指向下一个hook */
        uiIndex2++;

        while ((*pchCur == ':') && (*pchCur != '\0')) {
            pchCur++;
        }
    }

    if (uiIndex2 >= LVOS_MAX_HOOK_PER_TRACEP) {
        CLI_Print("Error %s: too many hooks in argument -- %s\n", v_szCommand, v_pstParam->szParam);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 分析输入行的TracePoint内容
* 输出参数: 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_PraseOneTracePointLine(OSP_CHAR *v_szCommand, LVOS_TRACEP_CMD_S *v_pstParam)
{
    OSP_U32 uiIndex;
    OSP_CHAR *pchCur = v_pstParam->szParam;

    for (uiIndex = 0;
         !TRACP_NAME_END(pchCur[uiIndex]) && (uiIndex < ARRAY_LEN(v_pstParam->szTracePoint) - 1);
         uiIndex++) {
        v_pstParam->szTracePoint[uiIndex] = pchCur[uiIndex];
    }

    /* 只有tracepoint成功 */
    if (pchCur[uiIndex] == '\0') {
        return RETURN_OK;
    }

    /* 查询tracepoint时最后一个通配符 * 可以超长一个字符 */
    if (pchCur[uiIndex] == '*') {
        v_pstParam->szTracePoint[uiIndex] = pchCur[uiIndex];
        uiIndex++;
    }

    if ((pchCur[uiIndex] != ':') && (pchCur[uiIndex] != '\0')) {
        CLI_Print("Error %s: tracepoint too long in argument -- %s\n", v_szCommand, v_pstParam->szParam);
        return RETURN_ERROR;
    }

    pchCur += (uiIndex + 1); /* 指向hook区域 */
    return LVOS_PraseHooksLine(v_szCommand, pchCur, v_pstParam);
}

/*****************************************************************************
* 功    能: 分析showtp命令的参数
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_PraseShowParam(OSP_CHAR *v_szCommand, LVOS_TRACEP_CMD_S *v_pstParam)
{
    OSP_S32 iRet;

    if (CLI_TestOpt(v_pstParam->uiOpts, 'n')) {
        if (CLI_TestOpt(v_pstParam->uiOpts, 'm') || CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
            CLI_Print("Error %s: must be used alone option -- n\n", v_szCommand);
            return RETURN_ERROR;
        }
    }

    if (!CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
        return RETURN_OK;
    }

    /* 解析后面的`p`参数 */
    iRet = LVOS_PraseOneTracePointLine(v_szCommand, v_pstParam);
    if (RETURN_OK != iRet) {
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 列表方式显示TracePoint内容
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowOneTracePointList(OSP_S32 v_iShowHeader, LVOS_TRACEP_S *v_pstTrace)
{
    if (v_iShowHeader != FALSE) {
        CLI_Print("<PID>  <State>  <TracePoint>        <Description>\n");
    }

    CLI_Print("%5u  %-8s %c%-18s %s\n", v_pstTrace->uiPid,
              v_pstTrace->iActive == LVOS_TRACEP_STAT_ACTIVE ? "active" : "deactive",
              v_pstTrace->iDbgOnly ? '*' : ' ', v_pstTrace->szName, v_pstTrace->szDesc);
}

/*****************************************************************************
* 功    能: 显示指定pid的TracePoint列表
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_ShowPidTraceListProc(LVOS_TRACEP_S *v_pstTrace, void *v_pParam)
{
    LVOS_TRACEP_WALK_S *pstWalk = v_pParam;

    /* 打印匹配的记录 */
    if (v_pstTrace->uiPid == pstWalk->uiCurPid) {
        if (pstWalk->uiShowCount == 0) {
            LVOS_ShowOneTracePointList(TRUE, v_pstTrace);
        } else {
            LVOS_ShowOneTracePointList(FALSE, v_pstTrace);
        }
        pstWalk->uiShowCount++;
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 显示下一页TracePoint列表处理回调函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_ShowNextProc(LVOS_TRACEP_S *v_pstTrace, void *v_pParam)
{
    LVOS_TRACEP_WALK_S *pstWalk = v_pParam;

    /* 打印匹配的记录 */
    (void) LVOS_ShowPidTraceListProc(v_pstTrace, v_pParam);

    /* 记录比uiCurPid大的最小PID */
    if (v_pstTrace->uiPid > pstWalk->uiCurPid) {
        if (pstWalk->uiNextPid == pstWalk->uiCurPid) {
            pstWalk->uiNextPid = v_pstTrace->uiPid;
        } else if (v_pstTrace->uiPid < pstWalk->uiNextPid) {
            pstWalk->uiNextPid = v_pstTrace->uiPid;
        }
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 显示下一页TracePoint列表
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowNextPage(LVOS_TRACEP_CMD_S *v_pstParam)
{
    LVOS_TRACEP_WALK_S stWalk;

    /* 距离上次查询时间 > 1分钟则重置起始PID */
    if ((LVOS_GetMilliSecond() - g_ullLastShowTime) > (LVOS_TRACEP_CONDITION_FOR_RESET_PID)) {
        g_uiLastPid = 0;
    }

    g_ullLastShowTime = LVOS_GetMilliSecond();

    stWalk.uiCurPid = g_uiLastPid;
    stWalk.uiNextPid = stWalk.uiCurPid;
    stWalk.uiShowCount = 0;
    stWalk.pstParam = v_pstParam;

    LVOS_WalkTracePoint(LVOS_ShowNextProc, &stWalk);

    while ((stWalk.uiNextPid > stWalk.uiCurPid)
           && (stWalk.uiShowCount < LVOS_TRACEP_TRACE_PER_PAGE)) {
        stWalk.uiCurPid = stWalk.uiNextPid;
        LVOS_WalkTracePoint(LVOS_ShowNextProc, &stWalk);
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

/*****************************************************************************
* 功    能: 显示指定PID的TracePoint列表
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowPidTraceList(LVOS_TRACEP_CMD_S *v_pstParam)
{
    LVOS_TRACEP_WALK_S stWalk;

    stWalk.uiCurPid = v_pstParam->uiPid;
    stWalk.uiShowCount = 0;
    stWalk.pstParam = v_pstParam;

    LVOS_WalkTracePoint(LVOS_ShowPidTraceListProc, &stWalk);

    if (0 == stWalk.uiShowCount) {
        CLI_Print("Error showtpold: No tracepoint for module <%d>\n", stWalk.uiCurPid);
    }
}

/*****************************************************************************
* 功    能: 显示匹配名称的TracePoint列表处理回调函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_ShowTraceMatchProc(LVOS_TRACEP_S *v_pstTrace, void *v_pParam)
{
    LVOS_TRACEP_WALK_S *pstWalk = v_pParam;

    /* 打印匹配的记录 */
    if ((0 == strncmp(v_pstTrace->szName, pstWalk->pstParam->szTracePoint, (size_t) pstWalk->uiNextPid))
        && ((0 == pstWalk->uiCurPid) || (pstWalk->uiCurPid == v_pstTrace->uiPid))) {
        if (pstWalk->uiShowCount == 0) {
            LVOS_ShowOneTracePointList(TRUE, v_pstTrace);
        } else {
            LVOS_ShowOneTracePointList(FALSE, v_pstTrace);
        }
        pstWalk->uiShowCount++;
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 显示匹配的TracePoint列表
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowTraceMatch(LVOS_TRACEP_CMD_S *v_pstParam)
{
    LVOS_TRACEP_WALK_S stWalk;

    stWalk.uiCurPid = v_pstParam->uiPid;
    stWalk.pstParam = v_pstParam;
    stWalk.uiShowCount = 0;
    stWalk.uiNextPid = (OSP_U32) strlen(v_pstParam->szTracePoint); /* 借用一下变量 */

    LVOS_WalkTracePoint(LVOS_ShowTraceMatchProc, &stWalk);

    if (0 == stWalk.uiShowCount) {
        if (0 == stWalk.uiCurPid) {
            CLI_Print("Error showtpold: No tracepoint match `%s`\n", v_pstParam->szTracePoint);
        } else {
            CLI_Print("Error showtpold: No tracepoint match `%s`, module <%d>\n",
                      v_pstParam->szTracePoint, stWalk.uiCurPid);
        }
    }
}

static OSP_S32 LVOS_FindOneTraceProc(LVOS_TRACEP_S *v_pstTrace, void *v_pParam)
{
    LVOS_TRACEP_WALK_S *pstWalk = v_pParam;

    if ((0 == strcmp(v_pstTrace->szName, pstWalk->pstParam->szTracePoint))
        && ((0 == pstWalk->uiCurPid) || (pstWalk->uiCurPid == v_pstTrace->uiPid))) {
        pstWalk->pstTrace = v_pstTrace;
        return RETURN_ERROR; /* 找到一个结束 */
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 列表方式显示Hook
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_ShowHooksProc(LVOS_TRACEP_HOOK_S *v_pstHook, void *v_pvParam)
{
    LVOS_TRACE_HOOK_WALK_S *pstWalkHook = v_pvParam;

    if (0 == pstWalkHook->uiShowCount) {
        CLI_Print("Hooks      :\n    <State>  <Hook>            <Description>\n");
    }

    CLI_Print("    %-8s %c%-16s %s\n", v_pstHook->iActive == LVOS_TRACEP_STAT_ACTIVE ? "active" : "deactive",
              v_pstHook->iDbgOnly ? '*' : ' ', v_pstHook->szName, v_pstHook->szDesc);

    pstWalkHook->uiShowCount++;

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 显示单个TracePoint详细信息
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowOneTrace(LVOS_TRACEP_CMD_S *v_pstParam)
{
    LVOS_TRACEP_WALK_S stWalk;
    LVOS_TRACE_HOOK_WALK_S stWalkHook;
    OSP_CHAR szParamBuf[LVOS_TRACEP_PARAM_SIZE * LVOS_TRACEP_PARAM_BUFF_OFFSET + 1];
    OSP_U32 uiIndex;

    stWalk.uiCurPid = v_pstParam->uiPid;
    stWalk.pstTrace = NULL;
    stWalk.pstParam = v_pstParam;

    LVOS_WalkTracePoint(LVOS_FindOneTraceProc, &stWalk);

    if (NULL == stWalk.pstTrace) {
        CLI_Print("Error showtpold: tracepoint `%s` not found.\n", v_pstParam->szTracePoint);
        return;
    }

    if (FALSE != stWalk.pstTrace->iDbgOnly) {
        CLI_Print("Name       : *%s\n", stWalk.pstTrace->szName);
    } else {
        CLI_Print("Name       : %s\n", stWalk.pstTrace->szName);
    }
    CLI_Print("State      : %s\n", stWalk.pstTrace->iActive == LVOS_TRACEP_STAT_ACTIVE ? "active" : "deactive");
    CLI_Print("Module     : <%d>\n", stWalk.pstTrace->uiPid);
    CLI_Print("Desctiption: %s\n", stWalk.pstTrace->szDesc);
    for (uiIndex = 0; uiIndex < LVOS_TRACEP_PARAM_SIZE; uiIndex++) {
        /* 每个字节包括空格3个字符, 包括'\0'4个字符 */
        snprintf(&szParamBuf[uiIndex * 3], 4UL, "%02x ",
                   (unsigned char) stWalk.pstTrace->stParam.achParamData[uiIndex]);
    }
    CLI_Print("Param Value: %s(HEX)\n", szParamBuf);

    /* 显示Hooks */
    stWalkHook.uiShowCount = 0;
    LVOS_WalkTraceHook(stWalk.pstTrace, LVOS_ShowHooksProc, &stWalkHook);

    if (0 == stWalkHook.uiShowCount) {
        CLI_Print("Hooks      : <None>\n");
    }
}

/*****************************************************************************
* 功    能: 显示指定TracePoint，包括匹配多个
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowSpecialTrace(LVOS_TRACEP_CMD_S *v_pstParam)
{
    OSP_U32 uiLen;

    uiLen = (OSP_U32) strlen(v_pstParam->szTracePoint);

    /* 最后一个字符是*时显示匹配的列表，如果最后一个是*则有可能超长一个字符，strlen可能不准 */
    if ((('*' == v_pstParam->szTracePoint[sizeof(v_pstParam->szTracePoint) - 1])
         || ('*' == v_pstParam->szTracePoint[uiLen - 1]))) {
        v_pstParam->szTracePoint[sizeof(v_pstParam->szTracePoint) - 1] = '\0';
        if (uiLen < sizeof(v_pstParam->szTracePoint)) {
            v_pstParam->szTracePoint[uiLen - 1] = '\0';
        }

        LVOS_ShowTraceMatch(v_pstParam);
        return;
    }

    LVOS_ShowOneTrace(v_pstParam);
}

/*****************************************************************************
* 功    能: 执行showtp命令
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ExeShowTrace(LVOS_TRACEP_CMD_S *v_pstParam)
{
    if (0 == v_pstParam->uiOpts) {
        g_uiLastPid = 0;
        LVOS_ShowNextPage(v_pstParam);
        return;
    }

    if (CLI_TestOpt(v_pstParam->uiOpts, 'n')) {
        LVOS_ShowNextPage(v_pstParam);
        return;
    }

    if (CLI_TestOpt(v_pstParam->uiOpts, 'm') && !CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
        LVOS_ShowPidTraceList(v_pstParam);
        return;
    }

    /* 指定tracepoint显示 */
    LVOS_ShowSpecialTrace(v_pstParam);

    return;
}

/*****************************************************************************
* 功    能: showtp命令处理函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_DoShowTracePointCmd(OSP_S32 v_iArgc, OSP_CHAR *v_szArgv[])
{
    OSP_S32 iRet;
    LVOS_TRACEP_CMD_S *pstParam = NULL;

    pstParam = malloc((OSP_U32) sizeof(LVOS_TRACEP_CMD_S));
    if (NULL == pstParam) {
        CLI_Print("Error %s: Out of memory\n", v_szArgv[0]);
        return;
    }

    iRet = LVOS_GetShowParam(v_iArgc, v_szArgv, pstParam);
    if (RETURN_OK != iRet) {
        free(pstParam);
        return;
    }

    iRet = LVOS_PraseShowParam(v_szArgv[0], pstParam);
    if (RETURN_OK != iRet) {
        free(pstParam);
        return;
    }

    LVOS_ExeShowTrace(pstParam);

    free(pstParam);
    return;
}

/*****************************************************************************
* 功    能: 获取activetp、deactivetp命令的处理参数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_GetActiveParam(OSP_S32 v_iArgc, OSP_CHAR *v_szArgv[], LVOS_TRACEP_CMD_S *v_pstParam)
{
    OSP_S32 iRet = 0;

    CLI_OPT_S stOpt = {0};

    OSP_U32 uiIndex = 0;

    memset(v_pstParam, 0, sizeof(LVOS_TRACEP_CMD_S) * LVOS_TRACEP_MAX_ACTIVE_PER_CMD);

    LVOS_GetoptInit();
    while ((iRet = CLI_GetOpt(v_iArgc, v_szArgv, "p:m:", &stOpt)) != -1) {
        if ('m' == iRet) {
            if (CLI_TestOpt(v_pstParam->uiOpts, 'm')) {
                CLI_Print("Error %s: too many option -- m\n", v_szArgv[0]);
                return RETURN_ERROR;
            }
            CLI_SetOpt(&v_pstParam->uiOpts, 'm');
            v_pstParam->uiPid = (OSP_U32) atoll(stOpt.szOptArg);
            if ((v_pstParam->uiPid == 0) || (v_pstParam->uiPid >= MAX_PID_NUM)) {
                CLI_Print("Error %s: invalid pid -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }
        } else if ('p' == iRet) {
            CLI_SetOpt(&v_pstParam->uiOpts, 'p');
            if (uiIndex < LVOS_TRACEP_MAX_ACTIVE_PER_CMD) {
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

/*****************************************************************************
* 功    能: 分析activetp、deactivetp命令的参数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_PraseActiveParam(OSP_CHAR *v_szCommand, LVOS_TRACEP_CMD_S *v_pstParam)
{
    OSP_S32 iRet;
    OSP_S32 uiIndex;

    if (!CLI_TestOpt(v_pstParam->uiOpts, 'p')) {
        CLI_Print("Error %s: missing required option -- p\n", v_szCommand);
        return RETURN_ERROR;
    }

    for (uiIndex = 0; uiIndex < LVOS_TRACEP_MAX_ACTIVE_PER_CMD; uiIndex++) {
        if (NULL == v_pstParam[uiIndex].szParam) {
            return RETURN_OK;
        }

        /* 解析后面的`p`参数 */
        iRet = LVOS_PraseOneTracePointLine(v_szCommand, v_pstParam + uiIndex);
        if (RETURN_OK != iRet) {
            return RETURN_ERROR;
        }
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: active、deactivetp的处理回调
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_ActiveHooksProc(LVOS_TRACEP_HOOK_S *v_pstHook, void *v_pvParam)
{
    LVOS_TRACE_HOOK_WALK_S *pstWalk = v_pvParam;
    OSP_U32 uiIndex;

    if (0 == strcmp(pstWalk->pstParam->szHooks[0], "*")) {
        v_pstHook->iActive = pstWalk->iActive;
        pstWalk->pstParam->aiHookFlag[0] = TRUE;
        return RETURN_OK;
    }

    for (uiIndex = 0; uiIndex < LVOS_MAX_HOOK_PER_TRACEP && pstWalk->pstParam->szHooks[uiIndex][0] != '\0'; uiIndex++) {
        if (0 == strcmp(pstWalk->pstParam->szHooks[uiIndex], v_pstHook->szName)) {
            v_pstHook->iActive = pstWalk->iActive;
            pstWalk->pstParam->aiHookFlag[uiIndex] = TRUE;
            return RETURN_OK;
        }
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 激活和去激活TracePoint处理回调
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static OSP_S32 LVOS_ActiveTraceProc(LVOS_TRACEP_S *v_pstTrace, void *v_pParam)
{
    OSP_U32 uiIndex;
    LVOS_TRACEP_WALK_S *pstWalk = v_pParam;
    LVOS_TRACE_HOOK_WALK_S stHookWalk;

    /* PID不匹配，直接下一个 */
    if ((0 != pstWalk->uiCurPid) && (v_pstTrace->uiPid != pstWalk->uiCurPid)) {
        return RETURN_OK;
    }

    for (uiIndex = 0; uiIndex < LVOS_TRACEP_MAX_ACTIVE_PER_CMD; uiIndex++) {
        /* 已达最后一个 */
        if ('\0' == pstWalk->pstParam[uiIndex].szTracePoint[0]) {
            return RETURN_OK;
        }

        /* tracepoint匹配 */
        if (0 != strcmp(pstWalk->pstParam[uiIndex].szTracePoint, v_pstTrace->szName)) {
            continue;
        }

        /* 没有hook，是激活/去激活tracepoint的操作 */
        if ('\0' == pstWalk->pstParam[uiIndex].szHooks[0][0]) {
            v_pstTrace->iActive = pstWalk->iActive;
            pstWalk->pstParam[uiIndex].iTracePointFlag = TRUE;
            continue;
        }

        /* 存在hook，遍历hooks进行激活和去激活操作 */
        stHookWalk.iActive = pstWalk->iActive;
        stHookWalk.pstParam = &pstWalk->pstParam[uiIndex];
        LVOS_WalkTraceHook(v_pstTrace, LVOS_ActiveHooksProc, &stHookWalk);
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 显示激活、去激活Hook的结果
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowActiveHookResult(LVOS_TRACEP_CMD_S *v_pstParam, OSP_S32 v_iFlag)
{
    OSP_U32 uiCount, uiHookCount;
    OSP_U32 uiIndex, uiHookIndex;

    uiCount = 0;
    for (uiIndex = 0; uiIndex < LVOS_TRACEP_MAX_ACTIVE_PER_CMD; uiIndex++) {
        if ('\0' == v_pstParam[uiIndex].szTracePoint[0]) {
            break;
        }

        /* 没有hook，本次不显示 */
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

/*****************************************************************************
* 功    能: 显示激活、去激活TracePoint的结果
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowActiveTraceResult(LVOS_TRACEP_CMD_S *v_pstParam, OSP_S32 v_iFlag)
{
    OSP_U32 uiCount;
    OSP_U32 uiIndex;

    uiCount = 0;
    for (uiIndex = 0; uiIndex < LVOS_TRACEP_MAX_ACTIVE_PER_CMD; uiIndex++) {
        if ('\0' == v_pstParam[uiIndex].szTracePoint[0]) {
            break;
        }

        /* 有hook，本次不显示 */
        if ('\0' != v_pstParam[uiIndex].szHooks[0][0]) {
            continue;
        }

        /* 判断成功标志位 */
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

/*****************************************************************************
* 功    能: 显示激活去激活命令的结果
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowActiveResult(OSP_CHAR *v_pszCommand, LVOS_TRACEP_CMD_S *v_pstParam)
{
    CLI_Print("%s result:\n  Success:\n", v_pszCommand);
    LVOS_ShowActiveTraceResult(v_pstParam, TRUE);
    LVOS_ShowActiveHookResult(v_pstParam, TRUE);

    CLI_Print("  Failure:\n");
    LVOS_ShowActiveTraceResult(v_pstParam, FALSE);
    LVOS_ShowActiveHookResult(v_pstParam, FALSE);
}

/*****************************************************************************
* 功    能: 执行激活、去激活操作
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ExeActiveTrace(OSP_CHAR *v_pszCommand, LVOS_TRACEP_CMD_S *v_pstParam)
{
    LVOS_TRACEP_WALK_S stWalk;

    memset(&stWalk, 0, sizeof(stWalk));

    stWalk.uiCurPid = v_pstParam->uiPid;
    stWalk.pstParam = v_pstParam;
    stWalk.iActive = LVOS_TRACEP_STAT_ACTIVE;
    if (0 == strcmp(v_pszCommand, "deactivetpold")) {
        stWalk.iActive = LVOS_TRACEP_STAT_DEACTIVE;
    }

    LVOS_WalkTracePoint(LVOS_ActiveTraceProc, &stWalk);

    LVOS_ShowActiveResult(v_pszCommand, v_pstParam);
}

/*****************************************************************************
* 功    能: 激活、去激活命令处理回调
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_DoTracePointCmd(OSP_S32 v_iArgc, OSP_CHAR *v_szArgv[])
{
    OSP_S32 iRet;
    LVOS_TRACEP_CMD_S *pstParam = NULL;

    pstParam = malloc((OSP_U32) sizeof(LVOS_TRACEP_CMD_S) * LVOS_TRACEP_MAX_ACTIVE_PER_CMD);
    if (NULL == pstParam) {
        CLI_Print("Error %s: Out of memory\n", v_szArgv[0]);
        return;
    }

    iRet = LVOS_GetActiveParam(v_iArgc, v_szArgv, pstParam);
    if (RETURN_OK != iRet) {
        free(pstParam);
        return;
    }

    iRet = LVOS_PraseActiveParam(v_szArgv[0], pstParam);
    if (RETURN_OK != iRet) {
        free(pstParam);
        return;
    }

    LVOS_ExeActiveTrace(v_szArgv[0], pstParam);

    free(pstParam);
    return;
}

/*****************************************************************************
* 功    能: tracepoint命令的帮助打印函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void LVOS_ShowTracePointHelpInfo(OSP_CHAR *v_szCommand, OSP_S32 iShowDetail)
{
    const OSP_CHAR *pszSHelp = /* 为了便于控制输出格式，下面没有缩进 */
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

    const OSP_CHAR *pszAHelp = /* 为了便于控制输出格式，下面没有缩进 */
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

    const OSP_CHAR *pszSUsage = "showtpold [-n] [-m pid] [-p tracepoint[*]]";
    const OSP_CHAR *pszAUsage = "[-m pid] [-p tracepoint[:[*][hook[:hook]]]]";
    const OSP_CHAR *pszAUsage2 = " %s [-m pid] [-p tracepoint[:[*][hook[:hook]]]]";

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

/*****************************************************************************
* 功    能: 显示一条Tracepoint
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void showOneTP(LVOS_TRACEP_NEW_S *tracepoint)
{
    /*lint -e785*/
    char *types[LVOS_TP_TYPE_BUTT] = {"callback", "reset", "pause"};
    /*lint +e785*/

    CLI_Print("Name       : %s\n", tracepoint->szName);
    CLI_Print("State      : %s\n", (LVOS_TRACEP_STAT_ACTIVE == tracepoint->iActive) ? "active" : "inactive");
    if (tracepoint->type >= LVOS_TP_TYPE_BUTT) {
        CLI_Print("Type       : NA\n");
    } else {
        CLI_Print("Type       : %s\n", types[tracepoint->type]);
    }

    CLI_Print("Module     : <%d>\n", tracepoint->uiPid);
    CLI_Print("Desctiption: %s\n", tracepoint->szDesc);
    CLI_Print("User Param : %s\n", tracepoint->stParam.achParamData);
    if (LVOS_TRACEP_STAT_ACTIVE == tracepoint->iActive) {
        CLI_Print("Time Called: %d\n", tracepoint->timeCalled);
    }
}

/*****************************************************************************
* 功    能: 遍历过程中显示一条Tracepoint的回调函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void showTPForTravel(LVOS_HASH_ITEM_S *hashdata, void *param, u32 showTpCount)
{
    LVOS_TP_HASH_S *hashTPData = NULL;
    LVOS_TRACEP_CMD_NEW_S *command = NULL;

    if ((NULL == hashdata) || (NULL == param)) {
        return;
    }

    hashTPData = (LVOS_TP_HASH_S *) (void *) hashdata;
    command = (LVOS_TRACEP_CMD_NEW_S *) param;

    /* 命令指定了PID和是否激活的先过滤 */
    if (((PID_OSP_NULL != command->pid) && (command->pid != hashTPData->stTP.uiPid))
        || ((LVOS_TRACEP_STAT_DELETED != command->type) && (command->type != hashTPData->stTP.iActive))) {
        return;
    }

    // 每显示SHOWTP_COUNT_NEED_FLUSH次TP点信息，就主动刷新一下缓冲区，防止TP点信息丢失
    if ((showTpCount % SHOWTP_COUNT_NEED_FLUSH) == 0) {
        CLI_Print(NULL);
    }

    CLI_PrintBuf("%5u  %-8s %-18s %s\n", hashTPData->stTP.uiPid,
                 (LVOS_TRACEP_STAT_ACTIVE == hashTPData->stTP.iActive) ? "active" : "inactive",
                 hashTPData->stTP.szName, hashTPData->stTP.szDesc);

    return;
}

/*****************************************************************************
* 功    能: 获取showtp命令的处理参数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static int32_t getShowParamNew(int32_t v_iArgc, char *v_szArgv[], LVOS_TRACEP_CMD_NEW_S *command)
{
    int32_t iRet = 0;
    bool isNameFound = FALSE;
    bool isTypeFound = FALSE;
    bool isPidFound = FALSE;
    CLI_OPT_S stOpt = {0};

    /* 内部函数参数有效性已保证 */

    memset(&stOpt, 0, sizeof(CLI_OPT_S));

    /* 初始化command */
    command->pid = PID_OSP_NULL;
    command->type = LVOS_TRACEP_STAT_DELETED;

    /* 命令行参数解析 */
    LVOS_GetoptInit();
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
                command->type = LVOS_TRACEP_STAT_ACTIVE;
            } else if (0 == strcmp(stOpt.szOptArg, "inactive")) {
                command->type = LVOS_TRACEP_STAT_DEACTIVE;
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

/*****************************************************************************
* 功    能:show命令处理
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void doTracePointShowCmdNew(int32_t v_iArgc, char *v_szArgv[])
{
    LVOS_TRACEP_NEW_S *tracepoint = NULL;
    LVOS_TRACEP_CMD_NEW_S command;

    /* 内部函数参数有效性已保证 */

    memset(&command, 0, sizeof(LVOS_TRACEP_CMD_NEW_S));

    if (RETURN_OK != getShowParamNew(v_iArgc, v_szArgv, &command)) {
        return;
    }

    if (0 != strnlen(command.traceName, (size_t) MAX_NAME_LEN)) {
        /* 命令指定了tracepoint */
        (void) LVOS_HVS_getTracePoint(PID_OSP_NULL, command.traceName, &tracepoint);
        if (NULL == tracepoint) {
            CLI_Print("Error %s: Cannot find tracepoint %s.\n", v_szArgv[0], command.traceName);
            return;
        }

        showOneTP(tracepoint);
    } else {
        CLI_Print("<PID>  <State>  <TracePoint>        <Description>\n");
        (void) LVOS_TpShowTpAll(g_TPHt, showTPForTravel, (void *) &command);
    }

    return;
}

/*****************************************************************************
* 功    能: 从命令参数中获取Tracepoint类型
* 输入参数: cmd
* 输出参数: type
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static int32_t getTypeFromCmd(const char *cmd, int32_t *type)
{
    /* 内部函数参数有效性已保证 */

    if (0 == strcmp(cmd, "callback")) {
        *type = LVOS_TP_TYPE_CALLBACK;
    } else if (0 == strcmp(cmd, "reset")) {
        *type = LVOS_TP_TYPE_RESET;
    } else if (0 == strcmp(cmd, "pause")) {
        *type = LVOS_TP_TYPE_PAUSE;
    } else if (0 == strcmp(cmd, "abort")) {
        *type = LVOS_TP_TYPE_ABORT;
    } else {
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 处理activetp、deactivetp命令的参数
* 输入参数:v_iArgc v_szArgv
* 输出参数: command paramRecord
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static int32_t parseActiveParam(int32_t v_iArgc, char *v_szArgv[], LVOS_TRACEP_CMD_NEW_S *command,
                                bool paramRecord[])
{
    int32_t iRet = 0;
    CLI_OPT_S stOpt = {0};

    /* 内部函数参数有效性已保证 */

    memset(&stOpt, 0, sizeof(CLI_OPT_S));

    LVOS_GetoptInit();
    while ((iRet = CLI_GetOpt(v_iArgc, v_szArgv, "p:t:a:u:", &stOpt)) != -1) {
        if ((int32_t) 'p' == iRet) {
            if (TRUE == paramRecord[LVOS_TP_INIT_ACTIVE_NAME]) {
                CLI_Print("Error %s: too many argument (p)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            strcpy(&command->traceName[0], stOpt.szOptArg);
            paramRecord[LVOS_TP_INIT_ACTIVE_NAME] = TRUE;
        } else if ((int32_t) 't' == iRet) {
            if (TRUE == paramRecord[LVOS_TP_INIT_ACTIVE_TYPE]) {
                CLI_Print("Error %s: too many argument (t)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            if (RETURN_OK != getTypeFromCmd(stOpt.szOptArg, &(command->type))) {
                CLI_Print("Error %s: invalid type -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }

            paramRecord[LVOS_TP_INIT_ACTIVE_TYPE] = TRUE;
        } else if ((int32_t) 'a' == iRet) {
            if (TRUE == paramRecord[LVOS_TP_INIT_ACTIVE_ALIVE]) {
                CLI_Print("Error %s: too many argument (a)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            command->timeAlive = (uint32_t) atoll(stOpt.szOptArg);
            if (0 == command->timeAlive) {
                CLI_Print("Error %s: invalid time of alive -- %s\n", v_szArgv[0], stOpt.szOptArg);
                return RETURN_ERROR;
            }

            paramRecord[LVOS_TP_INIT_ACTIVE_ALIVE] = TRUE;
        } else if ((int32_t) 'u' == iRet) {
            if (TRUE == paramRecord[LVOS_TP_INIT_ACTIVE_PARAM]) {
                CLI_Print("Error %s: too many argument (u)\n", v_szArgv[0]);
                return RETURN_ERROR;
            }

            strcpy(&command->userParam.achParamData[0], stOpt.szOptArg);
            command->userParam.achParamData[LVOS_TRACEP_PARAM_SIZE - 1] = '\0';

            paramRecord[LVOS_TP_INIT_ACTIVE_PARAM] = TRUE;
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

/*****************************************************************************
* 功    能: 获取activetp、deactivetp命令的处理参数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static int32_t getActiveParamNew(int32_t v_iArgc, char *v_szArgv[], LVOS_TRACEP_CMD_NEW_S *command)
{
    uint32_t interval = 0;
    bool paramRecord[LVOS_TP_INIT_ACTIVE_BUTT] = {FALSE, FALSE, FALSE, FALSE};

    /* 内部函数参数有效性已保证 */


    /* 初始化command */
    if (0 == strcmp(v_szArgv[0], "activetp")) {
        command->cmd = LVOS_TRACEP_STAT_ACTIVE;
    } else if (0 == strcmp(v_szArgv[0], "deactivetp")) {
        command->cmd = LVOS_TRACEP_STAT_DEACTIVE;
    }

    /* 激活/去激活时不指定pid，默认为0。这里为了可扩展性保留pid字段 */
    command->pid = PID_OSP_NULL;
    command->type = LVOS_TP_TYPE_BUTT;
    command->timeAlive = INT_MAX;

    /* 命令行参数解析 */
    if (RETURN_OK != parseActiveParam(v_iArgc, v_szArgv, command, paramRecord)) {
        return RETURN_ERROR;
    }

    /* p参数是必备的 */
    if (FALSE == paramRecord[LVOS_TP_INIT_ACTIVE_NAME]) {
        CLI_ShowUsageAndErrMsg("Error %s: lack of argument (p)", v_szArgv[0]);
        return RETURN_ERROR;
    }

    /* 激活时type默认为callback */
    if ((FALSE == paramRecord[LVOS_TP_INIT_ACTIVE_TYPE]) &&
        (LVOS_TRACEP_STAT_ACTIVE == command->cmd)) {
        command->type = LVOS_TP_TYPE_CALLBACK;
    }

    /* pause的时间不能超过20秒 */
    if (LVOS_TP_TYPE_PAUSE == command->type) {
        /*lint -e661 -e831*/
        if (FALSE == paramRecord[LVOS_TP_INIT_ACTIVE_PARAM]) {
            strcpy(&command->userParam.achParamData[0], "10000");
        } else { /*lint +e661 +e831*/
            interval = (uint32_t) atoll(command->userParam.achParamData);
            if (0 == interval) {
                CLI_Print("Error %s: time of pause must be less and equal than 20 and not be zero.\n", v_szArgv[0]);
                return RETURN_ERROR;
            }
        }
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 激活&去激活命令处理
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void doTracePointActiveCmdNew(int32_t v_iArgc, char *v_szArgv[])
{
    int32_t ret = RETURN_ERROR;
    LVOS_TRACEP_CMD_NEW_S command;

    /* 内部函数参数有效性已保证 */

    memset(&command, 0, sizeof(LVOS_TRACEP_CMD_NEW_S));

    if (RETURN_OK != getActiveParamNew(v_iArgc, v_szArgv, &command)) {
        return;
    }

    if (LVOS_TRACEP_STAT_ACTIVE == command.cmd) {
        ret = LVOS_HVS_activeTracePoint(command.pid, command.traceName, command.type, command.timeAlive,
                                        command.userParam);
        if (RETURN_OK != ret) {
            if (LVOS_TP_RETURN_CALLBACK_NULL == ret) {
                CLI_Print("Error %s: callback function is NULL.\n", v_szArgv[0]);
            } else if (LVOS_TP_RETURN_NOT_FOUND == ret) {
                CLI_Print("Error %s: tracepint %s not found.\n", v_szArgv[0], command.traceName);
            } else {
                CLI_Print("Error %s: failed to active tracepoint %s.\n", v_szArgv[0], command.traceName);
            }
        }
    } else if (LVOS_TRACEP_STAT_DEACTIVE == command.cmd) {
        /* 名字为all，表示去激活所有tracepoint */
        if (0 == strcmp(command.traceName, "all")) {
            ret = LVOS_HVS_deactiveTracePointAll();
            if (RETURN_OK != ret) {
                CLI_Print("Error %s: failed to deactive all the tracepoints.\n", v_szArgv[0]);
            }
        } else {
            ret = LVOS_HVS_deactiveTracePoint(command.pid, command.traceName);
            if (RETURN_OK != ret) {
                if (LVOS_TP_RETURN_NOT_FOUND == ret) {
                    CLI_Print("Error %s: tracepint %s not found.\n", v_szArgv[0], command.traceName);
                } else {
                    CLI_Print("Error %s: failed to deactive tracepoint %s.\n", v_szArgv[0], command.traceName);
                }
            }
        }
    }

    return;
}

/*****************************************************************************
* 功    能: tracepoint新框架命令的帮助打印函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
static void showTracePointHelpInfo(char *v_szCommand, int32_t iShowDetail)
{
    const char *pszSHelp = /* 为了便于控制输出格式，下面没有缩进 */
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

    const char *pszAHelp = /* 为了便于控制输出格式，下面没有缩进 */
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

    const char *pszDAHelp = /* 为了便于控制输出格式，下面没有缩进 */
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

/*****************************************************************************
* 功    能: 注册TracePoint命令
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_RegTracePointCmd(void)
{
    CLI_CMD_S stSCmdOld = {
            "showtpold",
            "show tracepoint informations",
            LVOS_DoShowTracePointCmd,
            LVOS_ShowTracePointHelpInfo,
    };
    CLI_CMD_S stACmdOld = {
            "activetpold",
            "active tracepoints and hooks",
            LVOS_DoTracePointCmd,
            LVOS_ShowTracePointHelpInfo,
    };
    CLI_CMD_S stDCmdOld = {
            "deactivetpold",
            "deactive tracepoints and hooks",
            LVOS_DoTracePointCmd,
            LVOS_ShowTracePointHelpInfo,
    };

    /* HVS新框架 */
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

/*****************************************************************************
* 功    能:Tracepoint 重复初始化判断
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
OSP_S32 LVOS_IsTracePointInited(void)
{
    if (NULL != g_TPHt) {
        return RETURN_OK;
    }

    return RETURN_ERROR;
}

/*****************************************************************************
* 功    能: TracePoint功能初始化，当前只注册了调试命令
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void TracePointCmdInit(void)
{
    pthread_mutex_lock(&g_initLock);
    if (RETURN_OK == LVOS_IsTracePointInited()) {
        pthread_mutex_unlock(&g_initLock);
        return;
    }

    LVOS_RegTracePointCmd();

    /* HVS新框架 */
    LVOS_HVS_initTracePoint();
    pthread_mutex_unlock(&g_initLock);
}

/*****************************************************************************
* 功    能: TracePoint卸载
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void TracePointCmdExit(void)
{
    LVOS_HVS_exitTracePoint();
}

void dpax_Reboot(unsigned long reset_reason, unsigned int pid, char *message)
{
    fprintf(stderr, "##### call reboot with reason(%lu) pid(%u) message(%s) #####\n", reset_reason, pid, message);
    fflush(stderr);
}