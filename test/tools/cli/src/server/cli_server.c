/*************************************************
  Copyright (C) Huawei Technologies Co., Ltd. 2015-2015. All rights reserved.  
  File name:      cli_server.c
  Author:	   ID:       Version:       Date: 
  Description:    cli Server端
  Others:          
  History:        
*************************************************/
#include <unistd.h>
#include "cli_server.h"
#ifdef __DIAG_USE_FDSA__
#include "fdsa_agent.h"
#endif


#ifdef __LLT_DIAGNOSE__
static u32 isServerExit = 0;
#endif

#ifdef __DIAG_LOG_PATH__
#define DIAGNOSE_SERVER_LOG_PATH "/var/log/infrastructure/diag_server"
#define DIAGNOSE_SERVER_LOG_NUM (150)
#endif

static DIAG_SYS_SERVER_S g_sys_server;

/*消息处理表，处理函数为NULL表示表结束*/
static DIAG_MSG_HANDLE_S g_serverMsgHandleTable[] = 
{ 
    {MSG_TYPE_CONTROL, 0, 0, diag_serverHandleMsgControl},
    {MSG_TYPE_CMD, 0, 0, diag_serverHandleMsgCmd},
    {MSG_TYPE_DATA, 0, 0, diag_serverHandleMsgData},
    {MSG_TYPE_ALIVE, 0, 0, diag_serverHandleMsgAlive},
    {MSG_TYPE_PROMPT, 0, 0, diag_serverHandleMsgPrompt},
    {0, 0, 0, NULL}
};

/*Server端自有命令*/
static DIAG_CMD_HANDLE_S g_serverCmdHandleTable[] = 
{
    {"ls",     "list Id of registed App",       diag_serverCmd_ls},
    {"attach", "attach to a running AppId",  diag_serverCmd_attach},
    {"help",   "show help information",          diag_serverCmd_help},
#ifdef __LLT_DIAGNOSE__
    {"serverexit",   "diagnose server exit",     diag_serverCmd_exit},
#endif
    {"",       "",                               NULL},
};

/**
*函   数  名: diag_getCurrentSysInfo
*功能描述: 获取当前处理流程使用的系统信息
*输入参数: 无
*输出参数: 无
*返   回  值: 返回当前流程使用的系统信息指针
*/
static inline DIAG_SYS_SERVER_S* diag_getCurrentSysInfo(void)
{
    return &g_sys_server;
}

/**
*函   数  名: diag_sendData
*功能描述: 发送socket数据
*输入参数: @format 格式化字符串
                            @... 可变入参
*输出参数: 
*返   回  值: 无
*/
void diag_serverPrint(const char* format, ...)
{
    s32 ret;
    va_list pArgList;
    DIAG_SYS_SERVER_S* pSysServer = diag_getCurrentSysInfo();
    DIAG_MSG_S* pSendMsg = &(pSysServer->sendBuffer);

    /*初始化发送消息头*/
    diag_initMsgHead(&(pSendMsg->msgHead));
    pSendMsg->msgHead.msgType = MSG_TYPE_DATA;
    pSendMsg->msgHead.msgOption = MSG_DATA_OPTION_SERVER;
    pSendMsg->msgHead.agentId = DIAG_INVALID_ID;
    pSendMsg->msgHead.clientId = pSysServer->recvBuffer.msgHead.clientId;

    /*格式化消息内容*/
    va_start(pArgList, format);
    ret = vsnprintf(&(pSendMsg->msgData[0]), (ulong)MAX_DIAG_BUFFER_LEN-1, format, pArgList);
    va_end(pArgList);
    if(ret <= 0)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Diagnose vsnprintf_s fail ret(%d)", ret);
        return;
    }
    pSendMsg->msgHead.dataLen = (u16)ret;

    /*发送消息*/
    ret = diag_sendToSocket(pSysServer->server.socketFd, &(pSysServer->recvAddr), (void*)pSendMsg);
    if(ret < 0)
    {
        DIAG_LOG(DIAG_LOG_INFO, "Diagnose socket send fail");
    }
}

/**
*函   数  名: diag_serverGetClientInfo
*功能描述: 获取指定clientId对应的信息
*输入参数: @pSysServer 指向Server端全部信息
                           @clientId 客户端Id
*输出参数: @ppFree 当返回值为NULL时,*ppFree用于保存指向空闲Buffer的指针, ppFree可以为NULL
*返   回  值: 成功返回指向clientId对应信息的指针，失败返回NULL
*/
static DIAG_CLIENT_INFO_S* diag_serverGetClientInfo(DIAG_SYS_SERVER_S* pSysServer, u32 clientId, DIAG_CLIENT_INFO_S** ppFree)
{
    s32 index;
    DIAG_CLIENT_INFO_S* pClientInfo = &(pSysServer->clientInfo[0]);
    DIAG_CLIENT_INFO_S* pFree = NULL;
    DIAG_CLIENT_INFO_S* pSelf = NULL;
    
    for(index = 0; index < MAX_DIAG_CLIENT_NUM; index++,pClientInfo++)
    {
        if(CONNECT_STATUS_ON != pClientInfo->status)
        {
            pFree = pClientInfo;
            continue;
        }

        if(clientId == pClientInfo->clientId)
        {
            pSelf = pClientInfo;
            break;
        }
    }

    /*ppFree允许为NULL*/
    if(NULL != ppFree)
    {
        *ppFree = pFree;
    }
    
    return pSelf;  
}

/**
*函   数  名: diag_serverHandleClientNormalCmdDone
*功能描述: 处理Client端命令完成
*输入参数: @pSysServer 指向Server端全部信息
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleClientCmdDone(DIAG_SYS_SERVER_S* pSysServer)
{
    s32 ret;
    SOCKET socketFd = pSysServer->server.socketFd;
    DIAG_MSG_SHORT_S cmdDoneMsg;

    diag_initMsgHead(&(cmdDoneMsg.msgHead));
    cmdDoneMsg.msgHead.msgType = MSG_TYPE_CMD;
    cmdDoneMsg.msgHead.msgOption = MSG_CMD_OPTION_AGENT_DONE;
    cmdDoneMsg.msgHead.clientId = pSysServer->recvBuffer.msgHead.clientId;
    cmdDoneMsg.msgHead.agentId = DIAG_INVALID_ID;
    
    ret = diag_sendToSocket(socketFd, &(pSysServer->recvAddr), (void*)(&cmdDoneMsg));
    if(ret < 0)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "normal cmd done send failed");
        return RETURN_ERROR;
    }

    return RETURN_OK; 
}

/**
*函   数  名: diag_serverGetAgentInfo
*功能描述: 获取指定agentId对应的信息
*输入参数: @pSysServer 指向Server端全部信息
                           @agentId Agent端Id
*输出参数: @ppFree 当返回值为NULL时,*ppFree用于保存指向空闲Buffer的指针, ppFree可以为NULL
*返   回  值: 成功返回指向agentId对应信息的指针，失败返回NULL
*/
static DIAG_AGENT_INFO_S* diag_serverGetAgentInfo(DIAG_SYS_SERVER_S* pSysServer, u32 agentId, DIAG_AGENT_INFO_S** ppFree)
{
    s32 index;
    DIAG_AGENT_INFO_S* pAgentInfo = &(pSysServer->agentInfo[0]);
    DIAG_AGENT_INFO_S* pFree = NULL;
    DIAG_AGENT_INFO_S* pSelf = NULL;
    
    for(index = 0; index < MAX_DIAG_AGENT_NUM; index++,pAgentInfo++)
    {
        if(CONNECT_STATUS_ON != pAgentInfo->status)
        {
            /* 保存空闲Info */
            if (NULL == pFree)
            {
                pFree = pAgentInfo;
            }
            continue;
        }

        if(agentId == pAgentInfo->agentId)
        {
            pSelf = pAgentInfo;
            break;
        }
    }

    /*ppFree允许为NULL*/
    if(NULL != ppFree)
    {
        *ppFree = pFree;
    }
    
    return pSelf;  
}

/**
*函   数  名: diag_serverCmd_ls
*功能描述: server端内部命令ls:显示在线的Agent端
*输入参数: @argc 参数个数
                           @argv 指向入参的指针
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
static s32 diag_serverCmd_ls(void* pSysInfo, s32 argc, char* argv[])
{
    s32 index;
    DIAG_SYS_SERVER_S* pSysServer = (DIAG_SYS_SERVER_S*)pSysInfo;
    DIAG_AGENT_INFO_S* pAgentInfo = NULL;
    
    UNREFERENCE_PARAM(argc);
    UNREFERENCE_PARAM(argv);
    
    diag_serverPrint("<AppId>        <State>            <AppName>\n");
    pAgentInfo = &(pSysServer->agentInfo[0]);
    for(index = 0; index < MAX_DIAG_AGENT_NUM; index++,pAgentInfo++)
    {
        if(CONNECT_STATUS_ON != pAgentInfo->status)
        {
            continue;
        }

        diag_serverPrint(" %-16u %-16u %s\n", pAgentInfo->agentId, pAgentInfo->status, pAgentInfo->agentName);
    }

    return DIAG_INNER_CMD_DONE;

}

/**
*函   数  名: diag_serverCheckInputString
*功能描述: 发送attach结果
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放待发送的消息
                           @pSocketAddr 目的地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverCheckInputString(char *inputString)
{
    u32 index = 0;
    while ('\0' != inputString[index])
    {
        if (('0' <= inputString[index])
             && ('9' >= inputString[index]))
        {
            index++;
            continue;
        }
        else
        {
            return RETURN_ERROR;
        }
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_serverSendAttachResult
*功能描述: 发送attach结果
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放待发送的消息
                           @pSocketAddr 目的地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverSendAttachResult(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pSendMsg, MSG_CONTROL_OPTION_E msgOption)
{
    s32 ret;
    SOCKET socketFd = pSysServer->server.socketFd;
        
    pSendMsg->msgHead.msgType = MSG_TYPE_CONTROL;
    pSendMsg->msgHead.msgOption = msgOption;
    
    ret = diag_sendToSocket(socketFd, &(pSysServer->recvAddr), (void*)pSendMsg);
    if(ret < 0)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "attach send socket fail");
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_serverCmd_attach
*功能描述: server端内部命令attach:和对应的Agent端建立连接
*输入参数: @argc 参数个数
                           @argv 指向入参的指针
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
static s32 diag_serverCmd_attach(void* pSysInfo, s32 argc, char* argv[])
{
    s32 ret;
    u32 attachId;
    DIAG_SYS_SERVER_S* pSysServer = (DIAG_SYS_SERVER_S*)pSysInfo;
    DIAG_MSG_S* pRecvMsg = &(pSysServer->recvBuffer);
    DIAG_AGENT_INFO_S* pAgentInfo = NULL;
    DIAG_CLIENT_INFO_S* pClientInfo = NULL;

    if (2 != argc)
    {
        diag_serverPrint("Invalid Arg number,you can only input two args.\n");
        return DIAG_INNER_CMD_ERROR;
    }

    ret = diag_serverCheckInputString(argv[1]);
    if(RETURN_OK != ret)
    {
        diag_serverPrint("Invalid AppId.\n");
        return DIAG_INNER_CMD_ERROR;
    }

    ret = atoi(argv[1]);
    if(ret < 0)
    {
        diag_serverPrint("Invalid AppId.\n");
        return DIAG_INNER_CMD_ERROR;
    }
    attachId = (u32)ret;

    pClientInfo = diag_serverGetClientInfo(pSysServer, pRecvMsg->msgHead.clientId, NULL);
    if(NULL == pClientInfo)
    {
        diag_serverPrint("Invalid client Id<%u>, please login again.\n", pRecvMsg->msgHead.clientId);
        return DIAG_INNER_CMD_ERROR;
    }

    if (DIAG_INVALID_ID != attachId)
    {
        pAgentInfo = diag_serverGetAgentInfo(pSysServer, attachId, NULL);
        if(NULL == pAgentInfo)
        {
            diag_serverPrint("Error attach: beyond domanial argument.\n");
            diag_serverPrint("Usage: attach AppId.\n");
            diag_serverPrint("Try `ls` to find available AppId.\n");
            (void)diag_serverSendAttachResult(pSysServer, pRecvMsg, MSG_CONTROL_OPTION_ATTACH_FAIL);
            return DIAG_INNER_CMD_ERROR;
        }
        diag_serverPrint("Attach AppId<%u> success\n", attachId);
    }
    else
    {
        diag_serverPrint("Detach success\n");
    }
    
    pClientInfo->attachedId = attachId;
    
    pRecvMsg->msgHead.agentId = attachId;
    (void)diag_serverSendAttachResult(pSysServer, pRecvMsg, MSG_CONTROL_OPTION_ATTACH_SUCCESS);
    return DIAG_INNER_CMD_DONE;
}

/**
*函   数  名: diag_serverHelpShowDefault
*功能描述: server端内部命令help:显示帮助信息
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
static void diag_serverHelpShowDefault(void)
{
    DIAG_CMD_HANDLE_S* pCmdHandle = &(g_serverCmdHandleTable[0]);

    diag_serverPrint("<command>        <description>\n");
    while(NULL != pCmdHandle->cmdHandle)
    {
        diag_serverPrint(" %-16s %s\n", pCmdHandle->pCmdName, pCmdHandle->pCmdDesc);
        pCmdHandle++;
    }

    return ;
}

/**
*函   数  名: diag_serverCmd_help
*功能描述: server端内部命令help:显示帮助信息
*输入参数: @argc 参数个数
                           @argv 指向入参的指针
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
static s32 diag_serverCmd_help(void* pSysInfo, s32 argc, char* argv[])
{
    DIAG_SYS_SERVER_S* pSysServer = (DIAG_SYS_SERVER_S*)pSysInfo;
    DIAG_MSG_S* pRecvMsg = &(pSysServer->recvBuffer);
    DIAG_CLIENT_INFO_S* pClientInfo = NULL;
    u32 attachedId;

    UNREFERENCE_PARAM(argc);
    UNREFERENCE_PARAM(argv);

    pClientInfo = diag_serverGetClientInfo(pSysServer, pRecvMsg->msgHead.clientId, NULL);
    if(NULL == pClientInfo)
    {
        diag_serverPrint("Invalid client Id<%u>, please login again\n", pRecvMsg->msgHead.clientId);
        return DIAG_INNER_CMD_ERROR;
    }

    attachedId = pClientInfo->attachedId;
    if(DIAG_INVALID_ID == attachedId)
    {
        diag_serverHelpShowDefault();
        return DIAG_INNER_CMD_DONE;
    }
    else
    {
        return DIAG_INNER_CMD_CONTINUE;
    }
        
}

#ifdef __LLT_DIAGNOSE__
/**
*函   数  名: diag_serverCmd_exit
*功能描述: server端内部命令exit:退出diagnose server
*输入参数: @argc 参数个数
                           @argv 指向入参的指针
*输出参数: 无
*返   回  值: RETURN_OK 内部命令执行成功
*/
static s32 diag_serverCmd_exit(void* pSysInfo, s32 argc, char* argv[])
{
    UNREFERENCE_PARAM(pSysInfo);
    UNREFERENCE_PARAM(argc);
    UNREFERENCE_PARAM(argv);

    isServerExit = 1;
    
    return RETURN_OK;        
}
#endif

/**
*函   数  名: diag_serverHandleAsServerCmd
*功能描述: 作为server端内部命令处理，包括ls、attach等
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
static s32 diag_serverHandleAsServerCmd(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret;
    DIAG_CMD_ARGS_S* pArgs = &(pSysServer->cmdArgs);

    UNREFERENCE_PARAM(pSocketAddr);
    
    ret = diag_getArgs(pRecvMsg, pArgs, MAX_DIAG_CMD_ARGCS_NUM);
    if(RETURN_OK != ret)
    {
        return DIAG_INNER_CMD_ERROR;
    }

    diag_strToLower(pArgs->argv[0]);

    ret = diag_executeAsInnerCmd((void*)pSysServer, &(g_serverCmdHandleTable[0]), pArgs->argc, pArgs->argv);
    return ret;
}

/**
*函   数  名: diag_serverTransClientToAgent
*功能描述: 转发Client端msg到Agent端
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverTransClientToAgent(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in* pSocketAddr)
{
    s32 ret;
    SOCKET socketFd = pSysServer->server.socketFd;    
    u32 clientId = pRecvMsg->msgHead.clientId;
    u32 attachedId = DIAG_INVALID_ID;
    DIAG_CLIENT_INFO_S* pClientInfo = NULL;
    DIAG_AGENT_INFO_S* pAgentInfo = NULL;

    UNREFERENCE_PARAM(pSocketAddr);
    
    pClientInfo = diag_serverGetClientInfo(pSysServer, clientId, NULL);
    if(NULL == pClientInfo)
    {
        diag_serverPrint("Invalid client Id<%u>, please login again.\n", clientId);
        return RETURN_ERROR;
    }

    attachedId = pClientInfo->attachedId;
    if(DIAG_INVALID_ID == attachedId)
    {
        diag_serverPrint("Please exec `attach <AppId>` first.\n");
        return RETURN_ERROR;
    }
 
    pAgentInfo = diag_serverGetAgentInfo(pSysServer, attachedId, NULL); 
    if(NULL == pAgentInfo)
    { 
        diag_serverPrint("Can't find attached AppId<%d>. make sure the agent is alive, and try again.\n", attachedId);
        return RETURN_ERROR;
    }
 
    pRecvMsg->msgHead.agentId = attachedId;
    ret = diag_sendToSocket(socketFd, &(pAgentInfo->socketAddr), (void*)pRecvMsg);
    if(ret < 0)
    {
        diag_serverPrint("Socket send failed\n");
        return RETURN_ERROR;
    }
 
    return RETURN_OK;    
}

/**
*函   数  名: diag_serverHandleClientNormalCmd
*功能描述: 处理Client端命令
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleClientNormalCmd(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret;
    
    ret = diag_serverTransClientToAgent(pSysServer, pRecvMsg, pSocketAddr);
    if(RETURN_OK != ret)
    {
        (void)diag_serverHandleClientCmdDone(pSysServer);
    }

    return ret;
}

/**
*函   数  名: diag_serverTransAgentToClient
*功能描述: 转发Agent端msg到Client端
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverTransAgentToClient(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret;
    SOCKET socketFd = pSysServer->server.socketFd;    
    u32 clientId = pRecvMsg->msgHead.clientId;
    u32 attachedId = DIAG_INVALID_ID;
    DIAG_CLIENT_INFO_S* pClientInfo = NULL;
    
    UNREFERENCE_PARAM(pSocketAddr);

    pClientInfo = diag_serverGetClientInfo(pSysServer, clientId, NULL);
    if(NULL == pClientInfo)
    {
        DIAG_LOG(DIAG_LOG_DEBUG, "client Id: %d not find", clientId);
        return RETURN_ERROR;
    }

    attachedId = pClientInfo->attachedId;
    if(attachedId != pRecvMsg->msgHead.agentId)
    {
        DIAG_LOG(DIAG_LOG_DEBUG, "Id: %d is not equal to attached AppId: %d", pRecvMsg->msgHead.agentId, attachedId);
        return RETURN_ERROR;
    }
    
    ret = diag_sendToSocket(socketFd, &(pClientInfo->socketAddr), (void*)pRecvMsg);
    if(ret < 0)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "agent done cmd send failed");
        return RETURN_ERROR;
    }

    return RETURN_OK;
}


/**
*函   数  名: diag_serverHandleAgentDoneCmd
*功能描述: 处理Agent端命令
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleAgentDoneCmd(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    return diag_serverTransAgentToClient(pSysServer, pRecvMsg, pSocketAddr);
}

/**
*函   数  名: diag_serverHandleMsgCmd
*功能描述: 处理命令类消息
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleMsgCmd(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret = RETURN_ERROR;
    DIAG_SYS_SERVER_S* pSysServer = (DIAG_SYS_SERVER_S*)pSysInfo;
    
    switch(pRecvMsg->msgHead.msgOption)
    {
    case MSG_CMD_OPTION_CLIENT_NORMAL:
    case MSG_CMD_OPTION_CLIENT_CLI:
        ret = diag_serverHandleAsServerCmd(pSysServer, pRecvMsg, pSocketAddr);
        if(DIAG_INNER_CMD_CONTINUE != ret)
        {
            (void)diag_serverHandleClientCmdDone(pSysServer);
            return RETURN_OK;
        }
        ret = diag_serverHandleClientNormalCmd(pSysServer, pRecvMsg, pSocketAddr);
        break;
    case MSG_CMD_OPTION_AGENT_DONE:
        ret = diag_serverHandleAgentDoneCmd(pSysServer, pRecvMsg, pSocketAddr);
        break;
    default:
        DIAG_LOG(DIAG_LOG_ERROR, "msgOption(%u) is wrong.", pRecvMsg->msgHead.msgOption);
        break;
    }

    return ret;
}

/**
*函   数  名: diag_serverHandleAgentData
*功能描述: 处理Agent端数据
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleAgentData(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    return diag_serverTransAgentToClient(pSysServer, pRecvMsg, pSocketAddr);  
} 

/**
*函   数  名: diag_serverHandleMsgData
*功能描述: 处理数据类消息
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleMsgData(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret = RETURN_ERROR;
    
    switch(pRecvMsg->msgHead.msgOption)
    {
    case MSG_DATA_OPTION_CLIENT:
        break;
    case MSG_DATA_OPTION_AGENT:
        ret = diag_serverHandleAgentData((DIAG_SYS_SERVER_S*)pSysInfo, pRecvMsg, pSocketAddr);
        break;
    default:
        DIAG_LOG(DIAG_LOG_ERROR, "msgOption(%u) is wrong.", pRecvMsg->msgHead.msgOption);
        break;
    }    
    
    return ret;
}

/**
*函   数  名: diag_serverHandleMsgPrompt
*功能描述: 处理交互式消息
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleMsgPrompt(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret = RETURN_ERROR;

    switch(pRecvMsg->msgHead.msgOption)
    {
     /* client发过来的交互消息，只需要转发给agent，由agent处理 */
    case MSG_DATA_OPTION_CLIENT:
        ret = diag_serverTransClientToAgent((DIAG_SYS_SERVER_S*)pSysInfo, pRecvMsg, pSocketAddr);
        break;
     /* agent发过来的交互消息，只需要转发client, 由client处理 */
    case MSG_DATA_OPTION_AGENT:
        ret = diag_serverTransAgentToClient((DIAG_SYS_SERVER_S*)pSysInfo, pRecvMsg, pSocketAddr);
        break;
    default:
        DIAG_LOG(DIAG_LOG_ERROR, "msgOption(%u) is wrong.", pRecvMsg->msgHead.msgOption);
        break;
    }    
    
    return ret;
}

/**
*函   数  名: diag_serverHandleClientAlive
*功能描述: 处理Client端心跳消息
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleClientAlive(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    u32 clientId = pRecvMsg->msgHead.clientId;
    DIAG_CLIENT_INFO_S* pClientInfo = NULL;
    DIAG_CLIENT_INFO_S* pClientFree = NULL;
    
    pClientInfo = diag_serverGetClientInfo(pSysServer, clientId, &pClientFree);
    if(NULL != pClientInfo)
    {
        pClientInfo->lastActiveMs = diag_getmillisec();        
        pClientInfo->socketAddr = *pSocketAddr;
        return RETURN_OK;
    }

    /*重新建立连接*/
    if(NULL != pClientFree)
    {
        pClientInfo = pClientFree;
        
        pClientInfo->attachedId = pRecvMsg->msgHead.agentId;
        pClientInfo->clientId = clientId;        
        pClientInfo->lastActiveMs = diag_getmillisec();        
        pClientInfo->socketAddr = *pSocketAddr;
        pClientInfo->status = CONNECT_STATUS_ON;
        return RETURN_OK;
    }
        
    return RETURN_ERROR;    
}

/**
*函   数  名: diag_serverHandleAgentAlive
*功能描述: 处理Agent端心跳消息
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleAgentAlive(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    u32 agentId = pRecvMsg->msgHead.agentId;
    DIAG_AGENT_INFO_S* pAgentInfo = NULL;
    DIAG_AGENT_INFO_S* pAgentFree = NULL;    

    pAgentInfo = diag_serverGetAgentInfo(pSysServer, agentId, &pAgentFree);
    if(NULL != pAgentInfo)
    {
        pAgentInfo->lastActiveMs = diag_getmillisec();
        pAgentInfo->socketAddr = *pSocketAddr;
        return RETURN_OK;
    }

    /*重新建立连接*/
    if(NULL != pAgentFree)
    {
        pAgentInfo = pAgentFree;

        pAgentInfo->agentId = agentId;    
        pAgentInfo->lastActiveMs = diag_getmillisec();
        pAgentInfo->socketAddr = *pSocketAddr;
        (void)strncpy(pAgentInfo->agentName, pRecvMsg->msgData, (ulong)(MAX_DIAG_AGENT_NAME-1));
        pAgentInfo->status = CONNECT_STATUS_ON;        
    }
    
    return RETURN_ERROR;    
}

/**
*函   数  名: diag_serverCheckClientAlive
*功能描述: Client端心跳检测
*输入参数: @pSysServer 指向Server端全部信息
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static void diag_serverCheckClientAlive(DIAG_SYS_SERVER_S* pSysServer)
{
    s32 index;
    DIAG_CLIENT_INFO_S* pClientInfo = &(pSysServer->clientInfo[0]);
    
    for(index = 0; index < MAX_DIAG_CLIENT_NUM; index++,pClientInfo++)
    {
        if(CONNECT_STATUS_ON != pClientInfo->status)
        {
            continue;
        }

        if(diag_getmillisec() > pClientInfo->lastActiveMs + DIAG_KEEP_ALIVE_TIMEOUT)
        {
            pClientInfo->status = CONNECT_STATUS_OFF;
        }
    }
}

/**
*函   数  名: diag_serverCheckAgentAlive
*功能描述: Agent端心跳检测
*输入参数: @pSysServer 指向Server端全部信息
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static void diag_serverCheckAgentAlive(DIAG_SYS_SERVER_S* pSysServer)
{
    s32 index;
    DIAG_AGENT_INFO_S* pAgentInfo = &(pSysServer->agentInfo[0]);    
    
    for(index = 0; index < MAX_DIAG_AGENT_NUM; index++,pAgentInfo++)
    {
        if(CONNECT_STATUS_ON != pAgentInfo->status)
        {
            continue;
        }

        if(diag_getmillisec() > pAgentInfo->lastActiveMs + DIAG_KEEP_ALIVE_TIMEOUT)
        {
            pAgentInfo->status = CONNECT_STATUS_OFF;
        }
    }
}


/**
*函   数  名: diag_serverCheckAlive
*功能描述: 心跳检测
*输入参数: @pSysServer 指向Server端全部信息
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static void diag_serverCheckAlive(DIAG_SYS_SERVER_S* pSysServer)
{
    diag_serverCheckClientAlive(pSysServer);
    diag_serverCheckAgentAlive(pSysServer);
}

/**
*函   数  名: diag_serverHandleMsgAlive
*功能描述: 处理心跳类消息
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleMsgAlive(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret = RETURN_OK;
    
    switch(pRecvMsg->msgHead.msgOption)
    {
    case MSG_ALIVE_OPTION_CLIENT:
        ret = diag_serverHandleClientAlive((DIAG_SYS_SERVER_S*)pSysInfo, pRecvMsg, pSocketAddr);
        break;
    case MSG_ALIVE_OPTION_AGENT:
        ret = diag_serverHandleAgentAlive((DIAG_SYS_SERVER_S*)pSysInfo, pRecvMsg, pSocketAddr);
        break;
    default:
        break;
    }    

    diag_serverCheckAlive((DIAG_SYS_SERVER_S*)pSysInfo);
    return ret;
}

/**
*函   数  名: diag_serverInit
*功能描述: diagnose Server端初始化
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverInit(void)
{
    s32 ret;
    DIAG_SYS_SERVER_S* pSysServer = &g_sys_server;    

    memset(pSysServer, 0, sizeof(DIAG_SYS_SERVER_S));
    ret = diag_initSocket(&(pSysServer->server.socketFd), DIAG_SERVER_SOCKET_PORT);
    if(RETURN_OK != ret)
    {
        return ret;
    }
    
    return RETURN_OK;
}

/**
*函   数  名: diag_serverSendLoginResult
*功能描述: 发送登陆结果
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放待发送的消息
                           @pSocketAddr 目的地址
                           @msgOption 登陆结果
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverSendLoginResult(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pSendMsg,  struct sockaddr_in*pSocketAddr, MSG_CONTROL_OPTION_E msgOption)
{
    s32 ret;
    SOCKET socketFd = pSysServer->server.socketFd;
    
    pSendMsg->msgHead.msgType = MSG_TYPE_CONTROL;
    pSendMsg->msgHead.msgOption = msgOption;
    
    ret = diag_sendToSocket(socketFd, pSocketAddr, (void*)pSendMsg);
    if(ret < 0)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "login send socket fail");
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_serverClientLogin
*功能描述: 处理Client端登陆命令
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverClientLogin(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in* pSocketAddr)
{
    s32 ret;
    u32 attachId;
    u32 clientId = (u32)ntohs(pSocketAddr->sin_port);
    DIAG_CLIENT_INFO_S* pClientInfo = NULL;    
    DIAG_CLIENT_INFO_S* pClientFree = NULL;
    MSG_CONTROL_OPTION_E msgOption;

    attachId = pRecvMsg->msgHead.agentId;
    pClientInfo = diag_serverGetClientInfo(pSysServer, clientId, &pClientFree);
    pClientInfo = (NULL == pClientInfo) ? pClientFree : pClientInfo;
    if(NULL != pClientInfo)
    {
        pClientInfo->attachedId = attachId;
        pClientInfo->clientId = clientId;        
        pClientInfo->lastActiveMs = diag_getmillisec();        
        pClientInfo->socketAddr = *pSocketAddr;
        pClientInfo->status = CONNECT_STATUS_ON;
        
        pRecvMsg->msgHead.clientId = clientId;
        msgOption = MSG_CONTROL_OPTION_LOGIN_SUCCESS;
    }
    else
    {
        pRecvMsg->msgHead.clientId = DIAG_INVALID_ID;
        msgOption = MSG_CONTROL_OPTION_LOGIN_BUSY;
    }
    
    /*发送登陆结果*/
    ret = diag_serverSendLoginResult(pSysServer, pRecvMsg, pSocketAddr, msgOption);
    if((NULL != pClientInfo) && (RETURN_OK != ret))
    {
        /*登陆失败回收资源*/
        pClientInfo->status = CONNECT_STATUS_OFF;
        pClientInfo->attachedId = DIAG_INVALID_ID;
    }
    
    DIAG_LOG(DIAG_LOG_INFO, "client login %s, clientId: %d", ((MSG_CONTROL_OPTION_LOGIN_SUCCESS == msgOption) && \
        (RETURN_OK == ret) ? "success" : "fail"), clientId);
    return ret;
}

/**
*函   数  名: diag_serverClientLogout
*功能描述: 处理Client端退出命令
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverClientLogout(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in* pSocketAddr)
{
    s32 ret;
    u32 clientId = pRecvMsg->msgHead.clientId;
    DIAG_CLIENT_INFO_S* pClientInfo = NULL;    

    pClientInfo = diag_serverGetClientInfo(pSysServer, clientId, NULL);
    if(NULL != pClientInfo)
    {
        pClientInfo->attachedId = DIAG_INVALID_ID;
        pClientInfo->status = CONNECT_STATUS_OFF;
    }

    pRecvMsg->msgHead.msgType = MSG_TYPE_CONTROL;
    pRecvMsg->msgHead.msgOption = MSG_CONTROL_OPTION_LOGOUT_SUCCESS;
    ret = diag_sendToSocket(pSysServer->server.socketFd, pSocketAddr, (void*)pRecvMsg);
    DIAG_LOG(DIAG_LOG_INFO, "client logout %s, clientId: %d", (ret < 0) ? "fail" : "success", clientId);
    return (ret < 0) ? RETURN_ERROR : RETURN_OK;
}

/**
*函   数  名: diag_serverAgentLogin
*功能描述: 处理Agent端登陆命令
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverAgentLogin(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret;
    u32 agentId = pRecvMsg->msgHead.agentId;
    DIAG_AGENT_INFO_S* pAgentInfo = NULL;
    DIAG_AGENT_INFO_S* pAgentFree = NULL;
    MSG_CONTROL_OPTION_E msgOption;
    bool successLogin = false;

    pAgentInfo = diag_serverGetAgentInfo(pSysServer, agentId, &pAgentFree);
    pAgentInfo = (NULL == pAgentInfo) ? pAgentFree : pAgentInfo;
    if(NULL != pAgentInfo)
    {
        pAgentInfo->agentId = agentId;    
        pAgentInfo->lastActiveMs = diag_getmillisec();
        pAgentInfo->socketAddr = *pSocketAddr;
        (void)strncpy(pAgentInfo->agentName, pRecvMsg->msgData, (ulong)(MAX_DIAG_AGENT_NAME-1));
        pAgentInfo->status = CONNECT_STATUS_ON;

        msgOption = MSG_CONTROL_OPTION_LOGIN_SUCCESS;
    }
    else
    {
        msgOption = MSG_CONTROL_OPTION_LOGIN_BUSY;
    }

    /*发送登陆结果*/
    ret = diag_serverSendLoginResult(pSysServer, pRecvMsg, pSocketAddr, msgOption);    
    if((NULL != pAgentInfo) && (RETURN_OK != ret))
    {
        /*登陆失败回收资源*/    
        pAgentInfo->status = CONNECT_STATUS_OFF;
    }

    DIAG_PRINT_LIMIT(successLogin); 
    
    if(successLogin)              /*lint !e774*/
    {
        DIAG_LOG(DIAG_LOG_INFO, "agent login %s, AppId: %d, name: %s", ((MSG_CONTROL_OPTION_LOGIN_SUCCESS == msgOption) && \
            (RETURN_OK == ret) ? "success" : "fail"), agentId, pRecvMsg->msgData);
    }
    
    return ret;
}

/**
*函   数  名: diag_serverAgentLogout
*功能描述: 处理Client端退出命令
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverAgentLogout(DIAG_SYS_SERVER_S* pSysServer, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in* pSocketAddr)
{
    s32 ret;
    u32 agentId = pRecvMsg->msgHead.agentId;
    DIAG_AGENT_INFO_S* pAgentInfo = NULL;    

    pAgentInfo = diag_serverGetAgentInfo(pSysServer, agentId, NULL);
    if(NULL != pAgentInfo)
    {
        pAgentInfo->status = CONNECT_STATUS_OFF;
    }

    pRecvMsg->msgHead.msgType = MSG_TYPE_CONTROL;
    pRecvMsg->msgHead.msgOption = MSG_CONTROL_OPTION_LOGOUT_SUCCESS;
    ret = diag_sendToSocket(pSysServer->server.socketFd, pSocketAddr, (void*)pRecvMsg);
    DIAG_LOG(DIAG_LOG_INFO, "agent logout %s, AppId: %d, name: %s", (ret < 0) ? "fail" : "success", \
        agentId, (NULL != pAgentInfo) ? (pAgentInfo->agentName) : "NULL");
    return (ret < 0) ? RETURN_ERROR : RETURN_OK;
}

/**
*函   数  名: diag_serverHandleMsgControl
*功能描述: 处理控制类消息
*输入参数: @pSysServer 指向Server端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverHandleMsgControl(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret = RETURN_OK;
    DIAG_SYS_SERVER_S* pSysServer = (DIAG_SYS_SERVER_S*)pSysInfo;
        
    switch(pRecvMsg->msgHead.msgOption)
    {
    case MSG_CONTROL_OPTION_CLIENT_LOGIN:
        /*处理登陆命令之前，删除不活动的节点*/
        diag_serverCheckClientAlive(pSysServer);
        ret = diag_serverClientLogin(pSysServer, pRecvMsg, pSocketAddr);
        break;
    case MSG_CONTROL_OPTION_AGENT_LOGIN:
        /*处理登陆命令之前，删除不活动的节点*/
        diag_serverCheckAgentAlive(pSysServer);
        ret = diag_serverAgentLogin(pSysServer, pRecvMsg, pSocketAddr);
        break;
    case MSG_CONTROL_OPTION_CLIENT_LOGOUT:
        ret = diag_serverClientLogout(pSysServer, pRecvMsg, pSocketAddr);
        break;
    case MSG_CONTROL_OPTION_AGENT_LOGOUT:
        ret = diag_serverAgentLogout(pSysServer, pRecvMsg, pSocketAddr);
        break;
    default:
        break;
    }
    
    return ret;
}

/**
*函   数  名: diag_serverRunning
*功能描述: diagnose Server端运行中
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_serverRunning(void)
{
    s32 ret;
    DIAG_SYS_SERVER_S* pSysServer = &g_sys_server;
    DIAG_MSG_S* pRecvMsg = &(pSysServer->recvBuffer);
    struct sockaddr_in* pSocketAddrRecv = &(pSysServer->recvAddr);

#ifdef __DIAG_USE_FDSA__    
    void *ptr_fdsa   = NULL;
    FDSA_FLOW_MONITOR_POLICY_S fdsa_flow_monitor_policy = {0};

    fdsa_flow_monitor_policy.maxCnt = 1;
    fdsa_flow_monitor_policy.timeout = DIAG_REFRESH_HEARTBEAT_PERIOD;
#endif
    
    for(;;)
    {
#ifdef __LLT_DIAGNOSE__
        if (1 == isServerExit)
        {
            break;
        }
#endif
        ret = diag_recvFromSocket(pSysServer->server.socketFd, pSocketAddrRecv, pRecvMsg, (bool)false);
        if(RETURN_OK != ret)
        {
            continue;
        }

#ifdef __DIAG_USE_FDSA__
        ptr_fdsa = FDSA_FlowMonitorStart(PID_DEBUG, DIAGNOSE_FDSA_FLOW_SERVER_HANDLE_MSG, &fdsa_flow_monitor_policy);        
#endif

        (void)diag_handleSocketMsg((void*)pSysServer, g_serverMsgHandleTable, pRecvMsg, pSocketAddrRecv);

#ifdef __DIAG_USE_FDSA__
        (void)FDSA_FlowMonitorEnd(ptr_fdsa);
#endif
    }

    return RETURN_ERROR; //lint !e527
}

/**
*函   数  名: main
*功能描述: main函数
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 main(s32 argc, char* argv[])
{   
    s32 ret,fd,num_fds;
    
    UNREFERENCE_PARAM(argc);
    UNREFERENCE_PARAM(argv);

    num_fds = getdtablesize();
    for (fd = 0; fd < num_fds; ++fd)
    {
        close(fd);
    }

    (void)diag_commInit();

#ifdef __DIAG_LOG_PATH__
    ret = dpax_log_file_path_set_ext(DIAGNOSE_SERVER_LOG_PATH);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "dplog set file path fail, ret: %d.", ret);
        dpax_log_destroy();
        return ret;
    }

    ret = dpax_log_backup_num_set(DIAGNOSE_SERVER_LOG_NUM);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "dplog set backup num fail, ret: %d.", ret);
        dpax_log_destroy();
        return ret;
    }
#endif

#ifdef __DIAG_USE_FDSA__
    if ((FDSA_OK != FDSA_AgentInit(PID_DEBUG)) 
        || (FDSA_OK != FDSA_AgentMonitorInit(PID_DEBUG, DIAGNOSE_FDSA_FLOW_CMD_BUTT, DIAG_REFRESH_HEARTBEAT_PERIOD)))
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose server init fdsa_agent failed.");
    }
#endif

    ret = diag_serverInit();
    if(RETURN_OK != ret)
    {
        // dpax_log_destroy();
        return ret;
    }

    DIAG_LOG(DIAG_LOG_INFO, "Diagnose server init sucess.");
    ret = diag_serverRunning();

    return ret;
}


