/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <mutex>
#include <climits>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstddef>
#include <cstring>
#include <stdarg.h>
#include <linux/fs.h>
#include "bio_err.h"
#include "bio_def.h"
#include "interceptor_path.h"
#include "interceptor_context.h"
#include "interceptor_log.h"
#include "proxy_operations.h"

using namespace ock::bio;

#define CONTEXT BioInterceptorContext::GetInstance()

void ProxyOperations::FillInterceptorOps(InterceptorProxyOperations &ops)
{
    ops.open = OpenProxy;
    ops.open64 = Open64Proxy;
    ops.openat = OpenAtProxy;
    ops.creat = Creat;
    ops.creat64 = Creat64;
    ops.close = Close;
    ops.read = Read;
    ops.readv = Readv;
    ops.pread = Pread;
    ops.pread64 = Pread64;
    ops.preadv64 = preadv64;
    ops.write = Write;
}

struct InterceptorProxyOperations *ProxyOperations::GetOperations() noexcept
{
    static volatile bool initialized = false;
    static std::mutex mutex;
    static struct InterceptorProxyOperations operations = { nullptr };

    std::unique_lock<std::mutex> lock(mutex);
    if (!initialized) {
        FillInterceptorOps(operations);
        initialized = true;
    }
    return &operations;
}

int32_t ProxyOperations::FullPath(const char *nativePath, std::string &realPath)
{
    std::string filePath;
    if (nativePath[0] != '/') {
        filePath = AddPrefix(GetCWD(), nativePath);
    } else {
        filePath = nativePath;
    }

    realPath = filePath;
    return BIO_OK;
}

bool ProxyOperations::MatchPath(int fd, std::string &path)
{
    static std::string link("/proc/self/fd/");
    link.append(std::to_string(fd));
    char pathStr[PATH_MAX] = {0};
    ssize_t len = readlink(link.c_str(), pathStr, sizeof(pathStr) - 1);
    if (len != -1) {
        pathStr[len] = '\0';
        path = pathStr;
        return true;
    }
    return false;
}

int32_t ProxyOperations::FullPath(int dirFd, const char *nativePath, std::string &realPath)
{
    std::string filePath;
    if (nativePath[0] != '/') {
        if (dirFd == AT_FDCWD) {
            filePath = AddPrefix(GetCWD(), nativePath);
        } else {
            std::string prefix;
            if (!MatchPath(dirFd, prefix)) {
                return BIO_ERR;
            }
            filePath = AddPrefix(prefix, nativePath);
        }
    } else {
        filePath = nativePath;
    }

    realPath = filePath;
    return BIO_OK;
}

int ProxyOperations::CheckSelfPath(const std::string &mountPoint, const std::string &restoredPath)
{
    size_t pointLen = mountPoint.size();
    return restoredPath.compare(0, pointLen, mountPoint, 0, pointLen);
}

int ProxyOperations::OpenProxy(const char *path, int flags, va_list args)
{
    int pError = 0;

    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE)) {
        mode = va_arg(args, mode_t);
        return OpenMode(path, flags, mode);
    }
    return Open(path, flags);
}

int ProxyOperations::Open(const char *path, int flags)
{
    int realFd = CONTEXT.GetOperations()->open(path, flags);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = OpenInner(path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }

    return realFd;
}

int ProxyOperations::OpenMode(const char *path, int flags, mode_t mode)
{
    int realFd = CONTEXT.GetOperations()->open(path, flags, mode);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = OpenInner(path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }
    return realFd;
}

int ProxyOperations::Open64Proxy(const char *path, int flags, va_list args)
{
    int pError = 0;

    mode_t mode = 0;
    if (flags & O_CREAT) {
        mode = va_arg(args, mode_t);
        return Open64Mode(path, flags, mode);
    }
    return Open64(path, flags);
}

int ProxyOperations::Open64(const char *path, int flags)
{
    int realFd = CONTEXT.GetOperations()->open64(path, flags);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = OpenInner(path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }

    return realFd;
}

int ProxyOperations::Open64Mode(const char *path, int flags, mode_t mode)
{
    int realFd = CONTEXT.GetOperations()->open64(path, flags, mode);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = OpenInner(path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }
    return realFd;
}

int ProxyOperations::OpenAtProxy(int dirFd, const char *path, int flags, va_list args)
{
    int pError = 0;

    mode_t mode = 0;
    if (flags & O_CREAT) {
        mode = va_arg(args, mode_t);
        return OpenAtMode(dirFd, path, flags, mode);
    }
    return OpenAt(dirFd, path, flags);
}

int ProxyOperations::OpenAt(int dirFd, const char *path, int flags)
{
    int realFd = CONTEXT.GetOperations()->openat(dirFd, path, flags);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = OpenInner(dirFd, path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }
    return realFd;
}

int ProxyOperations::OpenAtMode(int dirFd, const char *path, int flags, mode_t mode)
{
    int realFd = CONTEXT.GetOperations()->openat(dirFd, path, flags, mode);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = OpenInner(dirFd, path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }

    return realFd;
}

int ProxyOperations::Creat(const char *path, mode_t mode)
{
    int realFd = CONTEXT.GetOperations()->creat(path, mode);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = CreateInner(path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        return -1;
    }

    return realFd;
}

int ProxyOperations::Creat64(const char *path, mode_t mode)
{
    int realFd = CONTEXT.GetOperations()->creat64(path, mode);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = CreateInner(path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        return -1;
    }

    return realFd;
}

int ProxyOperations::Close(int fd)
{
    CLOG_DEBUG("Close file fd:" << fd << ".");
    auto &file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        return CONTEXT.GetOperations()->close(fd);
    }

    CONTEXT.files.Erase(fd);
    return CONTEXT.GetOperations()->close(fd);
}

int32_t ProxyOperations::OpenInner(const char *path, int fd)
{
    CLOG_DEBUG("Open file::" << path << ", fd:" << fd << ".");
    std::string restoredPath;
    auto ret = FullPath(path, restoredPath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    if (CheckSelfPath(CONTEXT.mountPoint, restoredPath) == 0) {
        std::string &&suffix = GetPathNoPrefix(path, CONTEXT.mountPoint);
        struct stat statBuf;
        ret = stat(restoredPath.c_str(), &statBuf);
        if (UNLIKELY(ret != 0)) {
            return BIO_ERR;
        }
        CONTEXT.files.Add(fd, std::make_shared<OpenFile>(fd, statBuf.st_ino));
    }

    return BIO_OK;
}

int32_t ProxyOperations::OpenInner(int dirFd, const char *path, int fd)
{
    CLOG_DEBUG("Open dir fd:" << dirFd << ", file:" << path << ", fd:" << fd << ".");
    std::string restoredPath;
    auto ret = FullPath(dirFd, path, restoredPath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    if (CheckSelfPath(CONTEXT.mountPoint, restoredPath) == 0) {
        std::string &&suffix = GetPathNoPrefix(path, CONTEXT.mountPoint);
        struct stat statBuf;
        ret = stat(restoredPath.c_str(), &statBuf);
        if (UNLIKELY(ret != 0)) {
            return BIO_ERR;
        }
        CONTEXT.files.Add(fd, std::make_shared<OpenFile>(fd, statBuf.st_ino));
    }

    return BIO_OK;
}

int32_t ProxyOperations::CreateInner(const char *path, int fd)
{
    CLOG_DEBUG("Create file:" << path << ", fd:" << fd << ".");
    std::string restoredPath;
    auto ret = FullPath(path, restoredPath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    if (CheckSelfPath(CONTEXT.mountPoint, restoredPath) == 0) {
        std::string &&suffix = GetPathNoPrefix(path, CONTEXT.mountPoint);
        struct stat statBuf;
        auto ret = stat(restoredPath.c_str(), &statBuf);
        if (UNLIKELY(ret != 0)) {
            return BIO_ERR;
        }
        CONTEXT.files.Add(fd, std::make_shared<OpenFile>(fd, statBuf.st_ino));
    }

    return BIO_OK;
}
