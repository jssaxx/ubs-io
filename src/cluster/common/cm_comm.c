/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
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
