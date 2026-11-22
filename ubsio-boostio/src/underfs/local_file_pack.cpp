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

/*
 * Local file layout (V = value_len, K = key_len; lengths and offsets are bytes):
 *
 *   0                  V                  V + K                  V + K + 11
 *   +------------------+------------------+----------------------+
 *   | value (V bytes)  | key (K bytes)    | footer (11 bytes)    |
 *   +------------------+------------------+----------------------+
 *
 * Footer layout (offsets relative to V + K):
 *
 *   0                      5                      7                      11
 *   +----------------------+----------------------+----------------------+
 *   | magic (5 bytes)      | key_len (2 bytes)    | value_len (4 bytes)  |
 *   +----------------------+----------------------+----------------------+
 *
 * magic is ASCII "UBSIO"; key is the complete key without a terminating NUL.
 * key_len is uint16 and value_len is uint32, encoded little-endian.
 * All fields are packed without padding; file size is V + K + 11.
 * Serialize emits [key][footer]; the file I/O path writes value before this metadata.
 */

#include <cstring>
#include "securec.h"
#include "bio_def.h"
#include "bio_log.h"
#include "local_file_pack.h"

namespace ock {
namespace bio {
namespace {
constexpr char MAGIC[] = "UBSIO";
constexpr size_t MAGIC_SIZE = sizeof(MAGIC) - 1;
constexpr size_t KEY_SIZE_OFFSET = MAGIC_SIZE;
constexpr size_t KEY_SIZE_BYTES = sizeof(uint16_t);
constexpr size_t VALUE_SIZE_OFFSET = KEY_SIZE_OFFSET + KEY_SIZE_BYTES;
static_assert(VALUE_SIZE_OFFSET + sizeof(uint32_t) == LOCAL_FILE_FOOTER_SIZE,
    "Local file footer must contain exactly 11 encoded bytes.");
static_assert(LOCAL_FILE_MAX_KEY_SIZE <= UINT16_MAX, "Local file key length must fit in two bytes.");
}

void LocalFilePack::EncodeInteger(char *target, uint64_t value, size_t bytes) noexcept
{
    for (size_t i = 0; i < bytes; ++i) {
        target[i] = static_cast<char>((value >> (i * 8)) & 0xff);
    }
}

uint64_t LocalFilePack::DecodeInteger(const char *source, size_t bytes) noexcept
{
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) {
        value |= static_cast<uint64_t>(static_cast<unsigned char>(source[i])) << (i * 8);
    }
    return value;
}

BResult LocalFilePack::Serialize(const char *key, uint64_t valueSize, char *metadata, size_t &metadataSize) noexcept
{
    metadataSize = 0;
    if (UNLIKELY(key == nullptr || metadata == nullptr || valueSize > LOCAL_FILE_MAX_VALUE_SIZE)) {
        LOG_ERROR("Serialize local file metadata failed, invalid input, keyNull:" << (key == nullptr) <<
            ", metadataNull:" << (metadata == nullptr) << ", valueSize:" << valueSize <<
            ", maxValueSize:" << LOCAL_FILE_MAX_VALUE_SIZE << ".");
        return BIO_INVALID_PARAM;
    }
    const size_t keySize = strnlen(key, LOCAL_FILE_MAX_KEY_SIZE + 1);
    if (UNLIKELY(keySize == 0 || keySize > LOCAL_FILE_MAX_KEY_SIZE)) {
        LOG_ERROR("Serialize local file metadata failed, invalid keySize:" << keySize <<
            ", maxKeySize:" << LOCAL_FILE_MAX_KEY_SIZE << ".");
        return BIO_INVALID_PARAM;
    }
    const auto keyCopyResult = memcpy_s(metadata, LOCAL_FILE_MAX_METADATA_SIZE, key, keySize);
    if (UNLIKELY(keyCopyResult != EOK)) {
        LOG_ERROR("Serialize local file metadata failed to copy key, keySize:" << keySize <<
            ", ret:" << keyCopyResult << ".");
        return BIO_INNER_ERR;
    }
    char *footer = metadata + keySize;
    const auto magicCopyResult = memcpy_s(footer, LOCAL_FILE_FOOTER_SIZE, MAGIC, MAGIC_SIZE);
    if (UNLIKELY(magicCopyResult != EOK)) {
        LOG_ERROR("Serialize local file metadata failed to copy footer magic, ret:" << magicCopyResult << ".");
        return BIO_INNER_ERR;
    }
    EncodeInteger(footer + KEY_SIZE_OFFSET, keySize, KEY_SIZE_BYTES);
    EncodeInteger(footer + VALUE_SIZE_OFFSET, valueSize, sizeof(uint32_t));
    metadataSize = keySize + LOCAL_FILE_FOOTER_SIZE;
    return BIO_OK;
}

BResult LocalFilePack::Deserialize(const char *metadata, size_t metadataSize, std::string &key,
    uint64_t &valueSize) noexcept
{
    if (UNLIKELY(metadata == nullptr || metadataSize < LOCAL_FILE_FOOTER_SIZE ||
        metadataSize > LOCAL_FILE_MAX_METADATA_SIZE)) {
        LOG_ERROR("Deserialize local file metadata failed, invalid input, metadataNull:" << (metadata == nullptr) <<
            ", metadataSize:" << metadataSize << ", footerSize:" << LOCAL_FILE_FOOTER_SIZE <<
            ", maxMetadataSize:" << LOCAL_FILE_MAX_METADATA_SIZE << ".");
        return BIO_UFS_IOERR;
    }
    const char *footer = metadata + metadataSize - LOCAL_FILE_FOOTER_SIZE;
    if (UNLIKELY(memcmp(footer, MAGIC, MAGIC_SIZE) != 0)) {
        LOG_ERROR("Deserialize local file metadata failed, footer magic mismatch, metadataSize:" << metadataSize << ".");
        return BIO_UFS_IOERR;
    }
    const uint64_t keySize = DecodeInteger(footer + KEY_SIZE_OFFSET, KEY_SIZE_BYTES);
    const uint64_t length = DecodeInteger(footer + VALUE_SIZE_OFFSET, sizeof(uint32_t));
    if (UNLIKELY(keySize == 0 || keySize > LOCAL_FILE_MAX_KEY_SIZE ||
        keySize > metadataSize - LOCAL_FILE_FOOTER_SIZE)) {
        LOG_ERROR("Deserialize local file metadata failed, invalid keySize:" << keySize <<
            ", maxKeySize:" << LOCAL_FILE_MAX_KEY_SIZE <<
            ", availableKeyBytes:" << metadataSize - LOCAL_FILE_FOOTER_SIZE << ".");
        return BIO_UFS_IOERR;
    }
    const char *keyData = footer - keySize;
    if (UNLIKELY(memchr(keyData, '\0', keySize) != nullptr)) {
        LOG_ERROR("Deserialize local file metadata failed, key contains a NUL byte, keySize:" << keySize << ".");
        return BIO_UFS_IOERR;
    }
    key.assign(keyData, keySize);
    valueSize = length;
    return BIO_OK;
}
}
}
