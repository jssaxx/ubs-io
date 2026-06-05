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

#ifndef BDM_CORE_H
#define BDM_CORE_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BDM_MAX_NUM (1024UL)
#define BDM_NAME_LEN (128UL)
#define BDM_SN_LEN (32UL)
#define BDM_MIN_CHUNK_LENGTH (1048576UL) /* 1M */
#define BDM_MAX_CHUNK_LENGTH (16777216UL) /* 16M */
#define BDM_ALIGN_SIZE (2097152UL)
#define BDM_RESTORE_META_SIZE (2097152UL)
#define BDM_INVALID_ID (1024UL)
/* Low 16 bits store standalone mode and device id. High 16 bits are reserved. */
#define BDM_DISK_HEAD_STANDALONE_MAGIC (0x0000BD00U)
#define BDM_DISK_HEAD_MODE_MASK (0x0000FF00U)
#define BDM_DISK_HEAD_DEVICE_ID_MASK (0x000000FFU)

typedef struct {
    char name[BDM_NAME_LEN];
    char sn[BDM_SN_LEN];
    uint64_t offset;
    uint64_t length;

    uint32_t bdmId; /* 故障恢复场景:由用户指定bdmId */
    uint32_t pad;
    uint64_t minChunkSize;
    uint64_t maxChunkSize;
} BdmCreatePara;

typedef enum {
    BDM_CODE_OK = 0,
    BDM_CODE_ERR = -1,
    BDM_CODE_ERR_IO = 5,
    BDM_CODE_INVALID_PARAM = -110,
    BDM_CODE_NOT_EXIST = -109,
    BDM_CODE_EXIST = -108,
    BDM_CODE_CACHELIST_FULL = -107,
    BDM_CODE_CROSS_BOUND = -106,
    BDM_CODE_INVALID_CHUNK_ID = -105,
    BDM_CODE_INVALID_BDM_ID = -104,
    BDM_CODE_SCAN_OFF = -103,
    BDM_CODE_METADATA_MISMATCH = -102,
} BdmRetCode;

typedef enum {
    BDM_DISK_STATE_NORMAL = 0,
    BDM_DISK_STATE_FAULT = 1,
    BDM_DISK_STATE_BUTT
} BdmDiskState;

#define BDM_IO_CTX_RES_LEN (256UL)

typedef void (*BdmIoCb)(void *ctx, int32_t ret);

typedef struct {
    BdmIoCb cb;
    void *ctx;
    char res[BDM_IO_CTX_RES_LEN];
} BdmIoCtx;

#define DISK_PATH_LEN (256UL)
#define DISK_DEV_NUM (16UL)

typedef struct {
    char path[DISK_PATH_LEN];
} DiskPath;

typedef struct {
    uint32_t num;
    uint64_t diskCaps[DISK_DEV_NUM];
    DiskPath list[DISK_DEV_NUM];
} DiskDevices;

int32_t BdmCreate(BdmCreatePara *createPara, uint32_t *bdmId);

int32_t BdmDestroy(uint32_t bdmId);

int32_t BdmAlloc(uint32_t bdmId, uint64_t bucketId, uint64_t bucketOffset, uint64_t len, uint64_t *chunkId);

int32_t BdmFree(uint32_t bdmId, uint64_t len, uint64_t chunkId);

int32_t BdmParseChunkId(uint64_t chunkId, uint64_t *offset, char *path);

int32_t BdmRead(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len);

int32_t BdmWrite(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len);

int32_t BdmReadAsync(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len, BdmIoCtx *ioCtx);

int32_t BdmWriteAsync(uint64_t chunkId, uint64_t offset, void *buf, uint64_t len, BdmIoCtx *ioCtx);

int32_t BdmGetCapacity(uint32_t bdmId, uint64_t *totalCapacity, uint64_t *usedCapacity);

int32_t BdmResetScanPool(uint32_t bdmId);

int32_t BdmGetNextUsedChunkId(uint32_t bdmId, uint64_t *chunkId, uint64_t *chunkSize, uint64_t *bucketId,
    uint64_t *bucketOffset);

int32_t BdmInit(void);

void BdmSetDiskStartupInfo(uint32_t isStandalone, uint32_t deviceId);

int32_t BdmStart(DiskDevices *diskList, uint64_t chunkSize);

int32_t BdmUpdate(char *diskPath, uint64_t chunkSize, uint64_t diskCap);

uint32_t BdmGetDiskCount(void);

int32_t BdmResetDisk(uint16_t diskId);

BdmDiskState BdmGetDiskStatus(uint32_t bdmId);

void BdmSetDiskUsedStatus(uint32_t bdmId, uint32_t status);

#ifdef __cplusplus
}
#endif

#endif
