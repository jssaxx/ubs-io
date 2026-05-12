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

#ifndef CM_THREADPOOL_H
#define CM_THREADPOOL_H

#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*THREAD_CALL_BACK)(void *arg);

#define THREAD_POOL_NAME_LEN 64
#define THREAD_POOL_MAX_THREADS 2048
#define THREAD_POOL_MAX_QUEUE_SIZE 32768

enum {
    THREAD_POOL_RUNNING,
    THREAD_POOL_EXIT_DELAY,      // 延迟退出，等待队列任务处理完
    THREAD_POOL_EXIT_IMMEDIATELY // 立即退出
};

typedef struct {
    THREAD_CALL_BACK callback;
    void *args;
} CM_THREAD_QUEUE_S;

typedef struct {
    char name[THREAD_POOL_NAME_LEN];
    uint16_t flags;
    uint16_t thread_num; // 线程总数

    pthread_t *tid;
    CM_THREAD_QUEUE_S *queue;

    pthread_mutex_t mutex_pool;  // thread pool操作互斥信号量
    pthread_mutex_t mutex_queue; // 队列操作互斥信号量
    pthread_cond_t notify;
    sem_t sem_queue_full; // 队列满

    uint16_t exit;
    uint16_t queue_full;

    uint16_t queue_size;
    uint16_t queue_head;
    uint16_t queue_tail; // 队列大小

    uint16_t enqueue_cnt;
    uint16_t total_invoke_cnt;
    uint16_t queue_full_cnt;
} CM_THREAD_POOL_S;

CM_THREAD_POOL_S *CmThreadPoolCreate(uint16_t thread_num, uint16_t queue_size, uint16_t flags, const char *pool_name);
int32_t CmThreadPoolAdd(CM_THREAD_POOL_S *pool, THREAD_CALL_BACK callback, void *args);
int32_t CmThreadPoolDestroy(CM_THREAD_POOL_S *pool, int32_t flags);

#ifdef __cplusplus
}
#endif

#endif

