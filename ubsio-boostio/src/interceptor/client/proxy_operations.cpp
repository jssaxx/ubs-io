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

#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <mutex>
#include <climits>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <cstddef>
#include <cstring>
#include <stdarg.h>
#include <fcntl.h>
#include <linux/fs.h>
#include "bio_err.h"
#include "bio_def.h"
#include "interceptor_path.h"
#include "interceptor_context.h"
#include "interceptor_log.h"
#include "interceptor_net.h"
#include "message_op.h"
#include "proxy_operations.h"

using namespace ock::bio;

#define CONTEXT BioInterceptorContext::GetInstance()

namespace {
bool OpenNeedsMode(int flags)
{
    return (static_cast<uint32_t>(flags) & O_CREAT) || (static_cast<uint32_t>(flags) & O_TMPFILE);
}

void LogOpenInterceptSuccess(const char *api, const std::string &path, int fd, uint64_t inode, off_t size)
{
    CLOG_DEBUG("Interceptor open success, api:" << api << ", path:" << path << ", fd:" << fd <<
        ", inode:" << inode << ", size:" << size << ".");
}

void LogFdInterceptSuccess(const char *api, int fd, uint64_t inode, int ret)
{
    CLOG_DEBUG("Interceptor fd success, api:" << api << ", fd:" << fd << ", inode:" << inode <<
        ", ret:" << ret << ".");
}

void LogLseekInterceptSuccess(const char *api, int fd, uint64_t inode, off64_t offset, int whence, off64_t ret)
{
    CLOG_DEBUG("Interceptor lseek success, api:" << api << ", fd:" << fd << ", inode:" << inode <<
        ", offset:" << offset << ", whence:" << whence << ", ret:" << ret << ".");
}

void LogForkInterceptSuccess(pid_t ret)
{
    CLOG_DEBUG("Interceptor process success, api:fork, ret:" << ret << ".");
}

uint64_t GetRegularFileSizeForPrefetch(const struct stat &statBuf)
{
    if (!S_ISREG(statBuf.st_mode) || statBuf.st_size < 0) {
        return UINT64_MAX;
    }
    return static_cast<uint64_t>(statBuf.st_size);
}

void ReleaseRemoteReadWindow(const RemoteReadWindowCache &cache)
{
    if (!cache.active || cache.addrLen == 0) {
        return;
    }
    ProxyOperations::QueueReadBufferRelease(cache.addrOffset, cache.addrLen);
}

void DropReadCaches(const std::shared_ptr<OpenFile> &file)
{
    std::array<RemoteReadWindowCache, REMOTE_READ_WINDOW_CACHE_SLOTS> evicted{};
    file->DropReadAddrCache();
    file->InvalidateRemoteReadWindowCache(&evicted);
    for (const auto &cache : evicted) {
        ReleaseRemoteReadWindow(cache);
    }
}

}

void ProxyOperations::FillInterceptorOps(InterceptorProxyOperations &ops)
{
    ops.open = OpenProxy;
    ops.open64 = Open64Proxy;
    ops.openat = OpenAtProxy;
    ops.openat64 = OpenAt64Proxy;
    ops.creat = Creat;
    ops.creat64 = Creat64;
    ops.close = Close;
    ops.fsync = Fsync;
    ops.fdatasync = Fdatasync;
    ops.sync = Sync;
    ops.syncfs = SyncFs;
    ops.lseek = Lseek;
    ops.lseek64 = Lseek64;
    ops.read = Read;
    ops.pread = Pread;
    ops.pread64 = Pread64;
    ops.write = Write;
    ops.pwrite = Pwrite;
    ops.pwrite64 = Pwrite64;
    ops.fork = Fork;
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

pid_t ProxyOperations::Fork(void)
{
    auto prepareRet = InterceptorClientNetService::Instance().PrepareBeforeFork();
    if (prepareRet != BIO_OK) {
        CLOG_ERROR("Prepare interceptor before fork failed, ret:" << prepareRet << ", pid:" << getpid() << ".");
    }

    pid_t pid = CONTEXT.GetOperations()->fork();
    if (pid == 0) {
        auto ret = InterceptorClientNetService::Instance().PrepareAfterForkChild();
        if (ret != BIO_OK) {
            CLOG_ERROR("Prepare interceptor after fork failed, ret:" << ret << ", pid:" << getpid() << ".");
        }
    }
    if (pid >= 0) {
        LogForkInterceptSuccess(pid);
    }
    return pid;
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
    std::string link = "/proc/self/fd/" + std::to_string(fd);
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
    if (restoredPath.compare(0, pointLen, mountPoint, 0, pointLen) != 0) {
        return BIO_ERR;
    }
    if (restoredPath.size() == pointLen || restoredPath[pointLen] == '/') {
        return BIO_OK;
    }
    return BIO_ERR;
}

int ProxyOperations::OpenProxy(const char *path, int flags, va_list args)
{
    mode_t mode = 0;
    if (OpenNeedsMode(flags)) {
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

    auto ret = OpenInner("open", path, realFd, flags);
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

    auto ret = OpenInner("open", path, realFd, flags);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }
    return realFd;
}

int ProxyOperations::Open64Proxy(const char *path, int flags, va_list args)
{
    mode_t mode = 0;
    if (OpenNeedsMode(flags)) {
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

    auto ret = OpenInner("open64", path, realFd, flags);
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

    auto ret = OpenInner("open64", path, realFd, flags);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }
    return realFd;
}

int ProxyOperations::OpenAtProxy(int dirFd, const char *path, int flags, va_list args)
{
    mode_t mode = 0;
    if (OpenNeedsMode(flags)) {
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

    auto ret = OpenInner("openat", dirFd, path, realFd, flags);
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

    auto ret = OpenInner("openat", dirFd, path, realFd, flags);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }

    return realFd;
}

int ProxyOperations::OpenAt64Proxy(int dirFd, const char *path, int flags, va_list args)
{
    mode_t mode = 0;
    if (OpenNeedsMode(flags)) {
        mode = va_arg(args, mode_t);
        return OpenAt64Mode(dirFd, path, flags, mode);
    }
    return OpenAt64(dirFd, path, flags);
}

int ProxyOperations::OpenAt64(int dirFd, const char *path, int flags)
{
    int realFd = CONTEXT.GetOperations()->openat64(dirFd, path, flags);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = OpenInner("openat64", dirFd, path, realFd, flags);
    if (UNLIKELY(ret != BIO_OK)) {
        CONTEXT.GetOperations()->close(realFd);
        return -1;
    }
    return realFd;
}

int ProxyOperations::OpenAt64Mode(int dirFd, const char *path, int flags, mode_t mode)
{
    int realFd = CONTEXT.GetOperations()->openat64(dirFd, path, flags, mode);
    if (UNLIKELY(realFd < 0)) {
        return -1;
    }

    auto ret = OpenInner("openat64", dirFd, path, realFd, flags);
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

    auto ret = CreateInner("creat", path, realFd);
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

    auto ret = CreateInner("creat64", path, realFd);
    if (UNLIKELY(ret != BIO_OK)) {
        return -1;
    }

    return realFd;
}

int ProxyOperations::Close(int fd)
{
    CLOG_DEBUG("Close file fd:" << fd << ".");
    auto file = CONTEXT.files.At(fd);
    if (UNLIKELY(file == nullptr)) {
        CLOG_DEBUG("Fallback close to native, fd:" << fd << ".");
        return CONTEXT.GetOperations()->close(fd);
    }

    bool flushOk = FlushPendingWriteWindow(file, fd);
    if (UNLIKELY(!flushOk)) {
        errno = EIO;
    }
    uint64_t inode = file->GetInode();
    DropReadCaches(file);
    CONTEXT.files.Erase(fd);
    int ret = CONTEXT.GetOperations()->close(fd);
    if (UNLIKELY(!flushOk)) {
        errno = EIO;
        return -1;
    }
    if (ret == 0) {
        LogFdInterceptSuccess("close", fd, inode, ret);
    }
    return ret;
}

int ProxyOperations::Fsync(int fd)
{
    auto file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->fsync(fd);
    }
    if (UNLIKELY(!FlushPendingWriteWindow(file, fd))) {
        errno = EIO;
        return -1;
    }
    int ret = CONTEXT.GetOperations()->fsync(fd);
    if (ret == 0) {
        LogFdInterceptSuccess("fsync", fd, file->GetInode(), ret);
    }
    return ret;
}

void ProxyOperations::FlushAllPendingWriteWindows()
{
    auto files = CONTEXT.files.Snapshot();
    for (auto &item : files) {
        if (item.second == nullptr || !item.second->IsActive()) {
            continue;
        }
        if (UNLIKELY(!FlushPendingWriteWindow(item.second, item.first))) {
            CLOG_ERROR("Flush pending write window failed, fd:" << item.first <<
                ", inode:" << item.second->GetInode() << ".");
        }
    }
}

int ProxyOperations::Fdatasync(int fd)
{
    auto file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->fdatasync(fd);
    }
    if (UNLIKELY(!FlushPendingWriteWindow(file, fd))) {
        errno = EIO;
        return -1;
    }
    int ret = CONTEXT.GetOperations()->fdatasync(fd);
    if (ret == 0) {
        LogFdInterceptSuccess("fdatasync", fd, file->GetInode(), ret);
    }
    return ret;
}

void ProxyOperations::Sync(void)
{
    auto files = CONTEXT.files.Snapshot();
    for (auto &item : files) {
        if (item.second == nullptr || !item.second->IsActive()) {
            continue;
        }
        if (UNLIKELY(!FlushPendingWriteWindow(item.second, item.first))) {
            CLOG_ERROR("Flush pending write window before sync failed, fd:" << item.first <<
                ", inode:" << item.second->GetInode() << ".");
        }
    }
    CONTEXT.GetOperations()->sync();
    CLOG_DEBUG("Interceptor fd success, api:sync, fd:-1, inode:0, ret:0.");
}

int ProxyOperations::SyncFs(int fd)
{
    auto file = CONTEXT.files.At(fd);
    uint64_t inode = file == nullptr ? 0 : file->GetInode();
    if (file != nullptr && UNLIKELY(!FlushPendingWriteWindow(file, fd))) {
        errno = EIO;
        return -1;
    }
    int ret = CONTEXT.GetOperations()->syncfs(fd);
    if (ret == 0 && file != nullptr) {
        LogFdInterceptSuccess("syncfs", fd, inode, ret);
    }
    return ret;
}

off_t ProxyOperations::Lseek(int fd, off_t offset, int whence)
{
    auto file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->lseek(fd, offset, whence);
    }

    if (whence == SEEK_SET || whence == SEEK_CUR) {
        off64_t targetOffset = file->CalcSeekOffset(offset, whence);
        if (UNLIKELY(targetOffset < 0)) {
            errno = EINVAL;
            return -1;
        }
        auto ret = CONTEXT.GetOperations()->lseek(fd, static_cast<off_t>(targetOffset), SEEK_SET);
        if (UNLIKELY(ret < 0)) {
            return ret;
        }
        file->SetOffset(ret);
        LogLseekInterceptSuccess("lseek", fd, file->GetInode(), offset, whence, ret);
        return ret;
    }

    auto ret = CONTEXT.GetOperations()->lseek(fd, offset, whence);
    if (LIKELY(ret >= 0)) {
        file->SetOffset(ret);
        LogLseekInterceptSuccess("lseek", fd, file->GetInode(), offset, whence, ret);
    }
    return ret;
}

off64_t ProxyOperations::Lseek64(int fd, off64_t offset, int whence)
{
    auto file = CONTEXT.files.At(fd);
    if (file == nullptr) {
        return CONTEXT.GetOperations()->lseek64(fd, offset, whence);
    }

    if (whence == SEEK_SET || whence == SEEK_CUR) {
        off64_t targetOffset = file->CalcSeekOffset(offset, whence);
        if (UNLIKELY(targetOffset < 0)) {
            errno = EINVAL;
            return -1;
        }
        auto ret = CONTEXT.GetOperations()->lseek64(fd, targetOffset, SEEK_SET);
        if (UNLIKELY(ret < 0)) {
            return ret;
        }
        file->SetOffset(ret);
        LogLseekInterceptSuccess("lseek64", fd, file->GetInode(), offset, whence, ret);
        return ret;
    }

    auto ret = CONTEXT.GetOperations()->lseek64(fd, offset, whence);
    if (LIKELY(ret >= 0)) {
        file->SetOffset(ret);
        LogLseekInterceptSuccess("lseek64", fd, file->GetInode(), offset, whence, ret);
    }
    return ret;
}

int32_t ProxyOperations::OpenInner(const char *api, const char *path, int fd, int flags)
{
    CLOG_DEBUG("Open file::" << path << ", fd:" << fd << ".");
    std::string restoredPath;
    auto ret = FullPath(path, restoredPath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    if (CheckSelfPath(CONTEXT.mountPoint, restoredPath) == 0) {
        struct stat statBuf;
        ret = stat(restoredPath.c_str(), &statBuf);
        if (UNLIKELY(ret != 0)) {
            return BIO_ERR;
        }
        std::shared_ptr<OpenFile> op = nullptr;
        try {
            op = std::make_shared<OpenFile>(fd, statBuf.st_ino, GetRegularFileSizeForPrefetch(statBuf));
        } catch (const std::bad_alloc&) {
            return BIO_ERR;
        }
        if ((flags & O_APPEND) != 0) {
            op->SetOffset(statBuf.st_size);
        }
        CONTEXT.files.Add(fd, std::move(op));
        LogOpenInterceptSuccess(api, restoredPath, fd, statBuf.st_ino, statBuf.st_size);
    } else {
        CLOG_DEBUG("Fallback open to native, path:" << restoredPath << ", fd:" << fd << ".");
    }

    return BIO_OK;
}

int32_t ProxyOperations::OpenInner(const char *api, int dirFd, const char *path, int fd, int flags)
{
    CLOG_DEBUG("Open dir fd:" << dirFd << ", file:" << path << ", fd:" << fd << ".");
    std::string restoredPath;
    auto ret = FullPath(dirFd, path, restoredPath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    if (CheckSelfPath(CONTEXT.mountPoint, restoredPath) == 0) {
        struct stat statBuf;
        ret = stat(restoredPath.c_str(), &statBuf);
        if (UNLIKELY(ret != 0)) {
            return BIO_ERR;
        }
        std::shared_ptr<OpenFile> op = nullptr;
        try {
            op = std::make_shared<OpenFile>(fd, statBuf.st_ino, GetRegularFileSizeForPrefetch(statBuf));
        } catch (const std::bad_alloc&) {
            return BIO_ERR;
        }
        if ((flags & O_APPEND) != 0) {
            op->SetOffset(statBuf.st_size);
        }
        CONTEXT.files.Add(fd, std::move(op));
        LogOpenInterceptSuccess(api, restoredPath, fd, statBuf.st_ino, statBuf.st_size);
    } else {
        CLOG_DEBUG("Fallback openat to native, path:" << restoredPath << ", fd:" << fd << ".");
    }

    return BIO_OK;
}

int32_t ProxyOperations::CreateInner(const char *api, const char *path, int fd)
{
    CLOG_DEBUG("Create file:" << path << ", fd:" << fd << ".");
    std::string restoredPath;
    auto ret = FullPath(path, restoredPath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    if (CheckSelfPath(CONTEXT.mountPoint, restoredPath) == 0) {
        struct stat statBuf;
        ret = stat(restoredPath.c_str(), &statBuf);
        if (UNLIKELY(ret != 0)) {
            return BIO_ERR;
        }
        std::shared_ptr<OpenFile> op = nullptr;
        try {
            op = std::make_shared<OpenFile>(fd, statBuf.st_ino, GetRegularFileSizeForPrefetch(statBuf));
        } catch (const std::bad_alloc&) {
            return BIO_ERR;
        }
        CONTEXT.files.Add(fd, std::move(op));
        LogOpenInterceptSuccess(api, restoredPath, fd, statBuf.st_ino, statBuf.st_size);
    } else {
        CLOG_DEBUG("Fallback create to native, path:" << restoredPath << ", fd:" << fd << ".");
    }

    return BIO_OK;
}
