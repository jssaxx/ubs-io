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

#include <unistd.h>
#include "cm_log.h"
#include "securec.h"
#include "cm_threadpool.h"

void *ThreadPoolThread(void *threadPool)
{
    CM_THREAD_POOL_S *pool = (CM_THREAD_POOL_S *)threadPool;

    while (1) {
        pthread_mutex_lock(&(pool->mutexQueue));

        while ((pool->exit == 0) && (pool->queueHead == pool->queueTail)) {
            pthread_cond_wait(&(pool->notify), &(pool->mutexQueue));

        }

        if ((pool->exit == THREAD_POOL_EXIT_IMMEDIATELY) ||
            ((pool->exit == THREAD_POOL_EXIT_DELAY) && (pool->queueHead == pool->queueTail))) {
            break;
        }

        THREAD_CALL_BACK callback = pool->queue[pool->queueHead].callback;
        void *args = pool->queue[pool->queueHead].args;
        pool->queueHead = (pool->queueHead + 1) % pool->queueSize;

        pool->totalInvokeCnt++;

        pthread_mutex_unlock(&(pool->mutexQueue));

        if (callback != NULL) {
            (*callback)(args);
        }
    }

    pthread_mutex_unlock(&(pool->mutexQueue));
    return NULL;
}

int ParamCheck(uint16_t *threadNum, uint16_t *queueSize)
{
    if (*threadNum == 0 || *queueSize == 0) {
        CM_LOGERROR(0, "create thread pool, invalid param, threadNum = %u, queueSize = %u\r\n", *threadNum,
            *queueSize);
        return -1;
    }
    if (*threadNum > THREAD_POOL_MAX_THREADS) {
        *threadNum = THREAD_POOL_MAX_THREADS;
        CM_LOGERROR(0, "create thread pool, threadNum = %u\r\n", *threadNum);
    }

    if (*queueSize > THREAD_POOL_MAX_QUEUE_SIZE) {
        *queueSize = THREAD_POOL_MAX_QUEUE_SIZE;
        CM_LOGERROR(0, "create thread pool, queueSize = %u\r\n", *queueSize);
    }
    return 0;
}

void FreeRes(CM_THREAD_POOL_S *pool)
{
    if (pool != NULL) {
        if (pool->tid != NULL) {
            free(pool->tid);
        }
        if (pool->queue != NULL) {
            free(pool->queue);
        }
        free(pool);
    }
    return;
}

CM_THREAD_POOL_S *CmThreadPoolCreate(uint16_t threadNum, uint16_t queueSize, uint16_t flags, const char *poolName)
{
    if (ParamCheck(&threadNum, &queueSize)) {
        return NULL;
    }
    CM_THREAD_POOL_S *pool = (CM_THREAD_POOL_S *)malloc(sizeof(CM_THREAD_POOL_S));
    if (pool == NULL) {
        return NULL;
    }

    memset_s(pool, sizeof(CM_THREAD_POOL_S), 0, sizeof(CM_THREAD_POOL_S));
    pool->flags = flags;
    pool->threadNum = threadNum;
    pool->queueSize = queueSize;
    int ret = strncpy_s(pool->name, sizeof(pool->name), poolName, sizeof(pool->name) - 1);
    if (ret != 0) {
        FreeRes(pool);
        return NULL;
    }

    pool->exit = THREAD_POOL_RUNNING;
    pool->queueFull = 0;
    pool->queueHead = 0;
    pool->queueTail = 0;

    pthread_mutex_init(&pool->mutexPool, 0);
    pthread_mutex_init(&pool->mutexQueue, 0);
    pthread_cond_init(&(pool->notify), 0);
    sem_init(&pool->semQueueFull, 0, 0);

    pool->tid = (pthread_t *)malloc(sizeof(pthread_t) * threadNum);
    pool->queue = (CM_THREAD_QUEUE_S *)malloc(sizeof(CM_THREAD_QUEUE_S) * queueSize);
    if ((pool->tid == NULL) || (pool->queue == NULL)) {
        FreeRes(pool);
        return NULL;
    }
    size_t len = sizeof(pthread_t) * threadNum;
    memset_s(pool->tid, len, 0, len);
    len = sizeof(CM_THREAD_QUEUE_S) * queueSize;
    memset_s(pool->queue, len, 0, len);

    for (uint16_t i = 0; i < threadNum; i++) {
        if (pthread_create(&(pool->tid[i]), NULL, ThreadPoolThread, (void *)pool) != 0) {
            CmThreadPoolDestroy(pool, 0);
            return NULL;
        }
        pthread_setname_np(pool->tid[i], poolName);
    }

    return pool;
}

int32_t ThreadPoolEnqueue(CM_THREAD_POOL_S *pool, THREAD_CALL_BACK callback, void *args)
{
    pthread_mutex_lock(&pool->mutexQueue);

    if (((pool->queueTail + 1) % pool->queueSize) == pool->queueHead) {
        pthread_mutex_unlock(&pool->mutexQueue);
        return -1;
    }

    pool->queue[pool->queueTail].callback = callback;
    pool->queue[pool->queueTail].args = args;

    pool->queueTail = (pool->queueTail + 1) % pool->queueSize;

    pthread_cond_signal(&pool->notify);

    pool->enqueueCnt++;

    pthread_mutex_unlock(&pool->mutexQueue);

    return 0;
}

int32_t CmThreadPoolAdd(CM_THREAD_POOL_S *pool, THREAD_CALL_BACK callback, void *args)
{
    if (pool == NULL) {
        return -1;
    }

    int32_t ret = -1;
    while (ret != 0) {
        ret = ThreadPoolEnqueue(pool, callback, args);
        if (ret == 0) {
            break;
        }

        pool->queueFullCnt++;
        usleep(200UL);
    }

    return ret;
}

int32_t CmThreadPoolDestroy(CM_THREAD_POOL_S *pool, int32_t flags)
{
    if (pool == NULL) {
        return -1;
    }

    pthread_mutex_lock(&pool->mutexPool);

    if (flags == 0) {
        pool->exit = THREAD_POOL_EXIT_DELAY;
    } else {
        pool->exit = THREAD_POOL_EXIT_IMMEDIATELY;
    }

    pthread_cond_broadcast(&pool->notify);

    for (uint16_t i = 0; i < pool->threadNum; i++) {
        if (pool->tid[i] != 0) {
            pthread_join(pool->tid[i], NULL);
        }
    }

    pthread_mutex_unlock(&pool->mutexPool);
    pthread_mutex_destroy(&(pool->mutexPool));
    pthread_mutex_destroy(&(pool->mutexQueue));
    pthread_cond_destroy(&(pool->notify));

    free(pool->tid);
    free(pool->queue);
    free(pool);

    return 0;
}
