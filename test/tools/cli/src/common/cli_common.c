/*************************************************
  Copyright (C) Huawei Technologies Co., Ltd. 2015-2015. All rights reserved.  
  File name:      cli_common.c
  Author:      ID:       Version:       Date: 
  Description:    cli 公共代码
  Others:          
  History:        
*************************************************/
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/prctl.h>
#include<sys/resource.h>
#include <sys/wait.h>
#include <syslog.h>
#include "cli_def.h"
#include "cli_common.h"
#include "cli_crc32.h"

/**
*函   数  名: diag_initMsgHead
*功能描述: 初始化消息头
*输入参数: @pMsgHead 待初始化的消息头地址
*输出参数: 无
*返   回  值: 无
*/
void diag_initMsgHead(DIAG_MSG_HEAD_S* pMsgHead)
{
    pMsgHead->agentId = DIAG_INVALID_ID;
    pMsgHead->clientId = DIAG_INVALID_ID;
    pMsgHead->dataLen = 0;
    pMsgHead->headLen = sizeof(DIAG_MSG_HEAD_S);
    pMsgHead->magic = DIAG_MAGIC;
    pMsgHead->msgOption = 0;
    pMsgHead->msgType = MSG_TYPE_BUTT;
    pMsgHead->version = DIAG_VERSION;
    pMsgHead->crc32 = 0;
    pMsgHead->unused = 0;
    
    return ;
}

/**
*函   数  名: diag_printMsgHead
*功能描述: 打印消息头
*输入参数: @pMsgHead 待打印的消息头地址
*输出参数: 无
*返   回  值: 无
*/
void diag_printMsgHead(DIAG_MSG_HEAD_S* pMsgHead)
{
    DIAG_LOG(DIAG_LOG_INFO, "Diagnose msg head print: pMsgHead(%p),msgType(%u),msgOption(%u),version(%u).",
                             pMsgHead, pMsgHead->msgType, pMsgHead->msgOption, pMsgHead->version);

    DIAG_LOG(DIAG_LOG_INFO, "Diagnose msg head print: unused(%u),dataLen(%u),headLen(%u).",
                             pMsgHead->unused, pMsgHead->dataLen, pMsgHead->headLen);

    DIAG_LOG(DIAG_LOG_INFO, "Diagnose msg head print: clientid(%u),agentid(%u),magic(%u),crc32(%u).",
                             pMsgHead->clientId, pMsgHead->agentId, pMsgHead->magic, pMsgHead->crc32);
    
    return ;
}

/**
*函   数  名: diag_initSocketAddr
*功能描述: 使用指定的端口，初始化socket地址
*输入参数: @socketPort 指定的socket端口号，0表示使用随机端口 
*输出参数: @pSocketAddr 返回的socket地址
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_initSocketAddr(struct sockaddr_in* pSocketAddr, u32 socketPort)
{
    memset(pSocketAddr, 0, sizeof(struct sockaddr_in));
    pSocketAddr->sin_port        = htons((u16)socketPort);
    pSocketAddr->sin_family      = AF_INET;
    pSocketAddr->sin_addr.s_addr = inet_addr("127.0.0.1");

    return RETURN_OK;
}

/**
*函   数  名: diag_initUnixDomainAddr
*功能描述: 使用指定本地路径，初始化socket地址(匿名)
*输入参数: @pSunPath 本地路径，用于唯一的表示一个unix domain
*输出参数: @pSocketAddr 返回的socket地址
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_initUnixDomainAddr(struct sockaddr_un* pSocketAddr, char* pSunPath)
{
    memset(pSocketAddr, 0, sizeof(struct sockaddr_un));

    pSocketAddr->sun_family  = AF_UNIX;
    pSocketAddr->sun_path[0] = '\0';
    strcpy(&(pSocketAddr->sun_path[1]), pSunPath);

    return RETURN_OK;
}

/**
*函   数  名: diag_isKnownRecvErrno
*功能描述: 判断是否是可以忽略的错误码
*输入参数: @retErrno 错误码
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_isSocketRetryErrno(s32 retErrno)
{
    switch(retErrno)
    {
    case EINTR:
    case EAGAIN:
        break;
    default:
        return RETURN_ERROR;
    }
    
    return RETURN_OK;
}

/**
*函   数  名: diag_setSocketOpt
*功能描述: 设置socket可选项，包括超时时间及缓存buffer大小
*输入参数: @socketFd socket文件描述符
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_setSocketOpt(SOCKET socketFd)
{
    s32 ret, result;
    s32 recvBufferSize = DIAG_SOCKET_BUFFER_SIZE;
    struct timeval timeout = {0, DIAG_SOCKET_TIMEOUT};
    
    /* 设置接收超时时间*/
    ret = setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout,(u32)sizeof(timeout));
    result = ret;
    if (RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose set socket timeout fail.");
    }

    /*设置发送超时时间*/
    ret = setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout,(u32)sizeof(timeout));
    result |= ret;
    if (RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose set socket timeout fail.");
    }    

    /* 默认8.5k在打印较多的情况下会丢包*/    
    ret = setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, (const char*)&recvBufferSize, (u32)sizeof(recvBufferSize));
    result |= ret;
    if (RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose set socket buffer size fail.");
    }   

    return (0 == result) ? RETURN_OK : RETURN_ERROR;
}

/**
*函   数  名: diag_socketBind
*功能描述: 绑定socktFd到指定地址
*输入参数: 
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_socketBind(SOCKET socketFd, const struct sockaddr *address, u32 addressLen)
{
    s32 ret;

    ret = bind(socketFd, address, addressLen);
    return ret;
}

/**
*函   数  名: diag_initSocket
*功能描述: 使用指定端口号，初始化socket
*输入参数: @socketPort 指定的socket端口号，0表示使用随机端口 
*输出参数: @pSocketFd 返回的socket描述符
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_initSocket(SOCKET* pSocketFd, u32 socketPort)
{
    s32 ret;
    struct sockaddr_in socketAddr;

    ret = diag_initSocketAddr(&socketAddr, socketPort);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose init socketAddr fail.");
        return ret;
    }
    
    *pSocketFd = socket(AF_INET, (s32)SOCK_DGRAM|SOCK_CLOEXEC, 0);
    if (DIAG_INVALID_SOCKET_FD == *pSocketFd)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose create socket fail, errno(%d).", errno);
        return RETURN_ERROR;
    }

    ret = diag_socketBind(*pSocketFd, (struct sockaddr *)(void *)&socketAddr, (u32)sizeof(socketAddr));
    if (RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose can not bind to local address, port:%u errno(%d).", socketPort, errno); 
        close(*pSocketFd);
        return RETURN_ERROR;
    }

    (void)diag_setSocketOpt(*pSocketFd);
    return RETURN_OK;
}

/**
*函   数  名: diag_initUnixDomainSocket
*功能描述: 使用指定路径，初始化抽象socket
*输入参数: @pSunPath 本地路径，用于唯一的表示一个unix domain
*输出参数: @pSocketFd 返回的socket描述符
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_initUnixDomainSocket(SOCKET* pSocketFd, s32 socketType, char* pSunPath)
{
    s32 ret;
    SOCKET socketFd;
    struct sockaddr_un socketAddr;
    
    ret = diag_initUnixDomainAddr(&socketAddr, pSunPath);
    if(RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose init socketAddr fail %s", pSunPath);
        return ret;
    }
    
    socketFd = socket(PF_UNIX, socketType|SOCK_CLOEXEC, 0);
    if (DIAG_INVALID_SOCKET_FD == socketFd)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose create unix domain socket fail, errno(%d) %s", errno, pSunPath);
        return RETURN_ERROR;
    }

    ret = diag_socketBind(socketFd, (struct sockaddr *)(void *)&socketAddr, (u32)sizeof(socketAddr));
    if (RETURN_OK != ret)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose can not bind to local address, errno(%d) %s", errno, pSunPath);        
        close(socketFd);
        return RETURN_ERROR;
    }

    (void)diag_setSocketOpt(socketFd);

    *pSocketFd = socketFd;
    return RETURN_OK;
}

/**
*函   数  名: diag_isKnownSendErrno
*功能描述: 判断是否是可以忽略的错误码
*输入参数: @sendErrno 错误码
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_isKnownSendErrno(s32 sendErrno)
{
    switch(sendErrno)
    {
    case EINTR:
    case EAGAIN:
        break;
    default:
        DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Diagnose socket send fail errno<%d>.", sendErrno);        
        return RETURN_ERROR;
    }    

    return RETURN_OK;
}

/**
*函   数  名: diag_sendToSocket
*功能描述: 发送socket数据
*输入参数: @socketFd 本地打开的socket文件描述符
                           @pSocketDstAddr socket目的地址
                           @pMsg 发送的socket消息
*输出参数: 
*返   回  值: 成功返回发送的字节数，否则，返回负值表示对应的错误码
*/
s32 diag_sendToSocket(SOCKET socketFd, struct sockaddr_in *pSocketDstAddr, void *pMsg)
{
    s32 ret;
    s32 retErrno = 0;    
    s32 failCnt;

    DIAG_MSG_S* pSendMsg = (DIAG_MSG_S*)pMsg;

    pSendMsg->msgHead.crc32 = 0;
    pSendMsg->msgHead.crc32 = diag_createCrc32(pSendMsg, (u32)pSendMsg->msgHead.dataLen + (u32)sizeof(DIAG_MSG_HEAD_S));

    for(failCnt = 0; failCnt < MAX_DIAG_SEND_FAIL_CNT; failCnt++)
    {
        ret = (s32)sendto(socketFd, (char *)pSendMsg, (size_t)(pSendMsg->msgHead.headLen + pSendMsg->msgHead.dataLen), 0, (struct sockaddr *)(void *)pSocketDstAddr, (u32)sizeof(struct sockaddr_in));
        if(ret >= 0)
            return ret;

        retErrno = errno;
        if(RETURN_OK == diag_isKnownSendErrno(retErrno))
            continue;
        
        DIAG_LOG(DIAG_LOG_DEBUG, "Diagnose socket<%d> sendto failCnt<%d> errno<%d>.", socketFd, failCnt+1, retErrno);
        break;
    }

    diag_msleep(DIAG_SOCKET_SEND_DEALY_MS);
    return (retErrno > 0) ? -retErrno : RETURN_ERROR;
}

/**
*函   数  名: diag_sendSocket
*功能描述: 将pMsg数据发送到和socketFd连接的对端
*输入参数: @socketFd 本地打开的socket文件描述符
           @pMsg 发送的socket消息
*输出参数: 
*返   回  值: 成功返回发送的字节数，否则，返回负值表示对应的错误码
*/
static s32 diag_sendSocket(SOCKET socketFd, void *pMsg)
{
    s32 ret;
    s32 retErrno = 0;
    s32 failCnt;

    DIAG_MSG_S* pSendMsg = (DIAG_MSG_S*)pMsg; //lint !e826

    pSendMsg->msgHead.crc32 = 0;
    pSendMsg->msgHead.crc32 = diag_createCrc32(pSendMsg, (u32)pSendMsg->msgHead.dataLen + (u32)sizeof(DIAG_MSG_HEAD_S));

    for(failCnt = 0; failCnt < MAX_DIAG_SEND_FAIL_CNT; failCnt++)
    {
        ret = (s32)send(socketFd, (char *)pSendMsg, (size_t)(pSendMsg->msgHead.headLen + pSendMsg->msgHead.dataLen), 0);
        if(ret >= 0)
            return ret;

        retErrno = errno;
        if(RETURN_OK == diag_isKnownSendErrno(retErrno))
            continue;
        
        DIAG_LOG(DIAG_LOG_DEBUG, "Diagnose socket<%d> sendto failCnt<%d> errno<%d>.", socketFd, failCnt+1, retErrno);
        break;
    }

    diag_msleep(DIAG_SOCKET_SEND_DEALY_MS);
    return (retErrno > 0) ? -retErrno : RETURN_ERROR;
}

/**
*函   数  名: diag_isKnownRecvErrno
*功能描述: 判断是否是可以忽略的错误码
*输入参数: @recvErrno 错误码
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_isKnownRecvErrno(s32 recvErrno)
{
    switch(recvErrno)
    {
    case EINTR:
    case EAGAIN:
        break;
    default:
        DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Diagnose socket recv fail errno<%d>.", recvErrno);
        return RETURN_ERROR;
    }    

    return RETURN_OK;
}

/**
*函   数  名: diag_SocketRecvFrom
*功能描述: 结束socket数据
*输入参数: @socketFd 本地打开的socket文件描述符
                           @pSocketSrcAddr socket源地址
                           @pMsg 接收的socket消息
                           @need_print_err 接收失败后，是否需要打印错误信息
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_recvFromSocket(SOCKET socketFd, struct sockaddr_in* pSocketSrcAddr, DIAG_MSG_S* pMsg, bool need_print_err)
{
    s32 ret;
    s32 failCnt;
    u32 crcCheck = 0;
    u32 crcOrigin = 0;
    socklen_t scketAddrLen = sizeof(struct sockaddr_in);

    for(failCnt = 0; failCnt < MAX_DIAG_RECV_FAIL_CNT; failCnt++)
    {
        ret = (s32)recvfrom(socketFd, (char *)pMsg, (ulong)sizeof(DIAG_MSG_S), 0, (struct sockaddr *)(void *)pSocketSrcAddr, &scketAddrLen);
        if(ret < 0)
        {
            if(RETURN_OK != diag_isKnownRecvErrno(errno))
            {
                diag_msleep(DIAG_SOCKET_RECV_DEALY_MS);
                return RETURN_ERROR;
            }
            continue;
        }
        
        if((ret < (s32)sizeof(DIAG_MSG_HEAD_S)) || (ret > (s32)sizeof(DIAG_MSG_S)))
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose socket recvfrom fail recvLen<%d> errno<%d>.", ret, errno);
            continue;
        }

        if(DIAG_MAGIC != pMsg->msgHead.magic)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose socket recvfrom fail magic<0x%x> dataLen<%d>.", pMsg->msgHead.magic, pMsg->msgHead.dataLen);
            continue;
        }

        if((s32)pMsg->msgHead.dataLen + (s32)sizeof(DIAG_MSG_HEAD_S) > ret)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose socket recvfrom fail dataLen<%d> recvLen<%d>.", pMsg->msgHead.dataLen, ret);
            continue;
        }

        if(pMsg->msgHead.dataLen < MAX_DIAG_BUFFER_LEN - 1)
        {
            pMsg->msgData[pMsg->msgHead.dataLen] = '\0';
        }
        else
        {
            pMsg->msgData[MAX_DIAG_BUFFER_LEN - 1] = '\0';
        }

        break;
    }

    if(failCnt >= MAX_DIAG_RECV_FAIL_CNT)
    {
        if (need_print_err)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose socket recvfrom failCnt<%d> errno<%d>.", failCnt, errno);
        }
        diag_msleep(DIAG_SOCKET_RECV_DEALY_MS);
        return RETURN_ERROR;
    }

    crcOrigin = pMsg->msgHead.crc32;
    pMsg->msgHead.crc32 = 0;

    crcCheck = diag_createCrc32(pMsg, (u32)pMsg->msgHead.dataLen + (u32)sizeof(DIAG_MSG_HEAD_S));
    if (crcCheck != crcOrigin)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose recv msg check crc fail,checkCrc(%u),originCrc(%u).", crcCheck, crcOrigin);
        diag_printMsgHead(&(pMsg->msgHead));
        return RETURN_ERROR;
    }
    
    return RETURN_OK;
}

/**
*函   数  名: diag_recvSocket
*功能描述: 从socketFd连接的对端接受数据
*输入参数: @socketFd 本地打开的socket文件描述符
           @pMsg 接收的socket消息
           @msgSize 接收buf大小
*输出参数: 
*返   回  值: 成功返回接受到的数据长度，对端关闭返回0，失败返回对应错误码
*/
static s32 diag_recvSocket(SOCKET socketFd, void* pRecvMsg, u64 msgSize)
{
    s32 ret;
    s32 retErrno = 0;
    s32 failCnt;
    u32 crcCheck = 0;
    u32 crcOrigin = 0;
    u64 dataBufSize = msgSize - sizeof(DIAG_MSG_HEAD_S);
    DIAG_MSG_S* pMsg = (DIAG_MSG_S*)pRecvMsg; //lint !e826

    for(failCnt = 0; failCnt < MAX_DIAG_RECV_FAIL_CNT; failCnt++)
    {
        ret = (s32)recv(socketFd, (char *)pMsg, msgSize, 0);
        if(0 == ret)
        {
            return 0;  //对端关闭，且无可用数据
        }
        
        if(ret < 0)
        {
            retErrno = errno;
            if(RETURN_OK == diag_isKnownRecvErrno(retErrno))
                continue;
        
            DIAG_LOG(DIAG_LOG_DEBUG, "Diagnose socket recv fail ret<%d> errno<%d>.", ret, retErrno);
            diag_msleep(DIAG_SOCKET_RECV_DEALY_MS);
            return -retErrno;
        }
        
        if((ret < (s32)sizeof(DIAG_MSG_HEAD_S)) || (ret > (s32)sizeof(DIAG_MSG_S)))
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose socket recv fail recvLen<%d> errno<%d>.", ret, errno);
            continue;
        }

        if(DIAG_MAGIC != pMsg->msgHead.magic)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose socket recv fail magic<0x%x> dataLen<%d>.", pMsg->msgHead.magic, pMsg->msgHead.dataLen);
            continue;
        }

        if((s32)pMsg->msgHead.dataLen + (s32)sizeof(DIAG_MSG_HEAD_S) > ret)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose socket recv fail dataLen<%d> recvLen<%d>.", pMsg->msgHead.dataLen, ret);
            continue;
        }

        if(0 == dataBufSize)
            break;

        if(pMsg->msgHead.dataLen < (dataBufSize - 1))
        {
            pMsg->msgData[pMsg->msgHead.dataLen] = '\0';
        }
        else
        {
            pMsg->msgData[dataBufSize - 1] = '\0';
        }

        break;
    }

    if(failCnt >= MAX_DIAG_RECV_FAIL_CNT)
    {
        diag_msleep(DIAG_SOCKET_RECV_DEALY_MS);
        return (retErrno > 0) ? -retErrno : RETURN_ERROR;
    }

    crcOrigin = pMsg->msgHead.crc32;
    pMsg->msgHead.crc32 = 0;

    crcCheck = diag_createCrc32(pMsg, (u32)pMsg->msgHead.dataLen + (u32)sizeof(DIAG_MSG_HEAD_S));
    if (crcCheck != crcOrigin)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose recv msg check crc fail,checkCrc(%u),originCrc(%u).", crcCheck, crcOrigin);
        diag_printMsgHead(&(pMsg->msgHead));
        return RETURN_ERROR;
    }

    return ret;
}

static s32 diag_sendTCP(SOCKET socketFd, void *pMsg)
{
    s32 ret;

    for(;;)
    {
        ret = diag_sendSocket(socketFd, pMsg);
        if(ret >= 0)
        {
            return ret;
        }

        if(RETURN_OK != diag_isSocketRetryErrno(-ret))
        {
            DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Send tcp socket fail, ret:%d", ret);
            break;
        }
    }

    return ret;    
}


static s32 diag_recvTCP(SOCKET clientFd, void* pRecvMsg, u64 msgSize)
{
    s32 ret;
    
    for(;;)
    {
        ret = diag_recvSocket(clientFd, pRecvMsg, msgSize);
        if(ret > 0)
        {
            return ret;
        }

        if(0 == ret)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Peer socket has been shutdown");
            break;
        }
        
        if(RETURN_OK != diag_isSocketRetryErrno(-ret))
        {
            DIAG_LOG(DIAG_LOG_INFO, "Recv tcp socket fail, ret:%d", ret);
            break;
        }
    }

    return ret;
}


static s32 diag_sendTCPSync(SOCKET socketFd)
{
    s32 ret;
    DIAG_MSG_HEAD_S sendMsgSync;

    diag_initMsgHead(&sendMsgSync);
    for(;;)
    {
        ret = diag_recvTCP(socketFd, (void*)&sendMsgSync, sizeof(DIAG_MSG_HEAD_S));
        if(ret <= 0)
            break;
        
        if((MSG_TYPE_CONTROL == sendMsgSync.msgType) && (MSG_CONTROL_OPTION_MSG_RECV_SYNC == sendMsgSync.msgOption))
            break;
    }
    
    return ret;
}

/**
*函   数  名: diag_sendSocketTCP
*功能描述: 将pMsg数据发送到和socketFd连接的对端，同时等待对端返回接受成功消息
*输入参数: @socketFd 本地打开的socket文件描述符
           @pMsg 发送的socket消息
*输出参数: 
*返   回  值: 成功返回发送的字节数，否则，返回负值表示对应的错误码
*/
s32 diag_sendSocketTCP(SOCKET socketFd, void *pMsg)
{
    s32 ret;

    ret = diag_sendTCP(socketFd, pMsg);
    if(ret >= 0)
    {
        (void)diag_sendTCPSync(socketFd);
    }

    return ret;
}

static s32 diag_recvTCPSync(SOCKET socketFd)
{
    s32 ret;
    DIAG_MSG_HEAD_S recvMsgSync;

    diag_initMsgHead(&recvMsgSync);
    recvMsgSync.msgType = MSG_TYPE_CONTROL;
    recvMsgSync.msgOption = MSG_CONTROL_OPTION_MSG_RECV_SYNC;

    ret = diag_sendTCP(socketFd, (void*)&recvMsgSync);
    return ret;
}

/**
*函   数  名: diag_recvSocketTCP
*功能描述: 从socketFd连接的对端接收数据，并返回接收成功消息
*输入参数: @socketFd 本地打开的socket文件描述符
           @pMsg 接收的socket消息
           @msgSize 接收buf大小
*输出参数: 
*返   回  值: 成功返回接收到的数据长度，对端关闭返回0，失败返回对应错误码
*/
s32 diag_recvSocketTCP(SOCKET clientFd, DIAG_MSG_S* pRecvMsg, u64 msgSize)
{
    s32 ret;

    ret = diag_recvTCP(clientFd, (void*)pRecvMsg, msgSize);
    if(ret > 0)
    {
        (void)diag_recvTCPSync(clientFd);
    }

    return ret;
}

/**
*函   数  名: diag_isKnownAcceptErrno
*功能描述: 判断是否是可以忽略的错误码
*输入参数: @acceptErrno 错误码
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_isKnownAcceptErrno(s32 acceptErrno)
{
    switch(acceptErrno)
    {
    case EINTR:
    case EAGAIN:
        break;
    default:
        DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Diagnose socket accept fail errno<%d>.", acceptErrno);
        return RETURN_ERROR;
    }    

    return RETURN_OK;
}

/**
*函   数  名: diag_acceptSocket
*功能描述: 接受建链请求
*输入参数: @socketFd 本地打开的socket文件描述符
*输出参数: 
*返   回  值: 失败返回小于0的错误码，否则返回建链成功的文件描述符
*/
s32 diag_acceptSocket(SOCKET socketFd)
{
    s32 retErrno = 0;
    s32 failCnt;
    s32 clientFd;

    for(failCnt = 0; failCnt < MAX_DIAG_RETRY_CNT; failCnt++)
    {
        clientFd = accept(socketFd, NULL, NULL);
        if(-1 != clientFd)
        {
            return clientFd;
        }

        retErrno = errno;
        diag_msleep(DIAG_RETRY_DELAY_MS);        
        if(RETURN_OK == diag_isKnownAcceptErrno(retErrno))
            continue;
       
        DIAG_LOG(DIAG_LOG_DEBUG, "Accept socket fail ret:%d", -retErrno);
        break;
    }

    return (retErrno > 0) ? -retErrno : INVALID_SOCKET;
}

/**
*函   数  名: diag_isKnownConnectErrno
*功能描述: 判断是否是可以忽略的错误码
*输入参数: @connectErrno 错误码
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
static s32 diag_isKnownConnectErrno(s32 connectErrno)
{
    switch(connectErrno)
    {
    case EINTR:
    case EAGAIN:
        break;
    default:
        DIAG_LOG_PRINT_LIMIT(DIAG_LOG_INFO, "Diagnose socket connect fail errno<%d>.", connectErrno);
        return RETURN_ERROR;
    }    

    return RETURN_OK;
}

/**
*函   数  名: diag_connectSocket
*功能描述: 请求建立连接
*输入参数: @socketFd 本地打开的socket文件描述符
            @address 建链的目的地址
            @addressLen 目的地址大小
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回小于0的错误码
*/
s32 diag_connectSocket(SOCKET socketFd, void* address, u32 addressLen)
{
    s32 ret;
    s32 retErrno = 0;
    s32 failCnt;

    for(failCnt = 0; failCnt < MAX_DIAG_RETRY_CNT; failCnt++)
    {
        ret = connect(socketFd, (struct sockaddr*)address, addressLen);
        if(0 == ret) //建链成功
            return RETURN_OK;
        
        retErrno = errno;
        diag_msleep(DIAG_RETRY_DELAY_MS);        
        if(RETURN_OK == diag_isKnownConnectErrno(retErrno))
            continue;

        DIAG_LOG(DIAG_LOG_DEBUG, "Connect socket fail ret:%d", -retErrno);
        break;
    }

    return (retErrno > 0) ? -retErrno : RETURN_ERROR;
}

/**
*函   数  名: diag_isSocketAddrEqual
*功能描述: 判断两个socket地址是否相同
*输入参数: @pSocketAddr1 socket地址1
                           @pSocketAddr2 socket地址2
*输出参数: 
*返   回  值: 相等返回RETURN_OK，不相等返回RETURN_ERROR
*/
s32 diag_isSocketAddrEqual(struct sockaddr_in* pSocketAddr1, struct sockaddr_in* pSocketAddr2)
{
    if(pSocketAddr1->sin_addr.s_addr != pSocketAddr2->sin_addr.s_addr)
    {
        return RETURN_ERROR;
    }

    if(pSocketAddr1->sin_port != pSocketAddr2->sin_port)
    {
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

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
s32 diag_login(SOCKET socketFd, struct sockaddr_in* pSocketDstAddr, DIAG_MSG_S* pSendMsg, DIAG_MSG_S* pRecvMsg)
{
    s32 ret;    
    s32 failCnt;
    struct sockaddr_in socketAddrRecv;

    for(failCnt = 0; failCnt < MAX_DIAG_LOGIN_FAIL_CNT; failCnt++)
    {
        ret = diag_sendToSocket(socketFd, pSocketDstAddr, (void *)pSendMsg);
        if(ret < 0)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose send login failCnt<%d> ret<%d>.", failCnt, ret);
            continue;
        }

        ret = diag_recvFromSocket(socketFd, &socketAddrRecv, pRecvMsg, (bool)true);
        if(RETURN_OK != ret)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose recv failCnt<%d>.", failCnt);      
            continue;
        }

        ret = diag_isSocketAddrEqual(&socketAddrRecv, pSocketDstAddr);
        if(RETURN_OK != ret)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose recev addr error, failCnt<%d>.", failCnt);      
            continue;
        }

        if(MSG_TYPE_CONTROL != pRecvMsg->msgHead.msgType)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose msgType<%d> is wrong, failCnt<%d>.", pRecvMsg->msgHead.msgType, failCnt);         
            continue;
        }

        if(MSG_CONTROL_OPTION_LOGIN_BUSY == pRecvMsg->msgHead.msgOption)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose Login failCnt<%d> msgOption<%d>", failCnt+1, pRecvMsg->msgHead.msgOption);
            return MSG_CONTROL_OPTION_LOGIN_BUSY;
        }

        if(MSG_CONTROL_OPTION_LOGIN_SUCCESS != pRecvMsg->msgHead.msgOption)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose Login failCnt<%d> msgOption<%d>", failCnt+1, pRecvMsg->msgHead.msgOption);
            continue;
        }       

        break;
    }

    if(failCnt >= MAX_DIAG_LOGIN_FAIL_CNT)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose Login failCnt<%d>.", failCnt);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

/**
*函   数  名: diag_logout
*功能描述: 退出登陆
*输入参数: @socketFd 本地打开的socket文件描述符
                           @pSocketDstAddr socket目的地址
                           @pSendMsg 待发送的socket消息
                           @pRecvMsg 接收的socket消息
*输出参数: 
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_logout(SOCKET socketFd, struct sockaddr_in* pSocketDstAddr, void* pSendMsg)
{
    s32 ret;    
    s32 failCnt;

    for(failCnt = 0; failCnt < MAX_DIAG_LOGIN_FAIL_CNT; failCnt++)
    {
        ret = diag_sendToSocket(socketFd, pSocketDstAddr, pSendMsg);
        if(ret < 0)
        {
            DIAG_LOG(DIAG_LOG_INFO, "Diagnose Logout failCnt<%d> errno<%d>.", failCnt, errno);
            continue;
        }

        break;
    }

    if(failCnt >= MAX_DIAG_LOGIN_FAIL_CNT)
    {
        DIAG_LOG(DIAG_LOG_ERROR, "Diagnose Logout failCnt<%d>.", failCnt);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

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
s32 diag_handleSocketMsg(void* pSysInfo, DIAG_MSG_HANDLE_S* pMsgHandleTable, DIAG_MSG_S* pRecvMsg,  struct sockaddr_in*pSocketAddr)
{
    s32 ret;
    DIAG_MSG_HEAD_S* pMsgHead = &(pRecvMsg->msgHead);
    DIAG_MSG_HANDLE_S* pMsgHandle = &(pMsgHandleTable[0]);

    while(NULL != pMsgHandle->msgHandle)
    {
        if(pMsgHead->msgType != pMsgHandle->msgType)
        {
            pMsgHandle++;
            continue;
        }

        ret = pMsgHandle->msgHandle(pSysInfo, pRecvMsg, pSocketAddr);
        return ret;
    }

    return RETURN_ERROR;
}

/**
*函   数  名: diag_getArgs
*功能描述: 从预处理后的数据格式中，解析出argc、argv[]格式的数据
*输入参数: @pMsg msg消息，用于存放预处理后或接收到的socket消息
                           @maxArgsNum 支撑的最大参数个数
*输出参数: @pArgs 用于存放解析后的argc、argv[]数据
*返   回  值: 成功返回RETURN_OK，失败返回RETURN_ERROR
*/
s32 diag_getArgs(DIAG_MSG_S* pMsg, DIAG_CMD_ARGS_S* pArgs, u32 maxArgsNum)
{
    u32 index = 0;
    s32 argc = 0;
    char* pSrc = &(pMsg->msgData[0]);
    u32 dataLen = pMsg->msgHead.dataLen;
    char* pArg = NULL;

    maxArgsNum = (maxArgsNum>1) ? (maxArgsNum-1) : 0;
    for(index=0; index<dataLen; index++)
    {
        if(NULL == pArg)
        {
            if(argc >= (s32)maxArgsNum)
            {
                DIAG_LOG(DIAG_LOG_INFO, "input cmd has too much argc<%d>", argc+1);
                pArgs->argv[argc] = NULL;                
                return RETURN_ERROR;
            }
            pArg = pSrc+index;
            pArgs->argv[argc++] = pArg;
        }
        
        if('\0' == *(pSrc+index))
        {   
            pArg = NULL;
        }
    }

    pArgs->argc = argc;
    pArgs->argv[argc] = NULL;
    return RETURN_OK;
}

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
s32 diag_executeAsInnerCmd(void* pSysInfo, DIAG_CMD_HANDLE_S* pCmdHandle, s32 argc, char* argv[])
{
    s32 ret;

    if(argc <= 0)
    {
        return DIAG_INNER_CMD_ERROR;
    }

    while(NULL != pCmdHandle->cmdHandle)
    {
        ret = strncmp(pCmdHandle->pCmdName, argv[0], (ulong)CLI_MAX_COMMAND_LEN);
        if(0 != ret)
        {
            pCmdHandle++;
            continue;
        }

        ret = pCmdHandle->cmdHandle(pSysInfo, argc, argv);
        return ret;
    }

    return DIAG_INNER_CMD_CONTINUE;
}

/**
*函   数  名: diag_getmillisec
*功能描述: 获取系统开工以来的毫秒数
*输入参数: 无
*输出参数: 无
*返   回  值: 系统开工以来的毫秒数
*/
u64 diag_getmillisec(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);

    return ((u64)ts.tv_sec * 1000ULL + (u64)ts.tv_nsec / 1000000);
}

/**
*函   数  名: diag_strToLower
*功能描述: 将大写字符转换成小写字符
*输入参数: v_pszStr输入字符串
*输出参数: 无
*返   回  值: 无
*/
void diag_strToLower(char *v_pszStr)
{
    s32 index;
    
    if (NULL == v_pszStr)
    {
        return;
    }

    index = 0;
    while (v_pszStr[index] != '\0')
    {
        v_pszStr[index] = (char)tolower(v_pszStr[index]);
        index++;
    }
}

/**
*函   数  名: diag_commInit
*功能描述: diagnose公共依赖初始化
*输入参数: 无
*输出参数: 无
*返   回  值: 成功返回RETURN_OK，否则表示失败
*/
s32 diag_commInit(void)
{
    /*防止tcp对端关闭场景触发的SIGPIPE信号*/
    (void)signal(SIGPIPE, SIG_IGN);
    
    /*日志限频依赖jiffies功能*/
    // jiffies_Init();

    // dpax_log_init();
    return RETURN_OK;
}

void diag_threadSetName(const char *name, ...)
{
    int32_t ret = 0;

    ret = prctl(PR_SET_NAME, name);
    if (-1 == ret)
    {    
        // DP_OSAX_LOG(DPLOG_LVL_ERROR, "dpax set thrd name fail, errno:%d", errno);
    }
}

int32_t diag_setCurThreadPrio(int32_t prio)
{
    int32_t ret;

    ret = setpriority(PRIO_PROCESS, 0,  prio);
    if(0 != ret)
    {
        syslog(LOG_INFO, "Set thread prio fail %d, errno %d", ret, errno);
        return RETURN_ERROR;
    }
        
    return RETURN_OK;
}

typedef void *(*THREAD_CALL_BACK)(void *arg);
int32_t diag_switchThreadAsync(void (*handle)(void *data), void *data, pthread_attr_t *attr)
{
    int32_t ret;
    pthread_t threadId;

    ret = pthread_create(&threadId, attr, (THREAD_CALL_BACK)handle, data);
    if (ret != RETURN_OK) {
        return ret;
    }

    pthread_detach(threadId);
    return RETURN_OK;
}

int32_t diag_cmd_exec(const char *cmdstring)
{
    pid_t pid;
    int32_t status = 0;
    uint64_t len = 0;
    uint64_t i ;
    int32_t flag = 0;

    len = strlen(cmdstring);
    for(i = 0; i < len - 1; i++)
    {
        if(cmdstring[i] == ';')
        {
            flag = 1;
            break;
        }
        if ((cmdstring[i] == '|') && (cmdstring[i + 1] == '|'))
        {
            flag = 1;
            break;
        }
    }

    if(flag)
        return RETURN_ERROR;

    if((pid = vfork()) < 0)
    {
        status = -1;
    }
    else if(pid == 0)
    {
        (void)execl("/bin/sh", "sh", "-c", cmdstring, (char *)0);
            _exit(127);
    }
    else
    {
        while(waitpid(pid, &status, 0) < 0)
        {
            if(errno != EINTR)
            {
                status = -1;
                break;
            }
        }
    }

    return status;
}

/* 文件操作中拼接命令的最大长度 命令最多由2个路径和几个字符组成 */
#define MAX_FILE_SHELL_CMD_LEN 4096

int32_t diag_cmd_mkdir(char *pFile)
{

    char *pcCmd = NULL;
    int32_t ret = RETURN_ERROR;

    if (strlen(pFile) >= MAX_PATH_NAME)
    {
        // DP_OSAX_LOG(DPLOG_LVL_ERROR, "File path too long pFile = %s",pFile);
        return RETURN_ERROR;
    }
    pcCmd = calloc((uint64_t)1, (uint64_t)MAX_FILE_SHELL_CMD_LEN);
    if (NULL == pcCmd)
    {
        // DP_OSAX_LOG(DPLOG_LVL_ERROR, "allocCmd is NULL");
        return RETURN_ERROR;
    }

    sprintf(pcCmd, "mkdir -p %s &>/dev/null", pFile);
    ret = diag_cmd_exec(pcCmd);
    if (RETURN_OK != ret)
    {
        // DP_OSAX_LOG(DPLOG_LVL_ERROR, "Execute cmd(%s) Fail(%d)",pcCmd , ret);
    }
    free(pcCmd);
    return ret;
}


#ifdef __DIAG_USE_SYSLOG__
/**
*函   数  名: diag_clientDoWriteLog
*功能描述: 将buf中内容，按原样写入日志文件，主要用于操作记录
*输入参数: @pBuffer 日志缓存
*输出参数: 无
*返   回  值: 无
**/
void diag_clientDoWriteLog(char* pBuffer, u32 bufferLen)
{
    UNREFERENCE_PARAM(bufferLen);
    
    syslog(LOG_INFO, "[%d]%s", getpid(), pBuffer);
    return;
}

/**
*函   数  名: diag_clientWriteLog
*功能描述: 保存日志信息，主要用于操作记录
*输入参数: @format 格式化字符串
            @... 可变入参
*输出参数: 无
*返   回  值: 无
*/
void diag_clientWriteLog(const char* format, ...)
{
    s32 ret;
    va_list pArgList;
    char buffer[DIAG_LOG_BUFFER_SIZE];
    
    va_start(pArgList, format);
    ret = vsnprintf_s(buffer, (ulong)DIAG_LOG_BUFFER_SIZE, (ulong)(DIAG_LOG_BUFFER_SIZE - 1),format, pArgList);
    va_end(pArgList);
    ret = (ret < 0) ? DIAG_LOG_BUFFER_SIZE : ret;
    
    diag_clientDoWriteLog(buffer, (u32)ret);    
}
#else
/**
*函   数  名: diag_clientDoWriteLog
*功能描述: 将buf中内容，按原样写入日志文件，主要用于操作记录
*输入参数: @pBuffer 日志缓存
*输出参数: 无
*返   回  值: 无
**/
void diag_clientDoWriteLog(char* pBuffer, u32 bufferLen)
{
    // (void)dplog_do_write_file(pBuffer, bufferLen, DPLOG_WRITE_FILE_NONE);
    return ;
}

/**
*函   数  名: diag_clientWriteLog
*功能描述: 保存日志信息，主要用于操作记录
*输入参数: @format 格式化字符串
            @... 可变入参
*输出参数: 无
*返   回  值: 无
*/
void diag_clientWriteLog(const char* format, ...)
{
    s32 ret = 0;
    u32 logLen;
    va_list pArgList;    
    char buffer[DIAG_LOG_BUFFER_SIZE];    

    //添加时间信息
    // ret = dplog_fill_timeInfo_sigsafe(buffer, DIAG_LOG_BUFFER_SIZE, "[%04u-%02u-%02u %02u:%02u:%02u.%03u]");
    logLen = (ret < 0) ? 0 : (u32)ret;

    //添加进程标识
    ret = sprintf(&(buffer[logLen]), "[%d]", getpid());
    logLen = (ret < 0) ? logLen : (logLen+(u32)ret);    

    va_start(pArgList, format);
    ret = vsnprintf(&(buffer[logLen]), (u64)(DIAG_LOG_BUFFER_SIZE-logLen-1),format, pArgList);
    va_end(pArgList);
    logLen = (ret < 0) ? (DIAG_LOG_BUFFER_SIZE) : (logLen+(u32)ret);

    diag_clientDoWriteLog(buffer, (u32)logLen);
}
#endif

