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

#ifndef UBSIO_NDS_MANAGER_H
#define UBSIO_NDS_MANAGER_H

#include <atomic>
#include <memory>
#include <vector>
#include <unordered_set>
#include "ubsio_kvc.h"
#include <mutex>
#include <shared_mutex>
#include "ubsio_kvc_err.h"
#include "ubsio_nds_dl_api.h"

namespace ock {
namespace ubsio {
namespace nds {

struct LocalFileRead {
    void *const *buffers;
    const size_t *lengths;
    uint32_t iovCount;
    uint64_t valueLen;
};

class NdsManager {
public:
    static NdsManager &Instance() noexcept;

    KvcError Initialize(int device) noexcept;

    KvcError UnInitialize() noexcept;

    KvcError RegisterMemory(const void *addr, size_t length) noexcept;

    KvcError UnRegisterMemory(const void *addr, size_t length) noexcept;

    void ReadLocalFiles(const LocalFileRead *items, const int *fds, const uint64_t *devices,
                        size_t count, int *results);

public:
    NdsManager(const NdsManager&) = delete;
    NdsManager& operator=(const NdsManager&) = delete;
    NdsManager(NdsManager&&) = delete;
    NdsManager& operator=(NdsManager&&) = delete;

private:
    struct IoContextSlot {
        NDSfileBatchHandle_t ctx{ nullptr };
        bool busy{ false };
    };

    struct MemoryRange {
        uintptr_t addr;
        size_t size;
    };

    KvcError InitializeIoContextPool(size_t count) noexcept;
    void DestroyIoContextPool() noexcept;
    IoContextSlot *AcquireIoContext() noexcept;
    void ReleaseIoContext(IoContextSlot *slot) noexcept;
    void RebuildIoContext(IoContextSlot *slot) noexcept;

    void SubmitReads(std::vector<NDSfileIOParams_t> &requests, int *results);

private:
    NdsManager() = default;
    ~NdsManager() noexcept;

private:
    int deviceId{};
    bool ndsInit{ false };
    bool ndsLoaded{ false };
    std::atomic<bool> readsEnabled{ false };
    bool skipMemRegister{ false };
    std::mutex topologyMutex;
    std::unordered_set<uint64_t> registeredDevices;
    std::vector<std::unique_ptr<IoContextSlot>> ioContextPool;
    std::mutex ioContextPoolMutex;
    // Readers keep the group and topology alive through completion.
    std::shared_mutex memoryMutex;
    std::vector<MemoryRange> registeredRanges;
};

}
}
}
#endif // UBSIO_NDS_MANAGER_H
