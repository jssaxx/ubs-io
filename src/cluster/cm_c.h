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
