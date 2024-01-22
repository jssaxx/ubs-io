/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef CM_PT_CALC_FIXED_STATE_H
#define CM_PT_CALC_FIXED_STATE_H

#include "cm_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

void ViewPtEntryListUpdateNodeState(uint16_t nodeId, NodeState state, NodeInfo *info, PtEntryList *ptList, int32_t *pgChange);

void ViewPtEntryListUpdateNodeFinish(uint16_t nodeId, CmPtFinish *ptList, uint16_t ptNum, PtEntryList *ptEntryList, int32_t *ptChange);

#ifdef __cplusplus
}
#endif

#endif
