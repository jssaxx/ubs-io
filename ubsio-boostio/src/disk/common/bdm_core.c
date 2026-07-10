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

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "bdm_common.h"
#include "bdm_obj.h"
#include "bdm_disk.h"
#include "securec.h"
#include "bdm_core.h"

#if defined(__aarch64__) || defined(__arm__)
#include "kunpeng_cpu_checker.h"
#else
#include "cpu_vendor_checker.h"
#endif

#define CHUNK_ID_TO_BDMID(chunkId) (((chunkId) >> 48UL) & 0xFFFF)
#define ENCODE_CHUNK_ID(chunkId, bdmId) ((((uint64_t)(bdmId) & 0xFFFF) << 48UL) | ((chunkId) & 0xFFFFFFFFFFFF))
#define DENCODE_CHUNK_ID(chunkId) ((chunkId) & 0xFFFFFFFFFFFF)

uint32_t g_bdmInit = 0UL;
uint32_t g_bdmStart = 0UL;
uint32_t g_bdmDiskCount = 0UL;
static uint32_t g_bdmDiskHeadPad = 0UL;

static uint32_t BdmBuildDiskHeadPad(uint32_t isStandalone, uint32_t deviceId)
{
    if (isStandalone == 0) {
        return 0;
    }
    return BDM_DISK_HEAD_STANDALONE_MAGIC | (deviceId & BDM_DISK_HEAD_DEVICE_ID_MASK);
}

void BdmSetDiskStartupInfo(uint32_t isStandalone, uint32_t deviceId)
{
    g_bdmDiskHeadPad = BdmBuildDiskHeadPad(isStandalone, deviceId);
    BDM_LOGINFO(0, "Set bdm disk startup info, standalone(%u), deviceId(%u).", isStandalone, deviceId);
}

int32_t BdmAlloc(uint32_t bdmId, uint64_t bucketId, uint64_t bucketOffset, uint64_t len, uint64_t *chunkId)
{
    if (UNLIKELY(len > BDM_MAX_CHUNK_LENGTH)) {
        BDM_LOGERROR(0, "bdm alloc len(%llu) is invalid.", len);
        return BDM_CODE_INVALID_PARAM;
    }
    if (UNLIKELY(chunkId == NULL)) {
        return BDM_CODE_ERR;
    }

    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.alloc == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register alloc function.");
        return BDM_CODE_ERR;
    }

    int32_t ret = bdm->ops.alloc((uintptr_t)bdm, bucketId, bucketOffset, len, chunkId);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Alloc failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }

    *chunkId = ENCODE_CHUNK_ID(*chunkId, bdmId);
    BDM_LOGDEBUG(0, "Alloc success, chunkId(%lu) bucketId(%lu) bucketOffset(%lu).", *chunkId, bucketId, bucketOffset);
    return BDM_CODE_OK;
}

int32_t BdmFree(uint32_t bdmId, uint64_t len, uint64_t chunkId)
{
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.free == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register free function.");
        return BDM_CODE_ERR;
    }

    uint64_t decodeChunkId = DENCODE_CHUNK_ID(chunkId);
    int32_t ret = bdm->ops.free((uintptr_t)bdm, len, decodeChunkId);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Free failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }

    BDM_LOGDEBUG(0, "Free success, chunkId(%lu).", decodeChunkId);
    return BDM_CODE_OK;
}

int32_t BdmParseChunkId(uint64_t chunkId, uint64_t *offset, char *path)
{
    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.parseChunkId == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register.");
        return BDM_CODE_ERR;
    }

    uint64_t decodeChunkId = DENCODE_CHUNK_ID(chunkId);
    int32_t ret = bdm->ops.parseChunkId((uintptr_t)bdm, decodeChunkId, offset, path);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "parse chunkId failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmRead(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len)
{
    if (UNLIKELY(len == 0 || len > BDM_MAX_CHUNK_LENGTH || offset > BDM_MAX_CHUNK_LENGTH ||
        len + offset > BDM_MAX_CHUNK_LENGTH)) {
        BDM_LOGERROR(0, "read invalid, length(%llu) offset(%llu).", len, offset);
        return BDM_CODE_INVALID_PARAM;
    }

    if (UNLIKELY(buf == NULL)) {
        return BDM_CODE_ERR;
    }

    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.read == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register read function.");
        return BDM_CODE_ERR;
    }

    uint64_t decodeChunkId = DENCODE_CHUNK_ID(chunkId);
    int32_t ret = bdm->ops.read((uintptr_t)bdm, decodeChunkId, offset, buf, len);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Read failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmWrite(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len)
{
    if (UNLIKELY(len == 0 || len > BDM_MAX_CHUNK_LENGTH || offset > BDM_MAX_CHUNK_LENGTH ||
        len + offset > BDM_MAX_CHUNK_LENGTH)) {
        BDM_LOGERROR(0, "write invalid  length(%llu), offset(%llu).", len, offset);
        return BDM_CODE_INVALID_PARAM;
    }

    if (UNLIKELY(buf == NULL)) {
        return BDM_CODE_ERR;
    }

    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.write == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register write function.");
        return BDM_CODE_ERR;
    }

    uint64_t decodeChunkId = DENCODE_CHUNK_ID(chunkId);
    int32_t ret = bdm->ops.write((uintptr_t)bdm, decodeChunkId, offset, buf, len);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Write failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmReadAsync(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len, BdmIoCtx *ioCtx)
{
    if (UNLIKELY(len == 0 || len > BDM_MAX_CHUNK_LENGTH || offset > BDM_MAX_CHUNK_LENGTH ||
        len + offset > BDM_MAX_CHUNK_LENGTH)) {
        BDM_LOGERROR(0, "async read invalid  length(%llu), offset(%llu).", len, offset);
        return BDM_CODE_INVALID_PARAM;
    }

    if (UNLIKELY(buf == NULL || ioCtx == NULL)) {
        return BDM_CODE_ERR;
    }

    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.readAsync == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register read async function.");
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
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Read async failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmWriteAsync(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len, BdmIoCtx *ioCtx)
{
    if (UNLIKELY(len == 0 || len > BDM_MAX_CHUNK_LENGTH || offset > BDM_MAX_CHUNK_LENGTH ||
        len + offset > BDM_MAX_CHUNK_LENGTH)) {
        BDM_LOGERROR(0, "async write invalid  length(%llu), offset(%llu).", len, offset);
        return BDM_CODE_INVALID_PARAM;
    }

    if (UNLIKELY(buf == NULL || ioCtx == NULL)) {
        return BDM_CODE_ERR;
    }

    uint32_t bdmId = CHUNK_ID_TO_BDMID(chunkId);
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.writeAsync == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register write async function.");
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
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Write async failed, bdm id(%u) len(%lu) ret(%d).", bdmId, len, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmGetCapacity(uint32_t bdmId, uint64_t *totalCapacity, uint64_t *usedCapacity)
{
    if (UNLIKELY(totalCapacity == NULL || usedCapacity == NULL)) {
        return BDM_CODE_ERR;
    }

    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.getcap == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register get capacity function.");
        return BDM_CODE_ERR;
    }

    int32_t ret = bdm->ops.getcap((uintptr_t)bdm, totalCapacity, usedCapacity);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Get cap failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmResetScanPool(uint32_t bdmId)
{
    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.allocatorReset == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register alloctor reset function.");
        return BDM_CODE_ERR;
    }

    int32_t ret = bdm->ops.allocatorReset((uintptr_t)bdm);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Reset alloctor failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    return BDM_CODE_OK;
}

BdmDiskState BdmGetDiskStatus(uint32_t bdmId)
{
    return BdmGetBdmStatus(bdmId);
}

int32_t BdmGetNextUsedChunkId(uint32_t bdmId, uint64_t *chunkId, uint64_t *chunkSize, uint64_t *bucketId,
    uint64_t *bucketOffset)
{
    if (UNLIKELY(chunkId == NULL || chunkSize == NULL || bucketId == NULL || bucketOffset == NULL)) {
        return BDM_CODE_ERR;
    }

    BdmObj *bdm = BdmGetBdmObj(bdmId);
    if (UNLIKELY(bdm == NULL)) {
        BDM_LOGERROR(0, "Invalid bdm id(%u), not exist bdm object.", bdmId);
        return BDM_CODE_NOT_EXIST;
    }

    if (UNLIKELY(bdm->ops.nextchunk == NULL)) {
        BDM_LOGERROR(0, "Invalid ops, not register next chunk function.");
        return BDM_CODE_ERR;
    }

    int32_t ret = bdm->ops.nextchunk((uintptr_t)bdm, chunkId, chunkSize, bucketId, bucketOffset);
    if (ret == BDM_CODE_SCAN_OFF) {
        return ret;
    }
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Scan failed, bdm id(%u) ret(%d).", bdmId, ret);
        return ret;
    }
    *chunkId = ENCODE_CHUNK_ID(*chunkId, bdmId);
    BDM_LOGDEBUG(0, "Recover one, chunk(%lu) bucketId(%lu) bucketOffset(%lu).", *chunkId, *bucketId, *bucketOffset);
    return BDM_CODE_OK;
}

int32_t BdmInit(void)
{
    if (UNLIKELY(g_bdmInit == 1UL)) {
        BDM_LOGINFO(0, "Bdm already init succeed.");
        return RETURN_OK;
    }

    int32_t ret = BdmObjInit();
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Bdm obj init failed, ret(%d).", ret);
        return ret;
    }

    ret = BdmDiskInit();
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Bdm disk obj init failed, ret(%d).", ret);
        return ret;
    }

    g_bdmInit = 1UL;
    BDM_LOGINFO(0, "Bdm init succeed.");
    return BDM_CODE_OK;
}

static int32_t BdmDevicesCreate(uint32_t diskId, char *name, uint64_t capacity, uint64_t chunkSize)
{
    if (UNLIKELY(name == NULL)) {
        BDM_LOGERROR(0, "Disk path is null.");
        return BDM_CODE_ERR;
    }

    BdmCreatePara para = { 0 };
    int32_t ret = strncpy_s(para.name, BDM_NAME_LEN, name, strlen(name));
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Sprintf device name failed, ret(%d).", ret);
        return BDM_CODE_ERR;
    }

    ret = sprintf_s(para.sn, BDM_SN_LEN, "%s_%u", "bio_file", diskId);
    if (UNLIKELY(ret < 0)) {
        BDM_LOGERROR(0, "Sprintf device sn failed, ret(%d).", ret);
        return BDM_CODE_ERR;
    }

    para.offset = 0;
    para.length = capacity;
    para.bdmId = diskId;
    para.pad = g_bdmDiskHeadPad;
    para.minChunkSize = chunkSize;
    para.maxChunkSize = chunkSize;
    uint32_t bdmId;
    ret = BdmCreate(&para, &bdmId);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    BDM_LOGINFO(0, "Create disk success, DiskId(%u) BdmId(%u) chunkSize(%lu) capacity(%lu).", diskId, bdmId, chunkSize, capacity);
    return ret;
}

static bool IsDiskFile(char *path)
{
    struct stat pathStat;
    if (stat(path, &pathStat) != 0) {
        // 获取状态失败，可能是路径不存在或无权限
        return false;
    }
    if (S_ISBLK(pathStat.st_mode)) {
        // 是块设备
        return true;
    }
    return false;
}

int32_t BdmStart(DiskDevices *diskList, uint64_t chunkSize)
{
    if (UNLIKELY(g_bdmStart == 1UL)) {
        BDM_LOGINFO(0, "Bdm already start succeed.");
        return RETURN_OK;
    }
    if (UNLIKELY(diskList == NULL)) {
        return BDM_CODE_ERR;
    }
    if (UNLIKELY(diskList->num > DISK_DEV_NUM)) {
        BDM_LOGERROR(0, "Disk device num input failed, diskNum(%u), limit(%u).", diskList->num,
            (uint32_t)DISK_DEV_NUM);
        return BDM_CODE_INVALID_PARAM;
    }
    if (chunkSize < BDM_MIN_CHUNK_LENGTH || chunkSize > BDM_MAX_CHUNK_LENGTH) { // chunkSize必须满足[1M, 16M].
        BDM_LOGERROR(0, "Disk device param input failed.");
        return BDM_CODE_INVALID_PARAM;
    }

    int32_t ret;
    uint32_t diskId;
    uint32_t failCnt = 0;
    for (diskId = 0; diskId < diskList->num; diskId++) {
#ifndef DEBUG_UT
        if (IsDiskFile(diskList->list[diskId].path) == false) {
            BDM_LOGERROR(0, "Check devices letter failed, diskId(%u).", diskId);
            return BDM_CODE_ERR;
        }
#endif
        ret = BdmDevicesCreate(diskId, diskList->list[diskId].path, diskList->diskCaps[diskId], chunkSize);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BDM_LOGERROR(0, "Create devices failed, diskId(%u) ret(%d).", diskId, ret);
            failCnt++;
        }
    }

    // 检查磁盘设备创建结果.
    if ((diskList->num != 0) && (failCnt == diskList->num)) {
        BDM_LOGERROR(0, "Create devices all disks failed, disk num(%u), ret(%d).", diskList->num, ret);
        return ret;
    }
    g_bdmStart = 1UL;
    g_bdmDiskCount = diskList->num - failCnt;
    BDM_LOGINFO(0, "Bdm start succeed, create device num is %u.", g_bdmDiskCount);
    return BDM_CODE_OK;
}

int32_t BdmUpdate(char *diskPath, uint64_t chunkSize, uint64_t diskCap)
{
    uint32_t diskId = g_bdmDiskCount;
    int32_t ret = BdmDevicesCreate(diskId, diskPath, diskCap, chunkSize);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Create devices failed, diskId(%u) ret(%d).", diskId, ret);
        return ret;
    }
    __sync_fetch_and_add(&g_bdmDiskCount, 1);
    return BDM_CODE_OK;
}

uint32_t BdmGetDiskCount()
{
    return g_bdmDiskCount;
}
