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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <chrono>
#include <new>
#include <tuple>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/statvfs.h>

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
constexpr char LAYOUT_DIRECTORY[] = "v1";
constexpr char FILE_SUFFIX[] = ".kv";
constexpr char CORRUPT_FILE_PREFIX[] = ".corrupt.";
constexpr char LEGACY_FILE_MAGIC[] = "UBSIOKV1";
constexpr char TEMPORARY_FILE_PREFIX[] = ".tmp.";
constexpr char TEMPORARY_FILE_TEMPLATE[] = ".XXXXXX";
constexpr char CRYPTO_LIBRARY[] = "libcrypto.so";
constexpr char SHA256_SYMBOL[] = "SHA256";
constexpr char HEX_DIGITS[] = "0123456789abcdef";
constexpr uint32_t FILESYSTEM_CAPACITY_PERCENT = 90;
constexpr uint32_t EVICTION_HIGH_WATER_LEVEL = 90;
constexpr size_t EVICTION_KEY_COUNT_DIVISOR = 10;
constexpr size_t MAX_BACKGROUND_EVICTION_BATCH_FILES = 128;
constexpr size_t MAX_WRITE_EVICTION_BATCH_FILES = 1024;
constexpr uint32_t MAX_WRITE_ATTEMPTS = 2;

int32_t WriteAll(int32_t fd, const char *value, size_t len, off_t offset) noexcept
{
    size_t written = 0;
    while (written < len) {
        const ssize_t n = pwrite(fd, value + written, len - written, offset + static_cast<off_t>(written));
        if (UNLIKELY(n < 0 && errno == EINTR)) {
            continue;
        }
        if (UNLIKELY(n <= 0)) {
            return n < 0 ? errno : EIO;
        }
        written += static_cast<size_t>(n);
    }
    return 0;
}

int32_t SyncFileData(int32_t fd) noexcept
{
    int32_t ret;
    do {
        ret = fdatasync(fd);
    } while (ret != 0 && errno == EINTR);
    return ret == 0 ? 0 : errno;
}

int32_t WriteAlignedFile(int32_t fd, const char *buffer, size_t fileSize, size_t alignedSize) noexcept
{
    const int32_t flags = fcntl(fd, F_GETFL);
    if (UNLIKELY(flags < 0)) {
        return errno;
    }
    const bool direct = fcntl(fd, F_SETFL, flags | O_DIRECT) == 0;
    int32_t ret = direct ? WriteAll(fd, buffer, alignedSize, 0) : errno;
    if (ret == EINVAL || ret == EOPNOTSUPP) {
        if (direct && fcntl(fd, F_SETFL, flags) != 0) {
            return errno;
        }
        ret = WriteAll(fd, buffer, fileSize, 0);
    }
    if (ret == 0 && alignedSize != fileSize && ftruncate(fd, static_cast<off_t>(fileSize)) != 0) {
        ret = errno;
    }
    return ret;
}

std::string SlotDirectory(const std::string &root, uint32_t slot)
{
    return root + "/slot_" + std::to_string(slot);
}

class ScopedFd {
public:
    explicit ScopedFd(int fd) noexcept : value(fd) {}
    ~ScopedFd() noexcept
    {
        if (value >= 0) {
            close(value);
        }
    }
    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;
    int value;
};

class ScopedDirectory {
public:
    explicit ScopedDirectory(int fd) noexcept : value(fd < 0 ? nullptr : fdopendir(fd))
    {
        if (UNLIKELY(fd >= 0 && value == nullptr)) {
            close(fd);
        }
    }
    ~ScopedDirectory() noexcept
    {
        if (value != nullptr) {
            closedir(value);
        }
    }
    ScopedDirectory(const ScopedDirectory &) = delete;
    ScopedDirectory &operator=(const ScopedDirectory &) = delete;
    DIR *value;
};

bool IsHashPart(const std::string &name, size_t length) noexcept
{
    return name.size() == length && name.find_first_not_of(HEX_DIGITS) == std::string::npos;
}

bool IsRecoveryTemporaryFile(const std::string &name, const std::string &firstName,
    const std::string &secondName) noexcept
{
    constexpr size_t prefixSize = sizeof(TEMPORARY_FILE_PREFIX) - 1;
    constexpr size_t suffixSize = sizeof(TEMPORARY_FILE_TEMPLATE) - 1;
    if (name.size() != prefixSize + SHA256_HEX_SIZE + suffixSize ||
        name.compare(0, prefixSize, TEMPORARY_FILE_PREFIX) != 0 || name[prefixSize + SHA256_HEX_SIZE] != '.') {
        return false;
    }
    const std::string hash = name.substr(prefixSize, SHA256_HEX_SIZE);
    if (!IsHashPart(hash, SHA256_HEX_SIZE) || hash.compare(0, LEVEL_ONE_HEX_SIZE, firstName) != 0 ||
        hash.compare(LEVEL_ONE_HEX_SIZE, LEVEL_TWO_HEX_SIZE, secondName) != 0) {
        return false;
    }
    return std::all_of(name.begin() + prefixSize + SHA256_HEX_SIZE + 1, name.end(), [](char character) noexcept {
        return (character >= '0' && character <= '9') || (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z');
    });
}

uint64_t PercentBytes(uint64_t capacity, uint32_t percent) noexcept
{
    return capacity / 100 * percent + capacity % 100 * percent / 100;
}

bool ExceedsLimit(uint64_t used, uint64_t additional, uint64_t limit) noexcept
{
    return used > limit || additional > limit - used;
}

bool IsDirectIoAligned(const char *value, size_t len, uint64_t off) noexcept
{
    uintptr_t address = reinterpret_cast<uintptr_t>(value);
    return address % LOCAL_FILE_IO_ALIGNMENT == 0 && len % LOCAL_FILE_IO_ALIGNMENT == 0 &&
        off % LOCAL_FILE_IO_ALIGNMENT == 0;
}

BResult AddRecoveryFile(FileIndex &index, std::string &key, const std::string &firstName,
    const std::string &secondName, uint64_t bytes) noexcept
{
    FileRecord record{firstName + "/" + secondName, bytes};
    const auto inserted = index.emplace(std::piecewise_construct, std::forward_as_tuple(std::move(key)),
        std::forward_as_tuple(std::move(record)));
    return inserted.second ? BIO_OK : BIO_UFS_IOERR;
}

BResult QuarantineRecoveryFile(int directory, const std::string &name) noexcept
{
    const std::string prefix = CORRUPT_FILE_PREFIX + name;
    std::string target = prefix;
    struct stat existing{};
    // Recovery holds the exclusive slot lock. Keep earlier quarantined copies of the same key.
    for (uint64_t suffix = 1; fstatat(directory, target.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0; ++suffix) {
        target = prefix + "." + std::to_string(suffix);
    }
    if (UNLIKELY(errno != ENOENT || renameat(directory, name.c_str(), directory, target.c_str()) != 0)) {
        LOG_ERROR("Quarantine corrupt local underfs file failed, file:" << name << ", errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    LOG_WARN("Quarantined corrupt local underfs file:" << name << ", target:" << target << ".");
    return BIO_OK;
}

class ScopedFileDelete {
public:
    ScopedFileDelete(FileEvictionPolicy &policy, uint64_t &usedBytes,
        const EvictionCandidate *files, size_t count = 1) noexcept
        : mPolicy(policy), mUsedBytes(usedBytes), mFiles(files), mCount(count) {}
    ScopedFileDelete(const ScopedFileDelete &) = delete;
    ScopedFileDelete &operator=(const ScopedFileDelete &) = delete;
    ~ScopedFileDelete() noexcept
    {
        // Restore unfinished candidates together at the cold end.
        size_t failures = 0;
        BResult result = BIO_OK;
        for (size_t i = mCount; i > mCompleted; --i) {
            BResult ret = mPolicy.ColdInsert(mFiles[i - 1]);
            if (UNLIKELY(ret != BIO_OK)) {
                ++failures;
                result = ret;
            }
        }
        if (UNLIKELY(failures != 0)) {
            LOG_ERROR("Restore local UnderFS eviction metadata failed, files:" << failures <<
                ", ret:" << result << ".");
        }
    }
    void Commit() noexcept
    {
        mUsedBytes -= mFiles[mCompleted].record.sizeBytes;
        ++mCompleted;
    }
private:
    FileEvictionPolicy &mPolicy;
    uint64_t &mUsedBytes;
    const EvictionCandidate *mFiles;
    size_t mCount;
    size_t mCompleted{0};
};
}

LocalSystem::~LocalSystem() noexcept
{
    Stop();
}

BResult LocalSystem::Init() noexcept
{
    WriteLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (mInited) {
        return BIO_OK;
    }

    BResult ret = InitializeFileSystem();
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    ret = InitializeEviction();
    if (UNLIKELY(ret != BIO_OK)) {
        StopLocalSystem();
        return ret;
    }
    mInited = true;
    LOG_INFO("Local underfs initialize success, rootPath:" << mRootPath << ".");
    return BIO_OK;
}

BResult LocalSystem::ResolvePath(const char *key, uint32_t slot, char *path, size_t capacity) noexcept
{
    ReadLocker<ReadWriteLock> guard(&mLifecycleLock);
    const auto &config = UnderFsConfig::Instance()->GetUnderFsConfig().localConfig;
    if (!mInited || path == nullptr || slot >= config.deviceCount) {
        return BIO_INVALID_PARAM;
    }
    std::string hash;
    auto ret = BuildHash(key, hash);
    if (ret != BIO_OK) {
        return ret;
    }
    const auto root = SlotDirectory(mStorageRoot, slot);
    const auto file = BuildFilePath(BuildLeafPath(root, hash), hash);
    if (file.size() >= capacity) {
        return BIO_INVALID_PARAM;
    }
    std::memcpy(path, file.c_str(), file.size() + 1);
    return BIO_OK;
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
    mStorageRoot = canonicalPath;
    std::string dataRoot = mStorageRoot;
    const bool local = underFs.underFsType == BIO_UNDERFS_LOCAL;
    if (local) {
        if (UNLIKELY(config.deviceCount == 0 || config.deviceCount > DEVICE_SIZE ||
            config.slotIndex >= config.deviceCount)) {
            return BIO_INVALID_PARAM;
        }
        dataRoot = SlotDirectory(mStorageRoot, config.slotIndex);
    } else {
        dataRoot += "/";
        dataRoot += LAYOUT_DIRECTORY;
    }

    bool outOfSpace = false;
    BResult ret = EnsureDirectory(dataRoot, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    if (local) {
        struct stat slotStat{};
        if (UNLIKELY(lstat(dataRoot.c_str(), &slotStat) != 0 || !S_ISDIR(slotStat.st_mode))) {
            LOG_ERROR("local underfs slot path is not a directory, errno:" << errno << ".");
            return BIO_UFS_IOERR;
        }
    }
    mRootPath = std::move(dataRoot);

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
    StopLocalSystem();
}

void LocalSystem::StopLocalSystem() noexcept
{
    {
        std::lock_guard<std::mutex> lock(mEvictionWaitMutex);
        mEvictionStopping.store(true);
    }
    mEvictionCond.notify_all();
    if (mEvictionThread.joinable()) {
        mEvictionThread.join();
    }
    if (mLeaseFd >= 0) {
        close(mLeaseFd);
        mLeaseFd = -1;
    }
    if (mRootFd >= 0) {
        close(mRootFd);
        mRootFd = -1;
    }
    mEvictionPolicy.reset();
    mUsedBytes = 0;
    mReservedBytes = 0;
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

BResult LocalSystem::Put(const char *key, const char *value, const size_t len) noexcept
{
    return PutValue(key, value, len, nullptr, 0);
}

BResult LocalSystem::Put(const char *key, char *buffer, size_t len, size_t capacity) noexcept
{
    if (UNLIKELY(buffer == nullptr || reinterpret_cast<uintptr_t>(buffer) % LOCAL_FILE_IO_ALIGNMENT != 0)) {
        return BIO_INVALID_PARAM;
    }
    return PutValue(key, buffer, len, buffer, capacity);
}

BResult LocalSystem::PutValue(const char *key, const char *value, size_t len, char *buffer, size_t capacity) noexcept
{
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited)) {
        return BIO_NOT_READY;
    }
    const size_t keySize = key == nullptr ? 0 : strnlen(key, LOCAL_FILE_MAX_KEY_SIZE + 1);
    if (UNLIKELY(keySize == 0 || keySize > LOCAL_FILE_MAX_KEY_SIZE ||
        (value == nullptr && len != 0) || len > LOCAL_FILE_MAX_VALUE_SIZE)) {
        LOG_ERROR("Invalid local underfs put size, keySize:" << keySize << ", valueSize:" << len <<
            ", maxKeySize:" << LOCAL_FILE_MAX_KEY_SIZE << ", maxValueSize:" << LOCAL_FILE_MAX_VALUE_SIZE << ".");
        return BIO_INVALID_PARAM;
    }
    const size_t fileBytes = len + keySize + LOCAL_FILE_FOOTER_SIZE;
    const size_t alignedSize = LocalFilePack::AlignedWriteSize(fileBytes);
    if (UNLIKELY(buffer != nullptr && alignedSize > capacity)) {
        return BIO_INVALID_PARAM;
    }
    EvictionCandidate committed{std::string(key, keySize), {"", fileBytes}};
    std::string temporaryPath;
    struct WriteReservation {
        LocalSystem *owner;
        const std::string &key;
        uint64_t bytes;
        const std::string &temporaryPath;
        ~WriteReservation() noexcept
        {
            if (owner != nullptr) {
                if (!temporaryPath.empty()) {
                    owner->CleanupTemporaryFile(temporaryPath);
                }
                owner->ReleaseWriteSpace(key, bytes);
            }
        }
    } reservation{nullptr, committed.key, fileBytes, temporaryPath};
    BIO_TRACE_START(UFS_TRACE_PUT_RESERVE);
    BResult ret = ReserveWriteSpace(committed.key, fileBytes);
    BIO_TRACE_END(UFS_TRACE_PUT_RESERVE, ret == BIO_ALREADY_EXISTS ? BIO_OK : ret);
    if (ret == BIO_ALREADY_EXISTS) {
        return BIO_OK;
    }
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    reservation.owner = this;
    void *allocation = nullptr;
    if (buffer == nullptr && posix_memalign(&allocation, LOCAL_FILE_IO_ALIGNMENT, alignedSize) != 0) {
        return BIO_ALLOC_FAIL;
    }
    const std::unique_ptr<void, decltype(&free)> storage(allocation, free);
    if (buffer == nullptr) {
        buffer = static_cast<char *>(allocation);
        if (len != 0) {
            std::memcpy(buffer, value, len);
        }
    }
    size_t metadataSize = 0;
    ret = LocalFilePack::Serialize(key, len, buffer + len, metadataSize);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    std::memset(buffer + fileBytes, 0, alignedSize - fileBytes);
    std::string hashHex;
    ret = BuildHash(key, hashHex);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    committed.record.leaf = hashHex.substr(0, LEVEL_ONE_HEX_SIZE) + "/" +
        hashHex.substr(LEVEL_ONE_HEX_SIZE, LEVEL_TWO_HEX_SIZE);
    std::string finalPath;
    for (uint32_t attempt = 0; attempt < MAX_WRITE_ATTEMPTS; ++attempt) {
        bool outOfSpace = false;
        ret = WriteTemporaryValue(hashHex, buffer, fileBytes, alignedSize, temporaryPath, finalPath, outOfSpace);
        if (ret == BIO_OK) {
            const std::function<BResult()> publish = [&]() noexcept {
                return PublishFile(temporaryPath, finalPath, outOfSpace);
            };
            {
                std::lock_guard<std::mutex> capacity(mEvictionMutex);
                ret = mEvictionPolicy->CommitWrite(committed.key, committed.record, publish);
                if (ret == BIO_OK) {
                    mUsedBytes += fileBytes;
                    mReservedBytes -= fileBytes;
                    reservation.owner = nullptr;
                }
            }
        }
        if (ret == BIO_OK || !outOfSpace || attempt + 1 == MAX_WRITE_ATTEMPTS) {
            break;
        }
        if (!temporaryPath.empty()) {
            if (UNLIKELY(CleanupTemporaryFile(temporaryPath) != BIO_OK)) {
                return BIO_UFS_IOERR;
            }
            temporaryPath.clear();
        }
        ret = CheckAndEvict(fileBytes);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
    }
    return ret;
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
    size_t readLength = len;
    // Local index entries bound the value; shared files contain only raw value bytes.
    if (reference.IsValid()) {
        if (len != 0 && off >= valueSize) {
            return BIO_READ_EXCEED;
        }
        readLength = static_cast<size_t>(std::min<uint64_t>(len, off < valueSize ? valueSize - off : 0));
    }
    bool useDirectIo = IsDirectIoAligned(value, readLength, off);
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
    ret = ReadAvailable(fd, value, readLength, off, realLen, useDirectIo);
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

BResult LocalSystem::Delete(const char *key) noexcept
{
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited)) {
        return BIO_NOT_READY;
    }
    if (UNLIKELY(key == nullptr || key[0] == '\0' ||
        strnlen(key, LOCAL_FILE_MAX_KEY_SIZE + 1) > LOCAL_FILE_MAX_KEY_SIZE)) {
        return BIO_INVALID_PARAM;
    }
    // Serialize detached-key reuse with write admission and background eviction until unlink completes.
    std::lock_guard<std::mutex> capacity(mEvictionMutex);
    EvictionCandidate candidate;
    BResult ret = mEvictionPolicy->Remove(key, candidate);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    ScopedFileDelete deleting(*mEvictionPolicy, mUsedBytes, &candidate);
    std::string hashHex;
    ret = BuildHash(candidate.key.c_str(), hashHex);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    ret = DeleteFile(mRootPath + "/" + candidate.record.leaf + "/" + hashHex + FILE_SUFFIX);
    if (ret == BIO_OK || ret == BIO_NOT_EXISTS) {
        deleting.Commit();
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

BResult LocalSystem::Stat(const char *key, ObjStat &objStat) noexcept
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

    struct stat fileStat = {};
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return HandleOpenError("Stat local underfs value file", errno);
    }
    objStat.size = valueSize;
    objStat.time = fileStat.st_ctime;
    return BIO_OK;
}

BResult LocalSystem::List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) noexcept
{
    objStat.clear();
    LOG_WARN("local underfs does not support prefix list, prefixLength:" << strlen(prefix) << ".");
    return BIO_ERR;
}

BResult LocalSystem::BuildHash(const char *key, std::string &hashHex) const noexcept
{
    if (UNLIKELY(key == nullptr || key[0] == '\0' ||
        strnlen(key, LOCAL_FILE_MAX_KEY_SIZE + 1) > LOCAL_FILE_MAX_KEY_SIZE)) {
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

BResult LocalSystem::LookupFile(const char *key, std::string &filePath, uint64_t &valueSize,
    FileReference &reference) noexcept
{
    if (UNLIKELY(key == nullptr || key[0] == '\0' ||
        strnlen(key, LOCAL_FILE_MAX_KEY_SIZE + 1) > LOCAL_FILE_MAX_KEY_SIZE)) {
        return BIO_INVALID_PARAM;
    }
    if (UNLIKELY(mEvictionPolicy == nullptr)) {
        return BIO_NOT_READY;
    }
    FileRecord record;
    const std::string keyName(key);
    BIO_TRACE_START(UFS_TRACE_LOOKUP);
    BResult ret = mEvictionPolicy->Lookup(keyName, record, reference);
    BIO_TRACE_END(UFS_TRACE_LOOKUP, ret);
    if (ret != BIO_OK) {
        return ret;
    }
    std::string hashHex;
    ret = BuildHash(key, hashHex);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    filePath = mRootPath + "/" + record.leaf + "/" + hashHex + FILE_SUFFIX;
    const size_t metadataSize = keyName.size() + LOCAL_FILE_FOOTER_SIZE;
    if (UNLIKELY(record.sizeBytes < metadataSize)) {
        return BIO_UFS_IOERR;
    }
    valueSize = record.sizeBytes - metadataSize;
    return BIO_OK;
}

BResult LocalSystem::ReadFileMetadata(int fd, std::string &key, uint64_t &valueSize, bool &corrupt) const noexcept
{
    corrupt = false;
    struct stat status{};
    if (UNLIKELY(fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0)) {
        return BIO_UFS_IOERR;
    }
    const uint64_t fileSize = static_cast<uint64_t>(status.st_size);
    if (fileSize > LOCAL_FILE_FOOTER_SIZE &&
        fileSize <= LOCAL_FILE_MAX_VALUE_SIZE + LOCAL_FILE_MAX_METADATA_SIZE) {
        char metadata[LOCAL_FILE_MAX_METADATA_SIZE];
        const size_t metadataSize = static_cast<size_t>(std::min<uint64_t>(fileSize, sizeof(metadata)));
        size_t readSize = 0;
        BResult ret = ReadAvailable(fd, metadata, metadataSize, fileSize - metadataSize, readSize, false);
        if (UNLIKELY(ret != BIO_OK && ret != BIO_READ_EXCEED)) {
            return ret;
        }
        if (readSize == metadataSize && LocalFilePack::Deserialize(metadata, metadataSize, key, valueSize) == BIO_OK &&
            fileSize == valueSize + key.size() + LOCAL_FILE_FOOTER_SIZE) {
            return BIO_OK;
        }
    }

    // A supported file can start with arbitrary value bytes. Check the legacy magic only after footer validation fails.
    char header[sizeof(LEGACY_FILE_MAGIC) - 1];
    if (fileSize >= sizeof(header)) {
        size_t readSize = 0;
        BResult ret = ReadAvailable(fd, header, sizeof(header), 0, readSize, false);
        if (UNLIKELY(ret != BIO_OK && ret != BIO_READ_EXCEED)) {
            return ret;
        }
        if (readSize == sizeof(header) && std::memcmp(header, LEGACY_FILE_MAGIC, sizeof(header)) == 0) {
            return BIO_UFS_IOERR;
        }
    }

    corrupt = true;
    return BIO_OK;
}

void LocalSystem::SetMetaEventCallback(UfsLocalMetaEventCallback callback, void *context) noexcept
{
    std::lock_guard<std::mutex> lock(mCallbackMutex);
    mMetaEventCallback = callback;
    mMetaEventContext = context;
}

void LocalSystem::NotifyMetaEvent(UfsLocalMetaEventType type, const std::string &key) noexcept
{
    std::lock_guard<std::mutex> lock(mCallbackMutex);
    if (mMetaEventCallback != nullptr) {
        mMetaEventCallback(mMetaEventContext, type, key.c_str());
    }
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
    return BuildLeafPath(mRootPath, hashHex);
}

std::string LocalSystem::BuildLeafPath(const std::string &root, const std::string &hashHex)
{
    return root + "/" + hashHex.substr(0, LEVEL_ONE_HEX_SIZE) + "/" +
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
    std::string &temporaryPath, int32_t &fd, bool &outOfSpace) const noexcept
{
    leafPath = BuildLeafPath(hashHex);
    temporaryPath = BuildTemporaryPath(leafPath, hashHex);
    BResult ret = CreateTemporaryFile(temporaryPath, fd, true, outOfSpace);
    if (ret != BIO_NOT_EXISTS) {
        return ret;
    }

    ret = EnsureLeafDirectory(hashHex, leafPath, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    temporaryPath = BuildTemporaryPath(leafPath, hashHex);
    return CreateTemporaryFile(temporaryPath, fd, false, outOfSpace);
}

BResult LocalSystem::CreateTemporaryFile(std::string &temporaryPath, int32_t &fd,
    bool allowMissingDirectory, bool &outOfSpace) const noexcept
{
    BIO_TRACE_START(UFS_TRACE_PUT_CREAT);
    fd = mkostemp(&temporaryPath[0], O_CLOEXEC);
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

BResult LocalSystem::WriteTemporaryValue(const std::string &hashHex, const char *value, size_t fileSize,
    size_t alignedSize, std::string &temporaryPath, std::string &finalPath, bool &outOfSpace) const noexcept
{
    outOfSpace = false;
    std::string leafPath;
    int32_t fd = -1;
    BResult ret = CreateValueTemporaryFile(hashHex, leafPath, temporaryPath, fd, outOfSpace);
    if (UNLIKELY(ret != BIO_OK)) {
        temporaryPath.clear();
        return ret;
    }
    finalPath = BuildFilePath(leafPath, hashHex);
    BIO_TRACE_START(UFS_TRACE_PUT_WRITE);
    int32_t writeError = alignedSize == 0 ? WriteAll(fd, value, fileSize, 0) :
        WriteAlignedFile(fd, value, fileSize, alignedSize);
    if (writeError == 0 && alignedSize != 0) {
        // Publish the file only after its data and extent metadata are ready for XDS.
        writeError = SyncFileData(fd);
    }
    if (UNLIKELY(writeError != 0)) {
        outOfSpace = writeError == ENOSPC;
        ret = BIO_UFS_IOERR;
        LOG_ERROR("Write underfs temporary file failed, errno:" << writeError << ".");
    }
    BIO_TRACE_END(UFS_TRACE_PUT_WRITE, ret);
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
        if (useDirectIo && static_cast<size_t>(readLen) % LOCAL_FILE_IO_ALIGNMENT != 0) {
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

BResult LocalSystem::InitializeEviction() noexcept
{
    const auto &config = UnderFsConfig::Instance()->GetUnderFsConfig().localConfig;
    if (UNLIKELY(config.evictionPolicy != FileEvictionPolicyType::LRU ||
        config.checkIntervalSeconds == 0 || config.checkIntervalSeconds > 3600)) {
        return BIO_INVALID_PARAM;
    }
    mRootFd = open(mRootPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (UNLIKELY(mRootFd < 0)) {
        LOG_ERROR("Open local UnderFS eviction root failed, errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    mLeaseFd = openat(mRootFd, ".eviction.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (UNLIKELY(mLeaseFd < 0 || flock(mLeaseFd, LOCK_EX | LOCK_NB) != 0)) {
        LOG_ERROR("Local UnderFS eviction requires an exclusive directory, errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    mDeviceCount = config.deviceCount;
    if (UNLIKELY(mDeviceCount == 0)) {
        return BIO_INVALID_PARAM;
    }
    mCheckIntervalSeconds = config.checkIntervalSeconds;
    mEvictionStopping.store(false);
    mEvictionPolicy = CreateFileEvictionPolicy(config.evictionPolicy);
    if (UNLIKELY(mEvictionPolicy == nullptr)) {
        return BIO_ALLOC_FAIL;
    }
    BIO_TRACE_START(UFS_TRACE_RECOVER);
    BResult ret = RecoverEvictionState();
    BIO_TRACE_END(UFS_TRACE_RECOVER, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    ret = CheckAndEvict();
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    mEvictionThread = std::thread(&LocalSystem::EvictionLoop, this);
    LOG_INFO("Local UnderFS eviction enabled, capacity:" << mCapacityBytes <<
        ", slotIndex:" << config.slotIndex << ", deviceCount:" << mDeviceCount <<
        ", filesystemCapacityPercent:" << FILESYSTEM_CAPACITY_PERCENT <<
        ", highWaterLevel:" << EVICTION_HIGH_WATER_LEVEL <<
        ", evictionKeyPercent:" << 100 / EVICTION_KEY_COUNT_DIVISOR << ", policy:lru.");
    return BIO_OK;
}

BResult LocalSystem::GetFileSystemSpace(uint64_t &total, uint64_t &available) const noexcept
{
    struct statvfs space{};
    BIO_TRACE_START(UFS_TRACE_OBTAIN_SPACE);
    if (UNLIKELY(fstatvfs(mRootFd, &space) != 0)) {
        const int32_t errorCode = errno;
        BIO_TRACE_END(UFS_TRACE_OBTAIN_SPACE, BIO_UFS_IOERR);
        LOG_ERROR("Query local UnderFS filesystem capacity failed, errno:" << errorCode << ".");
        return BIO_UFS_IOERR;
    }
    BIO_TRACE_END(UFS_TRACE_OBTAIN_SPACE, BIO_OK);
    uint64_t blockSize = space.f_frsize == 0 ? space.f_bsize : space.f_frsize;
    if (UNLIKELY(blockSize == 0 || space.f_blocks == 0 || space.f_blocks > UINT64_MAX / blockSize)) {
        return BIO_UFS_IOERR;
    }
    total = static_cast<uint64_t>(space.f_blocks) * blockSize;
    available = static_cast<uint64_t>(std::min(space.f_bavail, space.f_blocks)) * blockSize;
    return BIO_OK;
}

BResult LocalSystem::CollectLeafFiles(int firstFd, const std::string &firstName,
    FileIndex &index, uint64_t &used, bool cleanupFiles) const noexcept
{
    ScopedDirectory second(openat(firstFd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (UNLIKELY(second.value == nullptr)) {
        return BIO_UFS_IOERR;
    }
    while (!mEvictionStopping.load()) {
        errno = 0;
        auto entry = readdir(second.value);
        if (entry == nullptr) {
            return errno == 0 ? BIO_OK : BIO_UFS_IOERR;
        }
        std::string secondName(entry->d_name);
        if (!IsHashPart(secondName, LEVEL_TWO_HEX_SIZE)) {
            continue;
        }
        ScopedDirectory leaf(openat(firstFd, secondName.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (leaf.value == nullptr) {
            if (errno == ENOENT || errno == ENOTDIR || errno == ELOOP) {
                continue;
            }
            return BIO_UFS_IOERR;
        }
        std::vector<std::string> corruptFiles;
        while (!mEvictionStopping.load()) {
            errno = 0;
            auto value = readdir(leaf.value);
            if (value == nullptr) {
                if (UNLIKELY(errno != 0)) {
                    return BIO_UFS_IOERR;
                }
                break;
            }
            std::string name(value->d_name);
            if (cleanupFiles && UNLIKELY(IsRecoveryTemporaryFile(name, firstName, secondName))) {
                BResult ret = CleanupRecoveredTemporaryFile(dirfd(leaf.value), name);
                if (UNLIKELY(ret != BIO_OK)) {
                    return ret;
                }
                continue;
            }
            if (name.size() != SHA256_HEX_SIZE + strlen(FILE_SUFFIX) ||
                name.compare(SHA256_HEX_SIZE, strlen(FILE_SUFFIX), FILE_SUFFIX) != 0 ||
                !IsHashPart(name.substr(0, SHA256_HEX_SIZE), SHA256_HEX_SIZE) ||
                name.compare(0, LEVEL_ONE_HEX_SIZE, firstName) != 0 ||
                name.compare(LEVEL_ONE_HEX_SIZE, LEVEL_TWO_HEX_SIZE, secondName) != 0) {
                continue;
            }
            struct stat status{};
            if (fstatat(dirfd(leaf.value), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
                if (errno == ENOENT) {
                    continue;
                }
                return BIO_UFS_IOERR;
            }
            if (!S_ISREG(status.st_mode)) {
                continue;
            }
            if (UNLIKELY(status.st_size < 0)) {
                return BIO_UFS_IOERR;
            }
            uint64_t bytes = static_cast<uint64_t>(status.st_size);
            ScopedFd stored(openat(dirfd(leaf.value), name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
            if (UNLIKELY(stored.value < 0)) {
                return BIO_UFS_IOERR;
            }
            std::string key;
            uint64_t valueSize = 0;
            bool corrupt = false;
            BResult ret = ReadFileMetadata(stored.value, key, valueSize, corrupt);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("Read local underfs recovery metadata failed, file:" << name << ", ret:" << ret << ".");
                return ret;
            }
            if (LIKELY(!corrupt)) {
                std::string hash;
                ret = BuildHash(key.c_str(), hash);
                if (UNLIKELY(ret != BIO_OK)) {
                    return ret;
                }
                corrupt = hash + FILE_SUFFIX != name;
            }
            if (UNLIKELY(corrupt)) {
                if (cleanupFiles) {
                    corruptFiles.push_back(std::move(name));
                }
                continue;
            }
            if (UNLIKELY(used > UINT64_MAX - bytes)) {
                return BIO_UFS_IOERR;
            }
            const int32_t syncError = cleanupFiles ? SyncFileData(stored.value) : 0;
            if (UNLIKELY(syncError != 0)) {
                LOG_ERROR("Sync recovered LocalFS file failed, errno:" << syncError << ".");
                return BIO_UFS_IOERR;
            }
            ret = AddRecoveryFile(index, key, firstName, secondName, bytes);
            if (UNLIKELY(ret != BIO_OK)) {
                return ret;
            }
            used += bytes;
        }
        // Rename only after consuming this directory stream so valid files are not skipped.
        for (const auto &name : corruptFiles) {
            BResult ret = QuarantineRecoveryFile(dirfd(leaf.value), name);
            if (UNLIKELY(ret != BIO_OK)) {
                return ret;
            }
        }
    }
    return BIO_INNER_RETRY;
}

BResult LocalSystem::CleanupRecoveredTemporaryFile(int directory, const std::string &name) noexcept
{
    struct stat status{};
    if (UNLIKELY(fstatat(directory, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)) {
        return errno == ENOENT ? BIO_OK : BIO_UFS_IOERR;
    }
    if (!S_ISREG(status.st_mode)) {
        return BIO_OK;
    }
    if (UNLIKELY(unlinkat(directory, name.c_str(), 0) != 0 && errno != ENOENT)) {
        LOG_ERROR("Clean local underfs recovery temporary file failed, errno:" << errno << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult LocalSystem::CollectRecoveryFiles(FileIndex &index, uint64_t &used, bool cleanupFiles) const noexcept
{
    used = 0;
    // The caller supplies a temporary index; no resident metadata is changed during scanning.
    // Opening "." creates a fresh directory stream; dup() would share the scan offset.
    ScopedDirectory root(openat(mRootFd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (UNLIKELY(root.value == nullptr)) {
        return BIO_UFS_IOERR;
    }
    while (!mEvictionStopping.load()) {
        errno = 0;
        auto entry = readdir(root.value);
        if (entry == nullptr) {
            BResult ret = errno == 0 ? BIO_OK : BIO_UFS_IOERR;
            return ret;
        }
        std::string firstName(entry->d_name);
        if (!IsHashPart(firstName, LEVEL_ONE_HEX_SIZE)) {
            continue;
        }
        ScopedFd first(openat(mRootFd, firstName.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (first.value < 0) {
            if (errno == ENOENT || errno == ENOTDIR || errno == ELOOP) {
                continue;
            }
            return BIO_UFS_IOERR;
        }
        BResult ret = CollectLeafFiles(first.value, firstName, index, used, cleanupFiles);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
    }
    return BIO_INNER_RETRY;
}

BResult LocalSystem::GetStorageMetrics(UfsStorageMetrics &metrics) noexcept
{
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited || mEvictionPolicy == nullptr)) {
        return BIO_NOT_READY;
    }
    std::lock_guard<std::mutex> capacity(mEvictionMutex);
    metrics.totalCapacity = mCapacityBytes;
    metrics.usedBytes = mUsedBytes;
    return BIO_OK;
}

BResult LocalSystem::ScanKeys(UfsLocalKeyInfo **items, uint64_t *count) noexcept
{
    if (UNLIKELY(items == nullptr || count == nullptr)) {
        return BIO_INVALID_PARAM;
    }
    *items = nullptr;
    *count = 0;
    ReadLocker<ReadWriteLock> lifecycle(&mLifecycleLock);
    if (UNLIKELY(!mInited || mEvictionPolicy == nullptr)) {
        return BIO_NOT_READY;
    }
    // Keep the allocation bound stable while excluding unpublished placeholders from the snapshot.
    std::lock_guard<std::mutex> capacity(mEvictionMutex);
    const size_t size = mEvictionPolicy->Size();
    if (size == 0) {
        return BIO_OK;
    }
    if (UNLIKELY(size > SIZE_MAX / sizeof(UfsLocalKeyInfo))) {
        return BIO_ALLOC_FAIL;
    }
    std::unique_ptr<UfsLocalKeyInfo, decltype(&std::free)> snapshot(
        static_cast<UfsLocalKeyInfo *>(std::calloc(size, sizeof(UfsLocalKeyInfo))), &std::free);
    if (UNLIKELY(snapshot == nullptr)) {
        return BIO_ALLOC_FAIL;
    }
    uint64_t index = 0;
    BResult ret = mEvictionPolicy->ForEachFile([&](const std::string &key, const FileRecord &file) noexcept {
        const size_t metadataSize = key.size() + LOCAL_FILE_FOOTER_SIZE;
        if (UNLIKELY(key.size() >= MAX_KEY_SIZE || file.sizeBytes < metadataSize || index >= size)) {
            return BIO_UFS_IOERR;
        }
        auto &item = snapshot.get()[index++];
        std::memcpy(item.key, key.c_str(), key.size() + 1);
        item.valueLen = file.sizeBytes - metadataSize;
        return BIO_OK;
    });
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    if (index != 0) {
        *items = snapshot.release();
        *count = index;
    }
    return BIO_OK;
}

BResult LocalSystem::RecoverEvictionState() noexcept
{
    std::lock_guard<std::mutex> capacity(mEvictionMutex);
    if (UNLIKELY(mReservedBytes != 0)) {
        return BIO_INNER_RETRY;
    }
    FileIndex index;
    uint64_t used = 0;
    BResult ret = CollectRecoveryFiles(index, used, true);
    if (ret == BIO_OK) {
        ret = mEvictionPolicy->Recover(index);
    }
    if (ret == BIO_OK) {
        mUsedBytes = used;
        LOG_INFO("Recovered local UnderFS files:" << mEvictionPolicy->Size() <<
            ", usedBytes:" << used << ".");
    }
    return ret;
}

BResult LocalSystem::ReserveWriteSpace(const std::string &key, uint64_t bytes) noexcept
{
    std::unique_lock<std::mutex> capacity(mEvictionMutex);
    BResult ret = mEvictionPolicy->Insert(key);
    if (ret != BIO_OK) {
        return ret;
    }
    if (UNLIKELY(bytes > mCapacityBytes || mReservedBytes > mCapacityBytes - bytes)) {
        mEvictionPolicy->RollbackWrite(key);
        return BIO_UFS_IOERR;
    }
    const uint64_t highWatermark = PercentBytes(mCapacityBytes, EVICTION_HIGH_WATER_LEVEL);
    const bool needsEviction = ExceedsLimit(mUsedBytes, mReservedBytes, highWatermark) ||
        ExceedsLimit(mUsedBytes, mReservedBytes, mCapacityBytes - bytes);
    mReservedBytes += bytes;
    if (LIKELY(!needsEviction)) {
        return BIO_OK;
    }
    capacity.unlock();
    ret = CheckAndEvict(bytes);
    if (UNLIKELY(ret != BIO_OK)) {
        ReleaseWriteSpace(key, bytes);
    }
    return ret;
}

void LocalSystem::ReleaseWriteSpace(const std::string &key, uint64_t bytes) noexcept
{
    std::lock_guard<std::mutex> capacity(mEvictionMutex);
    mEvictionPolicy->RollbackWrite(key);
    mReservedBytes -= bytes;
}

BResult LocalSystem::DeleteEvictionCandidate(const EvictionCandidate &candidate, bool &deleted) noexcept
{
    const auto &file = candidate.record;
    deleted = false;
    std::string hashHex;
    BResult ret = BuildHash(candidate.key.c_str(), hashHex);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    auto slash = file.leaf.find('/');
    ScopedFd first(openat(mRootFd, file.leaf.substr(0, slash).c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (first.value < 0) {
        return errno == ENOENT ? BIO_OK : BIO_UFS_IOERR;
    }
    ScopedFd leaf(openat(first.value, file.leaf.substr(slash + 1).c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (leaf.value < 0) {
        return errno == ENOENT ? BIO_OK : BIO_UFS_IOERR;
    }
    struct stat current{};
    const auto name = hashHex + FILE_SUFFIX;
    if (fstatat(leaf.value, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0) {
        return errno == ENOENT ? BIO_OK : BIO_UFS_IOERR;
    }
    if (!S_ISREG(current.st_mode)) {
        return BIO_OK;
    }
    if (unlinkat(leaf.value, name.c_str(), 0) != 0) {
        LOG_ERROR("Evict local UnderFS file failed, errno:" << errno << ".");
        return errno == ENOENT ? BIO_OK : BIO_UFS_IOERR;
    }
    deleted = true;
    return BIO_OK;
}

BResult LocalSystem::CheckAndEvict(uint64_t requiredBytes) noexcept
{
    if (mEvictionStopping.load()) {
        return requiredBytes == 0 ? BIO_OK : BIO_NOT_READY;
    }
    bool expected = false;
    if (UNLIKELY(!mEvictionInProgress.compare_exchange_strong(expected, true))) {
        return BIO_INNER_RETRY;
    }
    struct EvictionGuard {
        std::atomic<bool> &inProgress;
        ~EvictionGuard() noexcept
        {
            inProgress.store(false);
        }
    } eviction{mEvictionInProgress};
    std::lock_guard<std::mutex> capacity(mEvictionMutex);
    if (UNLIKELY(requiredBytes > mReservedBytes)) {
        return BIO_INVALID_PARAM;
    }
    // Exclude this write from the other in-flight reservations while calculating its space deficit.
    mReservedBytes -= requiredBytes;
    uint64_t total = 0, available = 0;
    BResult ret = GetFileSystemSpace(total, available);
    if (ret == BIO_OK) {
        const uint64_t totalCapacity = PercentBytes(total, FILESYSTEM_CAPACITY_PERCENT);
        if (UNLIKELY(mDeviceCount == 0 || totalCapacity / mDeviceCount == 0)) {
            ret = BIO_INVALID_PARAM;
        } else {
            mCapacityBytes = totalCapacity / mDeviceCount;
            ret = EvictFiles(requiredBytes, available);
        }
    }
    if (ret == BIO_OK && requiredBytes != 0 && (requiredBytes > mCapacityBytes ||
        ExceedsLimit(mUsedBytes, mReservedBytes, mCapacityBytes - requiredBytes) ||
        ExceedsLimit(mReservedBytes, requiredBytes, available))) {
        ret = BIO_UFS_IOERR;
    }
    mReservedBytes += requiredBytes;
    return ret;
}

BResult LocalSystem::EvictFiles(uint64_t requiredBytes, uint64_t &available) noexcept
{
    BResult ret;
    if (requiredBytes != 0 &&
        (requiredBytes > mCapacityBytes || mReservedBytes > mCapacityBytes - requiredBytes)) {
        return BIO_UFS_IOERR;
    }
    const size_t limit = requiredBytes == 0 ? MAX_BACKGROUND_EVICTION_BATCH_FILES : MAX_WRITE_EVICTION_BATCH_FILES;
    const uint64_t highWatermark = PercentBytes(mCapacityBytes, EVICTION_HIGH_WATER_LEVEL);
    size_t remainingKeys = 0;
    size_t rounds = 0;
    size_t deletedCount = 0;
    size_t attempts = 0;
    uint64_t deletedFileBytes = 0;
    const uint64_t usedBefore = mUsedBytes;
    const uint64_t availableBefore = available;
    const auto finishBatch = [&](BResult result) noexcept {
        LOG_DEBUG("Local UnderFS watermark eviction, foreground:" << (requiredBytes != 0) <<
            ", capacity:" << mCapacityBytes << ", usedBefore:" << usedBefore << ", usedAfter:" << mUsedBytes <<
            ", reservedBytes:" << mReservedBytes << ", availableBefore:" << availableBefore <<
            ", availableAfter:" << available << ", rounds:" << rounds << ", examinedFiles:" << attempts <<
            ", deletedFiles:" << deletedCount << ", deletedFileBytes:" << deletedFileBytes << ", ret:" << result << ".");
        return result;
    };
    while (!mEvictionStopping.load()) {
        uint64_t reclaimBytes = 0;
        if (requiredBytes != 0) {
            const uint64_t quotaUsed = mUsedBytes + mReservedBytes;
            const uint64_t quotaLimit = mCapacityBytes - requiredBytes;
            reclaimBytes = quotaUsed > quotaLimit ? quotaUsed - quotaLimit : 0;
            if (ExceedsLimit(mReservedBytes, requiredBytes, available)) {
                reclaimBytes = std::max(reclaimBytes, mReservedBytes + requiredBytes - available);
            }
        }
        // Finish the current round across batches before starting another rounded key-count target.
        if (remainingKeys == 0 && ExceedsLimit(mUsedBytes, mReservedBytes, highWatermark)) {
            const size_t count = mEvictionPolicy->Size();
            if (UNLIKELY(count == 0)) {
                break;
            }
            remainingKeys = count / EVICTION_KEY_COUNT_DIVISOR + (count % EVICTION_KEY_COUNT_DIVISOR != 0);
            ++rounds;
        }
        if (remainingKeys == 0 && reclaimBytes == 0) {
            break;
        }
        const uint64_t batchUsedBefore = mUsedBytes;
        const size_t deletedBefore = deletedCount;
        std::vector<EvictionCandidate> candidates;
        const std::function<bool(const FileRecord &)> keepSelecting = [&](const FileRecord &file) noexcept {
            if (remainingKeys != 0) {
                --remainingKeys;
            }
            reclaimBytes -= std::min(reclaimBytes, file.sizeBytes);
            return remainingKeys != 0 || reclaimBytes != 0;
        };
        BIO_TRACE_START(UFS_TRACE_EVICT_SELECT);
        ret = mEvictionPolicy->TakeCandidates(limit, keepSelecting, candidates);
        BIO_TRACE_END(UFS_TRACE_EVICT_SELECT, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            return finishBatch(ret);
        }
        if (candidates.empty()) {
            break;
        }
        ScopedFileDelete deleting(*mEvictionPolicy, mUsedBytes, candidates.data(), candidates.size());
        for (const auto &candidate : candidates) {
            if (mEvictionStopping.load()) {
                break;
            }
            ++attempts;
            bool deleted = false;
            BIO_TRACE_START(UFS_TRACE_EVICT_FILE);
            ret = DeleteEvictionCandidate(candidate, deleted);
            BIO_TRACE_END(UFS_TRACE_EVICT_FILE, ret);
            if (ret == BIO_OK) {
                deleting.Commit();
                NotifyMetaEvent(UFS_LOCAL_META_DELETE, candidate.key);
            }
            if (UNLIKELY(ret != BIO_OK)) {
                return finishBatch(ret);
            }
            if (deleted) {
                ++deletedCount;
                deletedFileBytes += candidate.record.sizeBytes;
            }
        }
        if (deletedCount != deletedBefore) {
            uint64_t total = 0;
            ret = GetFileSystemSpace(total, available);
            if (UNLIKELY(ret != BIO_OK)) {
                return finishBatch(ret);
            }
        }
        if (mUsedBytes == batchUsedBefore) {
            break;
        }
    }
    if (attempts != 0) {
        return finishBatch(BIO_OK);
    }
    return BIO_OK;
}

void LocalSystem::EvictionLoop() noexcept
{
    std::unique_lock<std::mutex> lock(mEvictionWaitMutex);
    while (!mEvictionStopping.load()) {
        if (mEvictionCond.wait_for(lock, std::chrono::seconds(mCheckIntervalSeconds),
            [this]() { return mEvictionStopping.load(); })) {
            break;
        }
        lock.unlock();
        BResult ret = CheckAndEvict();
        if (UNLIKELY(ret != BIO_OK && ret != BIO_INNER_RETRY)) {
            LOG_WARN("Local UnderFS watermark check failed, ret:" << ret << ".");
        }
        lock.lock();
    }
}
}
}
