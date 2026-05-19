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

extern "C" {
INTERCEPTOR_API int open(const char* path, int flags, ...)
{
    INTERCEPTORLOG_DEBUG("Hooking open %s  %d succeeded.", path, flags);
    va_list args;
    va_start(args, flags);
    auto ret = HookOpen(path, flags, args);
    va_end(args);
    return ret;
}

INTERCEPTOR_API int open64(const char* path, int flags, ...)
{
    INTERCEPTORLOG_DEBUG("Hooking open64 %s %d succeeded.", path, flags);
    va_list args;
    va_start(args, flags);
    auto ret = HookOpen64(path, flags, args);
    va_end(args);
    return ret;
}

INTERCEPTOR_API int openat(int dirfd, const char *path, int flags, ...)
{
    INTERCEPTORLOG_DEBUG("Hooking openat %d %s succeeded.", dirfd, path);
    va_list args;
    va_start(args, flags);
    auto ret = HookOpenAt(dirfd, path, flags, args);
    va_end(args);
    return ret;
}

INTERCEPTOR_API int openat64(int dirfd, const char *path, int flags, ...)
{
    INTERCEPTORLOG_DEBUG("Hooking openat64 %d %s succeeded.", dirfd, path);
    va_list args;
    va_start(args, flags);
    auto ret = HookOpenAt64(dirfd, path, flags, args);
    va_end(args);
    return ret;
}

INTERCEPTOR_API int creat(const char* path, mode_t mode)
{
    INTERCEPTORLOG_DEBUG("Hooking creat %s %o succeeded.", path, mode);
    return HookCreat(path, mode);
}

INTERCEPTOR_API int creat64(const char* path, mode_t mode)
{
    INTERCEPTORLOG_DEBUG("Hooking creat64 %s %o succeeded.", path, mode);
    return HookCreat64(path, mode);
}

INTERCEPTOR_API int close(int fd)
{
    INTERCEPTORLOG_DEBUG("Hooking close %d succeeded.", fd);
    return HookClose(fd);
}

INTERCEPTOR_API off_t lseek(int fd, __off_t offset, int whence)
{
    INTERCEPTORLOG_DEBUG("Hooking lseek %d %lld %d succeeded.", fd, offset, whence);
    return HookLseek(fd, offset, whence);
}

INTERCEPTOR_API off64_t lseek64(int fd, __off64_t offset, int whence)
{
    INTERCEPTORLOG_DEBUG("Hooking lseek64 %d %lld %d succeeded.", fd, offset, whence);
    return HookLseek64(fd, offset, whence);
}

INTERCEPTOR_API ssize_t read(int fd, void* buf, size_t nbytes)
{
    INTERCEPTORLOG_DEBUG("Hooking read %d %llu entered.", fd, nbytes);
    return HookRead(fd, buf, nbytes);
}

INTERCEPTOR_API ssize_t pread(int fd, void* buf, size_t nbyte, off_t offset)
{
    INTERCEPTORLOG_DEBUG("Hooking pread %d %llu %lld entered.", fd, nbyte, offset);
    return HookPread(fd, buf, nbyte, offset);
}

INTERCEPTOR_API ssize_t pread64(int fildes, void *buf, size_t nbyte, off64_t offset)
{
    return HookPread64(fildes, buf, nbyte, offset);
}

INTERCEPTOR_API ssize_t write(int fd, const void* buf, size_t nbytes)
{
    INTERCEPTORLOG_DEBUG("Hooking write %d %llu entered.", fd, nbytes);
    return HookWrite(fd, buf, nbytes);
}

INTERCEPTOR_API ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset)
{
    INTERCEPTORLOG_DEBUG("Hooking pwrite %d %llu %lld entered.", fd, count, offset);
    return HookPwrite(fd, buf, count, offset);
}

INTERCEPTOR_API ssize_t pwrite64(int fd, const void* buf, size_t count, off_t offset)
{
    INTERCEPTORLOG_DEBUG("Hooking pwrite64 %d %llu %lld entered.", fd, count, offset);
    return HookPwrite64(fd, buf, count, offset);
}

INTERCEPTOR_API int dup(int fd)
{
    INTERCEPTORLOG_DEBUG("Hooking dup %d succeeded.", fd);
    return HookDup(fd);
}

INTERCEPTOR_API int dup2(int fd, int fd2)
{
    INTERCEPTORLOG_DEBUG("Hooking dup2 %d %d succeeded.", fd, fd2);
    return HookDup2(fd, fd2);
}

INTERCEPTOR_API int dup3(int fd, int fd2, int flags)
{
    INTERCEPTORLOG_DEBUG("Hooking dup3 %d %d succeeded.", fd, fd2);
    return HookDup3(fd, fd2, flags);
}

INTERCEPTOR_API int __xstat(int ver, const char* path, struct stat* stat_buf)
{
    INTERCEPTORLOG_DEBUG("Hooking stat %d %s succeeded.", ver, path);
    return HookStat(ver, path, stat_buf);
}

INTERCEPTOR_API int __xstat64(int ver, const char* path, struct stat64* stat_buf)
{
    INTERCEPTORLOG_DEBUG("Hooking stat64 %d %s succeeded.", ver, path);
    return HookStat64(ver, path, stat_buf);
}

INTERCEPTOR_API int __lxstat(int ver, const char* path, struct stat* stat_buf)
{
    INTERCEPTORLOG_DEBUG("Hooking lstat %d %s succeeded.", ver, path);
    return HookLstat(ver, path, stat_buf);
}

INTERCEPTOR_API int __lxstat64(int ver, const char* path, struct stat64* stat_buf)
{
    INTERCEPTORLOG_DEBUG("Hooking lstat64 %d %s succeeded.", ver, path);
    return HookLstat64(ver, path, stat_buf);
}

INTERCEPTOR_API int __fxstat(int ver, int fd, struct stat* buf)
{
    INTERCEPTORLOG_DEBUG("Hooking fstat %d %d succeeded.", ver, fd);
    return HookFstat(ver, fd, buf);
}

INTERCEPTOR_API int __fxstat64(int ver, int fd, struct stat64* buf)
{
    INTERCEPTORLOG_DEBUG("Hooking fstat64 %d %d succeeded.", ver, fd);
    return HookFstat64(ver, fd, buf);
}

INTERCEPTOR_API int __fxstatat(int ver, int fd, const char* file, struct stat* buf, int flags)
{
    INTERCEPTORLOG_DEBUG("Hooking fstatat %d %d %s %d succeeded.", ver, fd, file, flags);
    return HookFstatAt(ver, fd, file, buf, flags);
}

INTERCEPTOR_API int __fxstatat64(int ver, int fd, const char* file, struct stat64* buf, int flags)
{
    INTERCEPTORLOG_DEBUG("Hooking fstatat64 %d %d %s %d succeeded.", ver, fd, file, flags);
    return HookFstatAt64(ver, fd, file, buf, flags);
}

int INTERCEPTOR_API access(const char* path, int mode)
{
    INTERCEPTORLOG_DEBUG("Hooking access %s %o succeeded.", path, mode);
    return HookAccess(path, mode);
}

INTERCEPTOR_API int unlink(const char* path)
{
    INTERCEPTORLOG_DEBUG("Hooking unlink %s succeeded.", path);
    return HookUnlink(path);
}

INTERCEPTOR_API int unlinkat(int fd, const char *pathname, int flag)
{
    INTERCEPTORLOG_DEBUG("Hooking unlinkat %d %s %d succeeded.", fd, pathname, flag);
    return HookUnlinkat(fd, pathname, flag);
}

INTERCEPTOR_API int fsync(int fd)
{
    INTERCEPTORLOG_DEBUG("Hooking fsync %d succeeded.", fd);
    return HookFsync(fd);
}

INTERCEPTOR_API int fdatasync(int fd)
{
    INTERCEPTORLOG_DEBUG("Hooking fdatasync %d succeeded.", fd);
    return HookFdatasync(fd);
}

INTERCEPTOR_API void sync(void)
{
    INTERCEPTORLOG_DEBUG("Hooking sync succeeded.");
    HookSync();
}

INTERCEPTOR_API int syncfs(int fd)
{
    INTERCEPTORLOG_DEBUG("Hooking syncfs fd(%d) succeeded.", fd);
    return HookSyncFs(fd);
}

INTERCEPTOR_API int truncate(const char* filename, off_t length)
{
    INTERCEPTORLOG_DEBUG("Hooking truncate %s %lld succeeded.", filename, length);
    return HookTruncate(filename, length);
}

INTERCEPTOR_API int truncate64(const char* filename, off_t length)
{
    INTERCEPTORLOG_DEBUG("Hooking truncate64 %s %lld succeeded.", filename, length);
    return HookTruncate64(filename, length);
}

INTERCEPTOR_API int ftruncate(int fd, off_t length)
{
    INTERCEPTORLOG_DEBUG("Hooking ftruncate %d %lld succeeded.", fd, length);
    return HookFtruncate(fd, length);
}

INTERCEPTOR_API int ftruncate64(int fd, off_t length)
{
    INTERCEPTORLOG_DEBUG("Hooking ftruncate64 %d %lld succeeded.", fd, length);
    return HookFtruncate64(fd, length);
}

int INTERCEPTOR_API rename(const char* oldName, const char* newName)
{
    INTERCEPTORLOG_DEBUG("Hooking rename %s %s succeeded.", oldName, newName);
    return HookRename(oldName, newName);
}

INTERCEPTOR_API int utimes(const char *path, const struct timeval times[2])
{
    INTERCEPTORLOG_DEBUG("Hooking utimes %s succeeded.", path);
    return HookUtimes(path, times);
}

INTERCEPTOR_API int remove(const char* path)
{
    INTERCEPTORLOG_DEBUG("Hooking remove %s succeeded.", path);
    return HookRemove(path);
}

INTERCEPTOR_API pid_t fork(void)
{
    INTERCEPTORLOG_DEBUG("Hooking fork succeeded.");
    return HookFork();
}
}
