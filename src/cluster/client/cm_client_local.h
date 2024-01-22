/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef CM_CLIENT_LOCAL_H
#define CM_CLIENT_LOCAL_H

#include "cm_inner.h"

#ifdef __cplusplus
extern "C" {
#endif

uint16_t CmClientLocalGetNodeId(uint16_t poolId);

int32_t CmClientLocalGetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

void CmClientLocalUpdateNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

int32_t CmClientLocalInit(void);

void CmClientLocalExit(void);

#ifdef __cplusplus
}
#endif

#endif
