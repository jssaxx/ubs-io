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

#ifndef INTERCEPTOR_H
#define INTERCEPTOR_H
#include <unistd.h>
#include <cstdlib>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <stdio.h>
#include <dirent.h>

extern "C" {
struct InterceptorProxyOperations {
    int (*open)(const char *path, int flags, va_list args);

    int (*open64)(const char *path, int flags, va_list args);

    int (*openat)(int dirFd, const char *path, int flags, va_list args);

    int (*openat64)(int dirFd, const char *path, int flags, va_list args);

    int (*creat)(const char *path, mode_t mode);

    int (*creat64)(const char *path, mode_t mode);

    int (*close)(int fd);

    off_t (*lseek)(int fildes, off_t offset, int whence);

    off64_t (*lseek64)(int fildes, off64_t offset, int whence);

    ssize_t (*read)(int fildes, void *buf, size_t nbyte);

    ssize_t (*pread)(int fildes, void *buf, size_t nbyte, off_t offset);

    ssize_t (*pread64)(int fildes, void *buf, size_t nbyte, off64_t offset);

    ssize_t (*readv)(int fd, const struct iovec *vector, int count);

    ssize_t (*preadv)(int fd, const struct iovec *vector, int count, off_t offset);

    ssize_t (*preadv64)(int fd, const struct iovec *vector, int iovcnt, off64_t offset);

    ssize_t (*write)(int fildes, const void *buf, size_t nbyte);

    ssize_t (*pwrite)(int fildes, const void *buf, size_t nbyte, off_t offset);

    ssize_t (*pwrite64)(int fildes, const void *buf, size_t nbyte, off64_t offset);

    ssize_t (*writev)(int fd, const struct iovec *vector, int count);

    ssize_t (*pwritev)(int fd, const struct iovec *vector, int count, off_t offset);

    ssize_t (*pwritev64)(int fd, const struct iovec *vector, int count, off64_t offset);

    int (*fsync)(int fd);

    void (*sync)(void);

    int (*syncfs)(int fd);

    int (*truncate)(const char *path, off_t length);

    int (*truncate64)(const char *path, off_t length);

    int (*ftruncate)(int fildes, off_t length);

    int (*ftruncate64)(int fildes, off_t length);

    int (*dup)(int fd);

    int (*dup2)(int oldFd, int newFd);

    int (*dup3)(int oldFd, int newFd, int flags);

    int (*__xstat)(int ver, const char *path, struct stat *buf);

    int (*__xstat64)(int ver, const char *path, struct stat64 *buf);

    int (*__lxstat)(int ver, const char *path, struct stat *buf);

    int (*__lxstat64)(int ver, const char *path, struct stat64 *buf);

    int (*__fxstat)(int ver, int fildes, struct stat *buf);

    int (*__fxstat64)(int ver, int fildes, struct stat64 *buf);

    int (*__fxstatat)(int ver, int fd, const char *file, struct stat *buf, int flags);

    int (*__fxstatat64)(int ver, int fd, const char *file, struct stat64 *buf, int flags);

    int (*access)(const char *path, int mode);

    int (*unlink)(const char *path);

    int (*unlinkat)(int fd, const char *pathname, int flag);

    int (*remove)(const char *path);

    int (*rename)(const char *old_name, const char *new_name);

    int (*utimes)(const char *path, const struct timeval times[2]);

    FILE *(*fopen)(const char *file, const char *mode);

    FILE *(*fopen64)(const char *file, const char *mode);

    int (*fclose)(FILE *fp);

    int (*fseek)(FILE *fp, long offset, int whence);

    size_t (*fread)(void *ptr, size_t size, size_t count, FILE *fp);

    size_t (*fwrite)(const void *ptr, size_t size, size_t nitmes, FILE *fp);

    int (*fgetc)(FILE *fp);

    char *(*fgets)(char *s, int n, FILE *fp);

    int (*fflush)(FILE *fp);

    long int (*ftell)(FILE *fp);

    void (*rewind)(FILE *fp);
};

struct InterceptorNativeOperations {
    int (*open)(const char *path, int flags, ...);

    int (*open64)(const char *path, int flags, ...);

    int (*openat)(int dirFd, const char *path, int flags, ...);

    int (*openat64)(int dirFd, const char *path, int flags, ...);

    int (*creat)(const char *path, mode_t mode);

    int (*creat64)(const char *path, mode_t mode);

    int (*close)(int fd);

    off_t (*lseek)(int fildes, off_t offset, int whence);

    off64_t (*lseek64)(int fildes, off64_t offset, int whence);

    ssize_t (*read)(int fildes, void *buf, size_t nbyte);

    ssize_t (*pread)(int fildes, void *buf, size_t nbyte, off_t offset);

    ssize_t (*pread64)(int fildes, void *buf, size_t nbyte, off64_t offset);

    ssize_t (*readv)(int fd, const struct iovec *vector, int count);

    ssize_t (*preadv)(int fd, const struct iovec *vector, int count, off_t offset);

    ssize_t (*preadv64)(int fd, const struct iovec *vector, int iovcnt, off64_t offset);

    ssize_t (*write)(int fildes, const void *buf, size_t nbyte);

    ssize_t (*pwrite)(int fildes, const void *buf, size_t nbyte, off_t offset);

    ssize_t (*pwrite64)(int fildes, const void *buf, size_t nbyte, off64_t offset);

    ssize_t (*writev)(int fd, const struct iovec *vector, int count);

    ssize_t (*pwritev)(int fd, const struct iovec *vector, int count, off_t offset);

    ssize_t (*pwritev64)(int fd, const struct iovec *vector, int count, off64_t offset);

    int (*fsync)(int fd);

    void (*sync)(void);

    int (*syncfs)(int fd);

    int (*truncate)(const char *path, off_t length);

    int (*truncate64)(const char *path, off_t length);

    int (*ftruncate)(int fildes, off_t length);

    int (*ftruncate64)(int fildes, off_t length);

    int (*dup)(int fd);

    int (*dup2)(int oldFd, int newFd);

    int (*dup3)(int oldFd, int newFd, int flags);

    int (*__xstat)(int ver, const char *path, struct stat *buf);

    int (*__xstat64)(int ver, const char *path, struct stat64 *buf);

    int (*__lxstat)(int ver, const char *path, struct stat *buf);

    int (*__lxstat64)(int ver, const char *path, struct stat64 *buf);

    int (*__fxstat)(int ver, int fildes, struct stat *buf);

    int (*__fxstat64)(int ver, int fildes, struct stat64 *buf);

    int (*__fxstatat)(int ver, int fd, const char *file, struct stat *buf, int flags);

    int (*__fxstatat64)(int ver, int fd, const char *file, struct stat64 *buf, int flags);

    int (*access)(const char *path, int mode);

    int (*unlink)(const char *path);

    int (*unlinkat)(int fd, const char *pathname, int flag);

    int (*remove)(const char *path);

    int (*rename)(const char *old_name, const char *new_name);

    int (*utimes)(const char *path, const struct timeval times[2]);

    FILE *(*fopen)(const char *filename, const char *mode);

    FILE *(*fopen64)(const char *file, const char *mode);

    int (*fclose)(FILE *fp);

    int (*fseek)(FILE *fp, long offset, int whence);

    size_t (*fread)(void *ptr, size_t size, size_t count, FILE *fp);

    size_t (*fwrite)(const void *ptr, size_t size, size_t nitmes, FILE *fp);

    int (*fgetc)(FILE *fp);

    char *(*fgets)(char *s, int n, FILE *fp);

    int (*fflush)(FILE *fp);

    long int (*ftell)(FILE *fp);

    void (*rewind)(FILE *fp);
};

struct InterceptorNativeOperations *GetNativeOperations();

/**
 * @tips LD_PRELOAD=libock_interceptor.so 启动之后，构造函数执行顺序为
 * __attribute__((constructor)) InitHook()[内含两个静态变量nativeloader、proxyloader构造] --> 全局变量构造函数
 * 析构函数执行顺序为
 * 其他静态区变量析构函数 --> ~proxyloader() --> ~nativeloader()
 * 故使用libock_interceptor.so需要在(InitHook(), ~proxyloader()]内，保证proxy接口的可用性
 *
 * @brief 调用库需实现以下函数
 * @function struct InterceptorProxyOperations* RegisterHookFunction(void);
 * @tips 用于让interceptor获取proxy库下posix实现
 *
 * @function int InitializeProxyContext(void);
 * @tips 用于初始化proxy库工作环境
 *
 * @function void CleanProxyContext(void);
 * @tips 用于清理proxy库现场
 */

struct InterceptorProxyOperations *RegisterHookFunction(void);

struct InterceptorProxyOperations *RegisterHookFunctions(const struct InterceptorNativeOperations *nativeOperations);

int InitializeProxyContext(void);

void CleanProxyContext(void);
}
#endif // INTERCEPTOR_H
