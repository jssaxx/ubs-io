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

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <memory>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bio_err.h"
#include "dl_acl_api.h"
#include "dl_biosdk_api.h"
#include "ubsio_nds_manager.h"
#include "ubsio_kvc_instance.h"

namespace ock {
namespace ubsio {

namespace {
constexpr uint8_t MAX_LOCAL_FILE_SLOT = 16;
constexpr uint32_t FILE_READ_WINDOW = 64;

CResult ToLocalFilePathResult(int32_t result)
{
    switch (result) {
        case bio::BIO_OK:
            return RET_CACHE_OK;
        case bio::BIO_INVALID_PARAM:
            return RET_CACHE_EPERM;
        case bio::BIO_NOT_READY:
            return RET_CACHE_NOT_READY;
        case bio::BIO_UFS_IOERR:
            return RET_CACHE_UFS_FAULT;
        default:
            return RET_CACHE_ERROR;
    }
}

bool PrepareFileRead(const char *key, uint8_t slot, nds::LocalFileRead &item)
{
    if (slot == 0 || slot > MAX_LOCAL_FILE_SLOT || !key || !*key || strnlen(key, MAX_KEY_SIZE) >= MAX_KEY_SIZE ||
        !item.buffers || !item.lengths || item.iovCount == 0 || item.iovCount > NDS_FILE_MAX_IOV) {
        return false;
    }
    for (uint32_t i = 0; i < item.iovCount; ++i) {
        const auto addr = reinterpret_cast<uintptr_t>(item.buffers[i]);
        const size_t length = item.lengths[i];
        if (!addr || !length || length > UINT32_MAX - item.valueLen || length > UINTPTR_MAX - addr) {
            return false;
        }
        item.valueLen += length;
    }
    return true;
}

int ReadFileViaHost(int fd, const nds::LocalFileRead &item)
{
    void *data = nullptr;
    if (ACLApi::AclrtMallocHost(&data, item.valueLen) != 0 || !data) {
        return RET_CACHE_ERROR;
    }
    std::unique_ptr<void, decltype(&ACLApi::AclrtFreeHost)> host(data, ACLApi::AclrtFreeHost);
    auto *bytes = static_cast<char *>(data);
    size_t offset = 0;
    while (offset < item.valueLen) {
        const ssize_t got = pread(fd, bytes + offset, item.valueLen - offset, offset);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return RET_CACHE_ERROR;
        }
        offset += static_cast<size_t>(got);
    }
    offset = 0;
    for (uint32_t i = 0; i < item.iovCount; ++i) {
        if (ACLApi::AclrtMemcpy(item.buffers[i], item.lengths[i], bytes + offset, item.lengths[i],
                                ACL_MEMCPY_HOST_TO_DEVICE) != 0) {
            return RET_CACHE_ERROR;
        }
        offset += item.lengths[i];
    }
    return RET_CACHE_OK;
}

struct FileWindow {
    std::array<nds::LocalFileRead, FILE_READ_WINDOW> items{};
    std::array<int, FILE_READ_WINDOW> fds;
    std::array<uint64_t, FILE_READ_WINDOW> devices{};
    std::array<int, FILE_READ_WINDOW> direct{};

    FileWindow() { fds.fill(-1); }

    ~FileWindow()
    {
        for (int fd : fds) {
            if (fd >= 0) {
                close(fd);
            }
        }
    }
};

void ReadFileWindow(UfsResolveFilePathFunc resolve, const char *const *keys, const uint8_t *ownerSlots,
                    void *const *const *buffers, const size_t *const *lengths, const uint32_t *iovCounts,
                    uint32_t count, int *results)
{
    FileWindow window;
    for (uint32_t i = 0; i < count; ++i) {
        auto &item = window.items[i];
        item = {buffers[i], lengths[i], iovCounts[i], 0};
        if (!PrepareFileRead(keys[i], ownerSlots[i], item)) {
            results[i] = RET_CACHE_EPERM;
            continue;
        }
        char path[PATH_MAX]{};
        const auto rc = ToLocalFilePathResult(resolve(keys[i], ownerSlots[i] - 1U, path, sizeof(path)));
        if (rc != RET_CACHE_OK) {
            results[i] = rc;
            continue;
        }
        const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) {
            results[i] = errno == ENOENT ? RET_CACHE_NOT_FOUND : RET_CACHE_ERROR;
            continue;
        }
        struct stat status{};
        if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode)) {
            close(fd);
            results[i] = RET_CACHE_ERROR;
            continue;
        }
        window.fds[i] = fd;
        window.devices[i] = status.st_dev;
    }
    nds::NdsManager::Instance().ReadLocalFiles(window.items.data(), window.fds.data(), window.devices.data(),
                                               count, window.direct.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (window.fds[i] < 0) {
            continue;
        }
        if (window.direct[i] == UBSIO_KVC_NO_NDS) {
            results[i] = ReadFileViaHost(window.fds[i], window.items[i]);
        } else {
            results[i] = window.direct[i] == UBSIO_KVC_OK ? RET_CACHE_OK : RET_CACHE_ERROR;
        }
    }
}

} // namespace

int32_t KvcInstance::GetInitInfo(UbsioInitInfo &info)
{
    BioConfigInfo config{};
    config.structSize = sizeof(config);
    config.fields = BIO_CONFIG_UNDERFS_TYPE | BIO_CONFIG_NDS_ENABLED;
    if (DlBioSdkApi::GetConfigInfo(&config) != RET_CACHE_OK || config.underFsType >= BIO_UNDERFS_BUTT) {
        return UBSIO_KVC_ERR;
    }

    UbsioInitInfo result{};
    result.underFsType = static_cast<UbsioUnderFsType>(config.underFsType);
    if (config.ndsEnabled != 0 && config.localFileSlot <= MAX_LOCAL_FILE_SLOT) {
        result.slot = config.localFileSlot;
    }
    result.ndsEnable = result.slot != 0;
    info = result;
    return UBSIO_KVC_OK;
}

KvcError KvcInstance::ReadLocalFiles(const char *const *keys, uint32_t count, const uint8_t *ownerSlots,
                                       void *const *const *buffers, const size_t *const *lengths,
                                       const uint32_t *iovCounts, int *results)
{
    const auto resolve = DlBioSdkApi::GetFilePathResolver();
    if (resolve == nullptr) {
        return UBSIO_KVC_NO_NDS;
    }
    std::fill_n(results, count, RET_CACHE_ERROR);
    for (uint32_t i = 0; i < count; i += FILE_READ_WINDOW) {
        ReadFileWindow(resolve, keys + i, ownerSlots + i, buffers + i, lengths + i, iovCounts + i,
                       std::min(FILE_READ_WINDOW, count - i), results + i);
    }
    return UBSIO_KVC_OK;
}

} // namespace ubsio
} // namespace ock
