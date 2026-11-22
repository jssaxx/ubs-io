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

#ifndef BOOSTIO_LOCAL_FILE_PACK_H
#define BOOSTIO_LOCAL_FILE_PACK_H

#include <cstdint>
#include <cstddef>
#include <ctime>
#include <string>
#include "file_system.h"

namespace ock {
namespace bio {
constexpr size_t LOCAL_FILE_FOOTER_SIZE = 10;
constexpr size_t LOCAL_FILE_MAX_KEY_SIZE = 255;
constexpr uint64_t LOCAL_FILE_MAX_VALUE_SIZE = UINT32_MAX;
constexpr size_t LOCAL_FILE_MAX_METADATA_SIZE = LOCAL_FILE_MAX_KEY_SIZE + LOCAL_FILE_FOOTER_SIZE;

class LocalFilePack {
public:
    LocalFilePack() = delete;

    static BResult Serialize(const char *key, uint64_t valueSize, char *metadata, size_t &metadataSize) noexcept;

    static BResult Deserialize(const char *metadata, size_t metadataSize, std::string &key,
        uint64_t &valueSize) noexcept;

private:
    static void EncodeInteger(char *target, uint64_t value, size_t bytes) noexcept;
    static uint64_t DecodeInteger(const char *source, size_t bytes) noexcept;
};
}
}
#endif // BOOSTIO_LOCAL_FILE_PACK_H
