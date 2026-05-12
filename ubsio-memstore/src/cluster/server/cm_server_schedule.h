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

#ifndef CM_SERVER_SCHEDULE_H
#define CM_SERVER_SCHEDULE_H

#include "cm_threadpool.h"

#ifdef __cplusplus
extern "C" {
#endif

void CmServerSchedueAdd(uint16_t poolId, THREAD_CALL_BACK handle, void *ctx);

int32_t CmServerScheduleInit(void);

void CmServerScheduleExit(void);

#ifdef __cplusplus
}
#endif

#endif

