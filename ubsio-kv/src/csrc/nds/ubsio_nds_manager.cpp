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
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <limits>
#include <new>
#include <vector>
#include "ubsio_kvc_log.h"
#include "dl_biosdk_api.h"
#include "ubsio_nds_manager.h"

using namespace ock::ubsio;
using namespace ock::ubsio::nds;

constexpr int NDS_IO_TIMEOUT_SEC = 30;
constexpr size_t NDS_FILE_ALIGNMENT = 512;

static bool IsDirectUnavailable(int64_t result) noexcept
{
    return result == -EOPNOTSUPP || result == -ENOSYS || result == -ENODEV;
}

static bool NdsEnvEnabled(const char *name) noexcept
{
    const char *value = std::getenv(name);
    return value != nullptr && (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
}

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
    std::unique_lock<std::shared_mutex> lock(memoryMutex);
    if (ndsInit) {
        LOG_INFO("Nds has been initialized, which device id is " << deviceId);
        return UBSIO_KVC_OK;
    }

    BioConfigInfo config{};
    config.structSize = sizeof(config);
    config.fields = BIO_CONFIG_NDS_ENABLED;
    auto configRet = DlBioSdkApi::GetConfigInfo(&config);
    if (configRet != RET_CACHE_OK) {
        LOG_WARN("Cannot query NDS configuration, ret:" << configRet);
        return UBSIO_KVC_ERR;
    }
    if (config.ndsEnabled == 0) {
        return UBSIO_KVC_NO_NDS;
    }
    if (device < 0) {
        LOG_ERROR("Nds get invalid device id " << device);
        return UBSIO_KVC_ERR;
    }
    skipMemRegister = NdsEnvEnabled("UBSIO_NDS_SKIP_MEM_REGISTER");
    if (NdsApi::LoadLibrary() != 0) {
        LOG_WARN("Failed to load libascend_nds.so, disable the NDS feature.");
        return UBSIO_KVC_ERR;
    }
    ndsLoaded = true;
    deviceId = device;

    const int ret = NdsApi::DriverOpen();
    int version = 0;
    if (ret != 0 || NdsApi::GetVersion(&version) != 0 || version != NDS_DRIVER_UAPI_VERSION) {
        LOG_ERROR("Nds initialize failed or version mismatch, device:" << deviceId <<
            ", ret:" << ret << ", version:" << version);
        if (ret == 0) {
            NdsApi::DriverClose();
        }
        NdsApi::CleanupLibrary();
        ndsLoaded = false;
        return UBSIO_KVC_ERR;
    }

    // Close the pooled contexts before the dynamically loaded facade's static objects are destroyed.
    if (InitializeIoContextPool(1) != UBSIO_KVC_OK ||
        std::atexit([] { NdsManager::Instance().UnInitialize(); }) != 0) {
        LOG_ERROR("Nds io context pool initialization or exit registration failed.");
        DestroyIoContextPool();
        NdsApi::DriverClose();
        NdsApi::CleanupLibrary();
        ndsLoaded = false;
        return UBSIO_KVC_ERR;
    }

    ndsInit = true;
    readsEnabled = skipMemRegister;
    if (skipMemRegister) {
        LOG_INFO("NDS uses unregistered scatter-gather reads; memory registration is skipped.");
    }
    LOG_INFO("Nds init success, device:" << deviceId << ", submitMode:batch, maxIoNr:" << NDS_MAX_IO_DEPTH);
    return UBSIO_KVC_OK;
}

KvcError NdsManager::UnInitialize() noexcept
{
    std::unique_lock<std::shared_mutex> lock(memoryMutex);
    DestroyIoContextPool();
    if (ndsInit) {
        registeredDevices.clear();
        NdsApi::DriverClose();
        ndsInit = false;
    }
    if (ndsLoaded) {
        NdsApi::CleanupLibrary();
        ndsLoaded = false;
    }
    registeredRanges.clear();
    readsEnabled = false;
    skipMemRegister = false;
    return UBSIO_KVC_OK;
}

KvcError NdsManager::InitializeIoContextPool(size_t count) noexcept
{
    ioContextPool.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        auto slot = std::unique_ptr<IoContextSlot>(new (std::nothrow) IoContextSlot());
        if (slot == nullptr) {
            DestroyIoContextPool();
            return UBSIO_KVC_ERR;
        }
        const int ret = NdsApi::SetUpBatch(&slot->ctx, NDS_MAX_IO_DEPTH);
        if (ret != 0 || slot->ctx == nullptr) {
            LOG_ERROR("Nds io context pool create failed, index:" << i << ", ret:" << ret);
            if (slot->ctx != nullptr) {
                NdsApi::DestroyBatch(slot->ctx);
            }
            DestroyIoContextPool();
            return UBSIO_KVC_ERR;
        }
        ioContextPool.emplace_back(std::move(slot));
    }
    return UBSIO_KVC_OK;
}

void NdsManager::DestroyIoContextPool() noexcept
{
    std::lock_guard<std::mutex> guard(ioContextPoolMutex);
    for (auto &slot : ioContextPool) {
        if (slot != nullptr && slot->ctx != nullptr) {
            NdsApi::DestroyBatch(slot->ctx);
            slot->ctx = nullptr;
        }
    }
    ioContextPool.clear();
}

NdsManager::IoContextSlot *NdsManager::AcquireIoContext() noexcept
{
    std::unique_lock<std::mutex> lock(ioContextPoolMutex);
    IoContextSlot *selected = nullptr;
    for (auto &slot : ioContextPool) {
        if (slot != nullptr && !slot->busy) {
            slot->busy = true;
            selected = slot.get();
            break;
        }
    }
    if (selected == nullptr) {
        auto slot = std::unique_ptr<IoContextSlot>(new (std::nothrow) IoContextSlot());
        if (slot == nullptr) {
            return nullptr;
        }
        slot->busy = true;
        selected = slot.get();
        ioContextPool.emplace_back(std::move(slot));
    }
    lock.unlock();
    if (selected == nullptr || selected->ctx != nullptr) {
        return selected;
    }

    const int ret = NdsApi::SetUpBatch(&selected->ctx, NDS_MAX_IO_DEPTH);
    if (ret != 0 || selected->ctx == nullptr) {
        LOG_ERROR("Nds io context lazy rebuild failed, ret:" << ret);
        ReleaseIoContext(selected);
        return nullptr;
    }
    return selected;
}

void NdsManager::ReleaseIoContext(IoContextSlot *slot) noexcept
{
    if (slot == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(ioContextPoolMutex);
        slot->busy = false;
    }
}

KvcError NdsManager::RegisterMemory(const void *addr, size_t length) noexcept
{
    std::unique_lock<std::shared_mutex> lock(memoryMutex);
    if (!ndsInit) {
        LOG_ERROR("NDS not initialized, register memory rejected. "
                  "Call UbsioKvCacheInit with ubsio.nds.enabled=true first.");
        return UBSIO_KVC_NO_NDS;
    }
    if (addr == nullptr || length == 0) {
        return UBSIO_KVC_ERR;
    }
    if (skipMemRegister) {
        return UBSIO_KVC_OK;
    }
    const auto address = reinterpret_cast<uintptr_t>(addr);
    if (length > std::numeric_limits<uintptr_t>::max() - address) {
        return UBSIO_KVC_INVALID_PARAM;
    }
    const auto end = address + length;
    for (const auto &range : registeredRanges) {
        if (range.addr == address && range.size == length) {
            return UBSIO_KVC_OK;
        }
        if (address < range.addr + range.size && range.addr < end) {
            LOG_ERROR("NDS memory registration overlaps an existing range");
            return UBSIO_KVC_INVALID_PARAM;
        }
    }
    if (registeredRanges.size() >= NDS_FILE_MAX_MEM_RANGES) {
        LOG_ERROR("NDS memory registration exceeds " << NDS_FILE_MAX_MEM_RANGES << " ranges");
        return UBSIO_KVC_ERR;
    }
    registeredRanges.push_back({address, length});
    const int ret = NdsApi::RegisterMem(const_cast<void *>(addr), length);
    if (ret != 0) {
        registeredRanges.pop_back();
        LOG_ERROR("NDS memory registration failed, length:" << length << ", ret:" << ret);
        return UBSIO_KVC_ERR;
    }
    readsEnabled = true;
    return UBSIO_KVC_OK;
}

KvcError NdsManager::UnRegisterMemory(const void *addr, size_t length) noexcept
{
    std::unique_lock<std::shared_mutex> lock(memoryMutex);
    if (!ndsInit) {
        LOG_ERROR("NDS not initialized, unregister memory rejected.");
        return UBSIO_KVC_NO_NDS;
    }
    if (addr == nullptr || length == 0) {
        return UBSIO_KVC_ERR;
    }
    if (skipMemRegister) {
        return UBSIO_KVC_OK;
    }
    const auto address = reinterpret_cast<uintptr_t>(addr);
    auto entry = std::find_if(registeredRanges.begin(), registeredRanges.end(),
        [address](const MemoryRange &range) { return range.addr == address; });
    if (entry == registeredRanges.end()) {
        return UBSIO_KVC_ERR;
    }
    if (entry->size != length) {
        return UBSIO_KVC_INVALID_PARAM;
    }
    const int ret = NdsApi::UnregisterMem(const_cast<void *>(addr));
    if (ret != 0) {
        LOG_ERROR("NDS memory unregistration failed, length:" << length << ", ret:" << ret);
        return UBSIO_KVC_ERR;
    }
    registeredRanges.erase(entry);
    readsEnabled = !registeredRanges.empty();
    return UBSIO_KVC_OK;
}

void NdsManager::RebuildIoContext(IoContextSlot *slot) noexcept
{
    if (slot->ctx != nullptr) {
        NdsApi::DestroyBatch(slot->ctx);
        slot->ctx = nullptr;
    }
    const int ret = NdsApi::SetUpBatch(&slot->ctx, NDS_MAX_IO_DEPTH);
    if (ret != 0 || slot->ctx == nullptr) {
        LOG_ERROR("Nds io context rebuild failed, ret:" << ret);
        if (slot->ctx != nullptr) {
            NdsApi::DestroyBatch(slot->ctx);
            slot->ctx = nullptr;
        }
    }
}

void NdsManager::SubmitReads(std::vector<NDSfileIOParams_t> &requests, int *results)
{
    const size_t count = requests.size();
    if (count == 0) {
        return;
    }
    std::vector<size_t> cbKeys;
    cbKeys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        cbKeys.push_back(reinterpret_cast<uintptr_t>(requests[i].cookie));
        requests[i].cookie = reinterpret_cast<void *>(i);
    }
    std::vector<bool> completed(count, false);
    // Allocate before leasing a context; requests and buffers stay alive until all
    // accepted operations are reaped, or destroy_ctx drains a failed context.
    const size_t windowCapacity = std::min<size_t>(count, NDS_MAX_IO_DEPTH);
    std::vector<NDSfileIOEvents_t> events(windowCapacity);
    auto *slot = AcquireIoContext();
    if (slot == nullptr || slot->ctx == nullptr) {
        ReleaseIoContext(slot);
        LOG_ERROR("Nds batch io context acquire failed");
        return;
    }

    bool healthy = true;
    const unsigned flags = skipMemRegister ? 0 : NDS_FILE_IO_REGISTERED_MEM;
    size_t submitted = 0;
    size_t inflight = 0;
    while (inflight != 0 || (healthy && submitted < count)) {
        // Bound accepted-but-unreaped IOCBs, not just the size of one submit.
        // After a partial reap, fill the freed slots without draining the ctx.
        if (healthy && submitted < count && inflight < windowCapacity) {
            const int window = static_cast<int>(std::min(count - submitted, windowCapacity - inflight));
            const int accepted = NdsApi::SubmitBatch(slot->ctx, window, &requests[submitted], flags);
            if (IsDirectUnavailable(accepted)) {
                results[cbKeys[submitted]] = UBSIO_KVC_NO_NDS;
                completed[submitted++] = true;
                continue;
            }
            if (accepted < 0 || accepted > window) {
                LOG_ERROR("Nds batch submit failed, expected:" << window << ", ret:" << accepted);
                healthy = false;
                break;  // Destroy drains any operations whose acceptance is uncertain.
            }
            submitted += static_cast<size_t>(accepted);
            inflight += static_cast<size_t>(accepted);
            if (accepted == 0) {
                LOG_ERROR("Nds batch partial submit, expected:" << window << ", ret:" << accepted);
                healthy = false;  // Reap the accepted prefix; do not retry unknown failures.
            }
        }
        if (inflight == 0) {
            break;
        }
        struct timespec timeout{NDS_IO_TIMEOUT_SEC, 0};
        const int reaped = NdsApi::GetBatchEvents(slot->ctx, 1, static_cast<unsigned>(inflight), events.data(), &timeout);
        if (reaped <= 0 || static_cast<size_t>(reaped) > inflight) {
            LOG_ERROR("Nds batch getevents failed or timed out, inflight:" << inflight << ", ret:" << reaped);
            healthy = false;
            break;
        }
        bool invalidEvent = false;
        for (int i = 0; i < reaped; ++i) {
            const auto cookie = reinterpret_cast<uintptr_t>(events[i].cookie);
            if (cookie >= submitted || completed[cookie]) {
                LOG_ERROR("Nds batch completion has invalid or duplicate cb index:" << cookie);
                healthy = false;
                invalidEvent = true;
                continue;
            }
            completed[cookie] = true;
            auto &result = results[cbKeys[cookie]];
            if (events[i].ret == 0) {
                result = UBSIO_KVC_OK;
            } else if (IsDirectUnavailable(events[i].ret)) {
                result = UBSIO_KVC_NO_NDS;
            } else {
                LOG_ERROR("Nds batch completion failed, cb index:" << cookie << ", ret:" << events[i].ret);
                healthy = false;
            }
        }
        if (invalidEvent) {
            break;
        }
        inflight -= static_cast<size_t>(reaped);
    }
    if (!healthy) {
        RebuildIoContext(slot);
    }
    ReleaseIoContext(slot);
}

void NdsManager::ReadLocalFiles(const LocalFileRead *items, const int *fds, const uint64_t *devices,
                                size_t count, int *results)
{
    std::shared_lock<std::shared_mutex> lock(memoryMutex);
    using FileHandle = std::unique_ptr<void, decltype(&NdsApi::DeregisterFileHandle)>;
    std::vector<FileHandle> handles;
    handles.reserve(count);
    std::vector<NDSfileIOVec_t> iovs;
    std::vector<NDSfileIOParams_t> requests;
    requests.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (fds[i] < 0 || results[i] != UBSIO_KVC_OK) {
            continue;
        }
        results[i] = UBSIO_KVC_NO_NDS;
        if (!ndsInit || !readsEnabled.load()) {
            continue;
        }
        const auto &item = items[i];
        bool aligned = true;
        for (uint32_t j = 0; j < item.iovCount; ++j) {
            const auto addr = reinterpret_cast<uintptr_t>(item.buffers[j]);
            const size_t length = item.lengths[j];
            aligned = aligned && ((addr | length) % NDS_FILE_ALIGNMENT == 0);
        }
        if (!aligned) {
            continue;
        }
        {
            std::lock_guard<std::mutex> guard(topologyMutex);
            if (!registeredDevices.count(devices[i])) {
                const NDSfileFsDesc_t fs{&fds[i], 1, 0};
                const int rc = NdsApi::RegisterFs(&fs);
                if (rc != 0) {
                    if (!IsDirectUnavailable(rc)) {
                        results[i] = UBSIO_KVC_ERR;
                    }
                    continue;
                }
                registeredDevices.insert(devices[i]);
            }
        }
        NDSfileHandle_t handle = nullptr;
        const int handleRet = NdsApi::RegisterFileHandle(&handle, fds[i]);
        if (handleRet != 0) {
            LOG_ERROR("Nds file handle registration failed, ret:" << handleRet);
            results[i] = IsDirectUnavailable(handleRet) ? UBSIO_KVC_NO_NDS : UBSIO_KVC_ERR;
            continue;
        }
        handles.emplace_back(handle, NdsApi::DeregisterFileHandle);
        for (uint32_t j = 0; j < item.iovCount; ++j) {
            iovs.push_back({item.buffers[j], item.lengths[j]});
        }
        requests.push_back({nullptr, 0, 0, item.iovCount, handle, NDS_FILE_READ, reinterpret_cast<void *>(i)});
        results[i] = UBSIO_KVC_ERR;
    }
    size_t firstIov = 0;
    for (auto &request : requests) {
        request.bufPtr_bases = iovs.data() + firstIov;
        firstIov += request.iov_count;
    }
    SubmitReads(requests, results);
}
