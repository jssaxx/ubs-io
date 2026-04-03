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

#include <cstdint>
#include <vector>
#include "ubsio_kvc_log.h"
#include <ubsio_kvc_err.h>
#include "ubsio_kvc_operation.h"
#include "ubsio_kvc_instance.h"
#include "ubsio_kvc.h"

using namespace ock::ubsio;

namespace {
constexpr int MAX_KEY_LENGTH = 255;
constexpr int MAX_BATCH_OP_COUNT = 256;
constexpr int MAX_KV_LAYER_NUM = 2 * 512; // k layer + v layer
constexpr int64_t MAX_KV_LAYER_LENGTH = 2 * 1024 * 1024 * 1024LL; // 2G
}

UBSIO_API int32_t UbsioKvCacheInit(int32_t devId)
{
    int32_t ret = KvcInstance::Instance().Initialize(devId);
    if (UNLIKELY(ret != DFC_OK)) {
        LOG_ERROR("init kvc instance failed, device id: " << devId << ", ret: " << ret);
        return DFC_ERR;
    }
    return KvcOperationInit(devId);
}

UBSIO_API int32_t UbsioKvCachePut(const char *key, void *buf, size_t length, uint32_t flags)
{
    if (key == nullptr || buf == nullptr || length == 0) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (strlen(key) > MAX_KEY_LENGTH || strlen(key) < 1) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    return KvcPutData(key, buf, length, flags);
}

UBSIO_API int32_t UbsioKvCacheBatchGet(const char **keys,
                                       uint32_t keysCount,
                                       void **bufs,
                                       size_t *lengths,
                                       int *results,
                                       uint32_t flags)
{
    if (keys == nullptr || bufs == nullptr || lengths == nullptr || results == nullptr) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (keysCount > MAX_BATCH_OP_COUNT || keysCount < 1) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    std::vector<std::string> key_vector;
    std::vector<int> batchResult(keysCount, DFC_ERR);
    std::vector<size_t> lengths_vector;
    key_vector.reserve(keysCount);
    lengths_vector.reserve(keysCount);

    for (size_t i = 0; i < keysCount; i++) {
        if (keys[i] == nullptr) {
            LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        if (strlen(keys[i]) > MAX_KEY_LENGTH || strlen(keys[i]) < 1) {
            LOG_ERROR("Get invalid key length [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        if (lengths[i] == 0) {
            LOG_ERROR("Get invalid lengths [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        key_vector.emplace_back(keys[i]);
        lengths_vector.emplace_back(lengths[i]);
    }

    auto ret = KvcBatchGetData(key_vector, bufs, lengths_vector, batchResult, flags);
    if (ret != DFC_OK) {
        LOG_ERROR("Dfc batch get failed");
        return DFC_ERR;
    }
    for (uint32_t i = 0; i < keysCount; ++i) {
        results[i] = batchResult[i];
    }
    return DFC_OK;
}

int32_t UbsioKvCacheBatchGetDirect(const char **keys,
                                   uint32_t keysCount,
                                   void ***bufs,
                                   size_t **lengths,
                                   uint32_t lengthsRows,
                                   uint32_t lengthsCols,
                                   int *results,
                                   uint32_t flags)
{
    if (keys == nullptr || bufs == nullptr || lengths == nullptr || results == nullptr) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (keysCount > MAX_BATCH_OP_COUNT || keysCount < 1 || lengthsRows != keysCount || lengthsCols > MAX_KV_LAYER_NUM) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    std::vector<std::string> keyVector;
    std::vector<int> batchResult(keysCount, DFC_ERR);
    std::vector<std::vector<size_t>> lengthsVector;
    std::vector<std::vector<uintptr_t>> npuAddrsVector;
    keyVector.reserve(keysCount);
    lengthsVector.reserve(keysCount);
    npuAddrsVector.reserve(keysCount);
    for (uint32_t i = 0; i < keysCount; ++i) {
        if (keys[i] == nullptr) {
            LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        if (strlen(keys[i]) > MAX_KEY_LENGTH || strlen(keys[i]) < 1) {
            LOG_ERROR("Get invalid key length[" << i << "]");
            return DFC_INVALID_PARAM;
        }
        std::vector<size_t> layerLengths;
        std::vector<uintptr_t> layerAddrs;
        layerLengths.reserve(lengthsCols);
        layerAddrs.reserve(lengthsCols);
        for (uint32_t j = 0; j < lengthsCols; ++j) {
            if (lengths[i][j] == 0 || lengths[i][j] > MAX_KV_LAYER_LENGTH) {
                LOG_ERROR("Get invalid lengths[" << i << "][" << j << "], length " << lengths[i][j]);
                return DFC_INVALID_PARAM;
            }
            if (bufs[i][j] == nullptr) {
                LOG_ERROR("Get invalid npuAddrs[" << i << "][" << j << "]");
                return DFC_INVALID_PARAM;
            }
            layerLengths.emplace_back(lengths[i][j]);
            layerAddrs.emplace_back(reinterpret_cast<uintptr_t>(bufs[i][j]));
        }
        keyVector.emplace_back(keys[i]);
        lengthsVector.emplace_back(layerLengths);
        npuAddrsVector.emplace_back(layerAddrs);
    }
    return DfcIOInstance::Instance().Read(keyVector, npuAddrsVector, lengthsVector, results);
}

UBSIO_API int32_t UbsioKvCacheBatchExist(const char **keys, uint32_t keysCount, bool *results, uint32_t flags)
{
    if (keys == nullptr || results == nullptr) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (keysCount > MAX_BATCH_OP_COUNT || keysCount < 1) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    std::vector<std::string> key_vector;
    key_vector.reserve(keysCount);

    for (size_t i = 0; i < keysCount; i++) {
        if (keys[i] == nullptr) {
            LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        if (strlen(keys[i]) > MAX_KEY_LENGTH || strlen(keys[i]) < 1) {
            LOG_ERROR("Get invalid key length[" << i << "]");
            return DFC_INVALID_PARAM;
        }
        key_vector.emplace_back(keys[i]);
    }

    auto ret = KvcBatchExistKey(key_vector, results, flags);
    if (ret != DFC_OK) {
        LOG_ERROR("Dfc batch exist failed");
        return DFC_ERR;
    }
    return DFC_OK;
}

UBSIO_API int32_t UbsioKvCacheBatchFree(void **bufs, uint32_t keys_count)
{
    if (bufs == nullptr) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (keys_count > MAX_BATCH_OP_COUNT || keys_count < 1) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    int ret = KvcBatchFreeGetAddress(bufs, keys_count);
    if (ret != DFC_OK) {
        LOG_ERROR("Dfc batch free address failed");
        return DFC_ERR;
    }
    return DFC_OK;
}
