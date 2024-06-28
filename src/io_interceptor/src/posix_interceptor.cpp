/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 */

#include "posix_interceptor.h"

#include <cstring>
#include <fcntl.h>
#include <dirent.h>
#include <memory>
#include <cstdarg>

#include "ceptor_log.h"
#include "interceptor.h"
#include "native_operations_loader.h"
#include "proxy_operations_loader.h"
#include "interceptor_symbol_visibility.h"

#ifdef BUILD_WITH_UT
INTERCEPTOR_API struct InterceptorNativeOperations *GetNativeOperations()
{
    return &(ock::interceptor::NativeOperationsLoader::GetProxy());
}
#endif

namespace ock {
namespace interceptor {
void __attribute__((constructor)) InitHook();

void InitHook()
{
    static bool initialized = false;
    if (initialized) {
        INTERCEPTORLOG_INFO("Client is already initialized.");
        return;
    }
#ifndef BUILD_WITH_UT
    if (!ock::interceptor::NativeOperationsLoader::GetInstance().Initialize() ||
        !ock::interceptor::ProxyOperationsLoader::GetInstance().Initialize()) {
#else
    if (!ock::interceptor::NativeOperationsLoader::GetInstance().Initialize()) {
#endif
        initialized = true;
        return;
    }
    INTERCEPTORLOG_INFO("Finishing init.");
    initialized = true;
    return;
}

static inline bool CheckPointer(const void *ptr)
{
    if (ptr == nullptr) {
        errno = EFAULT;
        return false;
    }
    return true;
}

static inline bool CheckPath(const char *path)
{
    if (path == nullptr) {
        errno = EFAULT;
        return false;
    }
    if (path[0] == '\0') {
        errno = ENOENT;
        return false;
    }
    return true;
}

int HookOpen(const char *path, int flags, va_list args)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(open)) {
        return -1;
    }
    int ret;
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(open)) {
        mode_t mode = va_arg(args, mode_t);
        ret = NATIVE(open)(path, flags, mode);
        return ret;
    }
    ret = PROXY(open)(path, flags, args);
    return ret;
}

int HookOpen64(const char *path, int flags, va_list args)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(open64)) {
        return -1;
    }
    int ret;
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(open64)) {
        mode_t mode = va_arg(args, mode_t);
        ret = NATIVE(open64)(path, flags, mode);
        return ret;
    }
    ret = PROXY(open64)(path, flags, args);
    return ret;
}

int HookOpenAt(int dirfd, const char *path, int flags, va_list args)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(openat)) {
        return -1;
    }
    int ret;
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(openat)) {
        mode_t mode = va_arg(args, mode_t);
        ret = NATIVE(openat)(dirfd, path, flags, mode);
        return ret;
    }
    ret = PROXY(openat)(dirfd, path, flags, args);
    return ret;
}

int HookOpenAt64(int dirfd, const char *path, int flags, va_list args)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(openat64)) {
        return -1;
    }
    int ret;
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(openat64)) {
        mode_t mode = va_arg(args, mode_t);
        ret = NATIVE(openat64)(dirfd, path, flags, mode);
        return ret;
    }
    ret = PROXY(openat64)(dirfd, path, flags, args);
    return ret;
}

int HookCreat(const char *path, mode_t mode)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(creat)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(creat)) {
        return NATIVE(creat)(path, mode);
    }
    return PROXY(creat)(path, mode);
}

int HookCreat64(const char *path, mode_t mode)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(creat64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(creat64)) {
        return NATIVE(creat64)(path, mode);
    }
    return PROXY(creat64)(path, mode);
}

int HookClose(int fd)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(close)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(close)) {
        return NATIVE(close)(fd);
    }

    return PROXY(close)(fd);
}

off_t HookLseek(int fd, off_t offset, int whence)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(lseek)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(lseek)) {
        return NATIVE(lseek)(fd, offset, whence);
    }

    return PROXY(lseek)(fd, offset, whence);
}

off64_t HookLseek64(int fd, off64_t offset, int whence)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(lseek64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(lseek64)) {
        return NATIVE(lseek64)(fd, offset, whence);
    }

    return PROXY(lseek64)(fd, offset, whence);
}

ssize_t HookRead(int fd, void *buf, size_t nbytes)
{
    if (!CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(read)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(read)) {
        return NATIVE(read)(fd, buf, nbytes);
    }
    return PROXY(read)(fd, buf, nbytes);
}

ssize_t HookPread(int fd, void *buf, size_t count, off_t offset)
{
    if (!CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(pread)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(pread)) {
        return NATIVE(pread)(fd, buf, count, offset);
    }
    return PROXY(pread)(fd, buf, count, offset);
}

ssize_t HookPread64(int fd, void *buf, size_t nbyte, off64_t offset)
{
    if (!CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(pread64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(pread64)) {
        return NATIVE(pread64)(fd, buf, nbyte, offset);
    }
    return PROXY(pread64)(fd, buf, nbyte, offset);
}

ssize_t HookReadv(int fd, const struct iovec *vector, int count)
{
    if (!CheckPointer(vector) || !InitNativeHook() || CHECKNATIVEFUNC(readv)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(readv)) {
        return NATIVE(readv)(fd, vector, count);
    }
    return PROXY(readv)(fd, vector, count);
}

ssize_t HookPreadv(int fd, const struct iovec *vector, int count, off_t offset)
{
    if (!CheckPointer(vector) || !InitNativeHook() || CHECKNATIVEFUNC(preadv)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(preadv)) {
        return NATIVE(preadv)(fd, vector, count, offset);
    }
    return PROXY(preadv)(fd, vector, count, offset);
}

ssize_t HookPreadv64(int fd, const struct iovec *vector, int iovcnt, off64_t offset)
{
    if (!CheckPointer(vector) || !InitNativeHook() || CHECKNATIVEFUNC(preadv64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(preadv64)) {
        return NATIVE(preadv64)(fd, vector, iovcnt, offset);
    }
    return PROXY(preadv64)(fd, vector, iovcnt, offset);
}

ssize_t HookWrite(int fd, const void *buf, size_t nbytes)
{
    if (!CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(write)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(write)) {
        return NATIVE(write)(fd, buf, nbytes);
    }
    return PROXY(write)(fd, buf, nbytes);
}

ssize_t HookPwrite(int fd, const void *buf, size_t count, off_t offset)
{
    if (!CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(pwrite)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(pwrite)) {
        return NATIVE(pwrite)(fd, buf, count, offset);
    }
    return PROXY(pwrite)(fd, buf, count, offset);
}

ssize_t HookPwrite64(int fd, const void *buf, size_t count, off64_t offset)
{
    if (!CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(pwrite64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(pwrite64)) {
        return NATIVE(pwrite64)(fd, buf, count, offset);
    }
    return PROXY(pwrite64)(fd, buf, count, offset);
}

ssize_t HookWritev(int fd, const struct iovec *vector, int count)
{
    if (!CheckPointer(vector) || !InitNativeHook() || CHECKNATIVEFUNC(writev)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(writev)) {
        return NATIVE(writev)(fd, vector, count);
    }
    return PROXY(writev)(fd, vector, count);
}

ssize_t HookPwritev(int fd, const struct iovec *vector, int count, off_t offset)
{
    if (!CheckPointer(vector) || !InitNativeHook() || CHECKNATIVEFUNC(pwritev)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(pwritev)) {
        return NATIVE(pwritev)(fd, vector, count, offset);
    }
    return PROXY(pwritev)(fd, vector, count, offset);
}

ssize_t HookPwritev64(int fd, const struct iovec *vector, int count, off64_t offset)
{
    if (!CheckPointer(vector) || !InitNativeHook() || CHECKNATIVEFUNC(pwritev64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(pwritev64)) {
        return NATIVE(pwritev64)(fd, vector, count, offset);
    }
    return PROXY(pwritev64)(fd, vector, count, offset);
}

int HookTruncate(const char *path, off_t length)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(truncate)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(truncate)) {
        return NATIVE(truncate)(path, length);
    }
    return PROXY(truncate)(path, length);
}

int HookTruncate64(const char *path, off_t length)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(truncate64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(truncate64)) {
        return NATIVE(truncate64)(path, length);
    }
    return PROXY(truncate64)(path, length);
}

int HookFtruncate(int fd, off_t length)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(ftruncate)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(ftruncate)) {
        return NATIVE(ftruncate)(fd, length);
    }
    return PROXY(ftruncate)(fd, length);
}

int HookFtruncate64(int fd, off_t length)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(ftruncate64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(ftruncate64)) {
        return NATIVE(ftruncate64)(fd, length);
    }
    return PROXY(ftruncate64)(fd, length);
}

int HookDup(int fd)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(dup)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(dup)) {
        return NATIVE(dup)(fd);
    }
    return PROXY(dup)(fd);
}

int HookDup2(int oldFildes, int newFildes)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(dup2)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(dup2)) {
        return NATIVE(dup2)(oldFildes, newFildes);
    }
    return PROXY(dup2)(oldFildes, newFildes);
}

int HookDup3(int oldFildes, int newFildes, int flags)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(dup3)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(dup3)) {
        return NATIVE(dup3)(oldFildes, newFildes, flags);
    }
    return PROXY(dup3)(oldFildes, newFildes, flags);
}

int HookStat(int ver, const char *path, struct stat *buf)
{
    if (!CheckPath(path) || !CheckPointer((buf)) || !InitNativeHook() || CHECKNATIVEFUNC(__xstat)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(__xstat)) {
        return NATIVE(__xstat)(ver, path, buf);
    }
    return PROXY(__xstat)(ver, path, buf);
}

int HookStat64(int ver, const char *path, struct stat64 *buf)
{
    if (!CheckPath(path) || !CheckPointer((buf)) || !InitNativeHook() || CHECKNATIVEFUNC(__xstat64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(__xstat64)) {
        return NATIVE(__xstat64)(ver, path, buf);
    }
    return PROXY(__xstat64)(ver, path, buf);
}

int HookLstat(int ver, const char *path, struct stat *buf)
{
    if (!CheckPath(path) || !CheckPointer((buf)) || !InitNativeHook() || CHECKNATIVEFUNC(__lxstat)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(__lxstat)) {
        return NATIVE(__lxstat)(ver, path, buf);
    }
    return PROXY(__lxstat)(ver, path, buf);
}

int HookLstat64(int ver, const char *path, struct stat64 *buf)
{
    if (!CheckPath(path) || !CheckPointer((buf)) || !InitNativeHook() || CHECKNATIVEFUNC(__lxstat64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(__lxstat64)) {
        return NATIVE(__lxstat64)(ver, path, buf);
    }
    return PROXY(__lxstat64)(ver, path, buf);
}

int HookFstat(int ver, int fd, struct stat *buf)
{
    if (!CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(__fxstat)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(__fxstat)) {
        return NATIVE(__fxstat)(ver, fd, buf);
    }
    return PROXY(__fxstat)(ver, fd, buf);
}

int HookFstat64(int ver, int fd, struct stat64 *buf)
{
    if (!CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(__fxstat64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(__fxstat64)) {
        return NATIVE(__fxstat64)(ver, fd, buf);
    }
    return PROXY(__fxstat64)(ver, fd, buf);
}

int HookFstatAt(int ver, int fd, const char *file, struct stat *buf, int flags)
{
    if (!CheckPath(file) || !CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(__fxstatat)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(__fxstatat)) {
        return NATIVE(__fxstatat)(ver, fd, file, buf, flags);
    }
    return PROXY(__fxstatat)(ver, fd, file, buf, flags);
}

int HookFstatAt64(int ver, int fd, const char *file, struct stat64 *buf, int flags)
{
    if (!CheckPath(file) || !CheckPointer(buf) || !InitNativeHook() || CHECKNATIVEFUNC(__fxstatat64)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(__fxstatat64)) {
        return NATIVE(__fxstatat64)(ver, fd, file, buf, flags);
    }
    return PROXY(__fxstatat64)(ver, fd, file, buf, flags);
}

int HookAccess(const char *path, int mode)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(access)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(access)) {
        return NATIVE(access)(path, mode);
    }
    return PROXY(access)(path, mode);
}

int HookUnlink(const char *path)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(unlink)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(unlink)) {
        return NATIVE(unlink)(path);
    }
    return PROXY(unlink)(path);
}

int HookUnlinkat(int fd, const char *path, int flag)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(unlinkat)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(unlinkat)) {
        return NATIVE(unlinkat)(fd, path, flag);
    }
    return PROXY(unlinkat)(fd, path, flag);
}

int HookRename(const char *oldName, const char *newName)
{
    if (!CheckPath(oldName) || !CheckPath(newName) || !InitNativeHook() || CHECKNATIVEFUNC(rename)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(rename)) {
        return NATIVE(rename)(oldName, newName);
    }
    return PROXY(rename)(oldName, newName);
}

int HookUtimes(const char *path, const struct timeval times[2])
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(utimes)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(utimes)) {
        return NATIVE(utimes)(path, times);
    }
    return PROXY(utimes)(path, times);
}

int HookFsync(int fd)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(fsync)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fsync)) {
        return NATIVE(fsync)(fd);
    }
    return PROXY(fsync)(fd);
}

void HookSync(void)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(sync)) {
        return;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(sync)) {
        NATIVE(sync)();
        return;
    }
    PROXY(sync)();
}

int HookSyncFs(int fd)
{
    if (!InitNativeHook() || CHECKNATIVEFUNC(syncfs)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(syncfs)) {
        return NATIVE(syncfs)(fd);
    }
    return PROXY(syncfs)(fd);
}

int HookRemove(const char *path)
{
    if (!CheckPath(path) || !InitNativeHook() || CHECKNATIVEFUNC(remove)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(remove)) {
        return NATIVE(remove)(path);
    }
    return PROXY(remove)(path);
}
}
}
