/******************************************************************************

                  版权所有 (C), 2014-2024, 华为技术有限公司

 ******************************************************************************
  文 件 名   : cli_common.h
  版 本 号   : 1.0
  作    者   :
  生成日期   : 2016.9.27
  最近修改   : 
  功能描述   : 内部公共头文件
  函数列表   :

  修改历史   :
******************************************************************************/
#ifndef __CLI_COMMON_H__
#define __CLI_COMMON_H__

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>

#include "cli_def.h"
#include "cli.h"

#ifdef  __cplusplus
#if  __cplusplus
extern "C" {
#endif
#endif

#ifndef UNREFERENCE_PARAM
#define UNREFERENCE_PARAM(x) ((void)(x))
#endif

#ifdef _PCLINT_
#define __thread 
#endif

#define DIAG_MAGIC   (0x46576798)
#define DIAG_VERSION (1)
#define DIAG_SERVER_SOCKET_PORT    (7002)
#define DIAG_RAND_SOCKET_PORT   (0)

#define DIAG_UNIX_DOMAIN_PATH       "/OSM/diagnose/"
#define DIAG_CLIENT_SUN_PATH        DIAG_UNIX_DOMAIN_PATH"%s_%u_client.unixdomain"
#define DIAG_SERVER_SUN_PATH        DIAG_UNIX_DOMAIN_PATH"%s_%u_server.unixdomain"
#define DIAG_SERVER_SUN_PATH_SIMPLE DIAG_UNIX_DOMAIN_PATH"%s_server.unixdomain"

#define DIAG_INVALID_ID            (0)
#define DIAG_INVALID_SOCKET_FD     (-1)

/*设置心跳超时时间，单位ms*/
#define DIAG_KEEP_ALIVE_TIMEOUT    (10000)
/*设置socket超时时间，单位us*/
#define DIAG_SOCKET_TIMEOUT        (1000)
/*设置socket缓存区大小，单位bytes*/
#define DIAG_SOCKET_BUFFER_SIZE    (64*1024)
/*为了避免CPU占用率过高，而设置的睡眠时间间隔，单位ms*/
#define DIAG_DEALY_INTERVAL    (100)
/*为了避免socket发送太频繁导致丢包，而设置的发送时间间隔，单位ms*/
#define DIAG_SOCKET_SEND_DEALY_MS    (10)
/*为了避免异常场景socket频繁接收数据，导致cpu过高而设置的接收时间间隔，单位ms*/
#define DIAG_SOCKET_RECV_DEALY_MS    (10)

/* 交互式等待超时时间, 单位ms */
#define DIAG_CMD_RUNNING_TIMEOUT   (300*1000)
/* 交互式等待一次超时时间, 单位ms */
#define DIAG_CMD_RUNNING_WAIT_TIMEOUT  (1000)

#define MAX_DIAG_BUFFER_LEN        (1024)
#define MAX_DIAG_BUFFER_SHORT_LEN  (64)
#define MAX_DIAG_LOGIN_FAIL_CNT    (2)
#define MAX_DIAG_SEND_FAIL_CNT     (3)
#define MAX_DIAG_RECV_FAIL_CNT     (100)

#define MAX_DIAG_RETRY_CNT         (3)
#define DIAG_RETRY_DELAY_MS        (1)

#define MAX_DIAG_CMD_ARGCS_NUM     (128)
#define MAX_DIAG_PROMPT_INTERACT_LEN        (128)
#define MAX_DIAG_PROMPT_INPUT_LEN  (64)

/* 可连接的client数目 */
#define MAX_DIAG_CLIENT_NUM      (4)
/* Agent端最多并发命令数目 */
#define MAX_DIAG_CMD_THREAD_NUM  (4)

/*定义内部命令返回值*/
#define DIAG_INNER_CMD_ERROR    (-1)
#define DIAG_INNER_CMD_DONE     (0)
#define DIAG_INNER_CMD_CONTINUE (1)

/*心跳间隔，单位ms*/
#define DIAG_KEEP_ALIVE_INTERVAL   (3000)

#define DIAG_REFRESH_HEARTBEAT_PERIOD  (30)

/*定义自己的日志输出接口*/
#define DIAG_LOG_ERROR    DPLOG_LVL_ERROR
#define DIAG_LOG_WARN     DPLOG_LVL_WARN
#define DIAG_LOG_INFO     DPLOG_LVL_INFO
#define DIAG_LOG_DEBUG    DPLOG_LVL_DEBUG
#define DIAG_LOG(logLevel, format, ...)
#define DIAG_LOG_PRINT_LIMIT(logLevel, format, ...)

#define DIAGNOSE_CLIENT_LOG_PATH  "/var/log/infrastructure/diag_client"
#define DIAG_CLIENT_LOG_FILE_PATH "/var/log/infrastructure/diag_client/run/diagnose_usr.log"
#define DIAGNOSE_CLIENT_LOG_NUM (150)


/*枚举类型，定义报文类型*/
typedef enum
{
    MSG_TYPE_CONTROL = 0,
    MSG_TYPE_CMD,
    MSG_TYPE_DATA,
    MSG_TYPE_ALIVE,
    MSG_TYPE_PROMPT,    /*交互式输入*/
    MSG_TYPE_BUTT
}MSG_TYPE_E;

/*枚举类型，处理命令状态*/
typedef enum
{
    STATUS_IDLE = 0,
    STATUS_CMD_INPUT,  /* 等待输入新命令 */
    STATUS_CMD_RUNNING,/* 命令处理过程中 */
    STATUS_CMD_PROMPT, /* 等待输入交互式命令 */
    STATUS_LOGOUT,     /* 准备退出 */
    STATUS_BUTT
}CLIENT_STATUS_E;

/*枚举类型，定义不同报文对应的可选参数*/
typedef enum
{
    MSG_CONTROL_OPTION_CLIENT_LOGIN = 0,
    MSG_CONTROL_OPTION_AGENT_LOGIN,
    MSG_CONTROL_OPTION_CLIENT_LOGOUT,
    MSG_CONTROL_OPTION_AGENT_LOGOUT,
    MSG_CONTROL_OPTION_LOGIN_SUCCESS,
    MSG_CONTROL_OPTION_LOGIN_BUSY,
    MSG_CONTROL_OPTION_LOGIN_FAIL,
    MSG_CONTROL_OPTION_LOGOUT_SUCCESS,
    MSG_CONTROL_OPTION_ATTACH_SUCCESS,
    MSG_CONTROL_OPTION_ATTACH_FAIL,
    MSG_CONTROL_OPTION_MSG_RECV_SYNC,
    MSG_CONTROL_OPTION_BUTT
}MSG_CONTROL_OPTION_E;

typedef enum
{
    MSG_CMD_OPTION_CLIENT_NORMAL = 0,
    MSG_CMD_OPTION_CLIENT_CLI,
    MSG_CMD_OPTION_AGENT_DONE,
    MSG_CMD_OPTION_INTERACT,
    MSG_CMD_OPTION_SHELL_NORMAL,
    MSG_CMD_OPTION_SHELL_CLI,
    MSG_CMD_OPTION_BUTT
}MSG_CMD_OPTION_E;

typedef enum
{
    MSG_DATA_OPTION_CLIENT = 0,
    MSG_DATA_OPTION_AGENT,
    MSG_DATA_OPTION_SERVER,
    MSG_DATA_OPTION_SHELL,
    MSG_DATA_OPTION_BUTT
}MSG_DATA_OPTION_E;

typedef enum
{
    MSG_ALIVE_OPTION_CLIENT = 0,
    MSG_ALIVE_OPTION_AGENT,
    MSG_ALIVE_OPTION_BUTT
}MSG_ALIVE_OPTION_E;

/*DIANGOSE框架自愈流程ID定义*/
typedef enum
{
    /*配置流程*/
    DIAGNOSE_FDSA_FLOW_SERVER_HANDLE_MSG = 0,
    
    DIAGNOSE_FDSA_FLOW_CMD_BUTT            
}DIAGNOSE_FDSA_FLOW_CMF_TYPE_E;

/*定义结构体，用于保存解析后的命令*/
typedef struct DIAG_CMD_ARGS
{
    int32_t argc;
    char* argv[MAX_DIAG_CMD_ARGCS_NUM];
}DIAG_CMD_ARGS_S;

/*报文头，定义在Client - Server - Agent端传输的消息结构体 */
typedef struct DIAG_MSG_HEAD
{
    uint8_t msgType;     /*报文类型*/
    uint8_t msgOption;   /*可选报文参数*/
    uint8_t version;     /*版本号*/
    uint8_t unused;      
    uint16_t dataLen;    /*data中有效数据长度，含 '\0'*/
    uint16_t headLen;    /*报文头长度*/
    uint32_t clientId;   /*Client端Id*/
    uint32_t agentId;    /*Agent端Id*/
    uint32_t magic;      /*魔术字*/
    uint32_t crc32;      /*crc校验值*/
}DIAG_MSG_HEAD_S;

/*消息报文，用于在Client - Server - Agent端进行消息传输*/
typedef struct DIAG_MSG
{
    DIAG_MSG_HEAD_S msgHead;
    char msgData[MAX_DIAG_BUFFER_LEN];
}DIAG_MSG_S;

/*短消息报文，同DIAG_MSG_S，主要用于数据量较小的心跳消息传输*/
typedef struct DIAG_MSG_SHORT
{
    DIAG_MSG_HEAD_S msgHead;
    char msgData[MAX_DIAG_BUFFER_SHORT_LEN];
}DIAG_MSG_SHORT_S;


/*socket消息处理，函数驱动表*/
typedef int32_t (*pDiagMsgHandle)(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);
typedef struct DIAG_MSG_HANDLE
{
    uint8_t msgType;
    uint8_t unused1;
    uint16_t unused2;
    pDiagMsgHandle msgHandle;
}DIAG_MSG_HANDLE_S;

/*内部命令处理，函数驱动表*/
typedef int32_t (*pDiagCmdHandle)(void* pSysInfo, int32_t argc, char* argv[]);
typedef struct DIAG_CMD_HANDLE
{
    const char* pCmdName;
    const char* pCmdDesc;
    pDiagCmdHandle cmdHandle;
}DIAG_CMD_HANDLE_S;

/**
*函   数  名: diag_initMsgHead
*功能描述: 初始化消息头
*输入参数: @pMsgHead 待初始化的消息头地址
*输出参数: 无
*返   回  值: 无
*/
void diag_initMsgHead(DIAG_MSG_HEAD_S* pMsgHead);

/**
*函   数  名: diag_initSocketAddr
*功能描述: 使用指定的端口，初始化socket地址
*输入参数: @socketPort 指定的socket端口号，0表示使用随机端口 
*输出参数: @pSocketAddr 返回的socket地址
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_initSocketAddr(struct sockaddr_in* pSocketAddr, uint32_t socketPort);
int32_t diag_initUnixDomainAddr(struct sockaddr_un* pSocketAddr, char* pSunPath);
    
/**
*函   数  名: diag_initSocket
*功能描述: 使用指定端口号，初始化socket
*输入参数: @socketPort 指定的socket端口号，0表示使用随机端口 
*输出参数: @pSocketFd 返回的socket描述符
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_initSocket(SOCKET* pSocketFd, uint32_t socketPort);
int32_t diag_initUnixDomainSocket(SOCKET* pSocketFd, int32_t socketType, char* pSunPath);

/**
*函   数  名: diag_initSocket
*功能描述: 发送socket数据
*输入参数: @socketFd 本地打开的socket文件描述符
                           @pSocketDstAddr socket目的地址
                           @pMsg 发送的socket消息
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_sendToSocket(SOCKET socketFd, struct sockaddr_in *pSocketDstAddr, void *pMsg);

/**
*函   数  名: diag_sendSocketTCP
*功能描述: 从socket连接的本地，发送socket数据到对端
*输入参数: @socketFd 本地打开的socket文件描述符
           @pMsg 发送的socket消息
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_sendSocketTCP(SOCKET socketFd, void *pMsg);
    
/**
*函   数  名: diag_recvFromSocket
*功能描述: 结束socket数据
*输入参数: @socketFd 本地打开的socket文件描述符
                           @pSocketSrcAddr socket源地址
                           @pMsg 接收的socket消息
                           @need_print_err 接收失败后，是否需要打印错误信息
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_recvFromSocket(SOCKET socketFd, struct sockaddr_in* pSocketSrcAddr, DIAG_MSG_S* pMsg, bool need_print_err);

/**
*函   数  名: diag_recvSocketTCP
*功能描述: 从socketFd连接的对端接收数据
*输入参数: @socketFd 本地打开的socket文件描述符
           @pMsg 接收的socket消息
           @msgSize 接收buf大小
*输出参数: 
*返   回  值: 成功返回接收到的数据长度，对端关闭返回0，失败返回对应错误码
*/
int32_t diag_recvSocketTCP(SOCKET clientFd, DIAG_MSG_S* pRecvMsg, uint64_t msgSize);

/**
*函   数  名: diag_acceptSocket
*功能描述: 接受建链请求
*输入参数: @socketFd 本地打开的socket文件描述符
*输出参数: 
*返   回  值: 失败返回INVALID_SOCKET，否则返回建链成功的文件描述符
*/
int32_t diag_acceptSocket(SOCKET socketFd);

/**
*函   数  名: diag_connectSocket
*功能描述: 请求建立连接
*输入参数: @socketFd 本地打开的socket文件描述符
            @address 建链的目的地址
            @addressLen 目的地址大小
*输出参数: 
*返   回  值: 成功返回0，失败返回小于0的错误码
*/
int32_t diag_connectSocket(SOCKET socketFd, void* address, uint32_t addressLen);

/**
*函   数  名: diag_isSocketAddrEqual
*功能描述: 判断两个socket地址是否相同
*输入参数: @pSocketAddr1 socket地址1
                           @pSocketAddr2 socket地址2
*输出参数: 
*返   回  值: 相等返回RETURN_OK，不相等返回RETURN_ERROR
*/
int32_t diag_isSocketAddrEqual(struct sockaddr_in* pSocketAddr1, struct sockaddr_in* pSocketAddr2);

/**
*函   数  名: diag_login
*功能描述: 登陆到server端
*输入参数: @socketFd 本地打开的socket文件描述符
                           @pSocketDstAddr socket目的地址
                           @pSendMsg 待发送的socket消息
                           @pRecvMsg 接收的socket消息
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_login(SOCKET socketFd, struct sockaddr_in* pSocketDstAddr, DIAG_MSG_S* pSendMsg, DIAG_MSG_S* pRecvMsg);

/**
*函   数  名: diag_logout
*功能描述: 退出登陆
*输入参数: @socketFd 本地打开的socket文件描述符
                           @pSocketDstAddr socket目的地址
                           @pSendMsg 待发送的socket消息
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_logout(SOCKET socketFd, struct sockaddr_in* pSocketDstAddr, void* pSendMsg);

/**
*函   数  名: diag_handleSocketMsg
*功能描述: 处理socket消息
*输入参数: @pSysInfo 系统信息
                           @pMsgHandleTable 消息处理表，以空的回调函数指针表示表结束
                           @pRecvMsg 接收到的msg消息
                           @pSocketAddr 接收msg 的socket地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_handleSocketMsg(void* pSysInfo, DIAG_MSG_HANDLE_S* pMsgHandleTable, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr);

/**
*函   数  名: diag_getArgs
*功能描述: 从预处理后的数据格式中，解析出argc、argv[]格式的数据
*输入参数: @pMsg msg消息，用于存放预处理后或接收到的socket消息
                           @maxArgsNum 支撑的最大参数个数
*输出参数: @pArgs 用于存放解析后的argc、argv[]数据
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_getArgs(DIAG_MSG_S* pMsg, DIAG_CMD_ARGS_S* pArgs, uint32_t maxArgsNum);

/**
*函   数  名: diag_executeAsInnerCmd
*功能描述: 作为内部命令执行
*输入参数: @pSysInfo 系统信息
                           @pCmdHandle 内部命令处理表
                           @argc 参数个数
                           @argv 指向入参的指针
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
int32_t diag_executeAsInnerCmd(void* pSysInfo, DIAG_CMD_HANDLE_S* pCmdHandle, int32_t argc, char* argv[]);


/**
*函   数  名: diag_getmillisec
*功能描述: 获取系统开工以来的毫秒数
*输入参数: 无
*输出参数: 无
*返   回  值: 系统开工以来的毫秒数
*/
uint64_t diag_getmillisec(void);

/**
*函   数  名: diag_strToLower
*功能描述: 将大写字符转换成小写字符
*输入参数: v_pszStr输入字符串
*输出参数: 无
*返   回  值: 无
*/
void diag_strToLower(char *v_pszStr);

/**
*函   数  名: diag_clientPreProcData
*功能描述: 对用户输入进行预处理
*输入参数: @pSrc 输入字符串
            @pDst 预处理后输出
            @dstSize 目的缓存区大小
*输出参数: 无
*返   回  值: 失败返回RETURN_ERROR，否则返回处理后的数据长度
*/
int32_t diag_clientPreProcData(char* pSrc, char* pDst, uint32_t dstSize);

/**
*函   数  名: diag_commInit
*功能描述: diagnose公共依赖初始化
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，否则表示失败
*/
int32_t diag_commInit(void);

/**
*函   数  名: diag_clientDoWriteLog
*功能描述: 将buf中内容，按原样写入日志文件，主要用于保存操作记录
*输入参数: @pBuffer 日志缓存
*输出参数: 无
*返   回  值: 无
**/
void diag_clientDoWriteLog(char* pBuffer, uint32_t bufferLen);

/**
*函   数  名: diag_clientWriteLog
*功能描述: 保存日志信息，主要用于保存操作记录
*输入参数: @format 格式化字符串
            @... 可变入参
*输出参数: 无
*返   回  值: 无
*/
#define DIAG_LOG_BUFFER_SIZE   (256)
void diag_clientWriteLog(const char* format, ...);

void diag_threadSetName(const char *name, ...);
int32_t diag_setCurThreadPrio(int32_t prio);
int32_t diag_switchThreadAsync(void (*handle)(void *data), void *data, pthread_attr_t *attr);
int32_t diag_cmd_exec(const char *cmdstring);
int32_t diag_cmd_mkdir(char *pFile);

#ifdef  __cplusplus
#if  __cplusplus
}
#endif
#endif

#endif /* __CLI_COMMON_H__ */

