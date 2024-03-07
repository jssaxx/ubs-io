/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2025. All rights reserved.
 */

#ifndef CM_PT_CALC_H
#define CM_PT_CALC_H

#include "cm_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PT_NUM_LIMIT_FIXED = 0,
    PT_NUM_LIMIT_UNFIXED = 1,
    PT_NUM_LIMIT_BUTT
} PtNumLimitMode;

typedef void *Calculator;

typedef struct {
    Calculator (*createCalculator)(uint16_t maxNodeNum, uint16_t maxPtNum, uint16_t copyNum, uint16_t minCopyNum);
    void (*destoryCalculator)(Calculator calculator);
    int32_t (*viewInitial)(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList, PtEntryList *ptList);
    int32_t (*viewRebalance)(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList, PtEntryList *ptList);
    int32_t (*needRebalance)(Calculator calculator, NodeInfoList *nodeList, NodeStateList *stateList, PtEntryList *ptList);
    void (*updateState)(uint16_t nodeId, NodeState state, NodeInfo *info, PtEntryList *ptList, int32_t *ptChange);
    void (*updateFinish)(uint16_t nodeId, CmPtFinish *ptList, uint16_t ptNum, PtEntryList *ptEntryList,
        int32_t *ptChange, uint16_t nodeNum, uint16_t validNum);
} CalcOps;

CalcOps *CmPtCalcOpsGet(PtNumLimitMode numLimit);

#ifdef __cplusplus
}
#endif

#endif

