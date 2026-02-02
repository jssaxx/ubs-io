/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <malloc.h>
#include <stdbool.h>
#include <unistd.h>

#include "dpax_list.h"
#include "tracepoint_common.h"
#include "tracepoint.h"
#include "tracepoint_pub.h"

MODULE_ID(PID_DEBUG);

#ifdef BIO_TP_LOGID
#undef BIO_TP_LOGID
#endif

#ifndef NSEC_PER_MS
#define NSEC_PER_MS 1000000UL
#endif

#ifndef MSEC_PER_SEC
#define MSEC_PER_SEC    1000UL
#endif


#define TRACEPOINT_PRINT_LIMIT_INTERVAL (60*HZ)
#define TRACEPOINT_PRINT_LIMIT_COUNT (3)

#define BIO_TP_LOGID (0L)

static BioTracepoints g_stTp[BIO_MAX_TRACEP_NUM];
static BioTpInitActive g_initActiveTPs[BIO_TP_MAX_INIT_ACTIVE_NUM];
static int32_t g_initActiveTPNum = 0;
static int g_init = 0;
static pthread_mutex_t g_initLock = PTHREAD_MUTEX_INITIALIZER;
BioHtS *g_TPHt = NULL;
static pthread_spinlock_t g_TPHtLock;

void TracePointInit()
{
    pthread_spin_init(&g_TPHtLock, 0);
    pthread_mutex_lock(&g_initLock);
    if (g_init != 0) {
        pthread_mutex_unlock(&g_initLock);
        return;
    }

    BioHvsInitTracePoint();
    g_init = 1;
    pthread_mutex_unlock(&g_initLock);
}

void TracePointExit()
{
    pthread_spin_destroy(&g_TPHtLock);
}

static uint32_t BioHvsGetHashAddr(uint32_t key1, uint64_t key2, uint32_t bits)
{
    KeyS stKey;

    stKey.unKey64.ull64bits = key2;
    stKey.unKey64.ui32bits[1] = key1;
    uint64_t ullTmp = *((uint64_t *) (&stKey));
    ullTmp *= BIO_GOLDEN_RATIO_PRIME;

    return (uint32_t) (ullTmp >> (BIO_MAX_HT_BITS - bits));
}

static int32_t BioHvsStrToHashKey(const char *str, uint32_t maxLen, uint64_t *key)
{
    uint32_t i = 0;
    uint32_t len = 0;
    uint64_t keyTemp = 0ULL;

    if (UNLIKELY(NULL == str || NULL == key)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    len = (uint32_t) strnlen(str, (size_t) maxLen);
    if (UNLIKELY(0 == len || len >= maxLen)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Length of str is zero or beyond the max length.");
        return RETURN_ERROR;
    }

    for (i = 0; i < len; ++i) {
        keyTemp = BIO_STR_TO_KEY_BASE_NUM * keyTemp + (uint8_t) str[i];
    }

    *key = keyTemp;

    return RETURN_OK;
}

int32_t BioHvsCreateHashTable(uint32_t uiHashSize, uint32_t uiBits, BioHtS **ppstHashTable)
{
    BioHtS *pstHash = NULL;
    uint32_t uiCount = 0;

    if ((NULL == ppstHashTable) || (0 == uiHashSize) || (uiBits > BIO_MAX_HT_BITS)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is invalid.");
        return RETURN_ERROR;
    }

    pstHash = (BioHtS *) malloc((uint32_t) sizeof(BioHtS));
    if (NULL == pstHash) {
        BIO_LOG_INFO(BIO_TP_LOGID, "malloc BioHtS fail.");
        *ppstHashTable = NULL;
        return RETURN_ERROR;
    }

    pstHash->pstHashElem = (BioHashListS *) malloc((uint32_t) (uiHashSize * sizeof(BioHashListS)));
    if (NULL == pstHash->pstHashElem) {
        BIO_LOG_INFO(BIO_TP_LOGID, "malloc hashtable array fail");
        free(pstHash);
        *ppstHashTable = NULL;
        return RETURN_ERROR;
    }

    pstHash->uiTableSize = uiHashSize;
    pstHash->uiBits = uiBits;
    for (uiCount = 0; uiCount < uiHashSize; uiCount++) {
        INIT_LIST_HEAD(&(pstHash->pstHashElem[uiCount].stBucketList));
    }

    *ppstHashTable = pstHash;
    return RETURN_OK;
}

void BioHvsDestroyHashTable(BioHtS **ppstHashTable)
{
    if (NULL == ppstHashTable || NULL == *ppstHashTable) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return;
    }

    free((*ppstHashTable)->pstHashElem);

    free(*ppstHashTable);
}

int32_t BioHvsInsertHashItem(BioHtS *pstHashTable, BioHashItemS *tPstData)
{
    uint32_t uiAddr = 0;
    BioHashListS *pstElem = NULL;
    BioHashItemS *pstData = NULL;
    struct ListHead *pstPos = NULL;
    bool validParam = false;
    bool multipleRegistration = false;

    if ((NULL == pstHashTable) || (NULL == tPstData)) {
        PRINT_LIMIT(BIO_LOG_INFO, (long) BIO_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL, TRACEPOINT_PRINT_LIMIT_COUNT,
                    validParam);
        if (true == (int) validParam) {
            BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        }
        return RETURN_ERROR;
    }

    uiAddr = BioHvsGetHashAddr(tPstData->uiKey1, tPstData->ullKey2, pstHashTable->uiBits);
    pstElem = &(pstHashTable->pstHashElem[uiAddr]);

    pthread_spin_lock(&g_TPHtLock);
    DPAX_LIST_FOR_EACH(pstPos, &pstElem->stBucketList) {
        /* lint -e718 -e746 -e40 */
        pstData = DPAX_LIST_ENTRY(pstPos, BioHashItemS, stConflictList);
        /* lint +e718 +e746 +e40 */
        if ((pstData->uiKey1 == tPstData->uiKey1) && (pstData->ullKey2 == tPstData->ullKey2)) {
            pthread_spin_unlock(&g_TPHtLock);
            PRINT_LIMIT(BIO_LOG_INFO, (long) BIO_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL,
                        TRACEPOINT_PRINT_LIMIT_COUNT, multipleRegistration);
            if (true == (int) multipleRegistration) {
                BIO_LOG_INFO(BIO_TP_LOGID, "Hash item already exists.");
            }
            return RETURN_ERROR;
        }
    }
    DpaxListAdd(&tPstData->stConflictList, &pstElem->stBucketList);
    pthread_spin_unlock(&g_TPHtLock);
    return RETURN_OK;
}

int32_t BioHvsRemoveHashItem(BioHtS *pstHashTable, uint32_t key1, uint64_t key2)
{
    BioHashListS *pstElem = NULL;
    BioHashItemS *pstData = NULL;
    struct ListHead *pstPos = NULL;
    uint32_t uiAddr = 0;

    if (NULL == pstHashTable) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    uiAddr = BioHvsGetHashAddr(key1, key2, pstHashTable->uiBits);
    pstElem = &(pstHashTable->pstHashElem[uiAddr]);

    pthread_spin_lock(&g_TPHtLock);
    DPAX_LIST_FOR_EACH(pstPos, &pstElem->stBucketList) {
        pstData = DPAX_LIST_ENTRY(pstPos, BioHashItemS, stConflictList);
        if ((key1 == pstData->uiKey1) && (key2 == pstData->ullKey2)) {
            DpaxListDelInit(&pstData->stConflictList);
            pthread_spin_unlock(&g_TPHtLock);
            return RETURN_OK;
        }
    }
    pthread_spin_unlock(&g_TPHtLock);
    return RETURN_ERROR;
}

int32_t BioHvsSearchHashItem(BioHtS *pstHashTable, uint32_t key1, uint64_t key2, BioHashItemS **ppstData)
{
    BioHashListS *pstElem = NULL;
    BioHashItemS *pstData = NULL;
    struct ListHead *pstPos = NULL;
    uint32_t uiAddr = 0;

    if ((NULL == pstHashTable) || (NULL == ppstData)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    uiAddr = BioHvsGetHashAddr(key1, key2, pstHashTable->uiBits);
    pstElem = &(pstHashTable->pstHashElem[uiAddr]);

    pthread_spin_lock(&g_TPHtLock);
    DPAX_LIST_FOR_EACH(pstPos, &pstElem->stBucketList) {
        pstData = DPAX_LIST_ENTRY(pstPos, BioHashItemS, stConflictList);
        if ((key1 == pstData->uiKey1) && (key2 == pstData->ullKey2)) {
            *ppstData = pstData;
            pthread_spin_unlock(&g_TPHtLock);
            return RETURN_OK;
        }
    }

    pthread_spin_unlock(&g_TPHtLock);
    *ppstData = NULL;

    return BIO_TP_RETURN_NOT_FOUND;
}

int32_t BioHvsTravelHashTable(BioHtS *pstHashTable, void (*fn)(BioHashItemS *, void *), void *param)
{
    uint32_t i;
    struct ListHead *pstPos = NULL;
    BioHashItemS *pstData = NULL;
    BioHashListS *pstElem = NULL;

    if ((NULL == pstHashTable) || (NULL == fn)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    for (i = 0; i < pstHashTable->uiTableSize; ++i) {
        pstElem = &(pstHashTable->pstHashElem[i]);
        pthread_spin_lock(&g_TPHtLock);
        DPAX_LIST_FOR_EACH(pstPos, &pstElem->stBucketList) {
            /*lint -e40*/
            pstData = DPAX_LIST_ENTRY(pstPos, BioHashItemS, stConflictList);
            /*lint +e40*/
            fn(pstData, param);
        }
        pthread_spin_unlock(&g_TPHtLock);
    }

    return RETURN_OK;
}

void BioHvsDeactiveForTravel(BioHashItemS *hashdata, void *param)
{
    BioTpHashS *hashTPData = NULL;

    if (NULL != param) {
        BIO_LOG_WARNING(BIO_TP_LOGID, "param should be NULL.");
    }

    if (NULL == hashdata) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return;
    }

    hashTPData = (BioTpHashS *) (void *) hashdata;
    hashTPData->stTP.iActive = BIO_TRACEP_STAT_DEACTIVE;
    hashTPData->stTP.type = BIO_TP_TYPE_BUTT;
    hashTPData->stTP.timeAlive = 0;
    hashTPData->stTP.timeCalled = 0;
    memset(&(hashTPData->stTP.stParam.tpParamData[0]), 0, (size_t) BIO_TRACEP_PARAM_SIZE);
}

static BioTpType BioGetTpType(const char *type)
{
    BioTpType tp_type = BIO_TP_TYPE_BUTT;
    if (!type) {
        return tp_type;
    }
    if (0 == strcmp(type, "callback")) {
        tp_type = BIO_TP_TYPE_CALLBACK;
    } else if (0 == strcmp(type, "reset")) {
        tp_type = BIO_TP_TYPE_RESET;
    } else if (0 == strcmp(type, "pause")) {
        tp_type = BIO_TP_TYPE_PAUSE;
    } else if (0 == strcmp(type, "abort")) {
        tp_type = BIO_TP_TYPE_ABORT;
    } else {
        BIO_LOG_WARNING(BIO_TP_LOGID, "Type of config file is incorrect.");
    }
    return tp_type;
}

int32_t BioHvsParseConfigLine(char *tpInfo, int32_t index)
{
    BioTpInitActiveE step = BIO_TP_INIT_ACTIVE_NAME;
    int32_t pos = 0;
    uint64_t interval = 0ULL;
    uint64_t key = 0ULL;
    char *start = NULL;

    if (NULL == tpInfo) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    start = tpInfo;
    while (step < BIO_TP_INIT_ACTIVE_BUTT) {
        if (RETURN_OK != BIO_GET_CHAR_POS(start, ' ', &pos)) {
            BIO_LOG_WARNING(BIO_TP_LOGID, "Format of config file is incorrect.");
            return RETURN_ERROR;
        }
        start[pos] = '\0';
        if (BIO_TP_INIT_ACTIVE_NAME == step) {
            if (RETURN_OK != BioHvsStrToHashKey(start, MAX_NAME_LEN, &key)) {
                BIO_LOG_WARNING(BIO_TP_LOGID, "Cannot make the key from %s.", start);
                return RETURN_ERROR;
            }
            g_initActiveTPs[index].name = key;
        } else if (BIO_TP_INIT_ACTIVE_TYPE == step) { /* Type */
            BioTpType type = BioGetTpType(start);
            if (type == BIO_TP_TYPE_BUTT) {
                return RETURN_ERROR;
            }
            g_initActiveTPs[index].type = type;
        } else if (BIO_TP_INIT_ACTIVE_ALIVE == step) { /* num of alive */
            g_initActiveTPs[index].timeAlive = (uint32_t)atoll(start);

            start += pos + 1;

            BIO_FILTER_NEWLINE_SYMBOL(start);

            strcpy(g_initActiveTPs[index].param, start);
            g_initActiveTPs[index].param[BIO_TRACEP_PARAM_SIZE - 1] = '\0';

            break;
        }

        start += pos + 1;
        step++;
    }

    if (BIO_TP_TYPE_PAUSE == g_initActiveTPs[index].type) {
        interval = atoll(g_initActiveTPs[index].param);
        if (interval == 0) {
            strcpy(g_initActiveTPs[index].param, "10000");
        }
    }

    return RETURN_OK;
}

void BioHvsParseConfigFile(const char *fileName)
{
    int32_t ret = RETURN_ERROR;
    int32_t fd = -1;
    char tpInfo[BIO_TP_MAX_CFG_LINE];

    ret = access(fileName, F_OK);
    if (RETURN_OK == ret) {
        fd = open(fileName, O_RDONLY, S_IRUSR);
        if (fd < 0) {
            BIO_LOG_WARNING(BIO_TP_LOGID, "Open file %s failed.", fileName);
            return;
        }

        memset(tpInfo, 0, sizeof(tpInfo));
        while (NULL != BIO_READ_LINE(fd, tpInfo, (TpInt32)sizeof(tpInfo))) {
            tpInfo[BIO_TP_MAX_CFG_LINE - 1] = '\0';
            if (RETURN_OK == BioHvsParseConfigLine(tpInfo, g_initActiveTPNum)) {
                g_initActiveTPNum++;
            }
            memset(tpInfo, 0, sizeof(tpInfo));
        }

        (void)close(fd);
    }

}

void BioHvsInitTracePoint(void)
{
    if (RETURN_OK != BioHvsCreateHashTable(BIO_MAX_TP_HASH_SIZE, BIO_MAX_TP_HASH_SHIFT, &g_TPHt)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Create Hash table failed.");
        g_TPHt = NULL;
    }
}

void BioHvsExitTracePoint(void)
{
    BioHvsDestroyHashTable(&g_TPHt);
    g_TPHt = NULL;
}

void BioHvsDoTracepointPause(BioTracepointNews *tracepoint)
{
    uint32_t interval;
    TpInt32 ret;
    struct timespec rqtp;
    struct timespec rmtp;

    if (NULL == tracepoint) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parmeter tracepoint is NULL.");
        return;
    } else {
        interval = (uint32_t)atoll(tracepoint->stParam.tpParamData);
    }

    BIO_LOG_INFO(BIO_TP_LOGID, "Tracepoint(%s) pause %d milliseconds.", tracepoint->szName, interval);

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
}

EXPORT_SYMBOL(BioHvsDoTracepointPause);

int32_t BioHvsGetTracePoint(uint32_t pid, const char *name, BioTracepointNews **tracepoint)
{
    int32_t ret = RETURN_ERROR;
    uint64_t key = 0ULL;
    BioHashItemS *hashData = NULL;
    BioTpHashS *hashTPData = NULL;
    bool bcan = false;

    if (NULL == name || NULL == tracepoint) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL, get tracepoint failed.");
        return RETURN_ERROR;
    }

    if (RETURN_OK != BioHvsStrToHashKey(name, MAX_NAME_LEN, &key)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Cannot make the key from tracepoint name.");
        *tracepoint = NULL;
        return RETURN_ERROR;
    }

    ret = BioHvsSearchHashItem(g_TPHt, pid, key, &hashData);
    if (RETURN_OK != ret) {
        PRINT_LIMIT(BIO_LOG_INFO, (long)BIO_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL,
            TRACEPOINT_PRINT_LIMIT_COUNT, bcan);
        if (true == (int)bcan) {
            BIO_LOG_INFO(BIO_TP_LOGID, "Search tracepoint(%s) failed.", name);
        }
        *tracepoint = NULL;
        return ret;
    }

    hashTPData = (BioTpHashS *)(void *)hashData;
    *tracepoint = &(hashTPData->stTP);

    return RETURN_OK;
}

EXPORT_SYMBOL(BioHvsGetTracePoint);

int32_t BioHvsRegTracePoint(uint32_t pid, const char *name, const char *desc, FuncTracepointCommon fnHook)
{
    uint64_t key = 0ULL;
    int32_t loop = 0;
    static bool isFirstReg = TRUE;
    int32_t activeStat = BIO_TRACEP_STAT_DEACTIVE;
    uint32_t timeAlive = 0;
    int32_t type = BIO_TP_TYPE_BUTT;
    BioTpHashS *hashData = NULL;
    bool bcan = false;

    if (NULL == name || NULL == desc) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL, register failed.");
        return RETURN_ERROR;
    }

    if (isFirstReg) {
        memset(g_initActiveTPs, 0, sizeof(g_initActiveTPs));
        BioHvsParseConfigFile(BIO_TP_CONFIG_FILE);
        BioHvsParseConfigFile(BIO_TP_CONFIG_TMP_FILE);
        isFirstReg = FALSE;
    }

    if (RETURN_OK != BioHvsStrToHashKey(name, MAX_NAME_LEN, &key)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Cannot make the key from tracepoint name.");
        return RETURN_ERROR;
    }

    hashData = (BioTpHashS *)malloc((uint32_t)sizeof(BioTpHashS));
    if (NULL == hashData) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Allocate hash data failed.");
        return RETURN_ERROR;
    }

    memset(hashData, 0, sizeof(BioTpHashS));

    for (loop = 0; loop <= g_initActiveTPNum; loop++) {
        if (key == g_initActiveTPs[loop].name) {
            if (BIO_TP_TYPE_CALLBACK == g_initActiveTPs[loop].type && NULL == fnHook) {
                BIO_LOG_WARNING(BIO_TP_LOGID, "Tracepoint %s init active is invalid.", name);
                break;
            }

            activeStat = BIO_TRACEP_STAT_ACTIVE;
            type = g_initActiveTPs[loop].type;
            timeAlive = g_initActiveTPs[loop].timeAlive;
            strcpy(&hashData->stTP.stParam.tpParamData[0], g_initActiveTPs[loop].param);
            hashData->stTP.stParam.tpParamData[BIO_TRACEP_PARAM_SIZE - 1] = '\0';

            BIO_LOG_INFO(BIO_TP_LOGID, "Tracepoint %s is initially actived.", name);
            break;
        }
    }

    hashData->uiKey1 = PID_OSP_NULL;
    hashData->ullKey2 = key;
    hashData->stTP.uiPid = pid;
    hashData->stTP.iActive = activeStat;
    hashData->stTP.type = type;
    hashData->stTP.timeAlive = timeAlive;
    hashData->stTP.timeCalled = 0;
    strcpy(&hashData->stTP.szName[0], name);
    hashData->stTP.szName[MAX_NAME_LEN - 1] = '\0';
    strcpy(&hashData->stTP.szDesc[0], desc);
    hashData->stTP.szDesc[MAX_DESC_LEN - 1] = '\0';
    hashData->stTP.fnHook = fnHook;

    if (RETURN_OK != BioHvsInsertHashItem(g_TPHt, (BioHashItemS *)(void *)hashData)) {
        PRINT_LIMIT(BIO_LOG_INFO, (long)BIO_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL,
            TRACEPOINT_PRINT_LIMIT_COUNT, bcan);
        if (true == (int)bcan) {
            BIO_LOG_INFO(BIO_TP_LOGID, "Insert tracepoint(%s) to hash table failed.", name);
        }
        free(hashData);
        return RETURN_ERROR;
    }

    return RETURN_OK;
}

EXPORT_SYMBOL(BioHvsRegTracePoint);

int32_t BioHvsUnregTracePoint(uint32_t pid, const char *name)
{
    uint32_t key1 = pid;
    uint64_t key2 = 0ULL;
    BioHashItemS *hashData = NULL;
    bool bcan = false;

    if (NULL == name) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    if (RETURN_OK != BioHvsStrToHashKey(name, MAX_NAME_LEN, &key2)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Cannot make the key from tracepoint name.");
        return RETURN_ERROR;
    }

    if (RETURN_OK != BioHvsSearchHashItem(g_TPHt, key1, key2, &hashData)) {
        PRINT_LIMIT(BIO_LOG_INFO, (long)BIO_LOGID_BUTT, TRACEPOINT_PRINT_LIMIT_INTERVAL,
            TRACEPOINT_PRINT_LIMIT_COUNT, bcan);
        if (true == (int)bcan) {
            BIO_LOG_INFO(BIO_TP_LOGID, "Search tracepoint(%s) failed.", name);
        }
        return RETURN_ERROR;
    }

    (void)BioHvsRemoveHashItem(g_TPHt, key1, key2);

    free(hashData);

    return RETURN_OK;
}
EXPORT_SYMBOL(BioHvsUnregTracePoint);

int32_t BioHvsActiveTracePoint(uint32_t pid, const char *name, int32_t type, uint32_t time,
    BioTracepointParam userParam)
{
    int32_t ret = RETURN_ERROR;
    BioTracepointNews *tracepoint = NULL;

    if (NULL == name) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    if (type > BIO_TP_TYPE_BUTT) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is not correct,type=%d.", type);
        return RETURN_ERROR;
    }

    ret = BioHvsGetTracePoint(pid, name, &tracepoint);
    if (NULL == tracepoint) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Get tracepoint(%s) failed.", name);
        return ret;
    }

    if (BIO_TP_TYPE_CALLBACK == type && NULL == tracepoint->fnHook) {
        BIO_LOG_INFO(BIO_TP_LOGID,
            "Callback function of tracepoint(%s) is NULL, cannot active callback type.", name);
        return BIO_TP_RETURN_CALLBACK_NULL;
    }

    tracepoint->iActive = BIO_TRACEP_STAT_ACTIVE;
    tracepoint->type = type;
    tracepoint->timeAlive = time;
    tracepoint->timeCalled = 0;

    strcpy(&tracepoint->stParam.tpParamData[0], &userParam.tpParamData[0]);
    tracepoint->stParam.tpParamData[BIO_TRACEP_PARAM_SIZE - 1] = '\0';

    BIO_LOG_INFO(BIO_TP_LOGID, "Tracepoint(%s) is active.", name);

    return RETURN_OK;
}

EXPORT_SYMBOL(BioHvsActiveTracePoint);

int32_t BioHvsDeactiveTracePoint(uint32_t pid, const char *name)
{
    int32_t ret = RETURN_ERROR;
    BioTracepointNews *tracepoint = NULL;

    if (NULL == name) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Parameter is NULL.");
        return RETURN_ERROR;
    }

    ret = BioHvsGetTracePoint(pid, name, &tracepoint);
    if (NULL == tracepoint) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Get tracepoint(%s) failed.", name);
        return ret;
    }

    tracepoint->iActive = BIO_TRACEP_STAT_DEACTIVE;
    tracepoint->type = BIO_TP_TYPE_BUTT;
    tracepoint->timeAlive = 0;
    tracepoint->timeCalled = 0;
    memset(&tracepoint->stParam.tpParamData[0], 0, (size_t)BIO_TRACEP_PARAM_SIZE);

    BIO_LOG_INFO(BIO_TP_LOGID, "Tracepoint(%s) is deactive.", name);

    return RETURN_OK;
}
EXPORT_SYMBOL(BioHvsDeactiveTracePoint);

int32_t BioHvsDeactiveTracePointAll(void)
{
    return BioHvsTravelHashTable(g_TPHt, BioHvsDeactiveForTravel, NULL);
}

void BioWalkTracePoint(TpInt32 (*pfn)(BioTracepoints *, void *), void *pParam)
{
    TpUint32 uiIndex;
    TpInt32 iRet;

    if ((NULL == pfn) || (NULL == pParam)) {
        return;
    }
    for (uiIndex = 0; uiIndex < BIO_MAX_TRACEP_NUM; uiIndex++) {
        if (BIO_TRACEP_STAT_DELETED == g_stTp[uiIndex].iActive) {
            continue;
        }

        iRet = pfn(&g_stTp[uiIndex], pParam);
        if (RETURN_OK != iRet) {
            return;
        }
    }
}

void BioWalkTraceHook(BioTracepoints *pstTp, TpInt32 (*pfn)(BioTracepointHook *, void *), void *pParam)
{
    TpUint32 uiIndex;
    TpInt32 iRet;

    if ((NULL == pstTp) || (NULL == pfn) || (NULL == pParam)) {
        return;
    }

    for (uiIndex = 0; uiIndex < BIO_MAX_HOOK_PER_TRACEP; uiIndex++) {
        if (BIO_TRACEP_STAT_DELETED == pstTp->stHooks[uiIndex].iActive) {
            continue;
        }

        iRet = pfn(&pstTp->stHooks[uiIndex], pParam);
        if (RETURN_OK != iRet) {
            return;
        }
    }
}

BioTracepoints *BioFindTracePoint(TpUint32 uiPid, const TpChar *szName)
{
    TpUint32 uiIndex;

    if (UNLIKELY(NULL == szName)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "szName is NULL.");
        return NULL;
    }

    for (uiIndex = 0; uiIndex < BIO_MAX_TRACEP_NUM; uiIndex++) {
        if (uiPid != g_stTp[uiIndex].uiPid) {
            continue;
        }

        if (BIO_TRACEP_STAT_DELETED == g_stTp[uiIndex].iActive) {
            continue;
        }

        if (0 != strncmp(szName, g_stTp[uiIndex].szName, sizeof(g_stTp[uiIndex].szName) - 1)) {
            continue;
        }

        return g_stTp + uiIndex;
    }

    return NULL;
}

EXPORT_SYMBOL(BioFindTracePoint);

BioTracepoints *BioGetFreeTracePoint(void)
{
    TpUint32 uiIndex;

    for (uiIndex = 0; uiIndex < BIO_MAX_TRACEP_NUM; uiIndex++) {
        if (BIO_TRACEP_STAT_DELETED == g_stTp[uiIndex].iActive) {
            return g_stTp + uiIndex;
        }
    }

    return NULL;
}

void BioRegTracePoint(TpUint32 uiPid, const TpChar *szName, const TpChar *szDesc, TpInt32 iInitState,
                        TpInt32 iDbgOnly)
{
    BioTracepoints *pstTp = NULL;
    size_t len_name;
    size_t len_desc;
    int32_t ret;

    if ((NULL == szName) || (NULL == szDesc)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "NULL pointer of szName or szDesc, register fail Module: (%s).",
                     BIO_GET_PID_NAME(uiPid));
        return;
    }

    len_name = strlen(szName);
    len_desc = strlen(szDesc);
    if ((len_name >= MAX_NAME_LEN) || (len_desc >= MAX_DESC_LEN)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Length of szName(%s) or szDesc(%s) is too long, register fail Module: (%s).",
                     szName, szDesc, BIO_GET_PID_NAME(uiPid));
        return;
    }

    pstTp = BioFindTracePoint(uiPid, szName);
    if (NULL != pstTp) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Repeat register tracepoint Module: (%s), TracePoint: (%s).",
                    BIO_GET_PID_NAME(uiPid), szName);
        return;
    }

    pstTp = BioGetFreeTracePoint();
    if (NULL == pstTp) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Reach max tracepoint, register fail Module: (%s), TracePoint: (%s).",
                    BIO_GET_PID_NAME(uiPid), szName);
        return;
    }

    strcpy(pstTp->szName, szName);
    strcpy(pstTp->szDesc, szDesc);
    pstTp->uiPid = uiPid;
    pstTp->iId++;
    memset(&pstTp->stParam, 0, sizeof(pstTp->stParam));
    memset(pstTp->stHooks, 0, sizeof(pstTp->stHooks));
    pstTp->iDbgOnly = iDbgOnly;
    if (!iInitState) {
        pstTp->iActive = BIO_TRACEP_STAT_DEACTIVE;
    } else {
        pstTp->iActive = BIO_TRACEP_STAT_ACTIVE;
    }
}

EXPORT_SYMBOL(BioRegTracePoint);

void BioUnRegTracePoint(TpUint32 uiPid, const TpChar *szName)
{
    BioTracepoints *pstTp = NULL;

    pstTp = BioFindTracePoint(uiPid, szName);
    if (NULL == pstTp) {
        return;
    }

    pstTp->iActive = BIO_TRACEP_STAT_DELETED;
    pstTp->iId++;
}

EXPORT_SYMBOL(BioUnRegTracePoint);

BioTracepointHook *BioFindTraceHook(BioTracepoints *pstTp, const TpChar *szName)
{
    TpUint32 uiIndex;

    if (NULL == pstTp) {
        return NULL;
    }

    for (uiIndex = 0; uiIndex < BIO_MAX_HOOK_PER_TRACEP; uiIndex++) {
        if (BIO_TRACEP_STAT_DELETED == pstTp->stHooks[uiIndex].iActive) {
            continue;
        }

        if (NULL == pstTp->stHooks[uiIndex].fnHook) {
            pstTp->stHooks[uiIndex].iActive = BIO_TRACEP_STAT_DELETED;
            continue;
        }

        if (0 != strncmp(szName, pstTp->stHooks[uiIndex].szName, sizeof(pstTp->stHooks[uiIndex].szName) - 1)) {
            continue;
        }

        return pstTp->stHooks + uiIndex;
    }

    return NULL;
}

EXPORT_SYMBOL(BioFindTraceHook);

BioTracepointHook *BioGetFreeTraceHook(BioTracepoints *pstTp)
{
    TpUint32 uiIndex;

    for (uiIndex = 0; uiIndex < BIO_MAX_HOOK_PER_TRACEP; uiIndex++) {
        if (BIO_TRACEP_STAT_DELETED == pstTp->stHooks[uiIndex].iActive) {
            return pstTp->stHooks + uiIndex;
        }
    }

    return NULL;
}

void BioAddTracePointHook(TpUint32 uiPid, const TpChar *szName, const TpChar *szHookName,
    FuncTracepointCommon fnHook, const TpChar *szDesc, TpInt32 iInitState, TpInt32 iDbgOnly)
{
    BioTracepoints *pstTp = NULL;
    BioTracepointHook *pstHook = NULL;
    size_t lenName;
    size_t lenDesc;

    if ((NULL == szName) || (NULL == szHookName) || (NULL == fnHook) || (NULL == szDesc)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "some argument is NULL, szName(%p), szHookName(%p), fnHook(%p), szDesc(%p).",
                     szName, szHookName, fnHook, szDesc);
        return;
    }

    lenName = strlen(szHookName);
    lenDesc = strlen(szDesc);
    if ((lenName >= MAX_NAME_LEN) || (lenDesc >= MAX_DESC_LEN)) {
        BIO_LOG_INFO(BIO_TP_LOGID,
                    "the length of szHookName or szDesc is too long, szHookName(%u), szDesc(%u).", lenName,
                    lenDesc);
        return;
    }

    pstTp = BioFindTracePoint(uiPid, szName);
    if (NULL == pstTp) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Can not find tracepoint Module: (%s), TracePoint: (%s).", BIO_GET_PID_NAME(uiPid),
                    szName);
        return;
    }

    pstHook = BioFindTraceHook(pstTp, szHookName);
    if (NULL != pstHook) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Repeat register tracepoint hook Module: (%s), TracePoint: (%s), Hook: (%s).",
                    BIO_GET_PID_NAME(uiPid), szName, szHookName);
        return;
    }

    pstHook = BioGetFreeTraceHook(pstTp);
    if (NULL == pstHook) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Reach max tracepoint, register fail Module: (%s), TracePoint: (%s), Hook: (%s).",
                    BIO_GET_PID_NAME(uiPid), szName, szHookName);
        return;
    }

    strcpy(pstHook->szName, szHookName);
    strcpy(pstHook->szDesc, szDesc);
    pstHook->fnHook = fnHook;
    pstHook->iDbgOnly = iDbgOnly;
    pstHook->iId++;
    if (!iInitState) {
        pstHook->iActive = BIO_TRACEP_STAT_DEACTIVE;
    } else {
        pstHook->iActive = BIO_TRACEP_STAT_ACTIVE;
    }
}

EXPORT_SYMBOL(BioAddTracePointHook);

void BioDelTracePointHook(TpUint32 uiPid, const TpChar *szName, const TpChar *szHookName)
{
    BioTracepoints *pstTp = NULL;
    BioTracepointHook *pstHook = NULL;

    if ((NULL == szName) || (NULL == szHookName)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "some argument is NULL, szName(%p), szHookName(%p).", szName, szHookName);
        return;
    }

    pstTp = BioFindTracePoint(uiPid, szName);
    if (NULL == pstTp) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Can not find tracepoint Module: (%s), TracePoint: (%s).", BIO_GET_PID_NAME(uiPid),
                     szName);
        return;
    }

    pstHook = BioFindTraceHook(pstTp, szHookName);
    if (NULL == pstHook) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Can not find tracepoint hook Module: (%s), TracePoint: (%s), Hook: (%s).",
                    BIO_GET_PID_NAME(uiPid), szName, szHookName);
        return;
    }

    pstHook->iId++;
    pstHook->iActive = BIO_TRACEP_STAT_DELETED;
}

EXPORT_SYMBOL(BioDelTracePointHook);

void BioActiveTracePoint(TpUint32 uiPid, const TpChar *szName, TpInt32 iFlag)
{
    BioTracepoints *pstTp = NULL;

    if (NULL == szName) {
        BIO_LOG_INFO(BIO_TP_LOGID, "szName is NULL.");
        return;
    }

    pstTp = BioFindTracePoint(uiPid, szName);
    if (NULL == pstTp) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Can not find tracepoint Module: (%s), TracePoint: (%s).", BIO_GET_PID_NAME(uiPid),
                     szName);
        return;
    }

    if (!iFlag) {
        pstTp->iActive = BIO_TRACEP_STAT_DEACTIVE;
    } else {
        pstTp->iActive = BIO_TRACEP_STAT_ACTIVE;
    }
}

EXPORT_SYMBOL(BioActiveTracePoint);

void BioActiveTracePointHook(TpUint32 uiPid, const TpChar *szName, const TpChar *szHookName, TpInt32 iFlag)
{
    BioTracepoints *pstTp = NULL;
    BioTracepointHook *pstHook = NULL;

    if ((NULL == szName) || (NULL == szHookName)) {
        BIO_LOG_INFO(BIO_TP_LOGID, "some argument is NULL, szName(%p), szHookName(%p).", szName, szHookName);
        return;
    }

    pstTp = BioFindTracePoint(uiPid, szName);
    if (NULL == pstTp) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Can not Find Tracepoint Module: (%s), TracePoint: (%s).", BIO_GET_PID_NAME(uiPid),
                     szName);
        return;
    }

    pstHook = BioFindTraceHook(pstTp, szHookName);
    if (NULL == pstHook) {
        BIO_LOG_INFO(BIO_TP_LOGID, "Can not Find Tracepoint Hook Module: (%s), TracePoint: (%s), HookName: (%s).",
                     BIO_GET_PID_NAME(uiPid), szName, szHookName);
        return;
    }

    if (!iFlag) {
        pstHook->iActive = BIO_TRACEP_STAT_DEACTIVE;
    } else {
        pstHook->iActive = BIO_TRACEP_STAT_ACTIVE;
    }
}

EXPORT_SYMBOL(BioActiveTracePointHook);

TpInt32 BioTpStartStub(const char *name, BioTracepointNews **pstTp, TpInt32 *tracepointNotRegistered)
{
    TpInt32 _ret;
    if (*pstTp == NULL) {
        _ret = BioHvsGetTracePoint(PID_OSP_NULL, name, pstTp);
        if (_ret != RETURN_OK) {
            *pstTp = NULL;
        }
        if (*pstTp == NULL) {
            if (*tracepointNotRegistered == 0) {
                *tracepointNotRegistered = BIO_TRACEPOINT_NOT_REGISTERED;
                BIO_LOG_WARNING(BIO_LOGID_BUTT, "tracepoint (%s) not registered", name);
            }
        }
    }

    if (*pstTp == NULL || (*pstTp)->iActive != BIO_TRACEP_STAT_ACTIVE)
        return DP_FAIL;

    _ret = __sync_sub_and_fetch((int32_t *)&((*pstTp)->timeAlive), 1);
    if (_ret < 0) {
        return DP_FAIL;
    }

    return _ret;
}

int32_t BioTpShowTpAll(BioHtS *pstHashTable, void (*showTp)(BioHashItemS *, void *, TpUint32), void *param)
{
    uint32_t i;
    uint32_t showTpCount;
    struct ListHead *pstPos = NULL;
    BioHashItemS *pstData = NULL;
    BioHashListS *pstElem = NULL;

    for (i = 0; i < pstHashTable->uiTableSize; ++i) {
        pstElem = &(pstHashTable->pstHashElem[i]);

        showTpCount = 0;
        pthread_spin_lock(&g_TPHtLock);
        DPAX_LIST_FOR_EACH(pstPos, &pstElem->stBucketList)
        {
            pstData = DPAX_LIST_ENTRY(pstPos, BioHashItemS, stConflictList);
            showTp(pstData, param, ++showTpCount);
        }
        pthread_spin_unlock(&g_TPHtLock);
    }

    return RETURN_OK;
}

