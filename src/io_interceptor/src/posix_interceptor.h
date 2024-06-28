/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 */

#ifndef POXSIX_INTERCEPTOR_H
#define POXSIX_INTERCEPTOR_H

#include <unistd.h>
#include <cstdarg>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace ock {
namespace interceptor {
    int HookOpen(const char* path, int flags, va_list args);

    int HookOpen64(const char* path, int flags, va_list args);

    int HookOpenAt(int dirfd, const char* path, int flags, va_list args);

    int HookOpenAt64(int dirfd, const char* path, int flags, va_list args);

    int HookCreat(const char* path, mode_t mode);

    int HookCreat64(const char* path, mode_t mode);
    
    int HookClose(int fd);

    off_t HookLseek(int fd, off_t offset, int whence);

    off64_t HookLseek64(int fd, off64_t offset, int whence);

    ssize_t HookRead(int fd, void* buf, size_t nbytes);

    ssize_t HookPread(int fd, void* buf, size_t count, off_t offset);

    ssize_t HookPread64(int fd, void *buf, size_t nbyte, off64_t offset);

    ssize_t HookReadv(int fd, const struct iovec* vector, int count);

    ssize_t HookPreadv(int fd, const struct iovec* vector, int count, off_t offset);

    ssize_t HookPreadv64(int fd, const struct iovec* vector, int iovcnt, off64_t offset);

    ssize_t HookWrite(int fd, const void* buf, size_t nbytes);

    ssize_t HookPwrite64(int fd, const void* buf, size_t count, off64_t offset);

    ssize_t HookPwrite(int fd, const void* buf, size_t count, off_t offset);

    ssize_t HookWritev(int fd, const struct iovec* vector, int count);

    ssize_t HookPwritev(int fd, const struct iovec* vector, int count, off_t offset);

    ssize_t HookPwritev64(int fd, const struct iovec* vector, int count, off64_t offset);
    
    int HookTruncate(const char* path, off_t length);

    int HookTruncate64(const char* path, off_t length);

    int HookFtruncate(int fd, off_t length);

    int HookFtruncate64(int fd, off_t length);

    int HookDup(int fd);

    int HookDup2(int oldFildes, int newFildes);

    int HookDup3(int oldFildes, int newFildes, int flags);

    int HookStat(int ver, const char* path, struct stat* buf);

    int HookStat64(int ver, const char* path, struct stat64* buf);

    int HookLstat(int ver, const char* path, struct stat* buf);

    int HookLstat64(int ver, const char* path, struct stat64* buf);

    int HookFstat(int ver, int fd, struct stat* buf);

    int HookFstat64(int ver, int fd, struct stat64* buf);

    int HookFstatAt(int ver, int fd, const char* file, struct stat* buf, int flags);

    int HookFstatAt64(int ver, int fd, const char* file, struct stat64* buf, int flags);

    int HookAccess(const char* path, int mode);

    int HookUnlink(const char* path);

    int HookUnlinkat(int fd, const char* path, int flag);

    int HookRename(const char* oldName, const char* newName);

    int HookUtimes(const char *path, const struct timeval times[2]);

    int HookFsync(int fd);

    void HookSync(void);

    int HookSyncFs(int fd);

    int HookRemove(const char* path);
}
}

#endif // POXSIX_INTERCEPTOR_H
