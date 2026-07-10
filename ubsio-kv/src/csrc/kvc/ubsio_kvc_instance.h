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

#ifndef UBSIO_KVC_INSTANCE_H
#define UBSIO_KVC_INSTANCE_H

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>
#include "ubsio_kvc_execution.h"

namespace ock {
namespace ubsio {

class KvcInstance {
public:
    static KvcInstance &Instance() noexcept;
    KvcError Initialize(int32_t device) noexcept;
    void UnInitialize() noexcept;
    KvcError Read(const std::vector<std::string> &keyVector,
                  std::vector<std::vector<uintptr_t>> &npuAddrsVector,
                  const std::vector<std::vector<size_t>> &lengthsVector,
                  int *results) noexcept;

public:
    KvcInstance(const KvcInstance&) = delete;
    KvcInstance& operator=(const KvcInstance&) = delete;
    KvcInstance(KvcInstance&&) = delete;
    KvcInstance& operator=(KvcInstance&&) = delete;

private:
    KvcInstance() = default;

    ~KvcInstance()
    {
        UnInitialize();
    }

    void FreeDramAddrs(std::vector<void *> dramAddrsVector, uint32_t keysCount) noexcept;

private:
    ExecutorServicePtr m_readExecutor{ nullptr };
    int32_t m_deviceId{ -1 };
    std::mutex m_freeMutex;
    std::condition_variable m_freeCv;
    uint64_t m_pendingFreeTasks{ 0 };
    bool m_stoppingFreeTasks{ false };
};

} // namespace ubsio
} // namespace ock
#endif // UBSIO_KVC_INSTANCE_H
