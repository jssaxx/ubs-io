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

#include <unordered_map>
#include <vector>
#include <utility>
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

    BResult Init() noexcept override = 0;

    void Stop() noexcept override;

    BResult Put(const char *key, const char *value, const size_t len) noexcept override = 0;

    virtual BResult PutV(const char *key, const struct iovec *iov, size_t iovcnt) noexcept = 0;

    BResult Get(const char *key, char *value, const size_t len, const uint64_t off) noexcept override;

    BResult GetWithRealLen(const char *key, char *value, const size_t len, const uint64_t off,
        size_t &realLen) noexcept override;

    BResult Delete(const char *key) noexcept override = 0;

    BResult Exist(const char *key) noexcept override;

    BResult Stat(const char *key, ObjStat &objStat) noexcept override = 0;

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
        FileReference &reference) noexcept = 0;

    BResult BuildHash(const char *key, std::string &hashHex) const noexcept;

    BResult BuildFilePath(const char *key, std::string &filePath) const noexcept;

    BResult HandleOpenError(const char *operation, int32_t errorCode) const noexcept;

private:
    using Sha256Function = unsigned char *(*)(const unsigned char *data, size_t len, unsigned char *digest);

    std::string BuildLeafPath(const std::string &hashHex) const noexcept;

    std::string BuildFilePath(const std::string &leafPath, const std::string &hashHex) const noexcept;

    std::string BuildTemporaryPath(const std::string &leafPath, const std::string &hashHex) const noexcept;

    BResult CreateValueTemporaryFile(const std::string &hashHex, std::string &leafPath, std::string &temporaryPath,
        int32_t &fd, bool &outOfSpace, bool useDirectIo) const noexcept;

    BResult CreateTemporaryFile(std::string &temporaryPath, int32_t &fd, bool allowMissingDirectory,
        bool &outOfSpace, bool useDirectIo) const noexcept;

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
};
}
}

#endif // BOOSTIO_LOCALSYSTEM_H
