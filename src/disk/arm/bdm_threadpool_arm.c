/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "bdm_threadpool.h"
#include "bdm_common.h"
#include "securec.h"
#include "bdm_core.h"

#define LANE_INDEX_0 (0)
#define LANE_INDEX_1 (1)
#define LANE_INDEX_2 (2)
#define LANE_INDEX_3 (3)

static void *BdmThreadThread(void *arg)
{
    BDM_THREAD_S *thread = (BDM_THREAD_S *)arg;

    BdmThreadBindCPUs(thread->name, thread->cpuid);

    while (1) {
        pthread_mutex_lock(&(thread->queueMutex));

        while ((!vgetq_lane_u32(thread->state_desc, LANE_INDEX_0)) &&
               (vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2) == vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3))) {
            pthread_cond_wait(&(thread->notify), &(thread->queueMutex));
        }

        if ((vgetq_lane_u32(thread->state_desc, LANE_INDEX_0) == BDM_THREAD_EXIT_IMMEDIATELY) ||
            ((vgetq_lane_u32(thread->state_desc, LANE_INDEX_0) == BDM_THREAD_EXIT_DELAY) &&
             (vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2) == vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3)))) {
            break;
        }

        BDM_THREAD_HANDLE handle = thread->queue[vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2)].handle;
        void *ctx = thread->queue[vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2)].ctx;
        uint32_t tmpLane = vgetq_lane_u32(thread->queue_desc, LANE_INDEX_1);
        if (tmpLane == 0) {
            continue;
        }
        uint32_t val = (vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2) + 1) % tmpLane;
        thread->queue_desc = vsetq_lane_u32(val, thread->queue_desc, LANE_INDEX_2);

        thread->state_desc = vsetq_lane_u32(vgetq_lane_u32(thread->state_desc, LANE_INDEX_2) + 1, thread->state_desc,
                                            LANE_INDEX_2);

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

        while ((!vgetq_lane_u32(thread->state_desc, LANE_INDEX_0)) &&
            (vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2) == vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3))) {
            pthread_cond_wait(&(thread->notify), &(thread->queueMutex));
        }

        if ((vgetq_lane_u32(thread->state_desc, LANE_INDEX_0) == BDM_THREAD_EXIT_IMMEDIATELY) ||
            ((vgetq_lane_u32(thread->state_desc, LANE_INDEX_0) == BDM_THREAD_EXIT_DELAY) &&
            (vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2) == vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3)))) {
            break;
        }

        argNum = 0;
        while (argNum < BDM_BATCH_HANDLE_NUM &&
            vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2) != vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3)) {
            argList[argNum] = thread->queue[vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2)].ctx;
            argNum++;
            uint32_t tmpLane = vgetq_lane_u32(thread->queue_desc, LANE_INDEX_1);
            if (tmpLane == 0) {
                continue;
            }
            uint32_t val = (vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2) + 1) % tmpLane;
            thread->queue_desc = vsetq_lane_u32(val, thread->queue_desc, LANE_INDEX_2);

            thread->state_desc = vsetq_lane_u32(vgetq_lane_u32(thread->state_desc, LANE_INDEX_2) + 1,
                                                thread->state_desc, LANE_INDEX_2);
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

    uint32_t tmpLane = vgetq_lane_u32(thread->queue_desc, LANE_INDEX_1);
    if (tmpLane == 0) {
        return BDM_CODE_ERR;
    }

    if (((vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3) + 1) % tmpLane) ==
        vgetq_lane_u32(thread->queue_desc, LANE_INDEX_2)) {
        pthread_mutex_unlock(&thread->queueMutex);
        return BDM_CODE_ERR;
    }

    thread->queue[vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3)].handle = handle;
    thread->queue[vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3)].ctx = ctx;

    uint32_t val = (vgetq_lane_u32(thread->queue_desc, LANE_INDEX_3) + 1) % tmpLane;
    thread->queue_desc = vsetq_lane_u32(val, thread->queue_desc, LANE_INDEX_3);

    pthread_cond_signal(&thread->notify);

    thread->state_desc = vsetq_lane_u32(vgetq_lane_u32(thread->state_desc, LANE_INDEX_1) + 1, thread->state_desc,
                                        LANE_INDEX_1);

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
    thread->queue_desc = vdupq_n_u32(0);
    thread->queue_desc = vsetq_lane_u32(queueSize, thread->queue_desc, 1);
    int32_t ret = strncpy_s(thread->name, sizeof(thread->name), poolName, sizeof(thread->name) - 1);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Strncpy_s failed(%d).", ret);
        return BDM_CODE_ERR;
    }
    thread->batchHandle = batchCtx->batchHandle;
    thread->batchCtx = batchCtx->batchCtx;

    thread->cpuid = cpuid;

    thread->state_desc = vdupq_n_u32(0);
    thread->state_desc = vsetq_lane_u32(BDM_THREAD_RUNNING, thread->state_desc, 0);

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
    thread->queue_desc = vsetq_lane_u32(TRUE, thread->queue_desc, 0);
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

        thread->state_desc = vsetq_lane_u32(vgetq_lane_u32(thread->state_desc, LANE_INDEX_3) + 1, thread->state_desc,
                                            LANE_INDEX_3);
        usleep(THREAD_SLEEP_INTERVAL);
    }

    return ret;
}

static void BdmThreadDestroy(BDM_THREAD_S *thread, int32_t flags)
{
    pthread_mutex_lock(&thread->poolMutex);

    if (!flags) {
        thread->state_desc = vsetq_lane_u32(BDM_THREAD_EXIT_DELAY, thread->state_desc, 0);
    } else {
        thread->state_desc = vsetq_lane_u32(BDM_THREAD_EXIT_IMMEDIATELY, thread->state_desc, 0);
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
        if (vgetq_lane_u32(threadPool->threadList[index].queue_desc, LANE_INDEX_0) == FALSE) {
            continue;
        }
        BdmThreadDestroy(&threadPool->threadList[index], flags);
    }

    free(threadPool);
    threadPool = NULL;
    return BDM_CODE_OK;
}
