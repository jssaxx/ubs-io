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

#include <memory>
#include <string>
#include "interceptor.h"
#include "message.h"

namespace ock {
namespace bio {
class OpenFile;

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
    static int OpenAt64Proxy(int dirFd, const char *path, int flags, va_list args);
    static int OpenAt64(int dirFd, const char *path, int flags);
    static int OpenAt64Mode(int dirFd, const char *path, int flags, mode_t mode);
    static int Creat(const char *path, mode_t mode);
    static int Creat64(const char *path, mode_t mode);
    static int Close(int fd);
    static void QueueReadBufferRelease(uint64_t addrOffset, uint64_t length);
    static void FlushPendingReadBufferReleaseForCurrentThread();
    static void FlushAllPendingWriteWindows();
    static int Fsync(int fd);
    static int Fdatasync(int fd);
    static void Sync(void);
    static int SyncFs(int fd);
    static off_t Lseek(int fd, off_t offset, int whence);
    static off64_t Lseek64(int fd, off64_t offset, int whence);

    static ssize_t Pread(int fd, void *buf, size_t count, off_t offset);
    static ssize_t Pread64(int fd, void *buf, size_t count, off64_t offset);
    static ssize_t Read(int fd, void *buf, size_t nbytes);

    static ssize_t Write(int fd, const void *buf, size_t nbytes);
    static ssize_t Pwrite(int fd, const void *buf, size_t count, off_t offset);
    static ssize_t Pwrite64(int fd, const void *buf, size_t count, off64_t offset);
    static pid_t Fork(void);

private:
    static int32_t FullPath(const char *nativePath, std::string &realPath);
    static int32_t FullPath(int dirFd, const char *nativePath, std::string &realPath);
    static bool MatchPath(int fd, std::string &path);
    static int CheckSelfPath(const std::string &mountPoint, const std::string &restoredPath);
    static int32_t OpenInner(const char *api, const char *path, int fd, int flags);
    static int32_t OpenInner(const char *api, int dirFd, const char *path, int fd, int flags);
    static int32_t CreateInner(const char *api, const char *path, int fd);
    static int32_t AttachOpenFileIfNeeded(const char *api, int fd, int flags, const std::string &path,
        int32_t pathRet);
    static ssize_t PreadInner(const char *api, const std::shared_ptr<OpenFile> &file, int fd, void *buf,
        size_t count, off_t offset);
    static ssize_t PreadShmInner(const std::shared_ptr<OpenFile> &file, int fd, void *buf, size_t count,
        off_t offset);
    static ssize_t PwriteInner(const char *api, const std::shared_ptr<OpenFile> &file, int fd, const void *buf,
        size_t count, off_t offset);
    static bool FlushPendingWriteWindow(const std::shared_ptr<OpenFile> &file, int fd);
    static ssize_t PwriteAggregatedDirectSpaceInner(const std::shared_ptr<OpenFile> &file, int fd, const void *buf,
        size_t count, off_t offset);
    static ssize_t PwriteDirectSpaceInner(const std::shared_ptr<OpenFile> &file, int fd, const void *buf,
        size_t count, off_t offset);
    static ssize_t PwriteShmInner(const std::shared_ptr<OpenFile> &file, int fd, const void *buf, size_t count,
        off_t offset, bool *intercepted);
};
}
}
#endif // BOOST_IO_PROXY_OPERATIONS_H
