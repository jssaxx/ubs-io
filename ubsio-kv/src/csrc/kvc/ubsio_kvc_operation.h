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

#ifndef UBSIO_KVC_OPERATION_H
#define UBSIO_KVC_OPERATION_H

#include <cstdint>
#include <string>
#include <vector>
#include "ubsio_kvc_def.h"

namespace ock {
namespace ubsio {

void KvcExit();

int32_t KvcOperationInit(int32_t devId);

int32_t KvcRegisterKvCache(std::vector<uint64_t> &kvCacheAddrs,
                           std::vector<uint64_t> &kvCacheSizes);

int32_t KvcPutData(const std::string &key, void *value, size_t len, uint32_t flags);

int32_t KvcGetData(const std::string &key, void *value, size_t len, uint32_t flags);

int32_t KvcDeleteKey(const std::string &key, uint32_t flags);

bool KvcExistKey(const std::string &key, uint32_t flags);

int32_t KvcGetKeyLength(const std::string &key, uint32_t &length, uint32_t flags);

int32_t KvcBatchPutData(const std::vector<std::string> &key,
                        std::vector<void *> &value,
                        std::vector<size_t> &lengths,
                        std::vector<int> &results,
                        uint32_t flags);

int32_t KvcBatchGetData(const std::vector<std::string> &key,
                        void **bufs,
                        std::vector<size_t> &lengths,
                        std::vector<int> &results,
                        uint32_t flags);

int32_t KvcBatchExistKey(const std::vector<std::string> &key, bool *results, uint32_t flags);

int32_t KvcBatchFreeGetAddress(void **bufs, uint32_t keys_count);

int32_t KvcBatchDeleteKey(const std::vector<std::string> &key, std::vector<int> &results, uint32_t flags);

int32_t KvcBatchGetLengthKey(const std::vector<std::string> &key,
                             std::vector<uint32_t> &lengths,
                             std::vector<int> &results,
                             uint32_t flags);
                             
int32_t KvcGetPositions(const std::vector<std::string> &keys, std::vector<uint8_t> &positions);

int32_t KvBatchGetLocalData(const std::vector<std::string> &keys, void **bufs,
                            std::vector<size_t> &lengths,
                            std::vector<int32_t> &results,
                            uint32_t flags);

int32_t KvBatchGetRemoteData(const std::vector<std::string> &keys, uintptr_t **npuAddrs,
                             std::vector<std::vector<size_t>> &lengths, uintptr_t *dramAddrs,
                             std::vector<int32_t> &results, uint32_t flags);

} // namespace ubsio
} // namespace ock
#endif // UBSIO_KVC_OPERATION_H