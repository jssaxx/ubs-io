/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the License at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "mms_client_ioctx_mgr.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <limits>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "mms_comm.h"
#include "mms_log.h"
#include "mms_mem_common.h"

namespace ock {
namespace mms {

ClientIoCtx::~ClientIoCtx()
{
    if (mrRegistered && netEngine != nullptr) {
        netEngine->DestroyMemoryRegion(mr);
        mrRegistered = false;
    }
    if (address != 0 && totalSize != 0) {
        munmap(reinterpret_cast<void *>(address), totalSize);
        address = 0;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

BResult ClientIoCtxManager::Initialize(const uint16_t numaIds[], uint16_t numaNum, uint64_t sizePerNuma,
                                       const NetEnginePtr &netEngine)
{
    if (numaIds == nullptr || numaNum == 0 || numaNum > MAX_NUMAS_NUM || sizePerNuma == 0 ||
        numaNum > std::numeric_limits<uint64_t>::max() / sizePerNuma || netEngine == nullptr) {
        return MMS_INVALID_PARAM;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mNumaNum = numaNum;
    for (uint16_t index = 0; index < numaNum; ++index) {
        mNumaId[index] = numaIds[index];
    }
    mSizePerNuma = sizePerNuma;
    mGeneration.store(0, std::memory_order_release);
    mNetEngine = netEngine;
    mClients.clear();
    mCreating.clear();
    mStats = ClientIoCtxStats{};
    mStopping = false;
    return MMS_OK;
}

BResult ClientIoCtxManager::Create(uint32_t clientPid, ClientIoCtxPtr &ioCtx)
{
    if (mSizePerNuma == 0 || mNumaNum > std::numeric_limits<uint64_t>::max() / mSizePerNuma) {
        return MMS_INVALID_PARAM;
    }

    auto newCtx = std::make_shared<ClientIoCtx>();
    if (newCtx == nullptr) {
        return MMS_ALLOC_FAIL;
    }
    newCtx->generation = mGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    newCtx->pid = clientPid;
    newCtx->numaNum = mNumaNum;
    newCtx->totalSize = static_cast<uint64_t>(mNumaNum) * mSizePerNuma;
    newCtx->netEngine = mNetEngine;
    uint64_t offset = 0;
    for (uint16_t index = 0; index < mNumaNum; ++index) {
        newCtx->numaId[index] = mNumaId[index];
        newCtx->numaOffset[index] = offset;
        newCtx->numaSize[index] = mSizePerNuma;
        offset += mSizePerNuma;
    }

    std::string name = "mms_client_ioctx_" + std::to_string(clientPid) + "_" +
                       std::to_string(newCtx->generation);
    newCtx->fd = static_cast<int32_t>(syscall(SYS_memfd_create, name.c_str(), MFD_CLOEXEC));
    if (newCtx->fd < 0) {
        LOG_ERROR("Create client ioctx memfd failed, pid:" << clientPid << ", error:" << strerror(errno) << ".");
        return MMS_ALLOC_FAIL;
    }
    if (ftruncate(newCtx->fd, static_cast<off_t>(newCtx->totalSize)) != 0) {
        LOG_ERROR("Resize client ioctx memfd failed, pid:" << clientPid << ", size:" << newCtx->totalSize
                                                           << ", error:" << strerror(errno) << ".");
        return MMS_ALLOC_FAIL;
    }

    void *mapped = mmap(nullptr, newCtx->totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, newCtx->fd, 0);
    if (mapped == MAP_FAILED) {
        LOG_ERROR("Map client ioctx failed, pid:" << clientPid << ", size:" << newCtx->totalSize
                                                  << ", error:" << strerror(errno) << ".");
        return MMS_ALLOC_FAIL;
    }
    newCtx->address = reinterpret_cast<uintptr_t>(mapped);

    if (NumaAvailable() != -1) {
        for (uint16_t index = 0; index < newCtx->numaNum; ++index) {
            uintptr_t numaAddress = newCtx->address + newCtx->numaOffset[index];
            if (BindMemoryToNuma(reinterpret_cast<int *>(numaAddress), newCtx->numaSize[index],
                                newCtx->numaId[index]) < 0) {
                LOG_ERROR("Bind client ioctx to numa failed, pid:" << clientPid
                                                                   << ", numa:" << newCtx->numaId[index] << ".");
                return MMS_ERR;
            }
        }
    }

    auto ret = mNetEngine->RegisterMemoryRegion(newCtx->address, newCtx->totalSize, newCtx->mr);
    if (ret != MMS_OK) {
        LOG_ERROR("Register client ioctx MR failed, pid:" << clientPid << ", ret:" << ret << ".");
        return ret;
    }
    newCtx->mrRegistered = !newCtx->mr.GetHcomMrs().empty();
    if (newCtx->mrRegistered) {
        ret = mNetEngine->GetMemoryKey(newCtx->mr, newCtx->memoryKey);
        if (ret != MMS_OK) {
            LOG_ERROR("Get client ioctx memory key failed, pid:" << clientPid << ", ret:" << ret << ".");
            return ret;
        }
    }
    newCtx->state.store(ClientIoCtxState::ACTIVE, std::memory_order_release);
    ioCtx = std::move(newCtx);
    LOG_INFO("Create client ioctx success, pid:" << clientPid << ", generation:" << ioCtx->generation
                                                 << ", size:" << ioCtx->totalSize << ".");
    return MMS_OK;
}

BResult ClientIoCtxManager::Acquire(uint32_t clientPid, ClientIoCtxPtr &ioCtx)
{
    if (clientPid == 0) {
        return MMS_INVALID_PARAM;
    }

    std::shared_ptr<CreateSlot> slot;
    bool creator = false;
    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mStopping) {
            return MMS_NOT_READY;
        }
        auto iter = mClients.find(clientPid);
        if (iter != mClients.end() &&
            iter->second->state.load(std::memory_order_acquire) == ClientIoCtxState::ACTIVE) {
            ioCtx = iter->second;
            return MMS_OK;
        }

        auto creating = mCreating.find(clientPid);
        if (creating == mCreating.end()) {
            slot = std::make_shared<CreateSlot>();
            if (slot == nullptr) {
                return MMS_ALLOC_FAIL;
            }
            mCreating.emplace(clientPid, slot);
            creator = true;
        } else {
            slot = creating->second;
            ++mStats.createWaitTotal;
        }

        if (!creator) {
            slot->condition.wait(lock, [this, &slot]() { return slot->done || mStopping; });
            if (!slot->done) {
                return MMS_NOT_READY;
            }
            ioCtx = slot->ioCtx;
            return slot->result;
        }
    }

    auto start = std::chrono::steady_clock::now();
    ClientIoCtxPtr created;
    BResult ret = Create(clientPid, created);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();

    std::unique_lock<std::mutex> lock(mMutex);
    if (mStopping && ret == MMS_OK) {
        lock.unlock();
        created.reset();
        ret = MMS_NOT_READY;
        lock.lock();
    }

    ++mStats.createAttemptTotal;
    mStats.createLatencyTotalUs += static_cast<uint64_t>(elapsed);
    mStats.createLatencyMaxUs = std::max(mStats.createLatencyMaxUs, static_cast<uint64_t>(elapsed));
    if (ret == MMS_OK) {
        ++mStats.createdTotal;
        mClients[clientPid] = created;
        ioCtx = created;
        mStats.activeCapacity += created->totalSize;
        mStats.activeClients = static_cast<uint32_t>(mClients.size());
        mStats.peakClients = std::max(mStats.peakClients, mStats.activeClients);
        mStats.peakCapacity = std::max(mStats.peakCapacity, mStats.activeCapacity);
    } else {
        ++mStats.createFailedTotal;
    }

    slot->ioCtx = ioCtx;
    slot->result = ret;
    slot->done = true;
    mCreating.erase(clientPid);
    lock.unlock();
    slot->condition.notify_all();
    mCreateCondition.notify_all();
    return ret;
}

bool ClientIoCtxManager::Contains(const ClientIoCtx &ioCtx, uint64_t offset, uint64_t length)
{
    if (length == 0 || offset > std::numeric_limits<uint64_t>::max() - length ||
        ioCtx.state.load(std::memory_order_acquire) != ClientIoCtxState::ACTIVE) {
        return false;
    }
    for (uint16_t index = 0; index < ioCtx.numaNum; ++index) {
        uint64_t begin = ioCtx.numaOffset[index];
        uint64_t size = ioCtx.numaSize[index];
        if (offset >= begin && offset - begin < size && length <= size - (offset - begin)) {
            return true;
        }
    }
    return false;
}

BResult ClientIoCtxManager::Resolve(uint32_t clientPid, uint64_t generation, uint64_t offset, uint64_t length,
                                    ClientIoCtxPtr &ioCtx, uintptr_t &address) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = mClients.find(clientPid);
    if (iter == mClients.end() || generation == 0 || iter->second->generation != generation ||
        !Contains(*iter->second, offset, length)) {
        return MMS_INVALID_PARAM;
    }
    ioCtx = iter->second;
    address = ioCtx->address + offset;
    return MMS_OK;
}

std::vector<ClientIoCtxInfo> ClientIoCtxManager::GetInfos() const
{
    std::vector<ClientIoCtxInfo> infos;
    std::lock_guard<std::mutex> lock(mMutex);
    infos.reserve(mClients.size());
    for (const auto &item : mClients) {
        const auto &ioCtx = item.second;
        ClientIoCtxInfo info;
        info.generation = ioCtx->generation;
        info.totalSize = ioCtx->totalSize;
        info.pid = ioCtx->pid;
        info.state = ioCtx->state.load(std::memory_order_acquire);
        infos.push_back(info);
    }
    return infos;
}

ClientIoCtxStats ClientIoCtxManager::GetStats() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    ClientIoCtxStats stats = mStats;
    stats.activeClients = static_cast<uint32_t>(mClients.size());
    stats.creatingClients = static_cast<uint32_t>(mCreating.size());
    return stats;
}

void ClientIoCtxManager::Release(uint32_t clientPid)
{
    ClientIoCtxPtr ioCtx;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto iter = mClients.find(clientPid);
        if (iter == mClients.end()) {
            return;
        }
        ioCtx = iter->second;
        ioCtx->state.store(ClientIoCtxState::DRAINING, std::memory_order_release);
        mClients.erase(iter);
        ++mStats.releasedTotal;
        mStats.activeClients = static_cast<uint32_t>(mClients.size());
        mStats.activeCapacity -= ioCtx->totalSize;
    }
    LOG_INFO("Release client ioctx, pid:" << clientPid << ", generation:" << ioCtx->generation << ".");
}

void ClientIoCtxManager::Exit()
{
    std::unordered_map<uint32_t, ClientIoCtxPtr> clients;
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mStopping = true;
        mCreateCondition.wait(lock, [this]() { return mCreating.empty(); });
        clients.swap(mClients);
        mStats.releasedTotal += clients.size();
        mStats.activeClients = 0;
        mStats.activeCapacity = 0;
        mNetEngine = nullptr;
    }
    for (auto &item : clients) {
        item.second->state.store(ClientIoCtxState::DRAINING, std::memory_order_release);
    }
}

}
}
