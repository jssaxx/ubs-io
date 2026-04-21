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

#ifndef BOOST_IO_PROXY_OPERATIONS_H
#define BOOST_IO_PROXY_OPERATIONS_H

#include <tuple>
#include <chrono>
#include <pthread.h>
#include <unordered_map>
#include <string>
#include "interceptor.h"
#include "message.h"
#include "bufvec.h"

namespace ock {
namespace bio {
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
    static ssize_t Pwrite(int fd, const void *buf, size_t count, off_t offset);
    static ssize_t Pwrite64(int fd, const void *buf, size_t count, off64_t offset);
    static ssize_t Writev(int fd, const struct iovec *vector, int count);
    static ssize_t pwritev(int fd, const struct iovec *vector, int count, off_t offset);
    static ssize_t pwritev64(int fd, const struct iovec *vector, int count, off64_t offset);

private:
    static int32_t FullPath(const char *nativePath, std::string &realPath);
    static int32_t FullPath(int dirFd, const char *nativePath, std::string &realPath);
    static bool MatchPath(int fd, std::string &path);
    static int CheckSelfPath(const std::string &mountPoint, const std::string &restoredPath);
    static int32_t OpenInner(const char *path, int fd);
    static int32_t OpenInner(int dirFd, const char *path, int fd);
    static int32_t CreateInner(const char *path, int fd);
    static ssize_t PreadInner(int fd, void *buf, size_t count, off_t offset);
    static ssize_t PreadInner(int fd, BufVec &bufVec, off_t offset);
    static ssize_t PreadSmallInner(int fd, void *buf, size_t count, off_t offset);
    static ssize_t PreadSmallInner(int fd, BufVec &bufVec, off_t offset);
    static ssize_t PreadLargeInner(int fd, void *buf, size_t count, off_t offset);
    static ssize_t PreadLargeInner(int fd, BufVec &bufVec, off_t offset);
    static ssize_t PwriteInner(int fd, const void *buf, size_t count, off_t offset);
    static ssize_t PwriteInner(int fd, BufVec &bufVec, off_t offset);
    static ssize_t PwriteSmallInner(int fd, const void *buf, size_t count, off_t offset);
    static ssize_t PwriteSmallInner(int fd, BufVec &bufVec, off_t offset);
    static ssize_t PwriteLargeInner(int fd, const void *buf, size_t count, off_t offset);
    static ssize_t PwriteLargeInner(int fd, BufVec &bufVec, off_t offset);
};
}
}
#endif // BOOST_IO_PROXY_OPERATIONS_H
