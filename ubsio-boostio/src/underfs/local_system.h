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

#include "file_system.h"

namespace ock {
namespace bio {
class LocalSystem : public FileSystem {
public:
    BResult Init() override;

    void Stop() override;

    BResult Put(const char *key, const char *value, const size_t len) override;

    BResult Get(const char *key, char *value, const size_t len, const uint64_t off) override;

    BResult GetWithRealLen(const char *key, char *value, const size_t len, const uint64_t off,
        size_t &realLen) override;

    BResult Delete(const char *key) override;

    BResult Exist(const char *key) override;

    BResult Stat(const char *key, ObjStat &objStat) override;

    BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) override;

private:
    using Sha256Function = unsigned char *(*)(const unsigned char *data, size_t len, unsigned char *digest);

    BResult BuildHash(const char *key, std::string &hashHex) const;
    BResult BuildFilePath(const char *key, std::string &filePath) const;
    std::string BuildLeafPath(const std::string &hashHex) const;
    std::string BuildFilePath(const std::string &leafPath, const std::string &hashHex) const;
    std::string BuildTemporaryPath(const std::string &leafPath, const std::string &hashHex) const;
    BResult CreateValueTemporaryFile(const std::string &hashHex, std::string &leafPath, std::string &temporaryPath,
        int32_t &fd, bool useDirectIo) const;
    BResult CreateTemporaryFile(std::string &temporaryPath, int32_t &fd, bool allowMissingDirectory,
        bool useDirectIo) const;
    BResult EnsureLeafDirectory(const std::string &hashHex, std::string &leafPath) const;
    BResult EnsureDirectory(const std::string &path) const;
    BResult WriteAll(int32_t fd, const char *value, size_t len) const;
    BResult ReadAvailable(int32_t fd, char *value, size_t len, uint64_t off, size_t &realLen,
        bool useDirectIo) const;
    bool ValidateFileSize(off_t fileSize) const;
    BResult HandleOpenError(const char *operation, int32_t errorCode) const;
    void CleanupTemporaryFile(const std::string &temporaryPath) const;

private:
    std::string mRootPath;
    void *mCryptoHandle{ nullptr };
    Sha256Function mSha256{ nullptr };
};
}
}

#endif // BOOSTIO_LOCALSYSTEM_H
