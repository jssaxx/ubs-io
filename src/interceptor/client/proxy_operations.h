/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#ifndef BOOST_IO_PROXY_OPERATIONS_H
#define BOOST_IO_PROXY_OPERATIONS_H

#include <tuple>
#include <chrono>
#include <pthread.h>
#include <unordered_map>
#include <string>
#include "interceptor.h"
#include "message.h"

namespace ock {
namespace bio {
const size_t INTERCEPTOR_RDWR_BUFFER_SIZE = 8192L;
const size_t MAX_SMALL_WRITE_SIZIE = 4096L;
const size_t MAX_LARGE_WRITE_SIZE = (4 * 1024 * 1024L);

class ProxyOperations {
public:
    static void FillInterceptorOps(InterceptorProxyOperations &ops);
    static struct InterceptorProxyOperations *GetOperations() noexcept;

public:
    static int OpenProxy(const char *path, int flags, va_list args);
    static int Open(const char *path, int flags);
    static int OpenMode(const char *path, int flags, mode_t mode);
    static int Open64Proxy(const char *path, int flags, va_list args);
    static int Open64(const char *path, int flags);
    static int Open64Mode(const char *path, int flags, mode_t mode);
    static int OpenAtProxy(int dirFd, const char *path, int flags, va_list args);
    static int OpenAt(int dirFd, const char *path, int flags);
    static int OpenAtMode(int dirFd, const char *path, int flags, mode_t mode);
    static int Creat(const char *path, mode_t mode);
    static int Creat64(const char *path, mode_t mode);
    static int Close(int fd);

    static ssize_t Pread(int fd, void *buf, size_t count, off_t offset);
    static ssize_t Pread64(int fd, void *buf, size_t count, off64_t offset);
    static ssize_t Read(int fd, void *buf, size_t nbytes);
    static ssize_t Readv(int fd, const struct iovec *vector, int count);
    static ssize_t preadv64(int fd, const struct iovec *vector, int iovcnt, off64_t offset);

    static ssize_t Write(int fd, const void *buf, size_t nbytes);

    static bool CheckPwriteLargeInnerResp(InterceptorPwriteOut &writeResp);
    static bool CheckPwriteSmallInnerResp(InterceptorPwriteOut &resp);
    static bool CheckPreadInnerResp(InterceptorPreadOut **resp);

private:
    static int32_t FullPath(const char *nativePath, std::string &realPath);
    static int32_t FullPath(int dirFd, const char *nativePath, std::string &realPath);
    static bool MatchPath(int fd, std::string &path);
    static int CheckSelfPath(const std::string &mountPoint, const std::string &restoredPath);
    static int32_t OpenInner(const char *path, int fd);
    static int32_t OpenInner(int dirFd, const char *path, int fd);
    static int32_t CreateInner(const char *path, int fd);
    static ssize_t PreadInner(int fd, void *buf, size_t count, off_t offset);
    static ssize_t PwriteInner(int fd, const void *buf, size_t count, off_t offset);
    static ssize_t PwriteSmallInner(int fd, const void *buf, size_t count, off_t offset);
    static ssize_t PwriteLargeInner(int fd, const void *buf, size_t count, off_t offset);
};
}
}
#endif // BOOST_IO_PROXY_OPERATIONS_H
