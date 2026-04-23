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
        CLOG_ERROR("Alloc large write shm block failed, ret:" << ret << ", shmAddr:" << shmAddr <<
            ", mrOffset:" << mrOffset << ".");
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
        CLOG_ERROR("Get open file context failed, fd:" << fd << ".");
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
        CLOG_ERROR("Send small read request failed, ret:" << ret << ", fd:" << fd << ", inode:" << request.inode <<
            ", offset:" << request.offset << ", nbytes:" << request.nbytes << ", resp:" << resp << ".");
        return -1;
    }

    const uint64_t headerSize = sizeof(InterceptorPreadOut);
    if (rspLen < headerSize || rspLen - headerSize < resp->dataLen) {
        CLOG_ERROR("Invalid small read response, fd:" << fd << ", inode:" << request.inode << ", offset:" <<
            request.offset << ", rspLen:" << rspLen << ", headerSize:" << sizeof(InterceptorPreadOut) <<
            ", dataLen:" << resp->dataLen << ".");
        free(resp);
        resp = nullptr;
        return -1;
    }

    ret = memcpy_s(buf, count, resp->data, resp->dataLen);
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("Copy small read response failed, ret:" << ret << ", fd:" << fd << ", inode:" << request.inode <<
            ", offset:" << request.offset << ", dataLen:" << resp->dataLen << ", bufSize:" << count << ".");
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
        CLOG_ERROR("Send small read vec request failed, ret:" << ret << ", fd:" << fd << ", inode:" <<
            request.inode << ", offset:" << request.offset << ", nbytes:" << request.nbytes << ", resp:" <<
            resp << ".");
        return -1;
    }

    const uint64_t headerSize = sizeof(InterceptorPreadOut);
    if (rspLen < headerSize || rspLen - headerSize < resp->dataLen) {
        CLOG_ERROR("Invalid small read vec response, fd:" << fd << ", inode:" << request.inode << ", offset:" <<
            request.offset << ", rspLen:" << rspLen << ", headerSize:" << sizeof(InterceptorPreadOut) <<
            ", dataLen:" << resp->dataLen << ".");
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
        CLOG_ERROR("Alloc large read shm block failed, ret:" << ret << ", fd:" << fd << ", offset:" << offset <<
            ", nbytes:" << count << ".");
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
        CLOG_ERROR("Large read failed, sendRet:" << ret << ", respRet:" << resp.ret << ", fd:" << fd <<
            ", inode:" << request.inode << ", offset:" << request.offset << ", nbytes:" << request.nbytes <<
            ", mrOffset:" << request.mrOffset << ".");
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
        CLOG_ERROR("Copy large read response failed, ret:" << ret << ", fd:" << fd << ", offset:" << offset <<
            ", copyLen:" << copyLen << ", bufSize:" << count << ", dataLen:" << resp.dataLen << ".");
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
        return -1;
    }

    InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
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
        CLOG_ERROR("Alloc large read vec shm block failed, ret:" << ret << ", fd:" << fd << ", offset:" << offset <<
            ", nbytes:" << bufVec.size << ".");
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
        CLOG_ERROR("Large read vec failed, sendRet:" << ret << ", respRet:" << resp.ret << ", fd:" << fd <<
            ", inode:" << request.inode << ", offset:" << request.offset << ", nbytes:" << request.nbytes <<
            ", mrOffset:" << request.mrOffset << ".");
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
        CLOG_ERROR("Get current offset for readv failed, fd:" << fd << ", errno:" << errno << ".");
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

ssize_t ProxyOperations::Preadv64(int fd, const struct iovec *vector, int iovcnt, off64_t offset)
{
    CLOG_DEBUG("Preadv64 fd:" << fd << ", offset:" << offset << ", io count:" << iovcnt << ".");
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

ssize_t ProxyOperations::PwriteInner(int fd, BufVec &bufVec, off_t offset)
{
    if (bufVec.size <= MAX_SMALL_WRITE_SIZE) {
        return PwriteSmallInner(fd, bufVec, offset);
    }
    if (bufVec.size <= MAX_LARGE_WRITE_SIZE) {
        return PwriteLargeInner(fd, bufVec, offset);
    }
    return CONTEXT.GetOperations()->pwritev64(fd, bufVec.iov, bufVec.count, offset);
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
        CLOG_ERROR("Small write failed, sendRet:" << ret << ", respRet:" << resp.ret << ", fd:" << fd <<
            ", inode:" << request->inode << ", offset:" << offset << ", nbytes:" << count << ".");
        return -1;
    }

    return static_cast<ssize_t>(count);
}

ssize_t ProxyOperations::PwriteSmallInner(int fd, BufVec &bufVec, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    size_t reqLen = sizeof(InterceptorPwriteIn) + bufVec.size;
    char *tmpPtr = GetSmallWriteScratch(reqLen);

    InterceptorPwriteIn *request = static_cast<InterceptorPwriteIn *>(static_cast<void *>(tmpPtr));
    request->pid = static_cast<uint32_t>(getpid());
    request->fd = fd;
    request->inode = file->GetInode();
    request->offset = offset;
    request->nbytes = bufVec.size;
    request->startTime = Monotonic::TimeNs();
    auto ret = bufVec.Read(reinterpret_cast<uint8_t *>(request->data), bufVec.size);
    if (UNLIKELY(ret < 0 || static_cast<size_t>(ret) != bufVec.size)) {
        return -1;
    }

    InterceptorPwriteOut resp;
    ret = InterceptorClientNetService::Instance().SendSyncBuff<InterceptorPwriteOut>(INVALID_NID,
        BIO_OP_INTERCEPTOR_WRITE, request, reqLen, resp);
    if (UNLIKELY(ret != 0 || resp.ret != 0)) {
        CLOG_ERROR("Small write vec failed, sendRet:" << ret << ", respRet:" << resp.ret << ", fd:" << fd <<
            ", inode:" << request->inode << ", offset:" << offset << ", nbytes:" << bufVec.size << ".");
        return -1;
    }

    return static_cast<ssize_t>(bufVec.size);
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
        CLOG_ERROR("Copy user buffer to large write shm failed, ret:" << ret << ", fd:" << fd << ", offset:" <<
            offset << ", nbytes:" << count << ", mrOffset:" << mrOffset << ".");
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
        CLOG_ERROR("Send large write request failed, ret:" << ret << ", fd:" << fd << ", inode:" <<
            writeReq.inode << ", offset:" << writeReq.offset << ", nbytes:" << writeReq.nbytes <<
            ", mrOffset:" << writeReq.mrOffset << ".");
        ReleaseLargeWriteBlock(shmAddr, mrOffset, fromCache);
        return -1;
    }
    
    if (UNLIKELY(writeResp.ret != 0)) {
        CLOG_ERROR("Large write failed, respRet:" << writeResp.ret << ", fd:" << fd << ", inode:" <<
            writeReq.inode << ", offset:" << writeReq.offset << ", nbytes:" << writeReq.nbytes <<
            ", mrOffset:" << writeReq.mrOffset << ".");
        ReleaseLargeWriteBlock(shmAddr, mrOffset, fromCache);
        return -1;
    }

    CacheLargeWriteBlock(shmAddr, mrOffset);
    return static_cast<ssize_t>(count);
}

ssize_t ProxyOperations::PwriteLargeInner(int fd, BufVec &bufVec, off_t offset)
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

    auto ret = bufVec.Read(reinterpret_cast<uint8_t *>(shmAddr), bufVec.size);
    if (UNLIKELY(ret < 0 || static_cast<size_t>(ret) != bufVec.size)) {
        ReleaseLargeWriteBlock(shmAddr, mrOffset, fromCache);
        return -1;
    }

    InterceptorLargePwriteIn writeReq;
    writeReq.pid = static_cast<uint32_t>(getpid());
    writeReq.fd = fd;
    writeReq.inode = file->GetInode();
    writeReq.offset = offset;
    writeReq.nbytes = bufVec.size;
    writeReq.mrOffset = mrOffset;
    writeReq.startTime = Monotonic::TimeNs();

    InterceptorPwriteOut writeResp;
    ret = InterceptorClientNetService::Instance().SendSync<InterceptorLargePwriteIn, InterceptorPwriteOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_LARGE_WRITE, writeReq, writeResp);
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("Send large write vec request failed, ret:" << ret << ", fd:" << fd << ", inode:" <<
            writeReq.inode << ", offset:" << writeReq.offset << ", nbytes:" << writeReq.nbytes <<
            ", mrOffset:" << writeReq.mrOffset << ".");
        ReleaseLargeWriteBlock(shmAddr, mrOffset, fromCache);
        return -1;
    }

    if (UNLIKELY(writeResp.ret != 0)) {
        CLOG_ERROR("Large write vec failed, respRet:" << writeResp.ret << ", fd:" << fd << ", inode:" <<
            writeReq.inode << ", offset:" << writeReq.offset << ", nbytes:" << writeReq.nbytes <<
            ", mrOffset:" << writeReq.mrOffset << ".");
        ReleaseLargeWriteBlock(shmAddr, mrOffset, fromCache);
        return -1;
    }

    CacheLargeWriteBlock(shmAddr, mrOffset);
    return static_cast<ssize_t>(bufVec.size);
}

ssize_t ProxyOperations::Write(int fd, const void *buf, size_t nbytes)
{
    CLOG_DEBUG("Write fd:" << fd << ", count:" << nbytes << ".");
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

ssize_t ProxyOperations::Pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    CLOG_DEBUG("Pwrite fd:" << fd << ", offset:" << offset << ", length:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->pwrite(fd, buf, count, offset);
    }

    auto ret = PwriteInner(fd, buf, count, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }
    return ret;
}

ssize_t ProxyOperations::Pwrite64(int fd, const void *buf, size_t count, off64_t offset)
{
    CLOG_DEBUG("Pwrite64 fd:" << fd << ", offset:" << offset << ", length:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->pwrite64(fd, buf, count, offset);
    }

    auto ret = PwriteInner(fd, buf, count, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }
    return ret;
}

ssize_t ProxyOperations::Writev(int fd, const struct iovec *vector, int count)
{
    CLOG_DEBUG("Writev fd:" << fd << ", count:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->writev(fd, vector, count);
    }

    off_t offset = CONTEXT.GetOperations()->lseek(fd, 0, SEEK_CUR);
    if (UNLIKELY(offset == -1)) {
        errno = EIO;
        return -1;
    }

    BufVec bufVec(vector, count);
    if (UNLIKELY(bufVec.size == 0)) {
        return -1;
    }

    auto ret = PwriteInner(fd, bufVec, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }

    offset = CONTEXT.GetOperations()->lseek(fd, offset + ret, SEEK_SET);
    return ret;
}

ssize_t ProxyOperations::Pwritev(int fd, const struct iovec *vector, int count, off_t offset)
{
    CLOG_DEBUG("Pwritev fd:" << fd << ", offset:" << offset << ", count:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->pwritev(fd, vector, count, offset);
    }

    BufVec bufVec(vector, count);
    if (UNLIKELY(bufVec.size == 0)) {
        return -1;
    }

    auto ret = PwriteInner(fd, bufVec, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }
    return ret;
}

ssize_t ProxyOperations::Pwritev64(int fd, const struct iovec *vector, int count, off64_t offset)
{
    CLOG_DEBUG("Pwritev64 fd:" << fd << ", offset:" << offset << ", count:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->pwritev64(fd, vector, count, offset);
    }

    BufVec bufVec(vector, count);
    if (UNLIKELY(bufVec.size == 0)) {
        return -1;
    }

    auto ret = PwriteInner(fd, bufVec, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }
    return ret;
}
