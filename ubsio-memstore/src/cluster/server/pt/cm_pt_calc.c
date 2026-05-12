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

#include "cm_log.h"
#include "cm_pt_calc_fixed.h"
#include "cm_pt_calc_fixed_state.h"
#include "cm_pt_calc.h"

static CalcOps g_calcOps[PT_NUM_LIMIT_BUTT];

DATA_INIT CmPtCalcOpsInit(void)
{
    uint16_t index;

    for (index = 0; index < PT_NUM_LIMIT_BUTT; index++) {
        g_calcOps[index].updateState = NULL;
        g_calcOps[index].updateFinish = NULL;
        g_calcOps[index].createCalculator = NULL;
        g_calcOps[index].destoryCalculator = NULL;
        g_calcOps[index].viewInitial = NULL;
        g_calcOps[index].viewRebalance = NULL;
    }

    g_calcOps[PT_NUM_LIMIT_FIXED].updateState = ViewPtEntryListUpdateNodeState;
    g_calcOps[PT_NUM_LIMIT_FIXED].updateFinish = ViewPtEntryListUpdateNodeFinish;
    g_calcOps[PT_NUM_LIMIT_FIXED].createCalculator = CreateViewCalculator;
    g_calcOps[PT_NUM_LIMIT_FIXED].destoryCalculator = DestoryViewCalculator;
    g_calcOps[PT_NUM_LIMIT_FIXED].viewInitial = ViewCalculatorInitial;
    g_calcOps[PT_NUM_LIMIT_FIXED].viewRebalance = ViewCalculatorRebalance;
    g_calcOps[PT_NUM_LIMIT_FIXED].needRebalance = ViewCalculatorNeedRebalance;
}

CalcOps *CmPtCalcOpsGet(PtNumLimitMode numLimit)
{
    if (numLimit != PT_NUM_LIMIT_FIXED) {
        return NULL;
    }

    return &g_calcOps[numLimit];
}

