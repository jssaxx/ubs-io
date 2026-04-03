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

#include <cstdint>
#include "ubsio_kvc_execution.h"

namespace ock {
namespace ubsio {

class KvcInstance {
public:
    static KvcInstance &Instance() noexcept;
    KvcError Initialize(int32_t device) noexcept;
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
        if (m_readExecutor != nullptr) {
            m_readExecutor = nullptr;
        }
    }

private:
    ExecutorServicePtr m_readExecutor{ nullptr };
    int32_t m_deviceId{ -1 };
};

} // namespace ubsio
} // namespace ock
#endif // UBSIO_KVC_INSTANCE_H