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

#ifndef BDM_THREADPOOL_H
#define BDM_THREADPOOL_H

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <unistd.h>

#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>
#endif

#define THREAD_SLEEP_INTERVAL (200)

#ifdef __cplusplus
extern "C" {
#endif

#define BDM_BATCH_HANDLE_NUM (16UL)

typedef int32_t (*BDM_THREAD_BATCH_HANDLE)(void **argList, uint32_t num, void *ctx);
typedef void *(*BDM_THREAD_HANDLE)(void *arg);

#define BDM_THREAD_NAME_LEN 64
#define BDM_THREAD_MAX_THREADS 256
#define BDM_THREAD_MAX_QUEUE_SIZE 32768

enum
{
    BDM_THREAD_RUNNING,
    BDM_THREAD_EXIT_DELAY,      // 延迟退出，等待队列任务处理完
    BDM_THREAD_EXIT_IMMEDIATELY // 立即退出
};

typedef struct {
    BDM_THREAD_BATCH_HANDLE batchHandle;
    void *batchCtx;
} BDM_BATCH_CTX_S;

typedef struct {
    uint32_t cpunum;
    int32_t cpus[BDM_THREAD_MAX_THREADS];
} BDM_BIND_CPU_S;

typedef struct {
    BDM_THREAD_HANDLE handle;
    void *ctx;
} BDM_THREAD_QUEUE_S;

typedef struct {
    char name[BDM_THREAD_NAME_LEN];
    BDM_THREAD_BATCH_HANDLE batchHandle;
    void *batchCtx;

    int32_t cpuid;

    pthread_t tid;
    BDM_THREAD_QUEUE_S *queue;

    pthread_mutex_t poolMutex;
    pthread_mutex_t queueMutex;
    pthread_cond_t notify;

#if defined(__aarch64__) || defined(__arm__)
    uint32x4_t queue_desc; // init, queue size, head, tail
    uint32x4_t state_desc; // exit, enqueue cnt, invoke cnt, queue full cnt
#else
    uint32_t init;
    uint32_t exit;

    uint32_t queueSize;
    uint32_t queueHead;
    uint32_t queueTail;

    uint32_t enqueueCnt;
    uint32_t totalInvokeCnt;
    uint32_t queueFullCnt;
#endif
} BDM_THREAD_S;

typedef struct {
    uint32_t index;
    uint32_t threadNum;
    BDM_THREAD_S threadList[BDM_THREAD_MAX_THREADS];
} BDM_THREAD_POOL_S;

void BdmThreadBindCPUs(const char *name, int32_t cpuid);
BDM_THREAD_POOL_S *BdmThreadPoolCreate(uint32_t threadNum, uint32_t queueSize, BDM_BIND_CPU_S *binds,
                                       const char *poolName, BDM_BATCH_CTX_S *batchCtx);
void BdmThreadFreeRes(BDM_THREAD_S *thread);
int32_t BdmThreadCreate(BDM_THREAD_S *thread, uint32_t queueSize, int32_t cpuid, const char *poolName,
                        BDM_BATCH_CTX_S *batchCtx);
int32_t BdmThreadPoolAdd(BDM_THREAD_POOL_S *threadPool, BDM_THREAD_HANDLE handle, void *ctx);
int32_t BdmThreadPoolDestroy(BDM_THREAD_POOL_S *threadPool, int32_t flags);

#ifdef __cplusplus
}
#endif

#endif
