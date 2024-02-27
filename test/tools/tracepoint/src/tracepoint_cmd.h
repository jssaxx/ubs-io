/******************************************************************************
     版权所有 (C) 2010 - 2010  华为赛门铁克科技有限公司
*******************************************************************************
* 版 本 号: 初稿
* 作    者: x00001559
* 生成日期: 2010年6月30日
* 功能描述: tracepoint命令处理内部头文件
* 备    注: 
* 修改记录: 
*         1)时间    : 
*          修改人  : 
*          修改内容: 
******************************************************************************/
#ifndef __LVOS_TRACEPOINT_CMD_H__
#define __LVOS_TRACEPOINT_CMD_H__

#include "tracepoint.h"
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define LVOS_TRACEP_MAX_ACTIVE_PER_CMD  10

#define LVOS_TRACEP_TRACE_PER_PAGE      40
#define INT_MAX ((int)(~0U>>1))
typedef struct
{
    OSP_U32  uiOpts;
    OSP_U32  uiPid;
    OSP_CHAR *szParam;                      /* 指向 -p 后面的参数，便于后面分析 */
    OSP_CHAR szTracePoint[MAX_NAME_LEN];    /* TracePoint的名称 */
    OSP_CHAR szHooks[LVOS_MAX_HOOK_PER_TRACEP][MAX_NAME_LEN]; /* Hooks的名称 */
    OSP_S32  iTracePointFlag;
    OSP_S32  aiHookFlag[LVOS_MAX_HOOK_PER_TRACEP];
} LVOS_TRACEP_CMD_S;

/* 遍历TracePoint使用的参数 */
typedef struct
{
    OSP_U32 uiCurPid;
    OSP_U32 uiNextPid;
    OSP_U32 uiShowCount;
    OSP_S32 iActive;
    LVOS_TRACEP_CMD_S *pstParam;
    LVOS_TRACEP_S  *pstTrace;
} LVOS_TRACEP_WALK_S;

/* 遍历Hook使用的参数 */
typedef struct
{
    OSP_U32 uiShowCount;
    OSP_S32 iActive;
    LVOS_TRACEP_CMD_S *pstParam;
} LVOS_TRACE_HOOK_WALK_S;

/*HVS新框架*/
typedef struct
{
    uint32_t cmd;
    uint32_t pid;
    int32_t type;           /*activetp&deactive用于指定激活类型;showtp用于指定查询激活还是去激活*/
    uint32_t timeAlive;
    LVOS_TRACEP_PARAM_S userParam;
    char traceName[MAX_NAME_LEN];
} LVOS_TRACEP_CMD_NEW_S;

void TracePointCmdInit(void);
void TracePointCmdExit(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* end of __LVOS_TRACEPOINT_CMD_H__*/

