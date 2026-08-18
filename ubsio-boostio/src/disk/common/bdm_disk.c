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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <liburing.h>
#include <linux/version.h>
#include <pthread.h>
#include <semaphore.h>
#include "securec.h"
#include "dlist.h"
#include "bio_tracepoint_helper_c.h"
#include "cm_c.h"
#include "bdm_threadpool.h"
#include "bdm_obj.h"
#include "bdm_allocator.h"
#include "bdm_common.h"
#include "bdm_core.h"
#include "bdm_disk.h"
#include "htracer_c.h"

#ifndef O_DIRECT
#define O_DIRECT 00040000
#endif

#define BDM_OPEN_FILE_PERMISSION 0640

#define BDM_PAGE_SIZE getpagesize()

#define BDM_DISK_MAGIC (0xFFDDCBAABCDDFF)
#define BDM_IOCTX_EVENTS_NUM (1024UL)
#define BDM_URING_MAX_INFLIGHT (896UL)
#define BDM_IO_RETRY_NUM (3UL)
#define BDM_URING_EAGAIN_RETRY_NUM (64UL)
#define BDM_URING_EAGAIN_BACKOFF_BASE_US (10UL)
#define BDM_URING_EAGAIN_BACKOFF_MAX_US (1000UL)
#define BDM_URING_SQE_RETRY_NUM (10000UL)
#define BDM_URING_SQE_RETRY_INTERVAL_US (10UL)

#define BDM_AYSNC_IO_FD_NUM (8UL)

#define BDM_WORKER_THREAD_NUM (8UL)

#define BDM_DEFAULT_THREAD_NUM (1UL)

#define BDM_SYNC_WORKER_DEFAULT_NUM (16UL)
#define BDM_SYNC_WORKER_MAX_NUM (64UL)
#define BDM_SYNC_QUEUE_SIZE (1024UL)

#define BDM_BIND_CPU_DEFAULT (-1)

#define BDM_DIRECT_IO_ALIGN_SIZE (512UL)
#define BDM_SYNC_URING_QUEUE_DEPTH (32UL)
#define BDM_SYNC_URING_MIN_CHUNK_SIZE (4096UL)

static BdmIoEngine g_bdmIoEngine = BDM_IO_ENGINE_SYNC;
static BdmUringSqpollMode g_bdmUringSqpollMode = BDM_URING_SQPOLL_AUTO;
static uint32_t g_bdmSyncWorkerNum = BDM_SYNC_WORKER_DEFAULT_NUM;
static uint32_t g_bdmForceNew = 0;

typedef struct {
    uint64_t magic;
    uint32_t bdmId;
    uint32_t pad;
    uint64_t minChunkSize;
    uint64_t maxChunkSize;
    uint64_t totalSize;

    uint64_t metaOffset;
    uint64_t metaLength;
    uint64_t dataOffset;
    uint64_t dataLength;

    char name[BDM_NAME_LEN];
    char sn[BDM_SN_LEN];
    uint64_t offset;
    uint64_t headSize;
} BdmDiskHead;

typedef struct {
    BdmAllocator allocator;
    DList node;
    uint32_t bdmId;
    uint32_t pad;
    uint64_t minChunkSize;
    uint64_t maxChunkSize;
    uint64_t totalSize;

    uint64_t metaOffset;
    uint64_t metaLength;
    uint64_t dataOffset;
    uint64_t dataLength;

    char name[BDM_NAME_LEN];
    char sn[BDM_SN_LEN];
    uint64_t offset;
    uint64_t headSize;
    int32_t fd;
    int32_t asyncfd[BDM_AYSNC_IO_FD_NUM];
} BdmDiskItem;

typedef struct {
    BDM_SPINLOCK_T lock;
    DList head;
    uint32_t num;
} BdmDiskMgr;

typedef struct {
    uint32_t index;
    void *ctx;
} BdmThreadCtx;

typedef struct {
    struct io_uring ring;
    DList node;
    bool inited;
    bool registered;
} BdmSyncUringCtx;

typedef struct {
    char *ioBuf;
    uint64_t ioLen;
    uint64_t ioOffset;
    uint64_t userOffset;
    bool needBounce;
    bool overRead;
} BdmSyncUringBuffer;

typedef struct {
    int32_t cpus[BDM_WORKER_THREAD_NUM];
    BdmThreadCtx threadCtx[BDM_WORKER_THREAD_NUM];
    pthread_t threadId[BDM_WORKER_THREAD_NUM];
    struct io_uring ring[BDM_WORKER_THREAD_NUM];
    bool ringInited[BDM_WORKER_THREAD_NUM];
    sem_t ringSlots[BDM_WORKER_THREAD_NUM];
    bool ringSlotsInited[BDM_WORKER_THREAD_NUM];
    uint32_t ringInflight[BDM_WORKER_THREAD_NUM];
    bool eventThreadStarted[BDM_WORKER_THREAD_NUM];
    volatile bool stopping[BDM_WORKER_THREAD_NUM];
    pthread_mutex_t ringLock[BDM_WORKER_THREAD_NUM];
    bool ringSqpoll[BDM_WORKER_THREAD_NUM];
    BDM_THREAD_POOL_S *pool[BDM_WORKER_THREAD_NUM];
} BdmThreadPool;

typedef struct {
    void *buf;
    void *ioBuf;
    uint64_t len;
    uint64_t userLen;
    uint64_t userOffset;
    uint64_t chunkId;
    uint64_t offset;
    uint32_t retryNum;
    uint32_t eagainRetryNum;
    bool isRead;
    bool needBounce;
    bool slotAcquired;
    int32_t traceId;
    uint64_t traceStartNs;
    BdmIoCb cb;
    void *ctx;
    void *item;
} BdmIoContext;

static BdmDiskMgr g_bdmDisk = { 0 };

static uint64_t g_bdmIndex = 0;

static BdmThreadPool g_bdmThreadPool;
static BDM_THREAD_POOL_S *g_bdmSyncThreadPool = NULL;
static pthread_once_t g_bdmSyncUringKeyOnce = PTHREAD_ONCE_INIT;
static pthread_key_t g_bdmSyncUringKey;
static int32_t g_bdmSyncUringKeyRet = BDM_CODE_OK;
static bool g_bdmSyncUringKeyCreated = false;
static pthread_mutex_t g_bdmDiskFaultHandlerLock = PTHREAD_MUTEX_INITIALIZER;
static BdmDiskFaultHandler g_bdmDiskFaultHandler = NULL;
static void *g_bdmDiskFaultHandlerContext = NULL;
static pthread_mutex_t g_bdmSyncUringCtxLock = PTHREAD_MUTEX_INITIALIZER;
static DList g_bdmSyncUringCtxList = D_LIST_HEAD_INIT(g_bdmSyncUringCtxList);

void BdmRegisterDiskFaultHandler(BdmDiskFaultHandler handler, void *context)
{
    pthread_mutex_lock(&g_bdmDiskFaultHandlerLock);
    g_bdmDiskFaultHandler = handler;
    g_bdmDiskFaultHandlerContext = context;
    pthread_mutex_unlock(&g_bdmDiskFaultHandlerLock);
}

static int32_t BdmNotifyDiskFault(uint16_t diskId)
{
    int32_t ret;
    pthread_mutex_lock(&g_bdmDiskFaultHandlerLock);
    if (g_bdmDiskFaultHandler != NULL) {
        ret = g_bdmDiskFaultHandler(diskId, g_bdmDiskFaultHandlerContext);
        pthread_mutex_unlock(&g_bdmDiskFaultHandlerLock);
        return ret;
    }
    pthread_mutex_unlock(&g_bdmDiskFaultHandlerLock);
    return CmReportDiskStatus(diskId, CM_DISK_FAULT);
}

static void BdmDiskReportIoFault(BdmDiskItem *itemPtr, uint64_t offset, uint64_t len);

static int32_t BdmDiskFillDiskHead(BdmDiskHead *head, BdmDiskItem *item);
static void BdmDiskCompleteReq(BdmAsyncOpsReq *req, int32_t ret);

BdmIoEngine BdmGetIoEngine(void)
{
    return g_bdmIoEngine;
}

int32_t BdmSetIoEngine(const char *engineName)
{
    if (UNLIKELY(engineName == NULL)) {
        return BDM_CODE_INVALID_PARAM;
    }

    if (strcmp(engineName, "io_uring") == 0) {
        g_bdmIoEngine = BDM_IO_ENGINE_IO_URING;
        return BDM_CODE_OK;
    }

    if (strcmp(engineName, "sync") == 0) {
        g_bdmIoEngine = BDM_IO_ENGINE_SYNC;
        return BDM_CODE_OK;
    }

    BDM_LOGERROR(0, "Unsupported bdm io engine(%s).", engineName);
    return BDM_CODE_INVALID_PARAM;
}

int32_t BdmSetUringSqpollMode(const char *modeName)
{
    if (UNLIKELY(modeName == NULL)) {
        return BDM_CODE_INVALID_PARAM;
    }

    if (strcmp(modeName, "auto") == 0) {
        g_bdmUringSqpollMode = BDM_URING_SQPOLL_AUTO;
        return BDM_CODE_OK;
    }
    if (strcmp(modeName, "required") == 0) {
        g_bdmUringSqpollMode = BDM_URING_SQPOLL_REQUIRED;
        return BDM_CODE_OK;
    }
    if (strcmp(modeName, "disabled") == 0) {
        g_bdmUringSqpollMode = BDM_URING_SQPOLL_DISABLED;
        return BDM_CODE_OK;
    }

    BDM_LOGERROR(0, "Unsupported bdm io_uring SQPOLL mode(%s).", modeName);
    return BDM_CODE_INVALID_PARAM;
}

int32_t BdmSetSyncWorkerNum(uint32_t workerNum)
{
    if (UNLIKELY(workerNum == 0 || workerNum > BDM_SYNC_WORKER_MAX_NUM)) {
        BDM_LOGERROR(0, "Invalid bdm sync worker number(%u), range[1, %u].", workerNum,
            (uint32_t)BDM_SYNC_WORKER_MAX_NUM);
        return BDM_CODE_INVALID_PARAM;
    }
    g_bdmSyncWorkerNum = workerNum;
    return BDM_CODE_OK;
}

uint32_t BdmGetNormalDiskNum(void)
{
    BDM_SPIN_LOCK(&g_bdmDisk.lock);
    uint32_t num = g_bdmDisk.num;
    BDM_SPIN_UNLOCK(&g_bdmDisk.lock);
    return num;
}

void BdmSetNormalDiskNum(uint32_t diskNum)
{
    BDM_SPIN_LOCK(&g_bdmDisk.lock);
    g_bdmDisk.num = diskNum;
    BDM_SPIN_UNLOCK(&g_bdmDisk.lock);
}

uint64_t BdmDiskInnerReadWriteImpl(int32_t fd, char *buff, uint64_t len, uint64_t offset, int32_t isRead)
{
    uint64_t remain = len;
    int64_t rc = 0;
    BIO_TP_START(BDM_RW_IO_FAIL, 0);
    while (remain > 0) {
        if (isRead) {
            rc = pread(fd, buff + (len - remain), remain, offset + (len - remain));
        } else {
            rc = pwrite(fd, buff + (len - remain), remain, offset + (len - remain));
        }
        if (UNLIKELY(rc <= 0)) {
            BDM_LOGWARN(0, "%s failed (%s), fd %d, rc %d, len %d, off %lu, remain %lu.", isRead ? "Read" : "Write",
                strerror(errno), fd, rc, len, offset, remain);
            break;
        }
        remain -= (uint64_t)rc;
    }
    BIO_TP_END;
    return (len - remain);
}

static int32_t BdmDiskCheckRegionRange(const BdmDiskItem *item, uint64_t offset, uint64_t len)
{
    if (UNLIKELY(item == NULL || offset < item->offset)) {
        return BDM_CODE_CROSS_BOUND;
    }

    uint64_t relativeOffset = offset - item->offset;
    if (UNLIKELY(relativeOffset > item->totalSize || len > item->totalSize - relativeOffset)) {
        BDM_LOGERROR(0, "Disk io crosses region, bdmId(%u), device(%s), regionOffset(%llu), regionLength(%llu), "
            "ioOffset(%llu), ioLength(%llu).", item->bdmId, item->name, item->offset, item->totalSize, offset, len);
        return BDM_CODE_CROSS_BOUND;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskInnerReadWrite(BdmDiskItem *itemPtr, char *buff, uint64_t len, uint64_t offset, int32_t isRead)
{
    int32_t ret = BdmDiskCheckRegionRange(itemPtr, offset, len);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    // 磁盘读写失败最大允许重试3次.
    uint32_t retry = 0;
    while (retry <= BDM_IO_RETRY_NUM) {
        uint64_t rwLen = BdmDiskInnerReadWriteImpl(itemPtr->fd, buff, len, offset, isRead);
        if (LIKELY(rwLen == len)) {
            return BDM_CODE_OK;
        }
        usleep(10000); // 延迟10毫秒重试.
        retry++;
    }

    BdmDiskReportIoFault(itemPtr, offset, len);
    return BDM_CODE_ERR_IO;
}

static int32_t BdmDiskPrepareDirectBuffer(char *userBuf, uint64_t len, uint64_t offset, int32_t isRead, char **ioBuf,
    bool *needBounce);
static int32_t BdmDiskFinishDirectBuffer(char *userBuf, uint64_t len, int32_t isRead, char *ioBuf, bool needBounce,
    int32_t ioRet);
static int32_t BdmDiskFinishUringBuffer(BdmIoContext *bdmIo, int32_t ioRet);
static int32_t BdmDiskPreCheckFileLen(int32_t fd, uint64_t offset, uint64_t length);

int32_t BdmDiskInnerReadWriteDirect(BdmDiskItem *itemPtr, char *buff, uint64_t len, uint64_t offset, int32_t isRead)
{
    int32_t ret = BdmDiskCheckRegionRange(itemPtr, offset, len);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    char *ioBuf = NULL;
    bool needBounce = false;
    ret = BdmDiskPrepareDirectBuffer(buff, len, offset, isRead, &ioBuf, &needBounce);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    static uint64_t submitIndex = 0;
    uint64_t fdIdx = ATOMIC_INC(&submitIndex) % BDM_AYSNC_IO_FD_NUM;
    // 磁盘读写失败最大允许重试3次.
    uint32_t retry = 0;
    while (retry <= BDM_IO_RETRY_NUM) {
        uint64_t rwLen = BdmDiskInnerReadWriteImpl(itemPtr->asyncfd[fdIdx], ioBuf, len, offset, isRead);
        if (LIKELY(rwLen == len)) {
            return BdmDiskFinishDirectBuffer(buff, len, isRead, ioBuf, needBounce, BDM_CODE_OK);
        }
        usleep(10000); // 延迟10毫秒重试.
        retry++;
    }

    BdmDiskReportIoFault(itemPtr, offset, len);
    return BdmDiskFinishDirectBuffer(buff, len, isRead, ioBuf, needBounce, BDM_CODE_ERR_IO);
}

static bool BdmDiskIsRangeAligned(uint64_t len, uint64_t offset)
{
    return ((len % BDM_DIRECT_IO_ALIGN_SIZE) == 0 && (offset % BDM_DIRECT_IO_ALIGN_SIZE) == 0);
}

static bool BdmDiskCheckDirectRangeAligned(uint64_t len, uint64_t offset)
{
    if (LIKELY(BdmDiskIsRangeAligned(len, offset))) {
        return true;
    }

    BDM_LOGWARN(0, "direct io range is not aligned, len(%llu), offset(%llu), blockSize(%lu).", len, offset,
        BDM_DIRECT_IO_ALIGN_SIZE);
    return false;
}

static int32_t BdmDiskPrepareDirectBuffer(char *userBuf, uint64_t len, uint64_t offset, int32_t isRead, char **ioBuf,
    bool *needBounce)
{
    if (UNLIKELY(userBuf == NULL || ioBuf == NULL || needBounce == NULL)) {
        return BDM_CODE_INVALID_PARAM;
    }

    if (UNLIKELY(!BdmDiskCheckDirectRangeAligned(len, offset))) {
        return BDM_CODE_INVALID_PARAM;
    }

    if (LIKELY(((uintptr_t)userBuf % BDM_DIRECT_IO_ALIGN_SIZE) == 0)) {
        *ioBuf = userBuf;
        *needBounce = false;
        return BDM_CODE_OK;
    }

    void *alignedBuf = NULL;
    int32_t ret = posix_memalign(&alignedBuf, BDM_DIRECT_IO_ALIGN_SIZE, len);
    if (UNLIKELY(ret != 0 || alignedBuf == NULL)) {
        BDM_LOGERROR(0, "Alloc aligned io buffer failed, len(%llu), ret(%d).", len, ret);
        return BDM_CODE_ERR;
    }

    if (!isRead) {
        ret = memcpy_s(alignedBuf, len, userBuf, len);
        if (UNLIKELY(ret != EOK)) {
            BDM_LOGERROR(0, "Copy to aligned io buffer failed, len(%llu), ret(%d).", len, ret);
            free(alignedBuf);
            return BDM_CODE_ERR;
        }
    }

    *ioBuf = (char *)alignedBuf;
    *needBounce = true;
    return BDM_CODE_OK;
}

static int32_t BdmDiskFinishDirectBuffer(char *userBuf, uint64_t len, int32_t isRead, char *ioBuf, bool needBounce,
    int32_t ioRet)
{
    if (!needBounce) {
        return ioRet;
    }

    int32_t ret = ioRet;
    if (ioRet == BDM_CODE_OK && isRead) {
        ret = memcpy_s(userBuf, len, ioBuf, len);
        if (UNLIKELY(ret != EOK)) {
            BDM_LOGERROR(0, "Copy from aligned io buffer failed, len(%llu), ret(%d).", len, ret);
            ret = BDM_CODE_ERR;
        } else {
            ret = BDM_CODE_OK;
        }
    }
    free(ioBuf);
    return ret;
}

static int32_t BdmDiskFinishUringBuffer(BdmIoContext *bdmIo, int32_t ioRet)
{
    if (!bdmIo->needBounce) {
        return ioRet;
    }

    int32_t ret = ioRet;
    if (ioRet == BDM_CODE_OK && bdmIo->isRead) {
        ret = memcpy_s(bdmIo->buf, bdmIo->userLen, (char *)bdmIo->ioBuf + bdmIo->userOffset, bdmIo->userLen);
        if (UNLIKELY(ret != EOK)) {
            BDM_LOGERROR(0, "Copy from io_uring bounce buffer failed, len(%llu), offset(%llu), ret(%d).",
                bdmIo->userLen, bdmIo->userOffset, ret);
            ret = BDM_CODE_ERR;
        } else {
            ret = BDM_CODE_OK;
        }
    }
    free(bdmIo->ioBuf);
    return ret;
}

static int32_t BdmDiskPrepareUringBuffer(BdmIoContext *bdmIo, BdmDiskItem *item, uint64_t rwOffset)
{
    bdmIo->userLen = bdmIo->len;
    bdmIo->userOffset = 0;
    if (!bdmIo->isRead || BdmDiskIsRangeAligned(bdmIo->len, rwOffset)) {
        return BdmDiskPrepareDirectBuffer((char *)bdmIo->buf, bdmIo->len, rwOffset, bdmIo->isRead,
            (char **)&bdmIo->ioBuf, &bdmIo->needBounce);
    }

    uint64_t alignedOffset = rwOffset / BDM_DIRECT_IO_ALIGN_SIZE * BDM_DIRECT_IO_ALIGN_SIZE;
    uint64_t alignedEnd = (rwOffset + bdmIo->len + BDM_DIRECT_IO_ALIGN_SIZE - 1) / BDM_DIRECT_IO_ALIGN_SIZE *
        BDM_DIRECT_IO_ALIGN_SIZE;
    uint64_t alignedLen = alignedEnd - alignedOffset;
    uint64_t userOffset = rwOffset - alignedOffset;
    if (UNLIKELY(userOffset > bdmIo->offset || bdmIo->offset - userOffset > item->minChunkSize ||
                 alignedLen > item->minChunkSize - (bdmIo->offset - userOffset))) {
        BDM_LOGWARN(0, "io_uring over-read cross chunk boundary, chunkId(%lu), offset(%llu), len(%llu).",
            bdmIo->chunkId, bdmIo->offset, bdmIo->len);
        return BDM_CODE_INVALID_PARAM;
    }

    void *alignedBuf = NULL;
    int32_t ret = posix_memalign(&alignedBuf, BDM_DIRECT_IO_ALIGN_SIZE, alignedLen);
    if (UNLIKELY(ret != 0 || alignedBuf == NULL)) {
        BDM_LOGERROR(0, "Alloc io_uring over-read buffer failed, len(%llu), ret(%d).", alignedLen, ret);
        return BDM_CODE_ERR;
    }

    bdmIo->ioBuf = alignedBuf;
    bdmIo->needBounce = true;
    bdmIo->userOffset = userOffset;
    bdmIo->len = alignedLen;
    bdmIo->offset -= bdmIo->userOffset;
    BDM_LOGDEBUG(0, "Prepare io_uring over-read, chunkId(%lu), alignedLen(%llu), userLen(%llu), userOffset(%llu).",
        bdmIo->chunkId, bdmIo->len, bdmIo->userLen, bdmIo->userOffset);
    return BDM_CODE_OK;
}

static int32_t BdmDiskPrepareSyncUringBuffer(BdmDiskItem *itemPtr, char *userBuf, uint64_t len, uint64_t offset,
    int32_t isRead, BdmSyncUringBuffer *buffer)
{
    if (UNLIKELY(itemPtr == NULL || userBuf == NULL || buffer == NULL)) {
        return BDM_CODE_INVALID_PARAM;
    }

    buffer->ioBuf = NULL;
    buffer->ioLen = len;
    buffer->ioOffset = offset;
    buffer->userOffset = 0;
    buffer->needBounce = false;
    buffer->overRead = false;

    if (!isRead || BdmDiskIsRangeAligned(len, offset)) {
        return BdmDiskPrepareDirectBuffer(userBuf, len, offset, isRead, &buffer->ioBuf, &buffer->needBounce);
    }

    uint64_t dataStart = itemPtr->offset + itemPtr->dataOffset;
    if (UNLIKELY(offset < dataStart || itemPtr->minChunkSize == 0)) {
        BDM_LOGWARN(0, "Invalid io_uring over-read offset, offset(%llu), dataStart(%llu), minChunkSize(%llu).",
            offset, dataStart, itemPtr->minChunkSize);
        return BDM_CODE_INVALID_PARAM;
    }

    uint64_t alignedOffset = offset / BDM_DIRECT_IO_ALIGN_SIZE * BDM_DIRECT_IO_ALIGN_SIZE;
    uint64_t alignedEnd = (offset + len + BDM_DIRECT_IO_ALIGN_SIZE - 1) / BDM_DIRECT_IO_ALIGN_SIZE *
        BDM_DIRECT_IO_ALIGN_SIZE;
    uint64_t alignedLen = alignedEnd - alignedOffset;
    uint64_t userOffset = offset - alignedOffset;
    uint64_t chunkOffset = (offset - dataStart) % itemPtr->minChunkSize;
    if (UNLIKELY(userOffset > chunkOffset || alignedLen > itemPtr->minChunkSize - (chunkOffset - userOffset))) {
        BDM_LOGWARN(0, "io_uring sync over-read cross chunk boundary, offset(%llu), len(%llu), chunkOffset(%llu).",
            offset, len, chunkOffset);
        return BDM_CODE_INVALID_PARAM;
    }

    void *alignedBuf = NULL;
    int32_t ret = posix_memalign(&alignedBuf, BDM_DIRECT_IO_ALIGN_SIZE, alignedLen);
    if (UNLIKELY(ret != 0 || alignedBuf == NULL)) {
        BDM_LOGERROR(0, "Alloc sync io_uring over-read buffer failed, len(%llu), ret(%d).", alignedLen, ret);
        return BDM_CODE_ERR;
    }

    buffer->ioBuf = (char *)alignedBuf;
    buffer->ioLen = alignedLen;
    buffer->ioOffset = alignedOffset;
    buffer->userOffset = userOffset;
    buffer->needBounce = true;
    buffer->overRead = true;
    return BDM_CODE_OK;
}

static int32_t BdmDiskFinishSyncUringBuffer(char *userBuf, uint64_t userLen, int32_t isRead,
    BdmSyncUringBuffer *buffer, int32_t ioRet)
{
    if (buffer == NULL) {
        return ioRet;
    }

    if (!buffer->needBounce) {
        return ioRet;
    }

    int32_t ret = ioRet;
    if (ioRet == BDM_CODE_OK && isRead) {
        char *src = buffer->overRead ? buffer->ioBuf + buffer->userOffset : buffer->ioBuf;
        ret = memcpy_s(userBuf, userLen, src, userLen);
        if (UNLIKELY(ret != EOK)) {
            BDM_LOGERROR(0, "Copy from sync io_uring buffer failed, len(%llu), offset(%llu), ret(%d).",
                userLen, buffer->userOffset, ret);
            ret = BDM_CODE_ERR;
        } else {
            ret = BDM_CODE_OK;
        }
    }
    free(buffer->ioBuf);
    return ret;
}

static void BdmDiskReportIoFault(BdmDiskItem *itemPtr, uint64_t offset, uint64_t len)
{
    BDM_LOGWARN(0, "Report disk fault, bdmId(%u), device(%s), offset(%llu), len(%llu).", itemPtr->bdmId,
        itemPtr->name, offset, len);
    BdmSetDiskUsedStatus(itemPtr->bdmId, false);
    int32_t ret = BdmNotifyDiskFault((uint16_t)itemPtr->bdmId);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Report disk fault failed, bdmId(%u), device(%s).", itemPtr->bdmId, itemPtr->name);
    }
}

static void BdmDiskRegisterSyncUringCtx(BdmSyncUringCtx *uringCtx)
{
    pthread_mutex_lock(&g_bdmSyncUringCtxLock);
    if (!uringCtx->registered) {
        DListAddTail(&uringCtx->node, &g_bdmSyncUringCtxList);
        uringCtx->registered = true;
    }
    pthread_mutex_unlock(&g_bdmSyncUringCtxLock);
}

static void BdmDiskUnregisterSyncUringCtx(BdmSyncUringCtx *uringCtx)
{
    pthread_mutex_lock(&g_bdmSyncUringCtxLock);
    if (uringCtx->registered) {
        DListDel(&uringCtx->node);
        D_INIT_LIST_HEAD(&uringCtx->node);
        uringCtx->registered = false;
    }
    pthread_mutex_unlock(&g_bdmSyncUringCtxLock);
}

static void BdmDiskSyncUringDestroy(void *ctx)
{
    BdmSyncUringCtx *uringCtx = (BdmSyncUringCtx *)ctx;
    if (uringCtx == NULL) {
        return;
    }
    BdmDiskUnregisterSyncUringCtx(uringCtx);
    if (uringCtx->inited) {
        io_uring_queue_exit(&uringCtx->ring);
        uringCtx->inited = false;
    }
    free(uringCtx);
}

static int32_t BdmDiskResetSyncUringCtx(BdmSyncUringCtx *uringCtx)
{
    if (UNLIKELY(uringCtx == NULL)) {
        return BDM_CODE_INVALID_PARAM;
    }
    if (uringCtx->inited) {
        io_uring_queue_exit(&uringCtx->ring);
        uringCtx->inited = false;
    }
    int32_t ret = io_uring_queue_init(BDM_SYNC_URING_QUEUE_DEPTH, &uringCtx->ring, 0);
    if (UNLIKELY(ret != 0)) {
        BDM_LOGWARN(0, "Reset sync io_uring queue failed, ret(%d), errno(%s).", ret, strerror(-ret));
        return BDM_CODE_ERR_IO;
    }
    uringCtx->inited = true;
    return BDM_CODE_OK;
}

static void BdmDiskSyncUringCreateKey(void)
{
    int32_t ret = pthread_key_create(&g_bdmSyncUringKey, BdmDiskSyncUringDestroy);
    if (UNLIKELY(ret != 0)) {
        BDM_LOGWARN(0, "Create sync io_uring pthread key failed, ret(%d).", ret);
        g_bdmSyncUringKeyRet = BDM_CODE_ERR;
        return;
    }
    g_bdmSyncUringKeyCreated = true;
}

static void BdmDiskCleanupCurrentSyncUringCtx(void)
{
    if (!g_bdmSyncUringKeyCreated || g_bdmSyncUringKeyRet != BDM_CODE_OK) {
        return;
    }

    BdmSyncUringCtx *uringCtx = (BdmSyncUringCtx *)pthread_getspecific(g_bdmSyncUringKey);
    if (uringCtx == NULL) {
        return;
    }
    (void)pthread_setspecific(g_bdmSyncUringKey, NULL);
    BdmDiskSyncUringDestroy(uringCtx);
}

static void BdmDiskCleanupAllSyncUringCtxs(void)
{
    pthread_mutex_lock(&g_bdmSyncUringCtxLock);
    DList *pos = NULL;
    D_LIST_FOR_DEL_EACH(pos, &g_bdmSyncUringCtxList) {
        BdmSyncUringCtx *uringCtx = D_LIST_ENTRY(pos, BdmSyncUringCtx, node);
        DListDel(&uringCtx->node);
        D_INIT_LIST_HEAD(&uringCtx->node);
        uringCtx->registered = false;
        if (uringCtx->inited) {
            io_uring_queue_exit(&uringCtx->ring);
            uringCtx->inited = false;
        }
    }
    pthread_mutex_unlock(&g_bdmSyncUringCtxLock);
}

static BdmSyncUringCtx *BdmDiskGetSyncUringCtx(void)
{
    pthread_once(&g_bdmSyncUringKeyOnce, BdmDiskSyncUringCreateKey);
    if (UNLIKELY(g_bdmSyncUringKeyRet != BDM_CODE_OK)) {
        return NULL;
    }

    BdmSyncUringCtx *uringCtx = (BdmSyncUringCtx *)pthread_getspecific(g_bdmSyncUringKey);
    if (LIKELY(uringCtx != NULL)) {
        if (LIKELY(uringCtx->inited)) {
            return uringCtx;
        }
        (void)pthread_setspecific(g_bdmSyncUringKey, NULL);
        BdmDiskSyncUringDestroy(uringCtx);
    }

    uringCtx = (BdmSyncUringCtx *)calloc(1, sizeof(BdmSyncUringCtx));
    if (UNLIKELY(uringCtx == NULL)) {
        BDM_LOGWARN(0, "Allocate sync io_uring context failed.");
        return NULL;
    }
    D_INIT_LIST_HEAD(&uringCtx->node);

    int32_t ret = io_uring_queue_init(BDM_SYNC_URING_QUEUE_DEPTH, &uringCtx->ring, 0);
    if (UNLIKELY(ret != 0)) {
        BDM_LOGWARN(0, "sync io_uring queue init failed, ret(%d).", ret);
        free(uringCtx);
        return NULL;
    }
    uringCtx->inited = true;

    ret = pthread_setspecific(g_bdmSyncUringKey, uringCtx);
    if (UNLIKELY(ret != 0)) {
        BDM_LOGWARN(0, "Set sync io_uring pthread key failed, ret(%d).", ret);
        BdmDiskSyncUringDestroy(uringCtx);
        return NULL;
    }
    BdmDiskRegisterSyncUringCtx(uringCtx);
    return uringCtx;
}

static uint64_t BdmDiskNextPow2(uint64_t value)
{
    if (value <= 1UL) {
        return 1UL;
    }

    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return value + 1UL;
}

static uint64_t BdmDiskCalcUringChunk(uint64_t remain)
{
    uint64_t chunk = BdmDiskNextPow2((remain + BDM_SYNC_URING_QUEUE_DEPTH - 1UL) / BDM_SYNC_URING_QUEUE_DEPTH);
    if (chunk < BDM_SYNC_URING_MIN_CHUNK_SIZE) {
        chunk = BDM_SYNC_URING_MIN_CHUNK_SIZE;
    }
    if (chunk > remain) {
        chunk = remain;
    }
    return chunk;
}

static int32_t BdmDiskSubmitAllUring(struct io_uring *ring, uint32_t submitNum, uint32_t *submittedOut, bool isSqpoll)
{
    uint32_t submitted = 0;
    uint32_t noProgressRetry = 0;
    while (submitted < submitNum) {
        int32_t ret = io_uring_submit(ring);
        if (UNLIKELY(ret < 0)) {
            if (noProgressRetry == 0 || (noProgressRetry % BDM_URING_SQE_RETRY_NUM) == 0) {
                BDM_LOGWARN(0, "io_uring_submit failed, ret(%d), submitted(%u), expected(%u), errno(%s).", ret,
                    submitted, submitNum, strerror(-ret));
            }
            noProgressRetry++;
            if (noProgressRetry >= BDM_URING_SQE_RETRY_NUM) {
                if (submittedOut != NULL) {
                    *submittedOut = submitted;
                }
                return BDM_CODE_ERR;
            }
            usleep(BDM_URING_SQE_RETRY_INTERVAL_US);
            continue;
        }
        if (isSqpoll) {
            /* SQPOLL may consume SQEs concurrently; non-negative submit means prepared SQEs belong to the ring. */
            if (submittedOut != NULL) {
                *submittedOut = submitNum;
            }
            return BDM_CODE_OK;
        }
        if (UNLIKELY(ret == 0)) {
            if (noProgressRetry == 0 || (noProgressRetry % BDM_URING_SQE_RETRY_NUM) == 0) {
                BDM_LOGWARN(0, "io_uring_submit made no progress, submitted(%u), expected(%u).", submitted,
                    submitNum);
            }
            noProgressRetry++;
            if (noProgressRetry >= BDM_URING_SQE_RETRY_NUM) {
                if (submittedOut != NULL) {
                    *submittedOut = submitted;
                }
                return BDM_CODE_ERR;
            }
            usleep(BDM_URING_SQE_RETRY_INTERVAL_US);
            continue;
        }
        submitted += (uint32_t)ret;
        noProgressRetry = 0;
    }

    if (submittedOut != NULL) {
        *submittedOut = submitted;
    }
    return BDM_CODE_OK;
}

static void BdmDiskDrainSyncUringCqes(struct io_uring *ring, uint32_t drainNum)
{
    for (uint32_t i = 0; i < drainNum; i++) {
        struct io_uring_cqe *cqe = NULL;
        int32_t ret;
        do {
            ret = io_uring_wait_cqe(ring, &cqe);
        } while (ret == -EINTR);
        if (UNLIKELY(ret != 0 || cqe == NULL)) {
            BDM_LOGWARN(0, "Drain sync io_uring cqe failed, ret(%d), remain(%u).", ret, drainNum - i);
            return;
        }
        io_uring_cqe_seen(ring, cqe);
    }
}

static int32_t BdmDiskSubmitWaitUring(int32_t fd, char *buff, uint64_t len, uint64_t offset, int32_t isRead)
{
    BdmSyncUringCtx *uringCtx = BdmDiskGetSyncUringCtx();
    if (UNLIKELY(uringCtx == NULL || !uringCtx->inited)) {
        return BDM_CODE_ERR_IO;
    }
    struct io_uring *ring = &uringCtx->ring;

    uint64_t remain = len;
    while (remain > 0) {
        uint64_t chunkSize = BdmDiskCalcUringChunk(remain);
        uint32_t submitNum = 0;
        uint64_t submitLens[BDM_SYNC_URING_QUEUE_DEPTH] = {0};
        while (remain > 0 && submitNum < BDM_SYNC_URING_QUEUE_DEPTH) {
            uint64_t done = len - remain;
            uint64_t chunk = remain < chunkSize ? remain : chunkSize;
            struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
            if (UNLIKELY(sqe == NULL)) {
                (void)BdmDiskResetSyncUringCtx(uringCtx);
                return BDM_CODE_ERR_IO;
            }

            if (isRead) {
                io_uring_prep_read(sqe, fd, buff + done, chunk, offset + done);
            } else {
                io_uring_prep_write(sqe, fd, buff + done, chunk, offset + done);
            }

            remain -= chunk;
            submitLens[submitNum] = chunk;
            submitNum++;
        }

        uint32_t submitted = 0;
        int32_t ret = BdmDiskSubmitAllUring(ring, submitNum, &submitted, false);
        if (UNLIKELY(ret != BDM_CODE_OK && submitted == 0)) {
            (void)BdmDiskResetSyncUringCtx(uringCtx);
            return ret;
        }
        bool needResetRing = false;
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BDM_LOGWARN(0, "io_uring partial sync submit, submitted(%u), expected(%u).", submitted, submitNum);
            for (uint32_t i = submitted; i < submitNum; i++) {
                remain += submitLens[i];
            }
            submitNum = submitted;
            needResetRing = true;
        }

        uint64_t submitBytes = 0;
        for (uint32_t i = 0; i < submitNum; i++) {
            submitBytes += submitLens[i];
        }
        uint64_t doneBytes = 0;
        for (uint32_t i = 0; i < submitNum; i++) {
            struct io_uring_cqe *cqe = NULL;
            do {
                ret = io_uring_wait_cqe(ring, &cqe);
            } while (ret == -EINTR);
            if (UNLIKELY(ret != 0 || cqe == NULL || cqe->res <= 0)) {
                BDM_LOGWARN(0, "io_uring wait cqe failed, ret(%d), res(%d).", ret, cqe == NULL ? 0 : cqe->res);
                uint32_t completedNum = i;
                if (cqe != NULL) {
                    io_uring_cqe_seen(ring, cqe);
                    completedNum++;
                }
                if (completedNum < submitNum) {
                    BdmDiskDrainSyncUringCqes(ring, submitNum - completedNum);
                }
                (void)BdmDiskResetSyncUringCtx(uringCtx);
                return BDM_CODE_ERR_IO;
            }
            doneBytes += (uint64_t)cqe->res;
            io_uring_cqe_seen(ring, cqe);
        }
        if (UNLIKELY(doneBytes != submitBytes)) {
            BDM_LOGWARN(0, "io_uring partial io, need(%llu), done(%llu).", submitBytes, doneBytes);
            (void)BdmDiskResetSyncUringCtx(uringCtx);
            return BDM_CODE_ERR_IO;
        }
        if (needResetRing) {
            int32_t resetRet = BdmDiskResetSyncUringCtx(uringCtx);
            if (UNLIKELY(resetRet != BDM_CODE_OK)) {
                return resetRet;
            }
            ring = &uringCtx->ring;
        }
    }
    return BDM_CODE_OK;
}

static int32_t BdmDiskInnerReadWriteUring(BdmDiskItem *itemPtr, char *buff, uint64_t len, uint64_t offset,
    int32_t isRead)
{
    BdmSyncUringBuffer buffer;
    int32_t ret = BdmDiskPrepareSyncUringBuffer(itemPtr, buff, len, offset, isRead, &buffer);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }
    ret = BdmDiskCheckRegionRange(itemPtr, buffer.ioOffset, buffer.ioLen);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return BdmDiskFinishSyncUringBuffer(buff, len, isRead, &buffer, ret);
    }

    static uint64_t submitIndex = 0;
    uint64_t fdIdx = ATOMIC_INC(&submitIndex) % BDM_AYSNC_IO_FD_NUM;
    int32_t fd = itemPtr->asyncfd[fdIdx];

    uint32_t retry = 0;
    while (retry <= BDM_IO_RETRY_NUM) {
        ret = BdmDiskSubmitWaitUring(fd, buffer.ioBuf, buffer.ioLen, buffer.ioOffset, isRead);
        if (LIKELY(ret == BDM_CODE_OK)) {
            return BdmDiskFinishSyncUringBuffer(buff, len, isRead, &buffer, BDM_CODE_OK);
        }
        usleep(10000); // 延迟10毫秒重试.
        retry++;
    }

    if (ret != BDM_CODE_ERR_IO) {
        return BdmDiskFinishSyncUringBuffer(buff, len, isRead, &buffer, ret);
    }
    BdmDiskReportIoFault(itemPtr, buffer.ioOffset, buffer.ioLen);
    return BdmDiskFinishSyncUringBuffer(buff, len, isRead, &buffer, BDM_CODE_ERR_IO);
}

int32_t BdmDiskWriteMeta(uintptr_t itemPtr, uint64_t offset, void *buf, uint64_t len)
{
    BdmDiskItem *item = (BdmDiskItem *)itemPtr;
    uint64_t rwOffset = item->offset + item->metaOffset + offset;
    int ret = BdmDiskInnerReadWrite(item, (char *)buf, len, rwOffset, FALSE);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Write disk meta failed, need(%lu) device(%s).", len, item->name);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskReadMeta(uintptr_t itemPtr, uint64_t offset, void *buf, uint64_t len)
{
    BdmDiskItem *item = (BdmDiskItem *)itemPtr;
    uint64_t rwOffset = item->offset + item->metaOffset + offset;
    int ret = BdmDiskInnerReadWrite(item, (char *)buf, len, rwOffset, TRUE);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Read disk meta failed, need(%lu) device(%s).", len, item->name);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskAlloc(uintptr_t objPtr, uint64_t bucketId, uint64_t bucketOffset, uint64_t len, uint64_t *chunkId)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    if (UNLIKELY(len == 0 || item->minChunkSize == 0 || len % item->minChunkSize != 0)) {
        BDM_LOGWARN(0, "Invalid len, bdm id(%u) length(%lu).", obj->bdmId, len);
        return BDM_CODE_INVALID_PARAM;
    }

    int32_t ret = BDM_CODE_OK;
    BIO_TP_START(BDM_ALLOC_BLOCK_FAIL, &ret, BDM_CODE_ERR);
    ret = BdmAllocatorAllocChunk(item->allocator, bucketId, bucketOffset, len, chunkId);
    BIO_TP_END;
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Alloc chunk failed, bdm id(%u) length(%lu).", obj->bdmId, len);
        return ret;
    }

    return BDM_CODE_OK;
}

int32_t BdmDiskFree(uintptr_t objPtr, uint64_t len, uint64_t chunkId)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    int32_t ret = BdmAllocatorFreeChunk(item->allocator, len, chunkId);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Free chunk failed, bdm id(%u) length(%lu).", obj->bdmId, len);
        return ret;
    }

    return BDM_CODE_OK;
}

int32_t BdmDiskParseChunkId(uintptr_t objPtr, uint64_t chunkId, uint64_t *offset, char *path)
{
    if (UNLIKELY(path == NULL)) {
        BDM_LOGERROR(0, "Path is null.");
        return BDM_CODE_INVALID_PARAM;
    }
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    int32_t ret = BdmAllocatorCheckChunk(item->allocator, chunkId, 0, 0);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Bdm read check failed, bdm id(%u) chunk id(%llu) ret(%d).", obj->bdmId, chunkId, ret);
        return ret;
    }

    *offset = item->offset + item->dataOffset + item->minChunkSize * chunkId;
    ret = memcpy_s(path, BDM_NAME_LEN, item->name, BDM_NAME_LEN);
    path[BDM_NAME_LEN - 1] = '\0';
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Memcpy bdm name failed, name(%s).", item->name);
        return BDM_CODE_ERR;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskRead(uintptr_t objPtr, uint64_t chunkId, uint64_t offset, void *buf, uint64_t len)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    int32_t ret = BdmAllocatorCheckChunk(item->allocator, chunkId, offset, len);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Bdm read check chunk failed, bdm id(%u) chunk id(%lu) ret(%d).", obj->bdmId, chunkId, ret);
        return ret;
    }

    uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * chunkId + offset;
    if (g_bdmIoEngine == BDM_IO_ENGINE_IO_URING) {
        ret = BdmDiskInnerReadWriteUring(item, (char *)buf, len, rwOffset, TRUE);
    } else if (BdmDiskIsRangeAligned(len, rwOffset)) {
        ret = BdmDiskInnerReadWriteDirect(item, (char *)buf, len, rwOffset, TRUE);
    } else {
        ret = BdmDiskInnerReadWrite(item, (char *)buf, len, rwOffset, TRUE);
    }
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Read disk failed, need(%lu) device(%s).", len, item->name);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskWrite(uintptr_t objPtr, uint64_t chunkId, uint64_t offset, void *buf, uint64_t len)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    int32_t ret = BdmAllocatorCheckChunk(item->allocator, chunkId, offset, len);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Bdm write check chunk failed, bdm id(%u) chunk id(%lu) ret(%d).", obj->bdmId, chunkId, ret);
        return ret;
    }

    uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * chunkId + offset;
    if (g_bdmIoEngine == BDM_IO_ENGINE_IO_URING && BdmDiskIsRangeAligned(len, rwOffset)) {
        ret = BdmDiskInnerReadWriteUring(item, (char *)buf, len, rwOffset, FALSE);
    } else if (BdmDiskIsRangeAligned(len, rwOffset)) {
        ret = BdmDiskInnerReadWriteDirect(item, (char *)buf, len, rwOffset, FALSE);
    } else {
        ret = BdmDiskInnerReadWrite(item, (char *)buf, len, rwOffset, FALSE);
    }
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Write disk failed, need(%lu) device(%s).", len, item->name);
        return ret;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskAllocatorReset(uintptr_t objPtr)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    int32_t ret = BdmAllocatorResetChunk(item->allocator);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Bdm reset chunk failed, bdm id(%u) ret(%d).", obj->bdmId, ret);
    }
    return ret;
}

int32_t BdmDiskGetNextChunk(uintptr_t objPtr, uint64_t *chunkId, uint64_t *chunkSize, uint64_t *bucketId,
    uint64_t *bucketOffset)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    int32_t ret = BdmAllocatorGetNextChunk(item->allocator, chunkId, chunkSize, bucketId, bucketOffset);
    if (UNLIKELY(ret != BDM_CODE_OK && ret != BDM_CODE_SCAN_OFF)) {
        BDM_LOGWARN(0, "Bdm get next chunk failed, bdm id(%u) ret(%d).", obj->bdmId, ret);
    }
    return ret;
}

int32_t BdmDiskGetCap(uintptr_t objPtr, uint64_t *totalSize, uint64_t *usedSize)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    int32_t ret = BdmAllocatorGetCap(item->allocator, totalSize, usedSize);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Bdm get cap failed, bdm id(%u) ret(%d).", obj->bdmId, ret);
    }
    return ret;
}

static int32_t BdmDiskAcquireUringSlot(BdmIoContext *bdmIo, BdmThreadPool *bdmPool, uint32_t threadIdx)
{
    if (bdmIo->slotAcquired) {
        return BDM_CODE_OK;
    }

    while (sem_wait(&bdmPool->ringSlots[threadIdx]) != 0) {
        if (errno == EINTR) {
            continue;
        }
        BDM_LOGERROR(0, "Wait io_uring slot failed, threadIdx(%u), errno(%s).", threadIdx, strerror(errno));
        return BDM_CODE_ERR_IO;
    }
    bdmIo->slotAcquired = true;
    (void)__sync_add_and_fetch(&bdmPool->ringInflight[threadIdx], 1);
    return BDM_CODE_OK;
}

static void BdmDiskReleaseUringSlot(BdmIoContext *bdmIo, BdmThreadPool *bdmPool, uint32_t threadIdx)
{
    if (!bdmIo->slotAcquired) {
        return;
    }
    bdmIo->slotAcquired = false;
    (void)__sync_sub_and_fetch(&bdmPool->ringInflight[threadIdx], 1);
    if (UNLIKELY(sem_post(&bdmPool->ringSlots[threadIdx]) != 0)) {
        BDM_LOGERROR(0, "Release io_uring slot failed, threadIdx(%u), errno(%s).", threadIdx, strerror(errno));
    }
}

static uint32_t BdmDiskGetUringInflight(BdmThreadPool *bdmPool, uint32_t threadIdx)
{
    return __sync_add_and_fetch(&bdmPool->ringInflight[threadIdx], 0);
}

static void BdmDiskCancelPreparedSqe(struct io_uring_sqe *sqe)
{
    if (sqe == NULL) {
        return;
    }
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, NULL);
}

static int32_t BdmDiskPrepareOneUring(BdmIoContext *bdmIo, BdmThreadCtx *threadCtx, struct io_uring_sqe **sqeOut)
{
    BdmThreadPool *bdmPool = (BdmThreadPool *)threadCtx->ctx;
    uint32_t threadIdx = threadCtx->index;
    struct io_uring *ring = &bdmPool->ring[threadIdx];
    BdmDiskItem *item = (BdmDiskItem *)bdmIo->item;
    uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * bdmIo->chunkId + bdmIo->offset;
    int32_t ret = BdmDiskCheckRegionRange(item, rwOffset, bdmIo->len);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }
    if (UNLIKELY(!BdmDiskCheckDirectRangeAligned(bdmIo->len, rwOffset))) {
        return BDM_CODE_INVALID_PARAM;
    }

    static uint64_t submitIndex = 0;
    uint64_t fdIdx = ATOMIC_INC(&submitIndex) % BDM_AYSNC_IO_FD_NUM;
    int32_t fd = item->asyncfd[fdIdx];

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (UNLIKELY(sqe == NULL)) {
        BDM_LOGERROR(0, "No available io_uring sqe, chunkId(%lu), len(%lu).", bdmIo->chunkId, bdmIo->len);
        return BDM_CODE_ERR_IO;
    }

    if (bdmIo->isRead) {
        io_uring_prep_read(sqe, fd, bdmIo->ioBuf, bdmIo->len, rwOffset);
    } else {
        io_uring_prep_write(sqe, fd, bdmIo->ioBuf, bdmIo->len, rwOffset);
    }
    io_uring_sqe_set_data(sqe, bdmIo);
    if (sqeOut != NULL) {
        *sqeOut = sqe;
    }
    return BDM_CODE_OK;
}

static int32_t BdmDiskSubmitOneUring(BdmIoContext *bdmIo, BdmThreadCtx *threadCtx)
{
    BdmThreadPool *bdmPool = (BdmThreadPool *)threadCtx->ctx;
    uint32_t threadIdx = threadCtx->index;
    struct io_uring *ring = &bdmPool->ring[threadIdx];

    int32_t ret = BdmDiskAcquireUringSlot(bdmIo, bdmPool, threadIdx);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    pthread_mutex_lock(&bdmPool->ringLock[threadIdx]);

    struct io_uring_sqe *sqe = NULL;
    ret = BdmDiskPrepareOneUring(bdmIo, threadCtx, &sqe);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BdmDiskReleaseUringSlot(bdmIo, bdmPool, threadIdx);
        pthread_mutex_unlock(&bdmPool->ringLock[threadIdx]);
        return ret;
    }

    uint32_t submitted = 0;
    ret = BdmDiskSubmitAllUring(ring, 1, &submitted, bdmPool->ringSqpoll[threadIdx]);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "io_uring_submit one failed, submitted(%u).", submitted);
        if (submitted == 0) {
            BdmDiskCancelPreparedSqe(sqe);
        }
        BdmDiskReleaseUringSlot(bdmIo, bdmPool, threadIdx);
        pthread_mutex_unlock(&bdmPool->ringLock[threadIdx]);
        return BDM_CODE_ERR_IO;
    }
    pthread_mutex_unlock(&bdmPool->ringLock[threadIdx]);
    return BDM_CODE_OK;
}

static int32_t BdmDiskRetryUring(BdmIoContext *bdmIo, BdmThreadCtx *threadCtx)
{
    BdmThreadPool *bdmPool = (BdmThreadPool *)threadCtx->ctx;
    uint32_t threadIdx = threadCtx->index;
    if (!bdmPool->stopping[threadIdx] && bdmPool->pool[threadIdx] != NULL) {
        return BdmThreadPoolAdd(bdmPool->pool[threadIdx], NULL, (void *)bdmIo);
    }
    return BdmDiskSubmitOneUring(bdmIo, threadCtx);
}

static uint32_t BdmDiskGetEagainBackoffUs(uint32_t retryNum)
{
    uint64_t backoff = BDM_URING_EAGAIN_BACKOFF_BASE_US;
    uint32_t shift = retryNum > 6 ? 6 : retryNum;
    backoff <<= shift;
    if (backoff > BDM_URING_EAGAIN_BACKOFF_MAX_US) {
        backoff = BDM_URING_EAGAIN_BACKOFF_MAX_US;
    }
    return (uint32_t)backoff;
}

static void BdmDiskCompleteUringContext(BdmIoContext *bdmIo, int32_t ret)
{
    ret = BdmDiskFinishUringBuffer(bdmIo, ret);
    bdmIo->ioBuf = bdmIo->buf;
    bdmIo->needBounce = false;
    HTRACER_C_DELAY_END(bdmIo->traceId, bdmIo->traceStartNs, ret);
    bdmIo->cb(bdmIo->ctx, ret);
}

static void BdmDiskCleanupPreparedAsyncContext(BdmIoContext *bdmIo, int32_t ret)
{
    (void)BdmDiskFinishUringBuffer(bdmIo, ret);
    bdmIo->ioBuf = bdmIo->buf;
    bdmIo->needBounce = false;
    HTRACER_C_DELAY_END(bdmIo->traceId, bdmIo->traceStartNs, ret);
}

static bool BdmDiskShouldStopEvents(BdmThreadPool *bdmPool, uint32_t threadIdx)
{
    return bdmPool->stopping[threadIdx] && BdmDiskGetUringInflight(bdmPool, threadIdx) == 0;
}

static int32_t BdmDiskSubmitUring(void **argList, uint32_t argNum, void *ctx)
{
    BdmThreadCtx *threadCtx = (BdmThreadCtx *)ctx;
    BdmThreadPool *bdmPool = (BdmThreadPool *)threadCtx->ctx;
    uint32_t threadIdx = threadCtx->index;
    struct io_uring *ring = &bdmPool->ring[threadIdx];
    BdmIoContext *submitted[BDM_BATCH_HANDLE_NUM];
    struct io_uring_sqe *submittedSqes[BDM_BATCH_HANDLE_NUM];
    BdmIoContext *ready[BDM_BATCH_HANDLE_NUM];
    uint32_t submittedNum = 0;
    uint32_t readyNum = 0;
    int32_t finalRet = BDM_CODE_OK;

    for (uint32_t i = 0; i < argNum; i++) {
        BdmIoContext *bdmIo = (BdmIoContext *)argList[i];
        int32_t ret = BdmDiskAcquireUringSlot(bdmIo, bdmPool, threadIdx);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BdmDiskCompleteUringContext(bdmIo, ret);
            finalRet = ret;
            continue;
        }
        if (readyNum < BDM_BATCH_HANDLE_NUM) {
            ready[readyNum++] = bdmIo;
        }
    }
    if (readyNum == 0) {
        return finalRet;
    }

    for (uint32_t i = 0; i < readyNum; i++) {
        BdmIoContext *bdmIo = ready[i];
        struct io_uring_sqe *sqe = NULL;
        int32_t ret = BdmDiskPrepareOneUring(bdmIo, threadCtx, &sqe);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BDM_LOGERROR(0, "Submit bdm io_uring failed, chunkId(%lu), len(%lu).", bdmIo->chunkId, bdmIo->len);
            BdmDiskReleaseUringSlot(bdmIo, bdmPool, threadIdx);
            BdmDiskCompleteUringContext(bdmIo, ret);
            finalRet = ret;
            continue;
        }
        if (submittedNum < BDM_BATCH_HANDLE_NUM) {
            submitted[submittedNum++] = bdmIo;
            submittedSqes[submittedNum - 1] = sqe;
        }
    }

    if (submittedNum == 0) {
        return finalRet;
    }

    uint32_t realSubmitted = 0;
    int32_t ret =
        BdmDiskSubmitAllUring(ring, submittedNum, &realSubmitted, bdmPool->ringSqpoll[threadIdx]);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "io_uring_submit batch failed, submitted(%u), expected(%u), argNum(%u).", realSubmitted,
            submittedNum, argNum);
        for (uint32_t i = realSubmitted; i < submittedNum; i++) {
            BdmDiskCancelPreparedSqe(submittedSqes[i]);
            BdmDiskReleaseUringSlot(submitted[i], bdmPool, threadIdx);
            BdmDiskCompleteUringContext(submitted[i], BDM_CODE_ERR_IO);
        }
        return BDM_CODE_ERR_IO;
    }
    return BDM_CODE_OK;
}

static bool BdmCompleteUringHandler(const struct io_uring_cqe *cqe, BdmThreadCtx *threadCtx)
{
    BdmThreadPool *bdmPool = (BdmThreadPool *)threadCtx->ctx;
    if (bdmPool->stopping[threadCtx->index] && io_uring_cqe_get_data(cqe) == NULL) {
        return BdmDiskShouldStopEvents(bdmPool, threadCtx->index);
    }

    BdmIoContext *bdmIo = (BdmIoContext *)io_uring_cqe_get_data(cqe);
    if (UNLIKELY(bdmIo == NULL)) {
        return false;
    }

    if (UNLIKELY(cqe->res != (int32_t)bdmIo->len)) {
        if (cqe->res == -EAGAIN) {
            if (bdmIo->eagainRetryNum < BDM_URING_EAGAIN_RETRY_NUM) {
                uint32_t backoffUs = BdmDiskGetEagainBackoffUs(bdmIo->eagainRetryNum);
                BdmDiskItem *item = (BdmDiskItem *)bdmIo->item;
                uint32_t threadIdx = threadCtx->index;
                uint64_t rwOffset = item == NULL ? 0 :
                    item->offset + item->dataOffset + item->minChunkSize * bdmIo->chunkId + bdmIo->offset;
                if (bdmIo->eagainRetryNum == 0 || (bdmIo->eagainRetryNum % 16) == 15) {
                    BDM_LOGWARN(0,
                        "retry bdm io_uring after EAGAIN, pid(%d), rw(%s), bdmId(%u), device(%s), threadIdx(%u), "
                        "chunkId(%lu), offset(%lu), len(%lu), retry(%u), backoffUs(%u).",
                        getpid(), bdmIo->isRead ? "read" : "write", item == NULL ? UINT32_MAX : item->bdmId,
                        item == NULL ? "unknown" : item->name, threadIdx, bdmIo->chunkId, rwOffset, bdmIo->len,
                        bdmIo->eagainRetryNum + 1, backoffUs);
                }
                bdmIo->eagainRetryNum++;
                usleep(backoffUs);
                int32_t retryRet = BdmDiskRetryUring(bdmIo, threadCtx);
                if (retryRet == BDM_CODE_OK) {
                    return false;
                }
                BDM_LOGWARN(0,
                    "resubmit bdm io_uring after EAGAIN failed, ret(%d), pid(%d), rw(%s), bdmId(%u), device(%s), "
                    "threadIdx(%u), chunkId(%lu), offset(%lu), len(%lu), retry(%u).",
                    retryRet, getpid(), bdmIo->isRead ? "read" : "write", item == NULL ? UINT32_MAX : item->bdmId,
                    item == NULL ? "unknown" : item->name, threadIdx, bdmIo->chunkId, rwOffset, bdmIo->len,
                    bdmIo->eagainRetryNum);
            }

            BdmDiskItem *item = (BdmDiskItem *)bdmIo->item;
            BDM_LOGERROR(0,
                "bdm io_uring EAGAIN retry exhausted, bdmId(%u), device(%s), chunkId(%lu), len(%lu), retry(%u).",
                item->bdmId, item->name, bdmIo->chunkId, bdmIo->len, bdmIo->eagainRetryNum);
            BdmDiskReleaseUringSlot(bdmIo, bdmPool, threadCtx->index);
            BdmDiskCompleteUringContext(bdmIo, BDM_CODE_ERR_IO);
            return BdmDiskShouldStopEvents(bdmPool, threadCtx->index);
        }

        if (bdmIo->retryNum < BDM_IO_RETRY_NUM) {
            BDM_LOGWARN(0, "retry bdm io_uring, chunkId(%lu), len(%lu), res(%d).", bdmIo->chunkId, bdmIo->len,
                cqe->res);
            bdmIo->retryNum++;
            if (BdmDiskRetryUring(bdmIo, threadCtx) == BDM_CODE_OK) {
                return false;
            }
        }

        BdmDiskItem *item = (BdmDiskItem *)bdmIo->item;
        uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * bdmIo->chunkId + bdmIo->offset;
        BDM_LOGERROR(0, "bdm io_uring failed, bdmId(%u), device(%s), chunkId(%lu), len(%lu), res(%d).", item->bdmId,
            item->name, bdmIo->chunkId, bdmIo->len, cqe->res);
        BdmDiskReportIoFault(item, rwOffset, bdmIo->len);
        BdmDiskReleaseUringSlot(bdmIo, bdmPool, threadCtx->index);
        BdmDiskCompleteUringContext(bdmIo, BDM_CODE_ERR_IO);
        return BdmDiskShouldStopEvents(bdmPool, threadCtx->index);
    }

    BdmDiskReleaseUringSlot(bdmIo, bdmPool, threadCtx->index);
    BdmDiskCompleteUringContext(bdmIo, BDM_CODE_OK);
    return BdmDiskShouldStopEvents(bdmPool, threadCtx->index);
}

static int32_t BdmDiskPrepareAsyncContext(BdmAsyncOpsReq *req, bool isRead, bool useUring, BdmIoContext **ioOut)
{
    BdmObj *obj = (BdmObj *)req->objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    int32_t ret = BdmAllocatorCheckChunk(item->allocator, req->chunkId, req->offset, req->len);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGWARN(0, "Bdm read check failed, bdmId(%u) chunkId(%lu) ret(%d).", obj->bdmId, req->chunkId, ret);
        return ret;
    }

    if (UNLIKELY(req->ioCtx == NULL || req->ioCtx->cb == NULL)) {
        return BDM_CODE_INVALID_PARAM;
    }

    static size_t ctxLen = sizeof(BdmIoContext);
    if (UNLIKELY(ctxLen > BDM_IO_CTX_RES_LEN)) {
        BDM_LOGWARN(0, "Impossible, need len(%u).", ctxLen);
        return BDM_CODE_ERR;
    }

    BdmIoContext *bdmIo = (BdmIoContext *)req->ioCtx->res;
    if (UNLIKELY(bdmIo == NULL)) {
        BDM_LOGWARN(0, "Malloc iocontext failed.");
        return BDM_CODE_ERR;
    }

    bdmIo->buf = req->buf;
    bdmIo->ioBuf = req->buf;
    bdmIo->len = req->len;
    bdmIo->userLen = req->len;
    bdmIo->userOffset = 0;
    bdmIo->chunkId = req->chunkId;
    bdmIo->offset = req->offset;
    bdmIo->retryNum = 0;
    bdmIo->eagainRetryNum = 0;
    bdmIo->isRead = isRead;
    bdmIo->needBounce = false;
    bdmIo->slotAcquired = false;
    bdmIo->traceId = useUring ? (isRead ? BDM_DISK_TRACE_READ_ASYNC_C : BDM_DISK_TRACE_WRITE_ASYNC_C) :
                               (isRead ? BDM_DISK_TRACE_READ_SYNC_C : BDM_DISK_TRACE_WRITE_SYNC_C);
    bdmIo->traceStartNs = 0;
    if (HTracerIsEnableC()) {
        bdmIo->traceStartNs = HTracerNowNsC();
        const char *traceName = useUring ? (isRead ? "BDM_DISK_TRACE_READ_ASYNC" : "BDM_DISK_TRACE_WRITE_ASYNC") :
                                           (isRead ? "BDM_DISK_TRACE_READ_SYNC" : "BDM_DISK_TRACE_WRITE_SYNC");
        HTracerDelayBeginC(bdmIo->traceId, traceName);
    }
    bdmIo->cb = req->ioCtx->cb;
    bdmIo->ctx = req->ioCtx->ctx;
    bdmIo->item = (void *)item;
    if (useUring) {
        uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * req->chunkId + req->offset;
        ret = BdmDiskPrepareUringBuffer(bdmIo, item, rwOffset);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            HTRACER_C_DELAY_END(bdmIo->traceId, bdmIo->traceStartNs, ret);
            return ret;
        }
    }
    *ioOut = bdmIo;
    return BDM_CODE_OK;
}

static int32_t BdmDiskExecuteSyncBatch(void **argList, uint32_t argNum, void *ctx)
{
    (void)ctx;
    for (uint32_t i = 0; i < argNum; i++) {
        BdmIoContext *bdmIo = (BdmIoContext *)argList[i];
        if (UNLIKELY(bdmIo == NULL || bdmIo->item == NULL)) {
            continue;
        }
        BdmDiskItem *item = (BdmDiskItem *)bdmIo->item;
        uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * bdmIo->chunkId + bdmIo->offset;
        int32_t ret = BdmDiskIsRangeAligned(bdmIo->len, rwOffset) ?
                          BdmDiskInnerReadWriteDirect(
                              item, (char *)bdmIo->buf, bdmIo->len, rwOffset, bdmIo->isRead) :
                          BdmDiskInnerReadWrite(item, (char *)bdmIo->buf, bdmIo->len, rwOffset, bdmIo->isRead);
        HTRACER_C_DELAY_END(bdmIo->traceId, bdmIo->traceStartNs, ret);
        bdmIo->cb(bdmIo->ctx, ret);
    }
    return BDM_CODE_OK;
}

static int32_t BdmDiskHandleSyncBatch(BdmAsyncOpsReq *reqs, uint32_t reqNum, bool isRead)
{
    for (uint32_t i = 0; i < reqNum; i++) {
        BdmIoContext *bdmIo = NULL;
        int32_t ret = BdmDiskPrepareAsyncContext(&reqs[i], isRead, false, &bdmIo);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BdmDiskCompleteReq(&reqs[i], ret);
            continue;
        }
        ret = BdmThreadPoolAdd(g_bdmSyncThreadPool, NULL, (void *)bdmIo);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BdmDiskCleanupPreparedAsyncContext(bdmIo, ret);
            BdmDiskCompleteReq(&reqs[i], ret);
        }
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskHandleAsync(BdmAsyncOpsReq *req, bool isRead)
{
    BdmObj *obj = (BdmObj *)req->objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    if (g_bdmIoEngine == BDM_IO_ENGINE_IO_URING && !isRead) {
        if (UNLIKELY(req->ioCtx == NULL || req->ioCtx->cb == NULL)) {
            return BDM_CODE_INVALID_PARAM;
        }
        int32_t ret = BdmAllocatorCheckChunk(item->allocator, req->chunkId, req->offset, req->len);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BDM_LOGWARN(0, "Bdm write check failed, bdmId(%u) chunkId(%lu) ret(%d).", obj->bdmId, req->chunkId, ret);
            return ret;
        }
        uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * req->chunkId + req->offset;
        if (!BdmDiskIsRangeAligned(req->len, rwOffset)) {
            HTRACER_C_DELAY_BEGIN(BDM_DISK_TRACE_WRITE_ASYNC_C, "BDM_DISK_TRACE_WRITE_ASYNC", traceStartNs);
            ret = BdmDiskInnerReadWrite(item, (char *)req->buf, req->len, rwOffset, FALSE);
            HTRACER_C_DELAY_END(BDM_DISK_TRACE_WRITE_ASYNC_C, traceStartNs, ret);
            BDM_LOGDEBUG(0, "Fallback io_uring non-aligned write to sync fd, bdmId(%u), chunkId(%lu), offset(%llu), "
                "len(%llu).", obj->bdmId, req->chunkId, rwOffset, req->len);
            req->ioCtx->cb(req->ioCtx->ctx, ret);
            return BDM_CODE_OK;
        }
    }

    if (g_bdmIoEngine == BDM_IO_ENGINE_SYNC) {
        if (UNLIKELY(req->ioCtx == NULL || req->ioCtx->cb == NULL)) {
            return BDM_CODE_INVALID_PARAM;
        }
        int32_t ret = BdmAllocatorCheckChunk(item->allocator, req->chunkId, req->offset, req->len);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BDM_LOGWARN(0, "Bdm read check failed, bdmId(%u) chunkId(%lu) ret(%d).", obj->bdmId, req->chunkId, ret);
            return ret;
        }
        uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * req->chunkId + req->offset;
        int32_t traceId = isRead ? BDM_DISK_TRACE_READ_SYNC_C : BDM_DISK_TRACE_WRITE_SYNC_C;
        HTRACER_C_DELAY_BEGIN(traceId, isRead ? "BDM_DISK_TRACE_READ_SYNC" : "BDM_DISK_TRACE_WRITE_SYNC",
            traceStartNs);
        ret = BdmDiskIsRangeAligned(req->len, rwOffset) ?
                  BdmDiskInnerReadWriteDirect(item, (char *)req->buf, req->len, rwOffset, isRead) :
                  BdmDiskInnerReadWrite(item, (char *)req->buf, req->len, rwOffset, isRead);
        HTRACER_C_DELAY_END(traceId, traceStartNs, ret);
        req->ioCtx->cb(req->ioCtx->ctx, ret);
        return BDM_CODE_OK;
    }

    BdmIoContext *bdmIo = NULL;
    int32_t ret = BdmDiskPrepareAsyncContext(req, isRead, true, &bdmIo);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }
    uint64_t index = ATOMIC_INC(&g_bdmIndex) % BDM_WORKER_THREAD_NUM;
    ret = BdmThreadPoolAdd(g_bdmThreadPool.pool[index], NULL, (void *)bdmIo);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BdmDiskCleanupPreparedAsyncContext(bdmIo, ret);
    }
    return ret;
}

static void BdmDiskCompleteReq(BdmAsyncOpsReq *req, int32_t ret)
{
    if (req != NULL && req->ioCtx != NULL && req->ioCtx->cb != NULL) {
        req->ioCtx->cb(req->ioCtx->ctx, ret);
    }
}

static int32_t BdmDiskHandleAsyncBatch(BdmAsyncOpsReq *reqs, uint32_t reqNum, bool isRead)
{
    if (UNLIKELY(reqs == NULL || reqNum == 0)) {
        return BDM_CODE_INVALID_PARAM;
    }

    if (g_bdmIoEngine == BDM_IO_ENGINE_SYNC) {
        return BdmDiskHandleSyncBatch(reqs, reqNum, isRead);
    }

    for (uint32_t i = 0; i < reqNum; i++) {
        int32_t ret = BdmDiskHandleAsync(&reqs[i], isRead);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BdmDiskCompleteReq(&reqs[i], ret);
        }
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskReadAsync(BdmAsyncOpsReq *req)
{
    return BdmDiskHandleAsync(req, TRUE);
}

int32_t BdmDiskWriteAsync(BdmAsyncOpsReq *req)
{
    return BdmDiskHandleAsync(req, FALSE);
}

int32_t BdmDiskReadBatchAsync(BdmAsyncOpsReq *reqs, uint32_t reqNum)
{
    return BdmDiskHandleAsyncBatch(reqs, reqNum, TRUE);
}

int32_t BdmDiskWriteBatchAsync(BdmAsyncOpsReq *reqs, uint32_t reqNum)
{
    return BdmDiskHandleAsyncBatch(reqs, reqNum, FALSE);
}

int32_t BdmDiskCreateCheck(BdmCreatePara *para)
{
    if (UNLIKELY(para->name == NULL || para->sn == NULL)) {
        BDM_LOGERROR(0, "Invalid name or sn.");
        return BDM_CODE_INVALID_PARAM;
    }
    if (UNLIKELY(strlen(para->name) == 0UL || strlen(para->sn) == 0UL)) {
        BDM_LOGERROR(0, "Invalid name or sn.");
        return BDM_CODE_INVALID_PARAM;
    }
    if (UNLIKELY(para->length == 0UL)) {
        BDM_LOGERROR(0, "Invalid length(%lu).", para->length);
        return BDM_CODE_INVALID_PARAM;
    }
    if (UNLIKELY(para->minChunkSize == 0UL)) {
        BDM_LOGERROR(0, "Invalid min chunk size(%lu).", para->minChunkSize);
        return BDM_CODE_INVALID_PARAM;
    }
    if (UNLIKELY(para->maxChunkSize == 0UL)) {
        BDM_LOGERROR(0, "Invalid max chunk size(%lu), min chunk size(%lu).", para->maxChunkSize, para->minChunkSize);
        return BDM_CODE_INVALID_PARAM;
    }
    return BDM_CODE_OK;
}

void *BdmDiskEventsThread(void *argsP)
{
    BdmThreadCtx *threadCtx = (BdmThreadCtx *)argsP;
    BdmThreadPool *bdmPool = (BdmThreadPool *)threadCtx->ctx;

    uint32_t threadIdx = threadCtx->index;
    struct io_uring *ring = &bdmPool->ring[threadIdx];

    BDM_LOGINFO(0, "bdm disk events thread start.");
    BdmThreadBindCPUs("bdm_events", bdmPool->cpus[threadIdx]);
    while (true) {
        struct io_uring_cqe *cqe = NULL;
        int32_t ret = io_uring_wait_cqe(ring, &cqe);
        if (UNLIKELY(ret != 0 || cqe == NULL)) {
            if (bdmPool->stopping[threadIdx]) {
                break;
            }
            BDM_LOGERROR(0, "io_uring_wait_cqe failed, ret(%d).", ret);
            continue;
        }

        bool needStop = false;
        uint32_t head = 0;
        uint32_t completed = 0;
        io_uring_for_each_cqe(ring, head, cqe) {
            needStop = BdmCompleteUringHandler(cqe, threadCtx);
            completed++;
            if (needStop) {
                break;
            }
        }
        io_uring_cq_advance(ring, completed);
        if (needStop) {
            break;
        }
    }

    BDM_LOGINFO(0, "bdm disk events thread exit.");
    return NULL;
}

void BdmDiskCloseDisk(BdmDiskItem *item)
{
    if (UNLIKELY(item->fd >= 0)) {
        close(item->fd);
        item->fd = -1;
    }
    for (uint32_t index = 0; index < BDM_AYSNC_IO_FD_NUM; index++) {
        if (item->asyncfd[index] >= 0) {
            close(item->asyncfd[index]);
            item->asyncfd[index] = -1;
        }
    }
    return;
}

int32_t BdmDiskOpenDisk(BdmCreatePara *para, BdmDiskItem *item)
{
    item->totalSize = para->length;
    item->offset = para->offset;
    item->headSize = sizeof(BdmDiskHead);
    item->fd = -1;

    for (uint32_t index = 0; index < BDM_AYSNC_IO_FD_NUM; index++) {
        item->asyncfd[index] = -1;
    }

    int32_t ret = memcpy_s(item->name, BDM_NAME_LEN, para->name, BDM_NAME_LEN);
    item->name[BDM_NAME_LEN - 1] = '\0';
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR("Memcpy bdm name failed, name(%s).", para->name);
        return BDM_CODE_ERR;
    }

    ret = memcpy_s(item->sn, BDM_SN_LEN, para->sn, BDM_SN_LEN);
    item->sn[BDM_SN_LEN - 1] = '\0';
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR("Memcpy bdm name failed, name(%s).", para->name);
        return BDM_CODE_ERR;
    }

    item->fd = open(item->name, O_RDWR | O_CREAT | O_SYNC, BDM_OPEN_FILE_PERMISSION);
    if (UNLIKELY(item->fd < 0)) {
        BDM_LOGERROR(0, "Open device(%s) failed, errno(%s).", item->name, strerror(errno));
        return BDM_CODE_ERR;
    }

    struct stat diskStat = { 0 };
    if (UNLIKELY(fstat(item->fd, &diskStat) != 0)) {
        BDM_LOGERROR(0, "Get device(%s) status failed, errno(%s).", item->name, strerror(errno));
        BdmDiskCloseDisk(item);
        return BDM_CODE_ERR;
    }
    if (S_ISBLK(diskStat.st_mode) &&
        UNLIKELY(BdmDiskPreCheckFileLen(item->fd, item->offset, item->totalSize) != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Disk region exceeds device capacity, device(%s), offset(%llu), length(%llu).", item->name,
            item->offset, item->totalSize);
        BdmDiskCloseDisk(item);
        return BDM_CODE_CROSS_BOUND;
    }

    for (uint32_t index = 0; index < BDM_AYSNC_IO_FD_NUM; index++) {
        item->asyncfd[index] = open(item->name, O_RDWR | __O_DIRECT);
        if (UNLIKELY(item->asyncfd[index] < 0)) {
            BDM_LOGERROR(0, "Open device(%s) failed, errno(%s).", item->name, strerror(errno));
            BdmDiskCloseDisk(item);
            return BDM_CODE_ERR;
        }
    }
    return BDM_CODE_OK;
}

static bool BdmDiskHeadHasStandaloneInfo(uint32_t pad)
{
    return (pad & BDM_DISK_HEAD_MODE_MASK) == BDM_DISK_HEAD_STANDALONE_MAGIC;
}

static uint32_t BdmDiskHeadLayoutVersion(uint32_t pad)
{
    return (pad & BDM_DISK_HEAD_LAYOUT_VERSION_MASK) >> BDM_DISK_HEAD_LAYOUT_VERSION_SHIFT;
}

static bool BdmDiskHeadHasVirtualLayout(uint32_t pad)
{
    return BdmDiskHeadHasStandaloneInfo(pad) &&
        BdmDiskHeadLayoutVersion(pad) == BDM_DISK_HEAD_VIRTUAL_LAYOUT_VERSION;
}

static const char *BdmDiskHeadMode(uint32_t pad)
{
    return BdmDiskHeadHasStandaloneInfo(pad) ? "standalone" : "cluster";
}

static uint32_t BdmDiskHeadDeviceId(uint32_t pad)
{
    return pad & BDM_DISK_HEAD_DEVICE_ID_MASK;
}

static uint32_t BdmDiskHeadDeviceCount(uint32_t pad)
{
    return (pad & BDM_DISK_HEAD_DEVICE_COUNT_MASK) >> BDM_DISK_HEAD_DEVICE_COUNT_SHIFT;
}

static int32_t BdmDiskClearVirtualHeaders(BdmDiskItem *item, const BdmDiskHead *observedHead)
{
    uint32_t storedDeviceCount = BdmDiskHeadDeviceCount(observedHead->pad);
    uint32_t currentDeviceCount = BdmDiskHeadDeviceCount(item->pad);
    if (UNLIKELY(currentDeviceCount == 0 || currentDeviceCount > BDM_VIRTUAL_LAYOUT_SLOT_NUM)) {
        BDM_LOGERROR(0, "Invalid current virtual disk metadata, device(%s), bdmId(%u), currentDeviceCount(%u).",
            item->name, item->bdmId, currentDeviceCount);
        return BDM_CODE_ERR_IO;
    }

    /* A current Region is a union of 16-way slots; clear the 2 MiB header area of every owned slot. */
    uint32_t headerSlotCount = BDM_VIRTUAL_LAYOUT_SLOT_NUM / currentDeviceCount;
    if (UNLIKELY(item->totalSize % headerSlotCount != 0 ||
        item->totalSize / headerSlotCount < BDM_RESTORE_META_SIZE)) {
        BDM_LOGERROR(0, "Invalid current virtual disk region, device(%s), bdmId(%u), regionLength(%llu), "
            "headerSlotCount(%u).", item->name, item->bdmId, item->totalSize, headerSlotCount);
        return BDM_CODE_ERR_IO;
    }
    uint64_t headerStride = item->totalSize / headerSlotCount;

    char *clearBuff = (char *)calloc(1, BDM_RESTORE_META_SIZE);
    if (UNLIKELY(clearBuff == NULL)) {
        BDM_LOGERROR(0, "Allocate old virtual disk header clear buffer failed, device(%s), bdmId(%u).",
            item->name, item->bdmId);
        return BDM_CODE_ERR_IO;
    }

    int32_t ret = BDM_CODE_OK;
    /* Clear the Region anchor last so interrupted cleanup can be detected and retried on the next startup. */
    for (uint32_t headerIndex = headerSlotCount; headerIndex > 0; headerIndex--) {
        uint32_t slotIndex = headerIndex - 1;
        uint64_t headerOffset = item->offset + slotIndex * headerStride;
        ret = BdmDiskInnerReadWrite(item, clearBuff, BDM_RESTORE_META_SIZE, headerOffset, FALSE);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BDM_LOGERROR(0, "Clear old virtual disk header failed, device(%s), bdmId(%u), headerOffset(%llu).",
                item->name, item->bdmId, headerOffset);
            ret = BDM_CODE_ERR_IO;
            break;
        }
    }

    free(clearBuff);
    if (ret == BDM_CODE_OK) {
        BDM_LOGWARN(0, "Cleared old virtual disk headers, device(%s), bdmId(%u), storedDeviceCount(%u), "
            "currentDeviceCount(%u), currentDeviceId(%u), regionOffset(%llu), regionLength(%llu), "
            "headerStride(%llu), headerSlotCount(%u).",
            item->name, item->bdmId, storedDeviceCount, BdmDiskHeadDeviceCount(item->pad),
            BdmDiskHeadDeviceId(item->pad), item->offset, item->totalSize, headerStride, headerSlotCount);
    }
    return ret;
}

static bool BdmDiskCheckFixedItem(const BdmDiskHead *head, const BdmDiskItem *item)
{
    return (head->magic == BDM_DISK_MAGIC && head->bdmId == item->bdmId && head->minChunkSize == item->minChunkSize &&
        head->maxChunkSize == item->maxChunkSize && head->totalSize == item->totalSize &&
        head->metaOffset == item->metaOffset && head->metaLength == item->metaLength &&
        head->dataOffset == item->dataOffset && head->dataLength == item->dataLength && head->offset == item->offset &&
        head->headSize == item->headSize);
}

static int32_t BdmDiskCheckItem(const BdmDiskHead *head, const BdmDiskItem *item)
{
    if (UNLIKELY(!BdmDiskCheckFixedItem(head, item))) {
        return BDM_CODE_ERR;
    }
    if (head->pad == item->pad) {
        return BDM_CODE_OK;
    }
    if (UNLIKELY(!BdmDiskHeadHasStandaloneInfo(head->pad) && !BdmDiskHeadHasStandaloneInfo(item->pad))) {
        BDM_LOGWARN(0, "Disk metadata without standalone startup info, device(%s), bdmId(%u), currentDeviceId(%u).",
            item->name, item->bdmId, BdmDiskHeadDeviceId(item->pad));
        return BDM_CODE_OK;
    }

    BDM_LOGERROR(0,
        "Disk metadata mismatch, device(%s), bdmId(%u), storedMode(%s), storedVersion(%u), storedDeviceCount(%u), "
        "storedDeviceId(%u), currentMode(%s), currentVersion(%u), currentDeviceCount(%u), currentDeviceId(%u).",
        item->name, item->bdmId, BdmDiskHeadMode(head->pad), BdmDiskHeadLayoutVersion(head->pad),
        BdmDiskHeadDeviceCount(head->pad), BdmDiskHeadDeviceId(head->pad),
        BdmDiskHeadMode(item->pad), BdmDiskHeadLayoutVersion(item->pad), BdmDiskHeadDeviceCount(item->pad),
        BdmDiskHeadDeviceId(item->pad));
    return BDM_CODE_METADATA_MISMATCH;
}

static int32_t BdmDiskPreCheckFileLen(int32_t fd, uint64_t offset, uint64_t length)
{
    off_t fileLength = lseek(fd, 0, SEEK_END);
    if (UNLIKELY(fileLength < 0 || offset > (uint64_t)fileLength || length > (uint64_t)fileLength - offset)) {
        return BDM_CODE_ERR;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskRestoreCheckOK(BdmDiskItem *item)
{
    int32_t ret = BdmDiskPreCheckFileLen(item->fd, item->offset, item->totalSize);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    BdmDiskHead head;
    ret = BdmDiskCheckRegionRange(item, item->offset, item->headSize);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }
    uint64_t rwLen = BdmDiskInnerReadWriteImpl(item->fd, (char *)&head, item->headSize, item->offset, TRUE);
    if (UNLIKELY(rwLen != item->headSize)) {
        BDM_LOGWARN(0, "Read disk failed, need(%lu) real(%lu) device(%s).", item->headSize, rwLen, item->name);
        return BDM_CODE_ERR;
    }

    bool currentVirtualLayout = BdmDiskHeadHasVirtualLayout(item->pad);
    bool storedVirtualLayout = head.magic == BDM_DISK_MAGIC && BdmDiskHeadHasVirtualLayout(head.pad);
    if (currentVirtualLayout && (!storedVirtualLayout ||
        BdmDiskHeadDeviceCount(head.pad) != BdmDiskHeadDeviceCount(item->pad))) {
        ret = BdmDiskClearVirtualHeaders(item, &head);
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            return ret;
        }
        // Missing, damaged, or stale virtual metadata was cleared; continue with a fresh allocator.
        return BDM_CODE_METADATA_MISMATCH;
    }

    uint64_t metaSize;
    uint64_t dataSize;
    ret = BdmAllocatorGetSplitSize(item->headSize, item->minChunkSize, item->totalSize, &metaSize, &dataSize);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    item->metaOffset = item->headSize;
    item->metaLength = metaSize;
    item->dataOffset = ROUND_UP(item->headSize + metaSize, BDM_ALIGN_SIZE);
    item->dataLength = dataSize;
    ret = BdmDiskCheckItem(&head, item);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        if (currentVirtualLayout) {
            ret = BdmDiskClearVirtualHeaders(item, &head);
            return ret == BDM_CODE_OK ? BDM_CODE_METADATA_MISMATCH : ret;
        }
        return ret;
    }

    ret = BdmDiskFillDiskHead(&head, item);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    rwLen = BdmDiskInnerReadWriteImpl(item->fd, (char *)&head, item->headSize, item->offset, FALSE);
    if (UNLIKELY(rwLen != item->headSize)) {
        BDM_LOGWARN(0, "Write disk failed, need(%lu) real(%lu) device(%s).", item->headSize, rwLen, item->name);
        return BDM_CODE_ERR;
    }

    return BDM_CODE_OK;
}

int32_t BdmDiskRestoreAllocator(BdmDiskItem *item)
{
    int32_t ret = BdmDiskRestoreCheckOK(item);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    BdmAllocatorPara allocatorPara = { 0 };
    allocatorPara.metaOps.itemPtr = (uintptr_t)item;
    allocatorPara.metaOps.writeMeta = BdmDiskWriteMeta;
    allocatorPara.metaOps.readMeta = BdmDiskReadMeta;
    allocatorPara.minChunkSize = item->minChunkSize;
    allocatorPara.maxChunkSize = item->maxChunkSize;
    allocatorPara.totalSize = item->dataLength; /* 只有数据区用于chunk分配 */
    item->allocator = BdmAllocatorCreate(&allocatorPara, 1UL);
    if (UNLIKELY(item->allocator == 0L)) {
        BDM_LOGERROR(0, "Restore allocator failed.");
        return BDM_CODE_ERR;
    }

    return BDM_CODE_OK;
}

static int32_t BdmDiskFillDiskHead(BdmDiskHead *head, BdmDiskItem *item)
{
    head->magic = BDM_DISK_MAGIC;
    head->bdmId = item->bdmId;
    head->pad = item->pad;
    head->minChunkSize = item->minChunkSize;
    head->maxChunkSize = item->maxChunkSize;
    head->totalSize = item->totalSize;
    head->metaOffset = item->metaOffset;
    head->metaLength = item->metaLength;
    head->dataOffset = item->dataOffset;
    head->dataLength = item->dataLength;
    int32_t ret = memcpy_s(head->name, BDM_NAME_LEN, item->name, BDM_NAME_LEN);
    head->name[BDM_NAME_LEN - 1] = '\0';
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Memcpy bdm name failed, name(%s).", item->name);
        return BDM_CODE_ERR;
    }

    ret = memcpy_s(head->sn, BDM_SN_LEN, item->sn, BDM_SN_LEN);
    head->sn[BDM_SN_LEN - 1] = '\0';
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Memcpy bdm sn failed, sn(%s).", item->name);
        return BDM_CODE_ERR;
    }

    head->offset = item->offset;
    head->headSize = item->headSize;
    return BDM_CODE_OK;
}

int32_t BdmDiskStoreDiskHead(BdmDiskItem *item)
{
    BdmDiskHead head;
    int32_t ret = BdmDiskFillDiskHead(&head, item);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    void *restoreBuff = malloc(BDM_RESTORE_META_SIZE);
    if (UNLIKELY(restoreBuff == NULL)) {
        BDM_LOGERROR(0, "Malloc restore buff failed.");
        return BDM_CODE_ERR;
    }

    if (UNLIKELY(memset_s(restoreBuff, BDM_RESTORE_META_SIZE, 0, BDM_RESTORE_META_SIZE) != BDM_CODE_OK ||
        memcpy_s(restoreBuff, BDM_RESTORE_META_SIZE, &head, sizeof(BdmDiskHead)) != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Memcpy restore buff failed, name(%s).", item->name);
        free(restoreBuff);
        restoreBuff = NULL;
        return BDM_CODE_ERR;
    }

    uint64_t rwOffset = item->offset;
    ret = BdmDiskCheckRegionRange(item, rwOffset, BDM_RESTORE_META_SIZE);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        free(restoreBuff);
        return ret;
    }
    uint64_t rwLen = BdmDiskInnerReadWriteImpl(item->fd, (char *)restoreBuff, BDM_RESTORE_META_SIZE, rwOffset, FALSE);
    if (UNLIKELY(rwLen != BDM_RESTORE_META_SIZE)) {
        BDM_LOGWARN(0, "Write disk failed, need(%lu) real(%lu) device(%s).", BDM_RESTORE_META_SIZE, rwLen, item->name);
        free(restoreBuff);
        restoreBuff = NULL;
        return BDM_CODE_ERR;
    }
    free(restoreBuff);
    restoreBuff = NULL;
    return BDM_CODE_OK;
}

int32_t BdmDiskNewAllocator(BdmDiskItem *item)
{
    uint64_t metaSize;
    uint64_t dataSize;
    int32_t ret = BdmAllocatorGetSplitSize(item->headSize, item->minChunkSize, item->totalSize, &metaSize, &dataSize);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        return ret;
    }

    item->metaOffset = item->headSize;
    item->metaLength = metaSize;
    item->dataOffset = ROUND_UP(item->headSize + metaSize, BDM_ALIGN_SIZE);
    item->dataLength = dataSize;
    ret = BdmDiskStoreDiskHead(item);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Store disk head failed.");
        return ret;
    }

    BdmAllocatorPara allocatorPara = { 0 };
    allocatorPara.metaOps.itemPtr = (uintptr_t)item;
    allocatorPara.metaOps.writeMeta = BdmDiskWriteMeta;
    allocatorPara.metaOps.readMeta = BdmDiskReadMeta;
    allocatorPara.minChunkSize = item->minChunkSize;
    allocatorPara.maxChunkSize = item->maxChunkSize;
    allocatorPara.totalSize = item->dataLength; /* 只有数据区用于chunk分配 */
    item->allocator = BdmAllocatorCreate(&allocatorPara, 0UL);
    if (UNLIKELY(item->allocator == 0L)) {
        BDM_LOGERROR(0, "Create allocator failed.");
        return BDM_CODE_ERR;
    }

    return BDM_CODE_OK;
}

int32_t BdmDiskCreateAllocator(BdmDiskItem *item)
{
    if (g_bdmForceNew != 0) {
        BDM_LOGINFO(0, "Force new disk enabled, skip recovery and create a fresh allocator, device(%s), bdmId(%u).",
            item->name, item->bdmId);
        return BdmDiskNewAllocator(item);
    }

    int32_t ret = BdmDiskRestoreAllocator(item);
    if (ret == BDM_CODE_OK) {
        return ret;
    }
    // Cleanup failed; fail this virtual disk initialization instead of leaving stale headers behind.
    if (UNLIKELY(ret == BDM_CODE_ERR_IO)) {
        return ret;
    }
    if (UNLIKELY(ret == BDM_CODE_METADATA_MISMATCH && !BdmDiskHeadHasVirtualLayout(item->pad))) {
        return ret;
    }
    if (ret == BDM_CODE_METADATA_MISMATCH) {
        BDM_LOGWARN(0, "Virtual disk metadata does not match current layout, create a fresh allocator, device(%s), "
            "bdmId(%u).", item->name, item->bdmId);
    }
    return BdmDiskNewAllocator(item);
}

void BdmDiskDestroyAllocator(BdmDiskItem *item)
{
    int32_t ret = BdmAllocatorDestroy(item->allocator);
    item->allocator = 0;
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "destroy allocator failed.");
    }
}

void BdmDiskFillBdmObj(BdmObj *obj, BdmDiskItem *item)
{
    obj->bdmId = item->bdmId;
    obj->totalSize = item->totalSize;
    obj->minChunkSize = item->minChunkSize;
    obj->maxChunkSize = item->maxChunkSize;
    obj->ops.alloc = BdmDiskAlloc;
    obj->ops.free = BdmDiskFree;
    obj->ops.parseChunkId = BdmDiskParseChunkId;
    obj->ops.read = BdmDiskRead;
    obj->ops.write = BdmDiskWrite;
    obj->ops.readAsync = BdmDiskReadAsync;
    obj->ops.writeAsync = BdmDiskWriteAsync;
    obj->ops.readBatchAsync = BdmDiskReadBatchAsync;
    obj->ops.writeBatchAsync = BdmDiskWriteBatchAsync;
    obj->ops.allocatorReset = BdmDiskAllocatorReset;
    obj->ops.nextchunk = BdmDiskGetNextChunk;
    obj->ops.getcap = BdmDiskGetCap;
    obj->opsInfo = (BdmOpsInfo)item;
}

int32_t BdmGetDiskPath(uint32_t bdmId, char *path, uint32_t pathLen)
{
    if (path == NULL || pathLen == 0) {
        return BDM_CODE_ERR;
    }
    BdmObj *obj = BdmGetBdmObj(bdmId);
    if (obj == NULL || obj->opsInfo == NULL) {
        return BDM_CODE_NOT_EXIST;
    }
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    return strcpy_s(path, pathLen, item->name) == 0 ? BDM_CODE_OK : BDM_CODE_ERR;
}

static void BdmDiskFillItem(BdmDiskItem *item, BdmCreatePara *para, uint32_t bdmId)
{
    item->bdmId = bdmId;
    item->pad = para->pad;
    item->minChunkSize = para->minChunkSize;
    item->maxChunkSize = para->maxChunkSize;
}

BdmObj *BdmDiskCreate(uint32_t bdmId, uintptr_t createPara)
{
    BdmCreatePara *para = (BdmCreatePara *)createPara;
    int32_t ret = BdmDiskCreateCheck(para);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Bdm disk create check failed, ret(%d).", ret);
        return NULL;
    }

    BdmDiskItem *item = (BdmDiskItem *)malloc(sizeof(BdmDiskItem));
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Bdm disk alloc item context failed.");
        return NULL;
    }

    BdmDiskFillItem(item, para, bdmId);
    ret = BdmDiskOpenDisk(para, item);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Bdm disk create inter elem failed, ret(%d).", ret);
        free(item);
        item = NULL;
        return NULL;
    }

    ret = BdmDiskCreateAllocator(item);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Bdm disk create allocator failed.");
        BdmDiskCloseDisk(item);
        free(item);
        item = NULL;
        return NULL;
    }

    BdmObj *obj = (BdmObj *)malloc(sizeof(BdmObj));
    if (UNLIKELY(obj == NULL)) {
        BDM_LOGERROR(0, "Malloc obj failed.");
        BdmDiskDestroyAllocator(item);
        BdmDiskCloseDisk(item);
        free(item);
        item = NULL;
        return NULL;
    }
    BdmDiskFillBdmObj(obj, item);

    BDM_SPIN_LOCK(&g_bdmDisk.lock);
    DListAddTail(&item->node, &g_bdmDisk.head);
    g_bdmDisk.num++;
    BDM_SPIN_UNLOCK(&g_bdmDisk.lock);
    BDM_LOGINFO(0, "Bdm disk create succeed, bdm id(%u) size(%lu), fd(%d).", bdmId, para->length, item->fd);
    return obj;
}

int32_t BdmDiskDestroy(BdmObj *obj)
{
    if (UNLIKELY(obj == NULL)) {
        BDM_LOGINFO(0, "Bdm obj is null, no need to destroy.");
        return BDM_CODE_OK;
    }
    BDM_LOGINFO(0, "Bdm disk destroy, bdm id(%u).", obj->bdmId);

    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    BdmDiskDestroyAllocator(item);
    BdmDiskCloseDisk(item);

    BDM_SPIN_LOCK(&g_bdmDisk.lock);
    DListDel(&item->node);
    g_bdmDisk.num--;
    BDM_SPIN_UNLOCK(&g_bdmDisk.lock);
    free(item);
    item = NULL;
    free(obj);
    obj = NULL;
    return BDM_CODE_OK;
}

int32_t BdmReopenDisk(BdmDiskItem *item)
{
    uint32_t index;
    item->fd = open(item->name, O_RDWR | O_CREAT | O_SYNC, BDM_OPEN_FILE_PERMISSION);
    if (UNLIKELY(item->fd < 0)) {
        BDM_LOGERROR(0, "Open device(%s) failed, errno(%s).", item->name, strerror(errno));
        return BDM_CODE_ERR;
    }

    for (index = 0; index < BDM_AYSNC_IO_FD_NUM; index++) {
        item->asyncfd[index] = open(item->name, O_RDWR | __O_DIRECT);
        if (UNLIKELY(item->asyncfd[index] < 0)) {
            BDM_LOGERROR(0, "Open device(%s) failed, errno(%s).", item->name, strerror(errno));
            BdmDiskCloseDisk(item);
            return BDM_CODE_ERR;
        }
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskReset(BdmObj *obj)
{
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    if (UNLIKELY(item == NULL)) {
        BDM_LOGERROR(0, "Get bdm disk item failed.");
        return BDM_CODE_ERR;
    }

    // destroy old disk allocator
    BdmDiskDestroyAllocator(item);
    BdmDiskCloseDisk(item);

    // reopen disk fd
    int32_t ret = BdmReopenDisk(item);
    if (UNLIKELY(ret != 0)) {
        BDM_LOGERROR(0, "Reopen disk failed, ret(%d), bdmId(%d).", ret, item->bdmId);
        return BDM_CODE_ERR;
    }

    // create new allocator to item
    ret = BdmDiskNewAllocator(item);
    if (UNLIKELY(ret != 0)) {
        BDM_LOGERROR(0, "Create new allocator failed, ret(%d), bdmId(%d).", ret, item->bdmId);
        return BDM_CODE_ERR;
    }

    return BDM_CODE_OK;
}

static bool BdmDiskCanFallbackSqpoll(int32_t ret)
{
    int32_t err = -ret;
    return err == EPERM || err == EACCES || err == EINVAL || err == ENOSYS || err == EOPNOTSUPP;
}

static int32_t BdmPoolInit(BdmThreadPool *bdmPool, uint32_t index, bool useSqpoll, int32_t *queueRet)
{
    struct io_uring_params params = {0};
    if (useSqpoll) {
        params.flags = IORING_SETUP_SQPOLL;
        params.sq_thread_idle = 2000;
    }
    int32_t ret = io_uring_queue_init_params(BDM_IOCTX_EVENTS_NUM, &bdmPool->ring[index], &params);
    if (UNLIKELY(ret != 0)) {
        if (queueRet != NULL) {
            *queueRet = ret;
        }
        int32_t err = -ret;
        BDM_LOGERROR(0,
            "io_uring queue init failed, mode(%s), worker(%u), entries(%u), flags(0x%x), sq_thread_idle(%u), "
            "ret(%d), errno(%d:%s).",
            useSqpoll ? "sqpoll" : "normal", index, (uint32_t)BDM_IOCTX_EVENTS_NUM, params.flags,
            params.sq_thread_idle, ret, err, strerror(err));
        if (useSqpoll && (err == EPERM || err == EACCES)) {
            BDM_LOGERROR(0,
                "io_uring SQPOLL permission denied. Check process capabilities, container seccomp policy, and kernel "
                "io_uring/SQPOLL permission settings.");
        } else if (useSqpoll && (err == EINVAL || err == ENOSYS || err == EOPNOTSUPP)) {
            BDM_LOGERROR(0,
                "io_uring SQPOLL is not supported by current kernel or system policy.");
        }
        return BDM_CODE_ERR;
    }
    bdmPool->ringInited[index] = true;
    bdmPool->ringSqpoll[index] = useSqpoll;
    ret = pthread_mutex_init(&bdmPool->ringLock[index], NULL);
    if (UNLIKELY(ret != 0)) {
        BDM_LOGERROR(0, "ring lock init failed, ret(%d).", ret);
        io_uring_queue_exit(&bdmPool->ring[index]);
        bdmPool->ringInited[index] = false;
        return BDM_CODE_ERR;
    }
    ret = sem_init(&bdmPool->ringSlots[index], 0, BDM_URING_MAX_INFLIGHT);
    if (UNLIKELY(ret != 0)) {
        BDM_LOGERROR(0, "io_uring slot semaphore init failed, ret(%d), errno(%s).", ret, strerror(errno));
        pthread_mutex_destroy(&bdmPool->ringLock[index]);
        io_uring_queue_exit(&bdmPool->ring[index]);
        bdmPool->ringInited[index] = false;
        return BDM_CODE_ERR;
    }
    bdmPool->ringSlotsInited[index] = true;
    bdmPool->ringInflight[index] = 0;
    bdmPool->cpus[index] = BDM_BIND_CPU_DEFAULT;
    return ret;
}

static void BdmDiskThreadPoolCleanup(BdmThreadPool *bdmPool);

static int32_t BdmDiskSyncThreadPoolInit(void)
{
    BDM_BIND_CPU_S cpus = {0};
    cpus.cpunum = 1;
    cpus.cpus[0] = BDM_BIND_CPU_DEFAULT;
    BDM_BATCH_CTX_S batchCtx = {0};
    batchCtx.batchHandle = BdmDiskExecuteSyncBatch;
    g_bdmSyncThreadPool =
        BdmThreadPoolCreate(g_bdmSyncWorkerNum, BDM_SYNC_QUEUE_SIZE, &cpus, "bdm_sync", &batchCtx);
    if (UNLIKELY(g_bdmSyncThreadPool == NULL)) {
        BDM_LOGERROR(0, "Create bdm sync thread pool failed, workerNum(%u).", g_bdmSyncWorkerNum);
        return BDM_CODE_ERR;
    }
    BDM_LOGINFO(0, "Bdm sync thread pool initialized, workerNum(%u), queueSize(%u).", g_bdmSyncWorkerNum,
        (uint32_t)BDM_SYNC_QUEUE_SIZE);
    return BDM_CODE_OK;
}

static int32_t BdmDiskUringResourcesInit(BdmThreadPool *bdmPool, bool useSqpoll, int32_t *queueRet)
{
    for (uint32_t index = 0; index < BDM_WORKER_THREAD_NUM; index++) {
        BdmThreadCtx *threadCtx = &bdmPool->threadCtx[index];
        threadCtx->index = index;
        threadCtx->ctx = (void *)bdmPool;
        bdmPool->stopping[index] = false;
        bdmPool->eventThreadStarted[index] = false;
        int32_t ret = BdmPoolInit(bdmPool, index, useSqpoll, queueRet);
        if (UNLIKELY(ret != 0)) {
            BDM_LOGERROR(0, "Bdm pool init failed, ret(%d)", ret);
            return ret;
        }
    }
    return BDM_CODE_OK;
}

static int32_t BdmDiskUringWorkersStart(BdmThreadPool *bdmPool)
{
    for (uint32_t index = 0; index < BDM_WORKER_THREAD_NUM; index++) {
        BdmThreadCtx *threadCtx = &bdmPool->threadCtx[index];
        int32_t ret = pthread_create(&bdmPool->threadId[index], NULL, BdmDiskEventsThread, (void *)threadCtx);
        if (UNLIKELY(ret != 0)) {
            BDM_LOGERROR(0, "Pthread create failed, errno(%s).", strerror(errno));
            return BDM_CODE_ERR;
        }
        bdmPool->eventThreadStarted[index] = true;

        char threadName[BDM_THREAD_NAME_LEN] = {0};
        ret = sprintf_s(threadName, BDM_THREAD_NAME_LEN, "bdm_events");
        if (UNLIKELY(ret < 0)) {
            BDM_LOGERROR(0, "sprintf_s failed, ret(%d).", ret);
            return BDM_CODE_ERR;
        }
        pthread_setname_np(bdmPool->threadId[index], threadName);

        BDM_BIND_CPU_S cpus;
        cpus.cpunum = BDM_DEFAULT_THREAD_NUM;
        cpus.cpus[0] = bdmPool->cpus[index];
        BDM_BATCH_CTX_S batchCtx;
        batchCtx.batchHandle = BdmDiskSubmitUring;
        batchCtx.batchCtx = (void *)threadCtx;
        bdmPool->pool[index] = BdmThreadPoolCreate(BDM_DEFAULT_THREAD_NUM, 1024UL, &cpus, "bdm_disk", &batchCtx);
        if (UNLIKELY(bdmPool->pool[index] == NULL)) {
            BDM_LOGERROR(0, "Pthread pool create failed, errno(%s).", strerror(errno));
            return BDM_CODE_ERR;
        }
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskThreadPoolInit(void)
{
    if (g_bdmIoEngine == BDM_IO_ENGINE_SYNC) {
        return BdmDiskSyncThreadPoolInit();
    }

    BdmThreadPool *bdmPool = &g_bdmThreadPool;
    bool useSqpoll = g_bdmUringSqpollMode != BDM_URING_SQPOLL_DISABLED;
    int32_t queueRet = 0;
    int32_t ret = BdmDiskUringResourcesInit(bdmPool, useSqpoll, &queueRet);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BdmDiskThreadPoolCleanup(bdmPool);
        if (useSqpoll && g_bdmUringSqpollMode == BDM_URING_SQPOLL_AUTO &&
            BdmDiskCanFallbackSqpoll(queueRet)) {
            BDM_LOGWARN(0, "io_uring SQPOLL unavailable, ret(%d), retry all workers with normal io_uring.", queueRet);
            useSqpoll = false;
            queueRet = 0;
            ret = BdmDiskUringResourcesInit(bdmPool, false, &queueRet);
        }
        if (UNLIKELY(ret != BDM_CODE_OK)) {
            BdmDiskThreadPoolCleanup(bdmPool);
            BDM_LOGERROR(0, "Initialize io_uring resources failed, requestedMode(%d), actualMode(%s), ret(%d).",
                g_bdmUringSqpollMode, useSqpoll ? "sqpoll" : "normal", queueRet);
            return BDM_CODE_ERR;
        }
    }

    ret = BdmDiskUringWorkersStart(bdmPool);
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BdmDiskThreadPoolCleanup(bdmPool);
        return ret;
    }
    BDM_LOGINFO(0, "Bdm io_uring thread pool initialized, requestedMode(%d), actualMode(%s), workerNum(%u).",
        g_bdmUringSqpollMode, useSqpoll ? "sqpoll" : "normal", (uint32_t)BDM_WORKER_THREAD_NUM);
    return BDM_CODE_OK;
}

static void BdmDiskWakeEventsThread(BdmThreadPool *bdmPool, uint32_t index)
{
    if (!bdmPool->ringInited[index]) {
        return;
    }

    pthread_mutex_lock(&bdmPool->ringLock[index]);
    struct io_uring_sqe *sqe = io_uring_get_sqe(&bdmPool->ring[index]);
    if (sqe == NULL) {
        (void)io_uring_submit(&bdmPool->ring[index]);
        sqe = io_uring_get_sqe(&bdmPool->ring[index]);
    }
    if (sqe != NULL) {
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, NULL);
        (void)io_uring_submit(&bdmPool->ring[index]);
    }
    pthread_mutex_unlock(&bdmPool->ringLock[index]);
}

static void BdmDiskThreadPoolCleanup(BdmThreadPool *bdmPool)
{
    for (uint32_t index = 0; index < BDM_WORKER_THREAD_NUM; index++) {
        if (bdmPool->pool[index] != NULL) {
            (void)BdmThreadPoolDestroy(bdmPool->pool[index], 0);
            bdmPool->pool[index] = NULL;
        }
        bdmPool->stopping[index] = true;
        BdmDiskWakeEventsThread(bdmPool, index);
    }

    for (uint32_t index = 0; index < BDM_WORKER_THREAD_NUM; index++) {
        if (bdmPool->eventThreadStarted[index]) {
            (void)pthread_join(bdmPool->threadId[index], NULL);
            bdmPool->eventThreadStarted[index] = false;
        }
        if (bdmPool->ringInited[index]) {
            io_uring_queue_exit(&bdmPool->ring[index]);
            bdmPool->ringInited[index] = false;
            pthread_mutex_destroy(&bdmPool->ringLock[index]);
        }
        if (bdmPool->ringSlotsInited[index]) {
            sem_destroy(&bdmPool->ringSlots[index]);
            bdmPool->ringSlotsInited[index] = false;
        }
        bdmPool->ringInflight[index] = 0;
        bdmPool->ringSqpoll[index] = false;
        bdmPool->stopping[index] = false;
    }
}

int32_t BdmDiskExit(void)
{
    g_bdmForceNew = 0;
    BdmDiskCleanupCurrentSyncUringCtx();
    BdmDiskCleanupAllSyncUringCtxs();
    if (g_bdmIoEngine == BDM_IO_ENGINE_SYNC) {
        if (g_bdmSyncThreadPool != NULL) {
            BDM_THREAD_POOL_S *threadPool = g_bdmSyncThreadPool;
            g_bdmSyncThreadPool = NULL;
            (void)BdmThreadPoolDestroy(threadPool, 0);
        }
        return BDM_CODE_OK;
    }

    BdmDiskThreadPoolCleanup(&g_bdmThreadPool);
    return BDM_CODE_OK;
}

int32_t BdmDiskInit(void)
{
    g_bdmForceNew = 0;
    BDM_SPIN_INIT(&(g_bdmDisk.lock), 0);
    D_INIT_LIST_HEAD(&(g_bdmDisk.head));
    g_bdmDisk.num = 0UL;
    BdmRegOpsWithCreate(BdmDiskCreate);
    BdmRegOpsWithDestroy(BdmDiskDestroy);
    BdmRegOpsWithReset(BdmDiskReset);
    int32_t ret = BdmDiskThreadPoolInit();
    if (UNLIKELY(ret != BDM_CODE_OK)) {
        BDM_LOGERROR(0, "Bdm disk init thread pool failed, ret(%d).", ret);
    }
    return ret;
}

void BdmDiskSetForceNew(uint32_t forceNew)
{
    g_bdmForceNew = forceNew != 0 ? 1U : 0U;
}
