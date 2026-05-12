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

#include <unistd.h>
#include "cm_log.h"
#include "securec.h"
#include "cm_threadpool.h"

void *ThreadPoolThread(void *thread_pool)
{
    CM_THREAD_POOL_S *pool = (CM_THREAD_POOL_S *)thread_pool;

    while (1) {
        pthread_mutex_lock(&(pool->mutex_queue));

        while ((!pool->exit) && (pool->queue_head == pool->queue_tail)) {
            pthread_cond_wait(&(pool->notify), &(pool->mutex_queue));
        }

        if ((pool->exit == THREAD_POOL_EXIT_IMMEDIATELY) ||
            ((pool->exit == THREAD_POOL_EXIT_DELAY) && (pool->queue_head == pool->queue_tail))) {
            break;
        }

        THREAD_CALL_BACK callback = pool->queue[pool->queue_head].callback;
        void *args = pool->queue[pool->queue_head].args;
        pool->queue_head = (pool->queue_head + 1) % pool->queue_size;

        pool->total_invoke_cnt++;

        pthread_mutex_unlock(&(pool->mutex_queue));

        if (callback != NULL) {
            (*callback)(args);
        }
    }

    pthread_mutex_unlock(&(pool->mutex_queue));
    return NULL;
}

int ParamCheck(uint16_t *thread_num, uint16_t *queue_size)
{
    if (*thread_num == 0 || *queue_size == 0) {
        CM_LOGERROR(0, "create thread pool, invalid param, thread_num = %u, queue_size = %u\r\n", *thread_num,
            *queue_size);
        return -1;
    }
    if (*thread_num > THREAD_POOL_MAX_THREADS) {
        *thread_num = THREAD_POOL_MAX_THREADS;
        CM_LOGERROR(0, "create thread pool, thread_num = %u\r\n", *thread_num);
    }

    if (*queue_size > THREAD_POOL_MAX_QUEUE_SIZE) {
        *queue_size = THREAD_POOL_MAX_QUEUE_SIZE;
        CM_LOGERROR(0, "create thread pool, queue_size = %u\r\n", *queue_size);
    }
    return 0;
}

void FreeRes(CM_THREAD_POOL_S *pool)
{
    if (pool != NULL) {
        if (pool->tid != NULL) {
            free(pool->tid);
            pool->tid = NULL;
        }
        if (pool->queue != NULL) {
            free(pool->queue);
            pool->queue = NULL;
        }
        free(pool);
        pool = NULL;
    }
    return;
}

CM_THREAD_POOL_S *CmThreadPoolCreate(uint16_t thread_num, uint16_t queue_size, uint16_t flags, const char *pool_name)
{
    if (ParamCheck(&thread_num, &queue_size)) {
        return NULL;
    }
    CM_THREAD_POOL_S *pool = (CM_THREAD_POOL_S *)malloc(sizeof(CM_THREAD_POOL_S));
    if (pool == NULL) {
        return NULL;
    }

    memset_s(pool, sizeof(CM_THREAD_POOL_S), 0, sizeof(CM_THREAD_POOL_S));
    pool->flags = flags;
    pool->thread_num = thread_num;
    pool->queue_size = queue_size;
    strncpy_s(pool->name, sizeof(pool->name), pool_name, sizeof(pool->name) - 1);

    pool->exit = THREAD_POOL_RUNNING;
    pool->queue_full = 0;
    pool->queue_head = 0;
    pool->queue_tail = 0;

    pthread_mutex_init(&pool->mutex_pool, 0);
    pthread_mutex_init(&pool->mutex_queue, 0);
    pthread_cond_init(&(pool->notify), 0);
    sem_init(&pool->sem_queue_full, 0, 0);

    pool->tid = (pthread_t *)malloc(sizeof(pthread_t) * thread_num);
    pool->queue = (CM_THREAD_QUEUE_S *)malloc(sizeof(CM_THREAD_QUEUE_S) * queue_size);
    if ((pool->tid == NULL) || (pool->queue == NULL)) {
        FreeRes(pool);
        return NULL;
    }
    size_t len = sizeof(pthread_t) * thread_num;
    memset_s(pool->tid, len, 0, len);
    len = sizeof(CM_THREAD_QUEUE_S) * queue_size;
    memset_s(pool->queue, len, 0, len);

    for (uint16_t i = 0; i < thread_num; i++) {
        if (pthread_create(&(pool->tid[i]), NULL, ThreadPoolThread, (void *)pool) != 0) {
            CmThreadPoolDestroy(pool, 0);
            return NULL;
        }
        pthread_setname_np(pool->tid[i], pool_name);
    }

    return pool;
}

int32_t ThreadPoolEnqueue(CM_THREAD_POOL_S *pool, THREAD_CALL_BACK callback, void *args)
{
    pthread_mutex_lock(&pool->mutex_queue);

    if (((pool->queue_tail + 1) % pool->queue_size) == pool->queue_head) {
        pthread_mutex_unlock(&pool->mutex_queue);
        return -1;
    }

    pool->queue[pool->queue_tail].callback = callback;
    pool->queue[pool->queue_tail].args = args;

    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_size;

    pthread_cond_signal(&pool->notify);

    pool->enqueue_cnt++;

    pthread_mutex_unlock(&pool->mutex_queue);

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

        pool->queue_full_cnt++;
        usleep(200);
    }

    return ret;
}

int32_t CmThreadPoolDestroy(CM_THREAD_POOL_S *pool, int32_t flags)
{
    if (pool == NULL) {
        return -1;
    }

    pthread_mutex_lock(&pool->mutex_pool);

    if (!flags) {
        pool->exit = THREAD_POOL_EXIT_DELAY;
    } else {
        pool->exit = THREAD_POOL_EXIT_IMMEDIATELY;
    }

    pthread_cond_broadcast(&pool->notify);

    for (uint16_t i = 0; i < pool->thread_num; i++) {
        if (pool->tid[i] != 0) {
            pthread_join(pool->tid[i], NULL);
        }
    }

    pthread_mutex_unlock(&pool->mutex_pool);
    pthread_mutex_destroy(&(pool->mutex_pool));
    pthread_mutex_destroy(&(pool->mutex_queue));
    pthread_cond_destroy(&(pool->notify));

    free(pool->tid);
    pool->tid = NULL;
    free(pool->queue);
    pool->queue = NULL;
    free(pool);
    pool = NULL;

    return 0;
}

