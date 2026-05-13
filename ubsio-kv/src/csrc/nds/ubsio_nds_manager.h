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

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include "bio_c.h"
#include "ubsio_kvc_err.h"
#include "ubsio_kvc_execution.h"
#include "ubsio_nds_dl_api.h"

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

class NdsManager {
public:
    static NdsManager &Instance() noexcept;

    KvcError Initialize(int device) noexcept;

    KvcError UnInitialize() noexcept;

    KvcError RegisterMemory(const void *addr, size_t length) noexcept;

    KvcError UnRegisterMemory(const void *addr, size_t length) noexcept;

    KvcError DirectRead(const std::string &key,
                        const std::vector<uintptr_t> &buffers,
                        const std::vector<size_t> &sizes) noexcept;

    KvcError BatchDirectRead(const std::vector<std::string> &keys,
                             const std::vector<std::vector<uintptr_t>> &buffers,
                             const std::vector<std::vector<size_t>> &sizes) noexcept;

public:
    NdsManager(const NdsManager&) = delete;
    NdsManager& operator=(const NdsManager&) = delete;
    NdsManager(NdsManager&&) = delete;
    NdsManager& operator=(NdsManager&&) = delete;

private:
    ssize_t SingleRead(const KeyAddrInfo &addrInfo,
                       const std::vector<uintptr_t> &buffers,
                       const std::vector<size_t> &sizes,
                       TaskResults &taskResults) noexcept;

    ssize_t IOURingSingleRead(const KeyAddrInfo &addrInfo,
                              const std::vector<uintptr_t> &buffers,
                              const std::vector<size_t> &sizes) noexcept;

private:
    NdsManager() = default;
    ~NdsManager() noexcept;

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