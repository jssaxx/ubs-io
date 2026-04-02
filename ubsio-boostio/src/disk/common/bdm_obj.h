/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef BDM_OBJ_H
#define BDM_OBJ_H

#include "bdm_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uintptr_t objPtr;
    uint64_t chunkId;
    uint64_t offset;
    void *buf;
    uint64_t len;
    BdmIoCtx *ioCtx;
} BdmAsyncOpsReq;

typedef struct {
    int32_t (*alloc)(uintptr_t objPtr, uint64_t bucketId, uint64_t bucketOffset, uint64_t len, uint64_t *chunkId);
    int32_t (*free)(uintptr_t objPtr, uint64_t len, uint64_t chunkId);
    int32_t (*read)(uintptr_t objPtr, uint64_t chunkId, uint64_t offset, void *buf, uint64_t len);
    int32_t (*write)(uintptr_t objPtr, uint64_t chunkId, uint64_t offset, void *buf, uint64_t len);
    int32_t (*readAsync)(BdmAsyncOpsReq *req);
    int32_t (*writeAsync)(BdmAsyncOpsReq *req);
    int32_t (*allocatorReset)(uintptr_t objPtr);
    int32_t (*nextchunk)(uintptr_t objPtr, uint64_t *chunkId, uint64_t *chunkSize, uint64_t *bucketId,
        uint64_t *bucketOffset);
    int32_t (*getcap)(uintptr_t objPtr, uint64_t *totalSize, uint64_t *usedSize);
} BdmOps;

typedef void *BdmOpsInfo;

typedef struct {
    uint32_t bdmId;
    uint32_t pad;
    uint64_t totalSize;
    uint64_t minChunkSize;
    uint64_t maxChunkSize;

    BdmOps ops;
    BdmOpsInfo opsInfo;   // bdm disk item
} BdmObj;

BdmObj *BdmGetBdmObj(uint32_t bdmId);

BdmDiskState BdmGetBdmStatus(uint32_t bdmId);

typedef BdmObj *(*BdmCreateFunc)(uint32_t bdmId, uintptr_t createPara);

typedef int32_t (*BdmDestroyFunc)(BdmObj *obj);

typedef int32_t (*BdmResetFunc)(BdmObj *obj);

void BdmRegOpsWithCreate(BdmCreateFunc create);

void BdmRegOpsWithDestroy(BdmDestroyFunc destroy);

void BdmRegOpsWithReset(BdmResetFunc reset);

int32_t BdmObjInit(void);

#ifdef __cplusplus
}
#endif

#endif
