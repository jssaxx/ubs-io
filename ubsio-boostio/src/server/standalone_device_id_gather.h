/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef STANDALONE_DEVICE_ID_GATHER_H
#define STANDALONE_DEVICE_ID_GATHER_H

#include <cstdint>
#include <string>

#include "bio_err.h"

namespace ock {
namespace bio {

class StandaloneDeviceIdGather {
public:
    static constexpr uint64_t DEFAULT_TIMEOUT_MS = 180000;

    // Gather the logic device IDs of all standalone processes and return this
    // process's position in the ascending, duplicate-free ID list.
    static BResult Gather(uint32_t logicDeviceId, uint32_t deviceCount, uint32_t &virtualDeviceIndex,
        uint64_t timeoutMs = DEFAULT_TIMEOUT_MS);

private:
    static BResult BuildShmName(std::string &shmName);
};

}
}

#endif // STANDALONE_DEVICE_ID_GATHER_H
