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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>

#include "bio_log.h"
#include "bio_types.h"
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
constexpr char CRYPTO_LIBRARY[] = "libcrypto.so";
constexpr char SHA256_SYMBOL[] = "SHA256";
constexpr char HEX_DIGITS[] = "0123456789abcdef";

bool IsDirectIoAligned(const char *value, size_t len, uint64_t off)
{
    uintptr_t address = reinterpret_cast<uintptr_t>(value);
    return address % DIRECT_IO_ALIGN_SIZE == 0 && len % DIRECT_IO_ALIGN_SIZE == 0 &&
        off % DIRECT_IO_ALIGN_SIZE == 0;
}
}

BResult LocalSystem::Init()
{
    if (mInited) {
        return BIO_OK;
    }

    const auto &config = UnderFsConfig::Instance()->GetUnderFsConfig().localConfig;
    if (config.rootPath.empty()) {
        LOG_ERROR("Local underfs root path is empty.");
        return BIO_INVALID_PARAM;
    }

    char canonicalPath[PATH_MAX] = { 0 };
    if (realpath(config.rootPath.c_str(), canonicalPath) == nullptr) {
        LOG_ERROR("Resolve local underfs root path failed, errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    mRootPath = std::string(canonicalPath) + "/" + LAYOUT_DIRECTORY;
    BResult ret = EnsureDirectory(mRootPath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    mCryptoHandle = dlopen(CRYPTO_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    if (mCryptoHandle == nullptr) {
        LOG_ERROR("Load SHA-256 library failed, error:" << dlerror() << ".");
        return BIO_UFS_IOERR;
    }
    mSha256 = reinterpret_cast<Sha256Function>(dlsym(mCryptoHandle, SHA256_SYMBOL));
    if (mSha256 == nullptr) {
        LOG_ERROR("Load SHA-256 function failed, error:" << dlerror() << ".");
        dlclose(mCryptoHandle);
        mCryptoHandle = nullptr;
        return BIO_UFS_IOERR;
    }

    mInited = true;
    LOG_INFO("Local underfs initialize success, rootPath:" << mRootPath << ".");
    return BIO_OK;
}

void LocalSystem::Stop()
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

BResult LocalSystem::Put(const char *key, const char *value, const size_t len)
{
    if (UNLIKELY(len > IO_MAX_LEN)) {
        return BIO_INVALID_PARAM;
    }

    std::string hashHex;
    BResult ret = BuildHash(key, hashHex);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    std::string leafPath;
    std::string temporaryPath;
    int32_t fd = -1;
    ret = CreateValueTemporaryFile(hashHex, leafPath, temporaryPath, fd, IsDirectIoAligned(value, len, 0));
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    std::string finalPath = BuildFilePath(leafPath, hashHex);
    ret = WriteAll(fd, value, len);
    if (UNLIKELY(close(fd) != 0 && ret == BIO_OK)) {
        LOG_ERROR("Close local underfs temporary file failed, errno:" << errno << ".");
        ret = BIO_UFS_IOERR;
    }
    if (UNLIKELY(ret != BIO_OK)) {
        CleanupTemporaryFile(temporaryPath);
        return ret;
    }
    if (UNLIKELY(rename(temporaryPath.c_str(), finalPath.c_str()) != 0)) {
        LOG_ERROR("Replace local underfs value file failed, errno:" << errno << ".");
        CleanupTemporaryFile(temporaryPath);
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult LocalSystem::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    size_t realLen = 0;
    BResult ret = GetWithRealLen(key, value, len, off, realLen);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    if (UNLIKELY(realLen != len)) {
        LOG_ERROR("Local underfs read length is insufficient, readLength:" << realLen << ", expectedLength:" <<
            len << ".");
        return BIO_READ_EXCEED;
    }
    return BIO_OK;
}

BResult LocalSystem::GetWithRealLen(const char *key, char *value, const size_t len, const uint64_t off,
    size_t &realLen)
{
    realLen = 0;
    if (UNLIKELY(off > IO_MAX_LEN || len > IO_MAX_LEN - off)) {
        return BIO_INVALID_PARAM;
    }

    std::string filePath;
    BResult ret = BuildFilePath(key, filePath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    bool useDirectIo = IsDirectIoAligned(value, len, off);
    int32_t flags = O_RDONLY | O_CLOEXEC;
    if (useDirectIo) {
        flags |= O_DIRECT;
    }
    int32_t fd = open(filePath.c_str(), flags);
    if (UNLIKELY(fd < 0)) {
        return HandleOpenError("Open local underfs value file", errno);
    }

    ret = ReadAvailable(fd, value, len, off, realLen, useDirectIo);

    if (UNLIKELY(close(fd) != 0 && ret == BIO_OK)) {
        LOG_ERROR("Close local underfs value file failed, errno:" << errno << ".");
        ret = BIO_UFS_IOERR;
    }
    return ret;
}

BResult LocalSystem::Delete(const char *key)
{
    std::string filePath;
    BResult ret = BuildFilePath(key, filePath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    if (unlink(filePath.c_str()) == 0) {
        return BIO_OK;
    }
    if (errno == ENOENT) {
        return BIO_NOT_EXISTS;
    }
    LOG_ERROR("Delete local underfs value file failed, errno:" << errno << ".");
    return BIO_UFS_IOERR;
}

BResult LocalSystem::Exist(const char *key)
{
    std::string filePath;
    BResult ret = BuildFilePath(key, filePath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    if (access(filePath.c_str(), F_OK) == 0) {
        return BIO_OK;
    }
    return HandleOpenError("Check local underfs value file existence", errno);
}

BResult LocalSystem::Stat(const char *key, ObjStat &objStat)
{
    std::string filePath;
    BResult ret = BuildFilePath(key, filePath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    struct stat fileStat = {};
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return HandleOpenError("Stat local underfs value file", errno);
    }
    if (UNLIKELY(!ValidateFileSize(fileStat.st_size))) {
        LOG_ERROR("Invalid local underfs value file size, size:" << fileStat.st_size << ".");
        return BIO_UFS_IOERR;
    }
    objStat.size = static_cast<uint64_t>(fileStat.st_size);
    objStat.time = fileStat.st_ctime;
    return BIO_OK;
}

BResult LocalSystem::List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat)
{
    objStat.clear();
    LOG_WARN("Local underfs does not support prefix list, prefixLength:" << strlen(prefix) << ".");
    return BIO_ERR;
}

BResult LocalSystem::BuildHash(const char *key, std::string &hashHex) const
{
    unsigned char digest[SHA256_SIZE] = { 0 };
    size_t keyLen = strlen(key);
    if (UNLIKELY(mSha256 == nullptr || mSha256(reinterpret_cast<const unsigned char *>(key), keyLen, digest) ==
        nullptr)) {
        LOG_ERROR("Calculate key SHA-256 failed, keyLength:" << keyLen << ".");
        return BIO_UFS_IOERR;
    }
    hashHex.resize(SHA256_HEX_SIZE);
    for (size_t index = 0; index < SHA256_SIZE; ++index) {
        hashHex[index * 2] = HEX_DIGITS[digest[index] >> 4];
        hashHex[index * 2 + 1] = HEX_DIGITS[digest[index] & 0x0fU];
    }
    return BIO_OK;
}

BResult LocalSystem::BuildFilePath(const char *key, std::string &filePath) const
{
    std::string hashHex;
    BResult ret = BuildHash(key, hashHex);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    std::string leafPath = BuildLeafPath(hashHex);
    filePath = BuildFilePath(leafPath, hashHex);
    return BIO_OK;
}

std::string LocalSystem::BuildLeafPath(const std::string &hashHex) const
{
    return mRootPath + "/" + hashHex.substr(0, LEVEL_ONE_HEX_SIZE) + "/" +
        hashHex.substr(LEVEL_ONE_HEX_SIZE, LEVEL_TWO_HEX_SIZE);
}

std::string LocalSystem::BuildFilePath(const std::string &leafPath, const std::string &hashHex) const
{
    return leafPath + "/" + hashHex + FILE_SUFFIX;
}

std::string LocalSystem::BuildTemporaryPath(const std::string &leafPath, const std::string &hashHex) const
{
    return leafPath + "/.tmp." + hashHex + ".XXXXXX";
}

BResult LocalSystem::CreateValueTemporaryFile(const std::string &hashHex, std::string &leafPath,
    std::string &temporaryPath, int32_t &fd, bool useDirectIo) const
{
    leafPath = BuildLeafPath(hashHex);
    temporaryPath = BuildTemporaryPath(leafPath, hashHex);
    BResult ret = CreateTemporaryFile(temporaryPath, fd, true, useDirectIo);
    if (ret != BIO_NOT_EXISTS) {
        return ret;
    }

    ret = EnsureLeafDirectory(hashHex, leafPath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    temporaryPath = BuildTemporaryPath(leafPath, hashHex);
    return CreateTemporaryFile(temporaryPath, fd, false, useDirectIo);
}

BResult LocalSystem::CreateTemporaryFile(std::string &temporaryPath, int32_t &fd,
    bool allowMissingDirectory, bool useDirectIo) const
{
    uint32_t flags = static_cast<uint32_t>(O_CLOEXEC);
    if (useDirectIo) {
        flags |= static_cast<uint32_t>(O_DIRECT);
    }
    fd = mkostemp(&temporaryPath[0], static_cast<int32_t>(flags));
    if (UNLIKELY(fd < 0)) {
        int32_t errorCode = errno;
        if (allowMissingDirectory && errorCode == ENOENT) {
            return BIO_NOT_EXISTS;
        }
        LOG_ERROR("Create local underfs temporary file failed, errno:" << errorCode << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult LocalSystem::EnsureLeafDirectory(const std::string &hashHex, std::string &leafPath) const
{
    std::string levelOnePath = mRootPath + "/" + hashHex.substr(0, LEVEL_ONE_HEX_SIZE);
    BResult ret = EnsureDirectory(levelOnePath);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    leafPath = BuildLeafPath(hashHex);
    return EnsureDirectory(leafPath);
}

BResult LocalSystem::EnsureDirectory(const std::string &path) const
{
    if (mkdir(path.c_str(), DIRECTORY_MODE) == 0) {
        return BIO_OK;
    }
    if (errno != EEXIST) {
        LOG_ERROR("Create local underfs directory failed, errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    struct stat directoryStat = {};
    if (stat(path.c_str(), &directoryStat) != 0 || !S_ISDIR(directoryStat.st_mode)) {
        LOG_ERROR("Local underfs path is not a directory, errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult LocalSystem::WriteAll(int32_t fd, const char *value, size_t len) const
{
    size_t written = 0;
    while (written < len) {
        ssize_t writeLen = pwrite(fd, value + written, len - written, static_cast<off_t>(written));
        if (writeLen < 0 && errno == EINTR) {
            continue;
        }
        if (UNLIKELY(writeLen <= 0)) {
            LOG_ERROR("Write local underfs value failed, errno:" << errno << ", written:" << written <<
                ", total:" << len << ".");
            return BIO_UFS_IOERR;
        }
        written += static_cast<size_t>(writeLen);
    }
    return BIO_OK;
}

BResult LocalSystem::ReadAvailable(int32_t fd, char *value, size_t len, uint64_t off, size_t &realLen,
    bool useDirectIo) const
{
    realLen = 0;
    while (realLen < len) {
        ssize_t readLen = pread(fd, value + realLen, len - realLen, static_cast<off_t>(off + realLen));
        if (readLen < 0 && errno == EINTR) {
            continue;
        }
        if (UNLIKELY(readLen < 0)) {
            LOG_ERROR("Read local underfs value failed, errno:" << errno << ", readLength:" << realLen <<
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

bool LocalSystem::ValidateFileSize(off_t fileSize) const
{
    return fileSize > 0 && static_cast<uint64_t>(fileSize) <= IO_MAX_LEN;
}

BResult LocalSystem::HandleOpenError(const char *operation, int32_t errorCode) const
{
    if (errorCode == ENOENT) {
        return BIO_NOT_EXISTS;
    }
    LOG_ERROR(operation << " failed, errno:" << errorCode << ".");
    return BIO_UFS_IOERR;
}

void LocalSystem::CleanupTemporaryFile(const std::string &temporaryPath) const
{
    if (unlink(temporaryPath.c_str()) != 0 && errno != ENOENT) {
        LOG_WARN("Clean local underfs temporary file failed, errno:" << errno << ".");
    }
}
}
}
