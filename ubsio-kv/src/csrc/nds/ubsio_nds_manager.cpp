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

#include <fcntl.h>
#include <algorithm>
#include <random>
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_types.h"
#include "ubsio_kvc_str_util.h"
#include "dl_biosdk_api.h"
#include "ubsio_nds_manager.h"
#include "ubsio_nds_dl_api.h"

using namespace ock::ubsio;
using namespace ock::ubsio::nds;

constexpr int MAX_LAYER_NUM = 255;
constexpr int MAX_BATCH_SIZE = 512;
constexpr int MAX_KEY_LEN = 256;
constexpr int MAX_THREAD_CNT = 256;
constexpr int DEFAULT_THREAD_CNT = 32;
constexpr int TALENT_ID = 1;

NdsManager &NdsManager::Instance() noexcept
{
    static NdsManager instance;
    return instance;
}

NdsManager::~NdsManager() noexcept
{
    this->UnInitialize();
}

KvcError NdsManager::Initialize(int device) noexcept
{
    if (ndsInit) {
        LOG_INFO("Nds has been initialized, which device id is " << deviceId);
        return UBSIO_KVC_OK;
    }

    char *bioDisks = std::getenv("UBSIO_BIO_DISKS");
    if (bioDisks == nullptr || *bioDisks == '\0') {
        LOG_WARN("Nds env UBSIO_BIO_DISKS not set or empty, disable the NDS feature.");
        return UBSIO_KVC_ERR;
    }

    if (device < 0) {
        LOG_ERROR("Nds get invalid device id " << device);
        return UBSIO_KVC_ERR;
    }

    // Load libnds_file.so dynamically
    if (!g_ndsDlApi.Load()) {
        LOG_WARN("Failed to load libnds_file.so, disable the NDS feature.");
        return UBSIO_KVC_ERR;
    }

    deviceId = device;
    useIOURing = std::getenv("UBSIO_USE_IO_URING") != nullptr;
    int ret = 0;
    if (useIOURing) {
        if (!g_ndsDlApi.nds_init_async) {
            LOG_ERROR("nds_init_async not available");
            return UBSIO_KVC_ERR;
        }
        ret = g_ndsDlApi.nds_init_async(deviceId);
    } else {
        if (!g_ndsDlApi.nds_init) {
            LOG_ERROR("nds_init not available");
            return UBSIO_KVC_ERR;
        }
        ret = g_ndsDlApi.nds_init(deviceId);
    }
    if (ret < 0) {
        LOG_ERROR("Nds initialize failed with device " << deviceId);
        return UBSIO_KVC_ERR;
    }

    std::set<std::string> diskPaths;
    StrUtil::Split(bioDisks, ",", diskPaths);

    if (!g_ndsDlApi.nds_open) {
        LOG_ERROR("nds_open not available");
        return UBSIO_KVC_ERR;
    }
    for (const auto &diskPath: diskPaths) {
        int fd = g_ndsDlApi.nds_open(diskPath.c_str(), O_RDONLY | O_DIRECT);
        if (fd < 0) {
            LOG_ERROR("Nds open failed with device " << deviceId << ", errno: " << errno << ":" << strerror(errno));
            return UBSIO_KVC_ERR;
        }
        diskFdMap[diskPath] = { fd, deviceId };
    }

    long threadCnt = DEFAULT_THREAD_CNT;
    char *threadEnv = std::getenv("UBSIO_NDS_READ_THREAD");
    if (threadEnv != nullptr) {
        auto threadCntValid = StrUtil::StrToLong(threadEnv, threadCnt);
        if (!threadCntValid || threadCnt <= 0 || threadCnt > MAX_THREAD_CNT) {
            LOG_ERROR("Nds env is invalid, UBSIO_NDS_READ_THREAD range is (0, " << MAX_THREAD_CNT << "]");
            return UBSIO_KVC_ERR;
        }
    }

    threadCnt = useIOURing ? 1L : threadCnt;
    ndsReadPool = ExecutorService::Create(threadCnt);
    if (UNLIKELY(ndsReadPool == nullptr)) {
        LOG_ERROR("Nds thread pool init failed.");
        return UBSIO_KVC_ERR;
    }
    auto success = ndsReadPool->Start();
    if (UNLIKELY(!success)) {
        LOG_ERROR("Nds thread pool start failed.");
        ndsReadPool = nullptr;
        return UBSIO_KVC_ERR;
    }

    ndsInit = true;
    LOG_INFO("Nds init success, use ioURing:" << (useIOURing ? "true" : "false") << ", threadCnt:" << threadCnt);
    return UBSIO_KVC_OK;
}

KvcError NdsManager::UnInitialize() noexcept
{
    if (g_ndsDlApi.nds_uninit) {
        if (g_ndsDlApi.nds_uninit() != 0) {
            LOG_ERROR("Nds unInitialize failed.");
            return UBSIO_KVC_ERR;
        }
    }
    for (const auto &[diskPath, fid]: diskFdMap) {
        close(fid.fd);
    }
    diskFdMap.clear();
    if (ndsReadPool.Get() != nullptr) {
        ndsReadPool = nullptr;
    }
    g_ndsDlApi.Unload();
    return UBSIO_KVC_OK;
}

KvcError NdsManager::RegisterMemory(const void *addr, size_t length) noexcept
{
    if (!g_ndsDlApi.nds_regmem) {
        LOG_ERROR("nds_regmem not available");
        return UBSIO_KVC_ERR;
    }
    for (const auto &[diskPath, fid]: diskFdMap) {
        int ret = g_ndsDlApi.nds_regmem(fid, addr, length);
        if (ret < 0) {
            LOG_ERROR("Nds register memory failed with device " << fid.deviceID << ", length " << length);
            return UBSIO_KVC_ERR;
        }
    }
    ndsNormal = true;
    return UBSIO_KVC_OK;
}

KvcError NdsManager::UnRegisterMemory(const void *addr, size_t length) noexcept
{
    if (!g_ndsDlApi.nds_unregmem) {
        LOG_ERROR("nds_unregmem not available");
        return UBSIO_KVC_ERR;
    }
    for (const auto &[diskPath, fid]: diskFdMap) {
        if (g_ndsDlApi.nds_unregmem(fid, addr, length) < 0) {
            LOG_ERROR("Nds unregister memory failed.");
            return UBSIO_KVC_ERR;
        }
    }
    return UBSIO_KVC_OK;
}

ssize_t NdsManager::SingleRead(const KeyAddrInfo &addrInfo,
                                  const std::vector<uintptr_t> &buffers,
                                  const std::vector<size_t> &sizes,
                                  TaskResults &taskResults) noexcept
{
    auto diskPath = addrInfo.path;
    auto fileOffsets = addrInfo.offset;
    auto readLens = addrInfo.length;
    auto blkCnt = addrInfo.count;
    if (UNLIKELY(addrInfo.result != RET_CACHE_OK)) {
        LOG_ERROR("Bio get keyAddrInfo failed, ret: " << addrInfo.result);
        return -1;
    }
    if (UNLIKELY(diskFdMap.find(diskPath) == diskFdMap.end())) {
        LOG_ERROR("Bio get invalid disk path: " << diskPath << ", env UBSIO_BIO_DISKS may not be set correctly.");
        return -1;
    }

    auto blkIdx = 0;
    auto recordReadSize = 0UL;
    auto bioBlkLeftLen = readLens[blkIdx];
    auto fileOffset = fileOffsets[blkIdx];
    for (int i = 0; i < buffers.size(); ++i) {
        auto leftReadBytes = sizes[i];
        auto bufferOffset = 0UL;
        while (leftReadBytes > 0) {
            auto needReadBytes = std::min(bioBlkLeftLen, leftReadBytes);
            TaskInfo info {
                diskPath,
                reinterpret_cast<void*>(buffers[i]),
                needReadBytes,
                static_cast<off_t>(bufferOffset),
                static_cast<off_t>(fileOffset)
            };
            ndsReadPool->Execute([this, info, &taskResults]() -> void {
                if (!g_ndsDlApi.nds_read) {
                    LOG_ERROR("nds_read not available");
                    taskResults.failed.fetch_add(1UL);
                    return;
                }
                auto readBytes = g_ndsDlApi.nds_read(diskFdMap[info.path], info.buffer,
                    info.bufferOffset, info.size, info.fileOffset);
                if (UNLIKELY(readBytes < 0 || readBytes != info.size)) {
                    taskResults.failed.fetch_add(1UL);
                } else {
                    taskResults.succeed.fetch_add(1UL);
                }
            });
            fileOffset += needReadBytes;
            bufferOffset += static_cast<ssize_t>(needReadBytes);
            leftReadBytes -= needReadBytes;
            bioBlkLeftLen -= needReadBytes;
            recordReadSize += needReadBytes;
            taskResults.total++;
            if (bioBlkLeftLen == 0 && blkIdx < blkCnt - 1) {
                blkIdx++;
                bioBlkLeftLen = readLens[blkIdx];
                fileOffset = fileOffsets[blkIdx];
            }
        }
    }
    return static_cast<ssize_t>(recordReadSize);
}

ssize_t NdsManager::IOURingSingleRead(const KeyAddrInfo &addrInfo,
                                         const std::vector<uintptr_t> &buffers,
                                         const std::vector<size_t> &sizes) noexcept
{
    auto diskPath = addrInfo.path;
    auto fileOffsets = addrInfo.offset;
    auto readLens = addrInfo.length;
    auto blkCnt = addrInfo.count;
    if (UNLIKELY(addrInfo.result != RET_CACHE_OK)) {
        LOG_ERROR("Bio get keyAddrInfo failed, ret: " << addrInfo.result);
        return -1;
    }
    if (UNLIKELY(diskFdMap.find(diskPath) == diskFdMap.end())) {
        LOG_ERROR("Bio get invalid disk path: " << diskPath << ", env UBSIO_BIO_DISKS may not be set correctly.");
        return -1;
    }

    auto blkIdx = 0;
    auto totalReadBytes = 0UL;
    auto iovecReadBytes = 0UL;
    auto bioBlkLeftLen = readLens[blkIdx];
    auto fileOffset = fileOffsets[blkIdx];

    iovec vecs[IO_URING_MAX_DEPTH];
    memset(vecs, 0, sizeof(vecs));
    uint32_t count = 0;
    for (int i = 0; i < buffers.size(); ++i) {
        auto leftReadBytes = sizes[i];
        auto bufferOffset = 0UL;
        while (leftReadBytes > 0) {
            auto needReadBytes = std::min(bioBlkLeftLen, leftReadBytes);
            vecs[count++] = { reinterpret_cast<void*>(buffers[i] + bufferOffset), needReadBytes };
            bufferOffset += static_cast<ssize_t>(needReadBytes);
            leftReadBytes -= needReadBytes;
            bioBlkLeftLen -= needReadBytes;
            iovecReadBytes += needReadBytes;
            totalReadBytes += needReadBytes;

            if (bioBlkLeftLen == 0 && blkIdx < blkCnt - 1) {
                if (!g_ndsDlApi.nds_readv_batch) {
                    LOG_ERROR("nds_readv_batch not available");
                    return -1;
                }
                auto readRet = g_ndsDlApi.nds_readv_batch(diskFdMap[diskPath], vecs, count, static_cast<off_t>(fileOffset));
                UBSIO_KVC_ASSERT_RETURN(readRet >= 0, -1);
                blkIdx++;
                bioBlkLeftLen = readLens[blkIdx];
                fileOffset = fileOffsets[blkIdx];
                iovecReadBytes = 0UL;
                memset(vecs, 0, sizeof(vecs));
                count = 0;
            }

            if (count == IO_URING_MAX_DEPTH - 1) {
                if (!g_ndsDlApi.nds_readv_batch) {
                    LOG_ERROR("nds_readv_batch not available");
                    return -1;
                }
                auto readRet = g_ndsDlApi.nds_readv_batch(diskFdMap[diskPath], vecs, count, static_cast<off_t>(fileOffset));
                UBSIO_KVC_ASSERT_RETURN(readRet >= 0, -1);
                fileOffset += iovecReadBytes;
                iovecReadBytes = 0UL;
                memset(vecs, 0, sizeof(vecs));
                count = 0;
            }
        }
    }
    return static_cast<ssize_t>(totalReadBytes);
}

KvcError NdsManager::DirectRead(const std::string &key,
                                   const std::vector<uintptr_t> &buffers,
                                   const std::vector<size_t> &sizes) noexcept
{
    if (!ndsInit || !ndsNormal) {
        return UBSIO_KVC_NO_NDS;
    }
    if (UNLIKELY(key.empty() || key.length() > MAX_KEY_LEN)) {
        LOG_ERROR("Invalid param, key's len (" << key.length() << ") is not between 1 and " << MAX_KEY_LEN);
        return UBSIO_KVC_ERR;
    }

    auto layerCnt = buffers.size();
    if (UNLIKELY(layerCnt == 0 || layerCnt > MAX_LAYER_NUM)) {
        LOG_ERROR("Layer number is 0 or exceeds the limit of " << MAX_LAYER_NUM);
        return UBSIO_KVC_ERR;
    }

    if (UNLIKELY(sizes.size() != layerCnt)) {
        LOG_ERROR("Unmatched number of layers, offsets and sizes");
        return UBSIO_KVC_ERR;
    }

    std::vector<const char*> cKeys = { key.c_str() };
    std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
    CResult status = DlBioSdkApi::CalcLocation(
        TALENT_ID, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
    if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return UBSIO_KVC_ERR;
    }
    KeyAddrInfo addrInfo{};
    auto ret = DlBioSdkApi::BatchGetKeyDiskAddr(1, cKeys.data(), location.get(), 1, &addrInfo);
    if (UNLIKELY(ret != 0 || addrInfo.result != RET_CACHE_OK)) {
        LOG_ERROR("Bio get disk addr info failed, key@" << key << ", ret: " << ret);
        return UBSIO_KVC_ERR;
    }

    TaskResults results{};
    auto recordReadSize = SingleRead(addrInfo, buffers, sizes, results);
    if (UNLIKELY(recordReadSize < 0)) {
        LOG_ERROR("Nds read key@" << key << " failed, device: " << deviceId);
        return UBSIO_KVC_ERR;
    }
    results.WaitFinish();
    if (UNLIKELY(results.failed.load(std::memory_order_relaxed) > 0)) {
        LOG_ERROR("Nds batch read failed.");
        return UBSIO_KVC_ERR;
    }
    return UBSIO_KVC_OK;
}

KvcError NdsManager::BatchDirectRead(const std::vector<std::string> &keys,  
                                        const std::vector<std::vector<uintptr_t>> &buffers,
                                        const std::vector<std::vector<size_t>> &sizes) noexcept
{
    if (!ndsInit || !ndsNormal) {
        return UBSIO_KVC_NO_NDS;
    }
    const size_t batchSize = keys.size();
    if (UNLIKELY(batchSize == 0 || batchSize > MAX_BATCH_SIZE)) {
        LOG_ERROR("Batch size is 0 or exceeds the limit of " << MAX_BATCH_SIZE);
        return UBSIO_KVC_ERR;
    }

    std::vector<const char*> cKeys;
    std::vector<ObjLocation> locationVec;
    std::vector<KeyAddrInfo> addrInfo(batchSize);
    cKeys.reserve(batchSize);
    locationVec.reserve(batchSize);
    for (const auto &key : keys) {
        std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
        CResult status = DlBioSdkApi::CalcLocation(
            TALENT_ID, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
        if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
            LOG_ERROR("CalcLocation failed with returned status " << status);
            return UBSIO_KVC_ERR;
        }
        locationVec.emplace_back(*location);
        cKeys.emplace_back(key.c_str());
    }
    auto ret = DlBioSdkApi::BatchGetKeyDiskAddr(
        TALENT_ID, cKeys.data(), locationVec.data(), batchSize, addrInfo.data());
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Bio get key disk addr info failed, ret: " << ret);
        return UBSIO_KVC_ERR;
    }

    TaskResults results{};
    uint64_t recordReadSize = 0UL;
    for (int i = 0; i < batchSize; i++) {
        if (useIOURing) {
            recordReadSize = IOURingSingleRead(addrInfo[i], buffers[i], sizes[i]);
        } else {
            recordReadSize = SingleRead(addrInfo[i], buffers[i], sizes[i], results);
        }
        if (UNLIKELY(recordReadSize < 0)) {
            LOG_ERROR("Nds read key@" << keys[i] << " failed, device: " << deviceId);
            if (!useIOURing) {
                results.WaitFinish();
            }
            return UBSIO_KVC_ERR;
        }
    }
    if (!useIOURing) {
        results.WaitFinish();
        if (UNLIKELY(results.failed.load(std::memory_order_relaxed) > 0)) {
            LOG_ERROR("Nds batch read failed.");
            return UBSIO_KVC_ERR;
        }
    }

    return UBSIO_KVC_OK;
}
