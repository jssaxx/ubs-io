/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
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
    void (*destroyStorer)(Storer storer);
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
