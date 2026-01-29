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

#include "bdm_threadpool.h"
#include "bdm_common.h"
#include "securec.h"
#include "bdm_core.h"

static void *BdmThreadThread(void *arg)
{
    BDM_THREAD_S *thread = (BDM_THREAD_S *)arg;

    BdmThreadBindCPUs(thread->name, thread->cpuid);

    while (1) {
        pthread_mutex_lock(&(thread->queueMutex));

        while ((!thread->exit) && (thread->queueHead == thread->queueTail)) {
            pthread_cond_wait(&(thread->notify), &(thread->queueMutex));
        }

        if ((thread->exit == BDM_THREAD_EXIT_IMMEDIATELY) ||
            ((thread->exit == BDM_THREAD_EXIT_DELAY) && (thread->queueHead == thread->queueTail))) {
            break;
        }

        BDM_THREAD_HANDLE handle = thread->queue[thread->queueHead].handle;
        void *ctx = thread->queue[thread->queueHead].ctx;
        thread->queueHead = (thread->queueHead + 1) % thread->queueSize;

        thread->totalInvokeCnt++;

        pthread_mutex_unlock(&(thread->queueMutex));

        if (handle != NULL) {
            (*handle)(ctx);
        }
    }

    pthread_mutex_unlock(&(thread->queueMutex));
    return NULL;
}

static void *BdmThreadBatchThread(void *arg)
{
    BDM_THREAD_S *thread = (BDM_THREAD_S *)arg;
    void *argList[BDM_BATCH_HANDLE_NUM];
    uint32_t argNum;
    int32_t ret;

    BdmThreadBindCPUs(thread->name, thread->cpuid);

    while (1) {
        pthread_mutex_lock(&(thread->queueMutex));

        while ((!thread->exit) && (thread->queueHead == thread->queueTail)) {
            pthread_cond_wait(&(thread->notify), &(thread->queueMutex));
        }

        if ((thread->exit == BDM_THREAD_EXIT_IMMEDIATELY) ||
            ((thread->exit == BDM_THREAD_EXIT_DELAY) && (thread->queueHead == thread->queueTail))) {
            break;
        }

        argNum = 0;
        while (argNum < BDM_BATCH_HANDLE_NUM && thread->queueHead != thread->queueTail) {
            argList[argNum] = thread->queue[thread->queueHead].ctx;
            argNum++;
            thread->queueHead = (thread->queueHead + 1) % thread->queueSize;

            thread->totalInvokeCnt++;
        }

        pthread_mutex_unlock(&(thread->queueMutex));

        ret = thread->batchHandle(argList, argNum, thread->batchCtx);
        if (ret != 0) {
            BDM_LOGERROR(0, "handle failed, ret = %d.\n", ret);
        }
    }

    pthread_mutex_unlock(&(thread->queueMutex));
    return NULL;
}

static int32_t BdmThreadPoolEnqueue(BDM_THREAD_S *thread, BDM_THREAD_HANDLE handle, void *ctx)
{
    pthread_mutex_lock(&thread->queueMutex);

    if (((thread->queueTail + 1) % thread->queueSize) == thread->queueHead) {
        pthread_mutex_unlock(&thread->queueMutex);
        return BDM_CODE_ERR;
    }

    thread->queue[thread->queueTail].handle = handle;
    thread->queue[thread->queueTail].ctx = ctx;

    thread->queueTail = (thread->queueTail + 1) % thread->queueSize;

    pthread_cond_signal(&thread->notify);

    thread->enqueueCnt++;

    pthread_mutex_unlock(&thread->queueMutex);

    return BDM_CODE_OK;
}

int32_t BdmThreadCreate(BDM_THREAD_S *thread, uint32_t queueSize, int32_t cpuid, const char *poolName,
                               BDM_BATCH_CTX_S *batchCtx)
{
    if (thread == NULL) {
        BDM_LOGERROR(0, "Invalid parameters, thread is nullptr.");
        return BDM_CODE_ERR;
    }
    if (poolName == NULL) {
        BDM_LOGERROR(0, "Invalid parameters, poolName is nullptr.");
        return BDM_CODE_ERR;
    }
    if (batchCtx == NULL) {
        BDM_LOGERROR(0, "Invalid parameters, batchCtx is nullptr.");
        return BDM_CODE_ERR;
    }

    thread->queueSize = queueSize;
    strncpy_s(thread->name, sizeof(thread->name), poolName, sizeof(thread->name) - 1);
    thread->batchHandle = batchCtx->batchHandle;
    thread->batchCtx = batchCtx->batchCtx;

    thread->cpuid = cpuid;

    thread->exit = BDM_THREAD_RUNNING;
    thread->queueHead = 0;
    thread->queueTail = 0;

    pthread_mutex_init(&thread->poolMutex, 0);
    pthread_mutex_init(&thread->queueMutex, 0);
    pthread_cond_init(&(thread->notify), 0);

    thread->queue = (BDM_THREAD_QUEUE_S *)malloc(sizeof(BDM_THREAD_QUEUE_S) * queueSize);
    if (thread->queue == NULL) {
        BdmThreadFreeRes(thread);
        return BDM_CODE_ERR;
    }
    memset_s(&thread->tid, sizeof(pthread_t), 0, sizeof(pthread_t));
    size_t len = sizeof(BDM_THREAD_QUEUE_S) * queueSize;
    memset_s(thread->queue, len, 0, len);

    BDM_THREAD_HANDLE threadHandle;
    if (batchCtx != NULL) {
        threadHandle = BdmThreadBatchThread;
    } else {
        threadHandle = BdmThreadThread;
    }

    if (pthread_create(&(thread->tid), NULL, threadHandle, (void *)thread) != 0) {
        BdmThreadFreeRes(thread);
        return BDM_CODE_ERR;
    }
    pthread_setname_np(thread->tid, poolName);
    thread->init = TRUE;
    return BDM_CODE_OK;
}

int32_t BdmThreadPoolAdd(BDM_THREAD_POOL_S *threadPool, BDM_THREAD_HANDLE handle, void *ctx)
{
    if (threadPool == NULL) {
        return BDM_CODE_ERR;
    }

    uint64_t index = ATOMIC_INC(&threadPool->index) % threadPool->threadNum;
    BDM_THREAD_S *thread = &threadPool->threadList[index];

    int32_t ret = BDM_CODE_ERR;
    while (ret != 0) {
        ret = BdmThreadPoolEnqueue(thread, handle, ctx);
        if (ret == 0) {
            break;
        }

        thread->queueFullCnt++;
        usleep(THREAD_SLEEP_INTERVAL);
    }

    return ret;
}

static void BdmThreadDestroy(BDM_THREAD_S *thread, int32_t flags)
{
    pthread_mutex_lock(&thread->poolMutex);

    if (!flags) {
        thread->exit = BDM_THREAD_EXIT_DELAY;
    } else {
        thread->exit = BDM_THREAD_EXIT_IMMEDIATELY;
    }

    pthread_cond_broadcast(&thread->notify);

    pthread_join(thread->tid, NULL);

    pthread_mutex_unlock(&thread->poolMutex);
    pthread_mutex_destroy(&(thread->poolMutex));
    pthread_mutex_destroy(&(thread->queueMutex));
    pthread_cond_destroy(&(thread->notify));

    if (thread->queue != NULL) {
        free(thread->queue);
        thread->queue = NULL;
    }

    return;
}

int32_t BdmThreadPoolDestroy(BDM_THREAD_POOL_S *threadPool, int32_t flags)
{
    if (threadPool == NULL) {
        return BDM_CODE_ERR;
    }

    uint32_t index;
    for (index = 0; index < threadPool->threadNum; index++) {
        if (threadPool->threadList[index].init == FALSE) {
            continue;
        }
        BdmThreadDestroy(&threadPool->threadList[index], flags);
    }

    free(threadPool);
    return BDM_CODE_OK;
}