/*************************************************
  Copyright (C) Huawei Technologies Co., Ltd. 2015-2015. All rights reserved.  
  File name:      cli_agent.c
  Author:	   ID:       Version:       Date: 
  Description:    全用户态cli agent端
  Others:          
  History:        
*************************************************/
#include "cli_agent.h"
#include <ctype.h>


static DIAG_SYS_AGENT_S g_sys_agent;

/*消息处理表，处理函数为NULL表示表结束*/
static DIAG_MSG_HANDLE_S g_agentMsgHandleTable[] = 
{ 
    {MSG_TYPE_CONTROL, 0, 0, diag_agentHandleMsgControl},
    {MSG_TYPE_CMD,     0, 0, diag_agentHandleMsgCmd},
    {MSG_TYPE_DATA,    0, 0, diag_agentHandleMsgData},
    {MSG_TYPE_ALIVE,   0, 0, diag_agentHandleMsgAlive},
    {MSG_TYPE_PROMPT,  0, 0, diag_agentHandleMsgPrompt},
    {0, 0, 0, NULL}
};

/*Server端自有命令*/
static DIAG_CMD_HANDLE_S g_agentCmdHandleTable[] = 
{
    {"help",   "show help information",          diag_agentCmd_help},
    {"",       "",                               NULL},
};

/*不能注册的命令, 必须保留最后的NULL*/
static char *g_sys_black_cmd_list[] = 
{
    "help",
    "ls",
    "quit",
    "exit",
    "attach",
    NULL,
};

/*用于防止重复、并发初始化*/
static DIAG_INIT_S g_sys_init = 
{
    .mutex = PTHREAD_MUTEX_INITIALIZER, //lint !e708
    .inited = DIAG_AGENT_UNINIT,
    .initId = DIAG_INVALID_THREAD_ID,
};

/*采用其他内存地址传递initId，防止新创建线程的异步操作产生的时序问题*/
static u32 g_initIdTmp = DIAG_INVALID_THREAD_ID;

#define IS_DIAG_AGENT_INITED()   (DIAG_AGENT_UNINIT != g_sys_init.inited)
#define DIAG_AGENT_INIT_LOCK()   pthread_mutex_lock(&(g_sys_init.mutex))
#define DIAG_AGENT_INIT_UNLOCK() pthread_mutex_unlock(&(g_sys_init.mutex))
#define DIAG_AGENT_INIT_ID_UPDATE()  (++g_sys_init.initId)
#define DIAG_AGENT_INIT_ID()      (g_sys_init.initId)
#define DIAG_AGENT_INIT_SUCCESS() (g_sys_init.inited = ((u32)(DIAG_AGENT_UNINIT - 1) == g_sys_init.inited) ? g_sys_init.inited : (g_sys_init.inited + 1))
#define DIAG_AGENT_DESTROY_SUCCESS() (g_sys_init.inited = (DIAG_AGENT_UNINIT == g_sys_init.inited) ? g_sys_init.inited : (g_sys_init.inited - 1))

static __thread DIAG_SYS_CMD_THREAD_s*   g_sys_current_cmd_context = NULL;
#define IS_DIAG_AGENT_CMD_CONTEXT()  (NULL != g_sys_current_cmd_context)

/*用于保证用户注册命令的安全*/
#define DIAG_AGENT_RUN_CMD_START_CALLBACK(pRegCmd, callback) \
DIAG_ATOMIC_INC(&pRegCmd->runningCnt); \
if(DIAG_CMD_STATUS_NORMAL != pRegCmd->status) \
{ \
    DIAG_ATOMIC_DEC(&pRegCmd->runningCnt); \
    callback; \
}

#define DIAG_AGENT_RUN_CMD_END(pRegCmd) DIAG_ATOMIC_DEC(&pRegCmd->runningCnt)

#if  DESC("内部函数")
/**
*函   数  名: diag_agentSendSocket
*功能描述: agent端发送socket数据函数
*输入参数: @socketFd 本地打开的socket文件描述符
                           @pSocketDstAddr socket目的地址
                           @pMsg 发送的socket消息
*输出参数: 
*返   回  值: 成功返回发送的字节数，否则，返回负值表示对应的错误码
*/
s32 diag_agentSendSocket(SOCKET socketFd, DIAG_AGENT_SOCKADDR_S *pSocketDstAddr, void *pMsg)
{
    s32 ret;

    if(NULL == pSocketDstAddr)
    {   
        ret = diag_sendSocketTCP(socketFd, pMsg);
    }
    else
    {
        ret = diag_sendToSocket(socketFd, &(pSocketDstAddr->sockAddrIn), pMsg);    
    }
    
    usleep(DIAG_AGENT_SOCKET_SEND_DEALY);
    return ret;
}

/**
*函   数  名: diag_getCurrentSysInfo
*功能描述: 获取Agent端信息
*输入参数: 无
*输出参数: 无
*返   回  值: 返回指向Agent端信息的指针
*/
static inline DIAG_SYS_AGENT_S* diag_getCurrentSysInfo(void)
{
    return &g_sys_agent;
}

/**
*函   数  名: diag_agentSetCurCmdCtx
*功能描述: 设置当前命令执行的上下文信息
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
static inline void diag_agentSetCurCmdCtx(DIAG_SYS_CMD_THREAD_s* pCmdCtx)
{
    g_sys_current_cmd_context = pCmdCtx;
}

/**
*函   数  名: diag_agentGetCurCmdCtx
*功能描述: 返回当前命令执行的上下文信息
*输入参数: 无
*输出参数: 无
*返   回  值: 返回当前命令执行的上下文信息
*/
static inline DIAG_SYS_CMD_THREAD_s* diag_agentGetCurCmdCtx(void)
{
    return g_sys_current_cmd_context;
}

/**
*函   数  名: diag_agentGetCmdBuffer
*功能描述: 获取存放用户注册命令的命令池。
        内部根据接收的信息，自动判断是普通命令池，还是CLI命令池
        仅限于处理命令的上下文使用
*输入参数: 无
*输出参数: 无
*返   回  值: 指向存放用户注册命令的命令池
*/
static inline DBG_REG_CMD_S* diag_agentGetCmdBuffer(DIAG_SYS_AGENT_S* pSysAgent)
{
    DIAG_SYS_CMD_THREAD_s* pCmdCtx = diag_agentGetCurCmdCtx();
    DBG_REG_CMD_S* pCmdBuffer;
    MSG_CMD_OPTION_E msgOption;

    if(NULL == pCmdCtx)
    {
        pCmdBuffer = &(pSysAgent->cmdBufferCli[0]);
        return pCmdBuffer;
    }

    msgOption = (MSG_CMD_OPTION_E)pCmdCtx->recvBuffer.msgHead.msgOption;
    if((MSG_CMD_OPTION_CLIENT_NORMAL == msgOption) || (MSG_CMD_OPTION_SHELL_NORMAL == msgOption))
    {
        pCmdBuffer = &(pSysAgent->cmdBufferNormal[0]);
    }
    else
    {
        pCmdBuffer = &(pSysAgent->cmdBufferCli[0]);
    }

    return pCmdBuffer;
}

/**
*函   数  名: diag_agentInfoInit
*功能描述: 初始化Agent信息
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，否则失败
*/
static s32 diag_agentInfoInit(DIAG_SYS_AGENT_S* pSysAgent)
{
    s32 ret;
    DIAG_AGENT_S* pAgent = &(pSysAgent->agent);

    /*初始化数据为0*/
    memset(pSysAgent, 0, sizeof(DIAG_SYS_AGENT_S));

    /* 初始化锁 */
    DIAG_SPIN_INIT(&(pSysAgent->spinlock), 0);
    
    /*初始化Server端socket地址*/
    ret = diag_initSocketAddr(&(pAgent->socketServerAddr), DIAG_SERVER_SOCKET_PORT);
    if(RETURN_OK != ret)
    {
        return ret;
    }
    
    /*初始化Agent端socket*/
    ret = diag_initSocket(&(pAgent->socketFd), DIAG_RAND_SOCKET_PORT);
    if(RETURN_OK != ret)
    {
        return ret;
    }
    
    /*初始化发送Buffer消息头*/
    diag_initMsgHead(&(pSysAgent->sendBuffer.msgHead)); 
    
    return RETURN_OK;

}

/**
*函   数  名: diag_agentInfoSet
*功能描述: 设置AgentId和AgentName
*输入参数: @agentId 用户指定的，用于唯一标识一个进程的u32值，不能为0
                            @pAgentName 用户指定的Agent端别名。
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，否则失败
*/
static s32 diag_agentInfoSet(DIAG_AGENT_S* pAgent, u32 agentId, char* pAgentName)
{
    s32 ret;
    char* pAgentNameTmp;

    /*设置agentId*/
    pAgent->agentId= agentId;

    /*设置agentName*/
    pAgentNameTmp = strrchr(pAgentName, '/');
    pAgentNameTmp = (NULL == pAgentNameTmp) ? (pAgentName) : (pAgentNameTmp+1);
    strncpy(pAgent->agentName, pAgentNameTmp, (size_t)MAX_DIAG_AGENT_NAME_LEN-1);

    return RETURN_OK;
}

/**
*函   数  名: diag_agentLogin
*功能描述: 登陆到Server端
*输入参数: @pSysAgent 指向agent端信息的指针
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_agentLogin(DIAG_SYS_AGENT_S* pSysAgent)
{
    s32 ret = 0;
    DIAG_AGENT_S* pAgent = &(pSysAgent->agent);
    DIAG_MSG_S* pSendMsg = &(pSysAgent->sendBuffer);
    DIAG_MSG_S* pRecvMsg = &(pSysAgent->recvBuffer);    

    pSendMsg->msgHead.agentId = pAgent->agentId;
    pSendMsg->msgHead.msgType = MSG_TYPE_CONTROL;
    pSendMsg->msgHead.msgOption = MSG_CONTROL_OPTION_AGENT_LOGIN;
    snprintf(pSendMsg->msgData, (ulong)MAX_DIAG_BUFFER_LEN, "%s", pAgent->agentName);
    pSendMsg->msgHead.dataLen = (u16)ret;

    ret = diag_login(pAgent->socketFd, &(pAgent->socketServerAddr), pSendMsg, pRecvMsg);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose Agent Login fail");
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_agentFlushBuf
*功能描述: 将指定缓存区中的数据发送到客户端
*输入参数: @pBuffer 指向待输出buffer地址
                             @bufferLen 待输出buffer长度
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_agentFlushBuf(char *pBuffer, u32 bufferLen)
{
    s32 ret;
    u32 dataLen;    
    DIAG_SYS_AGENT_S* pSysAgent = diag_getCurrentSysInfo();
    DIAG_SYS_CMD_THREAD_s* pCmdThrd = diag_agentGetCurCmdCtx();
    DIAG_MSG_S* pSendMsg = NULL; 

    if (NULL == pCmdThrd)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Diagnose is not in diagnose context");
        return RETURN_ERROR;
    }
    
    pSendMsg = &(pCmdThrd->sendBuffer);
    
    if(NULL == pBuffer)
    {
        return RETURN_ERROR;
    }

    if(0 == bufferLen)
    {
        return RETURN_OK;
    }

    /*初始化发送消息头*/
    diag_initMsgHead(&(pSendMsg->msgHead));
    pSendMsg->msgHead.msgType = MSG_TYPE_DATA;
    pSendMsg->msgHead.msgOption = MSG_DATA_OPTION_AGENT;
    pSendMsg->msgHead.agentId = pSysAgent->agent.agentId;
    pSendMsg->msgHead.clientId = pCmdThrd->recvBuffer.msgHead.clientId;

    while(0 != bufferLen)
    {
        /*组装数据*/
        dataLen = (bufferLen >= MAX_DIAG_BUFFER_LEN - 1) ? (MAX_DIAG_BUFFER_LEN - 1) : bufferLen;
        pSendMsg->msgHead.dataLen = (u16)dataLen;
        memcpy(pSendMsg->msgData, pBuffer, (ulong)dataLen);
        pSendMsg->msgData[dataLen] = '\0';
        
        /*更新游标*/
        bufferLen -= dataLen;
        pBuffer += dataLen;

        /*发送消息*/
        ret = diag_agentSendSocket(pCmdThrd->socketFd, pCmdThrd->pRecvAddr, (void*)pSendMsg);
        if(ret < 0)
        {
            DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Diagnose socket send fail while flush buffer ret:%d", ret);
            break;
        }
    }

    return RETURN_OK;        
}

/**
*函   数  名: diag_agentFlushCurrentBuf
*功能描述: 将当前命令处理线程的缓存区中的数据发送到客户端
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentFlushCurrentBuf(void)
{
    DIAG_SYS_CMD_THREAD_s* pCmdThrd = diag_agentGetCurCmdCtx();
    char *pBuffer;
    u32 bufferLen;
    s32 ret;

    if (NULL == pCmdThrd)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Diagnose is not in diagnose context");
        return RETURN_ERROR;
    }

    pBuffer = pCmdThrd->printBuffer.buffer;
    bufferLen = pCmdThrd->printBuffer.len;

    ret = diag_agentFlushBuf(pBuffer, bufferLen);

    /* 清理当前缓冲区 */
    pBuffer[0] = '\0';
    pCmdThrd->printBuffer.len = 0;

    return ret;
}

/**
*函   数  名: diag_agentPrint
*功能描述: 对内提供的打印函数接口
*输入参数: @v_pchFormat 格式化字符串
                             @pArgList  可变入参
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentPrint(const OSP_CHAR* v_pchFormat, va_list pArgList)
{
    s32 ret;
    DIAG_SYS_AGENT_S* pSysAgent = diag_getCurrentSysInfo();
    DIAG_SYS_CMD_THREAD_s* pCmdThrd = diag_agentGetCurCmdCtx();
    DIAG_MSG_S* pSendMsg = NULL; 

    if (NULL == pCmdThrd)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Diagnose is not in diagnose context");
        return RETURN_ERROR;
    }

    pSendMsg = &(pCmdThrd->sendBuffer);
    
    if((NULL == v_pchFormat) || ('\0' == v_pchFormat[0]))
    {
        return RETURN_OK;
    }

    /*初始化发送消息头*/
    diag_initMsgHead(&(pSendMsg->msgHead));
    pSendMsg->msgHead.msgType = MSG_TYPE_DATA;
    pSendMsg->msgHead.msgOption = MSG_DATA_OPTION_AGENT;
    pSendMsg->msgHead.agentId = pSysAgent->agent.agentId;
    pSendMsg->msgHead.clientId = pCmdThrd->recvBuffer.msgHead.clientId;

    /*格式化消息内容*/
    ret = vsnprintf(&(pSendMsg->msgData[0]), (ulong)(MAX_DIAG_BUFFER_LEN-1), v_pchFormat, pArgList);
    pSendMsg->msgHead.dataLen = (u16)ret;

    /*发送消息*/
    ret = diag_agentSendSocket(pCmdThrd->socketFd, pCmdThrd->pRecvAddr, (void*)pSendMsg);
    if(ret < 0)
    {
        DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Print data msg send fail, ret:%d", ret);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

static s32 diag_agentWaitForTCPInput(DIAG_SYS_CMD_THREAD_s* pCmdThrd, char* v_szInput, u32 v_uiMaxInputLen)
{
    s32 ret;
    DIAG_MSG_S* pRecvMsg = &(pCmdThrd->recvBufferTmp);

    do{
        ret = diag_recvSocketTCP(pCmdThrd->socketFd, pRecvMsg, sizeof(DIAG_MSG_S));
        if(ret <= 0)
            return RETURN_ERROR;
    }while((MSG_TYPE_PROMPT != pRecvMsg->msgHead.msgType) || (MSG_DATA_OPTION_SHELL != pRecvMsg->msgHead.msgOption));

    /*将client输入信息拷贝给v_szInput */
    v_uiMaxInputLen = (v_uiMaxInputLen > MAX_DIAG_PROMPT_INPUT_LEN) ? MAX_DIAG_PROMPT_INPUT_LEN : v_uiMaxInputLen;
    (void)strncpy(v_szInput, pRecvMsg->msgData, (ulong)(v_uiMaxInputLen-1));
    v_szInput[v_uiMaxInputLen-1] = '\0';
    return RETURN_OK;    
}

static s32 diag_agentWaitForUDPInput(DIAG_SYS_CMD_THREAD_s* pCmdThrd, char* v_szInput, u32 v_uiMaxInputLen)
{
    s32 ret;
    u32 waitTime;
    
    /*等待输入数据*/
    waitTime = 0;
    while(waitTime < DIAG_CMD_RUNNING_TIMEOUT)
    {
        waitTime += DIAG_CMD_RUNNING_WAIT_TIMEOUT;

        ret = sem_trywait(&pCmdThrd->sem);
        if ((0 == ret) || (waitTime >= DIAG_CMD_RUNNING_TIMEOUT))
        {
            break;
        }

        diag_msleep(DIAG_CMD_RUNNING_WAIT_TIMEOUT);
    }

    if(0 != ret)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Diagnose get input timeout:%u ms", waitTime);
        return RETURN_ERROR;
    }

    /*将client输入信息拷贝给v_szInput */
    v_uiMaxInputLen = (v_uiMaxInputLen > MAX_DIAG_PROMPT_INPUT_LEN) ? MAX_DIAG_PROMPT_INPUT_LEN : v_uiMaxInputLen;
    (void)strncpy(v_szInput, pCmdThrd->recvInputData, (ulong)(v_uiMaxInputLen-1));
    v_szInput[v_uiMaxInputLen-1] = '\0';
    return RETURN_OK;
}

/**
*函   数  名: diag_agentWaitForPromptInput
*功能描述: 等待交互式命令的输入
*输入参数: @v_szPrompt 提示信息
           @v_szInput  保存待输入信息的空间
           v_uiMaxInputLen  待输入信息空间的最大长度
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentWaitForPromptInput(DIAG_SYS_CMD_THREAD_s* pCmdThrd, char* v_szInput, u32 v_uiMaxInputLen)
{
    s32 ret;
    
    if(NULL != pCmdThrd->pRecvAddr)
        ret = diag_agentWaitForUDPInput(pCmdThrd, v_szInput, v_uiMaxInputLen);
    else
        ret = diag_agentWaitForTCPInput(pCmdThrd, v_szInput, v_uiMaxInputLen);

    return ret;
}

/**
*函   数  名: diag_agentPromptInput
*功能描述: 交互式命令的实际实现
*输入参数: @v_szPrompt 提示信息
           @v_szInput  保存待输入信息的空间
           v_uiMaxInputLen  待输入信息空间的最大长度
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentPromptInput(OSP_CHAR *v_szPrompt, OSP_CHAR *v_szInput, OSP_U32 v_uiMaxInputLen)
{
    s32 ret;
    DIAG_SYS_AGENT_S* pSysAgent = diag_getCurrentSysInfo();
    DIAG_SYS_CMD_THREAD_s* pCmdThrd = diag_agentGetCurCmdCtx();
    DIAG_MSG_S* pSendMsg = NULL; 

    if (NULL == pCmdThrd)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose is not in diagnose context");
        return RETURN_ERROR;
    }

    pSendMsg = &(pCmdThrd->sendBuffer);
    
    /*初始化发送消息头*/
    diag_initMsgHead(&(pSendMsg->msgHead));
    pSendMsg->msgHead.msgType = MSG_TYPE_PROMPT;
    pSendMsg->msgHead.msgOption = MSG_DATA_OPTION_AGENT;
    pSendMsg->msgHead.agentId = pSysAgent->agent.agentId;
    pSendMsg->msgHead.clientId = pCmdThrd->recvBuffer.msgHead.clientId;
    
    /*格式化消息内容*/
    ret = snprintf(&(pSendMsg->msgData[0]), (ulong)(MAX_DIAG_PROMPT_INTERACT_LEN-1), "%s", v_szPrompt);
    pSendMsg->msgHead.dataLen = (u16)ret;
    pCmdThrd->status = STATUS_CMD_PROMPT;
    
    /*发送消息*/
    ret = diag_agentSendSocket(pCmdThrd->socketFd, pCmdThrd->pRecvAddr, (void*)pSendMsg);
    if(ret < 0)
    {
        pCmdThrd->status = STATUS_CMD_RUNNING;        
        DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Diagnose socket send fail, ret:%d", ret);
        return RETURN_ERROR;
    }

    ret = diag_agentWaitForPromptInput(pCmdThrd, v_szInput, v_uiMaxInputLen);
    /* 不管是否成功，都要把状态改回成 STATUS_CMD_RUNNING */
    pCmdThrd->status = STATUS_CMD_RUNNING;
    return ret;
}

/**
*函   数  名: diag_agentIsValidCmd
*功能描述: 检查是否是有效diagnose命令
*输入参数: @v_pstCmd 注册的diagnose命令
*输出参数: 无
*返   回  值: 有效返回RETURN_OK，无效返回RETURN_ERROR
*/
s32 diag_agentIsValidCmd(CLI_CMD_S* pCmd)
{
    u32 index;
    u32 cmdLen;
    char* pszCmd = pCmd->szCommand;
    char* blackcmd;

    /* 支持注册help函数为空的命令 */
    if(NULL == pCmd->fnCmdDo)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Invalid diagnose fnCmd: %p", \
            pCmd->fnCmdDo);
        return RETURN_ERROR;
    }

    cmdLen = (u32)strlen(pszCmd);
    if((cmdLen <= 1) || (cmdLen >= CLI_MAX_COMMAND_LEN))
    {
        DIAG_LOG(DIAG_LOG_INFO, "Invalid diagnose pszCmd: %s", pszCmd);
        return RETURN_ERROR;
    }

    for(index = 0; index < cmdLen; index++)
    {
        if((isalnum(0xFF & pszCmd[index]) || ('_' == pszCmd[index])))
        {
            continue;
        }
        
        DIAG_LOG(DIAG_LOG_INFO, "Invalid diagnose pszCmd: %s", pszCmd);
        return RETURN_ERROR;        
    }

    /*判断命令是否是黑名单中的*/
    for(index = 0; ;index++)
    {
        blackcmd = g_sys_black_cmd_list[index];
        if (NULL == blackcmd)
        {
            break;
        }
        if (0 == strncmp(blackcmd, pszCmd, (ulong)CLI_MAX_COMMAND_LEN))
        {
            DIAG_LOG(DIAG_LOG_ERROR, "cann't register diagnose pszCmd: %s", pszCmd);
            return RETURN_ERROR;
        }
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_agentGetCmdByName
*功能描述: 根据命令名获取已注册的diagnose命令
*输入参数: @pCmdBuffer 存放已注册命令的命令池
                             @pCmdName 待查询的命令名
*输出参数: 无
*返   回  值: 查找成功返回找到的已注册命令地址，此时ppCmdFree值未知。
        查找失败返回NULL，此时ppCmdFree中存放空闲的命令地址。
*/
DBG_REG_CMD_S* diag_agentGetCmdByName(DBG_REG_CMD_S* pCmdBuffer, char* pCmdName)
{
    s32 ret;
    u32 index;
    DBG_REG_CMD_S* pCmdTmp;
    DBG_REG_CMD_S* pCmdSelf = NULL;
    
    for(index = 0, pCmdTmp = pCmdBuffer; index < MAX_DIAG_CMD_NUM; index++, pCmdTmp++)
    {
        if(DIAG_CMD_STATUS_UNUSED == pCmdTmp->status)
        {
            continue;
        }
        
        ret = strncmp(pCmdTmp->cmd.szCommand, pCmdName, (ulong)CLI_MAX_COMMAND_LEN);
        if(0 == ret)
        {
            pCmdSelf = pCmdTmp;
            break;
        }
    }

    return pCmdSelf;
}


/**
*函   数  名: diag_agentGetFreeCmd
*功能描述: 获取空闲的命令注册空间
*输入参数: @pCmdBuffer 存放注册命令的命令池

*输出参数: @ppCmdFree 如果查询不到，返回空闲的命令地址，否则返回值未知
*返   回  值: 查找成功返回找到的已注册命令地址，此时ppCmdFree值未知。
        查找失败返回NULL，此时ppCmdFree中存放空闲的命令地址。
*/
void diag_agentGetFreeCmd(DBG_REG_CMD_S* pCmdBuffer, DBG_REG_CMD_S** ppCmdFree)
{
    u32 index;
    DBG_REG_CMD_S* pCmdTmp;
    DBG_REG_CMD_S* pCmdFree = NULL;
    
    for(index = 0, pCmdTmp = pCmdBuffer; index < MAX_DIAG_CMD_NUM; index++, pCmdTmp++)
    {
        if(DIAG_CMD_STATUS_UNUSED == pCmdTmp->status)
        {
            pCmdFree = pCmdTmp;
            break;
        }
    }

    if(NULL != ppCmdFree)
    {
        *ppCmdFree = pCmdFree;
    }
}

/**
*函   数  名: diag_agentRegCmd
*功能描述: 注册diagnose命令，初始化流程使用，不支持并发操作
*输入参数: @pCmdBuffer 保存注册命令的buffer池
           @pNewCmd 注册的diagnose命令
           @pSpinlock, buffer保护锁
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_agentRegCmd(DBG_REG_CMD_S* pCmdBuffer, CLI_CMD_S* pNewCmd, DiagSpinLockType* pSpinlock)
{
    DBG_REG_CMD_S* pCmdSelf;
    DBG_REG_CMD_S* pCmdFree = NULL;

    DIAG_SPIN_LOCK(pSpinlock);
    /*重复注册，直接返回失败*/
    pCmdSelf = diag_agentGetCmdByName(pCmdBuffer, pNewCmd->szCommand);
    if(NULL != pCmdSelf)
    {
        DIAG_SPIN_UNLOCK(pSpinlock);
        DIAG_LOG(DIAG_LOG_INFO, "RegCmd: %s fail, repeat register", pNewCmd->szCommand);
        return RETURN_ERROR;
    }

    diag_agentGetFreeCmd(pCmdBuffer, &pCmdFree);

    /*规格耗尽，返回失败*/
    if(NULL == pCmdFree)
    {
        DIAG_SPIN_UNLOCK(pSpinlock);
        DIAG_LOG(DIAG_LOG_INFO, "RegCmd: %s fail, max support cmd num: %u", pNewCmd->szCommand, MAX_DIAG_CMD_NUM);
        return RETURN_ERROR;
    }

    /*设置正在注册标记*/
    pCmdFree->status = DIAG_CMD_STATUS_REG;
    
    /*保存用户命令到内部数据结构*/
    memcpy(&(pCmdFree->cmd), pNewCmd, sizeof(CLI_CMD_S));
    pCmdFree->runningCnt = 0;

    /*注册成功*/
    pCmdFree->status = DIAG_CMD_STATUS_NORMAL;
    DIAG_SPIN_UNLOCK(pSpinlock);

    return RETURN_OK;
}

/**
*函   数  名: diag_agentUnRegCmd
*功能描述: 注销diagnose命令
*输入参数: @pCmdBuffer 保存注册命令的Buffer池
           @pCmdName 待注销的diagnose命令字
           @pSpinlock, buffer保护锁
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_agentUnRegCmd(DBG_REG_CMD_S* pCmdBuffer, char* pCmdName, DiagSpinLockType* pSpinlock)
{
    DBG_REG_CMD_S* pCmdSelf;

    DIAG_SPIN_LOCK(pSpinlock);
    
    /*获取注册的命令信息*/
    pCmdSelf = diag_agentGetCmdByName(pCmdBuffer, pCmdName);
    if(NULL == pCmdSelf)
    {
        DIAG_SPIN_UNLOCK(pSpinlock);
        return RETURN_OK;
    }

    /*设置正在注销标记*/
    pCmdSelf->status |= DIAG_CMD_STATUS_UNREG;
    DIAG_SPIN_UNLOCK(pSpinlock);

    /*等待命令运行结束*/
    while(0 != pCmdSelf->runningCnt)
    {
        diag_msleep(DIAG_DEALY_INTERVAL);
    }

    /*注销命令*/
    pCmdSelf->status = DIAG_CMD_STATUS_UNUSED;

    return RETURN_OK;
}

/**
*函   数  名: diag_agentKeepAlive
*功能描述: diagnose Agent端心跳线程
*输入参数: @pArg 线程入参
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
void diag_agentKeepAlive(void* pArg)
{
    s32 ret;
    u32 agentInitId = g_initIdTmp; //此处不可替换为DIAG_AGENT_INIT_ID()
    DIAG_MSG_SHORT_S keepAliveMsg;
    DIAG_SYS_AGENT_S* pSysAgent = (DIAG_SYS_AGENT_S*)pArg;
    DIAG_AGENT_S* pAgent = &(pSysAgent->agent);

    pAgent->aliveThreadId = (u64)gettid();
    diag_threadSetName("diagAgtAlvThrd");
    (void)diag_setCurThreadPrio(DIAG_THRD_PRIO_HIGHEST);
    
    diag_initMsgHead(&(keepAliveMsg.msgHead));    
    keepAliveMsg.msgHead.msgType = MSG_TYPE_ALIVE;
    keepAliveMsg.msgHead.msgOption = MSG_ALIVE_OPTION_AGENT;
    keepAliveMsg.msgHead.agentId = pAgent->agentId;
    ret = snprintf(keepAliveMsg.msgData, (ulong)MAX_DIAG_AGENT_NAME_LEN, "%s", pAgent->agentName);
    keepAliveMsg.msgHead.dataLen = (u16)((ret < 0) ? MAX_DIAG_AGENT_NAME_LEN : ret);
    
    for(;;)
    {
        if(agentInitId != DIAG_AGENT_INIT_ID())
        {
            break;
        }
        (void)diag_sendToSocket(pAgent->socketFd, &(pAgent->socketServerAddr), (void*)(&keepAliveMsg));

        diag_msleep(DIAG_KEEP_ALIVE_INTERVAL);
    }

    pAgent->aliveThreadId = DIAG_INVALID_THREAD_ID;
    DIAG_LOG(DIAG_LOG_INFO, "Keep alive thread tid: %d initId: %u exit success", gettid(), agentInitId);    
}

/**
*函   数  名: diag_agentHandleMsgControl
*功能描述: 处理Client端控制命令
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentHandleMsgControl(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    UNREFERENCE_PARAM(pSysInfo);
    UNREFERENCE_PARAM(pRecvMsg);
    UNREFERENCE_PARAM(pSocketAddr);
    
    return RETURN_OK;
}

/**
*函   数  名: diag_agentHandleCmdDone
*功能描述: 处理Client端命令完成
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentHandleCmdDone(DIAG_SYS_AGENT_S* pSysAgent, DIAG_SYS_CMD_THREAD_s* pCmdCtx)
{
    s32 ret;
    DIAG_MSG_SHORT_S cmdDoneMsg;
    
    /*将当前缓存区的内容，发送到客户端*/
    (void)diag_agentFlushCurrentBuf();
    
    diag_initMsgHead(&(cmdDoneMsg.msgHead));
    cmdDoneMsg.msgHead.msgType = MSG_TYPE_CMD;
    cmdDoneMsg.msgHead.msgOption = MSG_CMD_OPTION_AGENT_DONE;
    cmdDoneMsg.msgHead.clientId = pCmdCtx->recvBuffer.msgHead.clientId;
    cmdDoneMsg.msgHead.agentId = pSysAgent->agent.agentId;

    ret = diag_agentSendSocket(pCmdCtx->socketFd, pCmdCtx->pRecvAddr, (void*)(&cmdDoneMsg));
    if(ret < 0)
    {
        DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Cmd done msg send fail, ret:%d", ret);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_agentHelpShowDefault
*功能描述: agent端内部命令help:显示帮助信息
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
static void diag_agentHelpShowDefault(DIAG_SYS_AGENT_S* pSysAgent)
{
    u32 index;
    DBG_REG_CMD_S* pCmdBuffer = diag_agentGetCmdBuffer(pSysAgent);
    DBG_REG_CMD_S* pCmdTmp;

    CLI_PrintBuf("<command>        <description>\n");
    for(index = 0, pCmdTmp = pCmdBuffer; index < MAX_DIAG_CMD_NUM; index++, pCmdTmp++)
    {
        if(DIAG_CMD_STATUS_UNUSED == pCmdTmp->status)
        {
            continue;
        }
        
        DIAG_AGENT_RUN_CMD_START_CALLBACK(pCmdTmp, continue);
        CLI_PrintBuf(" %-16s %s\n", pCmdTmp->cmd.szCommand, pCmdTmp->cmd.szDescription);
        DIAG_AGENT_RUN_CMD_END(pCmdTmp);
    }

    return ;
}

/**
*函   数  名: diag_agentHelpShowDetail
*功能描述: agent端内部命令help:显示帮助信息
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
static void diag_agentHelpShowDetail(DIAG_SYS_AGENT_S* pSysAgent, char* pCmdName)
{
    s32 ret;
    u32 index;
    DBG_REG_CMD_S* pCmdBuffer = diag_agentGetCmdBuffer(pSysAgent);
    DBG_REG_CMD_S* pCmdTmp;
    FN_CLI_CMD_HELP_PROC funcHelp;

    diag_strToLower(pCmdName);

    for(index = 0, pCmdTmp = pCmdBuffer; index < MAX_DIAG_CMD_NUM; index++, pCmdTmp++)
    {
        if(DIAG_CMD_STATUS_UNUSED == pCmdTmp->status)
        {
            continue;
        }

        DIAG_AGENT_RUN_CMD_START_CALLBACK(pCmdTmp, continue); 
        ret = strncmp(pCmdTmp->cmd.szCommand, pCmdName, (ulong)CLI_MAX_COMMAND_LEN);
        if(0 != ret)
        {
            DIAG_AGENT_RUN_CMD_END(pCmdTmp);
            continue;
        }

        CLI_Print("NAME\n        %s - %s\n\n", pCmdTmp->cmd.szCommand, pCmdTmp->cmd.szDescription);
        funcHelp = pCmdTmp->cmd.fnPrintCmdHelp;
        if(NULL != funcHelp)
        {
            funcHelp(pCmdName, 1);
        }
        DIAG_AGENT_RUN_CMD_END(pCmdTmp);
        return ;
    }

    CLI_Print("Error help: command `%s` not found\n", pCmdName);
    return ;
}

/**
*函   数  名: diag_agentHelpShowWithOpt
*功能描述: agent端内部命令help:显示帮助信息
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
static void diag_agentHelpShowWithOpt(DIAG_SYS_AGENT_S* pSysAgent, s32 argc, char* argv[])
{
    char ch;
    CLI_OPT_S opt = {0};
    
    while(-1 != (ch = (char)CLI_GetOpt(argc, argv, "c:", &opt)))  
    {  
        switch(ch)  
        {  
            case 'c':  
                diag_agentHelpShowDetail(pSysAgent, opt.szOptArg);
                return;  
            default:  
                CLI_Print("Error help:\nUsage: help [-c command[*]]\n");
                return;
        }  
    }  
}

/**
*函   数  名: diag_agentCmd_help
*功能描述: 作为内部命令执行
*输入参数: @pSysInfo 系统信息
                           @argc 参数个数
                           @argv 指向入参的指针
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
static s32 diag_agentCmd_help(void* pSysInfo, s32 argc, char* argv[])
{
    if(1 == argc)
    {
        diag_agentHelpShowDefault((DIAG_SYS_AGENT_S*)pSysInfo);
    }
    else
    {
        diag_agentHelpShowWithOpt((DIAG_SYS_AGENT_S*)pSysInfo, argc, argv);
    }
    
    return DIAG_INNER_CMD_DONE;
}

/**
*函   数  名: diag_agentHandleAsAgentCmd
*功能描述: 作为agent端内部命令处理，包括help等
*输入参数: @pSysAgent 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
static s32 diag_agentHandleAsAgentCmd(DIAG_SYS_AGENT_S* pSysAgent, DIAG_SYS_CMD_THREAD_s* pCmdThrd)
{
    s32 ret;
    DIAG_CMD_ARGS_S* pArgs = &(pCmdThrd->cmdArgs);
    DIAG_MSG_S* pRecvMsg = &(pCmdThrd->recvBuffer);

    ret = diag_getArgs(pRecvMsg, pArgs, MAX_DIAG_CMD_ARGCS_NUM);
    if(RETURN_OK != ret)
    {
        return DIAG_INNER_CMD_ERROR;
    }

    ret = diag_executeAsInnerCmd((void*)pSysAgent, &(g_agentCmdHandleTable[0]), pArgs->argc, pArgs->argv);
    return ret;
}

/**
*函   数  名: diag_agentHandleAsRegCmd
*功能描述: 作为用户注册命令处理,必须在命令处理线程上下文中
*输入参数: @pSysAgent 指向Server端全部信息
                           @pCmdThrd 命令处理线程的管理信息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: RETURN_OK执行成功，RETURN_ERROR执行失败
*/
static s32 diag_agentHandleAsRegCmd(DIAG_SYS_AGENT_S* pSysAgent, DIAG_SYS_CMD_THREAD_s* pCmdThrd)
{
    DBG_REG_CMD_S* pCmdBuffer = diag_agentGetCmdBuffer(pSysAgent);
    DBG_REG_CMD_S* pCmdSelf;
    FN_CLI_CMD_PROC pFuncCmd;
    DIAG_CMD_ARGS_S* pArgs = NULL;

    pArgs = &(pCmdThrd->cmdArgs);

    diag_strToLower(pArgs->argv[0]);

    DIAG_SPIN_LOCK(&pSysAgent->spinlock);
    /*获取用户注册的命令*/
    pCmdSelf = diag_agentGetCmdByName(pCmdBuffer, pArgs->argv[0]);
    if(NULL == pCmdSelf)
    {
        CLI_PrintBuf("Error: command `%s` not found.\n", pArgs->argv[0]);
        DIAG_SPIN_UNLOCK(&pSysAgent->spinlock);
        return RETURN_ERROR;
    }

    DIAG_SPIN_UNLOCK(&pSysAgent->spinlock);
    /*执行用户注册的命令*/
    DIAG_AGENT_RUN_CMD_START_CALLBACK(pCmdSelf, return RETURN_ERROR);
    pFuncCmd = pCmdSelf->cmd.fnCmdDo;
    if(NULL != pFuncCmd)
    {
        pFuncCmd(pArgs->argc, pArgs->argv);
    }
    DIAG_AGENT_RUN_CMD_END(pCmdSelf);

    return RETURN_OK;
}

/**
*函   数  名: diag_agentHandleCmd
*功能描述: 处理接受到的命令
*输入参数: @pSysAgent 指向Server端全部信息
            @pCmdCtx  命令处理线程上下文
            @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: RETURN_OK执行成功，RETURN_ERROR执行失败
*/
static s32 diag_agentHandleCmd(DIAG_SYS_AGENT_S* pSysAgent, DIAG_SYS_CMD_THREAD_s* pCmdThrd)
{
    s32 ret;

    /*作为Agent内部命令处理*/
    ret = diag_agentHandleAsAgentCmd(pSysAgent, pCmdThrd);
    if(DIAG_INNER_CMD_CONTINUE == ret)
    {
        /*作为用户注册的命令执行*/
        (void)diag_agentHandleAsRegCmd(pSysAgent, pCmdThrd);
    }

    return ret;
}

/**
*函   数  名: diag_agentAddCmdToThread
*功能描述: 选择一个线程来处理命令
*输入参数: @pArg 线程入参
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentAddCmdToThread(DIAG_SYS_AGENT_S* pSysAgent, DIAG_MSG_S* pRecvMsg, struct sockaddr_in*pSocketAddr)
{
    u32 thrd_idx;
    DIAG_SYS_CMD_THREAD_s *pCmdThrd = NULL;
    u64  start_time;

    start_time = diag_getmillisec();
    for (;;)
    {
        for (thrd_idx = 0; thrd_idx < MAX_DIAG_CMD_THREAD_NUM; thrd_idx++)
        {
            pCmdThrd = &pSysAgent->cmdThread[thrd_idx];
            if (pCmdThrd->status == STATUS_CMD_INPUT)
            {
                memcpy(&pCmdThrd->recvBuffer, pRecvMsg, sizeof(DIAG_MSG_S));
                memcpy(&(pCmdThrd->recvAddr.sockAddrIn), pSocketAddr, sizeof(struct sockaddr_in));
                
                pCmdThrd->status = STATUS_CMD_RUNNING;
                DIAG_SEM_UP(&pCmdThrd->sem);
                
                return RETURN_OK;
            }
        }
        
        diag_msleep(DIAG_DEALY_INTERVAL);
        if ((diag_getmillisec() - start_time) > DIAG_CMD_RUNNING_TIMEOUT)
        {
            DIAG_LOG(DIAG_LOG_INFO, "There is no valid diag contex, maybe the cmd before was blocked");
            break;
        }
    }

    return RETURN_ERROR;
}

/**
*函   数  名: diag_agentHandleMsgCmd
*功能描述: 处理命令消息
*输入参数: @pSysAgent 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: RETURN_OK执行成功，RETURN_ERROR执行失败
*/
static s32 diag_agentHandleMsgCmd(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in *pSocketAddr)
{
    s32 ret = RETURN_ERROR;
    DIAG_SYS_AGENT_S* pSysAgent = (DIAG_SYS_AGENT_S*)pSysInfo;

    switch(pRecvMsg->msgHead.msgOption)
    {
    case MSG_CMD_OPTION_CLIENT_NORMAL:
    case MSG_CMD_OPTION_CLIENT_CLI:
        ret = diag_agentAddCmdToThread(pSysAgent, pRecvMsg, pSocketAddr);
        break;
    default:
        break;
    }
    return ret;
}

/**
*函   数  名: diag_agentHandleMsgData
*功能描述: 处理数据消息
*输入参数: @pSysAgent 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: RETURN_OK执行成功，RETURN_ERROR执行失败
*/
static s32 diag_agentHandleMsgData(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    UNREFERENCE_PARAM(pSysInfo);
    UNREFERENCE_PARAM(pRecvMsg);
    UNREFERENCE_PARAM(pSocketAddr);
    
    return RETURN_OK;
}

/**
*函   数  名: diag_agentHandleMsgAlive
*功能描述: 处理心跳消息
*输入参数: @pSysAgent 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: RETURN_OK执行成功，RETURN_ERROR执行失败
*/
static s32 diag_agentHandleMsgAlive(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    UNREFERENCE_PARAM(pSysInfo);
    UNREFERENCE_PARAM(pRecvMsg);
    UNREFERENCE_PARAM(pSocketAddr);
    
    return RETURN_OK;
}

/**
*函   数  名: diag_agentHandleMsgCmd
*功能描述: 处理命令消息
*输入参数: @pSysAgent 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: RETURN_OK执行成功，RETURN_ERROR执行失败
*/
static s32 diag_agentHandleMsgPrompt(void* pSysInfo, DIAG_MSG_S* pRecvMsg, struct sockaddr_in *pSocketAddr)
{
    DIAG_SYS_AGENT_S* pSysAgent = (DIAG_SYS_AGENT_S*)pSysInfo;

    u32 thrd_idx;
    DIAG_SYS_CMD_THREAD_s *pCmdThrd = NULL;
 
    for (thrd_idx = 0; thrd_idx < MAX_DIAG_CMD_THREAD_NUM; thrd_idx++)
    {
        pCmdThrd = &pSysAgent->cmdThread[thrd_idx];
        if ((pCmdThrd->status == STATUS_CMD_PROMPT)
            && (pCmdThrd->recvBuffer.msgHead.clientId == pRecvMsg->msgHead.clientId)) 
        {
            memcpy(pCmdThrd->recvInputData, pRecvMsg->msgData, (ulong)MAX_DIAG_PROMPT_INPUT_LEN);
            memcpy(&(pCmdThrd->recvAddr.sockAddrIn), pSocketAddr, sizeof(struct sockaddr_in));

            DIAG_SEM_UP(&pCmdThrd->sem);
            return RETURN_OK;
        }
    }
    
    return RETURN_ERROR;
}

static s32 diag_agentInitCmdCtx(DIAG_SYS_CMD_THREAD_s *pCmdCtx, SOCKET socketFd, DIAG_AGENT_SOCKADDR_S* pRecvAddr)
{
    memset(pCmdCtx, 0, sizeof(DIAG_SYS_CMD_THREAD_s));

    //socket info
    pCmdCtx->socketFd = socketFd;
    pCmdCtx->pRecvAddr = pRecvAddr;
    diag_initMsgHead(&(pCmdCtx->sendBuffer.msgHead));

    //sync info
    DIAG_SEM_INIT(&pCmdCtx->sem, 0);

    //bind to current thread
    diag_agentSetCurCmdCtx(pCmdCtx);

    //init OK
    pCmdCtx->status = STATUS_IDLE;    
    pCmdCtx->threadStatus = DIAG_THREAD_STATUS_NORMAL;

    return RETURN_OK;
}

static s32 diag_agentClearCmdCtx(DIAG_SYS_CMD_THREAD_s *pCmdCtx)
{
    diag_agentSetCurCmdCtx(NULL);
    pCmdCtx->pRecvAddr = NULL;    
    return RETURN_OK;
}

/**
*函   数  名: diag_agentCmdThread
*功能描述: 消息处理线程
*输入参数: @pArg 线程入参
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static void diag_agentCmdThread(void* pArg)
{
    DIAG_SYS_AGENT_S* pSysAgent = diag_getCurrentSysInfo();
    DIAG_SYS_CMD_THREAD_s *pCmdThrd = (DIAG_SYS_CMD_THREAD_s *)pArg;
    u32 agentInitId = g_initIdTmp; //此处不可替换为DIAG_AGENT_INIT_ID()
    
    (void)diag_agentInitCmdCtx(pCmdThrd, pSysAgent->agent.socketFd, &(pCmdThrd->recvAddr));

    diag_threadSetName("diagAgtCmdThrd");
    (void)diag_setCurThreadPrio(DIAG_THRD_PRIO_HIGHEST);
    
    for(;;)
    {
        pCmdThrd->status = STATUS_CMD_INPUT;
        DIAG_SEM_DOWN(&pCmdThrd->sem);
        if((agentInitId != DIAG_AGENT_INIT_ID()) || (DIAG_THREAD_STATUS_NORMAL != pCmdThrd->threadStatus))
        {
            break;
        }

        if(STATUS_CMD_RUNNING != pCmdThrd->status)
            continue;

        (void)diag_agentHandleCmd(pSysAgent, pCmdThrd);
        (void)diag_agentHandleCmdDone(pSysAgent, pCmdThrd);
    }

    (void)diag_agentClearCmdCtx(pCmdThrd);
    pCmdThrd->threadStatus = DIAG_THREAD_STATUS_EXITED;
    DIAG_LOG(DIAG_LOG_INFO, "Cmd exec thread tid: %d initId: %u exit success", gettid(), agentInitId);
    return; 
}

/**
*函   数  名: diag_agentShellCmd
*功能描述: diagnose shell端对应的命令处理线程
*输入参数: @pArg 线程入参
*输出参数: 无
*返   回  值: 无
*/
void diag_agentShellCmd(void* pArg)
{
    s32 ret;
    SOCKET clientFd = (SOCKET)(u64)pArg;
    DIAG_SYS_CMD_THREAD_s* pCmdContext;
    DIAG_MSG_S* pRecvMsg;

    diag_threadSetName("diagShCmd");

    pCmdContext = (DIAG_SYS_CMD_THREAD_s*)malloc((u32)sizeof(DIAG_SYS_CMD_THREAD_s));
    if(NULL == pCmdContext)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Malloc recv context fail ret:%p, errno:%d", pCmdContext, errno);
        close(clientFd);
        return;
    }

    (void)diag_agentInitCmdCtx(pCmdContext, clientFd, NULL);
    pRecvMsg = &(pCmdContext->recvBuffer);
    pCmdContext->status = STATUS_CMD_RUNNING;    

    ret = diag_recvSocketTCP(clientFd, pRecvMsg, sizeof(DIAG_MSG_S));
    if(ret > 0)
    {
        (void)diag_agentHandleCmd(&g_sys_agent, pCmdContext);
        (void)diag_agentHandleCmdDone(&g_sys_agent, pCmdContext);
    }
    
    (void)diag_agentClearCmdCtx(pCmdContext);
    free(pCmdContext);
    close(clientFd);
    return;
}

static s32 diag_agentShellSocketCreate(SOCKET* pSocketFd, s32 socketType, char* pSunPath)
{
    s32 ret;
    
    ret = diag_initUnixDomainSocket(pSocketFd, socketType, pSunPath);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Init server socket addr fail ret:%d, errno:%d %s", ret, errno, pSunPath);
        return RETURN_ERROR;
    }        

    ret = listen(*pSocketFd, 10);
    if(ret < 0)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Set listen num fail ret:%d errno:%d %s", ret, errno, pSunPath);
        close(*pSocketFd);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

static s32 diag_agentShellSocketInit(DIAG_SYS_AGENT_S* pSysAgent)
{
#define MAX_RETRY_INTERVAL (60)    
    s32 serverFd;
    u32 retryInterval = 1;
    char unixDomainPath[MAX_PATH_NAME];

    snprintf(unixDomainPath, (u64)(MAX_PATH_NAME-1), DIAG_SERVER_SUN_PATH, \
        pSysAgent->agent.agentName, pSysAgent->agent.agentId);

    while(RETURN_OK != diag_agentShellSocketCreate(&serverFd, SOCK_STREAM, unixDomainPath))
    {
        sleep(retryInterval);
        retryInterval *= 2;
        retryInterval = (retryInterval <= MAX_RETRY_INTERVAL) ? retryInterval : MAX_RETRY_INTERVAL;
    }

    DIAG_LOG(DIAG_LOG_INFO, "Init shell socket %s success", unixDomainPath);
    return serverFd;
}

/**
*函   数  名: diag_agentShellListen
*功能描述: diagnose shell端对应的建链线程
*输入参数: @pArg 线程入参
*输出参数: 无
*返   回  值: 无
*/
void diag_agentShellListen(void* pArg)
{
    s32 ret;
    u32 agentInitId = g_initIdTmp; //此处不可替换为DIAG_AGENT_INIT_ID()
    DIAG_SYS_AGENT_S* pSysAgent = (DIAG_SYS_AGENT_S*)pArg;
    DIAG_AGENT_S* pAgent = &(pSysAgent->agent);
    s32 serverFd, clientFd;

    pAgent->shellThreadId = (u64)gettid();
    diag_threadSetName("diagShListen");
    (void)diag_setCurThreadPrio(DIAG_THRD_PRIO_HIGHEST);

    serverFd = diag_agentShellSocketInit(pSysAgent);
    if(INVALID_SOCKET == serverFd)
    {
        pAgent->shellThreadId = DIAG_INVALID_THREAD_ID;
        DIAG_LOG(DIAG_LOG_INFO, "Init diagnose listen socket fail, thread tid: %d initId: %u exit", gettid(), agentInitId);  
        return;
    }         

    for(;;)
    {
        if(agentInitId != DIAG_AGENT_INIT_ID())
        {
            break;
        }
        
        clientFd = diag_acceptSocket(serverFd);
        if(clientFd < 0)
        {
            continue;
        }

        ret = diag_switchThreadAsync(diag_agentShellCmd, (void*)(u64)(u32)clientFd, NULL);
        if(RETURN_OK != ret)
        {
            close(clientFd);
            DIAG_LOG(DIAG_LOG_INFO, "Create thread fail ret:%d, errno:%d", ret, errno);
            continue;
        }
    }

    close(serverFd);
    pAgent->shellThreadId = DIAG_INVALID_THREAD_ID;
    DIAG_LOG(DIAG_LOG_INFO, "Shell thread tid: %d initId: %u exit success", gettid(), agentInitId);
    return;
    
}


/**
*函   数  名: diag_agentSocketRecv
*功能描述: diagnose Agent端socket接收处理线程
*输入参数: @pArg 线程入参
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
void diag_agentSocketRecv(void* pArg)
{
    s32 ret;
    u32 agentInitId = g_initIdTmp; //此处不可替换为DIAG_AGENT_INIT_ID()
    DIAG_SYS_AGENT_S* pSysAgent = (DIAG_SYS_AGENT_S*)pArg;
    DIAG_AGENT_S* pAgent = &(pSysAgent->agent); 
    DIAG_MSG_S* pRecvMsg = &(pSysAgent->recvBuffer);    
    struct sockaddr_in* pSocketAddrRecv = &(pSysAgent->recvAddr);

    pAgent->recvThreadId = (u64)gettid();
    diag_threadSetName("diagAgtRvThrd");
    (void)diag_setCurThreadPrio(DIAG_THRD_PRIO_HIGHEST);

    for(;;)
    {
        /*正常退出*/
        if(agentInitId != DIAG_AGENT_INIT_ID())
        {
            break;
        }
        
        ret = diag_recvFromSocket(pAgent->socketFd, pSocketAddrRecv, pRecvMsg, (bool)false);
        if(RETURN_OK != ret)
        {
            continue;
        }

        ret = diag_isSocketAddrEqual(pSocketAddrRecv, &(pAgent->socketServerAddr));
        if(RETURN_OK != ret)
        {
            continue;
        }

        (void)diag_handleSocketMsg((void*)pSysAgent, g_agentMsgHandleTable, pRecvMsg, pSocketAddrRecv);
    }

    pAgent->recvThreadId = DIAG_INVALID_THREAD_ID;
    DIAG_LOG(DIAG_LOG_INFO, "Socket recv thread tid: %d initId: %u exit success", gettid(), agentInitId);
    return;
}

/**
*函 数 名: diag_agentGetCurrentCmd
*功能描述: diagnose Agent端查询当前正在执行的cmd
*输入参数: 无
*输出参数: 无
*返 回 值: 成功返回pCmdSelf，失败返回NULL
*/
DBG_REG_CMD_S* diag_agentGetCurrentCmd()
{
    DIAG_SYS_AGENT_S* pSysAgent = diag_getCurrentSysInfo();
    DIAG_SYS_CMD_THREAD_s* pCmdThrd = diag_agentGetCurCmdCtx();
    DBG_REG_CMD_S* pCmdBuffer = diag_agentGetCmdBuffer(pSysAgent);
    DBG_REG_CMD_S* pCmdSelf;
    DIAG_CMD_ARGS_S* pArgs = NULL;

    if (NULL == pCmdThrd)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Error: pCmdThrd is null.");
        return NULL;
    }

    pArgs = &(pCmdThrd->cmdArgs);  

    DIAG_SPIN_LOCK(&pSysAgent->spinlock);
    /*获取用户注册的命令*/
    pCmdSelf = diag_agentGetCmdByName(pCmdBuffer, pArgs->argv[0]);
    if(NULL == pCmdSelf)
    {
        DIAG_SPIN_UNLOCK(&pSysAgent->spinlock);
        DIAG_LOG(DIAG_LOG_ERROR, "Error: command `%s` not found.", pArgs->argv[0]);
        return NULL;
    }

    DIAG_SPIN_UNLOCK(&pSysAgent->spinlock);

    return pCmdSelf;
}

/**
*函   数  名: diag_agentLogout
*功能描述: 注销登录
*输入参数: @pSysAgent 指向agent端信息的指针
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_agentLogout(DIAG_SYS_AGENT_S* pSysAgent)
{
    s32 ret = 0;
    DIAG_MSG_SHORT_S logoutMsg;
    DIAG_AGENT_S* pAgent = &(pSysAgent->agent);

    diag_initMsgHead(&(logoutMsg.msgHead));

    logoutMsg.msgHead.agentId = pAgent->agentId;
    logoutMsg.msgHead.msgType = MSG_TYPE_CONTROL;
    logoutMsg.msgHead.msgOption = MSG_CONTROL_OPTION_AGENT_LOGOUT;
    logoutMsg.msgHead.dataLen = 0;

    ret = diag_logout(pAgent->socketFd, &(pAgent->socketServerAddr), (void *)&logoutMsg);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose Agent Logout fail");
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_agentCmdThreadExit
*功能描述: diagnose Agent端等待命令线程退出
*输入参数: @pSysAgent 指向agent端信息的指针
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentCmdThreadExit(DIAG_SYS_AGENT_S* pSysAgent)
{
    u32 index;
    u32 exitThreadCnt;
    DIAG_SYS_CMD_THREAD_s* pCmdThread = &(pSysAgent->cmdThread[0]);

    for(;;)
    {
        exitThreadCnt=0;        
        for(index = 0; index < MAX_DIAG_CMD_THREAD_NUM; index++)
        {
            if(DIAG_THREAD_STATUS_EXITED != pCmdThread[index].threadStatus)
            {
                pCmdThread[index].threadStatus = DIAG_THREAD_STATUS_EXITING;
                DIAG_SEM_UP(&(pCmdThread[index].sem));
            }
            else
            {
                exitThreadCnt++;
            }
        }

        if(MAX_DIAG_CMD_THREAD_NUM == exitThreadCnt)
        {
            break;
        }

        /*睡眠等待线程退出*/
        diag_msleep(DIAG_DEALY_INTERVAL);
    }

    return RETURN_OK;
}

static s32 diag_agentThreadInit(DIAG_SYS_AGENT_S* pSysAgent)
{
    s32 ret;
    u64 idx;    
    pthread_attr_t threadAttr;

    /*保存当前初始化流程的ID值，用于传递给新创建的线程*/
    g_initIdTmp = DIAG_AGENT_INIT_ID();
    
    ret = pthread_attr_init(&threadAttr);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose thread_attr initting failed errno=%d.", errno);
        return RETURN_ERROR;
    }

    ret = pthread_attr_setstacksize(&threadAttr, (size_t)DIAG_THREAD_STACK_SIZE);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose thread_attr setting stack size failed errno=%d.", errno);
        (void)pthread_attr_destroy(&threadAttr);
        return RETURN_ERROR;
    }

    /*创建shell listen线程*/    
    ret = diag_switchThreadAsync(diag_agentShellListen, pSysAgent, &threadAttr);
    if(RETURN_OK != ret)
    {
        (void)pthread_attr_destroy(&threadAttr);
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose agent create keep alive thread fail.");
        return RETURN_ERROR;
    }

    /* 创建cmd处理线程 */
    for (idx = 0; idx < MAX_DIAG_CMD_THREAD_NUM; idx++)
    {
        ret = diag_switchThreadAsync(diag_agentCmdThread, (void*)&(pSysAgent->cmdThread[idx]), &threadAttr);
        if(RETURN_OK != ret)
        {
            (void)pthread_attr_destroy(&threadAttr);
            DIAG_LOG(DIAG_LOG_ERROR, "Diagnose agent create cmd thread fail.");
            return RETURN_ERROR;
        }
    }

    /*创建心跳线程*/    
    ret = diag_switchThreadAsync(diag_agentKeepAlive, pSysAgent, &threadAttr);
    if(RETURN_OK != ret)
    {
        (void)pthread_attr_destroy(&threadAttr);
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose agent create keep alive thread fail.");
        return RETURN_ERROR;
    }

    /*创建socket处理线程*/
    ret = diag_switchThreadAsync(diag_agentSocketRecv, pSysAgent, &threadAttr);
    if(RETURN_OK != ret)
    {
        (void)pthread_attr_destroy(&threadAttr);
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose agent create keep alive thread fail.");
        return RETURN_ERROR;
    }

    (void)pthread_attr_destroy(&threadAttr);
    return RETURN_OK;
}

/**
*函   数  名: diag_agentInit
*功能描述: diagnose Agent端初始化
*输入参数: @agentId agent端id
           @pAgentName, agent name
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentInit(u32 agentId, char* pAgentName)
{
    s32 ret = 0;
    DIAG_SYS_AGENT_S* pSysAgent = &g_sys_agent;
    DIAG_AGENT_S* pAgent = &(pSysAgent->agent);
    
    if((DIAG_INVALID_ID == agentId) || (NULL == pAgentName))
    {
        DIAG_LOG(DIAG_LOG_ERROR, "diag_agentInit fail uiCurPid(%u) pAppName(%p)", agentId, pAgentName);
        return RETURN_ERROR;
    }
    
    /*diagnose功能依赖初始化，例如jiffies*/
    (void)diag_commInit();
	
    /*Agent端基本信息初始化，包括端口号绑定等*/
    ret = diag_agentInfoInit(pSysAgent);
    if(RETURN_OK != ret)
    {
        return RETURN_ERROR;
    }

    /*设置agent端Id和name*/
    ret = diag_agentInfoSet(pAgent, agentId, pAgentName);
    if(RETURN_OK != ret)
    {
        (void)shutdown(pAgent->socketFd, SHUT_RDWR);
        (void)close(pAgent->socketFd);
        return RETURN_ERROR;
    }

    /*登陆Socket Server*/
    (void)diag_agentLogin(pSysAgent);

    /*创建处理线程*/
    ret = diag_agentThreadInit(pSysAgent);
    if(RETURN_OK != ret)
    {
        (void)shutdown(pAgent->socketFd, SHUT_RDWR);
        (void)close(pAgent->socketFd);
        return RETURN_ERROR;
    }    
    
    DIAG_LOG(DIAG_LOG_INFO, "Diagnose agent init success. agentId: %u agentName: %s", agentId, pAgentName);
    return RETURN_OK;
}

/**
*函   数  名: diag_agentInit
*功能描述: diagnose Agent端初始化
*输入参数: @agentId agent端id
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_agentDestroy(u32 agentId)
{
    DIAG_SYS_AGENT_S* pSysAgent = &g_sys_agent;
    DIAG_AGENT_S* pAgent = &(pSysAgent->agent);
    
    UNREFERENCE_PARAM(agentId);

    /*等待正在执行的任务执行完成*/
    diag_msleep(DIAG_DEALY_INTERVAL);

    /*退出登录操作*/
    (void)diag_agentLogout(pSysAgent);

    /*关闭socket*/
    (void)shutdown(pAgent->socketFd, SHUT_RDWR);
    (void)close(pAgent->socketFd);
    pAgent->socketFd = DIAG_INVALID_SOCKET_FD;
  
    /*关闭创建的命令执行线程*/
    (void)diag_agentCmdThreadExit(pSysAgent);

    /*等待接收线程执行完成*/
    while(DIAG_INVALID_THREAD_ID != pAgent->recvThreadId)
    {
        diag_msleep(DIAG_DEALY_INTERVAL);
    }

    /*等待心跳线程执行完成*/
    while(DIAG_INVALID_THREAD_ID != pAgent->aliveThreadId)
    {
        diag_msleep(DIAG_DEALY_INTERVAL);
    }

    /*等待shell线程执行完成*/
    while(DIAG_INVALID_THREAD_ID != pAgent->shellThreadId)
    {
        diag_msleep(DIAG_DEALY_INTERVAL);
    }

    DIAG_LOG(DIAG_LOG_INFO, "Diagnose agent exit. agentId: %u agentName: %s", pAgent->agentId, pAgent->agentName);
    pAgent->agentId = DIAG_INVALID_ID;
    return RETURN_OK;
}
#endif

#if  DESC("对外函数")
/**
*函   数  CLI_AgentInitnit
*功能描述: diagnose Agent端初始化
*输入参数: @uiCurPid agent端id
           @pAgentName, agent name
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 CLI_AgentInit(u32 uiCurPid, char *pAppName)
{
    s32 ret = 0;

    DIAG_AGENT_INIT_LOCK();
    /*如果已经初始化了，仅将引用计数加1*/
    if(IS_DIAG_AGENT_INITED())
    {
        DIAG_AGENT_INIT_SUCCESS();
        DIAG_AGENT_INIT_UNLOCK();
        return RETURN_OK;
    }

    /*初始化diagnose agent 端*/
    ret = diag_agentInit(uiCurPid, pAppName);
    if(RETURN_OK == ret)
    {
        DIAG_AGENT_INIT_SUCCESS();
    }
    else
    {
        DIAG_AGENT_INIT_ID_UPDATE();
    }
    DIAG_AGENT_INIT_UNLOCK();
    
    return ret;    
}

/**
*函   数  名: CLI_Print
*功能描述: 对外提供的打印函数接口
*输入参数: @v_pchFormat 格式化字符串
                             @...  可变入参
*输出参数: 无
*返   回  值: 无
*/
void CLI_Print(const OSP_CHAR* v_pchFormat, ...)
{
    va_list pArgList;

    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "CLI_Print: Agent is not init");
        return ;
    }

    if(!IS_DIAG_AGENT_CMD_CONTEXT())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Invalid diagnose context: %s", v_pchFormat);
        return ;
    }

    /*将当前缓存区的内容，发送到客户端*/
    (void)diag_agentFlushCurrentBuf();

    /*即使是无效入参，也要刷新Buffer中内容*/
    if((NULL == v_pchFormat) || ('\0' == v_pchFormat[0]))
    {
        return ;
    }

    /*实际的打印操作*/
    va_start(pArgList, v_pchFormat);
    (void)diag_agentPrint(v_pchFormat, pArgList);
    va_end(pArgList);

    return ;
}

/**
*函   数  名: CLI_PrintBuf
*功能描述: 对外提供的同步打印函数接口
*输入参数: @v_pchFormat 格式化字符串
                             @...  可变入参
*输出参数: 无
*返   回  值: 无
*/
void CLI_PrintBuf(const OSP_CHAR* v_pchFormat, ...)
{   
    s32 ret;
    DIAG_SYS_CMD_THREAD_s* pCmdThrd = diag_agentGetCurCmdCtx();
    DIAG_PRINT_BUFFER_S* pPrintBuffer = NULL;
    va_list pArgList;
     bool can = false;
    
    if((NULL == v_pchFormat) || ('\0' == v_pchFormat[0]))
    {
        return ;
    }

    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "CLI_PrintBuf: Agent is not init");
        return ;
    }

    if(!IS_DIAG_AGENT_CMD_CONTEXT())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Invalid diagnose context: %s", v_pchFormat);
        return ;
    }

    pPrintBuffer = &(pCmdThrd->printBuffer);

     /*格式化消息内容*/
    va_start(pArgList, v_pchFormat);
    ret = vsnprintf(&(pPrintBuffer->buffer[pPrintBuffer->len]), \
        (ulong)(MAX_DIAG_PRINT_BUFFER_LEN - pPrintBuffer->len - 1), v_pchFormat, pArgList);
    va_end(pArgList);
    if(ret < 0)
    {
        DIAG_PRINT_LIMIT(can);
        
        if(can)              /*lint !e774*/
        {
            DIAG_LOG(DIAG_LOG_INFO, "CLI_PrintBuf failed: not enough free buffer");
        }
        return ;
    }   

    pPrintBuffer->len += (u32)ret;
    return ;
}

/**
*函   数  名: CLI_SendBuf
*功能描述: 将用于自定义的缓存区内容输出到客户端
*输入参数: @v_pchBuf 用户自定义缓存区地址
                             @v_uiSize 用户自定义缓存区大小
*输出参数: 无
*返   回  值: 无
*/
void CLI_SendBuf(const OSP_CHAR *v_pchBuf, OSP_U32 v_uiSize)
{
    if((NULL == v_pchBuf) || (0 == v_uiSize))
    {
        return ;
    }
    
    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "CLI_SendBuf: Agent is not init");
        return ;
    }

    if(!IS_DIAG_AGENT_CMD_CONTEXT())
    {
        DIAG_LOG(DIAG_LOG_INFO, "SendBuf in invalid diagnose context");
        return ;
    }

    (void)diag_agentFlushBuf((char*)v_pchBuf, v_uiSize);
    return;
}

/**
*函   数  名: CLI_ShowUsageAndErrMsg
*功能描述: 用于打印简单的错误信息
*输入参数: @v_pchFormat 格式化字符串
                             @... 可变入参
*输出参数: 无
*返   回  值: 无
*/
void CLI_ShowUsageAndErrMsg(const OSP_CHAR *v_pchFormat, ...)
{
    DBG_REG_CMD_S* pCmdSelf;
    va_list pArgList;
    
    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "CLI_ShowUsageAndErrMsg: Agent is not init");
        return ;
    }

    if(!IS_DIAG_AGENT_CMD_CONTEXT())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Invalid diagnose context: %s", v_pchFormat);
        return ;
    }

    /*将当前缓存区的内容，发送到客户端*/
    (void)diag_agentFlushCurrentBuf();

    /*错误信息打印*/
    if((NULL != v_pchFormat) && ('\0' != v_pchFormat[0]))
    {
        va_start(pArgList, v_pchFormat);
        (void)diag_agentPrint(v_pchFormat, pArgList);
        va_end(pArgList);
    }

    pCmdSelf = diag_agentGetCurrentCmd();
    if (NULL == pCmdSelf)
    {
        return ;
    }

    /* 执行用户注册的帮助命令 */
    DIAG_AGENT_RUN_CMD_START_CALLBACK(pCmdSelf, return);
    if (NULL != pCmdSelf->cmd.fnPrintCmdHelp)
    {
        if (NULL == v_pchFormat || '\0' == v_pchFormat[0])
        {
            CLI_Print("Usage: ");
        }
        else
        {
            CLI_Print("\nUsage: ");
        }
        pCmdSelf->cmd.fnPrintCmdHelp(pCmdSelf->cmd.szCommand, false);
    }
    CLI_Print("\nTry `help -c %s` for more information\n", pCmdSelf->cmd.szCommand);
    DIAG_AGENT_RUN_CMD_END(pCmdSelf);
    
    return ;
}

/**
*函   数  名: CLI_RegCmd
*功能描述: 命令注册接口
*输入参数: @v_pstCmd 注册的diagnose命令
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 CLI_RegCmd(CLI_CMD_S *v_pstCmd)
{
    s32 ret;
    CLI_CMD_S tmpCmd;  

    if(NULL == v_pstCmd)
    {
        return RETURN_ERROR;
    }

    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Agent is not init, reg cmd:%s fail", v_pstCmd->szCommand);
        return RETURN_ERROR;
    }

    tmpCmd = *v_pstCmd;

    diag_strToLower(tmpCmd.szCommand);

    ret = diag_agentIsValidCmd(&tmpCmd);
    if(RETURN_OK != ret)
    {
        return RETURN_ERROR;
    }

    ret = diag_agentRegCmd(&(g_sys_agent.cmdBufferNormal[0]), &tmpCmd, &(g_sys_agent.spinlock));
    return (RETURN_OK != ret) ? RETURN_ERROR : RETURN_OK;
}

/**
*函   数  名: CLI_UnRegCmd
*功能描述: 命令注销接口
*输入参数: @v_pstCmd 注册的diagnose命令
*输出参数: 无
*返   回  值: 无
*/
void CLI_UnRegCmd(char* v_szCommand)
{
    char tmpCommand[CLI_MAX_COMMAND_LEN] = ""; 
    
    if((NULL == v_szCommand) || ('\0' == v_szCommand[0]))
    {
        return ;
    }

    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Agent is not init");
        return ;
    }

    (void)strncpy(tmpCommand, v_szCommand,(u64)CLI_MAX_COMMAND_LEN - 1);

    diag_strToLower(tmpCommand);

    (void)diag_agentUnRegCmd(&(g_sys_agent.cmdBufferNormal[0]), tmpCommand, &(g_sys_agent.spinlock));
    
    return ;
}

/**
*函   数  名: CLI_AgtSyncStatus
*功能描述: 处理同步状态命令
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
void CLI_AgtSyncStatus(void)
{
    DIAG_SYS_CMD_THREAD_s* pCmdThrd = diag_agentGetCurCmdCtx();    
    
    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Agent is not init");
        return ;
    }

    if(NULL == pCmdThrd)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Not in diagnose context");
        return ;
    }
    
    (void)diag_agentHandleCmdDone(diag_getCurrentSysInfo(), pCmdThrd);
}

/**
*函   数  名: CLI_GetInput
*功能描述: 暂不支持
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
OSP_S32 CLI_GetInput(OSP_CHAR *v_szPrompt, OSP_CHAR *v_szInput, OSP_U32 v_uiMaxInputLen)
{
    s32 ret;
    s32 len;
    
    if ((NULL == v_szPrompt) || (NULL == v_szInput))
    {
        DIAG_LOG(DIAG_LOG_ERROR, "The param v_szPrompt: %p or v_szInput: %p is NULL", v_szPrompt, v_szInput);
        return RETURN_ERROR;
    }

    if(v_uiMaxInputLen <= 1)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Invalid input len: %u", v_uiMaxInputLen);
        return RETURN_ERROR;        
    }
    
    len = (s32)strlen(v_szPrompt);
    if ((len < 0) || (len >= MAX_DIAG_PROMPT_INTERACT_LEN))
    {
        DIAG_LOG(DIAG_LOG_ERROR, "The v_szPrompt param's len: %d is wrong.", len);
        return RETURN_ERROR;
    }
    
    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Agent is not init");
        return RETURN_ERROR;
    }

    if(!IS_DIAG_AGENT_CMD_CONTEXT())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Invalid diagnose context: %s", v_szPrompt);
        return RETURN_ERROR;
    }

    ret = diag_agentPromptInput(v_szPrompt, v_szInput, v_uiMaxInputLen);

    return ret;
}

/**
*函   数  名: CLI_RegCmdToCLI
*功能描述: 暂不支持
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
OSP_S32 CLI_RegCmdToCLI(CLI_CMD_S *v_pstCmd)
{
    s32 ret;
    CLI_CMD_S tmpCmd;  

    if(NULL == v_pstCmd)
    {
        return RETURN_ERROR;
    }

    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Agent is not init");
        return RETURN_ERROR;
    }

    tmpCmd = *v_pstCmd;

    diag_strToLower(tmpCmd.szCommand);

    ret = diag_agentIsValidCmd(&tmpCmd);
    if(RETURN_OK != ret)
    {
        return RETURN_ERROR;
    }

    ret = diag_agentRegCmd(&(g_sys_agent.cmdBufferCli[0]), &tmpCmd, &(g_sys_agent.spinlock));
    return (RETURN_OK != ret) ? RETURN_ERROR : RETURN_OK;
}

/**
*函   数  名: CLI_UnRegCmdToCLI
*功能描述: 暂不支持
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
void CLI_UnRegCmdToCLI(OSP_CHAR *v_szCommand)
{
    char tmpCommand[CLI_MAX_COMMAND_LEN] = ""; 
    
    if((NULL == v_szCommand) || ('\0' == v_szCommand[0]))
    {
        return ;
    }

    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Agent is not init");
        return ;
    }

    (void)strncpy(tmpCommand, v_szCommand,(u64)CLI_MAX_COMMAND_LEN - 1);

    diag_strToLower(tmpCommand);

    (void)diag_agentUnRegCmd(&(g_sys_agent.cmdBufferCli[0]), tmpCommand, &(g_sys_agent.spinlock));
    
    return ;
}

/**
*函   数  名: CLI_GetOpt
*功能描述: 对外提供的getopt接口
*输入参数: @v_iArgc ,参数数目
            @v_szArgv，参数内容
            @v_szOptString opt字符串
            @v_pstOpt，保存解析后的opt
*输出参数: 无
*返   回  值:  -1 选项处理完成。
                '?' 当前的选项字符不再合法选项列表中。
            CLI_RET_UNKNOWN_ARG 发现非选项字符串。
            其他 返回当前的选项字符。
*/
OSP_S32 CLI_GetOpt(OSP_S32 v_iArgc, OSP_CHAR *v_szArgv[], const OSP_CHAR *v_szOptString, CLI_OPT_S *v_pstOpt)
{
    OSP_S32 iRet = 0;
    GETOPT_THREAD_DATA_S td = {0};      /* 便于使用宏定义 */

    if ((NULL == v_szOptString) || (NULL == v_pstOpt))
    {
        return -1;
    }
    v_pstOpt->szErrMsg[0] = '\0';
    td._optopt = v_pstOpt->chOpt;
    td._optind = v_pstOpt->iOptIndex;
    td._optarg = v_pstOpt->szOptArg;  
    if (td._optopt != 0)
    {
       return -1;
    }
    iRet = open_getopt(&td, v_iArgc, v_szArgv, v_szOptString);
    v_pstOpt->chOpt = (char)td._optopt;
    v_pstOpt->iOptIndex = td._optind;
    v_pstOpt->szOptArg = td._optarg;
    strncpy(v_pstOpt->szErrMsg, td.errmsg, sizeof(v_pstOpt->szErrMsg));
    
    return iRet;
}

/**
*函   数  名: CLI_SetOpt
*功能描述: diagnose调试命令中，用于设置参数包含某个参数
*输入参数: @v_puiOptBits ,用于保存参数位标识的变量，取值：非空。
            @iOpt 参数只能是小写字母，取值：小写字母，其它字符不做任何处理。
*输出参数: 无
*返   回  值: 无
*/
void CLI_SetOpt(OSP_U32 *v_puiOptBits, OSP_S32 iOpt)
{
    if (NULL == v_puiOptBits) {
        return;
    }

    if (iOpt >= 'a' && iOpt <= 'z') {
        *v_puiOptBits |= (1U << (iOpt - 'a'));
    }
}

/**
*函   数  名: CLI_TestOpt
*功能描述: 测试参数是否包含某个参数，供用户在参数处理函数中调用，与\ref CLI_SetOpt相对应。
*输入参数: @v_puiOptBits ,用于保存参数位标识的变量，取值：非空。
            @iOpt 参数只能是小写字母，取值：小写字母，其它字符不做任何处理。
*输出参数: 无
*返   回  值: 0 表示不包含  非0 表示包含
*/
OSP_S32 CLI_TestOpt(OSP_U32 uiOptBits, OSP_S32 iOpt)
{
    if (iOpt >= 'a' && iOpt <= 'z') {
        return (OSP_S32)(uiOptBits & (1U << (iOpt - 'a')));
    }
    return 0;
}

#define IS_HEX_PREFIX(y) (('0' == (y)[0]) && (('x' == (y)[1]) || ('X' == (y)[1])))

/*****************************************************************************
 函 数 名  : dpax_str_isuint
 功能描述  : 判断整数是否是无符号整数
 输入参数  : const char *pStr ：输入的字符串
 输出参数  : 无
 返 回 值  : RETURN_OK : 是无符号整数
             RETURN_ERROR : 不是无符号整数
*****************************************************************************/
s32 DBG_StrIsUint(const char *pStr)
{
    s32 siIndex = 0;
    u32 siFlag = false;

    if (NULL == pStr)
    {
        return RETURN_ERROR;
    }

    if (IS_HEX_PREFIX(pStr))
    {
        siFlag = true;
        siIndex = 2;
    }

    for (;pStr[siIndex] != '\0'; siIndex++)
    {
        if (true == siFlag)
        {
            if (!isxdigit(pStr[siIndex] & 0xFF))
            {
                return RETURN_ERROR;
            }
        }
        else
        {
            if (!isdigit(pStr[siIndex] & 0xFF))
            {
                return RETURN_ERROR;
            }
        }
    }

    return RETURN_OK;
}

uint64_t DBG_StrToU64(const char *pStr)
{
    uint64_t uiValue = 0;    /* 转换后的值 */
    char* pscEnd = NULL;

    if (IS_HEX_PREFIX(pStr))
    {
        uiValue = strtoull(pStr, (char**)&pscEnd, 16);
        return uiValue;
    }

    /* 如果不是16进制，则直接当10进制处理 */
    uiValue = strtoull(pStr, (char**)&pscEnd, 10);

    return uiValue;
}

/**
*函   数  名: CLI_GetParamU64
*功能描述: 对外提供的字符串转换成u64接口
*输入参数: pszParam ,输入字符串

*输出参数: pullData，输出数字
*返   回  值: 无
*/
OSP_S32 CLI_GetParamU64(const OSP_CHAR *pszParam, OSP_U64 *pullData)
{
    OSP_S32 iRet;

    if ((NULL == pszParam) || (NULL == pullData))
    {

        DIAG_LOG(DIAG_LOG_INFO,"null pointer, pszParam(%p), pullData(%p).", pszParam, pullData);
        return RETURN_ERROR;
    }

    iRet = DBG_StrIsUint(pszParam);
    if (RETURN_OK != iRet)
    {
        return RETURN_ERROR;
    }

    *pullData = DBG_StrToU64(pszParam);

    return RETURN_OK;
}

/**
*函   数  名: CLI_GetParamU32
*功能描述: 对外提供的字符串转换成u32接口
*输入参数: pszParam ,输入字符串

*输出参数: puiData，输出数字
*返   回  值: 无
*/
OSP_S32 CLI_GetParamU32(const OSP_CHAR *pszParam, OSP_U32 *puiData)
{
    OSP_U64 ullData;
    OSP_S32 iRet;

    if ((NULL == pszParam) || (NULL == puiData))
    {
        DIAG_LOG(DIAG_LOG_INFO,"null pointer, pszParam(%p), puiData(%p).", pszParam, puiData);
        return RETURN_ERROR;
    }
    
    iRet = CLI_GetParamU64(pszParam, &ullData);
    if (RETURN_OK != iRet)
    {
        return iRet;
    }

    *puiData = (OSP_U32)ullData;

    return RETURN_OK;
}

/**
*函   数  名: CLI_GetParamPointer
*功能描述: 对外提供的字符串转换成指针
*输入参数: pszParam ,输入字符串

*输出参数: puiData，输出数字
*返   回  值: 无
*/
OSP_S32 CLI_GetParamPointer(const OSP_CHAR *pszParam, void **ppPointer)
{
    OSP_U64 ullData;
    OSP_S32 iRet;

    if ((NULL == pszParam) || (NULL == ppPointer))
    {
        DIAG_LOG(DIAG_LOG_INFO,"null pointer, pszParam(%p), ppPointer(%p).\n", pszParam, ppPointer);
        return RETURN_ERROR;
    }
    
    iRet = CLI_GetParamU64(pszParam, &ullData);
    if (RETURN_OK != iRet)
    {
        return iRet;
    }

    *ppPointer = (void *)(ptrdiff_t )ullData;

    return RETURN_OK;
}

/**
*函   数  名: CLI_PrintMemContext
*功能描述: 对外提供的打印内存接口,以16进制格式打印
*输入参数: v_pvAddr ,待打印内存地址
            v_uiLen， 待打印内存长度

*输出参数: 无
*返   回  值: 无
*/
void CLI_PrintMemContext(void *v_pvAddr, OSP_U32 v_uiLen)
{
    OSP_CHAR *pchLineStart;
    OSP_U32 uiLoop;
    OSP_U32 uiOffset;
    OSP_U32 uiLineSize;
    
    if ((NULL == v_pvAddr || 0 == v_uiLen))
    {
       CLI_Print("Input incorrect address or len, addr = 0x%p, len = %u\n", v_pvAddr, v_uiLen);
       return;        
    } 

    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_LOG(DIAG_LOG_INFO, "CLI_PrintMemContext: Agent is not init");
        return ;
    }

    if(!IS_DIAG_AGENT_CMD_CONTEXT())
    {
        DIAG_LOG(DIAG_LOG_INFO, "Invalid diagnose context: %s", v_pvAddr);
        return ;
    }

    /* 最大显示一个页面内容 */
    if (v_uiLen > MAX_DIAG_PAGE_SIZE)
    {
       CLI_Print("length %u too long, must <= 4160 \n", v_uiLen);
       return;
    }
    
    /* 总长度小于256则每行显示16字节 (16行)，否则每行显示32字节 */
    if (v_uiLen <= MAX_DIAG_MIDDLE_MEM_LEN)
    {
        uiLineSize = DIAG_EACH_LINE_LEN_SMALL;
    }
    else
    {
        uiLineSize = DIAG_EACH_LINE_LEN_LARGE;
    }

    /* 显示读取的数据信息 */
    pchLineStart = v_pvAddr;
    uiOffset = 0;
    
    while (pchLineStart < (OSP_CHAR *)v_pvAddr + v_uiLen)
    {
        CLI_PrintBuf("%04xh: ", uiOffset);
        for (uiLoop = 0; uiLoop < uiLineSize; uiLoop++)
        {
            if (uiOffset + uiLoop < v_uiLen)
            {
                CLI_PrintBuf("%02x ", (OSP_U8)pchLineStart[uiLoop]);
            }
            else
            {
                CLI_PrintBuf("   ");
            }

            /* 如果是一行显示32字节，在中间插入一个空格 */
            if (uiLoop == DIAG_EACH_LINE_LEN_SMALL)
            {
                CLI_PrintBuf(" ");
            }
        }

        CLI_PrintBuf("; "); /* 二进制表示和字符串表示分割符 */

        for (uiLoop = 0; uiLoop < uiLineSize; uiLoop++)
        {
            if ((uiOffset + uiLoop < v_uiLen) && (pchLineStart[uiLoop] > ' '))
            {
                CLI_PrintBuf("%c", pchLineStart[uiLoop]);
            }
            else
            {
                CLI_PrintBuf(".");
            }
        }

        uiOffset += uiLineSize;
        pchLineStart += uiLineSize;
        CLI_Print("\n");
    }              
    
}

/**
*函   数  名: CLI_AgentDestroy
*功能描述: diagnose Agent端销毁接口
*输入参数: @uiCurPid agent端id
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 CLI_AgentDestroy(u32 uiCurPid)
{
    s32 ret = RETURN_OK;

    DIAG_AGENT_INIT_LOCK();
    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_AGENT_INIT_UNLOCK();
        return RETURN_OK;
    }

    /*此处仅将引用计数减1*/
    DIAG_AGENT_DESTROY_SUCCESS();

    /*当引用计数为0时，才执行实际的销毁操作*/
    if(!IS_DIAG_AGENT_INITED())
    {
        DIAG_AGENT_INIT_ID_UPDATE();        
        ret = diag_agentDestroy(uiCurPid);
    }

    DIAG_AGENT_INIT_UNLOCK();
    return ret;    
}
#endif

