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

#include "cm_comm.h"
#include <time.h>

#define CM_MILLI_SECOND 1000000

#define CM_NANO_SECOND 1000000000

uint64_t CmGetNanoTime(void)
{
    struct timespec tval;

    clock_gettime(CLOCK_MONOTONIC, &tval);

    return (uint64_t)tval.tv_sec * (uint64_t)CM_NANO_SECOND + (uint64_t)tval.tv_nsec;
}

uint64_t CmGetMilliTime(void)
{
    struct timespec tval;

    clock_gettime(CLOCK_MONOTONIC, &tval);

    return ((uint64_t)tval.tv_sec * (uint64_t)CM_NANO_SECOND + (uint64_t)tval.tv_nsec) / CM_MILLI_SECOND;
}

uint64_t CmGetSecondsTime(void)
{
    struct timespec tval;

    clock_gettime(CLOCK_MONOTONIC, &tval);

    return tval.tv_sec;
}

