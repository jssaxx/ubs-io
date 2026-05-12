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

#include "cm_pt_store.h"
#include "cm_zkadapter.h"
#include "cm_log.h"
#include "dlist.h"
#include "securec.h"

#define CM_PT_STORE_EXPAND_NUM (4)

typedef struct {
    DList node;
    PtEntry ptEntry;
} NodeElem;

typedef struct {
    uint16_t maxNodeNum;
    uint16_t maxPtNum;
    PtEntryList *ptList;
    DList *nodeList;
    DList *nodeListBak;
    DList free;
    NodeElem *cache;
} StoreCore;

static int32_t CmServerZkRecordSubPtEntryList(uint16_t poolId, uint16_t nodeId, PtEntryList *ptEntryList)
{
    UNREFERENCE_PARAM(poolId);
    UNREFERENCE_PARAM(nodeId);
    UNREFERENCE_PARAM(ptEntryList);
    return CM_OK;
}

static int32_t CmServerZkGetSubPtEntryList(uint16_t poolId, uint16_t nodeId, PtEntryList *ptEntryList)
{
    return CM_NOT_EXIST;
}

static void ViewDestoryStorer(Storer storer)
{
    StoreCore *store = (StoreCore *)storer;

    if (store->ptList != NULL) {
        free(store->ptList);
        store->ptList = NULL;
    }
    if (store->nodeList != NULL) {
        free(store->nodeList);
        store->nodeList = NULL;
    }
    if (store->nodeListBak != NULL) {
        free(store->nodeListBak);
        store->nodeListBak = NULL;
    }
    if (store->cache != NULL) {
        free(store->cache);
        store->cache = NULL;
    }
    free(store);
}

static Storer ViewCreateStorer(uint16_t maxNodeNum, uint16_t maxPtNum, uint16_t maxCopyNum)
{
    StoreCore *store = (StoreCore *)malloc(sizeof(StoreCore));
    if (store == NULL) {
        CM_LOGERROR("Malloc store cache failed.");
        return NULL;
    }

    store->maxNodeNum = maxNodeNum;
    store->maxPtNum = maxPtNum;

    store->ptList = NULL;
    store->nodeList = NULL;
    store->cache = NULL;

    store->nodeList = (DList *)malloc(sizeof(DList) * maxNodeNum);
    store->nodeListBak = (DList *)malloc(sizeof(DList) * maxNodeNum);
    store->ptList = (PtEntryList *)malloc(sizeof(PtEntry) * maxPtNum + sizeof(PtEntryList));

    uint16_t elemNum = maxPtNum * maxCopyNum * CM_PT_STORE_EXPAND_NUM;
    store->cache = (NodeElem *)malloc(sizeof(NodeElem) * elemNum);
    if (store->nodeList == NULL || store->nodeListBak == NULL || store->ptList == NULL || store->cache == NULL) {
        CM_LOGERROR("Malloc cacheList buff failed.");
        ViewDestoryStorer((Storer)store);
        return NULL;
    }

    uint16_t nodeId, index;
    for (nodeId = 0; nodeId < maxNodeNum; nodeId++) {
        D_INIT_LIST_HEAD(&store->nodeList[nodeId]);
        D_INIT_LIST_HEAD(&store->nodeListBak[nodeId]);
    }
    D_INIT_LIST_HEAD(&store->free);
    for (index = 0; index < elemNum; index++) {
        DListAddTail(&store->cache[index].node, &store->free);
    }

    return (Storer)store;
}

static NodeElem *ViewStoreGetNodeElem(StoreCore *store)
{
    if (D_LIST_EMPTY(&store->free)) {
        return NULL;
    }

    DList *pos = store->free.next;
    DListDel(pos);
    NodeElem *elem = D_LIST_ENTRY(pos, NodeElem, node);
    return elem;
}

static int32_t ViewStoreFillNodeList(StoreCore *store, DList *nodeList, PtEntryList *ptList)
{
    uint16_t ptId, copyIndex, nodeId;

    for (ptId = 0; ptId < ptList->ptNum; ptId++) {
        PtEntry *ptEntry = &ptList->ptEntryList[ptId];
        for (copyIndex = 0; copyIndex < ptEntry->copyNum; copyIndex++) {
            if (ptEntry->copyList[copyIndex].state == PT_COPY_STATE_INIT ||
                ptEntry->copyList[copyIndex].state == PT_COPY_STATE_OUT) {
                continue;
            }
            nodeId = ptEntry->copyList[copyIndex].nodeId;
            NodeElem *elem = ViewStoreGetNodeElem(store);
            if (elem == NULL) {
                CM_LOGERROR("Impossible.");
                return CM_ERR;
            }
            elem->ptEntry = *ptEntry;
            DListAddTail(&elem->node, &nodeList[nodeId]);
        }
    }
    return CM_OK;
}

static void ViewStoreClearNodeList(StoreCore *store)
{
    uint16_t nodeId;

    for (nodeId = 0; nodeId < store->maxNodeNum; nodeId++) {
        DListSpliceInit(&store->nodeList[nodeId], &store->free);
        D_INIT_LIST_HEAD(&store->nodeList[nodeId]);
    }
    for (nodeId = 0; nodeId < store->maxNodeNum; nodeId++) {
        DListSpliceInit(&store->nodeListBak[nodeId], &store->free);
        D_INIT_LIST_HEAD(&store->nodeListBak[nodeId]);
    }
    return;
}

static int32_t ViewStoreRecordSubPtView(StoreCore *store, PtEntryList *ptList)
{
    NodeElem *elem;
    DList *pos, *next;
    int32_t ret;

    store->ptList->poolId = ptList->poolId;
    store->ptList->ptNum = 0;
    store->ptList->maxCopyNum = ptList->maxCopyNum;
    store->ptList->minCopyNum = ptList->minCopyNum;
    store->ptList->globalVersion = ptList->globalVersion;

    uint16_t nodeId;

    for (nodeId = 0; nodeId < store->maxNodeNum; nodeId++) {
        if (D_LIST_EMPTY(&store->nodeList[nodeId])) {
            continue;
        }
        uint16_t ptNum = 0;
        D_LIST_FOR_EACH_SAFE(pos, next, &store->nodeList[nodeId])
        {
            elem = D_LIST_ENTRY(pos, NodeElem, node);
            store->ptList->ptEntryList[ptNum] = elem->ptEntry;
            ptNum++;
        }
        store->ptList->ptNum = ptNum;
        ret = CmServerZkRecordSubPtEntryList(ptList->poolId, nodeId, store->ptList);
        if (ret != CM_OK) {
            CM_LOGERROR("Record sub ptEntryList failed, ret(%d) nodeId(%d) poolId(%u).", ret, nodeId, ptList->poolId);
            return ret;
        }
    }

    return CM_OK;
}

static int32_t ViewStoreInitial(Storer storer, PtEntryList *ptList)
{
    StoreCore *store = (StoreCore *)storer;
    int32_t ret;

    ret = CmServerZkRecordPtEntryList(ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Record ptEntryList failed, poolId(%u) ret(%d).", ptList->poolId, ret);
        return ret;
    }

    ret = ViewStoreFillNodeList(store, store->nodeList, ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Fill nodeList failed, ret(%d) poolId(%u).", ret, ptList->poolId);
        ViewStoreClearNodeList(store);
        return ret;
    }

    ret = ViewStoreRecordSubPtView(store, ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Record sub ptview failed, ret(%d) poolId(%u).", ret, ptList->poolId);
        ViewStoreClearNodeList(store);
        return ret;
    }

    size_t len = sizeof(PtEntryList) + sizeof(PtEntry) * ptList->ptNum;
    ret = memcpy_s(store->ptList, len, ptList, len);
    if (ret != 0) {
        CM_LOGERROR("Memcpy_s failed, ret(%d) len(%u).", ret, len);
        ViewStoreClearNodeList(store);
        return CM_ERR;
    }

    ViewStoreClearNodeList(store);
    return CM_OK;
}

int32_t ViewStorePtEntryIsSame(PtEntry *elem1, PtEntry *elem2)
{
    if (elem1->ptId != elem2->ptId || elem1->state != elem2->state || elem1->birthVersion != elem2->birthVersion ||
        elem1->masterNodeId != elem2->masterNodeId || elem1->masterDiskId != elem2->masterDiskId ||
        elem1->copyNum != elem2->copyNum) {
        return FALSE;
    }

    uint16_t index;
    for (index = 0; index < elem1->copyNum; index++) {
        if (elem1->copyList[index].state != elem2->copyList[index].state ||
            elem1->copyList[index].nodeId != elem2->copyList[index].nodeId ||
            elem1->copyList[index].diskId != elem2->copyList[index].diskId) {
            return FALSE;
        }
    }
    return TRUE;
}

static int32_t ViewStoreCheckSubPtView1(PtEntryList *ptList, DList *head)
{
    NodeElem *elem;
    DList *pos, *next;

    int32_t isUpdate = FALSE;

    uint16_t ptNum = 0;
    D_LIST_FOR_EACH_SAFE(pos, next, head)
    {
        ptNum++;
    }

    if (ptNum != ptList->ptNum) {
        isUpdate = TRUE;
    }

    uint16_t index = 0;
    D_LIST_FOR_EACH_SAFE(pos, next, head)
    {
        elem = D_LIST_ENTRY(pos, NodeElem, node);
        if (index >= ptList->ptNum) {
            ptList->ptEntryList[index] = elem->ptEntry;
            index++;
            continue;
        }
        if (ViewStorePtEntryIsSame(&ptList->ptEntryList[index], &elem->ptEntry) == FALSE) {
            ptList->ptEntryList[index] = elem->ptEntry;
            index++;
            isUpdate = TRUE;
            continue;
        }
        index++;
    }
    ptList->ptNum = index;
    return isUpdate;
}

static int32_t ViewStoreCheckSubPtView(StoreCore *store, PtEntryList *ptList)
{
    uint16_t nodeId;
    int32_t ret;

    for (nodeId = 0; nodeId < store->maxNodeNum; nodeId++) {
        store->ptList->ptNum = store->maxPtNum;
        ret = CmServerZkGetSubPtEntryList(ptList->poolId, nodeId, store->ptList);
        if (ret != CM_OK && ret != CM_NOT_EXIST) {
            CM_LOGERROR("Get sub ptEntryList failed, ret(%d) nodeId(%d) poolId(%u).", ret, nodeId, ptList->poolId);
            return ret;
        }
        if (ret == CM_NOT_EXIST) {
            store->ptList->ptNum = 0;
        }
        if (D_LIST_EMPTY(&store->nodeList[nodeId]) && store->ptList->ptNum == 0) {
            continue;
        }
        int32_t isUpdate = ViewStoreCheckSubPtView1(store->ptList, &store->nodeList[nodeId]);
        if (isUpdate == FALSE) {
            continue;
        }
        store->ptList->poolId = ptList->poolId;
        store->ptList->maxCopyNum = ptList->maxCopyNum;
        store->ptList->minCopyNum = ptList->minCopyNum;
        store->ptList->globalVersion = ptList->globalVersion;
        ret = CmServerZkRecordSubPtEntryList(ptList->poolId, nodeId, store->ptList);
        if (ret != CM_OK) {
            CM_LOGERROR("Record sub ptEntryList failed, ret(%d) nodeId(%d) poolId(%u).", ret, nodeId, ptList->poolId);
            return ret;
        }
    }

    return CM_OK;
}

static int32_t ViewStoreLoadCheck(Storer storer, PtEntryList *ptList)
{
    StoreCore *store = (StoreCore *)storer;
    int32_t ret;

    ret = ViewStoreFillNodeList(store, store->nodeList, ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Fill nodeList failed, ret(%d) poolId(%u).", ret, ptList->poolId);
        ViewStoreClearNodeList(store);
        return ret;
    }

    ret = ViewStoreCheckSubPtView(store, ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Check sub ptview failed, ret(%d) poolId(%u).", ret, ptList->poolId);
        ViewStoreClearNodeList(store);
        return ret;
    }

    size_t len = sizeof(PtEntryList) + sizeof(PtEntry) * ptList->ptNum;
    ret = memcpy_s(store->ptList, len, ptList, len);
    if (ret != 0) {
        CM_LOGERROR("Memcpy_s failed, ret(%d) len(%u).", ret, len);
        ViewStoreClearNodeList(store);
        return CM_ERR;
    }

    ViewStoreClearNodeList(store);
    return CM_OK;
}

static int32_t ViewStoreUpdateSubPtView1(PtEntryList *ptList, DList *current, DList *last)
{
    NodeElem *elem1, *elem2;
    DList *pos1, *pos2;

    pos1 = current->next;
    pos2 = last->next;

    int32_t isUpdate = FALSE;

    uint16_t ptNum = 0;
    while (pos1 != current) {
        if (pos2 != last) {
            elem1 = D_LIST_ENTRY(pos1, NodeElem, node);
            elem2 = D_LIST_ENTRY(pos2, NodeElem, node);
            if (ViewStorePtEntryIsSame(&elem1->ptEntry, &elem2->ptEntry) == FALSE) {
                isUpdate = TRUE;
            }
            ptList->ptEntryList[ptNum] = elem1->ptEntry;
            ptNum++;
            pos1 = pos1->next;
            pos2 = pos2->next;
        } else {
            elem1 = D_LIST_ENTRY(pos1, NodeElem, node);
            ptList->ptEntryList[ptNum] = elem1->ptEntry;
            ptNum++;
            pos1 = pos1->next;
        }
    };

    ptList->ptNum = ptNum;
    return isUpdate;
}

static int32_t ViewStoreUpdateSubPtView(StoreCore *store, PtEntryList *ptList)
{
    uint16_t nodeId;
    int32_t ret;

    store->ptList->poolId = ptList->poolId;
    store->ptList->ptNum = 0;
    store->ptList->maxCopyNum = ptList->maxCopyNum;
    store->ptList->minCopyNum = ptList->minCopyNum;
    store->ptList->globalVersion = ptList->globalVersion;

    for (nodeId = 0; nodeId < store->maxNodeNum; nodeId++) {
        int32_t isUpdate =
            ViewStoreUpdateSubPtView1(store->ptList, &store->nodeList[nodeId], &store->nodeListBak[nodeId]);
        if (isUpdate == FALSE) {
            continue;
        }

        ret = CmServerZkRecordSubPtEntryList(ptList->poolId, nodeId, store->ptList);
        if (ret != CM_OK) {
            CM_LOGERROR("Record sub ptEntryList failed, ret(%d) nodeId(%d) poolId(%u).", ret, nodeId, ptList->poolId);
            return ret;
        }
    }

    return CM_OK;
}

static int32_t ViewStoreIsNeedUpdate(PtEntryList *preList, PtEntryList *nexList)
{
    if (preList->globalVersion != nexList->globalVersion || preList->ptNum != nexList->ptNum) {
        return CM_OK;
    }

    uint16_t ptId;
    for (ptId = 0; ptId < preList->ptNum; ptId++) {
        if (ViewStorePtEntryIsSame(&preList->ptEntryList[ptId], &nexList->ptEntryList[ptId]) == FALSE) {
            return CM_OK;
        }
    }
    return CM_ERR;
}

static int32_t ViewStoreUpdate(Storer storer, PtEntryList *ptList)
{
    StoreCore *store = (StoreCore *)storer;
    int32_t ret;

    ret = ViewStoreIsNeedUpdate(ptList, store->ptList);
    if (ret != CM_OK) {
        CM_LOGWARN("No need update ptEntryList, poolId(%u).", ptList->poolId);
        return CM_OK;
    }

    ret = CmServerZkRecordPtEntryList(ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Record ptEntryList failed, poolId(%u) ret(%d).", ptList->poolId, ret);
        return ret;
    }

    ret = ViewStoreFillNodeList(store, store->nodeList, ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Fill nodeList failed, ret(%d) poolId(%u).", ret, ptList->poolId);
        ViewStoreClearNodeList(store);
        return ret;
    }

    ret = ViewStoreFillNodeList(store, store->nodeListBak, store->ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Fill nodeList failed, ret(%d) poolId(%u).", ret, ptList->poolId);
        ViewStoreClearNodeList(store);
        return ret;
    }

    ret = ViewStoreUpdateSubPtView(store, ptList);
    if (ret != CM_OK) {
        CM_LOGERROR("Update sub ptview failed, ret(%d) poolId(%u).", ret, ptList->poolId);
        ViewStoreClearNodeList(store);
        return ret;
    }

    size_t len = sizeof(PtEntryList) + sizeof(PtEntry) * ptList->ptNum;
    ret = memcpy_s(store->ptList, len, ptList, len);
    if (ret != 0) {
        CM_LOGERROR("Memcpy_s failed, ret(%d) len(%u).", ret, len);
        ViewStoreClearNodeList(store);
        return CM_ERR;
    }

    ViewStoreClearNodeList(store);
    return CM_OK;
}

static StoreOps g_storeOps = {
    .createStorer = ViewCreateStorer,
    .destoryStorer = ViewDestoryStorer,
    .initial = ViewStoreInitial,
    .loadcheck = ViewStoreLoadCheck,
    .update = ViewStoreUpdate,
};

StoreOps *CmPtStoreOpsGet(void)
{
    return &g_storeOps;
}

