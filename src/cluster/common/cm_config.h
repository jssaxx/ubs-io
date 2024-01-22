/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef CM_CONFIG_H
#define CM_CONFIG_H

#include "cm_inner.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t CmConfigInit(ConfigRole role, PoolInfo *pools, uint16_t num, const char *zkIpMask, const char *ipStr);

int32_t CmConfigHasCfgPoolC(void);

PoolInfo *CmConfigGetPoolInfo(uint16_t poolId);

uint32_t CmConfigGetTimeOut(void);

uint32_t CmConfigGetPermFaultTimeOut(void);

const char *CmConfigGetIpv4AddrStr(void);

const char *CmConfigGetZkServerList(void);

int32_t CmConfigHasCfgPoolS(void);

#ifdef __cplusplus
}
#endif

#endif

