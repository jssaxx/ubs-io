/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UBSIO_NDS_MANAGER_H
#define UBSIO_NDS_MANAGER_H

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include "bio_c.h"
#include "nds_file.h"
#include "ubsio_kvc_err.h"
#include "ubsio_kvc_execution.h"

namespace ock {
namespace ubsio {
namespace nds {

struct TaskInfo {
    std::string path;
    void *buffer;
    uint64_t size;
    off_t bufferOffset;
    off_t fileOffset;
};

struct TaskResults {
    uint32_t total{ 0UL };
    std::atomic<uint32_t> succeed{ 0UL };
    std::atomic<uint32_t> failed{ 0UL };

    void WaitFinish()
    {
        while (total > (succeed.load(std::memory_order_relaxed) + failed.load(std::memory_order_relaxed))) {
            std::this_thread::yield();
        }
    }
};

class DfcNdsManager {
public:
    static DfcNdsManager &Instance() noexcept;

    DFCError Initialize(int device) noexcept;

    DFCError UnInitialize() noexcept;

    DFCError RegisterMemory(const void *addr, size_t length) noexcept;

    DFCError UnRegisterMemory(const void *addr, size_t length) noexcept;

    DFCError DirectRead(const std::string &key,
                        const std::vector<uintptr_t> &buffers,
                        const std::vector<size_t> &sizes) noexcept;

    DFCError BatchDirectRead(const std::vector<std::string> &keys,
                             const std::vector<std::vector<uintptr_t>> &buffers,
                             const std::vector<std::vector<size_t>> &sizes) noexcept;

public:
    DfcNdsManager(const DfcNdsManager&) = delete;
    DfcNdsManager& operator=(const DfcNdsManager&) = delete;
    DfcNdsManager(DfcNdsManager&&) = delete;
    DfcNdsManager& operator=(DfcNdsManager&&) = delete;

private:
    ssize_t SingleRead(const KeyAddrInfo &addrInfo,
                       const std::vector<uintptr_t> &buffers,
                       const std::vector<size_t> &sizes,
                       TaskResults &taskResults) noexcept;

    ssize_t IOURingSingleRead(const KeyAddrInfo &addrInfo,
                              const std::vector<uintptr_t> &buffers,
                              const std::vector<size_t> &sizes) noexcept;

private:
    DfcNdsManager() = default;
    ~DfcNdsManager() noexcept;

private:
    int deviceId{};
    bool ndsInit{ false };
    bool ndsNormal{ false };
    bool useIOURing{ false };
    std::unordered_map<std::string, nds_fileid_t> diskFdMap;
    ExecutorServicePtr ndsReadPool{ nullptr };
};

}
}
}
#endif // UBSIO_NDS_MANAGER_H