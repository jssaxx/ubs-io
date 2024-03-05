/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <libaio.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <linux/version.h>
#include "securec.h"
#include "dlist.h"
#include "bdm_threadpool.h"
#include "bdm_obj.h"
#include "bdm_allocator.h"
#include "bdm_common.h"
#include "bdm_core.h"
#include "bdm_disk.h"

#define BDM_OPEN_FILE_PERMISSION 0640

#define BDM_PAGE_SIZE getpagesize()

#define BDM_DISK_MAGIC (0xFFDDCBAABCDDFF)

#define BDM_IOCTX_EVENTS_NUM (128UL)
#define BDM_IO_RETRY_NUM (3UL)

#define BDM_AYSNC_IO_FD_NUM (8UL)

#define BDM_WORKER_THREAD_NUM (8UL)

#define BDM_DEFAULT_THREAD_NUM (1UL)

#define BDM_BIND_CPU_START (60UL)

static uint32_t g_bdmAioIsDirect = FALSE;

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
    int32_t cpus[BDM_WORKER_THREAD_NUM];
    BdmThreadCtx threadCtx[BDM_WORKER_THREAD_NUM];
    pthread_t threadId[BDM_WORKER_THREAD_NUM];
    int32_t efd[BDM_WORKER_THREAD_NUM];
    int32_t epfd[BDM_WORKER_THREAD_NUM];
    io_context_t ioctx[BDM_WORKER_THREAD_NUM];
    struct epoll_event epevent[BDM_WORKER_THREAD_NUM];
    BDM_THREAD_POOL_S *pool[BDM_WORKER_THREAD_NUM];
} BdmThreadPool;

typedef struct {
    struct iocb iocb;
    void *buf;
    uint64_t len;
    uint64_t chunkId;
    uint64_t offset;
    uint32_t retryNum;
    uint32_t isRead;
    BdmIoCb cb;
    void *ctx;
    void *item;
    uint64_t ts;
} BdmIoContext;

static BdmDiskMgr g_bdmDisk = { 0 };

static uint64_t g_bdmIndex = 0;

static BdmThreadPool g_bdmThreadPool;

uint64_t BdmDiskInnerReadWrite(int32_t fd, char *buff, uint64_t len, uint64_t offset, int32_t isRead)
{
    uint64_t remain = len;
    int64_t rc = 0;

    while (remain > 0) {
        if (isRead) {
            rc = pread(fd, buff + (len - remain), remain, offset + (len - remain));
        } else {
            rc = pwrite(fd, buff + (len - remain), remain, offset + (len - remain));
        }
        if (rc <= 0) {
            BDM_LOGWARN(0, "%s failed (%s), fd %d, rc %d, len %d, off %lu, remain %lu.",
                isRead ? "Read" : "Write", strerror(errno), fd, rc, len, offset, remain);
            break;
        }
        remain -= rc;
    }
    return (len - remain);
}

int32_t BdmDiskWriteMeta(uintptr_t itemPtr, uint64_t offset, void *buf, uint64_t len)
{
    BdmDiskItem *item = (BdmDiskItem *)itemPtr;

    uint64_t rwOffset = item->offset + item->metaOffset + offset;
    uint64_t rwLen = BdmDiskInnerReadWrite(item->fd, (char*)buf, len, rwOffset, FALSE);
    if (rwLen != len) {
        BDM_LOGWARN(0, "Write disk failed, need(%lu) real(%lu) device(%s).", len, rwLen, item->name);
        return BDM_CODE_ERR;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskReadMeta(uintptr_t itemPtr, uint64_t offset, void *buf, uint64_t len)
{
    BdmDiskItem *item = (BdmDiskItem *)itemPtr;

    uint64_t rwOffset = item->offset + item->metaOffset + offset;
    uint64_t rwLen = BdmDiskInnerReadWrite(item->fd, (char*)buf, len, rwOffset, TRUE);
    if (rwLen != len) {
        BDM_LOGWARN(0, "Read disk failed, need(%lu) real(%lu) device(%s).", len, rwLen, item->name);
        return BDM_CODE_ERR;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskAlloc(uintptr_t objPtr, uint64_t bucketId, uint64_t bucketOffset, uint64_t len, uint64_t *chunkId)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;

    if (len == 0 || len % item->minChunkSize != 0) {
        BDM_LOGWARN(0, "Invalid len, bdm id(%u) length(%lu).", obj->bdmId, len);
        return BDM_CODE_INVALID_PARAM;
    }

    int32_t ret = BdmAllocatorAllocChunk(item->allocator, bucketId, bucketOffset, len, chunkId);
    if (ret != BDM_CODE_OK) {
        BDM_LOGWARN(0, "Alloc chunk failed, bdm id(%u) length(%lu).", obj->bdmId, len);
        return ret;
    }

    return BDM_CODE_OK;
}

int32_t BdmDiskFree(uintptr_t objPtr, uint64_t len, uint64_t chunkId)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;

    int32_t ret = BdmAllocatorFreeChunk(item->allocator, len, chunkId);
    if (ret != BDM_CODE_OK) {
        BDM_LOGWARN(0, "Free chunk failed, bdm id(%u) length(%lu).", obj->bdmId, len);
        return ret;
    }

    return BDM_CODE_OK;
}

int32_t BdmDiskRead(uintptr_t objPtr, uint64_t chunkId, uint64_t offset, void *buf, uint64_t len)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;

    int32_t ret = BdmAllocatorCheckChunk(item->allocator, chunkId, offset, len);
    if (ret != BDM_CODE_OK) {
        BDM_LOGWARN(0, "Bdm read check failed, bdm id(%u) chunk id(%lu) ret(%d).", obj->bdmId, chunkId, ret);
        return ret;
    }

    int32_t fd = item->fd;
    uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * chunkId + offset;
    uint64_t rwLen = BdmDiskInnerReadWrite(fd, (char*)buf, len, rwOffset, TRUE);
    if (rwLen != len) {
        BDM_LOGWARN(0, "Read disk failed, need(%lu) real(%lu) device(%s).", len, rwLen, item->name);
        return BDM_CODE_ERR;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskWrite(uintptr_t objPtr, uint64_t chunkId, uint64_t offset, void *buf, uint64_t len)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;

    int32_t ret = BdmAllocatorCheckChunk(item->allocator, chunkId, offset, len);
    if (ret != BDM_CODE_OK) {
        BDM_LOGWARN(0, "Bdm write check failed, bdm id(%u) chunk id(%lu) ret(%d).", obj->bdmId, chunkId, ret);
        return ret;
    }

    int32_t fd = item->fd;
    uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * chunkId + offset;
    uint64_t rwLen = BdmDiskInnerReadWrite(fd, (char*)buf, len, rwOffset, FALSE);
    if (rwLen != len) {
        BDM_LOGWARN(0, "Write disk failed, need(%lu) real(%lu) device(%s).", len, rwLen, item->name);
        return BDM_CODE_ERR;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskReset(uintptr_t objPtr)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;

    int32_t ret = BdmAllocatorResetChunk(item->allocator);
    if (ret != BDM_CODE_OK) {
        BDM_LOGWARN(0, "Bdm reset failed, bdm id(%u) ret(%d).", obj->bdmId, ret);
        return ret;
    }

    return BDM_CODE_OK;
}

int32_t BdmDiskGetNextChunk(uintptr_t objPtr, uint64_t *chunkId, uint64_t *chunkSize,
    uint64_t *bucketId, uint64_t *bucketOffset)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;

    int32_t ret = BdmAllocatorGetNextChunk(item->allocator, chunkId, chunkSize, bucketId, bucketOffset);
    if (ret != BDM_CODE_OK && ret != BDM_CODE_SCAN_OFF) {
        BDM_LOGWARN(0, "Bdm get next chunk failed, bdm id(%u) ret(%d).", obj->bdmId, ret);
        return ret;
    }

    return ret;
}

int32_t BdmDiskGetCap(uintptr_t objPtr, uint64_t *totalSize, uint64_t *usedSize)
{
    BdmObj *obj = (BdmObj *)objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;

    int32_t ret = BdmAllocatorGetCap(item->allocator, totalSize, usedSize);
    if (ret != BDM_CODE_OK) {
        BDM_LOGWARN(0, "Bdm get cap failed, bdm id(%u) ret(%d).", obj->bdmId, ret);
        return ret;
    }

    return ret;
}

void BdmDiskAIOCallback(io_context_t ctx, struct iocb *iocb, long res, long res2)
{
    UNREFERENCE_PARAM(ctx);
    UNREFERENCE_PARAM(iocb);
    UNREFERENCE_PARAM(res);
    UNREFERENCE_PARAM(res2);
}

int32_t BdmDiskSubmitAIO(void **argList, uint32_t argNum, void *ctx)
{
    BdmThreadCtx *threadCtx = (BdmThreadCtx *)ctx;
    BdmThreadPool *bdmPool = (BdmThreadPool *)threadCtx->ctx;
    struct iocb *iocbPs[BDM_BATCH_HANDLE_NUM];
    int32_t ret;

    uint32_t threadIdx = threadCtx->index;

    static uint64_t submitIndex = 0;
    uint64_t fdIdx = ATOMIC_INC(&submitIndex) % BDM_AYSNC_IO_FD_NUM;

    uint32_t i;
    for (i = 0; i < argNum; i++) {
        BdmIoContext *bdmIo = (BdmIoContext *)argList[i];
        if (g_bdmAioIsDirect == FALSE) {
        }
        iocbPs[i] = &bdmIo->iocb;
        BdmDiskItem *item = (BdmDiskItem *)bdmIo->item;
        uint64_t rwOffset = item->offset + item->dataOffset + item->minChunkSize * bdmIo->chunkId + bdmIo->offset;
        if (bdmIo->isRead) {
            io_prep_pread(&bdmIo->iocb, item->asyncfd[fdIdx], bdmIo->buf, bdmIo->len, rwOffset);
        } else {
            io_prep_pwrite(&bdmIo->iocb, item->asyncfd[fdIdx], bdmIo->buf, bdmIo->len, rwOffset);
        }
        io_set_eventfd(&bdmIo->iocb, bdmPool->efd[threadIdx]);
        io_set_callback(&bdmIo->iocb, BdmDiskAIOCallback);
    }

    ret = io_submit(bdmPool->ioctx[threadIdx], argNum, &(iocbPs[0]));
    if (ret != (int32_t)argNum) {
        BDM_LOGWARN(0, "Failed to io submit, size %d, %d, %s", ret, errno, strerror(errno));
        return BDM_CODE_ERR;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskHandleAIO(BdmAsyncOpsReq *req, int32_t isRead)
{
    BdmObj *obj = (BdmObj *)req->objPtr;
    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;

    int32_t ret = BdmAllocatorCheckChunk(item->allocator, req->chunkId, req->offset, req->len);
    if (ret != BDM_CODE_OK) {
        BDM_LOGWARN(0, "Bdm read check failed, bdmId(%u) chunkId(%lu) ret(%d).", obj->bdmId, req->chunkId, ret);
        return ret;
    }

    static size_t ctxLen = sizeof(BdmIoContext);
    if (ctxLen > BDM_IO_CTX_RES_LEN) {
        BDM_LOGWARN(0, "Impossible, need len(%u).", ctxLen);
        return BDM_CODE_ERR;
    }

    BdmIoContext *bdmIo = (BdmIoContext *)req->ioCtx->res;
    if (bdmIo == NULL) {
        BDM_LOGWARN(0, "Malloc iocontext failed.");
        return BDM_CODE_ERR;
    }

    bdmIo->buf = req->buf;
    bdmIo->len = req->len;
    bdmIo->chunkId = req->chunkId;
    bdmIo->offset = req->offset;
    bdmIo->retryNum = 0;
    bdmIo->isRead = isRead;
    bdmIo->cb = req->ioCtx->cb;
    bdmIo->ctx = req->ioCtx->ctx;
    bdmIo->item = (void *)item;

    void *argList[1UL] = {(void *)bdmIo};

    uint64_t index = ATOMIC_INC(&g_bdmIndex) % BDM_WORKER_THREAD_NUM;
    if (g_bdmAioIsDirect) {
        ret = BdmDiskSubmitAIO(argList, 1UL, (void *)&g_bdmThreadPool.threadCtx[index]);
    } else {
        ret = BdmThreadPoolAdd(g_bdmThreadPool.pool[index], NULL, (void *)bdmIo);
    }
    return ret;
}

int32_t BdmDiskReadAsync(BdmAsyncOpsReq *req)
{
    int32_t ret;
    ret = BdmDiskHandleAIO(req, TRUE);
    return ret;
}

int32_t BdmDiskWriteAsync(BdmAsyncOpsReq *req)
{
    int32_t ret;
    ret = BdmDiskHandleAIO(req, FALSE);
    return ret;
}

int32_t BdmDiskCreateCheck(BdmCreatePara *para)
{
    if (strlen(para->name) == 0UL || strlen(para->sn) == 0UL) {
        BDM_LOGERROR(0, "Invalid name or sn.");
        return BDM_CODE_INVALID_PARAM;
    }

    if (para->length == 0UL) {
        BDM_LOGERROR(0, "Invalid length(%lu).", para->length);
        return BDM_CODE_INVALID_PARAM;
    }

    if (para->minChunkSize == 0UL) {
        BDM_LOGERROR(0, "Invalid min chunk size(%lu).", para->minChunkSize);
        return BDM_CODE_INVALID_PARAM;
    }
    if (para->maxChunkSize == 0UL) {
        BDM_LOGERROR(0, "Invalid max chunk size(%lu), min chunk size(%lu).", para->maxChunkSize, para->minChunkSize);
        return BDM_CODE_INVALID_PARAM;
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskRetryIo(io_context_t ctx, struct iocb** iocbPP)
{
    BdmIoContext *bdmIo = (BdmIoContext *)*iocbPP;
    int32_t j = 0;
    do {
        j = io_submit(ctx, 1, iocbPP);
        if (j == 1) {
            return 0;
        } else if (j == (-EAGAIN)) {
            BDM_LOGWARN(0, "retry: io_submit busy, chunkId %ld", bdmIo->chunkId);
            sched_yield();
        } else {
            BDM_LOGERROR(0, "doWork: io_submit failed, %s, j %d", strerror(errno), j);
            return -1;
        }
    } while (j == (-EAGAIN));
    return -1;
}

static void BdmCompleteIOHandler(const struct io_event *ioEvent, BdmThreadPool *bdmPool, uint32_t threadIdx)
{
    struct iocb *iocbP = NULL;
    io_callback_t bdmIOCallback = (io_callback_t)ioEvent->data;
    BdmIoContext *bdmIo = (BdmIoContext *)ioEvent->obj;

    if (bdmIOCallback == (io_callback_t)0) {
        BDM_LOGERROR(0, "Unexpected IO request with chunkId %lu", bdmIo->chunkId);
        return;
    }

    if ((long)(ioEvent->res) <= 0 || ioEvent->res2 != 0) {
        if (bdmIo->retryNum < BDM_IO_RETRY_NUM) {
            BDM_LOGERROR(0, "retry: chunkId %ld, res %ld res2 %ld", bdmIo->chunkId, ioEvent->res, ioEvent->res2);
            bdmIo->retryNum++;
            iocbP = &bdmIo->iocb;
            if (BdmDiskRetryIo(bdmPool->ioctx[threadIdx], &iocbP) == 0) {
                return;
            }
        }
        BDM_LOGERROR(0, "failed chunkId %ld, res %ld res2 %ld", bdmIo->chunkId, ioEvent->res, ioEvent->res2);
        bdmIo->cb(bdmIo->ctx, -1);
    } else {
        bdmIo->cb(bdmIo->ctx, 0);
    }
    return;
}

void *BdmDiskEventsThread(void *argsP)
{
    struct io_event events[BDM_IOCTX_EVENTS_NUM];
    BdmThreadCtx *threadCtx = (BdmThreadCtx *)argsP;
    BdmThreadPool *bdmPool = (BdmThreadPool *)threadCtx->ctx;

    uint32_t threadIdx = threadCtx->index;
    struct epoll_event *epeventP = &bdmPool->epevent[threadIdx];
    int32_t epfd = bdmPool->epfd[threadIdx];
    int32_t recvs;
    int32_t fdNum = 128UL;

    BDM_LOGINFO(0, "bdm disk events thread start.");

    BdmThreadBindCPUs("bdm_events", bdmPool->cpus[threadIdx]);
    while (true) {
        if (epoll_wait(epfd, epeventP, fdNum, -1) != 1) {
            BDM_LOGERROR(0, "disk event epoll_wait, error(%s).", strerror(errno));
        }

        recvs = io_getevents(bdmPool->ioctx[threadIdx], 1UL, BDM_IOCTX_EVENTS_NUM, events, NULL);
        if (recvs <= 0) {
            continue;
        }
        
        for (int k = 0; k < recvs; k++) {
            BdmCompleteIOHandler(&events[k], bdmPool, threadIdx);
            events[k].obj = NULL;
        }
    }
    
    return NULL;
}

void BdmDiskCloseDisk(BdmDiskItem *item)
{
    uint32_t index;

    if (item->fd >= 0) {
        close(item->fd);
        item->fd = -1;
    }
    for (index = 0; index < BDM_AYSNC_IO_FD_NUM; index++) {
        if (item->asyncfd[index] >= 0) {
            close(item->asyncfd[index]);
            item->asyncfd[index] = -1;
        }
    }
    return;
}

int32_t BdmDiskOpenDisk(BdmCreatePara *para, BdmDiskItem *item)
{
    uint32_t index;
    int ret;

    item->totalSize = para->length;
    item->offset = para->offset;
    item->headSize = sizeof(BdmDiskHead);
    item->fd = -1;

    for (index = 0; index < BDM_AYSNC_IO_FD_NUM; index++) {
        item->asyncfd[index] = -1;
    }

    ret = memcpy_s(item->name, BDM_NAME_LEN, para->name, BDM_NAME_LEN);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR("Memcpy bdm name failed, name(%s).", para->name);
        return BDM_CODE_ERR;
    }

    ret = memcpy_s(item->sn, BDM_SN_LEN, para->sn, BDM_SN_LEN);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR("Memcpy bdm name failed, name(%s).", para->name);
        return BDM_CODE_ERR;
    }

    item->fd = open(item->name, O_RDWR | O_CREAT | O_SYNC, BDM_OPEN_FILE_PERMISSION);
    if (item->fd < 0) {
        BDM_LOGERROR(0, "Open device(%s) failed, errno(%s).", item->name, strerror(errno));
        return BDM_CODE_ERR;
    }
    for (index = 0; index < BDM_AYSNC_IO_FD_NUM; index++) {
        item->asyncfd[index] = open(item->name, O_RDWR | O_DIRECT);
        if (item->asyncfd[index] < 0) {
            BDM_LOGERROR(0, "Open device(%s) failed, errno(%s).", item->name, strerror(errno));
            BdmDiskCloseDisk(item);
            return BDM_CODE_ERR;
        }
    }
    return BDM_CODE_OK;
}

bool BdmDiskCheckItem(const BdmDiskHead *head, const BdmDiskItem *item)
{
    return (head->magic == BDM_DISK_MAGIC &&
            head->bdmId == item->bdmId &&
            head->minChunkSize == item->minChunkSize &&
            head->maxChunkSize == item->maxChunkSize &&
            head->totalSize == item->totalSize &&
            head->metaOffset == item->metaOffset &&
            head->metaLength == item->metaLength &&
            head->dataOffset == item->dataOffset &&
            head->dataLength == item->dataLength &&
            head->offset == item->offset &&
            head->headSize == item->headSize);
}

int32_t BdmDiskPreCheckFileLen(int32_t fd, uint64_t length)
{
    off_t offset = lseek(fd, 0, SEEK_END);
    if (offset < (off_t)length) {
        return BDM_CODE_ERR;
    } else {
        return BDM_CODE_OK;
    }
}

int32_t BdmDiskRestoreCheckOK(BdmDiskItem *item)
{
    BdmDiskHead head;

    int32_t ret = BdmDiskPreCheckFileLen(item->fd, item->headSize + item->offset);
    if (ret != BDM_CODE_OK) {
        return ret;
    }

    uint64_t rwLen = BdmDiskInnerReadWrite(item->fd, (char*)&head, item->headSize, item->offset, TRUE);
    if (rwLen != item->headSize) {
        BDM_LOGWARN(0, "Read disk failed, need(%lu) real(%lu) device(%s).", item->headSize, rwLen, item->name);
        return BDM_CODE_ERR;
    }

    uint64_t metaSize, dataSize;
    ret = BdmAllocatorGetSplitSize(item->headSize, item->minChunkSize, item->totalSize, &metaSize, &dataSize);
    if (ret != BDM_CODE_OK) {
        return ret;
    }

    item->metaOffset = item->headSize;
    item->metaLength = metaSize;
    item->dataOffset = ROUND_UP(item->headSize + metaSize, BDM_ALIGN_SIZE);
    item->dataLength = dataSize;

    if (!BdmDiskCheckItem(&head, item)) {
        return BDM_CODE_ERR;
    }

    return BDM_CODE_OK;
}

int32_t BdmDiskRestoreAllocator(BdmDiskItem *item)
{
    int32_t ret = BdmDiskRestoreCheckOK(item);
    if (ret != BDM_CODE_OK) {
        return ret;
    }

    return BDM_CODE_ERR; // 临时关闭故障恢复，方便B002转测

    BdmAllocatorPara allocatorPara = { 0 };
    allocatorPara.metaOps.itemPtr = (uintptr_t)item;
    allocatorPara.metaOps.writeMeta = BdmDiskWriteMeta;
    allocatorPara.metaOps.readMeta = BdmDiskReadMeta;
    allocatorPara.minChunkSize = item->minChunkSize;
    allocatorPara.maxChunkSize = item->maxChunkSize;
    allocatorPara.totalSize = item->dataLength; /* 只有数据区用于chunk分配 */

    item->allocator = BdmAllocatorCreate(&allocatorPara, 1UL);
    if (item->allocator == 0L) {
        BDM_LOGERROR(0, "Restore allocator failed.");
        return BDM_CODE_ERR;
    }

    return BDM_CODE_OK;
}

static int32_t BdmDiskFillDiskHead(BdmDiskHead *head, BdmDiskItem *item)
{
    int32_t ret;

    head->magic = BDM_DISK_MAGIC;
    head->bdmId = item->bdmId;
    head->minChunkSize = item->minChunkSize;
    head->maxChunkSize = item->maxChunkSize;
    head->totalSize = item->totalSize;

    head->metaOffset = item->metaOffset;
    head->metaLength = item->metaLength;
    head->dataOffset = item->dataOffset;
    head->dataLength = item->dataLength;

    ret = memcpy_s(head->name, BDM_NAME_LEN, item->name, BDM_NAME_LEN);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Memcpy bdm name failed, name(%s).", item->name);
        return BDM_CODE_ERR;
    }

    ret = memcpy_s(head->sn, BDM_SN_LEN, item->sn, BDM_SN_LEN);
    if (ret != BDM_CODE_OK) {
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
    int32_t ret;

    ret = BdmDiskFillDiskHead(&head, item);
    if (ret != BDM_CODE_OK) {
        return ret;
    }

    void *restoreBuff = malloc(BDM_RESTORE_META_SIZE);
    if (restoreBuff == NULL) {
        BDM_LOGERROR(0, "Malloc restore buff failed.");
        return BDM_CODE_ERR;
    }
    ret |= memset_s(restoreBuff, BDM_RESTORE_META_SIZE, 0, BDM_RESTORE_META_SIZE);
    ret |= memcpy_s(restoreBuff, BDM_RESTORE_META_SIZE, &head, sizeof(BdmDiskHead));
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Memcpy restore buff failed, name(%s).", item->name);
        free(restoreBuff);
        return BDM_CODE_ERR;
    }

    uint64_t rwOffset = item->offset;
    uint64_t rwLen = BdmDiskInnerReadWrite(item->fd, (char*)restoreBuff, BDM_RESTORE_META_SIZE, rwOffset, FALSE);
    if (rwLen != BDM_RESTORE_META_SIZE) {
        BDM_LOGWARN(0, "Write disk failed, need(%lu) real(%lu) device(%s).",
            BDM_RESTORE_META_SIZE, rwLen, item->name);
        free(restoreBuff);
        return BDM_CODE_ERR;
    }
    free(restoreBuff);
    return BDM_CODE_OK;
}

int32_t BdmDiskCreateAllocator(BdmDiskItem *item)
{
    int32_t ret = BdmDiskRestoreAllocator(item);
    if (ret == BDM_CODE_OK) {
        return ret;
    }

    BdmAllocatorPara allocatorPara = { 0 };

    uint64_t metaSize, dataSize;
    ret = BdmAllocatorGetSplitSize(item->headSize, item->minChunkSize, item->totalSize, &metaSize, &dataSize);
    if (ret != BDM_CODE_OK) {
        return ret;
    }

    item->metaOffset = item->headSize;
    item->metaLength = metaSize;
    item->dataOffset = ROUND_UP(item->headSize + metaSize, BDM_ALIGN_SIZE);
    item->dataLength = dataSize;

    ret = BdmDiskStoreDiskHead(item);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Store disk head failed.");
        return ret;
    }

    allocatorPara.metaOps.itemPtr = (uintptr_t)item;
    allocatorPara.metaOps.writeMeta = BdmDiskWriteMeta;
    allocatorPara.metaOps.readMeta = BdmDiskReadMeta;
    allocatorPara.minChunkSize = item->minChunkSize;
    allocatorPara.maxChunkSize = item->maxChunkSize;
    allocatorPara.totalSize = item->dataLength; /* 只有数据区用于chunk分配 */

    item->allocator = BdmAllocatorCreate(&allocatorPara, 0UL);
    if (item->allocator == 0L) {
        BDM_LOGERROR(0, "Create allocator failed.");
        return BDM_CODE_ERR;
    }

    return BDM_CODE_OK;
}

void BdmDiskDestoryAllocator(BdmDiskItem *item)
{
    int32_t ret;

    ret = BdmAllocatorDestory(item->allocator);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Destory allocator failed.");
    }

    return;
}

void BdmDiskFillBdmObj(BdmObj *obj, BdmDiskItem *item)
{
    obj->bdmId = item->bdmId;
    obj->totalSize = item->totalSize;
    obj->minChunkSize = item->minChunkSize;
    obj->maxChunkSize = item->maxChunkSize;

    obj->ops.alloc = BdmDiskAlloc;
    obj->ops.free = BdmDiskFree;
    obj->ops.read = BdmDiskRead;
    obj->ops.write = BdmDiskWrite;
    obj->ops.readAsync = BdmDiskReadAsync;
    obj->ops.writeAsync = BdmDiskWriteAsync;
    obj->ops.reset = BdmDiskReset;
    obj->ops.nextchunk = BdmDiskGetNextChunk;
    obj->ops.getcap = BdmDiskGetCap;

    obj->opsInfo = (BdmOpsInfo)item;
}

static void BdmDiskFillItem(BdmDiskItem *item, BdmCreatePara *para, uint32_t bdmId)
{
    item->bdmId = bdmId;
    item->minChunkSize = para->minChunkSize;
    item->maxChunkSize = para->maxChunkSize;
    return;
}

BdmObj *BdmDiskCreate(uint32_t bdmId, uintptr_t createPara)
{
    BdmCreatePara *para = (BdmCreatePara *)createPara;
    int ret;

    ret = BdmDiskCreateCheck(para);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Bdm disk create check failed, ret(%d).", ret);
        return NULL;
    }

    BdmDiskItem *item = (BdmDiskItem *)malloc(sizeof(BdmDiskItem));
    if (item == NULL) {
        BDM_LOGERROR(0, "Bdm disk alloc item context failed.");
        return NULL;
    }

    BdmDiskFillItem(item, para, bdmId);

    ret = BdmDiskOpenDisk(para, item);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Bdm disk create inter elem failed, ret(%d).", ret);
        free(item);
        return NULL;
    }

    ret = BdmDiskCreateAllocator(item);
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Bdm disk create allocator failed.");
        BdmDiskCloseDisk(item);
        free(item);
        return NULL;
    }

    BdmObj *obj = (BdmObj *)malloc(sizeof(BdmObj));
    if (obj == NULL) {
        BDM_LOGERROR(0, "Malloc obj failed.");
        BdmDiskDestoryAllocator(item);
        BdmDiskCloseDisk(item);
        free(item);
        return NULL;
    }
    BdmDiskFillBdmObj(obj, item);

    BDM_SPIN_LOCK(&g_bdmDisk.lock);
    DListAddTail(&item->node, &g_bdmDisk.head);
    g_bdmDisk.num++;
    BDM_SPIN_UNLOCK(&g_bdmDisk.lock);

    BDM_LOGINFO(0, "Bdm disk create succeed, bdm id(%u) size(%lu).", bdmId, para->length);

    return obj;
}

int32_t BdmDiskDestory(BdmObj *obj)
{
    BDM_LOGINFO(0, "Bdm disk destory, bdm id(%u).", obj->bdmId);

    BdmDiskItem *item = (BdmDiskItem *)obj->opsInfo;
    BdmDiskDestoryAllocator(item);
    BdmDiskCloseDisk(item);

    BDM_SPIN_LOCK(&g_bdmDisk.lock);
    DListDel(&item->node);
    g_bdmDisk.num--;
    BDM_SPIN_UNLOCK(&g_bdmDisk.lock);

    free(item);
    free(obj);
    return BDM_CODE_OK;
}

static int32_t BdmPoolInit(BdmThreadPool *bdmPool, uint32_t index)
{
    int32_t ret;
    bdmPool->efd[index] = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (bdmPool->efd[index] < 0) {
        BDM_LOGERROR(0, "Eventfd failed, errno(%s).", strerror(errno));
        return BDM_CODE_ERR;
    }
    ret = memset_s(&(bdmPool->ioctx[index]), sizeof(io_context_t), 0, sizeof(io_context_t));
    if (ret != 0) {
        BDM_LOGERROR(0, "Memset ioctx failed, ret(%d).", ret);
        return BDM_CODE_ERR;
    }
    ret = io_setup(BDM_IOCTX_EVENTS_NUM, &(bdmPool->ioctx[index]));
    if (ret != 0) {
        BDM_LOGERROR(0, "Io setup failed, errno(%s).", strerror(errno));
        return BDM_CODE_ERR;
    }
    bdmPool->epfd[index] = epoll_create(1);
    if (bdmPool->epfd[index] < 0) {
        BDM_LOGERROR(0, "Epoll create failed, errno(%s).", strerror(errno));
        return BDM_CODE_ERR;
    }
    bdmPool->epevent[index].events = EPOLLIN | EPOLLET;
    bdmPool->epevent[index].data.ptr = NULL;
    ret = epoll_ctl(bdmPool->epfd[index], EPOLL_CTL_ADD, bdmPool->efd[index], &(bdmPool->epevent[index]));
    if (ret != 0) {
        BDM_LOGERROR(0, "Epoll ctl failed, errno(%s).", strerror(errno));
        return BDM_CODE_ERR;
    }
    bdmPool->cpus[index] = BDM_BIND_CPU_START + (int32_t)index;
    return ret;
}

int32_t BdmDiskThreadPoolInit(void)
{
    int32_t ret;

    BdmThreadPool *bdmPool = &g_bdmThreadPool;

    uint32_t index;
    for (index = 0; index < BDM_WORKER_THREAD_NUM; index++) {
        BdmThreadCtx *threadCtx = &bdmPool->threadCtx[index];
        threadCtx->index = index;
        threadCtx->ctx = (void *)bdmPool;
        ret = BdmPoolInit(bdmPool, index);
        if (ret != 0) {
            BDM_LOGERROR(0, "Bdm pool init failed, ret(%d)", ret);
            return BDM_CODE_ERR;
        }
        
        ret = pthread_create(&bdmPool->threadId[index], NULL, BdmDiskEventsThread, (void*)threadCtx);
        if (ret != 0) {
            BDM_LOGERROR(0, "Pthread create failed, errno(%s).", strerror(errno));
            return BDM_CODE_ERR;
        }

        char threadName[BDM_THREAD_NAME_LEN] = {0};
        ret = sprintf_s(threadName, BDM_THREAD_NAME_LEN, "bdm_events");
        if (ret < 0) {
            BDM_LOGERROR(0, "sprintf_s failed, ret(%d).", ret);
            return BDM_CODE_ERR;
        }
        pthread_setname_np(bdmPool->threadId[index], threadName);
        BDM_BIND_CPU_S cpus;
        cpus.cpunum = BDM_DEFAULT_THREAD_NUM;
        cpus.cpus[0] = bdmPool->cpus[index];
        BDM_BATCH_CTX_S batchCtx;
        batchCtx.batchHandle = BdmDiskSubmitAIO;
        batchCtx.batchCtx = (void *)threadCtx;
        bdmPool->pool[index] = BdmThreadPoolCreate(BDM_DEFAULT_THREAD_NUM, 1024UL, &cpus, "bdm_disk",
            &batchCtx);
        if (bdmPool->pool[index] == NULL) {
            BDM_LOGERROR(0, "Pthread pool create failed, errno(%s).", strerror(errno));
            return BDM_CODE_ERR;
        }
    }
    return BDM_CODE_OK;
}

int32_t BdmDiskInit(void)
{
    BDM_LOGINFO(0, "Bdm disk init.");

    BDM_SPIN_INIT(&(g_bdmDisk.lock), 0);
    D_INIT_LIST_HEAD(&(g_bdmDisk.head));
    g_bdmDisk.num = 0UL;

    BdmRegOpsWithCreate(BdmDiskCreate);
    BdmRegOpsWithDestory(BdmDiskDestory);

    int32_t ret = BdmDiskThreadPoolInit();
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Bdm disk init thread pool failed, ret(%d).", ret);
        return ret;
    }

    BDM_LOGINFO(0, "Bdm disk init succeed.");

    return BDM_CODE_OK;
}

