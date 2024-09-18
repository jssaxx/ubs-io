/******************************************************************************

                  版权所有 (C), 2014-2024, 华为技术有限公司

 ******************************************************************************
  文 件 名   : cli_client.h
  版 本 号   : 1.0
  作    者   :
  生成日期   : 2016.9.27
  最近修改   : 
  功能描述   : 内部Client端头文件
  函数列表   :

  修改历史   :
******************************************************************************/
#ifndef __CLI_CLIENT_H__
#define __CLI_CLIENT_H__

#include <termios.h>
#include <fcntl.h>
#include <libgen.h>
#include "cli_common.h"

#ifdef  __cplusplus
#if  __cplusplus
extern "C" {
#endif
#endif

#define DIAG_INPUT_BUFFER_SIZE (4096)
#define MAX_DIAG_PROMPT_LEN    (32)
#define MAX_DIAG_U16_VALUE     ((s32)(u16)(-1))
#define DIAG_INVALID_FD        (-1)
/*定义执行命令超时时间，单位ms*/
#define DIAG_CMD_EXEC_TIMOUT   (800000)

#define DIAG_CLIENT_FORMAT_TIME_STRING_LEN (25)
#define DIAG_CLIENT_FORMAT_TIME_STRING_LEN_MAX (50)
#define DIAG_CLIENT_TIME_CONVERSION_FACTOR  (1000)  //来自于ROC
#define DIAG_CLIENT_ID_SIZE    (32)//来自于ROC
#define DIAG_LOG_BUFFER_REAL_SIZE   (DIAG_LOG_BUFFER_SIZE + DIAG_CLIENT_ID_SIZE)//来自于ROC
#define DIAG_CLIENT_FILE_MAX_SIZE (8 * 1024 * 1024)//来自于ROC

/*特殊字符处理*/
/**
* 方向控制键字符编码
* 上移键：{0x1b, 0x5b, 0x41}
* 下移键：{0x1b, 0x5b, 0x42}
* 右移键：{0x1b, 0x5b, 0x43}
* 左移键：{0x1b, 0x5b, 0x44}
*/
#define LINUX_KEY1          0x1b
#define LINUX_KEY2          0x5b

#define LINUX_KEY_BS        (0x08)
#define LINUX_KEY_SPACE     (0x20)
#define LINUX_KEY_TAB       (0X09)
#define LINUX_KEY_CTRL_BS   (0x7f)


/*用于打印信息到屏幕*/
#define diag_clientPrint(...)   printf(__VA_ARGS__); fflush(stdout);
#define diag_clientPrintAndSave(format, ...)  do{\
    diag_clientPrint(format, ##__VA_ARGS__);\
    diag_clientWriteLog(format, ##__VA_ARGS__);\
}while(0)


#define FLAGS_AUTO_RUNNING  (0x0001)
#define FLAGS_NO_PROMPT     (0x0002)
#define FLAGS_CLI_MODE      (0x0004)


/*定义结构体，用于保存Client端配置信息*/
typedef struct DIAG_CLIENT_CFG
{
    u16 flags;
    u16 serverPort;
    u32 attachId;
    char logFileName[MAX_PATH_NAME];
    char scriptFile[MAX_PATH_NAME];
    char promptNormal[MAX_DIAG_PROMPT_LEN];
    char promptInteract[MAX_DIAG_PROMPT_LEN];
}DIAG_CLIENT_CFG_S;

/*定义日志文件相关信息*/
typedef struct DIAG_LOG_FILE
{
    s32 fd;
    s32 count;
}DIAG_LOG_FILE_S;

/*定义结构体，用于保存Client端信息*/
typedef struct DIAG_CLIENT
{
    u32 status;
    u32 id;
    u32 attachId;         /* 保存attachId */
    u32 pad;            /* 填充 */
    u64 lastCmdActiveMs;  /*用于判断命令执行是否超时*/
    SOCKET socketFd;
    struct sockaddr_in socketServerAddr;
    DIAG_LOG_FILE_S logFile;
    char programPath[MAX_PATH_NAME];
    pid_t processId;
}DIAG_CLIENT_S;

/*定义结构体，用于保存Client端全部信息*/
typedef struct DIAG_SYS_CLIENT
{
    DIAG_CLIENT_S client;
    DIAG_CLIENT_CFG_S config;
    DIAG_MSG_S recvBuffer;
    DIAG_MSG_S sendBuffer;
    DIAG_CMD_ARGS_S cmdArgs;
    char inputBuffer[DIAG_INPUT_BUFFER_SIZE];
}DIAG_SYS_CLIENT_S;

/*定义各入参的全局唯一标识符*/
typedef enum
{
    OPT_HELP = 0,
    OPT_AUTO,
    OPT_NO_PROMPT,
    OPT_SET_CLI,
    OPT_SET_DEBUG,
    OPT_SCRIPT,
    OPT_SERVER_PORT,
    OPT_LOG_FILE,
    OPT_ATTACH,
    OPT_BUTT
}CLIENT_OPT_ID_E;

/*定义结构体，用于解析Client端入参信息*/
typedef struct DIAG_CLIENT_OPT
{
    const char* pOpt;  /*用于匹配输入的OPT关键字*/
    u32 optLen;        /*关键字长度，不含'\0'*/
    CLIENT_OPT_ID_E id;  /*匹配成功后返回的全局唯一标识符*/
}DIAG_CLIENT_OPT_S;

static s32 diag_clientHandleMsgControl(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_clientHandleMsgCmd(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_clientHandleMsgData(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_clientHandleMsgAlive(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_clientHandleMsgPrompt(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);

static s32 diag_clientCmd_exit(void* pSysInfo, s32 argc, char* argv[]);
static s32 diag_clientCmd_script(void* pSysInfo, s32 argc, char* argv[]);

#ifdef  __cplusplus
#if  __cplusplus
}
#endif
#endif

#endif /* __CLI_CLIENT_H__ */

