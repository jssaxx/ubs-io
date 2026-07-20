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

#ifndef BDM_ALLOCATOR_H
#define BDM_ALLOCATOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uintptr_t itemPtr;
    int32_t (*writeMeta)(uintptr_t itemPtr, uint64_t offset, void *buf, uint64_t len);
    int32_t (*readMeta)(uintptr_t itemPtr, uint64_t offset, void *buf, uint64_t len);
} BdmMetaOps;

typedef struct {
    BdmMetaOps metaOps;
    uint64_t minChunkSize;
    uint64_t maxChunkSize;
    uint64_t totalSize;
} BdmAllocatorPara;

typedef uintptr_t BdmAllocator;

int32_t BdmAllocatorGetSplitSize(uint64_t head, uint64_t chunkSize, uint64_t totalSize, uint64_t *metaSize,
                                 uint64_t *dataSize);

int32_t BdmAllocatorAllocChunk(BdmAllocator allocator, uint64_t bucketId, uint64_t bucketOffset, uint64_t chunkSize,
                               uint64_t *chunkId);

int32_t BdmAllocatorFreeChunk(BdmAllocator allocator, uint64_t chunkSize, uint64_t chunkId);

int32_t BdmAllocatorCheckChunk(BdmAllocator allocator, uint64_t chunkId, uint64_t offset, uint64_t length);

int32_t BdmAllocatorResetChunk(BdmAllocator allocator);

int32_t BdmAllocatorGetNextChunk(BdmAllocator allocator, uint64_t *chunkId, uint64_t *chunkSize, uint64_t *bucketId,
                                 uint64_t *bucketOffset);

int32_t BdmAllocatorGetCap(BdmAllocator allocator, uint64_t *totalSize, uint64_t *usedSize);

BdmAllocator BdmAllocatorCreate(BdmAllocatorPara *para, uint32_t isRestore);

int32_t BdmAllocatorDestroy(BdmAllocator allocator);

#ifdef __cplusplus
}
#endif

#endif
