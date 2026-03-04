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

#ifndef BDM_COMMON_H
#define BDM_COMMON_H

#include <string.h>
#include <pthread.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RETURN_OK
#define RETURN_OK (0)
#endif

#ifndef RETURN_ERROR
#define RETURN_ERROR (-1)
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef INVALID_VALUE64
#define INVALID_VALUE64 0xFFFFFFFFFFFFFFFFULL
#endif

#ifndef INVALID_VALUE32
#define INVALID_VALUE32 0xFFFFFFFF
#endif

#ifndef INVALID_VALUE16
#define INVALID_VALUE16 0xFFFF
#endif

#ifndef INVALID_VALUE8
#define INVALID_VALUE8 0xFF
#endif

#ifndef ARRAY_LEN
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef ROUND_UP
#define ROUND_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
#endif

#ifndef ROUND_DOWN
#define ROUND_DOWN(x, y) (((x) / (y)) * (y))
#endif

#ifndef DESC
#define DESC(x) 1 /* 文件分段描述宏 */
#endif

#ifndef UNREFERENCE_PARAM
#define UNREFERENCE_PARAM(x) ((void)(x))
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - (char *)(&((type *)0)->member)))
#endif

#ifndef OFFSETOF
#define OFFSETOF(typ, memb) ((long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef ATOMIC_INC
#define ATOMIC_INC(x) __sync_add_and_fetch((x), 1)
#endif

#ifndef ATOMIC_DEC
#define ATOMIC_DEC(x) __sync_sub_and_fetch((x), 1)
#endif

#ifndef ATOMIC_ADD
#define ATOMIC_ADD(x, y) __sync_add_and_fetch((x), (y))
#endif

#ifndef ATOMIC_SUB
#define ATOMIC_SUB(x, y) __sync_sub_and_fetch((x), (y))
#endif

#ifndef ATOMIC_CAS
#define ATOMIC_CAS(x, y, z) __sync_val_compare_and_swap((x), (y), (z))
#endif

#define BDM_SPINLOCK_T pthread_spinlock_t
#define BDM_SPIN_INIT pthread_spin_init
#define BDM_SPIN_LOCK pthread_spin_lock
#define BDM_SPIN_UNLOCK pthread_spin_unlock
#define BDM_SPIN_DESTROY  pthread_spin_destroy

#define BDM_RWLOCK_T pthread_rwlock_t
#define BDM_RWLOCK_INIT pthread_rwlock_init
#define BDM_RWLOCK_RDLOCK pthread_rwlock_rdlock
#define BDM_RWLOCK_WRLOCK pthread_rwlock_wrlock
#define BDM_RWLOCK_UNLOCK pthread_rwlock_unlock
#define BDM_RWLOCK_DESTROY pthread_rwlock_destroy

#define BDM_LOG_BUF_LEN 512UL

typedef enum {
    BDM_LOG_TRACE = 0,
    BDM_LOG_DEBUG,
    BDM_LOG_INFO,
    BDM_LOG_WARNING,
    BDM_LOG_ERROR,
    BDM_LOG_CRITICAL,
    BDM_LOG_BUTT
} BdmLogLevel;

#ifndef BDM_LOG_FILENAME
#define BDM_LOG_FILENAME (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1) : __FILE__)
#endif


void BdmLogFunc(int logLevel, const char *funcName, int line, const char *fileName, const char *format, ...);

#define BDM_LOGINFO(LogID, ...)                                                          \
    do {                                                                                 \
        BdmLogFunc(BDM_LOG_INFO, __FUNCTION__, __LINE__, BDM_LOG_FILENAME, __VA_ARGS__); \
    } while (0)

#define BDM_LOGDEBUG(LogID, ...)                                                          \
    do {                                                                                  \
        BdmLogFunc(BDM_LOG_DEBUG, __FUNCTION__, __LINE__, BDM_LOG_FILENAME, __VA_ARGS__); \
    } while (0)

#define BDM_LOGWARN(LogID, ...)                                                             \
    do {                                                                                    \
        BdmLogFunc(BDM_LOG_WARNING, __FUNCTION__, __LINE__, BDM_LOG_FILENAME, __VA_ARGS__); \
    } while (0)

#define BDM_LOGERROR(LogID, ...)                                                          \
    do {                                                                                  \
        BdmLogFunc(BDM_LOG_ERROR, __FUNCTION__, __LINE__, BDM_LOG_FILENAME, __VA_ARGS__); \
    } while (0)

#ifdef __cplusplus
}
#endif
#endif
