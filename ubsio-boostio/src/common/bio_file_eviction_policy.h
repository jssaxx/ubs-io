/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef BOOSTIO_BIO_FILE_EVICTION_POLICY_H
#define BOOSTIO_BIO_FILE_EVICTION_POLICY_H

#include <cstdint>
#include <cstring>
#include "bio_def.h"

namespace ock {
namespace bio {
enum class FileEvictionPolicyType : uint32_t {
    LRU = 0,
    INVALID = 2,
};

inline FileEvictionPolicyType ParseFileEvictionPolicy(const char *name) noexcept
{
    if (UNLIKELY(name == nullptr)) {
        return FileEvictionPolicyType::INVALID;
    }
    if (std::strcmp(name, "lru") == 0) {
        return FileEvictionPolicyType::LRU;
    }
    return FileEvictionPolicyType::INVALID;
}

inline const char *FileEvictionPolicyName(FileEvictionPolicyType type) noexcept
{
    switch (type) {
        case FileEvictionPolicyType::LRU:
            return "lru";
        default:
            return nullptr;
    }
}
}
}

#endif // BOOSTIO_BIO_FILE_EVICTION_POLICY_H
