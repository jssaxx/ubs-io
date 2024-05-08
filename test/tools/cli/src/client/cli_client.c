/*************************************************
  Copyright (C) Huawei Technologies Co., Ltd. 2015-2015. All rights reserved.  
  File name:      cli_server.c
  Author:	   ID:       Version:       Date: 
  Description:    全用户态cli client端
  Others:          
  History:        
*************************************************/
#include "cli_client.h"


static DIAG_SYS_CLIENT_S g_sys_client;
static char g_szInputPrompt[MAX_DIAG_PROMPT_INTERACT_LEN] = ":";

/*消息处理表，处理函数为NULL表示表结束*/
static DIAG_MSG_HANDLE_S g_clientMsgHandleTable[] = 
{ 
    {MSG_TYPE_CONTROL, 0, 0, diag_clientHandleMsgControl},
    {MSG_TYPE_CMD,     0, 0, diag_clientHandleMsgCmd},
    {MSG_TYPE_DATA,    0, 0, diag_clientHandleMsgData},
    {MSG_TYPE_ALIVE,   0, 0, diag_clientHandleMsgAlive},
    {MSG_TYPE_PROMPT,  0, 0, diag_clientHandleMsgPrompt},
    {0, 0, 0, NULL}
};

/*Client端自有命令*/
static DIAG_CMD_HANDLE_S g_clientCmdHandleTable[] = 
{
    {"quit",     "quit client",       diag_clientCmd_exit},
    {"exit",     "the same as quit",  diag_clientCmd_exit},
    {".script",  "exec as script file", diag_clientCmd_script},
    {"",         "",                  NULL},
};

/*Client端入参解析*/
static DIAG_CLIENT_OPT_S g_clientOpts[] = 
{
    {"--help",         sizeof("--help")-1,      OPT_HELP},
    {"--auto",         sizeof("--auto")-1,      OPT_AUTO},
    {"--no-prompt",    sizeof("--no-prompt")-1, OPT_NO_PROMPT},
    {"--set-cli",      sizeof("--set-cli")-1,   OPT_SET_CLI},
    {"--set-debug",    sizeof("--set-debug")-1, OPT_SET_DEBUG},
    {"--script=",      sizeof("--script=")-1,   OPT_SCRIPT},
    {"--log-file=",    sizeof("--log-file=")-1, OPT_LOG_FILE},
    {"--server-port=", sizeof("--server-port=")-1, OPT_SERVER_PORT},
    {"--attach=",      sizeof("--attach=")-1,   OPT_ATTACH},
    {NULL, 0, OPT_BUTT},
};

/**
*函   数  名: diag_clientShowUsage
*功能描述: diagnose Client端显示帮助信息
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
static void diag_clientShowUsage(void)
{
    diag_clientPrint("%s",
    "Usage: diagnose [options]\n"\
    "  --help                : print help informations\n"\
    "  --auto                : auto run mode otherwise input characters during\n"\
    "                          execute command will be ignored\n"\
    "  --script=<filename>   : read command from <filename> instead of stdin\n"\
    "  --server-port=<port>  : connect to server port\n"\
    "  --log-file=<filename> : write log to file <filename> instead of default\n"\
    "  --set-debug           : set diagnose run in debug mode\n"\
    "  --set-cli             : set diagnose run in cli mode\n"\
    "  --no-prompt           : do not show a command line prompt\n"\
    "  --attach              : set attach agent id\n"
    );
}

/**
*函   数  名: diag_initProgramPath
*功能描述: diagnose Client端初始化进程路径信息
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_initProgramPath(DIAG_SYS_CLIENT_S* pSysClient)
{
#ifdef WIN32
    char* pProgramPath = &(pSysClient->client.programPath[0]);

    (void)GetModuleFileName(NULL, pProgramPath, (DWORD)MAX_PATH_NAME);
    
    return RETURN_OK;
#else
    long ret;
    char buf[MAX_PATH_NAME];
    char* pProgramPath = &(pSysClient->client.programPath[0]);

    ret = (long)sprintf(buf, "/proc/%d/exe", getpid());
    if(ret < 0)
    {
        diag_clientPrintAndSave("Can't get program path info\n");
        return RETURN_ERROR;
    }
    
    ret = readlink(buf, pProgramPath, (ulong)MAX_PATH_NAME);
    if(ret < 0)
    {
        diag_clientPrintAndSave("Readlink program path<%s> fail errno<%d>\n", pProgramPath, errno);
        return RETURN_ERROR;        
    }

    return RETURN_OK;
#endif
}

/**
*函   数  名: diag_clientInfoInit
*功能描述: diagnose Client端初始化
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientInfoInit(DIAG_SYS_CLIENT_S* pSysClient)
{
    int32_t ret;
    DIAG_CLIENT_S* pClient = &(pSysClient->client);
    DIAG_CLIENT_CFG_S* pClientCfg = &(pSysClient->config);

    /*初始化status和Id值*/
    pClient->status = STATUS_IDLE;
    pClient->id = DIAG_INVALID_ID;    
    pClient->logFile.fd = DIAG_INVALID_FD;
    pClient->processId = getpid();

    /*初始化自身路径*/
    (void)diag_initProgramPath(pSysClient);
    
    /*初始化Server端socket地址*/
    ret = diag_initSocketAddr(&(pClient->socketServerAddr), pClientCfg->serverPort);
    if(RETURN_OK != ret)
    {
        diag_clientPrint("init server socket addr fail port<%u>\n", pClientCfg->serverPort);
        return ret;
    }
    
    /*初始化Client端socket*/
    ret = diag_initSocket(&(pClient->socketFd), DIAG_RAND_SOCKET_PORT);
    if(RETURN_OK != ret)
    {
        diag_clientPrint("init client socket addr fail ret<%d>\n", ret);
        return ret;
    }    

    /*初始化attach的进程*/
    pClient->attachId = pClientCfg->attachId;
    /*初始化发送Buffer消息头*/
    diag_initMsgHead(&(pSysClient->sendBuffer.msgHead));

    return RETURN_OK;
}

/**
*函   数  名: diag_clientSetDefCfgInfo
*功能描述: diagnose Client端配置初始化
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientSetDefCfgInfo(DIAG_CLIENT_CFG_S* pClientCfg)
{
    int32_t ret;
    char* pUserName = getenv("USER");
    
    pClientCfg->flags &= ~(FLAGS_CLI_MODE | FLAGS_NO_PROMPT | FLAGS_AUTO_RUNNING);
    pClientCfg->serverPort = DIAG_SERVER_SOCKET_PORT;
    pClientCfg->attachId = DIAG_INVALID_ID;

    pClientCfg->logFileName[0] = '\0';
    pClientCfg->promptInteract[0] = '\0';
    if (pUserName == NULL) {
        (void)sprintf(pClientCfg->promptNormal, "mdogfs:/cli>");
    } else {
        ret = sprintf(pClientCfg->promptNormal, "%s:/cli>", pUserName);
        if(ret <= 0)
        {
            (void)sprintf(pClientCfg->promptNormal, "mdogfs:/cli>");
        }
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_clientGetopt
*功能描述: diagnose Client端解析入参信息
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static CLIENT_OPT_ID_E diag_clientGetopt(DIAG_CLIENT_OPT_S* pClientOpt, char* pParam, char** ppParam)
{
    int32_t ret;

    while(NULL != pClientOpt->pOpt)
    {
        ret = strncmp(pClientOpt->pOpt, pParam, (ulong)pClientOpt->optLen);
        if(0 != ret)
        {
            pClientOpt++;
            continue;
        }

        *ppParam = pParam + pClientOpt->optLen;
        return pClientOpt->id;   
    }

    return OPT_BUTT;
}

/**
*函   数  名: diag_clientParseCfgInfo
*功能描述: diagnose Client端解析配置信息
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientParseCfgInfo(DIAG_CLIENT_CFG_S* pClientCfg, int32_t argc, char* argv[])
{
    int32_t index;
    CLIENT_OPT_ID_E optId;
    char* pParam = NULL;
    int32_t port;
    int32_t attachId;

    for(index = 1; index < argc; index++)
    {
        optId = diag_clientGetopt(g_clientOpts, argv[index], &pParam);
        switch(optId)
        {
        case OPT_HELP:
            diag_clientShowUsage();
            exit(0); 
        case OPT_AUTO:
            pClientCfg->flags |= FLAGS_AUTO_RUNNING;
            break;
        case OPT_NO_PROMPT:
            pClientCfg->flags |= FLAGS_NO_PROMPT;
            break;
        case OPT_SET_CLI:
            pClientCfg->flags |= FLAGS_CLI_MODE;
            break;
        case OPT_SET_DEBUG:
            pClientCfg->flags &= (~FLAGS_CLI_MODE);
            break;
        case OPT_SCRIPT:
            (void)strcpy(pClientCfg->scriptFile, pParam);
            break;
        case OPT_SERVER_PORT:
            port = atoi(pParam);
            if((port > 0) && (port <= MAX_DIAG_U16_VALUE))
            {
                pClientCfg->serverPort = (u16)port;
            }
            else
            {
                diag_clientPrint("Set server port<%s>, use default port<%d>\n", pParam, (int)DIAG_SERVER_SOCKET_PORT);
            }
            break;  
        case OPT_ATTACH:
            attachId = (int32_t)atoi(pParam);
            if(attachId >= 0)
            {
                pClientCfg->attachId = (u32)attachId;
            }
            break; 
        case OPT_LOG_FILE:
            if('\0' != pParam[0])
            {
                diag_clientPrint("Specify log outfile is not supported yet\n");
            }
            break;
        default:
            diag_clientPrint("Invalid param<%s>\n", argv[index]);
            break; 
        } 
    }
    
    return RETURN_OK;
}

/**
*函   数  名: diag_clientProcScriptFile
*功能描述: diagnose Client端处理输入脚本
*输入参数: @pScriptFile 输入的脚本文件
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败直接退出
*/
static int32_t diag_clientProcScriptFile(char* pScriptFile)
{
    int32_t fd;
    
    if('\0' == pScriptFile[0])
    {
        return RETURN_OK;
    }

    fd = open(pScriptFile, O_RDONLY, 0);
    if(fd < 0)
    {
        diag_clientPrint("Can't open script file<%s>\n", pScriptFile);
        exit(-1);
    }

    (void)dup2(fd, 0);
    close(fd);

    return RETURN_OK;
}

/**
*函   数  名: diag_clientProcLogFile
*功能描述: diagnose Client端处理日志文件
*输入参数: @pLogFile 输入的日志文件
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败直接退出
*/
static int32_t diag_clientProcLogFile(char* pLogFile)
{
    char tmpLogFile[MAX_PATH_NAME];
    
    if('\0' == pLogFile[0])
    {
        return RETURN_OK;
    }

    (void)strcpy(tmpLogFile, pLogFile);
    (void)diag_cmd_mkdir(dirname(tmpLogFile));
    return RETURN_OK;
}

/**
*函   数  名: diag_clientPreProcCfgInfo
*功能描述: diagnose Client端预处理配置项
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientPreProcCfgInfo(DIAG_CLIENT_CFG_S* pClientCfg)
{
    /*如果指定了脚本文件，则默认自动处理*/
    if('\0' != pClientCfg->scriptFile[0])
    {
        pClientCfg->flags |= FLAGS_AUTO_RUNNING;
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_clientProcCfgInfo
*功能描述: diagnose Client端处理配置项
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientProcCfgInfo(DIAG_CLIENT_CFG_S* pClientCfg)
{
    (void)diag_clientPreProcCfgInfo(pClientCfg);
    
    (void)diag_clientProcScriptFile(pClientCfg->scriptFile);
    (void)diag_clientProcLogFile(pClientCfg->logFileName);

    return RETURN_OK;
}


/**
*函   数  名: diag_clientCfgInit
*功能描述: diagnose Client端配置初始化
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientCfgInit(DIAG_CLIENT_CFG_S* pClientCfg, int32_t argc, char* argv[])
{
    int32_t ret;

    ret = diag_clientSetDefCfgInfo(pClientCfg);
    if(RETURN_OK != ret)
    {
        return RETURN_ERROR;
    }

    ret = diag_clientParseCfgInfo(pClientCfg, argc, argv);
    if(RETURN_OK != ret)
    {
        return RETURN_ERROR;
    }

    ret = diag_clientProcCfgInfo(pClientCfg);
    return ret;
}

/**
*函   数  名: diag_clientLogin
*功能描述: diagnose Client登陆
*输入参数: @pSysClient 指向diagnose客户端全部信息
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientLogin(DIAG_SYS_CLIENT_S* pSysClient)
{
    int32_t ret;
    DIAG_CLIENT_S* pClient = &(pSysClient->client);
    DIAG_MSG_S* pSendMsg = &(pSysClient->sendBuffer);
    DIAG_MSG_S* pRecvMsg = &(pSysClient->recvBuffer);

    pSendMsg->msgHead.msgType = MSG_TYPE_CONTROL;
    pSendMsg->msgHead.msgOption = MSG_CONTROL_OPTION_CLIENT_LOGIN;
    pSendMsg->msgHead.dataLen = 0;
    pSendMsg->msgHead.agentId = pClient->attachId;

    ret = diag_login(pClient->socketFd, &(pClient->socketServerAddr), pSendMsg, pRecvMsg);
    if(MSG_CONTROL_OPTION_LOGIN_BUSY == ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose Client Login fail, server is busy, please wait");
        diag_clientPrint("client login fail, server is busy, please wait\n");
        return RETURN_ERROR;
    }
    
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose Client Login fail");
        diag_clientPrint("client login fail\n");
        return RETURN_ERROR;
    }

    pClient->id = pRecvMsg->msgHead.clientId;
    pClient->status = STATUS_CMD_INPUT;
    pSendMsg->msgHead.clientId = pClient->id;
    
    return RETURN_OK;
}

/**
*函   数  名: diag_clientLogout
*功能描述: diagnose Client退出登陆
*输入参数: @pSysClient 指向diagnose客户端全部信息
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientLogout(DIAG_SYS_CLIENT_S* pSysClient)
{
    int32_t ret;
    DIAG_CLIENT_S* pClient = &(pSysClient->client);
    DIAG_MSG_S* pSendMsg = &(pSysClient->sendBuffer);

    pSendMsg->msgHead.msgType = MSG_TYPE_CONTROL;
    pSendMsg->msgHead.msgOption = MSG_CONTROL_OPTION_CLIENT_LOGOUT;
    pSendMsg->msgHead.dataLen = 0;

    ret = diag_logout(pClient->socketFd, &(pClient->socketServerAddr), (void *)pSendMsg);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose Client Logout fail");
        diag_clientPrint("client logout fail\n");
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_clientKeepAlive
*功能描述: diagnose Client端心跳线程
*输入参数: @pArg 线程入参
*输出参数: 无
*返   回  值: 无
*/
void diag_clientKeepAlive(void* pArg)
{
    DIAG_MSG_SHORT_S keepAliveMsg;
    DIAG_SYS_CLIENT_S* pSysClient = (DIAG_SYS_CLIENT_S*)pArg;
    DIAG_CLIENT_S* pClient = &(pSysClient->client);

    diag_initMsgHead(&(keepAliveMsg.msgHead));
    
    keepAliveMsg.msgHead.msgType = MSG_TYPE_ALIVE;
    keepAliveMsg.msgHead.msgOption = MSG_ALIVE_OPTION_CLIENT;
    keepAliveMsg.msgHead.clientId = pClient->id;

    diag_threadSetName("diagCliAlvThrd");
    (void)diag_setCurThreadPrio(DIAG_THRD_PRIO_HIGHEST);

    for (;;)
    {
        /* 如果状态是logout，不再发送心跳 */
        if (STATUS_LOGOUT == pClient->status)
        {
            break;
        }
        
        keepAliveMsg.msgHead.agentId = pClient->attachId;
        (void)diag_sendToSocket(pClient->socketFd, &(pClient->socketServerAddr), (void*)(&keepAliveMsg));

        diag_msleep(DIAG_KEEP_ALIVE_INTERVAL);
    }

    return;
}

static int32_t diag_clientHandleMsgControl(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    DIAG_SYS_CLIENT_S* pSysServer = (DIAG_SYS_CLIENT_S*)pSysInfo;
    DIAG_CLIENT_S* pClient = &(pSysServer->client);
    u32 attachId;

    UNREFERENCE_PARAM(pSocketAddr);

    switch(pRecvMsg->msgHead.msgOption)
    {
    case MSG_CONTROL_OPTION_LOGOUT_SUCCESS:
        pClient->status = STATUS_IDLE;
        pClient->id = DIAG_INVALID_ID;
        pSysServer->sendBuffer.msgHead.clientId = DIAG_INVALID_ID;
        break;
    case MSG_CONTROL_OPTION_ATTACH_SUCCESS:
        attachId = pRecvMsg->msgHead.agentId;
        pClient->attachId = attachId;
        pSysServer->sendBuffer.msgHead.agentId = attachId;
        break;
    default:
        break;
    }
    return RETURN_OK;
}

/**
*函   数  名: diag_clientExecuteCmdDone
*功能描述: 命令执行完成
*输入参数: @pSysClient 指向Client端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientExecuteCmdDone(DIAG_SYS_CLIENT_S* pSysClient, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    UNREFERENCE_PARAM(pRecvMsg);
    UNREFERENCE_PARAM(pSocketAddr);
    
    pSysClient->client.status = STATUS_CMD_INPUT;
    return RETURN_OK;
}

/**
*函   数  名: diag_clientHandleMsgCmd
*功能描述: client端命令处理
*输入参数: @pSysInfo 指向Client端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientHandleMsgCmd(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    int32_t ret = RETURN_ERROR;

    switch(pRecvMsg->msgHead.msgOption)
    {
    case MSG_CMD_OPTION_AGENT_DONE:
        ret = diag_clientExecuteCmdDone((DIAG_SYS_CLIENT_S*)pSysInfo, pRecvMsg, pSocketAddr);
        break;
    default:
        break;
    }

    return ret;
}

/**
*函   数  名: diag_clientHandleMsgData
*功能描述: client端数据处理
*输入参数: @pSysInfo 指向Client端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientHandleMsgData(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    DIAG_SYS_CLIENT_S* pSysClient = (DIAG_SYS_CLIENT_S*)pSysInfo;

    UNREFERENCE_PARAM(pSocketAddr);

    diag_clientPrint("%s", pRecvMsg->msgData);
    diag_clientDoWriteLog(pRecvMsg->msgData, pRecvMsg->msgHead.dataLen);
    pSysClient->client.lastCmdActiveMs = diag_getmillisec();

    return RETURN_OK;
}

/**
*函   数  名: diag_clientHandleMsgData
*功能描述: client端心跳处理， 目前架构无需处理
*输入参数: @pSysInfo 指向Client端全部信息
                           @pRecvMsg 存放接收到的消息
                           @pSocketAddr 消息来源地址
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientHandleMsgAlive(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    UNREFERENCE_PARAM(pSysInfo);
    UNREFERENCE_PARAM(pRecvMsg);
    UNREFERENCE_PARAM(pSocketAddr);
    return RETURN_OK;
}

static int32_t diag_clientHandleMsgPrompt(void* pSysInfo, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    DIAG_SYS_CLIENT_S* pSysClient = (DIAG_SYS_CLIENT_S*)pSysInfo;

    UNREFERENCE_PARAM(pSocketAddr);

    (void)strncpy(g_szInputPrompt, pRecvMsg->msgData, sizeof(g_szInputPrompt) - 1);
    g_szInputPrompt[sizeof(g_szInputPrompt) - 1] = '\0';

    pSysClient->client.status = STATUS_CMD_PROMPT;    
    diag_clientDoWriteLog(pRecvMsg->msgData, pRecvMsg->msgHead.dataLen);
    pSysClient->client.lastCmdActiveMs = diag_getmillisec();

    return RETURN_OK;
}

/**
*函   数  名: diag_clientSocketRecv
*功能描述: diagnose Client端socket接收线程
*输入参数: @pArg 线程入参
*输出参数: 无
*返   回  值: 无
*/
void diag_clientSocketRecv(void* pArg)
{
    int32_t ret;
    struct sockaddr_in socketAddrRecv;
    DIAG_SYS_CLIENT_S* pSysClient = (DIAG_SYS_CLIENT_S*)pArg;
    DIAG_CLIENT_S* pClient = &(pSysClient->client);    
    DIAG_MSG_S* pRecvMsg = &(pSysClient->recvBuffer);

    diag_threadSetName("diagCliRcvThrd");
    (void)diag_setCurThreadPrio(DIAG_THRD_PRIO_HIGHEST);

    for(;;)
    {
        ret = diag_recvFromSocket(pClient->socketFd, &socketAddrRecv, pRecvMsg, (bool)false);
        if(RETURN_OK != ret)
        {
            continue;
        }

        ret = diag_isSocketAddrEqual(&socketAddrRecv, &(pClient->socketServerAddr));
        if(RETURN_OK != ret)
        {
            continue;
        }

        (void)diag_handleSocketMsg((void*)pSysClient, g_clientMsgHandleTable, pRecvMsg, &socketAddrRecv);
    }
}


/**
*函   数  名: diag_clientInit
*功能描述: diagnose Client端初始化
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientInit(DIAG_SYS_CLIENT_S* pSysClient)
{
    int32_t ret;

    /*Client端基本信息初始化*/
    ret = diag_clientInfoInit(pSysClient);
    if(RETURN_OK != ret)
    {
        return RETURN_ERROR;
    }

    /*登陆Socket Server*/
    ret = diag_clientLogin(pSysClient);
    if(RETURN_OK != ret)
    {
        close(pSysClient->client.socketFd);
        return RETURN_ERROR;
    }

    ret = diag_switchThreadAsync(diag_clientKeepAlive, pSysClient, NULL);
    if(RETURN_OK != ret)
    {
        (void)diag_clientLogout(pSysClient);
        close(pSysClient->client.socketFd);        
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose client create keep alive thread fail.");
        diag_clientPrint("create keep alive thread fail\n");
        return RETURN_ERROR;
    }

    ret = diag_switchThreadAsync(diag_clientSocketRecv, pSysClient, NULL);
    if(RETURN_OK != ret)
    {
        (void)diag_clientLogout(pSysClient);
        close(pSysClient->client.socketFd);        
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose client create keep alive thread fail.");
        diag_clientPrint("create socket recv thread fail\n");
        return RETURN_ERROR;
    }
    
    return RETURN_OK;
}


/**
*函   数  名: diag_clientClearInputBuf
*功能描述: diagnose Client端清空输入buffer
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
static void diag_clientClearInputBuf(void)
{
    int32_t ret;
    char ch;
    struct timeval timeout;
    fd_set fdset;

    DIAG_CLIENT_CFG_S * pClientCfg = &(g_sys_client.config);

    /*自动运行模式，不清缓存区*/
    if(pClientCfg->flags & FLAGS_AUTO_RUNNING)
    {
        return;
    }

    /* 清空输入缓存区*/
    timeout.tv_sec = 0;
    timeout.tv_usec = 1;
    for (;;)
    {
        FD_ZERO(&fdset);
        FD_SET(STDIN_FILENO, &fdset); //lint !e845 !e835

        ret = select(STDIN_FILENO+1, &fdset, NULL, NULL, &timeout); //lint !e835
        if ((ret <= 0) || !FD_ISSET(STDIN_FILENO, &fdset))  //lint !e845 !e835
        {
            break;
        }

        ssize_t readRet = read(STDIN_FILENO, &ch, (size_t)1);
        if (readRet == -1) {
            break;
        }
    }

    return;
}

/**
*函   数  名: diag_clientGetChar
*功能描述: 读取一个字符
*输入参数: 无
*输出参数: 无
*返   回  值: 读取到的字符，-1表示失败
*/
int32_t diag_clientGetChar(void)
{
    int32_t ch;
    struct termios oldTermios, newTermios;

    /*保存原有配置*/
    (void)tcgetattr(STDIN_FILENO, &oldTermios);

    /*删除标准输入模式和输出*/
    newTermios = oldTermios;
    newTermios.c_lflag &= ~(ICANON | ECHO);
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);
    
    ch = getchar();
    if(0 > ch)
    {	 	 
        DIAG_LOG(DPLOG_LVL_INFO,"cli_client can not exec in background，errno (%d).", errno);	 	 
        exit(0);	 	 
    }
    
    /*恢复原有配置*/
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &oldTermios);
    
    return ch;
}

/**
*函   数  名: diag_clientGetChar
*功能描述: 读取一个字符
*输入参数: 无
*输出参数: 无
*返   回  值: 读取到的字符，-1表示失败
*/
int32_t diag_clientGetInput(char* pBuffer, u32 bufferSize)
{    
    char ch;
    u64 cnt = 0;
    DIAG_SYS_CLIENT_S* pSysClient = &g_sys_client;
    
    memset(pBuffer, '\0', (ulong)bufferSize);
    bufferSize--;
    while(('\n' != (ch = (char)diag_clientGetChar())))
    {
        switch(ch)
        {
        case LINUX_KEY_BS:
        case LINUX_KEY_CTRL_BS:
            if(cnt > 0)
            {
                printf("\b \b");
                cnt--;
            }
            break;
            
        case LINUX_KEY_SPACE:
        case LINUX_KEY_TAB:
            printf("%c", LINUX_KEY_SPACE);
            if (cnt < (u64)bufferSize)
            {
                pBuffer[cnt] = LINUX_KEY_SPACE;
            }
            cnt++;
            break;
            
        case LINUX_KEY1:
            if ((ch = (char)diag_clientGetChar()) != '\n' && LINUX_KEY2 == ch)
            {
                ch = (char)diag_clientGetChar();
            }
            break;
            
        default:
            printf("%c", ch);
            if (cnt < (u64)bufferSize)
            {
                pBuffer[cnt] = ch;
            }
            cnt++;
            break;
        }
    }

    printf("\n");

    if  (cnt > (u64)bufferSize)
    {
        printf("Error: input too long\n");
        return RETURN_ERROR;
    }
    
    /*记录到日志文件*/
    pBuffer[cnt] = '\n';

    diag_clientWriteLog("%s", pSysClient->config.promptNormal);
    diag_clientDoWriteLog(pBuffer, (u32)(cnt + 1));

    /*补齐结束标记*/    
    pBuffer[cnt] = '\0';    
    return RETURN_OK;
}

#define DIAG_DATA_PREPROC_START  0
#define DIAG_DATA_PREPROC_NORMAL 1
#define DIAG_DATA_PREPROC_QUATES 2
#define DIAG_DATA_PREPROC_TRANS  3
#define DIAG_IS_INVALID_CHAR(c) ((' ' == c) || ('\t' == c))
#define DIAG_IS_QUATES(c) ('\"' == c)
#define DIAG_IS_TRANS(c)  ('\\' == c) /* 转移开始 */

/**
*函   数  名: diag_clientPreProcStart
*功能描述: 预处理开始
*输入参数: 无
*输出参数: 无
*返   回  值: 读取到的字符，-1表示失败
*/
static u32 diag_clientPreProcStart(char character, char* pDst, u32* pDstLen)
{
    /*无效字符，跳过*/
    if (DIAG_IS_INVALID_CHAR(character))
    {
        return DIAG_DATA_PREPROC_START;
    }

    /* 转义字符处理 */
    if (DIAG_IS_TRANS(character))
    {
        /*先保存，可能要回退*/
        pDst[(*pDstLen)++] = character;
        return DIAG_DATA_PREPROC_TRANS;
    }
    
    /*引号之内的数据需要特殊处理*/
    if(DIAG_IS_QUATES(character))
    {                    
        return DIAG_DATA_PREPROC_QUATES;
    }
    
    /*有效字符*/
    pDst[(*pDstLen)++] = character;
    return DIAG_DATA_PREPROC_NORMAL;
}

/**
*函   数  名: diag_clientPreProcNormal
*功能描述: 正常数据
*输入参数: 无
*输出参数: 无
*返   回  值: 读取到的字符，-1表示失败
*/
static u32 diag_clientPreProcNormal(char character, char* pDst, u32* pDstLen)
{
    /*无效字符，字段结束*/
    if(DIAG_IS_INVALID_CHAR(character))
    {
        pDst[(*pDstLen)++] = '\0';
        return DIAG_DATA_PREPROC_START;
    }
    
    /* 转义开始 */
    if (DIAG_IS_TRANS(character))
    {
        /*先保存，可能要回退*/
        pDst[(*pDstLen)++] = character;
        return DIAG_DATA_PREPROC_TRANS;
    }
    
    /*引号之内的数据需要特殊处理*/
    if(DIAG_IS_QUATES(character))
    {
        return DIAG_DATA_PREPROC_QUATES;
    }
    
    /*有效字符*/
    pDst[(*pDstLen)++] = character;
    return DIAG_DATA_PREPROC_NORMAL;
}

/**
*函   数  名: diag_clientPreProcQuates
*功能描述: 舍弃字符处理
*输入参数: 无
*输出参数: 无
*返   回  值: 读取到的字符，-1表示失败
*/
static u32 diag_clientPreProcQuates(char character, char* pDst, u32* pDstLen)
{
    /* 转义开始 */
    if (DIAG_IS_TRANS(character))
    {
        /*先保存，可能要回退*/
        pDst[(*pDstLen)++] = character;
        return DIAG_DATA_PREPROC_TRANS;
    }
    
    /*遇到配对引号，返回正常处理流程*/
    if(DIAG_IS_QUATES(character))
    {
        return DIAG_DATA_PREPROC_NORMAL;
    }
    
    /*引号中任何字符都视为有效*/
    pDst[(*pDstLen)++] = character; 
    return DIAG_DATA_PREPROC_QUATES;
}

/**
*函   数  名: diag_clientPreProcTrans
*功能描述: 转义字符处理
*输入参数: 无
*输出参数: 无
*返   回  值: 读取到的字符，-1表示失败
*/
static u32 diag_clientPreProcTrans(char character, char* pDst, u32* pDstLen)
{
    /* 判断是否为待转义的字符 */
    if (DIAG_IS_TRANS(character))
    {
        /* 覆盖上一个转义符号 长度不增长 */
        pDst[((*pDstLen) - 1)] = character;
        return DIAG_DATA_PREPROC_NORMAL;
    }
    
    /* 判断是否为待转义的字符 */
    if (DIAG_IS_QUATES(character))
    {
        /* 覆盖上一个转义符号 长度不增长 */
        pDst[((*pDstLen) - 1)] = character;
        return DIAG_DATA_PREPROC_NORMAL;
    }

    /*无效字符，字段结束*/
    if(DIAG_IS_INVALID_CHAR(character))
    {
        pDst[(*pDstLen)++] = '\0';
        return DIAG_DATA_PREPROC_START;
    }

    /*非待转义字符都视为有效*/
    pDst[(*pDstLen)++] = character; 
    return DIAG_DATA_PREPROC_NORMAL;
}

int32_t diag_clientPreProcData(char* pSrc, char* pDst, u32 dstSize)
{
    u32 index = 0;
    u32 dstLen = 0;
    u32 status = DIAG_DATA_PREPROC_START;
    char character;

    while('\0' != (character = pSrc[index++]))
    {
        switch(status)
        {
        case DIAG_DATA_PREPROC_START:
            status = diag_clientPreProcStart(character, pDst, &dstLen);
            break;
        case DIAG_DATA_PREPROC_NORMAL:
            status = diag_clientPreProcNormal(character, pDst, &dstLen);
            break;
        case DIAG_DATA_PREPROC_QUATES:
            status = diag_clientPreProcQuates(character, pDst, &dstLen);
            break;
        case DIAG_DATA_PREPROC_TRANS:
            status = diag_clientPreProcTrans(character, pDst, &dstLen);
            break;
        default:
            break;
        }
        
        /*防止拷贝越界*/
        if(dstLen >= dstSize)
        {   
            DIAG_LOG(DIAG_LOG_INFO, "input is too long, srcLen<%d> dstSize<%d>", dstLen, dstSize);
            return RETURN_ERROR;
        }        
    }

    if(DIAG_DATA_PREPROC_START != status)
    {
        pDst[dstLen++] = '\0';
    }
    
    return (int32_t)dstLen;
}

/**
*函   数  名: diag_clientCmd_exit
*功能描述: Client端内部命令exit:退出客户端
*输入参数: @argc 参数个数
                           @argv 指向入参的指针
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
static int32_t diag_clientCmd_exit(void* pSysInfo, int32_t argc, char* argv[])
{
    DIAG_SYS_CLIENT_S *pSysclient = (DIAG_SYS_CLIENT_S*)pSysInfo;
    
    if(1 == argc)
    {   /* 退出前阻止心跳发送 */
        pSysclient->client.status = STATUS_LOGOUT;
        (void)diag_clientLogout(pSysclient);
        exit(0);
    }

    diag_clientPrint("cmd %s has no param %s...\n", argv[0], argv[1]);
    return DIAG_INNER_CMD_ERROR;
}

/**
*函   数  名: diag_clientCmd_script
*功能描述: Client端内部命令.script:作为脚本文件执行
*输入参数: @argc 参数个数
                           @argv 指向入参的指针
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
static int32_t diag_clientCmd_script(void* pSysInfo, int32_t argc, char* argv[])
{
    int32_t ret;
    DIAG_SYS_CLIENT_S* pSysClient = (DIAG_SYS_CLIENT_S*)pSysInfo;
    DIAG_CLIENT_S* pClient = &(pSysClient->client);
    DIAG_CLIENT_CFG_S* pClientCfg = &(pSysClient->config);
    char* pTmpBuffer = pSysClient->inputBuffer;
        
    if(2 != argc)
    {
        diag_clientPrintAndSave("Usage: .script <filename>\n");
        return DIAG_INNER_CMD_ERROR;
    }

    if(0 != access(argv[1], R_OK))
    {
        diag_clientPrintAndSave("Error: Can't open file `%s` for read\n", argv[1]);
        return DIAG_INNER_CMD_ERROR;
    }

    ret = sprintf(pTmpBuffer, \
        "%s --auto --script=%s --log-file=%s %s %s --attach=%u", pClient->programPath, argv[1], pClientCfg->logFileName,\
        (pClientCfg->flags & FLAGS_CLI_MODE) ? "--set-cli" : "--set-debug", \
        (pClientCfg->flags & FLAGS_NO_PROMPT) ? "--no-prompt" : "", pClientCfg->attachId);
    if(ret < 0)
    {
        diag_clientPrintAndSave("Error: assemble script cmd fail\n");
        return DIAG_INNER_CMD_ERROR;
    }

    (void)diag_cmd_exec(pTmpBuffer);

    return DIAG_INNER_CMD_DONE;
}

/**
*函   数  名: diag_clientHandleAsClientCmd
*功能描述: 作为client端内部命令处理，包括quit、exit等
*输入参数: @pSysClient 指向Client端全部信息
                           @pSendMsg 存放用户刚输入的命令
*输出参数: 无
*返   回  值: DIAG_INNER_CMD_DONE 内部命令执行成功
                           DIAG_INNER_CMD_ERROR 内部命令执行失败
                           DIAG_INNER_CMD_CONTINUE 继续执行后续流程
*/
int32_t diag_clientHandleAsClientCmd(DIAG_SYS_CLIENT_S* pSysClient, DIAG_MSG_S* pSendMsg)
{
    int32_t ret;
    DIAG_CMD_ARGS_S* pArgs = &(pSysClient->cmdArgs);

    ret = diag_getArgs(pSendMsg, pArgs, MAX_DIAG_CMD_ARGCS_NUM);
    if(RETURN_OK != ret)
    {
        return DIAG_INNER_CMD_ERROR;
    }

    ret = diag_executeAsInnerCmd((void*)pSysClient, &(g_clientCmdHandleTable[0]), pArgs->argc, pArgs->argv);
    return ret;
}

/**
*函   数  名: diag_clientStatusCmdInput
*功能描述: 接收用户输入，并处理命令
*输入参数: @pSysClient 指向diagnose客户端全部信息
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t diag_clientStatusCmdInput(DIAG_SYS_CLIENT_S* pSysClient,MSG_TYPE_E msgType)
{
    int32_t ret;
    u32  buffersize;
    char* pInputBuffer = &(pSysClient->inputBuffer[0]);
    DIAG_CLIENT_S* pClient = &(pSysClient->client);
    DIAG_MSG_S* pSendMsg = &(pSysClient->sendBuffer);
    pSendMsg->msgHead.msgType = msgType;
    if (MSG_TYPE_PROMPT == msgType)
    {
        buffersize = (u32)MAX_DIAG_PROMPT_INPUT_LEN;
        pSendMsg->msgHead.msgOption = MSG_DATA_OPTION_CLIENT;
    }
    else
    {
        buffersize = (u32)DIAG_INPUT_BUFFER_SIZE;
        pSendMsg->msgHead.msgOption = (pSysClient->config.flags & FLAGS_CLI_MODE) ? MSG_CMD_OPTION_CLIENT_CLI : MSG_CMD_OPTION_CLIENT_NORMAL;   
    }
    
    ret = diag_clientGetInput(pInputBuffer, buffersize);
    if (RETURN_OK != ret)
    {
        return RETURN_ERROR;
    }
    
    ret = diag_clientPreProcData(pInputBuffer, pSendMsg->msgData, MAX_DIAG_BUFFER_LEN);
    if(ret <= 0)
    {
        return RETURN_ERROR;
    }
    pSendMsg->msgHead.dataLen = (u16)ret;

    if (STATUS_CMD_INPUT == pClient->status)
    {
        ret = diag_clientHandleAsClientCmd(pSysClient, pSendMsg);
        if(DIAG_INNER_CMD_CONTINUE != ret)
        {
            return RETURN_OK;
        }
    }

    pClient->lastCmdActiveMs = diag_getmillisec();
    pClient->status = STATUS_CMD_RUNNING;
    ret = diag_sendToSocket(pClient->socketFd, &(pClient->socketServerAddr), (void*)pSendMsg);
    if(ret < 0)
    {
        pClient->status = STATUS_CMD_INPUT;
        return RETURN_ERROR;
    }

    return RETURN_OK;
}


/**
*函   数  名: diag_clientCheckTimeOut
*功能描述: diagnose Client端检测命令执行是否超时
*输入参数: 无
*输出参数: 无
*返   回  值: 无
*/
static void diag_clientCheckTimeOut(DIAG_CLIENT_S* pClient)
{
    if(diag_getmillisec() > pClient->lastCmdActiveMs + DIAG_CMD_EXEC_TIMOUT)
    {
        pClient->status = STATUS_CMD_INPUT;
    }
}

/**
*函   数  名: diag_clientRunning
*功能描述: diagnose Client端运行中
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static int32_t diag_clientRunning(void)
{
    DIAG_SYS_CLIENT_S* pSysClient = &g_sys_client;
    DIAG_CLIENT_S* pClient = &(pSysClient->client);

    pSysClient->client.status = STATUS_CMD_INPUT;
    for(;;)
    {   
        switch(pSysClient->client.status)
        {
            case STATUS_CMD_INPUT:
                if(!(pSysClient->config.flags & FLAGS_NO_PROMPT))
                {
                    diag_clientPrint("%s", pSysClient->config.promptNormal);                    
                }
                
                (void)diag_clientStatusCmdInput(pSysClient,MSG_TYPE_CMD);
                break;
            case STATUS_CMD_PROMPT:
                diag_clientPrint("%s", g_szInputPrompt);
                (void)diag_clientStatusCmdInput(pSysClient,MSG_TYPE_PROMPT);              
                break;
            default:
                diag_msleep(DIAG_DEALY_INTERVAL);
                diag_clientCheckTimeOut(pClient);
                break;
        }
        diag_clientClearInputBuf();
    }

    return RETURN_ERROR; //lint !e527
}

static int32_t diag_clientCheckNum()
{
    char buffer[128];
    int32_t clientProcessNum = 0;
    FILE *fp = NULL;

    memset(buffer, '\0', sizeof(buffer));
    fp = popen("pidof cli_client|awk '{print NF}'", "r");
    if(NULL == fp)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "popen fail.");
        return RETURN_ERROR;
    }

    char *retPtr = fgets(buffer, (int32_t)sizeof(buffer), fp);
    if (retPtr == NULL) {
        DIAG_LOG(DIAG_LOG_ERROR, "fgets fail.");
        return RETURN_ERROR;
    }
    pclose(fp);
    if('\0' != buffer[0])
    {
        clientProcessNum = (int32_t)atoi(buffer);
    } 
    else 
    {
        return RETURN_ERROR;
    }

    if (clientProcessNum > MAX_DIAG_CLIENT_NUM)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "clientProcessNum(%d).",clientProcessNum);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/**
*函   数  名: main
*功能描述: diagnose Client端main函数
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
int32_t main(int32_t argc, char* argv[])
{
    int32_t ret;
    DIAG_SYS_CLIENT_S* pSysClient = &g_sys_client;

    (void)diag_commInit();

#ifdef __DIAG_LOG_PATH__
    ret = dpax_log_file_path_set_ext(DIAGNOSE_CLIENT_LOG_PATH);
    if(RETURN_OK != ret)
    {
        diag_clientPrint("dplog set file path fail, ret: %d\n", ret);
        DIAG_LOG(DIAG_LOG_ERROR, "dplog set file path fail, ret: %d.", ret);
        dpax_log_destroy();
        return ret;
    }

    ret = dpax_log_backup_num_set(DIAGNOSE_CLIENT_LOG_NUM);
    if(RETURN_OK != ret)
    {
        diag_clientPrint("dplog set backup num fail, ret: %d\n", ret);
        DIAG_LOG(DIAG_LOG_ERROR, "dplog set backup num fail, ret: %d.", ret);
        dpax_log_destroy();
        return ret;
    }
#endif

    ret = diag_clientCheckNum();
    if(RETURN_OK != ret)
    {
        diag_clientPrint("client start fail, client process num cannot large than %d\n", MAX_DIAG_CMD_THREAD_NUM);
        DIAG_LOG(DIAG_LOG_ERROR, "client start fail, client process num cannot large than %d.", MAX_DIAG_CMD_THREAD_NUM);
        return RETURN_ERROR;
    }
    
    /*初始化数据为0*/
    memset(pSysClient, 0, sizeof(DIAG_SYS_CLIENT_S));

    /*解析入参信息*/
    ret = diag_clientCfgInit(&(pSysClient->config), argc, argv);
    if(RETURN_OK != ret)
    {
        diag_clientPrint("client start fail\n");
         // dpax_log_destroy();
        return ret;        
    }

    /*Client端初始化工作*/
    ret = diag_clientInit(pSysClient);
    if(RETURN_OK != ret)
    {
        diag_clientPrint("client start fail\n");
        // dpax_log_destroy();
        return ret;
    }

    /*正常运行*/
    ret = diag_clientRunning();

    return ret;
}

