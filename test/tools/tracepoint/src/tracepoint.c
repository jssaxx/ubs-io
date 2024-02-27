/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: 无
 */
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <malloc.h>
#include <stdbool.h>

#include "cli.h"
#include "dpax_list.h"
#include "tracepoint_common.h"
#include "tracepoint.h"
#include "tracepoint_cmd.h"
#include "tracepoint_pub.h"

MODULE_ID(PID_DEBUG);


#ifdef LVOS_TP_LOGID
#undef LVOS_TP_LOGID
#endif

#ifndef NSEC_PER_MS
#define NSEC_PER_MS 1000000UL
#endif

#ifndef MSEC_PER_SEC
#define MSEC_PER_SEC    1000UL
#endif


#define TRACEPOINT_PRINT_LIMIT_INTERVAL (60*HZ)
#define TRACEPOINT_PRINT_LIMIT_COUNT (3)

#define LVOS_TP_LOGID (0L)

static LVOS_TRACEP_S g_stTp[LVOS_MAX_TRACEP_NUM]; /* 保存所有的TracePoint信息 */

/* add by gzb:HVS新框架 begin */
static LVOS_TP_INIT_ACTIVE_S g_initActiveTPs[LVOS_TP_MAX_INIT_ACTIVE_NUM];   /* 保存初始状态为激活的TP */
static int32_t g_initActiveTPNum = 0;           /* 初始状态为激活的TP数量 */
LVOS_HT_S *g_TPHt = NULL;      /* HVS新框架TracePoint的Hash表 */
static pthread_spinlock_t g_TPHtLock;

void TracePointInit()
{
    pthread_spin_init(&g_TPHtLock, 0);
    TracePointCmdInit();
}

void TracePointExit()
{
    TracePointCmdExit();
    pthread_spin_destroy(&g_TPHtLock);
}

/************************************************************************
* 函数名称: static uint32_t LVOS_HVS_getHashAddr(uint32_t v_uiKey1, uint64_t v_ullKey2, uint32_t v_uiBits)
*
* 功能           : Hash函数
*
* 输入参数:
*                        1)v_uiKey1 : Key1
*                        2)v_ullKey2: Key2
*                        3)v_uiBits : Hash算法参数
*
* 输出参数: 无
*
* 返回值      : Hash地址
*
* 调用其他函数列表: 无
*
*************************************************************************/
static uint32_t LVOS_HVS_getHashAddr(uint32_t v_uiKey1, uint64_t v_ullKey2, uint32_t v_uiBits)
{
    uint64_t ullTmp;
    KEY_S stKey;

    stKey.unKey64.ull64bits = v_ullKey2;
    stKey.unKey64.ui32bits[1] = v_uiKey1;
    /*lint -e740*/
    ullTmp = *((uint64_t *) (&stKey));
    /*lint +e740*/
    /*lint -e435*/
    ullTmp *= LVOS_GOLDEN_RATIO_PRIME;
    /*lint +e435*/

    /* 将最后得到的随机数右移防止hash表索引值越界 */
    return (uint32_t) (ullTmp >> (LVOS_MAX_HT_BITS - v_uiBits));
}

/*****************************************************************************
* 功    能: 字符串转换为u64的HASH key
                    公式:s[0]*31^(n-1) + s[1]*31^(n-2) + ... + s[n-1]
* 输入参数: char str[]
* 输出参数: key
* 返回值  : hash key -1:error
* 其 他   : 无
*****************************************************************************/
static int32_t LVOS_HVS_strToHashKey(const char *str, uint32_t maxLen, uint64_t *key)
{
    uint32_t i = 0;
    uint32_t len = 0;
    uint64_t keyTemp = 0ULL;

    if (unlikely(NULL == str || NULL == key)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    len = (uint32_t) strnlen(str, (size_t) maxLen);
    if (unlikely(0 == len || len >= maxLen)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Length of str is zero or beyond the max length.");
        return RETURN_ERROR;
    }

    for (i = 0; i < len; ++i) {
        keyTemp = LVOS_STR_TO_KEY_BASE_NUM * keyTemp + (uint8_t) str[i];
    }

    *key = keyTemp;

    return RETURN_OK;
}

/************************************************************************
* 函数名称:  uint32_t LVOS_HVS_createHashTable(uint32_t v_uiHashSize, uint32_t v_uiBits, LVOS_HT_S **v_ppstHashTable)
*
* 功能           : 新建Hash表。
*                  为防止越界,v_uiHashSize应为2的v_uiBits次方幂
*
* 输入参数:
*                        1)v_uiHashSize: Hash表长度
*                        2)v_uiBits:Hash算法参数
*                        3)v_uiPid :创建Hash的模块ID
*
* 输出参数:
*                        1)v_ppstHashTable: 新建的Hash表指针
*
* 返回值      :
*                        1)RETURN_OK: 创建成功
*                        2)RETURN_PARAM_ERROR: 输入参数错误
*                        3)RETURN_NO_MEM: 申请内存失败
*
* 调用其他函数列表: 无
*
*************************************************************************/
int32_t LVOS_HVS_createHashTable(uint32_t v_uiHashSize, uint32_t v_uiBits, LVOS_HT_S **v_ppstHashTable)
{
    LVOS_HT_S *pstHash = NULL;
    uint32_t uiCount = 0;

    /* 检查参数合法性 */
    if ((NULL == v_ppstHashTable) || (0 == v_uiHashSize) || (v_uiBits > LVOS_MAX_HT_BITS)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is invalid.");
        return RETURN_ERROR;
    }

    /* 分配Hash表结构 */
    pstHash = (LVOS_HT_S *) malloc((uint32_t) sizeof(LVOS_HT_S));
    if (NULL == pstHash) {
        DBG_LogInfo(LVOS_TP_LOGID, "malloc LVOS_HT_S fail.");
        *v_ppstHashTable = NULL;
        return RETURN_ERROR;
    }

    /* 分配Hash表项数组 */
    pstHash->pstHashElem = (LVOS_HASH_LIST_S *) malloc((uint32_t) (v_uiHashSize * sizeof(LVOS_HASH_LIST_S)));
    if (NULL == pstHash->pstHashElem) {
        DBG_LogInfo(LVOS_TP_LOGID, "malloc hashtable array fail");
        free(pstHash);
        *v_ppstHashTable = NULL;
        return RETURN_ERROR;
    }

    /* 初始化Hash表 */
    pstHash->uiTableSize = v_uiHashSize;
    pstHash->uiBits = v_uiBits;
    for (uiCount = 0; uiCount < v_uiHashSize; uiCount++) {
        INIT_LIST_HEAD(&(pstHash->pstHashElem[uiCount].stBucketList));
    }

    *v_ppstHashTable = pstHash;
    return RETURN_OK;
}

/************************************************************************
* 函数名称:  void LVOS_HVS_destroyHashTable(LVOS_HT_S **v_ppstHashTable);
*
* 功能           : 释放Hash表及表中存储元素,并将*v_ppstHashTable置为NULL
*
* 输入参数:
*                        1)v_pstHashTable: Hash表指针
*
* 输出参数: 无
*
* 返回值      : 无
*
* 调用其他函数列表: 无
*
*************************************************************************/
void LVOS_HVS_destroyHashTable(LVOS_HT_S **v_ppstHashTable)
{
    /* 检查参数合法性 */
    if (NULL == v_ppstHashTable || NULL == *v_ppstHashTable) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return;
    }

    /* 释放Hash表项数组  */
    free((*v_ppstHashTable)->pstHashElem);

    /* 释放Hash表结构 */
    free(*v_ppstHashTable);
}

/************************************************************************
* 函数名称:  uint32_t LVOS_HVS_insertHashItem(LVOS_HT_S *v_pstHashTable, LVOS_HASH_ITEM_S *v_pstData);
*
* 功能           : 插入Hash表
*
* 输入参数:
*                        1)v_pstHashTable: Hash表指针
*                        2)v_pstData: 插入Hash表的数据
*
* 输出参数: 无
*
* 返回值      :
*                         1)RETURN_OK: 插入成功
*                         2)HASH_DUPLICATE: 指定元素已存在
*
* 调用其他函数列表:
*                         1)getHashAddr
*
*************************************************************************/
int32_t LVOS_HVS_insertHashItem(LVOS_HT_S *v_pstHashTable, LVOS_HASH_ITEM_S *v_pstData)
{
    uint32_t uiAddr = 0;
    LVOS_HASH_LIST_S *pstElem = NULL;
    LVOS_HASH_ITEM_S *pstData = NULL;
    struct list_head *pstPos = NULL;
    bool validParam = false;
    bool multipleRegistration = false;

    /* 检查参数合法性 */
    if ((NULL == v_pstHashTable) || (NULL == v_pstData)) {
        PRINT_LIMIT(DBG_LOG_INFO, (long) DBG_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL, TRACEPOINT_PRINT_LIMIT_COUNT,
                    validParam);
        if (true == (int) validParam) {
            DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        }
        return RETURN_ERROR;
    }

    /* 计算Hash地址 */
    uiAddr = LVOS_HVS_getHashAddr(v_pstData->uiKey1, v_pstData->ullKey2, v_pstHashTable->uiBits);
    pstElem = &(v_pstHashTable->pstHashElem[uiAddr]);

    /* 判断是否已存在该项 */
    pthread_spin_lock(&g_TPHtLock);
    dpax_list_for_each(pstPos, &pstElem->stBucketList) {
        /* lint -e718 -e746 -e40 */
        pstData = dpax_list_entry(pstPos, LVOS_HASH_ITEM_S, stConflictList);
        /* lint +e718 +e746 +e40 */
        if ((pstData->uiKey1 == v_pstData->uiKey1) && (pstData->ullKey2 == v_pstData->ullKey2)) {
            pthread_spin_unlock(&g_TPHtLock);
            PRINT_LIMIT(DBG_LOG_INFO, (long) DBG_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL,
                        TRACEPOINT_PRINT_LIMIT_COUNT, multipleRegistration);
            if (true == (int) multipleRegistration) {
                DBG_LogInfo(LVOS_TP_LOGID, "Hash item already exists.");
            }
            return RETURN_ERROR;
        }
    }
    /* 若不存在该项，则在hash表加入该元素 */
    dpax_list_add(&v_pstData->stConflictList, &pstElem->stBucketList);
    pthread_spin_unlock(&g_TPHtLock);
    return RETURN_OK;
}

/************************************************************************
* 函数名称:  uint32_t LVOS_HVS_removeHashItem(LVOS_HT_S *v_pstHashTable, uint32_t v_uiKey1, uint64_t v_ullKey2)
*
* 功能           : 删除Hash表中某元素
*
* 输入参数:
*                        1)v_pstHashTable: Hash表指针
*                        2)v_uiKey1 : 删除的元素的uiKey1
*                        3)v_ullKey2: 删除的元素的ullKey2
*
* 输出参数: 无
*
* 返回值      :
*                          1)RETURN_OK: 删除成功
*                          2)RETURN_ERROR: 删除失败, 指定元素不存在
*                          3)RETURN_PARAM_ERROR:参数错误
*
* 调用其他函数列表:
*                         1)getHashAddr
*
*************************************************************************/
int32_t LVOS_HVS_removeHashItem(LVOS_HT_S *v_pstHashTable, uint32_t v_uiKey1, uint64_t v_ullKey2)
{
    LVOS_HASH_LIST_S *pstElem = NULL;
    LVOS_HASH_ITEM_S *pstData = NULL;
    struct list_head *pstPos = NULL;
    uint32_t uiAddr = 0;

    /* 检查参数合法性 */
    if (NULL == v_pstHashTable) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    /* 计算Hash地址 */
    uiAddr = LVOS_HVS_getHashAddr(v_uiKey1, v_ullKey2, v_pstHashTable->uiBits);
    pstElem = &(v_pstHashTable->pstHashElem[uiAddr]);

    /* 扫描冲突链，查找要删除的元素 */
    pthread_spin_lock(&g_TPHtLock);
    dpax_list_for_each(pstPos, &pstElem->stBucketList) {
        /*lint -e40*/
        pstData = dpax_list_entry(pstPos, LVOS_HASH_ITEM_S, stConflictList);
        /*lint +e40*/
        if ((v_uiKey1 == pstData->uiKey1) && (v_ullKey2 == pstData->ullKey2)) {
            dpax_list_del_init(&pstData->stConflictList);
            pthread_spin_unlock(&g_TPHtLock);
            return RETURN_OK;
        }
    }
    /* 若不存在该项，则返回失败 */
    pthread_spin_unlock(&g_TPHtLock);
    return RETURN_ERROR;
}

/************************************************************************
* 函数名称:  uint32_t LVOS_HVS_searchHashItem(LVOS_HT_S *v_pstHashTable, uint32_t v_uiKey1, uint64_t v_ullKey2,
                                     LVOS_HASH_ITEM_S **v_ppstData)
*
* 功能           : 查找Hash表中某元素
*
* 输入参数:
*                        1)v_pstHashTable: Hash表指针
*                        2)v_uiKey1 : 查找的元素的uiKey1
*                        3)v_ullKey2: 查找的元素的ullKey2
*
* 输出参数:
*                        1)v_ppstData: 查找到的元素指针
*
* 返回值      :
*                         1)RETURN_OK: 成功返回
*                         2)RETURN_ERROR: 未找到指定元素
*                         3)RETURN_PARAM_ERROR:参数错误
*
* 调用其他函数列表:
*                         1)getHashAddr
*
*************************************************************************/
int32_t LVOS_HVS_searchHashItem(LVOS_HT_S *v_pstHashTable, uint32_t v_uiKey1, uint64_t v_ullKey2,
                                LVOS_HASH_ITEM_S **v_ppstData)
{
    LVOS_HASH_LIST_S *pstElem = NULL;
    LVOS_HASH_ITEM_S *pstData = NULL;
    struct list_head *pstPos = NULL;
    uint32_t uiAddr = 0;

    /* 检查参数合法性 */
    if ((NULL == v_pstHashTable) || (NULL == v_ppstData)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    /* 计算Hash地址 */
    uiAddr = LVOS_HVS_getHashAddr(v_uiKey1, v_ullKey2, v_pstHashTable->uiBits);
    pstElem = &(v_pstHashTable->pstHashElem[uiAddr]);

    /* 扫描冲突链，查找指定元素 */
    pthread_spin_lock(&g_TPHtLock);
    dpax_list_for_each(pstPos, &pstElem->stBucketList) {
        /*lint -e40*/
        pstData = dpax_list_entry(pstPos, LVOS_HASH_ITEM_S, stConflictList);
        /*lint +e40*/
        if ((v_uiKey1 == pstData->uiKey1) && (v_ullKey2 == pstData->ullKey2)) {
            *v_ppstData = pstData;
            pthread_spin_unlock(&g_TPHtLock);
            return RETURN_OK;
        }
    }

    /* 若不存在该项，则返回失败 */
    pthread_spin_unlock(&g_TPHtLock);
    *v_ppstData = NULL;

    return LVOS_TP_RETURN_NOT_FOUND;
}


/************************************************************************
* 函数名称:  uint32_t LVOS_HVS_travelHashTable(LVOS_HT_S *v_pstHashTable, void (*fn)(LVOS_HASH_ITEM_S *, void *), void *param)
*
* 功能           : 遍历HASH表
*
* 输入参数:
*                        1)v_pstHashTable: Hash表指针
*                        2)fn : 遍历处理函数
                          3)param:处理函数的参数
*
* 输出参数:                无
*
*
* 返回值      :
*                         1)RETURN_OK: 成功返回
*                         2)RETURN_ERROR: 未找到指定元素
*
* 调用其他函数列表:         无
*
*
*************************************************************************/
int32_t LVOS_HVS_travelHashTable(LVOS_HT_S *v_pstHashTable, void (*fn)(LVOS_HASH_ITEM_S *, void *), void *param)
{
    uint32_t i;
    struct list_head *pstPos = NULL;
    LVOS_HASH_ITEM_S *pstData = NULL;
    LVOS_HASH_LIST_S *pstElem = NULL;

    /* 检查参数合法性 ，param可以为NULL，表示fn不需要额外参数 */
    if ((NULL == v_pstHashTable) || (NULL == fn)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    for (i = 0; i < v_pstHashTable->uiTableSize; ++i) {
        pstElem = &(v_pstHashTable->pstHashElem[i]);
        /* 扫描冲突链，查找指定元素 */
        pthread_spin_lock(&g_TPHtLock);
        dpax_list_for_each(pstPos, &pstElem->stBucketList) {
            /*lint -e40*/
            pstData = dpax_list_entry(pstPos, LVOS_HASH_ITEM_S, stConflictList);
            /*lint +e40*/
            fn(pstData, param);
        }
        pthread_spin_unlock(&g_TPHtLock);
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 去激活tracepoint(用于遍历)
* 输入参数: hashdata
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_HVS_deactiveForTravel(LVOS_HASH_ITEM_S *hashdata, void *param)
{
    LVOS_TP_HASH_S *hashTPData = NULL;

    /* param应该为NULL，不处理 */
    if (NULL != param) {
        DBG_LogWarning(LVOS_TP_LOGID, "param should be NULL.");
    }

    if (NULL == hashdata) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return;
    }

    /* Tracepoint恢复为初始值 */
    hashTPData = (LVOS_TP_HASH_S *) (void *) hashdata;
    hashTPData->stTP.iActive = LVOS_TRACEP_STAT_DEACTIVE;
    hashTPData->stTP.type = LVOS_TP_TYPE_BUTT;
    hashTPData->stTP.timeAlive = 0;
    hashTPData->stTP.timeCalled = 0;
    memset(&(hashTPData->stTP.stParam.achParamData[0]), 0, (size_t) LVOS_TRACEP_PARAM_SIZE);
}

static LVOS_TP_TYPE_E LVOS_get_tp_type(const char *type)
{
    LVOS_TP_TYPE_E tp_type = LVOS_TP_TYPE_BUTT;
    if (!type) {
        return tp_type;
    }
    if (0 == strcmp(type, "callback")) {
        tp_type = LVOS_TP_TYPE_CALLBACK;
    } else if (0 == strcmp(type, "reset")) {
        tp_type = LVOS_TP_TYPE_RESET;
    } else if (0 == strcmp(type, "pause")) {
        tp_type = LVOS_TP_TYPE_PAUSE;
    } else if (0 == strcmp(type, "abort")) {
        tp_type = LVOS_TP_TYPE_ABORT;
    } else {
        DBG_LogWarning(LVOS_TP_LOGID, "Type of config file is incorrect.");
    }
    return tp_type;
}

/*****************************************************************************
* 功    能: 解析Tracepoint初始激活配置文件的一行
* 输入参数: tpInfo
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
int32_t LVOS_HVS_parseConfigLine(char *tpInfo, int32_t index)
{
    LVOS_TP_INIT_ACTIVE_E step = LVOS_TP_INIT_ACTIVE_NAME;
    int32_t pos = 0;
    uint64_t interval = 0ULL;
    uint64_t key = 0ULL;
    int32_t ret;
    /* start和end用于标识字符串中每一配置项的起始和结束 */
    char *start = NULL;

    if (NULL == tpInfo) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    start = tpInfo;
    /* 按顺序解析配置文件的行，配置项以空格隔开 */
    while (step < LVOS_TP_INIT_ACTIVE_BUTT) {
        if (RETURN_OK != LVOS_getCharPos(start, ' ', &pos)) {
            DBG_LogWarning(LVOS_TP_LOGID, "Format of config file is incorrect.");
            return RETURN_ERROR;
        }
        /* 截断已解析部分 */
        start[pos] = '\0';
        /* Name */
        if (LVOS_TP_INIT_ACTIVE_NAME == step) {
            /* 计算HASH key */
            if (RETURN_OK != LVOS_HVS_strToHashKey(start, MAX_NAME_LEN, &key)) {
                DBG_LogWarning(LVOS_TP_LOGID, "Cannot make the key from %s.", start);
                return RETURN_ERROR;
            }
            g_initActiveTPs[index].name = key;
        } else if (LVOS_TP_INIT_ACTIVE_TYPE == step) { /* Type */
            LVOS_TP_TYPE_E type = LVOS_get_tp_type(start);
            if (type == LVOS_TP_TYPE_BUTT) {
                return RETURN_ERROR;
            }
            g_initActiveTPs[index].type = type;
        } else if (LVOS_TP_INIT_ACTIVE_ALIVE == step) { /* num of alive */
            g_initActiveTPs[index].timeAlive = (uint32_t)atoll(start);

            /* 剩下的为userparam */
            start += pos + 1;

            /* 过滤换行符 */
            LVOS_filterNewlineSymbol(start);

            strcpy(g_initActiveTPs[index].param, start);
            g_initActiveTPs[index].param[LVOS_TRACEP_PARAM_SIZE - 1] = '\0';

            /* 解析完毕，退出循环 */
            break;
        }

        /* start指向下一个配置项 */
        start += pos + 1;
        step++;
    }

    if (LVOS_TP_TYPE_PAUSE == g_initActiveTPs[index].type) {
        /* pause的时间非法，自动记为10秒 */
        interval = atoll(g_initActiveTPs[index].param);
        if (interval == 0) {
            strcpy(g_initActiveTPs[index].param, "10000");
        }
    }

    return RETURN_OK;
}

/*****************************************************************************
* 功    能: 解析Tracepoint初始激活配置文件
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_HVS_parseConfigFile(const char *fileName)
{
    int32_t ret = RETURN_ERROR;
    int32_t fd = -1;
    char tpInfo[LVOS_TP_MAX_CFG_LINE];

    ret = access(fileName, F_OK);
    if (RETURN_OK == ret) {
        /* 配置文件存在则解析 */
        fd = open(fileName, O_RDONLY, S_IRUSR);
        if (fd < 0) {
            DBG_LogWarning(LVOS_TP_LOGID, "Open file %s failed.", fileName);
            return;
        }

        memset(tpInfo, 0, sizeof(tpInfo));
        while (NULL != LVOS_readline(fd, tpInfo, (OSP_S32)sizeof(tpInfo))) {

            tpInfo[LVOS_TP_MAX_CFG_LINE - 1] = '\0';

            if (RETURN_OK == LVOS_HVS_parseConfigLine(tpInfo, g_initActiveTPNum)) {
                g_initActiveTPNum++;
            }
            memset(tpInfo, 0, sizeof(tpInfo));
        }

        (void)close(fd);
    }

}


/*****************************************************************************
* 功    能: 初始化HASH表
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_HVS_initTracePoint(void)
{
    if (RETURN_OK != LVOS_HVS_createHashTable(LVOS_MAX_TP_HASH_SIZE, LVOS_MAX_TP_HASH_SHIFT, &g_TPHt)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Create Hash table failed.");
        g_TPHt = NULL;
        return;
    }

    return;
}

/*****************************************************************************
* 功    能: 卸载HASH表
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_HVS_exitTracePoint(void)
{
    LVOS_HVS_destroyHashTable(&g_TPHt);
    g_TPHt = NULL;
    return;
}


/*****************************************************************************
* 功    能: pause类型Tracepoint的处理函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_HVS_doTracePointPause(LVOS_TRACEP_NEW_S *tracepoint)
{
    uint32_t interval;
    s32 ret;
    struct timespec rqtp;
    struct timespec rmtp;

    if (NULL == tracepoint) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parmeter tracepoint is NULL.");
        return;
    } else {
        interval = (uint32_t)atoll(tracepoint->stParam.achParamData);
    }

    DBG_LogInfo(LVOS_TP_LOGID, "Tracepoint(%s) pause %d milliseconds.", tracepoint->szName, interval);

    rqtp.tv_sec = interval / MSEC_PER_SEC;
    rmtp.tv_sec = rqtp.tv_sec;
    rqtp.tv_nsec = (interval % MSEC_PER_SEC) * NSEC_PER_MS;
    rmtp.tv_nsec = rqtp.tv_nsec;
    for (;;) {
        ret = nanosleep(&rqtp, &rmtp);
        if (0 == ret || errno != EINTR) {
            break;
        }
        memcpy(&rqtp, &rmtp, sizeof(struct timespec));
    }

    return;
}
EXPORT_SYMBOL(LVOS_HVS_doTracePointPause);

/*****************************************************************************
* 功    能:获取Tracepoint
* 输入参数: pid, name
* 输出参数: tracepoint
* 返回值  : Tracepoint
* 其 他   : 无
*****************************************************************************/
int32_t LVOS_HVS_getTracePoint(uint32_t pid, const char *name, LVOS_TRACEP_NEW_S **tracepoint)
{
    int32_t ret = RETURN_ERROR;
    uint64_t key = 0ULL;
    LVOS_HASH_ITEM_S *hashData = NULL;
    LVOS_TP_HASH_S *hashTPData = NULL;
    bool bcan = false;

    if (NULL == name || NULL == tracepoint) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL, get tracepoint failed.");
        return RETURN_ERROR;
    }

    /* 计算HASH key */
    if (RETURN_OK != LVOS_HVS_strToHashKey(name, MAX_NAME_LEN, &key)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Cannot make the key from tracepoint name.");
        *tracepoint = NULL;
        return RETURN_ERROR;
    }

    ret = LVOS_HVS_searchHashItem(g_TPHt, pid, key, &hashData);
    if (RETURN_OK != ret) {
        PRINT_LIMIT(DBG_LOG_INFO, (long)DBG_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL, TRACEPOINT_PRINT_LIMIT_COUNT, bcan);
        if (true == (int)bcan) {
            DBG_LogInfo(LVOS_TP_LOGID, "Search tracepoint(%s) failed.", name);
        }
        *tracepoint = NULL;
        return ret;
    }

    hashTPData = (LVOS_TP_HASH_S *)(void *)hashData;
    *tracepoint = &(hashTPData->stTP);

    return RETURN_OK;
}
EXPORT_SYMBOL(LVOS_HVS_getTracePoint);

/*****************************************************************************
* 功    能: 注册Tracepoint到HASH表中
* 输入参数: pid, name, desc, fnHook
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
int32_t LVOS_HVS_regTracePoint(uint32_t pid, const char *name, const char *desc, FN_TRACEP_COMMON_T fnHook)
{
    uint64_t key = 0ULL;
    int32_t loop = 0;
    static bool isFirstReg = TRUE;
    int32_t activeStat = LVOS_TRACEP_STAT_DEACTIVE;
    uint32_t timeAlive = 0;
    int32_t type = LVOS_TP_TYPE_BUTT;
    int32_t ret;
    LVOS_TP_HASH_S *hashData = NULL;
    bool bcan = false;

    if (NULL == name || NULL == desc) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL, register failed.");
        return RETURN_ERROR;
    }

    /* 第一次注册，触发解析配置文件 */
    if (isFirstReg) {
        memset(g_initActiveTPs, 0, sizeof(g_initActiveTPs));
        LVOS_HVS_parseConfigFile(LVOS_TP_CONFIG_FILE);
        LVOS_HVS_parseConfigFile(LVOS_TP_CONFIG_TMP_FILE);
        isFirstReg = FALSE;
    }

    /* 计算HASH key */
    if (RETURN_OK != LVOS_HVS_strToHashKey(name, MAX_NAME_LEN, &key)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Cannot make the key from tracepoint name.");
        return RETURN_ERROR;
    }

    hashData = (LVOS_TP_HASH_S *)malloc((uint32_t)sizeof(LVOS_TP_HASH_S));
    if (NULL == hashData) {
        DBG_LogInfo(LVOS_TP_LOGID, "Allocate hash data failed.");
        return RETURN_ERROR;
    }

    memset(hashData, 0, sizeof(LVOS_TP_HASH_S));

    /* 查询TP是否初始激活 */
    for (loop = 0; loop <= g_initActiveTPNum; loop++) {
        if (key == g_initActiveTPs[loop].name) {
            /* 回调函数为NULL，但初始激活为callback的，不激活 */
            if (LVOS_TP_TYPE_CALLBACK == g_initActiveTPs[loop].type && NULL == fnHook) {
                DBG_LogWarning(LVOS_TP_LOGID, "Tracepoint %s init active is invalid.", name);
                break;
            }

            activeStat = LVOS_TRACEP_STAT_ACTIVE;
            type = g_initActiveTPs[loop].type;
            timeAlive = g_initActiveTPs[loop].timeAlive;
            strcpy(&hashData->stTP.stParam.achParamData[0], g_initActiveTPs[loop].param);
            hashData->stTP.stParam.achParamData[LVOS_TRACEP_PARAM_SIZE - 1] = '\0';

            DBG_LogInfo(LVOS_TP_LOGID, "Tracepoint %s is initially actived.", name);
            break;
        }
    }

    /* 初始化Tracepoint */
    /* 新框架PID全部用无效值0计算HASH */
    hashData->uiKey1 = PID_OSP_NULL;
    hashData->ullKey2 = key;
    hashData->stTP.uiPid = pid;
    /* 注册后默认为不激活 */
    hashData->stTP.iActive = activeStat;
    hashData->stTP.type = type;
    hashData->stTP.timeAlive = timeAlive;
    hashData->stTP.timeCalled = 0;
    strcpy(&hashData->stTP.szName[0], name);
    hashData->stTP.szName[MAX_NAME_LEN - 1] = '\0';
    strcpy(&hashData->stTP.szDesc[0], desc);
    hashData->stTP.szDesc[MAX_DESC_LEN - 1] = '\0';
    hashData->stTP.fnHook = fnHook;

    /* 将Tracepoint链入Hash table */
    if (RETURN_OK != LVOS_HVS_insertHashItem(g_TPHt, (LVOS_HASH_ITEM_S *)(void *)hashData)) {
        PRINT_LIMIT(DBG_LOG_INFO, (long)DBG_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL, TRACEPOINT_PRINT_LIMIT_COUNT, bcan);
        if (true == (int)bcan) {
            DBG_LogInfo(LVOS_TP_LOGID, "Insert tracepoint(%s) to hash table failed.", name);
        }
        free(hashData);
        return RETURN_ERROR;
    }

    /* hashData在malloc后慈?Hash table，不需要free */
    return RETURN_OK;
}
EXPORT_SYMBOL(LVOS_HVS_regTracePoint);

/*****************************************************************************
* 功    能: 从HASH表中注销Tracepoint
* 输入参数: pid, name
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
int32_t LVOS_HVS_unregTracePoint(uint32_t pid, const char *name)
{
    uint32_t key1 = pid;
    uint64_t key2 = 0ULL;
    LVOS_HASH_ITEM_S *hashData = NULL;
    bool bcan = false;

    if (NULL == name) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    /* 计算HASH key */
    if (RETURN_OK != LVOS_HVS_strToHashKey(name, MAX_NAME_LEN, &key2)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Cannot make the key from tracepoint name.");
        return RETURN_ERROR;
    }

    /* 查找HashTable */
    if (RETURN_OK != LVOS_HVS_searchHashItem(g_TPHt, key1, key2, &hashData)) {
        PRINT_LIMIT(DBG_LOG_INFO, (long)DBG_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL, TRACEPOINT_PRINT_LIMIT_COUNT, bcan);
        if (true == (int)bcan) {
            DBG_LogInfo(LVOS_TP_LOGID, "Search tracepoint(%s) failed.", name);
        }
        return RETURN_ERROR;
    }

    /* 链表操作,查找成功就不会删除失败，不判断返回值 */
    (void)LVOS_HVS_removeHashItem(g_TPHt, key1, key2);

    free(hashData);

    return RETURN_OK;
}
EXPORT_SYMBOL(LVOS_HVS_unregTracePoint);

/*****************************************************************************
* 功    能:激活Tracepoint
* 输入参数: pid, name, userParam
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
int32_t LVOS_HVS_activeTracePoint(uint32_t pid, const char *name, int32_t type, uint32_t time, LVOS_TRACEP_PARAM_S userParam)
{
    int32_t ret = RETURN_ERROR;
    LVOS_TRACEP_NEW_S *tracepoint = NULL;

    if (NULL == name) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    if (type > LVOS_TP_TYPE_BUTT) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is not correct,type=%d.", type);
        return RETURN_ERROR;
    }

    /* 获取tracepoint */
    ret = LVOS_HVS_getTracePoint(pid, name, &tracepoint);
    if (NULL == tracepoint) {
        DBG_LogInfo(LVOS_TP_LOGID, "Get tracepoint(%s) failed.", name);
        return ret;
    }

    /* 回调函数为NULL，不能被激活 */
    if (LVOS_TP_TYPE_CALLBACK == type && NULL == tracepoint->fnHook) {
        DBG_LogInfo(LVOS_TP_LOGID,
            "Callback function of tracepoint(%s) is NULL, cannot active callback type.", name);
        return LVOS_TP_RETURN_CALLBACK_NULL;
    }

    tracepoint->iActive = LVOS_TRACEP_STAT_ACTIVE;
    tracepoint->type = type;
    tracepoint->timeAlive = time;
    tracepoint->timeCalled = 0;

    strcpy(&tracepoint->stParam.achParamData[0], &userParam.achParamData[0]);
    tracepoint->stParam.achParamData[LVOS_TRACEP_PARAM_SIZE - 1] = '\0';

    DBG_LogInfo(LVOS_TP_LOGID, "Tracepoint(%s) is active.", name);

    return RETURN_OK;
}
EXPORT_SYMBOL(LVOS_HVS_activeTracePoint);

/*****************************************************************************
* 功    能:去激活Tracepoint
* 输入参数: pid, name
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
int32_t LVOS_HVS_deactiveTracePoint(uint32_t pid, const char *name)
{
    int32_t ret = RETURN_ERROR;
    LVOS_TRACEP_NEW_S *tracepoint = NULL;

    if (NULL == name) {
        DBG_LogInfo(LVOS_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    /* 获取Tracepoint */
    ret = LVOS_HVS_getTracePoint(pid, name, &tracepoint);
    if (NULL == tracepoint) {
        DBG_LogInfo(LVOS_TP_LOGID, "Get tracepoint(%s) failed.", name);
        return ret;
    }

    /* Tracepoint所有信息恢复为初始值 */
    tracepoint->iActive = LVOS_TRACEP_STAT_DEACTIVE;
    tracepoint->type = LVOS_TP_TYPE_BUTT;
    tracepoint->timeAlive = 0;
    tracepoint->timeCalled = 0;
    memset(&tracepoint->stParam.achParamData[0], 0, (size_t)LVOS_TRACEP_PARAM_SIZE);

    DBG_LogInfo(LVOS_TP_LOGID, "Tracepoint(%s) is deactive.", name);

    return RETURN_OK;
}
EXPORT_SYMBOL(LVOS_HVS_deactiveTracePoint);


/*****************************************************************************
* 功    能:去激活所有Tracepoint
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
int32_t LVOS_HVS_deactiveTracePointAll(void)
{
    /* 遍历Hash表，执行去激活动作 */
    return LVOS_HVS_travelHashTable(g_TPHt, LVOS_HVS_deactiveForTravel, NULL);
}

/************************************************************************
* 函数名称:  LVOS_HT_S *LVOS_HVS_getHashTable(void)
*
* 功能           : 供外部模块获取tracepoint的全局Hash表指针,屏蔽本模块的全局变量。
*
*
* 输入参数:
*                        无
* 输出参数:
*                        无
*
* 返回值      :
*                        1)全局Hash表指针
*
* 调用其他函数列表: 无
*  ps:  可以考虑定义成inline格式
*************************************************************************/
LVOS_HT_S *LVOS_HVS_getHashTable(void)
{
    return g_TPHt;
}

/* add by gzb:HVS新框架 end */

/*****************************************************************************
* 功    能: TracePoint遍历函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_WalkTracePoint(OSP_S32 (*v_pfn)(LVOS_TRACEP_S *, void *), void *v_pParam)
{
    OSP_U32 uiIndex;
    OSP_S32 iRet;

    if ((NULL == v_pfn) || (NULL == v_pParam)) {
        return;
    }
    for (uiIndex = 0; uiIndex < LVOS_MAX_TRACEP_NUM; uiIndex++) {
        if (LVOS_TRACEP_STAT_DELETED == g_stTp[uiIndex].iActive) {
            continue;
        }

        iRet = v_pfn(&g_stTp[uiIndex], v_pParam);
        if (RETURN_OK != iRet) {
            return;
        }
    }
}

/*****************************************************************************
* 功    能: Hook遍历函数
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_WalkTraceHook(LVOS_TRACEP_S *v_pstTp, OSP_S32 (*v_pfn)(LVOS_TRACEP_HOOK_S *, void *), void *v_pParam)
{
    OSP_U32 uiIndex;
    OSP_S32 iRet;

    if ((NULL == v_pstTp) || (NULL == v_pfn) || (NULL == v_pParam)) {
        return;
    }
    for (uiIndex = 0; uiIndex < LVOS_MAX_HOOK_PER_TRACEP; uiIndex++) {
        if (LVOS_TRACEP_STAT_DELETED == v_pstTp->stHooks[uiIndex].iActive) {
            continue;
        }

        iRet = v_pfn(&v_pstTp->stHooks[uiIndex], v_pParam);
        if (RETURN_OK != iRet) {
            return;
        }
    }

    return;
}

/*****************************************************************************
* 功    能: 查找TracePoint
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
LVOS_TRACEP_S *LVOS_FindTracePoint(OSP_U32 v_uiPid, const OSP_CHAR *v_szName)
{
    OSP_U32 uiIndex;

    if (unlikely(NULL == v_szName)) {
        DBG_LogInfo(LVOS_TP_LOGID, "v_szName is NULL.");
        return NULL;
    }

    for (uiIndex = 0; uiIndex < LVOS_MAX_TRACEP_NUM; uiIndex++) {
        if (v_uiPid != g_stTp[uiIndex].uiPid) {
            continue;
        }

        if (LVOS_TRACEP_STAT_DELETED == g_stTp[uiIndex].iActive) {
            continue;
        }

        /* 只比较前面有效部分 */
        if (0 != strncmp(v_szName, g_stTp[uiIndex].szName, sizeof(g_stTp[uiIndex].szName) - 1)) {
            continue;
        }

        return g_stTp + uiIndex;
    }

    return NULL;
}

EXPORT_SYMBOL(LVOS_FindTracePoint);

/*****************************************************************************
* 功    能: 获取空闲TracePoint空间
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
LVOS_TRACEP_S *LVOS_GetFreeTracePoint(void)
{
    OSP_U32 uiIndex;

    for (uiIndex = 0; uiIndex < LVOS_MAX_TRACEP_NUM; uiIndex++) {
        if (LVOS_TRACEP_STAT_DELETED == g_stTp[uiIndex].iActive) {
            return g_stTp + uiIndex;
        }
    }

    return NULL;
}

/*****************************************************************************
* 功    能: 注册TracePoint处理接口
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_RegTracePoint(OSP_U32 v_uiPid, const OSP_CHAR *v_szName, const OSP_CHAR *v_szDesc, OSP_S32 v_iInitState,
                        OSP_S32 v_iDbgOnly)
{
    LVOS_TRACEP_S *pstTp = NULL;
    size_t len_name;
    size_t len_desc;
    int32_t ret;

    if ((NULL == v_szName) || (NULL == v_szDesc)) {
        DBG_LogInfo(LVOS_TP_LOGID, "NULL pointer of v_szName or v_szDesc, register fail Module: (%s).",
                    DBG_GetPidName(v_uiPid));
        return;
    }

    len_name = strlen(v_szName);
    len_desc = strlen(v_szDesc);
    if ((len_name >= MAX_NAME_LEN) || (len_desc >= MAX_DESC_LEN)) {
        DBG_LogInfo(LVOS_TP_LOGID, "Length of v_szName(%s) or v_szDesc(%s) is too long, register fail Module: (%s).",
                    v_szName, v_szDesc, DBG_GetPidName(v_uiPid));
        return;
    }

    pstTp = LVOS_FindTracePoint(v_uiPid, v_szName);
    if (NULL != pstTp) {
        DBG_LogInfo(LVOS_TP_LOGID, "Repeat register tracepoint Module: (%s), TracePoint: (%s).",
                    DBG_GetPidName(v_uiPid), v_szName);
        return;
    }

    pstTp = LVOS_GetFreeTracePoint();
    if (NULL == pstTp) {
        DBG_LogInfo(LVOS_TP_LOGID, "Reach max tracepoint, register fail Module: (%s), TracePoint: (%s).",
                    DBG_GetPidName(v_uiPid), v_szName);
        return;
    }

    strcpy(pstTp->szName, v_szName);
    strcpy(pstTp->szDesc, v_szDesc);
    pstTp->uiPid = v_uiPid;
    pstTp->iId++;
    memset(&pstTp->stParam, 0, sizeof(pstTp->stParam));
    memset(pstTp->stHooks, 0, sizeof(pstTp->stHooks));
    pstTp->iDbgOnly = v_iDbgOnly;
    if (!v_iInitState) {
        pstTp->iActive = LVOS_TRACEP_STAT_DEACTIVE;
    } else {
        pstTp->iActive = LVOS_TRACEP_STAT_ACTIVE;
    }
}

EXPORT_SYMBOL(LVOS_RegTracePoint);

/*****************************************************************************
* 功    能: 注销TracePoint接口
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_UnRegTracePoint(OSP_U32 v_uiPid, const OSP_CHAR *v_szName)
{
    LVOS_TRACEP_S *pstTp = NULL;

    pstTp = LVOS_FindTracePoint(v_uiPid, v_szName);
    if (NULL == pstTp) {
        return;
    }

    pstTp->iActive = LVOS_TRACEP_STAT_DELETED;
    pstTp->iId++;
}

EXPORT_SYMBOL(LVOS_UnRegTracePoint);

/*****************************************************************************
* 功    能: 查找Hook
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
LVOS_TRACEP_HOOK_S *LVOS_FindTraceHook(LVOS_TRACEP_S *v_pstTp, const OSP_CHAR *v_szName)
{
    OSP_U32 uiIndex;

    if (NULL == v_pstTp) {
        return NULL;
    }

    for (uiIndex = 0; uiIndex < LVOS_MAX_HOOK_PER_TRACEP; uiIndex++) {
        if (LVOS_TRACEP_STAT_DELETED == v_pstTp->stHooks[uiIndex].iActive) {
            continue;
        }

        if (NULL == v_pstTp->stHooks[uiIndex].fnHook) {
            v_pstTp->stHooks[uiIndex].iActive = LVOS_TRACEP_STAT_DELETED;
            continue;
        }

        /* 只比较前面有效部分 */
        if (0 != strncmp(v_szName, v_pstTp->stHooks[uiIndex].szName, sizeof(v_pstTp->stHooks[uiIndex].szName) - 1)) {
            continue;
        }

        return v_pstTp->stHooks + uiIndex;
    }

    return NULL;
}

EXPORT_SYMBOL(LVOS_FindTraceHook);

/*****************************************************************************
* 功    能: 获取空闲Hook
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
LVOS_TRACEP_HOOK_S *LVOS_GetFreeTraceHook(LVOS_TRACEP_S *v_pstTp)
{
    OSP_U32 uiIndex;

    for (uiIndex = 0; uiIndex < LVOS_MAX_HOOK_PER_TRACEP; uiIndex++) {
        if (LVOS_TRACEP_STAT_DELETED == v_pstTp->stHooks[uiIndex].iActive) {
            return v_pstTp->stHooks + uiIndex;
        }
    }

    return NULL;
}

/*****************************************************************************
* 功    能: 添加Hook
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_AddTracePointHook(OSP_U32 v_uiPid,
                            const OSP_CHAR *v_szName,
                            const OSP_CHAR *v_szHookName,
                            FN_TRACEP_COMMON_T fnHook,
                            const OSP_CHAR *v_szDesc,
                            OSP_S32 v_iInitState,
                            OSP_S32 v_iDbgOnly)
{
    LVOS_TRACEP_S *pstTp = NULL;
    LVOS_TRACEP_HOOK_S *pstHook = NULL;
    size_t len_name;
    size_t len_desc;
    int32_t ret;

    if ((NULL == v_szName) || (NULL == v_szHookName) || (NULL == fnHook) || (NULL == v_szDesc)) {
        DBG_LogInfo(LVOS_TP_LOGID, "some argument is NULL, v_szName(%p), v_szHookName(%p), fnHook(%p), v_szDesc(%p).",
                    v_szName, v_szHookName, fnHook, v_szDesc);
        return;
    }

    len_name = strlen(v_szHookName);
    len_desc = strlen(v_szDesc);
    if ((len_name >= MAX_NAME_LEN) || (len_desc >= MAX_DESC_LEN)) {
        DBG_LogInfo(LVOS_TP_LOGID,
                    "the length of v_szHookName or v_szDesc is too long, v_szHookName(%u), v_szDesc(%u).", len_name,
                    len_desc);
        return;
    }

    pstTp = LVOS_FindTracePoint(v_uiPid, v_szName);
    if (NULL == pstTp) {
        DBG_LogInfo(LVOS_TP_LOGID, "Can not find tracepoint Module: (%s), TracePoint: (%s).", DBG_GetPidName(v_uiPid),
                    v_szName);
        return;
    }

    pstHook = LVOS_FindTraceHook(pstTp, v_szHookName);
    if (NULL != pstHook) {
        DBG_LogInfo(LVOS_TP_LOGID, "Repeat register tracepoint hook Module: (%s), TracePoint: (%s), Hook: (%s).",
                    DBG_GetPidName(v_uiPid), v_szName, v_szHookName);
        return;
    }

    pstHook = LVOS_GetFreeTraceHook(pstTp);
    if (NULL == pstHook) {
        DBG_LogInfo(LVOS_TP_LOGID, "Reach max tracepoint, register fail Module: (%s), TracePoint: (%s), Hook: (%s).",
                    DBG_GetPidName(v_uiPid), v_szName, v_szHookName);
        return;
    }

    strcpy(pstHook->szName, v_szHookName);
    strcpy(pstHook->szDesc, v_szDesc);
    pstHook->fnHook = fnHook;
    pstHook->iDbgOnly = v_iDbgOnly;
    pstHook->iId++;
    if (!v_iInitState) {
        pstHook->iActive = LVOS_TRACEP_STAT_DEACTIVE;
    } else {
        pstHook->iActive = LVOS_TRACEP_STAT_ACTIVE;
    }
}

EXPORT_SYMBOL(LVOS_AddTracePointHook);

/*****************************************************************************
* 功    能: 删除Hook
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_DelTracePointHook(OSP_U32 v_uiPid, const OSP_CHAR *v_szName, const OSP_CHAR *v_szHookName)
{
    LVOS_TRACEP_S *pstTp = NULL;
    LVOS_TRACEP_HOOK_S *pstHook = NULL;

    if ((NULL == v_szName) || (NULL == v_szHookName)) {
        DBG_LogInfo(LVOS_TP_LOGID, "some argument is NULL, v_szName(%p), v_szHookName(%p).", v_szName, v_szHookName);
        return;
    }

    pstTp = LVOS_FindTracePoint(v_uiPid, v_szName);
    if (NULL == pstTp) {
        DBG_LogInfo(LVOS_TP_LOGID, "Can not find tracepoint Module: (%s), TracePoint: (%s).", DBG_GetPidName(v_uiPid),
                    v_szName);
        return;
    }

    pstHook = LVOS_FindTraceHook(pstTp, v_szHookName);
    if (NULL == pstHook) {
        DBG_LogInfo(LVOS_TP_LOGID, "Can not find tracepoint hook Module: (%s), TracePoint: (%s), Hook: (%s).",
                    DBG_GetPidName(v_uiPid), v_szName, v_szHookName);
        return;
    }

    pstHook->iId++;
    pstHook->iActive = LVOS_TRACEP_STAT_DELETED;
}

EXPORT_SYMBOL(LVOS_DelTracePointHook);

/*****************************************************************************
* 功    能: 激活TracePoint
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_ActiveTracePoint(OSP_U32 v_uiPid, const OSP_CHAR *v_szName, OSP_S32 v_iFlag)
{
    LVOS_TRACEP_S *pstTp = NULL;

    if (NULL == v_szName) {
        DBG_LogInfo(LVOS_TP_LOGID, "v_szName is NULL.");
        return;
    }

    pstTp = LVOS_FindTracePoint(v_uiPid, v_szName);
    if (NULL == pstTp) {
        DBG_LogInfo(LVOS_TP_LOGID, "Can not find tracepoint Module: (%s), TracePoint: (%s).", DBG_GetPidName(v_uiPid),
                    v_szName);
        return;
    }

    if (!v_iFlag) {
        pstTp->iActive = LVOS_TRACEP_STAT_DEACTIVE;
    } else {
        pstTp->iActive = LVOS_TRACEP_STAT_ACTIVE;
    }
}

EXPORT_SYMBOL(LVOS_ActiveTracePoint);

/*****************************************************************************
* 功    能: 激活Hook
* 输入参数: 无
* 输出参数: 无
* 返回值  : 无
* 其 他   : 无
*****************************************************************************/
void LVOS_ActiveTracePointHook(OSP_U32 v_uiPid, const OSP_CHAR *v_szName, const OSP_CHAR *v_szHookName, OSP_S32 v_iFlag)
{
    LVOS_TRACEP_S *pstTp = NULL;
    LVOS_TRACEP_HOOK_S *pstHook = NULL;

    if ((NULL == v_szName) || (NULL == v_szHookName)) {
        DBG_LogInfo(LVOS_TP_LOGID, "some argument is NULL, v_szName(%p), v_szHookName(%p).", v_szName, v_szHookName);
        return;
    }

    pstTp = LVOS_FindTracePoint(v_uiPid, v_szName);
    if (NULL == pstTp) {
        DBG_LogInfo(LVOS_TP_LOGID, "Can not Find Tracepoint Module: (%s), TracePoint: (%s).", DBG_GetPidName(v_uiPid),
                    v_szName);
        return;
    }

    pstHook = LVOS_FindTraceHook(pstTp, v_szHookName);
    if (NULL == pstHook) {
        DBG_LogInfo(LVOS_TP_LOGID, "Can not Find Tracepoint Hook Module: (%s), TracePoint: (%s), HookName: (%s).",
                    DBG_GetPidName(v_uiPid), v_szName, v_szHookName);
        return;
    }

    if (!v_iFlag) {
        pstHook->iActive = LVOS_TRACEP_STAT_DEACTIVE;
    } else {
        pstHook->iActive = LVOS_TRACEP_STAT_ACTIVE;
    }
}

EXPORT_SYMBOL(LVOS_ActiveTracePointHook);

s32 LVOS_TpStartStub(const char *name, LVOS_TRACEP_NEW_S **pstTp, OSP_S32 *tracepointNotRegistered)
{
    OSP_S32 _ret;
    if (*pstTp == NULL) {
        _ret = LVOS_HVS_getTracePoint(PID_OSP_NULL, name, pstTp);
        if (_ret != RETURN_OK) {
            *pstTp = NULL;
        }
        if (*pstTp == NULL) {
            if (*tracepointNotRegistered == 0) {
                *tracepointNotRegistered = TRACEPOINT_NOT_REGISTERED;
                DBG_LogWarning(DBG_LOGID_BUTT, "tracepoint (%s) not registered", name);
            }
        }
    }

    if (*pstTp == NULL || (*pstTp)->iActive != LVOS_TRACEP_STAT_ACTIVE)
        return DP_FAIL;

    _ret = __sync_sub_and_fetch((int32_t *)&((*pstTp)->timeAlive), 1);
    if (_ret < 0) {
        return DP_FAIL;
    }

    return _ret;
}

int32_t LVOS_TpShowTpAll(LVOS_HT_S *v_pstHashTable, void (*showTp)(LVOS_HASH_ITEM_S *, void *, u32), void *param)
{
    uint32_t i;
    uint32_t showTpCount;
    struct list_head *pstPos = NULL;
    LVOS_HASH_ITEM_S *pstData = NULL;
    LVOS_HASH_LIST_S *pstElem = NULL;

    for (i = 0; i < v_pstHashTable->uiTableSize; ++i) {
        pstElem = &(v_pstHashTable->pstHashElem[i]);

        showTpCount = 0;
        pthread_spin_lock(&g_TPHtLock);
        dpax_list_for_each(pstPos, &pstElem->stBucketList)
        {
            pstData = dpax_list_entry(pstPos, LVOS_HASH_ITEM_S, stConflictList);
            showTp(pstData, param, ++showTpCount);
        }
        pthread_spin_unlock(&g_TPHtLock);

        CLI_Print(NULL);
    }

    return RETURN_OK;
}


