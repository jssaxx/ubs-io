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

#include "cm_thread.h"
#include <unistd.h>

#define CM_MILLI_TO_SECOND 1000

void CmSleep(unsigned int seconds)
{
    sleep((seconds) / CM_MILLI_TO_SECOND);
}

int32_t CmSwitchThreadAsync(const char *name, void *data, THREAD_CALL_BACK handle)
{
    int32_t ret;
    pthread_t threadId;

    ret = pthread_create(&threadId, NULL, handle, data);
    if (ret != 0) {
        return RETURN_ERROR;
    }
    pthread_setname_np(threadId, name);
    pthread_detach(threadId);
    return RETURN_OK;
}

