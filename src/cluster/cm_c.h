/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef CM_C_H
#define CM_C_H

#include <stdint.h>
#include <stdbool.h>
#include "bdm_core.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CM_DISK_NORMAL = 0,
    CM_DISK_FAULT = 1,
} CmDiskStatus;

int32_t CmReportDiskStatus(uint16_t diskId, CmDiskStatus status);

int32_t CmAddNewDisk(uint16_t diskId, CmDiskStatus status, bool isNewDisk);

#ifdef __cplusplus
}
#endif

#endif // CM_C_H
