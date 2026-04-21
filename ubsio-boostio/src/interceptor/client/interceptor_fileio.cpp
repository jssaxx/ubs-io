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

#include <algorithm>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <vector>
#include "securec.h"
#include "bio_def.h"
#include "message_op.h"
#include "interceptor_log.h"
#include "interceptor_net.h"
#include "proxy_operations.h"
#include "interceptor_context.h"
#include "bio_monotonic.h"
#include "bufvec.h"

using namespace ock::bio;

#define CONTEXT BioInterceptorContext::GetInstance()

namespace {
struct CachedWriteBlock {
    uintptr_t address = 0;
    uint64_t mrOffset = 0;

    ~CachedWriteBlock()
    {
        if (address != 0) {
            InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
            address = 0;
            mrOffset = 0;
        }
    }
};
}

static thread_local CachedWriteBlock g_cachedWriteBlock;

static bool AcquireLargeWriteBlock(uintptr_t &shmAddr, uint64_t &mrOffset, bool &fromCache)
{
    if (g_cachedWriteBlock.address != 0) {
        shmAddr = g_cachedWriteBlock.address;
        mrOffset = g_cachedWriteBlock.mrOffset;
        fromCache = true;
        return true;
    }

    auto ret = InterceptorClientNetService::Instance().AllocShmBlock(shmAddr, mrOffset);
    if (UNLIKELY(ret != BIO_OK || shmAddr == 0)) {
        CLOG_ERROR("AcquireLargeWriteBlock: alloc shm block failed, ret:" << ret << ".");
        return false;
    }

    fromCache = false;
    return true;
}

static void CacheLargeWriteBlock(uintptr_t shmAddr, uint64_t mrOffset)
{
    g_cachedWriteBlock.address = shmAddr;
    g_cachedWriteBlock.mrOffset = mrOffset;
}

static void ReleaseLargeWriteBlock(uintptr_t shmAddr, uint64_t mrOffset, bool fromCache)
{
    if (fromCache) {
        return;
    }
    InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
    (void)shmAddr;
}

static char *GetSmallWriteScratch(size_t reqLen)
{
    static thread_local std::vector<char> scratch;
    if (scratch.size() < reqLen) {
        scratch.resize(reqLen);
    }
    return scratch.data();
}

ssize_t ProxyOperations::PreadInner(int fd, void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        CLOG_ERROR("Get fd:" << fd << "failed.");
        return -1;
    }

    if (count > MAX_LARGE_WRITE_SIZE) {
        return CONTEXT.GetOperations()->pread(fd, buf, count, offset);
    }

    if (count <= MAX_SMALL_WRITE_SIZE) {
        return PreadSmallInner(fd, buf, count, offset);
    } else {
        return PreadLargeInner(fd, buf, count, offset);
    }
}

ssize_t ProxyOperations::PreadInner(int fd, BufVec &bufVec, off_t offset)
{
    if (bufVec.size > MAX_LARGE_WRITE_SIZE) {
        errno = EIO;
        return -1;
    }

    if (bufVec.size <= MAX_SMALL_WRITE_SIZE) {
        return PreadSmallInner(fd, bufVec, offset);
    }
    return PreadLargeInner(fd, bufVec, offset);
}

ssize_t ProxyOperations::PreadSmallInner(int fd, void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    InterceptorPreadIn request;
    request.pid = static_cast<uint32_t>(getpid());
    request.fd = fd;
    request.inode = file->GetInode();
    request.offset = offset;
    request.nbytes = count;
    request.startTime = Monotonic::TimeNs();

    InterceptorPreadOut *resp = nullptr;
    uint64_t rspLen = 0;
    auto ret = InterceptorClientNetService::Instance().SendSync<InterceptorPreadIn, InterceptorPreadOut>(INVALID_NID,
        BIO_OP_INTERCEPTOR_READ, request, &resp, rspLen);
    if (UNLIKELY(ret != 0 || resp == nullptr)) {
        CLOG_ERROR("Send read failed inode:" << request.inode << ", offset:" << request.offset << ", length:" <<
            request.nbytes << ".");
        return -1;
    }

    const uint64_t headerSize = sizeof(InterceptorPreadOut);
    if (rspLen < headerSize || rspLen - headerSize < resp->dataLen) {
        CLOG_ERROR("rspLen: " << rspLen << " less than the InterceptorPreadOut: " << sizeof(InterceptorPreadOut) <<
            " and dataLen: " << resp->dataLen << ".");
        free(resp);
        resp = nullptr;
        return -1;
    }
    CLOG_DEBUG("Read inode:" << request.inode << ", offset:" << request.offset << ", length:" << request.nbytes <<
        "rsp len:" << resp->dataLen << ".");

    ret = memcpy_s(buf, count, resp->data, resp->dataLen);
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("Memory copy read data:" << resp->dataLen << ", buff size:" << count << " failed:" << ret);
        free(resp);
        resp = nullptr;
        return -1;
    }

    auto retLen = static_cast<ssize_t>(resp->dataLen);
    free(resp);
    resp = nullptr;
    return retLen;
}

ssize_t ProxyOperations::PreadSmallInner(int fd, BufVec &bufVec, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    InterceptorPreadIn request;
    request.pid = static_cast<uint32_t>(getpid());
    request.fd = fd;
    request.inode = file->GetInode();
    request.offset = offset;
    request.nbytes = bufVec.size;
    request.startTime = Monotonic::TimeNs();

    InterceptorPreadOut *resp = nullptr;
    uint64_t rspLen = 0;
    auto ret = InterceptorClientNetService::Instance().SendSync<InterceptorPreadIn, InterceptorPreadOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_READ, request, &resp, rspLen);
    if (UNLIKELY(ret != 0 || resp == nullptr)) {
        CLOG_ERROR("Send read failed inode:" << request.inode << ", offset:" << request.offset << ", length:" <<
            request.nbytes << ".");
        return -1;
    }

    const uint64_t headerSize = sizeof(InterceptorPreadOut);
    if (rspLen < headerSize || rspLen - headerSize < resp->dataLen) {
        CLOG_ERROR("rspLen: " << rspLen << " less than the InterceptorPreadOut: " << sizeof(InterceptorPreadOut) <<
            " and dataLen: " << resp->dataLen << ".");
        free(resp);
        resp = nullptr;
        return -1;
    }

    if (resp->dataLen == 0) {
        free(resp);
        resp = nullptr;
        return 0;
    }

    ret = static_cast<int32_t>(bufVec.Write(reinterpret_cast<uint8_t *>(resp->data), resp->dataLen));
    if (UNLIKELY(ret < 0)) {
        free(resp);
        resp = nullptr;
        return -1;
    }

    auto retLen = static_cast<ssize_t>(resp->dataLen);
    free(resp);
    resp = nullptr;
    return retLen;
}

ssize_t ProxyOperations::PreadLargeInner(int fd, void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    uintptr_t shmAddr = 0;
    uint64_t mrOffset = 0;
    auto ret = InterceptorClientNetService::Instance().AllocShmBlock(shmAddr, mrOffset);
    if (UNLIKELY(ret != BIO_OK || shmAddr == 0)) {
        CLOG_ERROR("PreadLargeInner: alloc shm block failed, ret:" << ret << ".");
        return -1;
    }

    InterceptorLargePreadIn request;
    request.pid = static_cast<uint32_t>(getpid());
    request.fd = fd;
    request.inode = file->GetInode();
    request.offset = offset;
    request.nbytes = count;
    request.mrOffset = mrOffset;
    request.startTime = Monotonic::TimeNs();

    InterceptorLargePreadOut resp;
    ret = InterceptorClientNetService::Instance().SendSync<InterceptorLargePreadIn, InterceptorLargePreadOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_LARGE_READ, request, resp);
    if (UNLIKELY(ret != 0 || resp.ret != 0)) {
        CLOG_ERROR("PreadLargeInner: large read failed, ret:" << ret << ", resp.ret:" << resp.ret << ".");
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
        return -1;
    }

    if (resp.dataLen == 0) {
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
        return 0;
    }

    size_t copyLen = std::min(count, static_cast<size_t>(resp.dataLen));
    ret = memcpy_s(buf, count, reinterpret_cast<uint8_t *>(shmAddr), copyLen);
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("PreadLargeInner: memcpy_s failed, ret:" << ret << ".");
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
        return -1;
    }

    InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
    CLOG_DEBUG("PreadLargeInner: success, fd:" << fd << ", offset:" << offset << ", count:" << count <<
        ", dataLen:" << resp.dataLen << ".");
    return static_cast<ssize_t>(resp.dataLen);
}

ssize_t ProxyOperations::PreadLargeInner(int fd, BufVec &bufVec, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    uintptr_t shmAddr = 0;
    uint64_t mrOffset = 0;
    auto ret = InterceptorClientNetService::Instance().AllocShmBlock(shmAddr, mrOffset);
    if (UNLIKELY(ret != BIO_OK || shmAddr == 0)) {
        CLOG_ERROR("PreadLargeInner: alloc shm block failed, ret:" << ret << ".");
        return -1;
    }

    InterceptorLargePreadIn request;
    request.pid = static_cast<uint32_t>(getpid());
    request.fd = fd;
    request.inode = file->GetInode();
    request.offset = offset;
    request.nbytes = bufVec.size;
    request.mrOffset = mrOffset;
    request.startTime = Monotonic::TimeNs();

    InterceptorLargePreadOut resp;
    ret = InterceptorClientNetService::Instance().SendSync<InterceptorLargePreadIn, InterceptorLargePreadOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_LARGE_READ, request, resp);
    if (UNLIKELY(ret != 0 || resp.ret != 0)) {
        CLOG_ERROR("PreadLargeInner: large read failed, ret:" << ret << ", resp.ret:" << resp.ret << ".");
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
        return -1;
    }

    if (resp.dataLen == 0) {
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
        return 0;
    }

    ret = static_cast<int32_t>(bufVec.Write(reinterpret_cast<uint8_t *>(shmAddr), resp.dataLen));
    InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
    if (UNLIKELY(ret < 0)) {
        return -1;
    }

    CLOG_DEBUG("PreadLargeInner(vec): success, fd:" << fd << ", offset:" << offset << ", count:" << bufVec.size <<
        ", dataLen:" << resp.dataLen << ".");
    return static_cast<ssize_t>(resp.dataLen);
}

ssize_t ProxyOperations::Pread(int fd, void *buf, size_t count, off_t offset)
{
    CLOG_DEBUG("Pread fd:" << fd << ", offset:" << offset << ", length:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->pread(fd, buf, count, offset);
    }

    return PreadInner(fd, buf, count, offset);
}

ssize_t ProxyOperations::Pread64(int fd, void *buf, size_t count, off64_t offset)
{
    CLOG_DEBUG("Pread64 fd:" << fd << ", offset:" << offset << ", length:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->pread64(fd, buf, count, offset);
    }

    return PreadInner(fd, buf, count, offset);
}

ssize_t ProxyOperations::Read(int fd, void *buf, size_t nbytes)
{
    CLOG_DEBUG("Read fd:" << fd << ", length:" << nbytes << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->read(fd, buf, nbytes);
    }

    off_t offset = CONTEXT.GetOperations()->lseek(fd, 0, SEEK_CUR);
    if (UNLIKELY(offset == -1)) {
        errno = EIO;
        return -1;
    }

    auto ret = PreadInner(fd, buf, nbytes, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }

    offset = CONTEXT.GetOperations()->lseek(fd, offset + ret, SEEK_SET);
    return ret;
}

ssize_t ProxyOperations::Readv(int fd, const struct iovec *vector, int count)
{
    CLOG_DEBUG("Readv fd:" << fd << ", length:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->readv(fd, vector, count);
    }

    off_t offset = CONTEXT.GetOperations()->lseek(fd, 0, SEEK_CUR);
    if (UNLIKELY(offset == -1)) {
        CLOG_ERROR("Seek:" << fd << " failed.");
        errno = EIO;
        return -1;
    }

    BufVec bufVec(vector, count);
    if (UNLIKELY(bufVec.size == 0)) {
        return -1;
    }

    if (bufVec.size > MAX_LARGE_WRITE_SIZE) {
        return CONTEXT.GetOperations()->readv(fd, vector, count);
    }

    auto ret = PreadInner(fd, bufVec, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }

    offset = CONTEXT.GetOperations()->lseek(fd, offset + ret, SEEK_SET);
    return ret;
}

ssize_t ProxyOperations::preadv64(int fd, const struct iovec *vector, int iovcnt, off64_t offset)
{
    CLOG_DEBUG("preadv64 fd:" << fd << ", offset:" << offset << ", io count" << iovcnt << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->preadv64(fd, vector, iovcnt, offset);
    }

    BufVec bufVec(vector, iovcnt);
    if (UNLIKELY(bufVec.size == 0)) {
        return -1;
    }

    if (bufVec.size > MAX_LARGE_WRITE_SIZE) {
        return CONTEXT.GetOperations()->preadv64(fd, vector, iovcnt, offset);
    }

    auto ret = PreadInner(fd, bufVec, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }

    return ret;
}

ssize_t ProxyOperations::PwriteInner(int fd, const void *buf, size_t count, off_t offset)
{
    if (count <= MAX_SMALL_WRITE_SIZE) {
        return PwriteSmallInner(fd, buf, count, offset);
    }
    if (count <= MAX_LARGE_WRITE_SIZE) {
        return PwriteLargeInner(fd, buf, count, offset);
    }
    return CONTEXT.GetOperations()->pwrite64(fd, buf, count, offset);
}

ssize_t ProxyOperations::PwriteSmallInner(int fd, const void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    size_t reqLen = sizeof(InterceptorPwriteIn) + count;
    char *tmpPtr = GetSmallWriteScratch(reqLen);

    InterceptorPwriteIn *request = static_cast<InterceptorPwriteIn *>(static_cast<void *>(tmpPtr));
    request->pid = static_cast<uint32_t>(getpid());
    request->fd = fd;
    request->inode = file->GetInode();
    request->offset = offset;
    request->nbytes = count;
    request->startTime = Monotonic::TimeNs();
    auto ret = memcpy_s(request->data, count, (const char *)buf, count);
    if (UNLIKELY(ret != 0)) {
        return -1;
    }

    InterceptorPwriteOut resp;
    ret = InterceptorClientNetService::Instance().SendSyncBuff<InterceptorPwriteOut>(INVALID_NID,
        BIO_OP_INTERCEPTOR_WRITE, request, reqLen, resp);
    if (UNLIKELY(ret != 0 || resp.ret != 0)) {
        CLOG_ERROR("Send Sync Buff Write ret: " << ret << ", resp.ret:" << resp.ret << ", fd:" << fd <<
            ", offset:" << offset << ", count:" << count << ".");
        return -1;
    }

    CLOG_DEBUG("Write fd:" << fd << ", offset:" << offset << ", count:" << count << ".");
    return static_cast<ssize_t>(count);
}

ssize_t ProxyOperations::PwriteLargeInner(int fd, const void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    uintptr_t shmAddr = 0;
    uint64_t mrOffset = 0;
    bool fromCache = false;
    if (UNLIKELY(!AcquireLargeWriteBlock(shmAddr, mrOffset, fromCache))) {
        return -1;
    }

    auto ret = memcpy_s(reinterpret_cast<uint8_t *>(shmAddr), MAX_LARGE_WRITE_SIZE, buf, count);
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("PwriteLargeInner: memcpy_s failed, ret:" << ret << ".");
        ReleaseLargeWriteBlock(shmAddr, mrOffset, fromCache);
        return -1;
    }

    InterceptorLargePwriteIn writeReq;
    writeReq.pid = static_cast<uint32_t>(getpid());
    writeReq.fd = fd;
    writeReq.inode = file->GetInode();
    writeReq.offset = offset;
    writeReq.nbytes = count;
    writeReq.mrOffset = mrOffset;
    writeReq.startTime = Monotonic::TimeNs();

    InterceptorPwriteOut writeResp;
    ret = InterceptorClientNetService::Instance().SendSync<InterceptorLargePwriteIn, InterceptorPwriteOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_LARGE_WRITE, writeReq, writeResp);
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("Send sync large write failed, ret:" << ret << ".");
        ReleaseLargeWriteBlock(shmAddr, mrOffset, fromCache);
        return -1;
    }
    
    if (UNLIKELY(writeResp.ret != 0)) {
        CLOG_ERROR("large write failed, respRet:" << writeResp.ret << ".");
        ReleaseLargeWriteBlock(shmAddr, mrOffset, fromCache);
        return -1;
    }

    CacheLargeWriteBlock(shmAddr, mrOffset);
    CLOG_DEBUG("PwriteLargeInner: success, fd:" << fd << ", offset:" << offset << ", count:" << count << ".");

    return static_cast<ssize_t>(count);
}

ssize_t ProxyOperations::Write(int fd, const void *buf, size_t nbytes)
{
    CLOG_DEBUG("Write fd:" << fd << ", count" << nbytes << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->write(fd, buf, nbytes);
    }

    off_t offset = CONTEXT.GetOperations()->lseek(fd, 0, SEEK_CUR);
    if (UNLIKELY(offset == -1)) {
        errno = EIO;
        return -1;
    }

    auto ret = PwriteInner(fd, buf, nbytes, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }

    offset = CONTEXT.GetOperations()->lseek(fd, offset + ret, SEEK_SET);
    return ret;
}
