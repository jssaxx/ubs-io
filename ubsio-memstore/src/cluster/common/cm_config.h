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

#ifndef CM_CONFIG_H
#define CM_CONFIG_H

#include "cm_inner.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t CmConfigInit(ConfigRole role, PoolInfo *pools, uint16_t num, const CmCfgInfo *cfgInfo);

int32_t CmConfigHasCfgPoolC(void);

PoolInfo *CmConfigGetPoolInfo(uint16_t poolId);

uint32_t CmConfigGetTimeOut(void);

uint32_t CmConfigGetPermFaultTimeOut(void);

uint32_t CmConfigGetDiskPermFaultTimeOut(void);

uint16_t CmConfigGetNodeId(void);

const char *CmConfigGetIpv4AddrStr(void);

const char *CmConfigGetZkServerList(void);

int32_t CmConfigHasCfgPoolS(void);

#ifdef __cplusplus
}
#endif

#endif

