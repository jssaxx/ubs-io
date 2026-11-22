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

#ifndef BOOSTIO_LOCALSYSTEM_H
#define BOOSTIO_LOCALSYSTEM_H

#include <condition_variable>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <thread>
#include <vector>
#include <utility>
#include <functional>
#include "bio_lock.h"
#include "file_system.h"
#include "file_eviction_policy.h"
#include "local_file_pack.h"
#include "underfs_c.h"

namespace ock {
namespace bio {
class LocalSystem : public FileSystem {
public:
    virtual ~LocalSystem() noexcept;

    BResult Init() noexcept override;

    void Stop() noexcept override;

    BResult Put(const char *key, const char *value, const size_t len) noexcept override;

    virtual BResult PutV(const char *key, const struct iovec *iov, size_t iovcnt) noexcept;

    BResult Get(const char *key, char *value, const size_t len, const uint64_t off) noexcept override;

    BResult GetWithRealLen(const char *key, char *value, const size_t len, const uint64_t off,
        size_t &realLen) noexcept override;

    BResult Delete(const char *key) noexcept override;

    BResult Exist(const char *key) noexcept override;

    BResult Stat(const char *key, ObjStat &objStat) noexcept override;

    BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) noexcept override;

    BResult ScanKeys(UfsLocalKeyInfo **items, uint64_t *count) noexcept;

    BResult GetStorageMetrics(UfsStorageMetrics &metrics) noexcept;

    void SetMetaEventCallback(UfsLocalMetaEventCallback callback, void *context) noexcept;

protected:
    BResult InitializeFileSystem() noexcept;

    void StopFileSystem() noexcept;

    static BResult ValidateWriteBuffers(const struct iovec *iov, size_t iovcnt, size_t &len) noexcept;
    // The caller owns cleanup of a created temporary file until publication succeeds.
    // outOfSpace reports ENOSPC independently of subsequent close, trace and logging calls.
    BResult WriteTemporaryValue(const std::string &hashHex, const struct iovec *iov, size_t iovcnt,
        const char *metadata, size_t metadataSize, std::string &temporaryPath,
        std::string &finalPath, bool &outOfSpace) const noexcept;

    BResult PublishFile(const std::string &temporaryPath, const std::string &finalPath,
        bool &outOfSpace) const noexcept;

    BResult CleanupTemporaryFile(const std::string &temporaryPath) const noexcept;

    BResult DeleteFile(const std::string &filePath) const noexcept;

    virtual BResult LookupFile(const char *key, std::string &filePath, uint64_t &valueSize,
        FileReference &reference) noexcept;

    BResult BuildHash(const char *key, std::string &hashHex) const noexcept;

    BResult BuildFilePath(const char *key, std::string &filePath) const noexcept;

    BResult HandleOpenError(const char *operation, int32_t errorCode) const noexcept;

private:
    void StopLocalSystem() noexcept;

    BResult PutValue(const char *key, const struct iovec *iov, size_t iovcnt) noexcept;

    BResult InitializeEviction() noexcept;

    BResult RecoverEvictionState() noexcept;

    BResult ReserveWriteSpace(const std::string &key, uint64_t bytes) noexcept;
    // Claims eviction ownership before locking; requiredBytes must already be reserved by this write.
    BResult CheckAndEvict(uint64_t requiredBytes = 0) noexcept;

    void ReleaseWriteSpace(const std::string &key, uint64_t bytes) noexcept;

    BResult GetFileSystemSpace(uint64_t &total, uint64_t &available) const noexcept;
    // Cleanup requires exclusive directory ownership and no reserved writes under mEvictionMutex.
    BResult CollectRecoveryFiles(FileIndex &index, uint64_t &used, bool cleanupFiles = false) const noexcept;

    BResult CollectLeafFiles(int firstFd, const std::string &firstName,
        FileIndex &index, uint64_t &used, bool cleanupFiles) const noexcept;

    static BResult CleanupRecoveredTemporaryFile(int directory, const std::string &name) noexcept;
    // Caller holds mEvictionMutex and supplies a space snapshot; deletion refreshes available bytes.
    BResult EvictFiles(uint64_t requiredBytes, uint64_t &available) noexcept;

    BResult DeleteEvictionCandidate(const EvictionCandidate &candidate, bool &deleted) noexcept;

    void EvictionLoop() noexcept;

    void NotifyMetaEvent(UfsLocalMetaEventType type, const std::string &key) noexcept;

    BResult ReadFileMetadata(int fd, std::string &key, uint64_t &valueSize, bool &corrupt) const noexcept;

    using Sha256Function = unsigned char *(*)(const unsigned char *data, size_t len, unsigned char *digest);

    std::string BuildLeafPath(const std::string &hashHex) const noexcept;

    std::string BuildFilePath(const std::string &leafPath, const std::string &hashHex) const noexcept;

    std::string BuildTemporaryPath(const std::string &leafPath, const std::string &hashHex) const noexcept;

    BResult CreateValueTemporaryFile(const std::string &hashHex, std::string &leafPath, std::string &temporaryPath,
        int32_t &fd, bool &outOfSpace) const noexcept;

    BResult CreateTemporaryFile(std::string &temporaryPath, int32_t &fd, bool allowMissingDirectory,
        bool &outOfSpace) const noexcept;

    BResult EnsureLeafDirectory(const std::string &hashHex, std::string &leafPath,
        bool &outOfSpace) const noexcept;

    BResult EnsureDirectory(const std::string &path, bool &outOfSpace) const noexcept;

    BResult WriteAll(int32_t fd, const char *value, size_t len, uint64_t offset,
        bool &outOfSpace) const noexcept;

    BResult WriteAllV(int32_t fd, const struct iovec *iov, size_t iovcnt,
        const char *metadata, size_t metadataSize, bool &outOfSpace) const noexcept;

    BResult ReadAvailable(int32_t fd, char *value, size_t len, uint64_t off, size_t &realLen,
        bool useDirectIo) const noexcept;

    BResult ReadValue(const char *key, char *value, size_t len, uint64_t off, size_t &realLen,
        bool requireFullLength) noexcept;

protected:
    // Calls share the lock; initialization and shutdown wait for all calls to finish.
    ReadWriteLock mLifecycleLock;

private:
    std::string mRootPath;
    void *mCryptoHandle{ nullptr };
    Sha256Function mSha256{ nullptr };
    int mRootFd{ -1 };
    int mLeaseFd{ -1 };
    // Configured ubsio.standalone.device_count; lazy slot allocation does not change capacity division.
    uint32_t mDeviceCount{ 1 };
    // 90% of filesystem total divided by configured device count, refreshed during space checks.
    uint64_t mCapacityBytes{ 0 };
    // Protected by mEvictionMutex; detached files remain used until physical deletion completes.
    uint64_t mUsedBytes{ 0 };
    uint64_t mReservedBytes{ 0 };
    uint32_t mCheckIntervalSeconds{ 10 };
    std::unique_ptr<FileEvictionPolicy> mEvictionPolicy;
    std::atomic<bool> mEvictionStopping{ true };
    // One capacity check and eviction pass per instance; concurrent triggers return BIO_INNER_RETRY.
    std::atomic<bool> mEvictionInProgress{ false };
    std::mutex mEvictionWaitMutex;
    // Protects capacity accounting and key reuse through deletion; data reads/writes never hold it.
    std::mutex mEvictionMutex;
    std::mutex mCallbackMutex;
    std::condition_variable mEvictionCond;
    std::thread mEvictionThread;
    UfsLocalMetaEventCallback mMetaEventCallback{ nullptr };
    void *mMetaEventContext{ nullptr };
};
}
}

#endif // BOOSTIO_LOCALSYSTEM_H
