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

#ifndef CM_PT_FIXED_CALC_H
#define CM_PT_FIXED_CALC_H

#include "cm_pt_calc.h"

#ifdef __cplusplus
extern "C" {
#endif

Calculator CreateViewCalculator(uint16_t maxNodeNum, uint16_t maxPtNum, uint16_t copyNum, uint16_t minCopyNum);
void DestoryViewCalculator(Calculator calculator);
int32_t ViewCalculatorInitial(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList);
int32_t ViewCalculatorRebalance(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList);
int32_t ViewCalculatorNeedRebalance(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList,
    PtEntryList *ptEntryList);

#ifdef __cplusplus
}
#endif

#endif

