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

#include <cerrno>
#include <climits>
#include <cstring>
#include <algorithm>
#include <array>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>

#include "bio_log.h"
#include "bio_types.h"
#include "bio_trace.h"
#include "underfs_config.h"

#include "local_system.h"

namespace ock {
namespace bio {
namespace {
constexpr mode_t DIRECTORY_MODE = S_IRWXU | S_IRGRP | S_IXGRP;
constexpr size_t SHA256_SIZE = 32;
constexpr size_t SHA256_HEX_SIZE = SHA256_SIZE * 2;
constexpr size_t LEVEL_ONE_HEX_SIZE = 2;
constexpr size_t LEVEL_TWO_HEX_SIZE = 3;
constexpr uintptr_t DIRECT_IO_ALIGN_SIZE = 512UL;
constexpr char LAYOUT_DIRECTORY[] = "v1";
constexpr char FILE_SUFFIX[] = ".kv";
constexpr char TEMPORARY_FILE_PREFIX[] = ".tmp.";
constexpr char TEMPORARY_FILE_TEMPLATE[] = ".XXXXXX";
constexpr char CRYPTO_LIBRARY[] = "libcrypto.so";
constexpr char SHA256_SYMBOL[] = "SHA256";
constexpr char HEX_DIGITS[] = "0123456789abcdef";
bool IsDirectIoAligned(const char *value, size_t len, uint64_t off) noexcept
{
    uintptr_t address = reinterpret_cast<uintptr_t>(value);
    return address % DIRECT_IO_ALIGN_SIZE == 0 && len % DIRECT_IO_ALIGN_SIZE == 0 &&
        off % DIRECT_IO_ALIGN_SIZE == 0;
}

}

LocalSystem::~LocalSystem() noexcept
{
    Stop();
}

BResult LocalSystem::InitializeFileSystem() noexcept
{
    const auto &underFs = UnderFsConfig::Instance()->GetUnderFsConfig();
    const auto &config = underFs.localConfig;
    if (UNLIKELY(config.rootPath.empty())) {
        LOG_ERROR("local underfs root path is empty.");
        return BIO_INVALID_PARAM;
    }

    char canonicalPath[PATH_MAX] = { 0 };
    if (UNLIKELY(realpath(config.rootPath.c_str(), canonicalPath) == nullptr)) {
        LOG_ERROR("Resolve local underfs root path failed, errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    bool outOfSpace = false;
    mRootPath = std::string(canonicalPath) + "/" + LAYOUT_DIRECTORY;
    BResult ret = EnsureDirectory(mRootPath, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    mCryptoHandle = dlopen(CRYPTO_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    if (UNLIKELY(mCryptoHandle == nullptr)) {
        LOG_ERROR("Load SHA-256 library failed, error:" << dlerror() << ".");
        return BIO_UFS_IOERR;
    }
    mSha256 = reinterpret_cast<Sha256Function>(dlsym(mCryptoHandle, SHA256_SYMBOL));
    if (UNLIKELY(mSha256 == nullptr)) {
        LOG_ERROR("Load SHA-256 function failed, error:" << dlerror() << ".");
        dlclose(mCryptoHandle);
        mCryptoHandle = nullptr;
        return BIO_UFS_IOERR;
    }

    return BIO_OK;
}

void LocalSystem::Stop() noexcept
{
    WriteLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    StopFileSystem();
}

void LocalSystem::StopFileSystem() noexcept
{
    mInited = false;
    mSha256 = nullptr;
    if (mCryptoHandle != nullptr) {
        int32_t ret = dlclose(mCryptoHandle);
        if (UNLIKELY(ret != 0)) {
            LOG_WARN("Close SHA-256 library failed, error:" << dlerror() << ".");
        }
        mCryptoHandle = nullptr;
    }
}

BResult LocalSystem::ValidateWriteBuffers(const struct iovec *iov, size_t iovcnt, size_t &len) noexcept
{
    len = 0;
    if (UNLIKELY(iov == nullptr || iovcnt == 0 || iovcnt > INT_MAX)) {
        return BIO_INVALID_PARAM;
    }
    for (size_t i = 0; i < iovcnt; ++i) {
        if (UNLIKELY((iov[i].iov_base == nullptr && iov[i].iov_len != 0) ||
            iov[i].iov_len > IO_MAX_LEN - len)) {
            return BIO_INVALID_PARAM;
        }
        len += iov[i].iov_len;
    }
    return BIO_OK;
}

BResult LocalSystem::Get(const char *key, char *value, const size_t len, const uint64_t off) noexcept
{
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited)) {
        return BIO_NOT_READY;
    }
    size_t realLen = 0;
    return ReadValue(key, value, len, off, realLen, true);
}

BResult LocalSystem::GetWithRealLen(const char *key, char *value, const size_t len, const uint64_t off,
    size_t &realLen) noexcept
{
    realLen = 0;
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited)) {
        return BIO_NOT_READY;
    }
    return ReadValue(key, value, len, off, realLen, false);
}

BResult LocalSystem::ReadValue(const char *key, char *value, size_t len, uint64_t off, size_t &realLen,
    bool requireFullLength) noexcept
{
    realLen = 0;
    if (UNLIKELY(off > IO_MAX_LEN || len > IO_MAX_LEN - off)) {
        return BIO_INVALID_PARAM;
    }

    std::string filePath;
    uint64_t valueSize = 0;
    FileReference reference;
    BResult ret = LookupFile(key, filePath, valueSize, reference);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    bool useDirectIo = IsDirectIoAligned(value, len, off);
    int32_t flags = O_RDONLY | O_CLOEXEC;
    if (useDirectIo) {
        flags |= O_DIRECT;
    }
    int32_t fd = open(filePath.c_str(), flags);
    const int32_t openError = fd < 0 ? errno : 0;
    if (UNLIKELY(fd < 0)) {
        ret = HandleOpenError("Open local underfs value file", openError);
        return ret;
    }

    BIO_TRACE_START(UFS_TRACE_GET_READ);
    ret = ReadAvailable(fd, value, len, off, realLen, useDirectIo);
    BIO_TRACE_END(UFS_TRACE_GET_READ, ret);
    const int32_t closeRet = close(fd);
    const int32_t closeError = closeRet != 0 ? errno : 0;
    if (UNLIKELY(closeRet != 0 && ret == BIO_OK)) {
        LOG_ERROR("Close local underfs value file failed, errno:" << closeError << ".");
        ret = BIO_UFS_IOERR;
    }
    if (UNLIKELY(ret == BIO_OK && requireFullLength && realLen != len)) {
        LOG_ERROR("local underfs read length is insufficient, readLength:" << realLen << ", expectedLength:" <<
            len << ".");
        return BIO_READ_EXCEED;
    }
    return ret;
}

BResult LocalSystem::Exist(const char *key) noexcept
{
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited)) {
        return BIO_NOT_READY;
    }
    std::string filePath;
    uint64_t valueSize = 0;
    FileReference reference;
    BResult ret = LookupFile(key, filePath, valueSize, reference);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    if (access(filePath.c_str(), F_OK) == 0) {
        return BIO_OK;
    }
    return HandleOpenError("Check local underfs value file existence", errno);
}

BResult LocalSystem::List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) noexcept
{
    objStat.clear();
    LOG_WARN("local underfs does not support prefix list, prefixLength:" << strlen(prefix) << ".");
    return BIO_ERR;
}

BResult LocalSystem::BuildHash(const char *key, std::string &hashHex) const noexcept
{
    if (UNLIKELY(key == nullptr)) {
        return BIO_INVALID_PARAM;
    }
    std::array<unsigned char, SHA256_SIZE> hash{};
    size_t keyLen = strlen(key);
    if (UNLIKELY(mSha256 == nullptr)) {
        LOG_ERROR("Calculate key SHA-256 failed, keyLength:" << keyLen << ".");
        return BIO_UFS_IOERR;
    }
    BIO_TRACE_START(UFS_TRACE_HASH_KEY);
    const auto *digest = mSha256(reinterpret_cast<const unsigned char *>(key), keyLen, hash.data());
    BIO_TRACE_END(UFS_TRACE_HASH_KEY, digest == nullptr ? BIO_UFS_IOERR : BIO_OK);
    if (UNLIKELY(digest == nullptr)) {
        LOG_ERROR("Calculate key SHA-256 failed, keyLength:" << keyLen << ".");
        return BIO_UFS_IOERR;
    }
    hashHex.resize(SHA256_HEX_SIZE);
    for (size_t index = 0; index < SHA256_SIZE; ++index) {
        hashHex[index * 2] = HEX_DIGITS[hash[index] >> 4];
        hashHex[index * 2 + 1] = HEX_DIGITS[hash[index] & 0x0fU];
    }
    return BIO_OK;
}

void LocalSystem::SetMetaEventCallback(UfsLocalMetaEventCallback, void *) noexcept
{
}

BResult LocalSystem::BuildFilePath(const char *key, std::string &filePath) const noexcept
{
    std::string hashHex;
    BResult ret = BuildHash(key, hashHex);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    filePath = BuildFilePath(BuildLeafPath(hashHex), hashHex);
    return BIO_OK;
}

std::string LocalSystem::BuildLeafPath(const std::string &hashHex) const noexcept
{
    return mRootPath + "/" + hashHex.substr(0, LEVEL_ONE_HEX_SIZE) + "/" +
        hashHex.substr(LEVEL_ONE_HEX_SIZE, LEVEL_TWO_HEX_SIZE);
}

std::string LocalSystem::BuildFilePath(const std::string &leafPath, const std::string &hashHex) const noexcept
{
    return leafPath + "/" + hashHex + FILE_SUFFIX;
}

std::string LocalSystem::BuildTemporaryPath(const std::string &leafPath, const std::string &hashHex) const noexcept
{
    return leafPath + "/" + TEMPORARY_FILE_PREFIX + hashHex + TEMPORARY_FILE_TEMPLATE;
}

BResult LocalSystem::CreateValueTemporaryFile(const std::string &hashHex, std::string &leafPath,
    std::string &temporaryPath, int32_t &fd, bool &outOfSpace, bool useDirectIo) const noexcept
{
    leafPath = BuildLeafPath(hashHex);
    temporaryPath = BuildTemporaryPath(leafPath, hashHex);
    BResult ret = CreateTemporaryFile(temporaryPath, fd, true, outOfSpace, useDirectIo);
    if (ret != BIO_NOT_EXISTS) {
        return ret;
    }

    ret = EnsureLeafDirectory(hashHex, leafPath, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    temporaryPath = BuildTemporaryPath(leafPath, hashHex);
    return CreateTemporaryFile(temporaryPath, fd, false, outOfSpace, useDirectIo);
}

BResult LocalSystem::CreateTemporaryFile(std::string &temporaryPath, int32_t &fd,
    bool allowMissingDirectory, bool &outOfSpace, bool useDirectIo) const noexcept
{
    BIO_TRACE_START(UFS_TRACE_PUT_CREAT);
    fd = mkostemp(&temporaryPath[0], O_CLOEXEC | (useDirectIo ? O_DIRECT : 0));
    const int32_t errorCode = fd < 0 ? errno : 0;
    BIO_TRACE_END(UFS_TRACE_PUT_CREAT, fd < 0 ? BIO_UFS_IOERR : BIO_OK);
    if (UNLIKELY(fd < 0)) {
        if (allowMissingDirectory && errorCode == ENOENT) {
            return BIO_NOT_EXISTS;
        }
        outOfSpace = errorCode == ENOSPC;
        LOG_ERROR("Create local underfs temporary file failed, errno:" << errorCode << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult LocalSystem::EnsureLeafDirectory(const std::string &hashHex, std::string &leafPath,
    bool &outOfSpace) const noexcept
{
    BResult ret = EnsureDirectory(mRootPath, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    std::string levelOnePath = mRootPath + "/" + hashHex.substr(0, LEVEL_ONE_HEX_SIZE);
    ret = EnsureDirectory(levelOnePath, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    leafPath = BuildLeafPath(hashHex);
    return EnsureDirectory(leafPath, outOfSpace);
}

BResult LocalSystem::EnsureDirectory(const std::string &path, bool &outOfSpace) const noexcept
{
    const int32_t ret = mkdir(path.c_str(), DIRECTORY_MODE);
    const int32_t errorCode = ret != 0 ? errno : 0;
    if (ret == 0) {
        return BIO_OK;
    }
    if (UNLIKELY(errorCode != EEXIST)) {
        outOfSpace = errorCode == ENOSPC;
        LOG_ERROR("Create local underfs directory failed, errno:" << errorCode << ".");
        return BIO_UFS_IOERR;
    }
    struct stat directoryStat = {};
    if (UNLIKELY(stat(path.c_str(), &directoryStat) != 0 || !S_ISDIR(directoryStat.st_mode))) {
        LOG_ERROR("local underfs path is not a directory, errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult LocalSystem::WriteTemporaryValue(const std::string &hashHex, const struct iovec *iov, size_t iovcnt,
    const char *metadata, size_t metadataSize, std::string &temporaryPath, std::string &finalPath,
    bool &outOfSpace) const noexcept
{
    outOfSpace = false;
    std::string leafPath;
    int32_t fd = -1;
    const bool useDirectIo = iovcnt == 1 && metadataSize == 0 &&
        IsDirectIoAligned(static_cast<const char *>(iov[0].iov_base), iov[0].iov_len, 0);
    BResult ret = CreateValueTemporaryFile(hashHex, leafPath, temporaryPath, fd, outOfSpace, useDirectIo);
    if (UNLIKELY(ret != BIO_OK)) {
        temporaryPath.clear();
        return ret;
    }
    finalPath = BuildFilePath(leafPath, hashHex);
    if (iovcnt == 1 && metadataSize == 0) {
        BIO_TRACE_START(UFS_TRACE_PUT_WRITE);
        ret = WriteAll(fd, static_cast<const char *>(iov[0].iov_base), iov[0].iov_len, 0, outOfSpace);
        BIO_TRACE_END(UFS_TRACE_PUT_WRITE, ret);
    } else {
        BIO_TRACE_START(UFS_TRACE_PUT_WRITEV);
        ret = WriteAllV(fd, iov, iovcnt, metadata, metadataSize, outOfSpace);
        BIO_TRACE_END(UFS_TRACE_PUT_WRITEV, ret);
    }
    const int32_t closeRet = close(fd);
    const int32_t closeError = closeRet != 0 ? errno : 0;
    if (UNLIKELY(closeRet != 0 && ret == BIO_OK)) {
        outOfSpace = closeError == ENOSPC;
        LOG_ERROR("Close local underfs temporary file failed, errno:" << closeError << ".");
        ret = BIO_UFS_IOERR;
    }
    return ret;
}

BResult LocalSystem::PublishFile(const std::string &temporaryPath, const std::string &finalPath,
    bool &outOfSpace) const noexcept
{
    BIO_TRACE_START(UFS_TRACE_PUT_RENAME);
    const int32_t ret = rename(temporaryPath.c_str(), finalPath.c_str());
    const int32_t errorCode = ret != 0 ? errno : 0;
    BIO_TRACE_END(UFS_TRACE_PUT_RENAME, ret != 0 ? BIO_UFS_IOERR : BIO_OK);
    if (UNLIKELY(ret != 0)) {
        outOfSpace = errorCode == ENOSPC;
        LOG_ERROR("Replace local underfs value file failed, errno:" << errorCode << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult LocalSystem::DeleteFile(const std::string &filePath) const noexcept
{
    BIO_TRACE_START(UFS_TRACE_UNLINK);
    const int32_t ret = unlink(filePath.c_str());
    const int32_t errorCode = ret != 0 ? errno : 0;
    BIO_TRACE_END(UFS_TRACE_UNLINK, ret != 0 ? BIO_UFS_IOERR : BIO_OK);
    if (ret == 0) {
        return BIO_OK;
    }
    return HandleOpenError("Delete local underfs value file", errorCode);
}

BResult LocalSystem::WriteAll(int32_t fd, const char *value, size_t len, uint64_t offset,
    bool &outOfSpace) const noexcept
{
    size_t written = 0;
    while (written < len) {
        ssize_t writeLen = pwrite(fd, value + written, len - written, static_cast<off_t>(offset + written));
        if (UNLIKELY(writeLen < 0 && errno == EINTR)) {
            continue;
        }
        if (UNLIKELY(writeLen <= 0)) {
            const int32_t errorCode = writeLen < 0 ? errno : EIO;
            outOfSpace = errorCode == ENOSPC;
            LOG_ERROR("Write local underfs value failed, errno:" << errorCode << ", written:" << written <<
                ", total:" << len << ".");
            return BIO_UFS_IOERR;
        }
        written += static_cast<size_t>(writeLen);
    }
    return BIO_OK;
}

BResult LocalSystem::WriteAllV(int32_t fd, const struct iovec *iov, size_t iovcnt,
    const char *metadata, size_t metadataSize, bool &outOfSpace) const noexcept
{
    const size_t suffixCount = metadataSize == 0 ? 0 : 1;
    const size_t count = iovcnt + suffixCount;
    std::unique_ptr<struct iovec[]> pending(new (std::nothrow) struct iovec[count]);
    if (UNLIKELY(pending == nullptr)) {
        return BIO_ALLOC_FAIL;
    }
    std::copy_n(iov, iovcnt, pending.get());
    if (suffixCount != 0) {
        pending[iovcnt] = {const_cast<char *>(metadata), metadataSize};
    }
    size_t first = 0;
    off_t offset = 0;
    while (first < count) {
        if (pending[first].iov_len == 0) {
            ++first;
            continue;
        }
        const int vectors = static_cast<int>(std::min(count - first, static_cast<size_t>(IOV_MAX)));
        ssize_t result = pwritev(fd, pending.get() + first, vectors, offset);
        if (UNLIKELY(result < 0 && errno == EINTR)) {
            continue;
        }
        if (UNLIKELY(result <= 0)) {
            const int32_t errorCode = result < 0 ? errno : EIO;
            outOfSpace = errorCode == ENOSPC;
            LOG_ERROR("Write local underfs vectors failed, errno:" << errorCode << ", offset:" << offset << ".");
            return BIO_UFS_IOERR;
        }
        offset += result;
        size_t consumed = static_cast<size_t>(result);
        while (first < count && consumed >= pending[first].iov_len) {
            consumed -= pending[first].iov_len;
            ++first;
        }
        if (first < count && consumed != 0) {
            pending[first].iov_base = static_cast<char *>(pending[first].iov_base) + consumed;
            pending[first].iov_len -= consumed;
        }
    }
    return BIO_OK;
}

BResult LocalSystem::ReadAvailable(int32_t fd, char *value, size_t len, uint64_t off, size_t &realLen,
    bool useDirectIo) const noexcept
{
    realLen = 0;
    while (realLen < len) {
        ssize_t readLen = pread(fd, value + realLen, len - realLen, static_cast<off_t>(off + realLen));
        const int32_t errorCode = readLen < 0 ? errno : 0;
        if (UNLIKELY(readLen < 0 && errorCode == EINTR)) {
            continue;
        }
        if (UNLIKELY(readLen < 0)) {
            LOG_ERROR("Read local underfs value failed, errno:" << errorCode << ", readLength:" << realLen <<
                ", total:" << len << ".");
            return BIO_UFS_IOERR;
        }
        if (readLen == 0) {
            return realLen == 0 ? BIO_READ_EXCEED : BIO_OK;
        }
        realLen += static_cast<size_t>(readLen);
        if (useDirectIo && static_cast<size_t>(readLen) % DIRECT_IO_ALIGN_SIZE != 0) {
            return BIO_OK;
        }
    }
    return BIO_OK;
}

BResult LocalSystem::HandleOpenError(const char *operation, int32_t errorCode) const noexcept
{
    if (errorCode == ENOENT) {
        return BIO_NOT_EXISTS;
    }
    LOG_ERROR(operation << " failed, errno:" << errorCode << ".");
    return BIO_UFS_IOERR;
}

BResult LocalSystem::CleanupTemporaryFile(const std::string &temporaryPath) const noexcept
{
    BIO_TRACE_START(UFS_TRACE_UNLINK);
    const int32_t ret = unlink(temporaryPath.c_str());
    const int32_t errorCode = ret != 0 ? errno : 0;
    BIO_TRACE_END(UFS_TRACE_UNLINK, ret != 0 ? BIO_UFS_IOERR : BIO_OK);
    if (UNLIKELY(ret != 0 && errorCode != ENOENT)) {
        LOG_WARN("Clean local underfs temporary file failed, errno:" << errorCode << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult LocalSystem::GetStorageMetrics(UfsStorageMetrics &) noexcept
{
    return BIO_NOT_READY;
}

BResult LocalSystem::ScanKeys(UfsLocalKeyInfo **, uint64_t *) noexcept
{
    return BIO_NOT_READY;
}

}
}
