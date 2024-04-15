/******************************************************************************

                  版权所有 (C), 2014-2024, 华为技术有限公司

 ******************************************************************************
  文 件 名   : cli_agent.h
  版 本 号   : 1.0
  作    者   :
  生成日期   : 2016.9.27
  最近修改   : 
  功能描述   : 内部Agent端头文件
  函数列表   :

  修改历史   :
******************************************************************************/
#ifndef __CLI_AGENT_H__
#define __CLI_AGENT_H__

#include "cli_common.h"
#include "cli_getopt.h"
#include "cli_def.h"


#ifndef gettid
#include <sys/syscall.h>
#define gettid() syscall((long)__NR_gettid)
#endif

#ifdef  __cplusplus
#if  __cplusplus
extern "C" {
#endif
#endif

#define MAX_DIAG_CMD_NUM           (256)
#define MAX_DIAG_AGENT_NAME_LEN    (32)
#define MAX_DIAG_CMD_LEN           (32)
#define MAX_DIAG_CMD_DESC_LEN      (64)
#define MAX_DIAG_PRINT_BUFFER_LEN  (512*1024)

#define MAX_DIAG_PAGE_SIZE         (4160)
#define MAX_DIAG_MIDDLE_MEM_LEN    (256)
#define DIAG_EACH_LINE_LEN_SMALL   (16)
#define DIAG_EACH_LINE_LEN_LARGE   (32)
#define DIAG_THREAD_STACK_SIZE     (4*1024*1024)

#define DIAG_INVALID_THREAD_ID     (0)

/*0表示未初始化，非0表示初始化引用计数*/
#define DIAG_AGENT_UNINIT          (0)

/*线程运行状态*/
#define DIAG_THREAD_STATUS_IDLE     (0x00)
#define DIAG_THREAD_STATUS_NORMAL   (0x01)
#define DIAG_THREAD_STATUS_EXITING  (0x02)
#define DIAG_THREAD_STATUS_EXITED   (0x04) //跟ROC保持一致

/*定义命令状态*/
#define DIAG_CMD_STATUS_UNUSED     (0x00)
#define DIAG_CMD_STATUS_NORMAL     (0x01)
#define DIAG_CMD_STATUS_REG        (0x02)
#define DIAG_CMD_STATUS_UNREG      (0x04)

/*定义Agent端内部日志打印限速宏*/
#define DIAGNOSE_AGENT_PRINT_LIMIT_INTERVAL (60*HZ)
#define DIAGNOSE_AGENT_PRINT_LIMIT_COUNT (3)
#define DIAG_PRINT_LIMIT(can)    // PRINT_LIMIT(DBG_LOG_INFO, (long)DBG_LOGID_BUTT, DIAGNOSE_AGENT_PRINT_LIMIT_INTERVAL, DIAGNOSE_AGENT_PRINT_LIMIT_COUNT, can)

/*为了避免socket发送太频繁导致丢包，而设置的发送时间间隔，单位us*/
#define DIAG_AGENT_SOCKET_SEND_DEALY    (10000)

/*定义结构体，用于保存Agent端信息*/
typedef struct DIAG_AGENT
{
    u32 agentId;
    char agentName[MAX_DIAG_AGENT_NAME_LEN]; 
    SOCKET socketFd;    
    struct sockaddr_in socketServerAddr;
    CLI_CMD_S* pCurrentCmd;   /*用于保存正在执行的命令，执行这次命令所在的链表地址*/
    u64 recvThreadId;
    u64 aliveThreadId;
    u64 shellThreadId;    
}DIAG_AGENT_S;

/*定义结构体，用CLI_PrintintBuf打印的信息*/
typedef struct DIAG_PRINT_BUFFER
{
    u32 len;
    char buffer[MAX_DIAG_PRINT_BUFFER_LEN];
}DIAG_PRINT_BUFFER_S;

/*定义结构体，用于保存用户注册的命令*/
typedef struct DBG_REG_CMD
{
    volatile u32  status;
    u64  runningCnt;
    CLI_CMD_S cmd;    
}DBG_REG_CMD_S;

typedef union {
    struct sockaddr_in sockAddrIn;
    struct sockaddr_un sockAddrUn;
}DIAG_AGENT_SOCKADDR_S;

/* 定义结构体， client消息处理线程信息定义 */
typedef struct DIAG_SYS_CMD_THREAD
{
    diag_sem_t      sem;
    u32             threadStatus;
    volatile CLIENT_STATUS_E status;
    u32             pad;
    SOCKET          socketFd;
    DIAG_AGENT_SOCKADDR_S* pRecvAddr; //NULL:表示基于连接的socket; 否则指向recvAddr，用于标识消息来源
    DIAG_AGENT_SOCKADDR_S recvAddr;
//    struct sockaddr_in recvAddr;
    
    DIAG_MSG_S      recvBuffer;       //仅在接收命令时使用，在整个命令执行生命周期内保证存储的数据有效
    DIAG_MSG_S      recvBufferTmp;    //在命令执行过程中，如果需要临时的和client交互，使用该buf接收数据，仅作临时缓存，不做持久化
    char            recvInputData[MAX_DIAG_PROMPT_INPUT_LEN];
    DIAG_MSG_S      sendBuffer;
    DIAG_CMD_ARGS_S cmdArgs;
    
    DIAG_PRINT_BUFFER_S printBuffer;
}DIAG_SYS_CMD_THREAD_s;

/*定义结构体，用于保存Agent端全部信息*/
typedef struct DIAG_SYS_AGENT
{
    DIAG_AGENT_S agent;
    struct sockaddr_in recvAddr;
    DiagSpinLockType spinlock;       /*保护对buffer的操作*/
    DBG_REG_CMD_S cmdBufferNormal[MAX_DIAG_CMD_NUM];
    DBG_REG_CMD_S cmdBufferCli[MAX_DIAG_CMD_NUM];
    DIAG_SYS_CMD_THREAD_s cmdThread[MAX_DIAG_CMD_THREAD_NUM];
    DIAG_MSG_S recvBuffer;
    DIAG_MSG_S sendBuffer;
}DIAG_SYS_AGENT_S;

/*自动初始化*/
typedef struct DIAG_INIT
{
    pthread_mutex_t mutex;
    u32        inited;  //0未初始化，非零被初始化的次数
    u32        initId;  //初始化命名域，单调递增，用于标识初始化和销毁的区间
}DIAG_INIT_S;

static s32 diag_agentHandleMsgControl(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_agentHandleMsgCmd(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_agentHandleMsgData(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_agentHandleMsgAlive(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
static s32 diag_agentHandleMsgPrompt(void* pSysInfo, DIAG_MSG_S* pRecvMsg, struct sockaddr_in *pSocketAddr);

static s32 diag_agentCmd_help(void* pSysInfo, s32 argc, char* argv[]);
#ifdef  __cplusplus
#if  __cplusplus
}
#endif
#endif

#endif /* __CLI_AGENT_H__ */

