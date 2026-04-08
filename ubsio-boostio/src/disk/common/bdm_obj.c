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

static BdmObjMgr g_bdmObj = { 0 };  // 管理bdm object

typedef struct {
    BdmCreateFunc create;
    BdmDestroyFunc destroy;
    BdmResetFunc reset;
} BdmOpsEx;

static BdmOpsEx g_bdmOpsEx = { 0 };

void BdmRegOpsWithCreate(BdmCreateFunc create)
{
    g_bdmOpsEx.create = create;
}

void BdmRegOpsWithDestroy(BdmDestroyFunc destroy)
{
    g_bdmOpsEx.destroy = destroy;
}

void BdmRegOpsWithReset(BdmResetFunc reset)
{
    g_bdmOpsEx.reset = reset;
}

static uint32_t BdmObjAllocBdmId(uint32_t bdmId)
{
    BDM_SPIN_LOCK(&g_bdmObj.lock);
    if (UNLIKELY(g_bdmObj.num == BDM_MAX_NUM)) {
        BDM_SPIN_UNLOCK(&g_bdmObj.lock);
        BDM_LOGWARN(0, "No valid bdm id.");
        return BDM_INVALID_ID;
    }

    if (UNLIKELY(bdmId < BDM_INVALID_ID && g_bdmObj.list[bdmId].used == TRUE)) {
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
    for (uint32_t i = 0; i < BDM_MAX_NUM; i++) {
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

static void BdmObjFreeBdmId(uint32_t bdmId)
{
    if (UNLIKELY(bdmId >= BDM_MAX_NUM)) {
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
}

static void BdmObjInsert(BdmObj *obj)
{
    BDM_SPIN_LOCK(&g_bdmObj.lock);
    g_bdmObj.list[obj->bdmId].obj = obj;
    BDM_SPIN_UNLOCK(&g_bdmObj.lock);
}

int32_t BdmCreate(BdmCreatePara *createPara, uint32_t *bdmId)
{
    if (UNLIKELY(createPara == NULL || bdmId == NULL)) {
        return BDM_CODE_ERR;
    }

    if (UNLIKELY(g_bdmOpsEx.create == NULL)) {
        BDM_LOGERROR(0, "Invalid create operate.");
        return BDM_CODE_ERR;
    }

    *bdmId = BdmObjAllocBdmId(createPara->bdmId);
    if (UNLIKELY(*bdmId == BDM_INVALID_ID)) {
        BDM_LOGERROR(0, "Alloc bdm id failed.");
        return BDM_CODE_CACHELIST_FULL;
    }

    BdmObj *obj = g_bdmOpsEx.create(*bdmId, (uintptr_t)createPara);
    if (UNLIKELY(obj == NULL)) {
        BDM_LOGERROR(0, "Bdm object create failed.");
        BdmObjFreeBdmId(*bdmId);
        return BDM_CODE_ERR;
    }

    BdmObjInsert(obj);
    BDM_LOGINFO(0, "Bdm object create succeed, bdm id(%u).", *bdmId);
    return BDM_CODE_OK;
}

int32_t BdmDestroy(uint32_t bdmId)
{
    if (UNLIKELY(bdmId >= BDM_MAX_NUM || g_bdmObj.list[bdmId].used == FALSE)) {
        BDM_LOGWARN(0, "Invalid bdm id(%u).", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    BdmObj *obj = g_bdmObj.list[bdmId].obj;
    if (UNLIKELY(g_bdmOpsEx.destroy == NULL)) {
        BDM_LOGERROR(0, "Invalid operate.");
        return BDM_CODE_ERR;
    }

    int32_t ret = g_bdmOpsEx.destroy(obj);
    if (ret != RETURN_OK) {
        BDM_LOGERROR(0, "Bdm destroy failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    BdmObjFreeBdmId(bdmId);
    BDM_LOGINFO(0, "Bdm destroy succeed, bdm id(%u).", bdmId);
    return BDM_CODE_OK;
}

BdmObj *BdmGetBdmObj(uint32_t bdmId)
{
    if (UNLIKELY(bdmId >= BDM_MAX_NUM || g_bdmObj.list[bdmId].used == FALSE)) {
        BDM_LOGWARN(0, "Invalid bdm id(%u).", bdmId);
        return NULL;
    }
    return g_bdmObj.list[bdmId].obj;
}

BdmDiskState BdmGetBdmStatus(uint32_t bdmId)
{
    if (UNLIKELY(bdmId >= BDM_MAX_NUM || g_bdmObj.list[bdmId].used == FALSE || g_bdmObj.list[bdmId].obj == NULL)) {
        BDM_LOGWARN(0, "Invalid bdm id(%u) or bdm is not ok.", bdmId);
        return BDM_DISK_STATE_FAULT;
    }
    return BDM_DISK_STATE_NORMAL;
}

void BdmSetDiskUsedStatus(uint32_t bdmId, uint32_t status)
{
    if (UNLIKELY(bdmId >= BDM_MAX_NUM)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u) .", bdmId);
        return;
    }
    if (UNLIKELY(g_bdmObj.list[bdmId].obj == NULL)) {
        BDM_LOGERROR(0, "Bdm set disk status failed, bdm id(%u).", bdmId);
        return;
    }
    g_bdmObj.list[bdmId].used = status;
}

int32_t BdmObjInit(void)
{
    BDM_SPIN_INIT(&g_bdmObj.lock, 0);
    BDM_SPIN_LOCK(&g_bdmObj.lock);
    g_bdmObj.index = 0UL;
    g_bdmObj.num = 0UL;
    for (uint32_t index = 0; index < BDM_MAX_NUM; index++) {
        g_bdmObj.list[index].used = FALSE;
        g_bdmObj.list[index].obj = NULL;
    }
    BDM_SPIN_UNLOCK(&g_bdmObj.lock);
    return BDM_CODE_OK;
}

int32_t BdmResetDisk(uint16_t diskId)
{
    BdmObj *obj = BdmGetBdmObj(diskId);
    if (UNLIKELY(obj == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", diskId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(g_bdmOpsEx.reset == NULL)) {
        BDM_LOGERROR(0, "Invalid reset operate.");
        return BDM_CODE_ERR;
    }

    int32_t ret = g_bdmOpsEx.reset(obj);
    if (UNLIKELY(ret != RETURN_OK)) {
        BDM_LOGERROR(0, "Bdm reset failed, bdm id(%u) ret(%d).", diskId, ret);
        return ret;
    }

    BDM_LOGINFO(0, "Bdm reset succeed, bdm id(%u).", diskId);
    return BDM_CODE_OK;
}
