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
#include "securec.h"
#include "bio_def.h"
#include "message_op.h"
#include "message.h"
#include "interceptor_log.h"
#include "interceptor_net.h"
#include "proxy_operations.h"
#include "interceptor_context.h"
#include "bio_monotonic.h"
#include "bufvec.h"

using namespace ock::bio;

#define CONTEXT BioInterceptorContext::GetInstance()

static bool CheckCopyFreeSpace(const CacheSpaceDesc &spaceInfo, uint64_t shmLength, size_t expectedLen)
{
    if (spaceInfo.addressNum == 0 || spaceInfo.addressNum > CACHE_SPACE_ADDRESS_SIZE) {
        return false;
    }

    uint64_t totalLen = 0;
    for (uint32_t i = 0; i < spaceInfo.addressNum; i++) {
        uint64_t offset = spaceInfo.address[i].address;
        uint64_t size = spaceInfo.address[i].size;
        if (size == 0 || offset >= shmLength || size > shmLength - offset) {
            return false;
        }
        totalLen += size;
    }

    return totalLen == expectedLen;
}

static bool PrepareLargeWriteSpace(size_t count, uint8_t *&bioShmBase, CacheSpaceDesc &spaceInfo)
{
    bioShmBase = InterceptorClientNetService::Instance().GetBioShmAddr();
    if (UNLIKELY(bioShmBase == nullptr)) {
        CLOG_ERROR("PrepareLargeWriteSpace: bio shm not ready.");
        return false;
    }

    InterceptorAllocCacheSpaceReq allocReq;
    allocReq.pid = static_cast<uint32_t>(getpid());
    allocReq.nbytes = count;

    InterceptorAllocCacheSpaceResp allocResp;
    auto ret = InterceptorClientNetService::Instance().SendSync<InterceptorAllocCacheSpaceReq,
        InterceptorAllocCacheSpaceResp>(INVALID_NID, BIO_OP_INTERCEPTOR_ALLOC_CACHE_SPACE, allocReq, allocResp);
    if (UNLIKELY(ret != BIO_OK || allocResp.ret != 0)) {
        CLOG_ERROR("PrepareLargeWriteSpace: alloc cache space failed, ret:" << ret <<
            ", resp.ret:" << allocResp.ret << ".");
        return false;
    }

    spaceInfo = allocResp.spaceInfo;
    uint64_t bioShmLength = InterceptorClientNetService::Instance().GetBioShmLength();
    if (UNLIKELY(!CheckCopyFreeSpace(spaceInfo, bioShmLength, count))) {
        CLOG_ERROR("PrepareLargeWriteSpace: invalid cache space, count:" << count << ", shmLength:" << bioShmLength <<
            ", addressNum:" << spaceInfo.addressNum << ".");
        return false;
    }

    return true;
}

static bool CopyBufferToCopyFreeSpace(const uint8_t *src, const CacheSpaceDesc &spaceInfo, uint8_t *bioShmBase)
{
    for (uint32_t i = 0; i < spaceInfo.addressNum; i++) {
        uint8_t *dstAddr = bioShmBase + spaceInfo.address[i].address;
        uint32_t copyLen = spaceInfo.address[i].size;
        memcpy(dstAddr, src, copyLen);
        src += copyLen;
    }
    return true;
}

static bool CopyBufVecToCopyFreeSpace(BufVec &bufVec, const CacheSpaceDesc &spaceInfo, uint8_t *bioShmBase)
{
    for (uint32_t i = 0; i < spaceInfo.addressNum; i++) {
        uint8_t *dstAddr = bioShmBase + spaceInfo.address[i].address;
        uint32_t copyLen = spaceInfo.address[i].size;
        auto copied = bufVec.Read(dstAddr, copyLen);
        if (UNLIKELY(copied < 0 || static_cast<uint32_t>(copied) != copyLen)) {
            CLOG_ERROR("CopyBufVecToCopyFreeSpace failed, copied:" << copied << ", expect:" << copyLen << ".");
            return false;
        }
    }
    return true;
}

static ssize_t SubmitLargeWriteRequest(int fd, uint64_t inode, size_t count, off_t offset, const CacheSpaceDesc &spaceInfo)
{
    InterceptorLargePwriteIn writeReq;
    writeReq.pid = static_cast<uint32_t>(getpid());
    writeReq.fd = fd;
    writeReq.inode = inode;
    writeReq.offset = offset;
    writeReq.nbytes = count;
    writeReq.mrOffset = 0;
    writeReq.startTime = Monotonic::TimeNs();
    writeReq.spaceInfo = spaceInfo;

    InterceptorPwriteOut writeResp {};
    auto ret = InterceptorClientNetService::Instance().SendSync<InterceptorLargePwriteIn, InterceptorPwriteOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_LARGE_WRITE, writeReq, writeResp);
    if (UNLIKELY(ret != 0 || writeResp.ret != 0)) {
        CLOG_ERROR("SubmitLargeWriteRequest failed, ret:" << ret << ", resp.ret:" << writeResp.ret << ".");
        return -1;
    }

    return static_cast<ssize_t>(count);
}

static size_t GetLargeReadChunkSize()
{
    size_t shmBlockSize = static_cast<size_t>(InterceptorClientNetService::Instance().GetDataMsgMemBlockSize());
    if (shmBlockSize == 0) {
        return 0;
    }
    return std::min(shmBlockSize, MAX_LARGE_WRITE_SIZE);
}

static ssize_t PreadShmToBuffer(int fd, uint64_t inode, uint8_t *buf, size_t count, off_t offset)
{
    size_t readed = 0;
    size_t shmBlockSize = GetLargeReadChunkSize();
    if (UNLIKELY(shmBlockSize == 0)) {
        CLOG_ERROR("PreadShmToBuffer: invalid shm block size.");
        return -1;
    }
    while (readed < count) {
        size_t copyLen = std::min(count - readed, shmBlockSize);
        uintptr_t shmAddr = 0;
        uint64_t mrOffset = 0;
        auto ret = InterceptorClientNetService::Instance().AllocShmBlock(shmAddr, mrOffset);
        if (UNLIKELY(ret != BIO_OK || shmAddr == 0)) {
            CLOG_ERROR("PreadShmToBuffer: alloc shm block failed, ret:" << ret << ".");
            return readed > 0 ? static_cast<ssize_t>(readed) : -1;
        }

        InterceptorLargePreadIn request;
        request.pid = static_cast<uint32_t>(getpid());
        request.fd = fd;
        request.inode = inode;
        request.offset = offset + static_cast<off_t>(readed);
        request.nbytes = copyLen;
        request.mrOffset = mrOffset;
        request.startTime = Monotonic::TimeNs();

        InterceptorLargePreadOut resp;
        ret = InterceptorClientNetService::Instance().SendSync<InterceptorLargePreadIn, InterceptorLargePreadOut>(
            INVALID_NID, BIO_OP_INTERCEPTOR_LARGE_READ, request, resp);
        if (UNLIKELY(ret != 0 || resp.ret != 0)) {
            CLOG_ERROR("PreadShmToBuffer: large read failed, ret:" << ret << ", resp.ret:" << resp.ret << ".");
            InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
            return readed > 0 ? static_cast<ssize_t>(readed) : -1;
        }
        if (UNLIKELY(resp.dataLen > copyLen)) {
            CLOG_ERROR("PreadShmToBuffer: invalid dataLen:" << resp.dataLen << ", expect <= " << copyLen << ".");
            InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
            return readed > 0 ? static_cast<ssize_t>(readed) : -1;
        }

        memcpy(buf + readed, reinterpret_cast<uint8_t *>(shmAddr), static_cast<size_t>(resp.dataLen));
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);

        readed += static_cast<size_t>(resp.dataLen);
        if (resp.dataLen < copyLen) {
            break;
        }
    }

    return static_cast<ssize_t>(readed);
}

static ssize_t PreadShmToBufVec(int fd, uint64_t inode, BufVec &bufVec, off_t offset)
{
    size_t readed = 0;
    size_t shmBlockSize = GetLargeReadChunkSize();
    if (UNLIKELY(shmBlockSize == 0)) {
        CLOG_ERROR("PreadShmToBufVec: invalid shm block size.");
        return -1;
    }
    while (readed < bufVec.size) {
        size_t copyLen = std::min(bufVec.size - readed, shmBlockSize);
        uintptr_t shmAddr = 0;
        uint64_t mrOffset = 0;
        auto ret = InterceptorClientNetService::Instance().AllocShmBlock(shmAddr, mrOffset);
        if (UNLIKELY(ret != BIO_OK || shmAddr == 0)) {
            CLOG_ERROR("PreadShmToBufVec: alloc shm block failed, ret:" << ret << ".");
            return readed > 0 ? static_cast<ssize_t>(readed) : -1;
        }

        InterceptorLargePreadIn request;
        request.pid = static_cast<uint32_t>(getpid());
        request.fd = fd;
        request.inode = inode;
        request.offset = offset + static_cast<off_t>(readed);
        request.nbytes = copyLen;
        request.mrOffset = mrOffset;
        request.startTime = Monotonic::TimeNs();

        InterceptorLargePreadOut resp;
        ret = InterceptorClientNetService::Instance().SendSync<InterceptorLargePreadIn, InterceptorLargePreadOut>(
            INVALID_NID, BIO_OP_INTERCEPTOR_LARGE_READ, request, resp);
        if (UNLIKELY(ret != 0 || resp.ret != 0)) {
            CLOG_ERROR("PreadShmToBufVec: large read failed, ret:" << ret << ", resp.ret:" << resp.ret << ".");
            InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
            return readed > 0 ? static_cast<ssize_t>(readed) : -1;
        }
        if (UNLIKELY(resp.dataLen > copyLen)) {
            CLOG_ERROR("PreadShmToBufVec: invalid dataLen:" << resp.dataLen << ", expect <= " << copyLen << ".");
            InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
            return readed > 0 ? static_cast<ssize_t>(readed) : -1;
        }

        auto wrote = bufVec.Write(reinterpret_cast<uint8_t *>(shmAddr), static_cast<size_t>(resp.dataLen));
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
        if (UNLIKELY(wrote < 0 || static_cast<uint64_t>(wrote) != resp.dataLen)) {
            CLOG_ERROR("PreadShmToBufVec: bufvec write failed, wrote:" << wrote << ", dataLen:" << resp.dataLen << ".");
            return readed > 0 ? static_cast<ssize_t>(readed) : -1;
        }

        readed += static_cast<size_t>(resp.dataLen);
        if (resp.dataLen < copyLen) {
            break;
        }
    }

    return static_cast<ssize_t>(readed);
}

static ssize_t PwriteLargeVecInner(int fd, uint64_t inode, BufVec &bufVec, off_t offset)
{
    size_t written = 0;
    while (written < bufVec.size) {
        size_t copyLen = std::min(bufVec.size - written, MAX_LARGE_WRITE_SIZE);
        uint8_t *bioShmBase = nullptr;
        CacheSpaceDesc spaceInfo {};
        if (UNLIKELY(!PrepareLargeWriteSpace(copyLen, bioShmBase, spaceInfo))) {
            return written > 0 ? static_cast<ssize_t>(written) : -1;
        }

        if (UNLIKELY(!CopyBufVecToCopyFreeSpace(bufVec, spaceInfo, bioShmBase))) {
            return written > 0 ? static_cast<ssize_t>(written) : -1;
        }

        auto ret = SubmitLargeWriteRequest(fd, inode, copyLen, offset + static_cast<off_t>(written), spaceInfo);
        if (UNLIKELY(ret < 0)) {
            return written > 0 ? static_cast<ssize_t>(written) : -1;
        }
        written += copyLen;
    }

    return static_cast<ssize_t>(written);
}

ssize_t ProxyOperations::PreadInner(int fd, void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        CLOG_ERROR("Get fd:" << fd << "failed.");
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    return PreadShmToBuffer(fd, file->GetInode(), static_cast<uint8_t *>(buf), count, offset);
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
    if (resp->dataLen == 0 || rspLen < headerSize || rspLen - headerSize < resp->dataLen) {
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

ssize_t ProxyOperations::PreadLargeInner(int fd, void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    if (count > MAX_LARGE_WRITE_SIZE) {
        return CONTEXT.GetOperations()->pread(fd, buf, count, offset);
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
    if (UNLIKELY(ret != 0 || resp.dataLen <= 0)) {
        CLOG_ERROR("PreadLargeInner: large read failed, ret:" << ret << ", dataLen:" << resp.dataLen << ".");
        InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
        return -1;
    }

    size_t copyLen = std::min(count, static_cast<size_t>(resp.dataLen));
    memcpy(buf, reinterpret_cast<uint8_t *>(shmAddr), copyLen);
    InterceptorClientNetService::Instance().ReleaseShmBlock(mrOffset);
    CLOG_DEBUG("PreadLargeInner: success, fd:" << fd << ", offset:" << offset << ", count:" << count <<
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
        return count == 0 ? 0 : -1;
    }
    auto ret = PreadShmToBufVec(fd, file->GetInode(), bufVec, offset);
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
        return iovcnt == 0 ? 0 : -1;
    }
    auto ret = PreadShmToBufVec(fd, file->GetInode(), bufVec, static_cast<off_t>(offset));
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        return -1;
    }
    return ret;
}

ssize_t ProxyOperations::PwriteInner(int fd, const void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        CLOG_ERROR("Get fd:" << fd << "failed.");
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    return PwriteLargeInner(fd, buf, count, offset);
}

ssize_t ProxyOperations::PwriteSmallInner(int fd, const void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    size_t reqLen = sizeof(InterceptorPwriteIn) + count;
    auto *tmpPtr = static_cast<uint8_t *>(malloc(reqLen));
    if (UNLIKELY(tmpPtr == nullptr)) {
        CLOG_ERROR("Memory allocation failed.");
        errno = ENOMEM;
        return -1;
    }

    InterceptorPwriteIn *request = static_cast<InterceptorPwriteIn *>(static_cast<void *>(tmpPtr));
    request->pid = static_cast<uint32_t>(getpid());
    request->inode = file->GetInode();
    request->offset = offset;
    request->nbytes = count;
    request->startTime = Monotonic::TimeNs();

    auto mcRet = memcpy_s(request->data, count, buf, count);
    if (UNLIKELY(mcRet != 0)) {
        CLOG_ERROR("Memory copy write data:" << count << " failed:" << mcRet);
        free(tmpPtr);
        return -1;
    }

    InterceptorPwriteOut resp;
    auto ret = InterceptorClientNetService::Instance().SendSyncBuff<InterceptorPwriteOut>(
        INVALID_NID, BIO_OP_INTERCEPTOR_WRITE, request, static_cast<uint32_t>(reqLen), resp);
    free(tmpPtr);
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

    if (count > MAX_LARGE_WRITE_SIZE) {
        return CONTEXT.GetOperations()->pwrite(fd, buf, count, offset);
    }

    auto t0 = Monotonic::TimeNs();

    uint8_t *bioShmBase = nullptr;
    CacheSpaceDesc spaceInfo {};
    if (UNLIKELY(!PrepareLargeWriteSpace(count, bioShmBase, spaceInfo))) {
        return -1;
    }

    auto t1 = Monotonic::TimeNs();

    if (UNLIKELY(!CopyBufferToCopyFreeSpace(static_cast<const uint8_t *>(buf), spaceInfo, bioShmBase))) {
        return -1;
    }

    auto t2 = Monotonic::TimeNs();

    auto ret = SubmitLargeWriteRequest(fd, file->GetInode(), count, offset, spaceInfo);
    if (UNLIKELY(ret < 0)) {
        return -1;
    }

    auto t3 = Monotonic::TimeNs();

    static uint64_t sCount = 0;
    static uint64_t sAllocUs = 0;
    static uint64_t sMemcpyUs = 0;
    static uint64_t sRpcUs = 0;
    static uint64_t sTotalUs = 0;
    sCount++;
    sAllocUs += (t1 - t0) / 1000;
    sMemcpyUs += (t2 - t1) / 1000;
    sRpcUs += (t3 - t2) / 1000;
    sTotalUs += (t3 - t0) / 1000;
    if (sCount >= 1000) {
        CLOG_ERROR("PwriteLargeInner avg latency(us) over " << sCount <<
            " io: alloc=" << sAllocUs / sCount <<
            " memcpy=" << sMemcpyUs / sCount <<
            " rpc=" << sRpcUs / sCount <<
            " total=" << sTotalUs / sCount);
        sCount = 0;
        sAllocUs = 0;
        sMemcpyUs = 0;
        sRpcUs = 0;
        sTotalUs = 0;
    }
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

ssize_t ProxyOperations::Pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    CLOG_DEBUG("Pwrite fd:" << fd << ", offset:" << offset << ", count:" << count << ".");
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
    CLOG_DEBUG("Pwrite64 fd:" << fd << ", offset:" << offset << ", count:" << count << ".");
    auto &file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->pwrite64(fd, buf, count, offset);
    }

    auto ret = PwriteInner(fd, buf, count, static_cast<off_t>(offset));
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
        return count == 0 ? 0 : -1;
    }

    auto ret = PwriteLargeVecInner(fd, file->GetInode(), bufVec, offset);
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
        return count == 0 ? 0 : -1;
    }

    auto ret = PwriteLargeVecInner(fd, file->GetInode(), bufVec, offset);
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

    return Pwritev(fd, vector, count, static_cast<off_t>(offset));
}
