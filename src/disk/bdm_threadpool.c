/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "bdm_threadpool.h"
#include "bdm_common.h"
#include "securec.h"

#define THREAD_SLEEP_INTERVAL 200

void BdmThreadBindCPUs(const char *name, int32_t cpuid)
{
    int32_t cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (cpuid == -1 || cpuid >= cpus) {
        return;
    }

    pthread_t pSelf = pthread_self();
    int32_t ret;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpuid, &mask);

    ret = pthread_setaffinity_np(pSelf, sizeof(mask), &mask);
    if (ret < 0) {
        BDM_LOGERROR(0, "Thread:%s, binding to CPU:%d, failed, errno(%s).", name, cpuid, strerror(errno));
        return;
    }

    usleep(THREAD_SLEEP_INTERVAL);
    cpu_set_t get;
    CPU_ZERO(&get);

    ret = pthread_getaffinity_np(pSelf, sizeof(get), &get);
    if (ret < 0) {
        BDM_LOGERROR(0, "Thread:%s, failed to get bind CPU:%d, failed, errno(%s).", name, cpuid, strerror(errno));
        return;
    }

    for (int i = 0; i < cpus; i++) {
        if (CPU_ISSET(i, &get)) {
            BDM_LOGINFO(0, "Thread:%s, index:%d, tid:%ld, is binded to CPU %d.", name, i, pSelf, cpuid);
        }
    }

    return;
}

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

static int BdmThreadParaCheck(uint32_t threadNum, uint32_t queueSize)
{
    if (threadNum == 0 || threadNum > BDM_THREAD_MAX_THREADS) {
        BDM_LOGERROR(0, "Invalid param, threadNum = %u.", threadNum);
        return -1;
    }

    if (queueSize == 0 || queueSize > BDM_THREAD_MAX_QUEUE_SIZE) {
        BDM_LOGERROR(0, "Invalid param, queueSize = %u.", queueSize);
        return -1;
    }
    return 0;
}

static void BdmThreadFreeRes(BDM_THREAD_S *thread)
{
    pthread_mutex_destroy(&(thread->poolMutex));
    pthread_mutex_destroy(&(thread->queueMutex));
    pthread_cond_destroy(&(thread->notify));

    if (thread->queue != NULL) {
        free(thread->queue);
        thread->queue = NULL;
    }
    return;
}

static int32_t BdmThreadPoolEnqueue(BDM_THREAD_S *thread, BDM_THREAD_HANDLE handle, void *ctx)
{
    pthread_mutex_lock(&thread->queueMutex);

    if (((thread->queueTail + 1) % thread->queueSize) == thread->queueHead) {
        pthread_mutex_unlock(&thread->queueMutex);
        return -1;
    }

    thread->queue[thread->queueTail].handle = handle;
    thread->queue[thread->queueTail].ctx = ctx;

    thread->queueTail = (thread->queueTail + 1) % thread->queueSize;

    pthread_cond_signal(&thread->notify);

    thread->enqueueCnt++;

    pthread_mutex_unlock(&thread->queueMutex);

    return 0;
}

static int32_t BdmThreadCreate(BDM_THREAD_S *thread, uint32_t queueSize, int32_t cpuid, const char *poolName,
    BDM_BATCH_CTX_S *batchCtx)
{
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
        return -1;
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
        return -1;
    }
    pthread_setname_np(thread->tid, poolName);
    thread->init = TRUE;
    return 0;
}

BDM_THREAD_POOL_S *BdmThreadPoolCreate(uint32_t threadNum, uint32_t queueSize, BDM_BIND_CPU_S *binds,
    const char *poolName, BDM_BATCH_CTX_S *batchCtx)
{
    int32_t ret;

    if (BdmThreadParaCheck(threadNum, queueSize) != 0) {
        return NULL;
    }

    BDM_THREAD_POOL_S *threadPool = (BDM_THREAD_POOL_S *)malloc(sizeof(BDM_THREAD_POOL_S));
    if (threadPool == NULL) {
        return NULL;
    }
    memset_s(threadPool, sizeof(BDM_THREAD_POOL_S), 0, sizeof(BDM_THREAD_POOL_S));

    threadPool->index = 0;
    threadPool->threadNum = threadNum;

    uint32_t index;
    int32_t cpuid = -1;
    for (index = 0; index < threadNum; index++) {
        if (binds != NULL) {
            cpuid = binds->cpus[index % binds->cpunum];
        }
        ret = BdmThreadCreate(&threadPool->threadList[index], queueSize, cpuid, poolName, batchCtx);
        if (ret != 0) {
            ret = BdmThreadPoolDestroy(threadPool, 0);
            if (ret != 0) {
                BDM_LOGERROR(0, "ThreadPool destory fail, ret(%d).", ret);
            }
            return NULL;
        }
    }

    return threadPool;
}

int32_t BdmThreadPoolAdd(BDM_THREAD_POOL_S *threadPool, BDM_THREAD_HANDLE handle, void *ctx)
{
    if (threadPool == NULL) {
        return -1;
    }

    uint64_t index = ATOMIC_INC(&threadPool->index) % threadPool->threadNum;
    BDM_THREAD_S *thread = &threadPool->threadList[index];

    int32_t ret = -1;
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
        return -1;
    }

    uint32_t index;
    for (index = 0; index < threadPool->threadNum; index++) {
        if (threadPool->threadList[index].init == FALSE) {
            continue;
        }
        BdmThreadDestroy(&threadPool->threadList[index], flags);
    }

    free(threadPool);
    return 0;
}
