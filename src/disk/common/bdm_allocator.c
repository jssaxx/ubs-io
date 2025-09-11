/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "bdm_allocator.h"
#include "bdm_common.h"
#include "bdm_core.h"

#include "ngx_rbtree.h"
#include "dlist.h"

#define BDM_FREE_LIST_NUM (1024UL)

typedef struct {
    uint32_t head : 1;
    uint32_t free : 1;
    uint32_t restore : 1;
    uint32_t length : 21;
    uint32_t resv : 8;
    uint64_t bucketId;
    uint64_t bucketOffset;
} BdmChunkMeta;

typedef struct {
    ngx_rbtree_node_t node;
    DList head;
    uint64_t index;
    uint64_t length;
} BdmChunkIndex;

typedef struct {
    DList head;
    uint64_t count;
} BdmChunkList;

typedef struct {
    BDM_RWLOCK_T lock;
    ngx_rbtree_t root;
    ngx_rbtree_node_t sentinel;
    BdmChunkList free[BDM_FREE_LIST_NUM];
    BdmMetaOps metaOps;
    uint64_t metaAddr;
    uint64_t minChunkSize;
    uint64_t maxChunkSize;
    uint64_t totalSize;
    uint64_t usedSize;
    uint64_t scanIndex;
    uint64_t chunkNum;
    BdmChunkIndex chunkList[0];
} BdmAllocatorRealize;

typedef struct {
    BdmAllocatorRealize *realize;
    ngx_rbtree_node_t *eraseNode;
    BdmChunkIndex *chunk;
    int result;
} InsertCbContext;

int32_t BdmAllocatorGetSplitSize(uint64_t head, uint64_t chunkSize, uint64_t totalSize, uint64_t *metaSize,
                                 uint64_t *dataSize)
{
    uint64_t elemSize = sizeof(BdmChunkMeta);

    if (head + chunkSize + elemSize > totalSize) {
        BDM_LOGERROR(0, "Too small, head(%llu) chunkSize(%llu), totalSize(%llu).", head, chunkSize, totalSize);
        return BDM_CODE_ERR;
    }

    uint64_t calcNum = (totalSize - head) / (elemSize + chunkSize);
    uint64_t metaSize1 = elemSize * calcNum;
    metaSize1 = ROUND_UP(metaSize1 + head, BDM_ALIGN_SIZE);

    uint64_t dataSize1 = chunkSize * calcNum;

    if (metaSize1 + dataSize1 > totalSize) {
        if (calcNum <= 1) {
            BDM_LOGERROR(0, "Too small, head(%llu) chunkSize(%llu), totalSize(%llu).", head, chunkSize, totalSize);
            return BDM_CODE_ERR;
        } else {
            calcNum--;
            metaSize1 = elemSize * calcNum;
            dataSize1 = chunkSize * calcNum;
        }
    }

    BDM_LOGINFO(0, "Get, head(%llu) meta(%llu) data(%llu) total(%llu).", head, metaSize1, dataSize1, totalSize);

    *metaSize = metaSize1 - head;
    *dataSize = dataSize1;
    return BDM_CODE_OK;
}

static int32_t BdmAllocatorUpdateMeta(BdmAllocatorRealize *realize, uint64_t bucketId, uint64_t bucketOffset,
                                      BdmChunkIndex *chunk, uint32_t isFree)
{
    BdmChunkMeta *metaAddr = (BdmChunkMeta *)realize->metaAddr;
    BdmChunkMeta *meta = &metaAddr[chunk->index];

    meta->head = 1UL;
    meta->free = isFree;
    meta->restore = 1UL;
    meta->length = (uint32_t)chunk->length;
    meta->bucketId = bucketId;
    meta->bucketOffset = bucketOffset;

    if (realize->metaOps.writeMeta != NULL) {
        uint64_t len = sizeof(BdmChunkMeta);
        uint64_t offset = chunk->index * len;
        int32_t ret = realize->metaOps.writeMeta(realize->metaOps.itemPtr, offset, (void *)(meta), len);
        if (ret != BDM_CODE_OK) {
            BDM_LOGWARN(0, "Update pri meta failed, ret(%d).", ret);
            return ret;
        }
    }

    return BDM_CODE_OK;
}

static void BdmAllocatorInsertPre(BdmChunkIndex *chunk, BdmChunkIndex *next, InsertCbContext *context)
{
    uint64_t freeIndex;
    int32_t ret;

    if (next->length == 0) {
        context->result = BDM_CODE_INVALID_PARAM;
        return;
    }
    freeIndex = MIN(next->length, BDM_FREE_LIST_NUM) - 1;
    DListDel(&next->head);
    context->realize->free[freeIndex].count--;

    chunk->length += next->length;
    if (chunk->length == 0) {
        context->result = BDM_CODE_INVALID_PARAM;
        return;
    }
    freeIndex = MIN(chunk->length, BDM_FREE_LIST_NUM) - 1;
    DListAddTail(&chunk->head, &context->realize->free[freeIndex].head);
    context->realize->free[freeIndex].count++;

    ret = BdmAllocatorUpdateMeta(context->realize, 0, 0, chunk, 1UL);
    if (ret != BDM_CODE_OK) {
        context->result = ret;
        return;
    }
    ngx_rbtree_replace(&context->realize->root, &chunk->node, &next->node);
    context->result = BDM_CODE_OK;
}

static void BdmAllocatorInsertNext(BdmChunkIndex *chunk, BdmChunkIndex *prev, InsertCbContext *context)
{
    ngx_rbtree_node_t *neighbNode = NULL;
    BdmChunkIndex *neighChunk = NULL;
    uint64_t freeIndex;
    int32_t ret;

    if (prev->length == 0) {
        context->result = BDM_CODE_INVALID_PARAM;
        return;
    }
    freeIndex = MIN(prev->length, BDM_FREE_LIST_NUM) - 1;
    DListDel(&prev->head);
    context->realize->free[freeIndex].count--;

    prev->length += chunk->length;
    neighbNode = ngx_rbtree_next(&context->realize->root, &prev->node);
    if (neighbNode != NULL) {
        neighChunk = ngx_rb_entry(neighbNode, BdmChunkIndex, node);
        if (chunk->index + chunk->length == neighChunk->index) {
            if (neighChunk->length == 0) {
                context->result = BDM_CODE_INVALID_PARAM;
                return;
            }
            freeIndex = MIN(neighChunk->length, BDM_FREE_LIST_NUM) - 1;
            DListDel(&neighChunk->head);
            context->realize->free[freeIndex].count--;

            prev->length += neighChunk->length;
            if (prev->length == 0) {
                context->result = BDM_CODE_INVALID_PARAM;
                return;
            }
            freeIndex = MIN(prev->length, BDM_FREE_LIST_NUM) - 1;
            DListAddTail(&prev->head, &context->realize->free[freeIndex].head);
            context->realize->free[freeIndex].count++;

            ret = BdmAllocatorUpdateMeta(context->realize, 0, 0, prev, 1UL);
            if (ret != BDM_CODE_OK) {
                context->result = ret;
                return;
            }
            context->eraseNode = &neighChunk->node;
            context->result = BDM_CODE_OK;
            return;
        }
    }

    if (prev->length == 0) {
        context->result = BDM_CODE_INVALID_PARAM;
        return;
    }
    freeIndex = MIN(prev->length, BDM_FREE_LIST_NUM) - 1;
    DListAddTail(&prev->head, &context->realize->free[freeIndex].head);
    context->realize->free[freeIndex].count++;

    ret = BdmAllocatorUpdateMeta(context->realize, 0, 0, chunk, 1UL);
    if (ret != BDM_CODE_OK) {
        context->result = ret;
        return;
    }
    context->result = BDM_CODE_OK;
}


static int BdmAllocatorInsertCallback(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel,
                                      void *context)
{
    ngx_rbtree_node_t **p;
    InsertCbContext *cbContext = (InsertCbContext *)context;
    if (node == NULL) {
        BDM_LOGERROR(0, "Invalid para, node is null.");
        cbContext->result = BDM_CODE_ERR;
        return BDM_CODE_ERR;
    }
    BdmChunkIndex *nodePos = ngx_rb_entry(node, BdmChunkIndex, node);
    if (temp != NULL) {
        while (1) {
            BdmChunkIndex *tempPos = ngx_rb_entry(temp, BdmChunkIndex, node);
            if (nodePos->index == tempPos->index + tempPos->length) {
                BdmAllocatorInsertNext(nodePos, tempPos, cbContext);
                return 1;
            }
            if (nodePos->index + nodePos->length == tempPos->index) {
                BdmAllocatorInsertPre(nodePos, tempPos, cbContext);
                return 1;
            }

            if (nodePos->index + nodePos->length < tempPos->index) {
                p = &temp->left;
            } else if (nodePos->index > tempPos->index + tempPos->length) {
                p = &temp->right;
            } else {
                BDM_LOGERROR(0, "Areas overlapped failed, start(%llu) length(%llu).", nodePos->index, nodePos->length);
                cbContext->result = BDM_CODE_ERR;
                return BDM_CODE_ERR;
            }

            if (*p == sentinel) {
                break;
            }

            temp = *p;
        }
        *p = node;
        node->parent = temp;
        node->left = sentinel;
        node->right = sentinel;
        ngx_rbt_red(node);
    }

    uint64_t freeIndex = MIN(cbContext->chunk->length, BDM_FREE_LIST_NUM) - 1;
    DListAddTail(&cbContext->chunk->head, &cbContext->realize->free[freeIndex].head);
    cbContext->realize->free[freeIndex].count++;

    return 0;
}

static int32_t BdmAllocatorInsert(BdmAllocatorRealize *realize, BdmChunkIndex *chunk)
{
    ngx_rbtree_t *root = &realize->root;

    InsertCbContext context = {.realize = realize, .eraseNode = NULL, .chunk = chunk, .result = BDM_CODE_OK};
    ngx_rbtree_insert(root, &chunk->node, &context);
    if (context.result != BDM_CODE_OK) {
        return context.result;
    }

    if (context.eraseNode != NULL) {
        ngx_rbtree_delete(root, context.eraseNode);
    }

    int32_t ret = BdmAllocatorUpdateMeta(realize, 0, 0, chunk, 1UL);
    if (ret != BDM_CODE_OK) {
        return ret;
    }

    return BDM_CODE_OK;
}

static int32_t BdmAllocatorRestoreInsert(BdmAllocatorRealize *realize, BdmChunkIndex *chunk)
{
    ngx_rbtree_t *root = &realize->root;
    if (root == NULL) {
        return BDM_CODE_INVALID_PARAM;
    }

    InsertCbContext context = {.realize = realize, .eraseNode = NULL, .chunk = chunk, .result = BDM_CODE_OK};
    ngx_rbtree_insert(root, &chunk->node, &context);
    if (context.result != BDM_CODE_OK) {
        return context.result;
    }

    if (context.eraseNode != NULL) {
        ngx_rbtree_delete(root, context.eraseNode);
    }

    return BDM_CODE_OK;
}

static int32_t BdmAllocatorRemoveSameLen(BdmChunkIndex *chunk, BdmAllocatorRealize *realize, uint64_t bucketId,
                                         uint64_t bucketOffset, uint64_t freeIndex, uint64_t *index)
{
    ngx_rbtree_t *root = &realize->root;
    *index = chunk->index;
    DListDel(&chunk->head);
    realize->free[freeIndex].count--;
    int32_t ret = BdmAllocatorUpdateMeta(realize, bucketId, bucketOffset, chunk, 0UL);
    if (ret != BDM_CODE_OK) {
        return ret;
    }
    ngx_rbtree_delete(root, &chunk->node);
    return BDM_CODE_OK;
}

static int32_t BdmAllocatorRemoveDiffLen(BdmChunkIndex *chunk, BdmAllocatorRealize *realize, uint64_t bucketId,
                                         uint64_t bucketOffset, uint64_t length, uint64_t *index)
{
    ngx_rbtree_t *root = &realize->root;
    BdmChunkIndex *neighChunk = (BdmChunkIndex *)(chunk + length);
    neighChunk->index = chunk->index + length;
    neighChunk->length = chunk->length - length;
    *index = chunk->index;

    uint64_t freeIdx = MIN(neighChunk->length, BDM_FREE_LIST_NUM) - 1;
    DListAddTail(&neighChunk->head, &realize->free[freeIdx].head);
    realize->free[freeIdx].count++;
    int32_t ret = BdmAllocatorUpdateMeta(realize, 0, 0, neighChunk, 1UL);
    if (ret != BDM_CODE_OK) {
        return ret;
    }

    freeIdx = MIN(chunk->length, BDM_FREE_LIST_NUM) - 1;
    DListDel(&chunk->head);
    realize->free[freeIdx].count--;
    chunk->length -= neighChunk->length;
    ret = BdmAllocatorUpdateMeta(realize, bucketId, bucketOffset, chunk, 0UL);
    if (ret != BDM_CODE_OK) {
        return ret;
    }
    ngx_rbtree_replace(&realize->root, &neighChunk->node, &chunk->node);
    return BDM_CODE_OK;
}

static int32_t BdmAllocatorRemove(BdmAllocatorRealize *realize, uint64_t bucketId, uint64_t bucketOffset,
                                  uint64_t *index, uint64_t chunkSize)
{
    BdmChunkIndex *chunk = NULL;
    uint64_t freeIndex;
    int32_t ret;

    if (realize->minChunkSize == 0) {
        return BDM_CODE_ERR;
    }
    uint64_t length = chunkSize / realize->minChunkSize;
    if (length == 0) {
        return BDM_CODE_INVALID_PARAM;
    }
    freeIndex = MIN(length, BDM_FREE_LIST_NUM) - 1;

    for (; freeIndex < BDM_FREE_LIST_NUM; freeIndex++) {
        if (D_LIST_EMPTY(&realize->free[freeIndex].head)) {
            continue;
        }
        DList *areaNode = realize->free[freeIndex].head.next;
        while (areaNode != &realize->free[freeIndex].head) {
            chunk = (BdmChunkIndex *)D_LIST_ENTRY(areaNode, BdmChunkIndex, head);
            if (chunk->length == length) {
                ret = BdmAllocatorRemoveSameLen(chunk, realize, bucketId, bucketOffset, freeIndex, index);
                return ret;
            } else if (chunk->length > length) {
                ret = BdmAllocatorRemoveDiffLen(chunk, realize, bucketId, bucketOffset, length, index);
                return ret;
            } else {
                areaNode = areaNode->next;
                continue;
            }
        }
    }

    return BDM_CODE_ERR;
}

int32_t BdmAllocatorAllocChunk(BdmAllocator allocator, uint64_t bucketId, uint64_t bucketOffset, uint64_t chunkSize,
                               uint64_t *chunkId)
{
    BdmAllocatorRealize *realize = (BdmAllocatorRealize *)allocator;

    BDM_RWLOCK_WRLOCK(&realize->lock);
    int32_t ret = BdmAllocatorRemove(realize, bucketId, bucketOffset, chunkId, chunkSize);
    if (ret != BDM_CODE_OK) {
        BDM_RWLOCK_UNLOCK(&realize->lock);
        BDM_LOGWARN(0, "Alloc chunk failed, chunk size(%llu), used cap(%llu), total cap(%llu).",
                    chunkSize, realize->usedSize, realize->totalSize);
        return ret;
    }
    realize->usedSize += chunkSize;
    BDM_RWLOCK_UNLOCK(&realize->lock);
    return BDM_CODE_OK;
}

int32_t BdmAllocatorFreeChunk(BdmAllocator allocator, uint64_t chunkSize, uint64_t chunkId)
{
    BdmAllocatorRealize *realize = (BdmAllocatorRealize *)allocator;
    if (realize == NULL) {
        return BDM_CODE_INVALID_PARAM;
    }

    if (chunkId >= realize->chunkNum) {
        BDM_LOGERROR(0, "Invalid chunk id(%llu), chunk num(%llu).", chunkId, realize->chunkNum);
        return BDM_CODE_ERR;
    }

    BDM_RWLOCK_WRLOCK(&realize->lock);
    BdmChunkIndex *chunk = &realize->chunkList[chunkId];
    if (chunkSize != (chunk->length * realize->minChunkSize)) {
        BDM_RWLOCK_UNLOCK(&realize->lock);
        BDM_LOGWARN(0, "Invalid chunk id(%llu), length(%llu) real(%llu).", chunkId, chunkSize,
                    chunk->length * realize->minChunkSize);
        return BDM_CODE_INVALID_PARAM;
    }
    int32_t ret = BdmAllocatorInsert(realize, chunk);
    if (ret != BDM_CODE_OK) {
        BDM_RWLOCK_UNLOCK(&realize->lock);
        BDM_LOGWARN(0, "Free chunk failed, chunk size(%llu).", chunkSize);
        return ret;
    }
    realize->usedSize -= chunkSize;
    BDM_RWLOCK_UNLOCK(&realize->lock);
    return BDM_CODE_OK;
}

int32_t BdmAllocatorCheckChunk(BdmAllocator allocator, uint64_t chunkId, uint64_t offset, uint64_t length)
{
    BdmAllocatorRealize *realize = (BdmAllocatorRealize *)allocator;
    if (chunkId >= realize->chunkNum) {
        BDM_LOGERROR(0, "Invalid chunk id(%llu), chunk num(%llu).", chunkId, realize->chunkNum);
        return BDM_CODE_INVALID_CHUNK_ID;
    }

    BdmChunkMeta *metaAddr = (BdmChunkMeta *)realize->metaAddr;
    BdmChunkMeta *meta = &metaAddr[chunkId];
    if (meta->head != 1UL || meta->free == 1UL) {
        BDM_LOGERROR(0, "Invalid chunk id(%llu), chunk num(%llu).", chunkId, realize->chunkNum);
        return BDM_CODE_INVALID_CHUNK_ID;
    }

    if ((offset + length) > (meta->length * realize->minChunkSize)) {
        BDM_LOGERROR(0, "Invalid chunk id(%llu), offset(%llu) length(%llu).", chunkId, offset, length);
        return BDM_CODE_CROSS_BOUND;
    }

    return BDM_CODE_OK;
}

int32_t BdmAllocatorResetChunk(BdmAllocator allocator)
{
    BdmAllocatorRealize *realize = (BdmAllocatorRealize *)allocator;
    realize->scanIndex = 0UL;
    return BDM_CODE_OK;
}

int32_t BdmAllocatorGetNextChunk(BdmAllocator allocator, uint64_t *chunkId, uint64_t *chunkSize, uint64_t *bucketId,
                                 uint64_t *bucketOffset)
{
    BdmAllocatorRealize *realize = (BdmAllocatorRealize *)allocator;
    BdmChunkMeta *chunkMeta = (BdmChunkMeta *)realize->metaAddr + realize->scanIndex;
    uint64_t chunkIndex = realize->scanIndex;
    while (chunkIndex < realize->chunkNum) {
        if (chunkMeta->head != 1UL) {
            BDM_LOGERROR(0, "Impossible, min chunk(%llu) max chunk(%llu) total size(%llu).", realize->minChunkSize,
                         realize->maxChunkSize, realize->totalSize);
            return BDM_CODE_ERR;
        }
        if (chunkMeta->free == 0UL) {
            *chunkId = chunkIndex;
            *chunkSize = chunkMeta->length * realize->minChunkSize;
            *bucketId = chunkMeta->bucketId;
            *bucketOffset = chunkMeta->bucketOffset;
            realize->scanIndex = chunkIndex + chunkMeta->length;
            return BDM_CODE_OK;
        }
        chunkIndex += chunkMeta->length;
        chunkMeta = chunkMeta + chunkMeta->length;
        continue;
    }

    BDM_LOGINFO(0, "Allocator restore, min chunk(%llu) max chunk(%llu) total size(%llu).", realize->minChunkSize,
                realize->maxChunkSize, realize->totalSize);
    realize->scanIndex = realize->chunkNum;
    return BDM_CODE_SCAN_OFF;
}

int32_t BdmAllocatorGetCap(BdmAllocator allocator, uint64_t *totalSize, uint64_t *usedSize)
{
    BdmAllocatorRealize *realize = (BdmAllocatorRealize *)allocator;
    *totalSize = realize->totalSize;
    *usedSize = realize->usedSize;
    return BDM_CODE_OK;
}

static int32_t BdmAllocatorCreateImpl(BdmAllocatorRealize *realize)
{
    BdmChunkIndex *chunk = &realize->chunkList[0];
    chunk->index = 0UL;
    chunk->length = realize->chunkNum;
    int32_t ret = BdmAllocatorInsert(realize, chunk);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Insert failed, ret(%d) chunk num(%llu).", ret, realize->chunkNum);
        return BDM_CODE_ERR;
    }

    BDM_LOGINFO(0, "Allocator create, min chunk(%llu) max chunk(%llu) total size(%llu).", realize->minChunkSize,
                realize->maxChunkSize, realize->totalSize);

    return BDM_CODE_OK;
}

static int32_t BdmAllocatorRestoreImpl(BdmAllocatorRealize *realize)
{
    if (realize->metaOps.readMeta == NULL) {
        BDM_LOGWARN(0, "Bdm meta ops not register.");
        return BDM_CODE_ERR;
    }

    uint64_t len = sizeof(BdmChunkMeta) * realize->chunkNum;
    int32_t ret = realize->metaOps.readMeta(realize->metaOps.itemPtr, 0, (void *)realize->metaAddr, len);
    if (ret != BDM_CODE_OK) {
        BDM_LOGWARN(0, "Bdm read meta failed, ret(%d).", ret);
        return ret;
    }

    BdmChunkMeta *nextChunkMeta = (BdmChunkMeta *)realize->metaAddr;
    BdmChunkMeta *chunkMeta = NULL;
    uint64_t chunkIndex = 0;
    uint64_t length;
    while (chunkIndex < realize->chunkNum) {
        chunkMeta = nextChunkMeta;
        if (chunkMeta->head != 1UL) {
            BDM_LOGERROR(0, "Impossible, min chunk(%llu) max chunk(%llu) total size(%llu).", realize->minChunkSize,
                         realize->maxChunkSize, realize->totalSize);
            return BDM_CODE_ERR;
        }
        if (chunkMeta->free == 1UL || chunkMeta->restore == 0UL) {
            nextChunkMeta = chunkMeta + chunkMeta->length;
            length = chunkMeta->length;
            BdmChunkIndex *chunk = &realize->chunkList[chunkIndex];
            chunk->index = chunkIndex;
            chunk->length = length;
            ret = BdmAllocatorRestoreInsert(realize, chunk);
            if (ret != BDM_CODE_OK) {
                BDM_LOGERROR(0, "Insert failed, ret(%d) chunk num(%llu).", ret, realize->chunkNum);
                return BDM_CODE_ERR;
            }

            chunkIndex += length;
            continue;
        } else {
            nextChunkMeta = chunkMeta + chunkMeta->length;
            BdmChunkIndex *chunk = &realize->chunkList[chunkIndex];
            chunk->index = chunkIndex;
            chunk->length = chunkMeta->length;
            chunkIndex += chunkMeta->length;
            realize->usedSize += realize->minChunkSize * chunkMeta->length;
            continue;
        }
    }

    BDM_LOGINFO(0, "Allocator restore, min chunk(%llu) max chunk(%llu) total size(%llu) used size(%llu).",
                realize->minChunkSize, realize->maxChunkSize, realize->totalSize, realize->usedSize);

    return BDM_CODE_OK;
}

BdmAllocator BdmAllocatorCreate(BdmAllocatorPara *para, uint32_t isRestore)
{
    if (para->minChunkSize == 0) {
        BDM_LOGERROR(0, "Alloc failed, min chunk size error.");
        return 0L;
    }
    uint64_t chunkNum = para->totalSize / para->minChunkSize;
    BdmAllocatorRealize *realize =
            (BdmAllocatorRealize *)malloc(sizeof(BdmAllocatorRealize) + sizeof(BdmChunkIndex) * chunkNum);
    if (realize == NULL) {
        BDM_LOGERROR(0, "Malloc failed, chunk num(%llu).", chunkNum);
        return 0L;
    }
    BDM_RWLOCK_INIT(&realize->lock, NULL);
    ngx_rbtree_init(&realize->root, &realize->sentinel, BdmAllocatorInsertCallback);

    uint32_t index;
    for (index = 0; index < BDM_FREE_LIST_NUM; index++) {
        D_INIT_LIST_HEAD(&realize->free[index].head);
        realize->free[index].count = 0UL;
    }

    realize->metaOps = para->metaOps;
    realize->metaAddr = (uint64_t)malloc(sizeof(BdmChunkMeta) * chunkNum);
    if (realize->metaAddr == 0UL) {
        BDM_LOGERROR(0, "Malloc failed, chunk num(%llu).", chunkNum);
        free(realize);
        return 0L;
    }
    realize->minChunkSize = para->minChunkSize;
    realize->maxChunkSize = para->maxChunkSize;
    realize->totalSize = para->totalSize;
    realize->usedSize = 0UL;
    realize->scanIndex = 0UL;
    realize->chunkNum = chunkNum;

    int32_t ret;
    if (isRestore == 0UL) {
        ret = BdmAllocatorCreateImpl(realize);
    } else {
        ret = BdmAllocatorRestoreImpl(realize);
    }

    if (ret != BDM_CODE_OK) {
        free((void *)realize->metaAddr);
        realize->metaAddr = 0;
        free(realize);
        realize = NULL;
        return 0L;
    }

    return (BdmAllocator)realize;
}

int32_t BdmAllocatorDestroy(BdmAllocator allocator)
{
    BdmAllocatorRealize *realize = (BdmAllocatorRealize *)allocator;
    BDM_LOGINFO(0, "Allocator destroy, min chunk(%llu) max chunk(%llu) total size(%llu).", realize->minChunkSize,
                realize->maxChunkSize, realize->totalSize);
    BDM_RWLOCK_DESTROY(&realize->lock);
    free((void *)realize->metaAddr);
    realize->metaAddr = 0;
    free(realize);
    realize = NULL;
    return BDM_CODE_OK;
}
