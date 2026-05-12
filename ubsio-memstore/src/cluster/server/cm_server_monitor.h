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

#ifndef CM_SERVER_MONITOR_H
#define CM_SERVER_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*ExpiredNodeSet)(uint16_t poolId, uint16_t nodeId);
    void (*ExpiredDiskSet)(uint16_t poolId, uint16_t nodeId, uint16_t diskId);
    int32_t (*ExpiredCommit)(uint16_t poolId);
} CmServerMonitorExpiredHandle;

void CmServerMonitorRegisterHandle(CmServerMonitorExpiredHandle handle);

void CmServerListenNodeFault(uint16_t poolId, uint16_t nodeId);

void CmServerListenDiskFault(uint16_t poolId, uint16_t nodeId, uint16_t diskId);

void CmServerCancelNodeFault(uint16_t poolId, uint16_t nodeId);

void CmServerCancelDiskFault(uint16_t poolId, uint16_t nodeId, uint16_t diskId);

int32_t CmServerMonitorLoadPool(uint16_t poolId);

void CmServerMonitorInitMgr(void);

void CmServerMonitorReset(void);

int32_t CmServerMonitorInit(void);

void CmServerMonitorExit(void);

#ifdef __cplusplus
}
#endif

#endif

