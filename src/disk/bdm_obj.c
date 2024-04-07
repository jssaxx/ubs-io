/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "bdm_obj.h"
#include "bdm_common.h"
#include "bdm_core.h"

typedef struct {
    bool used;
    BdmObj *obj;
} BdmElem;

typedef struct {
    BDM_SPINLOCK_T lock;
    uint32_t index;
    uint32_t num;
    BdmElem list[BDM_MAX_NUM];
} BdmObjMgr;

static BdmObjMgr g_bdmObj = { 0 };

typedef struct {
    BdmCreateFunc create;
    BdmDestoryFunc destory;
} BdmOpsEx;

static BdmOpsEx g_bdmOpsEx = { 0 };

void BdmRegOpsWithCreate(BdmCreateFunc create)
{
    BDM_LOGINFO(0, "Reg create ops succeed.");
    g_bdmOpsEx.create = create;
    return;
}

void BdmRegOpsWithDestory(BdmDestoryFunc destory)
{
    BDM_LOGINFO(0, "Reg destory ops succeed.");
    g_bdmOpsEx.destory = destory;
    return;
}

uint32_t BdmObjAllocBdmId(uint32_t bdmId)
{
    uint32_t i;

    BDM_SPIN_LOCK(&g_bdmObj.lock);
    if (g_bdmObj.num == BDM_MAX_NUM) {
        BDM_SPIN_UNLOCK(&g_bdmObj.lock);
        BDM_LOGWARN(0, "No valid bdm id.");
        return BDM_INVALID_ID;
    }

    if (bdmId < BDM_INVALID_ID && g_bdmObj.list[bdmId].used == TRUE) {
        BDM_SPIN_UNLOCK(&g_bdmObj.lock);
        BDM_LOGERROR(0, "Invalid bdm id(%u).", bdmId);
        return BDM_INVALID_ID;
    }

    if (bdmId < BDM_INVALID_ID && g_bdmObj.list[bdmId].used == FALSE) {
        g_bdmObj.list[bdmId].used = TRUE;
        g_bdmObj.num++;
        BDM_SPIN_UNLOCK(&g_bdmObj.lock);
        return bdmId;
    }

    uint32_t index = BDM_INVALID_ID;
    for (i = 0; i < BDM_MAX_NUM; i++) {
        if (g_bdmObj.list[g_bdmObj.index].used == FALSE) {
            g_bdmObj.list[g_bdmObj.index].used = TRUE;
            index = g_bdmObj.index;
            g_bdmObj.index = (g_bdmObj.index + 1) % BDM_MAX_NUM;
            g_bdmObj.num++;
            break;
        }
        g_bdmObj.index = (g_bdmObj.index + 1) % BDM_MAX_NUM;
    }
    BDM_SPIN_UNLOCK(&g_bdmObj.lock);
    return index;
}

void BdmObjFreeBdmId(uint32_t bdmId)
{
    if (bdmId >= BDM_MAX_NUM) {
        BDM_LOGERROR(0, "Invalid bdm id(%u).", bdmId);
        return;
    }

    BDM_SPIN_LOCK(&g_bdmObj.lock);
    if (g_bdmObj.list[bdmId].used == FALSE) {
        BDM_SPIN_UNLOCK(&g_bdmObj.lock);
        BDM_LOGINFO(0, "Already free, bdm id(%u).", bdmId);
        return;
    }
    g_bdmObj.list[bdmId].used = FALSE;
    g_bdmObj.list[bdmId].obj = NULL;
    g_bdmObj.num--;
    BDM_SPIN_UNLOCK(&g_bdmObj.lock);
    return;
}

void BdmObjInsert(BdmObj *obj)
{
    BDM_SPIN_LOCK(&g_bdmObj.lock);
    g_bdmObj.list[obj->bdmId].obj = obj;
    BDM_SPIN_UNLOCK(&g_bdmObj.lock);
    return;
}

int32_t BdmCreate(BdmCreatePara *createPara, uint32_t *bdmId)
{
    if (g_bdmOpsEx.create == NULL) {
        BDM_LOGERROR(0, "Invalid operate.");
        return BDM_CODE_ERR;
    }

    *bdmId = BdmObjAllocBdmId(createPara->bdmId);
    if (*bdmId == BDM_INVALID_ID) {
        BDM_LOGERROR(0, "Alloc bdm id failed.");
        return BDM_CODE_CACHELIST_FULL;
    }

    BdmObj *obj = g_bdmOpsEx.create(*bdmId, (uintptr_t)createPara);
    if (obj == NULL) {
        BDM_LOGERROR(0, "Bdm create failed.");
        BdmObjFreeBdmId(*bdmId);
        return BDM_CODE_ERR;
    }

    BdmObjInsert(obj);
    BDM_LOGINFO(0, "Bdm create succeed, bdm id(%u).", *bdmId);
    return BDM_CODE_OK;
}

int32_t BdmDestory(uint32_t bdmId)
{
    if (bdmId >= BDM_MAX_NUM || g_bdmObj.list[bdmId].used == FALSE) {
        BDM_LOGWARN(0, "Invalid bdm id(%u).", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    BdmObj *obj = g_bdmObj.list[bdmId].obj;
    if (g_bdmOpsEx.destory == NULL) {
        BDM_LOGERROR(0, "Invalid operate.");
        return BDM_CODE_ERR;
    }

    int32_t ret = g_bdmOpsEx.destory(obj);
    if (ret != RETURN_OK) {
        BDM_LOGERROR(0, "Bdm destory failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    BdmObjFreeBdmId(bdmId);
    BDM_LOGINFO(0, "Bdm destory succeed, bdm id(%u).", bdmId);
    return BDM_CODE_OK;
}

BdmObj *BdmGetBdmObj(uint32_t bdmId)
{
    if (bdmId >= BDM_MAX_NUM || g_bdmObj.list[bdmId].used == FALSE) {
        BDM_LOGWARN(0, "Invalid bdm id(%u).", bdmId);
        return NULL;
    }

    return g_bdmObj.list[bdmId].obj;
}

BdmDiskState BdmGetBdmStatus(uint32_t bdmId)
{
    if (bdmId >= BDM_MAX_NUM || g_bdmObj.list[bdmId].used == FALSE || g_bdmObj.list[bdmId].obj == NULL) {
        BDM_LOGWARN(0, "Invalid bdm id(%u) or bdm is not ok.", bdmId);
        return BDM_DISK_STATE_FAULT;
    }

    return BDM_DISK_STATE_NORMAL;
}

int32_t BdmObjInit(void)
{
    uint32_t index;

    BDM_LOGINFO(0, "Bdm obj init.");

    BDM_SPIN_INIT(&g_bdmObj.lock, 0);
    BDM_SPIN_LOCK(&g_bdmObj.lock);
    g_bdmObj.index = 0UL;
    g_bdmObj.num = 0UL;
    for (index = 0; index < BDM_MAX_NUM; index++) {
        g_bdmObj.list[index].used = FALSE;
        g_bdmObj.list[index].obj = NULL;
    }
    BDM_SPIN_UNLOCK(&g_bdmObj.lock);

    BDM_LOGINFO(0, "Bdm obj init succeed.");

    return BDM_CODE_OK;
}
