/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef CM_SERVER_MONITOR_H
#define CM_SERVER_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*expiredNodeSet)(uint16_t poolId, uint16_t nodeId);
    void (*expiredDiskSet)(uint16_t poolId, uint16_t nodeId, uint16_t diskId);
    int32_t (*expiredCommit)(uint16_t poolId);
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
