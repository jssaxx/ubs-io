/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "bdm_core.h"
#include "bdm_common.h"
#include "bdm_obj.h"
#include "bdm_disk.h"
#include "securec.h"

#define CHUNK_ID_TO_BDMID(chunkId) (((chunkId) >> 48UL) & 0xFFFF)
#define ENCODE_CHUNK_ID(chunkId, bdmId) ((((uint64_t)(bdmId) & 0xFFFF) << 48UL) | ((chunkId) & 0xFFFFFFFFFFFF))
#define DENCODE_CHUNK_ID(chunkId) ((chunkId) & 0xFFFFFFFFFFFF)

uint32_t g_bdmInit = 0UL;
uint32_t g_bdmStart = 0UL;

int32_t BdmAlloc(uint32_t bdmId, uint64_t bucketId, uint64_t len, uint64_t *chunkId)
{
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.alloc == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    int32_t ret = bdm->ops.alloc((uintptr_t)bdm, bucketId, len, chunkId);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Alloc failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    *chunkId = ENCODE_CHUNK_ID(*chunkId, bdmId);
    return BDM_CODE_OK;
}

int32_t BdmFree(uint32_t bdmId, uint64_t len, uint64_t chunkId)
{
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.free == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    chunkId = DENCODE_CHUNK_ID(chunkId);
    int32_t ret = bdm->ops.free((uintptr_t)bdm, len, chunkId);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Free failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmRead(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len)
{
    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);

    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.read == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    chunkId = DENCODE_CHUNK_ID(chunkId);
    int32_t ret = bdm->ops.read((uintptr_t)bdm, chunkId, offset, buf, len);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Read failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmWrite(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len)
{
    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);

    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.write == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    chunkId = DENCODE_CHUNK_ID(chunkId);
    int32_t ret = bdm->ops.write((uintptr_t)bdm, chunkId, offset, buf, len);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Write failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmReadAsync(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len, BdmIoCtx *ioCtx)
{
    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);

    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.readAsync == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    BdmAsyncOpsReq req = { 0 };
    req.objPtr = (uintptr_t)bdm;
    req.chunkId = DENCODE_CHUNK_ID(chunkId);
    req.offset = offset;
    req.buf = buf;
    req.len = len;
    req.ioCtx = ioCtx;

    int32_t ret = bdm->ops.readAsync(&req);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Read failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmWriteAsync(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len, BdmIoCtx *ioCtx)
{
    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);

    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.writeAsync == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    BdmAsyncOpsReq req = { 0 };
    req.objPtr = (uintptr_t)bdm;
    req.chunkId = DENCODE_CHUNK_ID(chunkId);
    req.offset = offset;
    req.buf = buf;
    req.len = len;
    req.ioCtx = ioCtx;

    int32_t ret = bdm->ops.writeAsync(&req);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Read failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmGetCapacity(uint32_t bdmId, uint64_t *totalCapacity, uint64_t *usedCapacity)
{
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.getcap == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    int32_t ret = bdm->ops.getcap((uintptr_t)bdm, totalCapacity, usedCapacity);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Getcap failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmResetScanPool(uint32_t bdmId)
{
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.reset == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    int32_t ret = bdm->ops.reset((uintptr_t)bdm);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Reset failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmGetNextUsedChunkId(uint32_t bdmId, uint64_t *chunkId, uint64_t *chunkSize, uint64_t *bucketId)
{
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (bdm == NULL) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (bdm->ops.nextchunk == NULL) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    int32_t ret = bdm->ops.nextchunk((uintptr_t)bdm, chunkId, chunkSize, bucketId);
    if (ret == BDM_CODE_SCAN_OFF) {
        return ret;
    }
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Scan failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmInit(void)
{
    if (g_bdmInit == 1UL) {
        BDM_LOGINFO(0, "Bdm already init succeed.");
        return RETURN_OK;
    }

    BDM_LOGINFO(0, "Bdm init beign.");

    int32_t ret = BdmObjInit();
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Bdm obj init failed, ret(%d).", ret);
        return ret;
    }

    ret = BdmDiskInit();
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Bdm disk obj init failed, ret(%d).", ret);
        return ret;
    }

    g_bdmInit = 1UL;

    BDM_LOGINFO(0, "Bdm init succeed.");
    return BDM_CODE_OK;
}

static int32_t BdmDevicesCreate(uint32_t diskId, char *name, uint64_t capacity, uint64_t chunkSize)
{
    BdmCreatePara para = { 0 };
    int32_t ret;

    ret = strncpy_s(para.name, BDM_NAME_LEN, name, strlen(name));
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Sprintf_s failed(%d).", ret);
        return BDM_CODE_ERR;
    }

    ret = sprintf_s(para.sn, BDM_SN_LEN, "%s_%u", "bio_file", diskId);
    if (ret < 0) {
        BDM_LOGERROR(0, "Sprintf_s failed(%d).", ret);
        return BDM_CODE_ERR;
    }

    para.offset = 0;
    para.length = capacity;
    para.bdmId = diskId;
    para.pad = 0;
    para.minChunkSize = chunkSize;
    para.maxChunkSize = chunkSize;

    uint32_t bdmId;
    ret = BdmCreate(&para, &bdmId);
    if (ret != BDM_CODE_OK) {
        return ret;
    }
    BDM_LOGINFO(0, "DiskId(%u) BdmId(%u) chunkSize(%lu) capacity(%lu).",
        diskId, bdmId, chunkSize, capacity);
    return ret;
}

int32_t BdmStart(DiskDevices *diskList, uint64_t capacity, uint64_t chunkSize)
{
    int32_t ret;

    if (g_bdmStart == 1UL) {
        BDM_LOGINFO(0, "Bdm already start succeed.");
        return RETURN_OK;
    }

    uint32_t diskId;
    for (diskId = 0; diskId < diskList->num; diskId++) {
        ret = BdmDevicesCreate(diskId, diskList->list[diskId].path, capacity, chunkSize);
        if (ret != BDM_CODE_OK) {
            BDM_LOGERROR(0, "Create devices failed, diskId(%u) ret(%d).", diskId, ret);
            return ret;
        }
    }
    g_bdmStart = 1UL;
    BDM_LOGINFO(0, "Bdm start succeed.");
    return BDM_CODE_OK;
}
