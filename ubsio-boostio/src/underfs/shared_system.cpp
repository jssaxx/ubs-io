/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <cerrno>
#include <sys/stat.h>
#include "bio_def.h"
#include "bio_log.h"
#include "shared_system.h"

namespace ock {
namespace bio {

BResult SharedSystem::Init() noexcept
{
    WriteLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (mInited) {
        return BIO_OK;
    }
    BResult ret = InitializeFileSystem();
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    mInited = true;
    LOG_INFO("Shared UnderFS initialize success.");
    return BIO_OK;
}

void SharedSystem::Stop() noexcept
{
    WriteLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    StopFileSystem();
}

BResult SharedSystem::Put(const char *key, const char *value, const size_t len) noexcept
{
    struct iovec buffer{const_cast<char *>(value), len};
    return PutV(key, &buffer, 1);
}

BResult SharedSystem::PutV(const char *key, const struct iovec *iov, size_t iovcnt) noexcept
{
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited)) {
        return BIO_NOT_READY;
    }
    size_t len = 0;
    BResult ret = ValidateWriteBuffers(iov, iovcnt, len);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    std::string hashHex;
    ret = BuildHash(key, hashHex);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    std::string temporaryPath;
    std::string finalPath;
    bool outOfSpace = false;
    ret = WriteTemporaryValue(hashHex, iov, iovcnt, nullptr, 0, temporaryPath, finalPath, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        if (!temporaryPath.empty()) {
            CleanupTemporaryFile(temporaryPath);
        }
        return ret;
    }
    ret = PublishFile(temporaryPath, finalPath, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        CleanupTemporaryFile(temporaryPath);
    }
    return ret;
}

BResult SharedSystem::LookupFile(const char *key, std::string &filePath, uint64_t &valueSize,
    FileReference &) noexcept
{
    valueSize = 0;
    return BuildFilePath(key, filePath);
}

BResult SharedSystem::Delete(const char *key) noexcept
{
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited)) {
        return BIO_NOT_READY;
    }
    std::string filePath;
    BResult ret = BuildFilePath(key, filePath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    return DeleteFile(filePath);
}

BResult SharedSystem::Stat(const char *key, ObjStat &objStat) noexcept
{
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited)) {
        return BIO_NOT_READY;
    }
    std::string filePath;
    BResult ret = BuildFilePath(key, filePath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    struct stat fileStat{};
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return HandleOpenError("Stat shared UnderFS value file", errno);
    }
    if (UNLIKELY(fileStat.st_size <= 0 || static_cast<uint64_t>(fileStat.st_size) > IO_MAX_LEN)) {
        LOG_ERROR("Invalid shared UnderFS value file size, size:" << fileStat.st_size << ".");
        return BIO_UFS_IOERR;
    }
    objStat.size = static_cast<uint64_t>(fileStat.st_size);
    objStat.time = fileStat.st_ctime;
    return BIO_OK;
}
}
}
