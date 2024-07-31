/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
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

ssize_t ProxyOperations::PreadInner(int fd, void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        CLOG_ERROR("Get fd:" << fd << "failed.");
        return -1;
    }

    if (count > INTERCEPTOR_RDWR_BUFFER_SIZE) {
        return CONTEXT.GetOperations()->pread(fd, buf, count, offset);
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
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("Send read failed inode:" << request.inode << ", offset:" << request.offset << ", length:" <<
            request.nbytes << ".");
        return -1;
    }

    CLOG_DEBUG("Read inode:" << request.inode << ", offset:" << request.offset << ", length:" << request.nbytes <<
        "rsp len:" << resp->dataLen << ".");

    ret = memcpy_s(buf, count, resp->data, resp->dataLen);
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("Memory copy read data:" << resp->dataLen << ", buff size:" << count << " failed:" << ret);
        free(resp);
        return -1;
    }
    auto retLen = static_cast<ssize_t>(resp->dataLen);
    free(resp);
    return retLen;
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
        CLOG_ERROR("Lseek:" << fd << " failed.");
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

    uint8_t *buf = new uint8_t[bufVec.size];
    if (UNLIKELY(buf == nullptr)) {
        return -1;
    }

    auto ret = PreadInner(fd, buf, bufVec.size, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        delete[] buf;
        return -1;
    }

    bufVec.Write((uint8_t *)buf, bufVec.size);
    offset = CONTEXT.GetOperations()->lseek(fd, offset + ret, SEEK_SET);
    delete[] buf;
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

    uint8_t *buf = new uint8_t[bufVec.size];
    if (UNLIKELY(buf == nullptr)) {
        return -1;
    }

    auto ret = PreadInner(fd, buf, bufVec.size, offset);
    if (UNLIKELY(ret < 0)) {
        errno = EIO;
        delete[] buf;
        return -1;
    }

    bufVec.Write(buf, bufVec.size);
    delete[] buf;
    return ret;
}

ssize_t ProxyOperations::PwriteInner(int fd, const void *buf, size_t count, off_t offset)
{
    if (count <= MAX_SMALL_WRITE_SIZIE) {
        return PwriteSmallInner(fd, buf, count, offset);
    } else {
        return PwriteLargeInner(fd, buf, count, offset);
    }
}

ssize_t ProxyOperations::PwriteSmallInner(int fd, const void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    size_t realCount = std::min(count, INTERCEPTOR_RDWR_BUFFER_SIZE);
    size_t reqLen = sizeof(InterceptorPwriteIn) + realCount;
    InterceptorPwriteIn *request = static_cast<InterceptorPwriteIn *>(malloc(reqLen));
    if (UNLIKELY(request == nullptr)) {
        CLOG_ERROR("Memory allocation failed.");
        return -1;
    }
    request->pid = static_cast<uint32_t>(getpid());
    request->fd = static_cast<uint64_t>(fd);
    request->inode = file->GetInode();
    request->offset = offset;
    request->nbytes = realCount;
    request->startTime = Monotonic::TimeNs();
    auto ret = memcpy_s(request->data, realCount, (const char *)buf, realCount);
    if (UNLIKELY(ret != 0)) {
        free(request);
        request = nullptr;
        return -1;
    }

    InterceptorPwriteOut resp;
    ret = InterceptorClientNetService::Instance().SendSyncBuff<InterceptorPwriteOut>(INVALID_NID,
        BIO_OP_INTERCEPTOR_WRITE, request, reqLen, resp);
    if (UNLIKELY(ret != 0)) {
        CLOG_DEBUG("Write fd:" << fd << ", offset:" << offset << ", req->offset" << request->offset << ", count" <<
            count << ", rsp len:" << resp.dataLen << ".");
        free(request);
        request = nullptr;
        return -1;
    }

    free(request);
    request = nullptr;
    CLOG_DEBUG("Write fd:" << fd << ", offset:" << offset << ", count" << count << ", rspLen:" << resp.dataLen << ".");
    return count;
}

ssize_t ProxyOperations::PwriteLargeInner(int fd, const void *buf, size_t count, off_t offset)
{
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return -1;
    }

    if (count != MAX_LARGE_WRITE_SIZE) {
        return CONTEXT.GetOperations()->write(fd, buf, count);
    }

    InterceptorAllocPageReq req;
    InterceptorLargePwriteIn writeReq;
    writeReq.startTime = Monotonic::TimeNs();

    req.pid = static_cast<uint32_t>(getpid());
    req.length = count;
    InterceptorAllocPageRsp resp;
    auto ret = InterceptorClientNetService::Instance().SendSync<InterceptorAllocPageReq, InterceptorAllocPageRsp>(
        INVALID_NID, BIO_OP_INTERCEPTOR_ALLOC_BUFF, req, resp);
    if (UNLIKELY(ret != 0)) {
        CLOG_ERROR("Send large write io request failed:" << ret << ".");
        return -1;
    }

    char *copyBuff = static_cast<char *>(const_cast<void *>(buf));
    CLOG_DEBUG("Alloc write large space:" << count << ", location0:" << resp.address.loc.location[0] <<
        ", location1:" << resp.address.loc.location[1] << ", address0:" << resp.address.address[0].address <<
        ", address0 size:" << resp.address.address[0].size << ", address1:" << resp.address.address[1].address <<
        ", address1 size:" << resp.address.address[1].size << ", address num:" << resp.address.addressNum << ".");
    for (uint32_t i = 0; i < resp.address.addressNum; i++) {
        void *dataBuff = InterceptorClientNetService::Instance().GetShmAddress(resp.addrOffset[i]);
        ret = memcpy_s(dataBuff, resp.address.address[i].size, (const char *)copyBuff, resp.address.address[i].size);
        if (UNLIKELY(ret != 0)) {
            return -1;
        }
        copyBuff += resp.address.address[i].size;
    }

    writeReq.pid = static_cast<uint32_t>(getpid());
    writeReq.fd = static_cast<uint64_t>(fd);
    writeReq.inode = file->GetInode();
    writeReq.offset = offset;
    writeReq.nbytes = count;
    writeReq.address = resp.address;
    InterceptorPwriteOut writeResp;
    ret = InterceptorClientNetService::Instance().SendSync<InterceptorLargePwriteIn, InterceptorPwriteOut>(INVALID_NID,
        BIO_OP_INTERCEPTOR_LARGE_WRITE, writeReq, writeResp);
    if (UNLIKELY(ret != 0)) {
        return -1;
    }

    CLOG_DEBUG("Write fd:" << fd << ", offset:" << offset << ", count" << count << ", rspLen:" << writeResp.dataLen);
    return count;
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
