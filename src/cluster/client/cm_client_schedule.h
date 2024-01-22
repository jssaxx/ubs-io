/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef CM_CLIENT_SCHEDULE_H
#define CM_CLIENT_SCHEDULE_H

#include "cm_threadpool.h"

#ifdef __cplusplus
extern "C" {
#endif

void CmClientSchedueAdd(uint16_t poolId, THREAD_CALL_BACK handle, void *ctx);

int32_t CmClientScheduleInit(void);

void CmClientScheduleExit(void);

#ifdef __cplusplus
}
#endif

#endif
