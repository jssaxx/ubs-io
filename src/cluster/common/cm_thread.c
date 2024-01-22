/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "cm_thread.h"
#include <unistd.h>

#define CM_MILLI_TO_SECOND 1000

void CmSleep(unsigned int var)
{
    sleep((var) / CM_MILLI_TO_SECOND);
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

