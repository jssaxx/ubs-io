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
    static KvcInstance &Instance();
    KvcError Initialize(int32_t device);
    KvcError Read(const std::vector<std::string> &keyVector,
                  std::vector<std::vector<uintptr_t>> &npuAddrsVector,
                  const std::vector<std::vector<size_t>> &lengthsVector,
                  int *results);
    int32_t inline GetDeviceId() const { return m_deviceId; }

public:
    KvcInstance(const KvcInstance&) = delete;
    KvcInstance& operator=(const KvcInstance&) = delete;
    KvcInstance(KvcInstance&&) = delete;
    KvcInstance& operator=(KvcInstance&&) = delete;

private:
    struct H2DParams {
        H2DParams() = delete;
        H2DParams(std::vector<std::vector<uintptr_t>>& npuAddrVec,
                  std::vector<std::vector<size_t>>& lengthVec,
                  std::vector<std::vector<void *>>& hostAddrVec)
                  : npuAddrs(npuAddrVec), lengths(lengthVec), hostAddrs(hostAddrVec) {}
        std::vector<std::vector<uintptr_t>>& npuAddrs;
        std::vector<std::vector<size_t>>& lengths;
        std::vector<std::vector<void *>>& hostAddrs;
    };

    struct ReadParams {
        ReadParams() = delete;
        ReadParams(uint32_t count) {
            keys.reserve(count);
            npuAddrs.reserve(count);
            lengths.reserve(count);
            oriIndex.reserve(count);
        }
        std::vector<std::string> keys;
        std::vector<std::vector<uintptr_t>> npuAddrs;
        std::vector<std::vector<size_t>> lengths;
        std::vector<uint32_t> oriIndex;
    };
    KvcInstance() = default;

    ~KvcInstance()
    {
        if (m_readExecutor != nullptr) {
            m_readExecutor = nullptr;
        }
    }
    KvcError ReadLocal(const std::vector<std::string> &keyVector,
                  std::vector<std::vector<uintptr_t>> &npuAddrsVector,
                  const std::vector<std::vector<size_t>> &lengthsVector,
                  int *results);
    
    KvcError ReadRemote(const std::vector<std::string> &keyVector,
                  std::vector<std::vector<uintptr_t>> &npuAddrsVector,
                  const std::vector<std::vector<size_t>> &lengthsVector,
                  int *results);

    KvcError CopyDataH2D(H2DParams &params, std::vector<int32_t> &batchResult,
                         const std::vector<uint32_t> &origIndex, int *results);

private:
    ExecutorServicePtr m_readExecutor{ nullptr };
    int32_t m_deviceId{ -1 };
};

} // namespace ubsio
} // namespace ock
#endif // UBSIO_KVC_INSTANCE_H