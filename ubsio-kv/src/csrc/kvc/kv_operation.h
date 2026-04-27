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

#ifndef KVC_OPERATION_H
#define KVC_OPERATION_H

#include <vector>
#include "ubsio_kvc_def.h"
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_execution.h"

namespace ock {
namespace ubsio {

constexpr uint16_t KV_THREAD_NUM = 16;
constexpr uint16_t KV_QUEUE_SIZE = 8192;

class KvOperation {
public:
    static inline KvOperation *Instance()
    {
        if (gInstance == nullptr) {
            std::lock_guard<std::mutex> guard(gLock);
            if (gInstance == nullptr) {
                gInstance = new (std::nothrow) KvOperation();
                if (gInstance == nullptr) {
                    LOG_ERROR("Failed to new KvOperation object, probably out of memory");
                    return nullptr;
                }
            }
        }
        return gInstance;
    }

public:
    ~KvOperation() = default;
    int32_t Initialize(const std::string &path);
    int32_t KvcRegisterKvCache(std::vector<uint64_t> &kvCacheAddrs,
                                            std::vector<uint64_t> &kvCacheSizes);
    void UnInitialize();

    // kv operation
    int32_t KvPutData(const std::string &key, void *value, size_t len);
    int32_t KvGetData(const std::string &key, void *value, size_t len);
    int32_t KvDeleteKey(const std::string &key);
    bool KvExistKey(const std::string &key);
    int32_t KvGetLengthKey(const std::string &key, uint32_t &length);
    int32_t BatchKvPutData(const std::vector<std::string> &key,
                           std::vector<void *> &value,
                           std::vector<size_t> &lengths,
                           std::vector<int> &results);
    int32_t BatchKvGetData(const std::vector<std::string> &key,
                           void **bufs,
                           std::vector<size_t> &lengths,
                           std::vector<int> &results);
    int32_t BatchKvExistKey(const std::vector<std::string> &key, bool *results);
    int32_t BatchKvDeleteKey(const std::vector<std::string> &key, std::vector<int> &results);
    int32_t BatchGetLengthKey(const std::vector<std::string> &key, std::vector<uint32_t> &lengths, std::vector<int> &results);    

    int32_t KvcGetPositions(const std::vector<std::string> &keys, std::vector<uint8_t> &positions);

    int32_t KvBatchGetLocalData(const std::vector<std::string> &keys, void **bufs,
                                std::vector<size_t> &lengths,
                                std::vector<int32_t> &results);
    
    int32_t KvBatchGetRemoteData(const std::vector<std::string> &keys, uintptr_t **npuAddrs,
                                 std::vector<std::vector<size_t>> &lengths, uintptr_t *dramAddrs,
                                 std::vector<int32_t> &results);
    
    inline int32_t InitKvExecutor(void)
    {
        mKvExecutor = ExecutorService::Create(KV_THREAD_NUM, KV_QUEUE_SIZE);
        if (UNLIKELY(mKvExecutor == nullptr)) {
            LOG_ERROR("Failed to create execution service for get kv, probably out of memory");
            return UBSIO_KVC_ERR;
        }
        auto result = mKvExecutor->Start();
        if (!result) {
            LOG_ERROR("Failed to start execution service for get kv, probably out of memory");
            return UBSIO_KVC_ERR;
        }
        return UBSIO_KVC_OK;
    }

private:
    KvOperation() = default;

private:
    int mSharedFd{ -1 };
    std::mutex mMutex;
    bool mInited{ false };
    uint64_t tenantId{ 1 };
    ExecutorServicePtr mKvExecutor{ nullptr };
    static std::mutex gLock;
    static KvOperation *gInstance;
};

} // namespace ubsio
} // namespace ock
#endif // KVC_OPERATION_H