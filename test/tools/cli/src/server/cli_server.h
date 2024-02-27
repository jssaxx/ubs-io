/******************************************************************************

                  版权所有 (C), 2014-2024, 华为技术有限公司

 ******************************************************************************
  文 件 名   : cli_server.h
  版 本 号   : 1.0
  作    者   :
  生成日期   : 2016.9.27
  最近修改   : 
  功能描述   : 内部Server端头文件
  函数列表   :

  修改历史   :
******************************************************************************/
#ifndef __CLI_SERVER_H__
#define __CLI_SERVER_H__

#include "cli_common.h"

#ifdef  __cplusplus
#if  __cplusplus
extern "C" {
#endif
#endif

#define MAX_DIAG_AGENT_NAME (32)
#define MAX_DIAG_AGENT_NUM  (256)

#define CONNECT_STATUS_IDLE (0)
#define CONNECT_STATUS_ON   (1)
#define CONNECT_STATUS_OFF  (2)

/*定义内部命令，该命令在Server端处理*/
#define INNER_CMD_LS        "ls"
#define INNER_CMD_HELP      "help"
#define INNER_CMD_ATTACH    "attach"

/*定义Server端内部日志打印限速宏*/
#define DIAGNOSE_SERVER_PRINT_LIMIT_INTERVAL (60*HZ)
#define DIAGNOSE_SERVER_PRINT_LIMIT_COUNT (3)
#define DIAG_PRINT_LIMIT(can)    // PRINT_LIMIT(DBG_LOG_ERROR, (long)DBG_LOGID_BUTT, DIAGNOSE_SERVER_PRINT_LIMIT_INTERVAL, DIAGNOSE_SERVER_PRINT_LIMIT_COUNT, can)

/*定义结构体，用于保存Client端信息*/
typedef struct DIAG_CLIENT_INFO
{
    struct sockaddr_in socketAddr;
    u64 lastActiveMs;
    u32 status;
    u32 clientId;
    u32 attachedId;
}DIAG_CLIENT_INFO_S;

/*定义结构体，用于保存Agent端信息*/
typedef struct DIAG_AGENT_INFO
{
    struct sockaddr_in socketAddr;
    u64 lastActiveMs;
    u32 status;
    u32 agentId;
    char agentName[MAX_DIAG_AGENT_NAME];
}DIAG_AGENT_INFO_S;

/*定义结构体，用于保存Server端信息*/
typedef struct DIAG_SERVER
{
    SOCKET socketFd;
}DIAG_SERVER_S;

/*定义结构体，用于保存Server端全部信息*/
typedef struct DIAG_SYS_SERVER
{
    DIAG_SERVER_S server;
    DIAG_CLIENT_INFO_S clientInfo[MAX_DIAG_CLIENT_NUM];
    DIAG_AGENT_INFO_S agentInfo[MAX_DIAG_AGENT_NUM];
    struct sockaddr_in recvAddr;
    DIAG_MSG_S recvBuffer;
    DIAG_MSG_S sendBuffer;
    DIAG_CMD_ARGS_S cmdArgs;    
}DIAG_SYS_SERVER_S;

static s32 diag_serverHandleMsgControl(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_serverHandleMsgCmd(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_serverHandleMsgData(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_serverHandleMsgAlive(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_serverHandleMsgPrompt(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);

static s32 diag_serverCmd_ls(void* pSysInfo, s32 argc, char* argv[]);
static s32 diag_serverCmd_attach(void* pSysInfo, s32 argc, char* argv[]);
static s32 diag_serverCmd_help(void* pSysInfo, s32 argc, char* argv[]);
#ifdef __LLT_DIAGNOSE__
static s32 diag_serverCmd_exit(void* pSysInfo, s32 argc, char* argv[]);
#endif

#ifdef  __cplusplus
#if  __cplusplus
}
#endif
#endif

#endif /* __CLI_SERVER_H__ */


