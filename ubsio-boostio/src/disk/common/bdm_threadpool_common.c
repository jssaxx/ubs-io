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

void BdmThreadBindCPUs(const char *name, int32_t cpuid)
{
    int32_t cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (cpuid == -1 || cpuid >= cpus) {
        return;
    }

    // bind cpu
    pthread_t pSelf = pthread_self();
    int32_t ret;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpuid, &mask);
    ret = pthread_setaffinity_np(pSelf, sizeof(mask), &mask);
    if (ret != 0) {
        BDM_LOGERROR(0, "Thread:%s, binding to CPU:%d, failed, ret: %d", name, cpuid, ret);
        return;
    }

    usleep(THREAD_SLEEP_INTERVAL);
    cpu_set_t get;
    CPU_ZERO(&get);
    ret = pthread_getaffinity_np(pSelf, sizeof(get), &get);
    if (ret != 0) {
        BDM_LOGERROR(0, "Thread:%s, failed to get bind CPU:%d, failed, ret(%d).", name, cpuid, ret);
        return;
    }

    for (int i = 0; i < cpus; i++) {
        if (CPU_ISSET(i, &get)) {
            BDM_LOGINFO(0, "Thread:%s, index:%d, tid:%ld, is binded to CPU %d.", name, i, pSelf, cpuid);
        }
    }

    return;
}

static int BdmThreadParaCheck(uint32_t threadNum, uint32_t queueSize)
{
    if (threadNum == 0 || threadNum > BDM_THREAD_MAX_THREADS) {
        BDM_LOGERROR(0, "Invalid param, threadNum = %u.", threadNum);
        return BDM_CODE_ERR;
    }
    if (queueSize == 0 || queueSize > BDM_THREAD_MAX_QUEUE_SIZE) {
        BDM_LOGERROR(0, "Invalid param, queueSize = %u.", queueSize);
        return BDM_CODE_ERR;
    }
    return BDM_CODE_OK;
}

void BdmThreadFreeRes(BDM_THREAD_S *thread)
{
    pthread_mutex_destroy(&(thread->poolMutex));
    pthread_mutex_destroy(&(thread->queueMutex));
    pthread_cond_destroy(&(thread->notify));
    if (thread->queue != NULL) {
        free(thread->queue);
        thread->queue = NULL;
    }
}

BDM_THREAD_POOL_S *BdmThreadPoolCreate(uint32_t threadNum, uint32_t queueSize, BDM_BIND_CPU_S *binds,
                                       const char *poolName, BDM_BATCH_CTX_S *batchCtx)
{
    if (BdmThreadParaCheck(threadNum, queueSize) != 0) {
        return NULL;
    }

    BDM_THREAD_POOL_S *threadPool = (BDM_THREAD_POOL_S *)malloc(sizeof(BDM_THREAD_POOL_S));
    if (threadPool == NULL) {
        return NULL;
    }
    int32_t ret = memset_s(threadPool, sizeof(BDM_THREAD_POOL_S), 0, sizeof(BDM_THREAD_POOL_S));
    if (ret != 0) {
        BDM_LOGERROR(0, "Memset_s fail, ret(%d).", ret);
        free(threadPool);
        threadPool = NULL;
        return NULL;
    }

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
