/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef CM_SERVER_VIEW_H
#define CM_SERVER_VIEW_H

#include "cm_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t CmServerViewGetNodeState(uint16_t poolId, NodeStateInfo *state);

int32_t CmServerViewInit(void);

void CmServerViewExit(void);

#ifdef __cplusplus
}
#endif

#endif
