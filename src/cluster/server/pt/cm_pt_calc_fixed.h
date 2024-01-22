/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef CM_PT_FIXED_CALC_H
#define CM_PT_FIXED_CALC_H

#include "cm_pt_calc.h"

#ifdef __cplusplus
extern "C" {
#endif

Calculator CreateViewCalculator(uint16_t maxNodeNum, uint16_t maxPtNum, uint16_t copyNum, uint16_t minCopyNum);
void DestoryViewCalculator(Calculator calculator);
int32_t ViewCalculatorInitial(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList, PtEntryList *ptEntryList);
int32_t ViewCalculatorRebalance(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList, PtEntryList *ptEntryList);
int32_t ViewCalculatorNeedRebalance(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList, PtEntryList *ptEntryList);

#ifdef __cplusplus
}
#endif

#endif
