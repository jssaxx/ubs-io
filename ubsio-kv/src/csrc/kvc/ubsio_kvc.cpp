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
constexpr int MAX_BATCH_OP_COUNT = 16 << 10; // 16K
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
    if (UNLIKELY(key == nullptr || buf == nullptr || length == 0)) {
        LOG_ERROR("Invalid parma, length:" << length);
        return DFC_INVALID_PARAM;
    }
    if (UNLIKELY(strlen(key) > MAX_KEY_LENGTH || strlen(key) < 1)) {
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
    if (UNLIKELY(keys == nullptr || bufs == nullptr || lengths == nullptr || results == nullptr)) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (UNLIKELY(keysCount > MAX_BATCH_OP_COUNT || keysCount < 1)) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }

    std::vector<std::string> keyVector(keysCount);
    std::vector<int> batchResult(keysCount, DFC_ERR);
    std::vector<size_t> lengthsVector(keysCount);
    for (size_t i = 0; i < keysCount; i++) {
        if (UNLIKELY(keys[i] == nullptr)) {
            LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        if (UNLIKELY(strlen(keys[i]) > MAX_KEY_LENGTH || strlen(keys[i]) < 1)) {
            LOG_ERROR("Get invalid key length [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        if (UNLIKELY(lengths[i] == 0)) {
            LOG_ERROR("Get invalid lengths [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        keyVector[i] = keys[i];
        lengthsVector[i] = lengths[i];
    }

    auto ret = KvcBatchGetData(keyVector, bufs, lengthsVector, batchResult, flags);
    if (UNLIKELY(ret != DFC_OK)) {
        LOG_ERROR("Kvc batch get failed, ret:" << ret);
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
    if (UNLIKELY(keys == nullptr || bufs == nullptr || lengths == nullptr || results == nullptr)) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (UNLIKELY(keysCount > MAX_BATCH_OP_COUNT || keysCount < 1 || lengthsRows != keysCount || lengthsCols > MAX_KV_LAYER_NUM)) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }

    std::vector<std::string> keyVector(keysCount);
    std::vector<int> batchResult(keysCount, DFC_ERR);
    std::vector<std::vector<size_t>> lengthsVector(keysCount);
    std::vector<std::vector<uintptr_t>> npuAddrsVector(keysCount);
    for (uint32_t i = 0; i < keysCount; ++i) {
        if (UNLIKELY(keys[i] == nullptr)) {
            LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        if (UNLIKELY(strlen(keys[i]) > MAX_KEY_LENGTH || strlen(keys[i]) < 1)) {
            LOG_ERROR("Get invalid key length[" << i << "]");
            return DFC_INVALID_PARAM;
        }
        std::vector<size_t> layerLengths(lengthsCols);
        std::vector<uintptr_t> layerAddrs(lengthsCols);
        for (uint32_t j = 0; j < lengthsCols; ++j) {
            if (UNLIKELY(lengths[i][j] == 0 || lengths[i][j] > MAX_KV_LAYER_LENGTH)) {
                LOG_ERROR("Get invalid lengths[" << i << "][" << j << "], length " << lengths[i][j]);
                return DFC_INVALID_PARAM;
            }
            if (UNLIKELY(bufs[i][j] == nullptr)) {
                LOG_ERROR("Get invalid npuAddrs[" << i << "][" << j << "]");
                return DFC_INVALID_PARAM;
            }
            layerLengths[j] = lengths[i][j];
            layerAddrs[j] = reinterpret_cast<uintptr_t>(bufs[i][j]);
        }
        keyVector[i] = keys[i];
        lengthsVector[i] = layerLengths;
        npuAddrsVector[i] = layerAddrs;
    }

    return KvcInstance::Instance().Read(keyVector, npuAddrsVector, lengthsVector, results);
}

UBSIO_API int32_t UbsioKvCacheBatchExist(const char **keys, uint32_t keysCount, bool *results, uint32_t flags)
{
    if (UNLIKELY(keys == nullptr || results == nullptr)) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (UNLIKELY(keysCount > MAX_BATCH_OP_COUNT || keysCount < 1)) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }

    std::vector<std::string> keyVector(keysCount);
    for (size_t i = 0; i < keysCount; i++) {
        if (UNLIKELY(keys[i] == nullptr)) {
            LOG_ERROR("Get invalid key nullptr on idx [" << i << "]");
            return DFC_INVALID_PARAM;
        }
        if (UNLIKELY(strlen(keys[i]) > MAX_KEY_LENGTH || strlen(keys[i]) < 1)) {
            LOG_ERROR("Get invalid key length[" << i << "]");
            return DFC_INVALID_PARAM;
        }
        keyVector[i] = keys[i];
    }

    auto ret = KvcBatchExistKey(keyVector, results, flags);
    if (UNLIKELY(ret != DFC_OK)) {
        LOG_ERROR("Kvc batch exist failed, ret:" << ret);
        return DFC_ERR;
    }
    return DFC_OK;
}

UBSIO_API int32_t UbsioKvCacheBatchFree(void **bufs, uint32_t keysCount)
{
    if (UNLIKELY(bufs == nullptr)) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }
    if (UNLIKELY(keysCount > MAX_BATCH_OP_COUNT || keysCount < 1)) {
        LOG_ERROR("Invalid parma");
        return DFC_INVALID_PARAM;
    }

    int ret = KvcBatchFreeGetAddress(bufs, keysCount);
    if (UNLIKELY(ret != DFC_OK)) {
        LOG_ERROR("Dfc batch free address failed, ret:" << ret);
        return DFC_ERR;
    }
    return DFC_OK;
}
