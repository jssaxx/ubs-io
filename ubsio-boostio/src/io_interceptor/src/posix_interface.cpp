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

#include <cstdarg>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <sys/stat.h>

#include "ceptor_log.h"
#include "interceptor_symbol_visibility.h"
#include "posix_interceptor.h"

using namespace ock::interceptor;

#define POSIX_HOOK_ENTER(args...) INTERCEPTORLOG_DEBUG("Intercept %s enter, " args, __FUNCTION__)
#define POSIX_HOOK_SUCCESS(args...) INTERCEPTORLOG_DEBUG("Intercept %s success, " args, __FUNCTION__)

extern "C" {
INTERCEPTOR_API int open(const char* path, int flags, ...)
{
    POSIX_HOOK_ENTER("path:%s, flags:%d.", path, flags);
    va_list args;
    va_start(args, flags);
    auto ret = HookOpen(path, flags, args);
    va_end(args);
    POSIX_HOOK_SUCCESS("path:%s, flags:%d, ret:%d.", path, flags, ret);
    return ret;
}

INTERCEPTOR_API int open64(const char* path, int flags, ...)
{
    POSIX_HOOK_ENTER("path:%s, flags:%d.", path, flags);
    va_list args;
    va_start(args, flags);
    auto ret = HookOpen64(path, flags, args);
    va_end(args);
    POSIX_HOOK_SUCCESS("path:%s, flags:%d, ret:%d.", path, flags, ret);
    return ret;
}

INTERCEPTOR_API int openat(int dirfd, const char *path, int flags, ...)
{
    POSIX_HOOK_ENTER("dirfd:%d, path:%s, flags:%d.", dirfd, path, flags);
    va_list args;
    va_start(args, flags);
    auto ret = HookOpenAt(dirfd, path, flags, args);
    va_end(args);
    POSIX_HOOK_SUCCESS("dirfd:%d, path:%s, flags:%d, ret:%d.", dirfd, path, flags, ret);
    return ret;
}

INTERCEPTOR_API int openat64(int dirfd, const char *path, int flags, ...)
{
    POSIX_HOOK_ENTER("dirfd:%d, path:%s, flags:%d.", dirfd, path, flags);
    va_list args;
    va_start(args, flags);
    auto ret = HookOpenAt64(dirfd, path, flags, args);
    va_end(args);
    POSIX_HOOK_SUCCESS("dirfd:%d, path:%s, flags:%d, ret:%d.", dirfd, path, flags, ret);
    return ret;
}

INTERCEPTOR_API int creat(const char* path, mode_t mode)
{
    POSIX_HOOK_ENTER("path:%s, mode:%o.", path, mode);
    auto ret = HookCreat(path, mode);
    POSIX_HOOK_SUCCESS("path:%s, mode:%o, ret:%d.", path, mode, ret);
    return ret;
}

INTERCEPTOR_API int creat64(const char* path, mode_t mode)
{
    POSIX_HOOK_ENTER("path:%s, mode:%o.", path, mode);
    auto ret = HookCreat64(path, mode);
    POSIX_HOOK_SUCCESS("path:%s, mode:%o, ret:%d.", path, mode, ret);
    return ret;
}

INTERCEPTOR_API int close(int fd)
{
    POSIX_HOOK_ENTER("fd:%d.", fd);
    auto ret = HookClose(fd);
    POSIX_HOOK_SUCCESS("fd:%d, ret:%d.", fd, ret);
    return ret;
}

INTERCEPTOR_API off_t lseek(int fd, __off_t offset, int whence)
{
    POSIX_HOOK_ENTER("fd:%d, offset:%lld, whence:%d.", fd, offset, whence);
    auto ret = HookLseek(fd, offset, whence);
    POSIX_HOOK_SUCCESS("fd:%d, offset:%lld, whence:%d, ret:%lld.", fd, offset, whence, ret);
    return ret;
}

INTERCEPTOR_API off64_t lseek64(int fd, __off64_t offset, int whence)
{
    POSIX_HOOK_ENTER("fd:%d, offset:%lld, whence:%d.", fd, offset, whence);
    auto ret = HookLseek64(fd, offset, whence);
    POSIX_HOOK_SUCCESS("fd:%d, offset:%lld, whence:%d, ret:%lld.", fd, offset, whence, ret);
    return ret;
}

INTERCEPTOR_API ssize_t read(int fd, void* buf, size_t nbytes)
{
    POSIX_HOOK_ENTER("fd:%d, nbytes:%llu.", fd, nbytes);
    auto ret = HookRead(fd, buf, nbytes);
    POSIX_HOOK_SUCCESS("fd:%d, nbytes:%llu, ret:%zd.", fd, nbytes, ret);
    return ret;
}

INTERCEPTOR_API ssize_t pread(int fd, void* buf, size_t nbyte, off_t offset)
{
    POSIX_HOOK_ENTER("fd:%d, nbytes:%llu, offset:%lld.", fd, nbyte, offset);
    auto ret = HookPread(fd, buf, nbyte, offset);
    POSIX_HOOK_SUCCESS("fd:%d, nbytes:%llu, offset:%lld, ret:%zd.", fd, nbyte, offset, ret);
    return ret;
}

INTERCEPTOR_API ssize_t pread64(int fildes, void *buf, size_t nbyte, off64_t offset)
{
    POSIX_HOOK_ENTER("fd:%d, nbytes:%llu, offset:%lld.", fildes, nbyte, offset);
    auto ret = HookPread64(fildes, buf, nbyte, offset);
    POSIX_HOOK_SUCCESS("fd:%d, nbytes:%llu, offset:%lld, ret:%zd.", fildes, nbyte, offset, ret);
    return ret;
}

INTERCEPTOR_API ssize_t readv(int fd, const struct iovec* vector, int count)
{
    POSIX_HOOK_ENTER("fd:%d, iovcnt:%d.", fd, count);
    auto ret = HookReadv(fd, vector, count);
    POSIX_HOOK_SUCCESS("fd:%d, iovcnt:%d, ret:%zd.", fd, count, ret);
    return ret;
}

INTERCEPTOR_API ssize_t preadv(int fd, const struct iovec* vector, int iovcnt, off_t offset)
{
    POSIX_HOOK_ENTER("fd:%d, iovcnt:%d, offset:%lld.", fd, iovcnt, offset);
    auto ret = HookPreadv(fd, vector, iovcnt, offset);
    POSIX_HOOK_SUCCESS("fd:%d, iovcnt:%d, offset:%lld, ret:%zd.", fd, iovcnt, offset, ret);
    return ret;
}

INTERCEPTOR_API ssize_t preadv64(int fd, const struct iovec* vector, int iovcnt, off64_t offset)
{
    POSIX_HOOK_ENTER("fd:%d, iovcnt:%d, offset:%lld.", fd, iovcnt, offset);
    auto ret = HookPreadv64(fd, vector, iovcnt, offset);
    POSIX_HOOK_SUCCESS("fd:%d, iovcnt:%d, offset:%lld, ret:%zd.", fd, iovcnt, offset, ret);
    return ret;
}

INTERCEPTOR_API ssize_t write(int fd, const void* buf, size_t nbytes)
{
    POSIX_HOOK_ENTER("fd:%d, nbytes:%llu.", fd, nbytes);
    auto ret = HookWrite(fd, buf, nbytes);
    POSIX_HOOK_SUCCESS("fd:%d, nbytes:%llu, ret:%zd.", fd, nbytes, ret);
    return ret;
}

INTERCEPTOR_API ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset)
{
    POSIX_HOOK_ENTER("fd:%d, nbytes:%llu, offset:%lld.", fd, count, offset);
    auto ret = HookPwrite(fd, buf, count, offset);
    POSIX_HOOK_SUCCESS("fd:%d, nbytes:%llu, offset:%lld, ret:%zd.", fd, count, offset, ret);
    return ret;
}

INTERCEPTOR_API ssize_t pwrite64(int fd, const void* buf, size_t count, off_t offset)
{
    POSIX_HOOK_ENTER("fd:%d, nbytes:%llu, offset:%lld.", fd, count, offset);
    auto ret = HookPwrite64(fd, buf, count, offset);
    POSIX_HOOK_SUCCESS("fd:%d, nbytes:%llu, offset:%lld, ret:%zd.", fd, count, offset, ret);
    return ret;
}

INTERCEPTOR_API ssize_t writev(int fd, const struct iovec* vector, int count)
{
    POSIX_HOOK_ENTER("fd:%d, iovcnt:%d.", fd, count);
    auto ret = HookWritev(fd, vector, count);
    POSIX_HOOK_SUCCESS("fd:%d, iovcnt:%d, ret:%zd.", fd, count, ret);
    return ret;
}

INTERCEPTOR_API ssize_t pwritev(int fd, const struct iovec* vector, int iovcnt, off_t offset)
{
    POSIX_HOOK_ENTER("fd:%d, iovcnt:%d, offset:%lld.", fd, iovcnt, offset);
    auto ret = HookPwritev(fd, vector, iovcnt, offset);
    POSIX_HOOK_SUCCESS("fd:%d, iovcnt:%d, offset:%lld, ret:%zd.", fd, iovcnt, offset, ret);
    return ret;
}

INTERCEPTOR_API ssize_t pwritev64(int fd, const struct iovec* vector, int iovcnt, off64_t offset)
{
    POSIX_HOOK_ENTER("fd:%d, iovcnt:%d, offset:%lld.", fd, iovcnt, offset);
    auto ret = HookPwritev64(fd, vector, iovcnt, offset);
    POSIX_HOOK_SUCCESS("fd:%d, iovcnt:%d, offset:%lld, ret:%zd.", fd, iovcnt, offset, ret);
    return ret;
}

INTERCEPTOR_API int dup(int fd)
{
    POSIX_HOOK_ENTER("fd:%d.", fd);
    auto ret = HookDup(fd);
    POSIX_HOOK_SUCCESS("fd:%d, ret:%d.", fd, ret);
    return ret;
}

INTERCEPTOR_API int dup2(int fd, int fd2)
{
    POSIX_HOOK_ENTER("oldfd:%d, newfd:%d.", fd, fd2);
    auto ret = HookDup2(fd, fd2);
    POSIX_HOOK_SUCCESS("oldfd:%d, newfd:%d, ret:%d.", fd, fd2, ret);
    return ret;
}

INTERCEPTOR_API int dup3(int fd, int fd2, int flags)
{
    POSIX_HOOK_ENTER("oldfd:%d, newfd:%d, flags:%d.", fd, fd2, flags);
    auto ret = HookDup3(fd, fd2, flags);
    POSIX_HOOK_SUCCESS("oldfd:%d, newfd:%d, flags:%d, ret:%d.", fd, fd2, flags, ret);
    return ret;
}

INTERCEPTOR_API int __xstat(int ver, const char* path, struct stat* stat_buf)
{
    POSIX_HOOK_ENTER("ver:%d, path:%s.", ver, path);
    auto ret = HookStat(ver, path, stat_buf);
    POSIX_HOOK_SUCCESS("ver:%d, path:%s, ret:%d.", ver, path, ret);
    return ret;
}

INTERCEPTOR_API int __xstat64(int ver, const char* path, struct stat64* stat_buf)
{
    POSIX_HOOK_ENTER("ver:%d, path:%s.", ver, path);
    auto ret = HookStat64(ver, path, stat_buf);
    POSIX_HOOK_SUCCESS("ver:%d, path:%s, ret:%d.", ver, path, ret);
    return ret;
}

INTERCEPTOR_API int __lxstat(int ver, const char* path, struct stat* stat_buf)
{
    POSIX_HOOK_ENTER("ver:%d, path:%s.", ver, path);
    auto ret = HookLstat(ver, path, stat_buf);
    POSIX_HOOK_SUCCESS("ver:%d, path:%s, ret:%d.", ver, path, ret);
    return ret;
}

INTERCEPTOR_API int __lxstat64(int ver, const char* path, struct stat64* stat_buf)
{
    POSIX_HOOK_ENTER("ver:%d, path:%s.", ver, path);
    auto ret = HookLstat64(ver, path, stat_buf);
    POSIX_HOOK_SUCCESS("ver:%d, path:%s, ret:%d.", ver, path, ret);
    return ret;
}

INTERCEPTOR_API int __fxstat(int ver, int fd, struct stat* buf)
{
    POSIX_HOOK_ENTER("ver:%d, fd:%d.", ver, fd);
    auto ret = HookFstat(ver, fd, buf);
    POSIX_HOOK_SUCCESS("ver:%d, fd:%d, ret:%d.", ver, fd, ret);
    return ret;
}

INTERCEPTOR_API int __fxstat64(int ver, int fd, struct stat64* buf)
{
    POSIX_HOOK_ENTER("ver:%d, fd:%d.", ver, fd);
    auto ret = HookFstat64(ver, fd, buf);
    POSIX_HOOK_SUCCESS("ver:%d, fd:%d, ret:%d.", ver, fd, ret);
    return ret;
}

INTERCEPTOR_API int __fxstatat(int ver, int fd, const char* file, struct stat* buf, int flags)
{
    POSIX_HOOK_ENTER("ver:%d, dirfd:%d, path:%s, flags:%d.", ver, fd, file, flags);
    auto ret = HookFstatAt(ver, fd, file, buf, flags);
    POSIX_HOOK_SUCCESS("ver:%d, dirfd:%d, path:%s, flags:%d, ret:%d.", ver, fd, file, flags, ret);
    return ret;
}

INTERCEPTOR_API int __fxstatat64(int ver, int fd, const char* file, struct stat64* buf, int flags)
{
    POSIX_HOOK_ENTER("ver:%d, dirfd:%d, path:%s, flags:%d.", ver, fd, file, flags);
    auto ret = HookFstatAt64(ver, fd, file, buf, flags);
    POSIX_HOOK_SUCCESS("ver:%d, dirfd:%d, path:%s, flags:%d, ret:%d.", ver, fd, file, flags, ret);
    return ret;
}

int INTERCEPTOR_API access(const char* path, int mode)
{
    POSIX_HOOK_ENTER("path:%s, mode:%o.", path, mode);
    auto ret = HookAccess(path, mode);
    POSIX_HOOK_SUCCESS("path:%s, mode:%o, ret:%d.", path, mode, ret);
    return ret;
}

INTERCEPTOR_API int unlink(const char* path)
{
    POSIX_HOOK_ENTER("path:%s.", path);
    auto ret = HookUnlink(path);
    POSIX_HOOK_SUCCESS("path:%s, ret:%d.", path, ret);
    return ret;
}

INTERCEPTOR_API int unlinkat(int fd, const char *pathname, int flag)
{
    POSIX_HOOK_ENTER("dirfd:%d, path:%s, flags:%d.", fd, pathname, flag);
    auto ret = HookUnlinkat(fd, pathname, flag);
    POSIX_HOOK_SUCCESS("dirfd:%d, path:%s, flags:%d, ret:%d.", fd, pathname, flag, ret);
    return ret;
}

INTERCEPTOR_API int fsync(int fd)
{
    POSIX_HOOK_ENTER("fd:%d.", fd);
    auto ret = HookFsync(fd);
    POSIX_HOOK_SUCCESS("fd:%d, ret:%d.", fd, ret);
    return ret;
}

INTERCEPTOR_API void sync(void)
{
    POSIX_HOOK_ENTER("void.");
    HookSync();
    POSIX_HOOK_SUCCESS("completed.");
}

INTERCEPTOR_API int syncfs(int fd)
{
    POSIX_HOOK_ENTER("fd:%d.", fd);
    auto ret = HookSyncFs(fd);
    POSIX_HOOK_SUCCESS("fd:%d, ret:%d.", fd, ret);
    return ret;
}

INTERCEPTOR_API int truncate(const char* filename, off_t length)
{
    POSIX_HOOK_ENTER("path:%s, length:%lld.", filename, length);
    auto ret = HookTruncate(filename, length);
    POSIX_HOOK_SUCCESS("path:%s, length:%lld, ret:%d.", filename, length, ret);
    return ret;
}

INTERCEPTOR_API int truncate64(const char* filename, off_t length)
{
    POSIX_HOOK_ENTER("path:%s, length:%lld.", filename, length);
    auto ret = HookTruncate64(filename, length);
    POSIX_HOOK_SUCCESS("path:%s, length:%lld, ret:%d.", filename, length, ret);
    return ret;
}

INTERCEPTOR_API int ftruncate(int fd, off_t length)
{
    POSIX_HOOK_ENTER("fd:%d, length:%lld.", fd, length);
    auto ret = HookFtruncate(fd, length);
    POSIX_HOOK_SUCCESS("fd:%d, length:%lld, ret:%d.", fd, length, ret);
    return ret;
}

INTERCEPTOR_API int ftruncate64(int fd, off_t length)
{
    POSIX_HOOK_ENTER("fd:%d, length:%lld.", fd, length);
    auto ret = HookFtruncate64(fd, length);
    POSIX_HOOK_SUCCESS("fd:%d, length:%lld, ret:%d.", fd, length, ret);
    return ret;
}

int INTERCEPTOR_API rename(const char* oldName, const char* newName)
{
    POSIX_HOOK_ENTER("old:%s, new:%s.", oldName, newName);
    auto ret = HookRename(oldName, newName);
    POSIX_HOOK_SUCCESS("old:%s, new:%s, ret:%d.", oldName, newName, ret);
    return ret;
}

INTERCEPTOR_API int utimes(const char *path, const struct timeval times[2])
{
    POSIX_HOOK_ENTER("path:%s.", path);
    auto ret = HookUtimes(path, times);
    POSIX_HOOK_SUCCESS("path:%s, ret:%d.", path, ret);
    return ret;
}

INTERCEPTOR_API int remove(const char* path)
{
    POSIX_HOOK_ENTER("path:%s.", path);
    auto ret = HookRemove(path);
    POSIX_HOOK_SUCCESS("path:%s, ret:%d.", path, ret);
    return ret;
}
}
