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

#ifndef CM_PT_CALC_FIXED_STATE_H
#define CM_PT_CALC_FIXED_STATE_H

#include "cm_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

void ViewPtEntryListUpdateNodeState(uint16_t nodeId, NodeState state, NodeInfo *info, PtEntryList *ptList,
    int32_t *pgChange);

void ViewPtEntryListUpdateNodeFinish(uint16_t nodeId, CmPtFinish *ptList, uint16_t ptNum, PtEntryList *ptEntryList,
    int32_t *ptChange, uint16_t nodeNum);

#ifdef __cplusplus
}
#endif

#endif

