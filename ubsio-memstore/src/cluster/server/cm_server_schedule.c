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

#include "cm_server_schedule.h"
#include "cm_log.h"

#define CM_SERVER_THREADPOOL_NUM (1)
#define CM_SERVER_THREADPOOL_NAME "cm_server_schedule"
#define CM_SERVER_THREAD_NUM (1)
#define CM_SERVER_QUEUE_SIZE (1024)

static CM_THREAD_POOL_S *g_scheduleThread[CM_SERVER_THREADPOOL_NUM] = { NULL };

void CmServerSchedueAdd(uint16_t poolId, THREAD_CALL_BACK handle, void *ctx)
{
    uint16_t index = poolId / CM_SERVER_THREADPOOL_NUM;

    int32_t ret = CmThreadPoolAdd(g_scheduleThread[index], handle, ctx);
    if (ret != RETURN_OK) {
        CM_LOGERROR("Threadpool add failed, poolId(%u) ret(%d).", poolId, ret);
        handle(ctx);
        return;
    }

    return;
}

int32_t CmServerScheduleInit(void)
{
    uint16_t i;

    for (i = 0; i < CM_SERVER_THREADPOOL_NUM; i++) {
        g_scheduleThread[i] =
            CmThreadPoolCreate(CM_SERVER_THREAD_NUM, CM_SERVER_QUEUE_SIZE, 0, CM_SERVER_THREADPOOL_NAME);
        if (g_scheduleThread[i] == NULL) {
            CM_LOGERROR("Threadpool create failed.");
            return RETURN_ERROR;
        }
    }
    CM_LOGINFO("Cm server schedule init succeed.");
    return RETURN_OK;
}

void CmServerScheduleExit(void)
{
    uint16_t i;

    for (i = 0; i < CM_SERVER_THREADPOOL_NUM; i++) {
        if (g_scheduleThread[i] != NULL) {
            int32_t ret = CmThreadPoolDestroy(g_scheduleThread[i], THREAD_POOL_EXIT_DELAY);
            if (ret != RETURN_OK) {
                CM_LOGERROR("Threadpool destory failed, ret(%d).", ret);
                continue;
            }
            g_scheduleThread[i] = NULL;
        }
    }

    return;
}

