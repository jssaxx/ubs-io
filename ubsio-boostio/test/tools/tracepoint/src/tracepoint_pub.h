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

#ifndef BIO_TRACEPOINT_PUB_H
#define BIO_TRACEPOINT_PUB_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define BIO_MAX_TRACEP_NUM   1024

#define BIO_STR_TO_KEY_BASE_NUM   31

#define BIO_MAX_TP_HASH_SHIFT 11

#define BIO_MAX_TP_HASH_SIZE (1 << BIO_MAX_TP_HASH_SHIFT)

#define BIO_TP_LOGID    (0)

#define PID_DEBUG 100
#define PID_OSP_NULL 0
#define BIO_GOLDEN_RATIO_PRIME 0x9e37fffffffc0001ULL
#define BIO_MAX_HT_BITS     64

#define BIO_TP_RETURN_CALLBACK_NULL         2

#define BIO_TP_RETURN_NOT_FOUND                3

#if defined(__KERNEL__) || defined(__KAPI__)
#define BIO_TP_CONFIG_FILE         "/OSM/conf/tracepoint.ini"
#define BIO_TP_CONFIG_TMP_FILE     "/OSM/conf/tracepoint_tmp.ini"
#else
#define BIO_TP_CONFIG_FILE         "./conf/tracepoint.ini"
#define BIO_TP_CONFIG_TMP_FILE     "./conf/tracepoint_tmp.ini"
#endif /*__KERNEL__*/

#define BIO_TP_MAX_CFG_LINE   256

#define BIO_TP_MAX_INIT_ACTIVE_NUM           512

typedef enum TagBioTpInitActiveE
{
	BIO_TP_INIT_ACTIVE_NAME = 0,
	BIO_TP_INIT_ACTIVE_TYPE = 1,
	BIO_TP_INIT_ACTIVE_ALIVE = 2,
	BIO_TP_INIT_ACTIVE_PARAM = 3,
	BIO_TP_INIT_ACTIVE_BUTT = 4
} BioTpInitActiveE;

typedef struct tagBIO_TP_INIT_ACTIVE_S
{
    int32_t type;
    uint32_t timeAlive;
    uint64_t name;
    char param[BIO_TRACEP_PARAM_SIZE];
} BioTpInitActive;

typedef struct TagBioTpHashS
{
    struct ListHead stConflictList;
    uint64_t ullKey2;
    uint32_t uiKey1;
    BioTracepointNews stTP;
} BioTpHashS;

typedef struct TagBioHashItemS
{
    struct ListHead stConflictList;
    uint64_t ullKey2;
    uint32_t uiKey1;
} BioHashItemS;

typedef struct TagBioHashListS
{
    struct ListHead  stBucketList;
} BioHashListS;

typedef struct TagBioHashTableS
{
    BioHashListS  *pstHashElem;
    uint32_t uiTableSize;
    uint32_t uiBits;
} BioHtS;

typedef struct TagKeyS
{
    union
    {
        uint64_t ull64bits;
        uint32_t ui32bits[2];
    }unKey64;
} KeyS;

void BioWalkTracePoint(TpInt32 (*pfn)(BioTracepoints *, void *), void *pParam);

void BioWalkTraceHook(BioTracepoints *pstTp, TpInt32 (*pfn)(BioTracepointHook *, void *), void *pParam);

int32_t BioHvsCreateHashTable(uint32_t uiHashSize, uint32_t uiBits, BioHtS **ppstHashTable);

void BioHvsDestroyHashTable(BioHtS **ppstHashTable);

int32_t insertHashItem(BioHtS *v_pstHashTable, BioHashItemS *v_pstData);

int32_t removeHashItem(BioHtS *v_pstHashTable, uint32_t v_uiKey1, uint64_t v_ullKey2);

int32_t searchHashItem(BioHtS *v_pstHashTable, uint32_t v_uiKey1, uint64_t v_ullKey2, BioHashItemS **v_ppstData);

int32_t BioHvsTravelHashTable(BioHtS *pstHashTable, void (*fn)(BioHashItemS *, void *), void *param);

void BioHvsParseConfigFile(const char *fileName);

void BioHvsDeactiveForTravel(BioHashItemS *hashdata, void *param);

void BioHvsInitTracePoint(void);

void BioHvsExitTracePoint(void);

int32_t BioTpShowTpAll(BioHtS *pstHashTable, void (*showTp)(BioHashItemS *, void *, TpUint32), void *param);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif // BIO_TRACEPOINT_PUB_H