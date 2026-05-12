/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef CM_THREAD_H
#define CM_THREAD_H

#include <stdint.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include "cm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

void CmSleep(unsigned int seconds);

#define CM_SLEEP(var) CmSleep(var)
#define CM_SEM_S sem_t

#define CM_SEM_INIT(sem, i)                                                                              \
    do {                                                                                                 \
        int initRet = sem_init(sem, i, 0);                                                               \
        if (initRet != 0) {                                                                              \
            CM_LOGERROR(0, "Func(%s) Line(%d) init sem fail, ret(%d)", __FUNCTION__, __LINE__, initRet); \
        }                                                                                                \
    } while (0)

#define CM_SEM_DOWN(sem)                         \
    do {                                         \
        int downRet;                             \
        downRet = sem_wait(sem);                 \
        while (downRet != 0 && errno == EINTR) { \
            downRet = sem_wait(sem);             \
        }                                        \
    } while (0)

#define CM_SEM_UP(sem)                                                                                   \
    do {                                                                                                 \
        int upRet = sem_post(sem);                                                                       \
        if (upRet != 0) {                                                                                \
            CM_LOGERROR(0, "FuncName(%s)Line(%d) up lwtSem failed(%d).", __FUNCTION__, __LINE__, upRet); \
        }                                                                                                \
    } while (0)

#define CM_SEM_DESTROY(sem)                                                                                     \
    do {                                                                                                        \
        int destRet = sem_destroy(sem);                                                                         \
        if (destRet != 0) {                                                                                     \
            CM_LOGERROR(0, "FuncName(%s)Line(%d) destory lwtSem failed(%d).", __FUNCTION__, __LINE__, destRet); \
        }                                                                                                       \
    } while (0)

#define CM_SEM_YIELD()                                                                                     \
    do {                                                                                                   \
        int yieldRet = sched_yield();                                                                      \
        if (yieldRet != 0) {                                                                               \
            CM_LOGERROR(0, "Func(%s) Line(%d) yield sem fail, ret(%d)", __FUNCTION__, __LINE__, yieldRet); \
        }                                                                                                  \
    } while (0)

typedef void *(*THREAD_CALL_BACK)(void *arg);

int32_t CmSwitchThreadAsync(const char *name, void *data, THREAD_CALL_BACK handle);

#ifdef __cplusplus
};
#endif

#endif

