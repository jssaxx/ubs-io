/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef BDM_DISK_H
#define BDM_DISK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t BdmDiskInit(void);

uint32_t BdmGetNormalDiskNum(void);

void BdmSetNormalDiskNum(uint32_t diskNum);

#ifdef __cplusplus
}
#endif

#endif
