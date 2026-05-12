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

#ifndef CM_PT_STORE_H
#define CM_PT_STORE_H

#include "cm_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *Storer;

typedef struct {
    Storer (*createStorer)(uint16_t maxNodeNum, uint16_t maxPtNum, uint16_t copyNum);
    void (*destoryStorer)(Storer storer);
    int32_t (*initial)(Storer storer, PtEntryList *ptList);
    int32_t (*loadcheck)(Storer storer, PtEntryList *ptList);
    int32_t (*update)(Storer storer, PtEntryList *ptList);
} StoreOps;

StoreOps *CmPtStoreOpsGet(void);

int32_t ViewStorePtEntryIsSame(PtEntry *elem1, PtEntry *elem2);

#ifdef __cplusplus
}
#endif

#endif

