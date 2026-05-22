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

#include <numeric>
#include <semaphore.h>
#include <atomic>
#include "ubsio_kvc_operation.h"
#include "ubsio_kvc_stream_manager.h"
#include "dl_acl_api.h"
#include "ubsio_nds_manager.h"
#include "ubsio_kvc_instance.h"

namespace ock {
namespace ubsio {

using namespace ock::ubsio::nds;
constexpr int KVC_INSTANCE_THREAD_NUM = 8;
constexpr uint32_t MAX_READ_BATCH_SIZE = 128;

KvcInstance &KvcInstance::Instance()
{
    static KvcInstance instance;
    return instance;
}

KvcError KvcInstance::Initialize(int32_t device)
{
    // 创建batch read的executor线程池.
    m_readExecutor = ExecutorService::Create(KVC_INSTANCE_THREAD_NUM);
    if (m_readExecutor == nullptr) {
        LOG_ERROR("Nds thread pool init failed.");
        return UBSIO_KVC_ERR;
    }
    auto success = m_readExecutor->Start();
    if (!success) {
        m_readExecutor = nullptr;
        LOG_ERROR("kv instance thread pool start failed.");
        return UBSIO_KVC_ERR;
    }

    m_deviceId = device;
    return UBSIO_KVC_OK;
}

KvcError KvcInstance::CopyDataH2D(H2DParams &params, std::vector<int32_t> &batchResult,
                         const std::vector<uint32_t> &origIndex, int *results)
{
    void* stream = KvcStreamManager::GetAclStream();
    if (UNLIKELY(stream == nullptr)) {
        LOG_ERROR("Kv cache stream is nullptr, can not do memcpy");
        return UBSIO_KVC_ERR;
    }
    uint32_t count = params.hostAddrs.size();
    for (uint32_t i = 0; i < count; ++i) {
        results[origIndex[i]] = batchResult[i];
        if (batchResult[i] != UBSIO_KVC_OK) {
            continue;
        }
        void* dramAddr = params.hostAddrs[i];
        uint32_t offset = 0;

        for (uint32_t j = 0; j < params.npuAddrs[i].size(); ++j) {
            void* dst = reinterpret_cast<void*>(params.npuAddrs[i][j]);
            void* src = reinterpret_cast<void*>(reinterpret_cast<char*>(dramAddr) + offset);
            int32_t aclRet = ACLApi::AclrtMemcpyAsync(dst, params.lengths[i][j], src, params.lengths[i][j],
                ACL_MEMCPY_HOST_TO_DEVICE, stream);
            if (UNLIKELY(aclRet != 0)) {
                LOG_ERROR("Aclrt memcpy async failed, ret: " << aclRet);
                results[origIndex[i]] = UBSIO_KVC_ERR;
                break;
            }
            offset += params.lengths[i][j];
        }
    }
    int32_t aclRet = ACLApi::AclrtSynchronizeStream(stream);
    if (UNLIKELY(aclRet != 0)) {
        LOG_ERROR("Aclrt synchronize stream failed, ret: " << aclRet);
    }

    // 释放dram地址
    std::vector<void *> addrsToFree = std::move(params.hostAddrs);
    m_readExecutor->Execute([addrsToFree = std::move(addrsToFree)]()->void {
        auto ret = KvcBatchFreeGetAddress(const_cast<void **>(addrsToFree.data()), addrsToFree.size());
        if (UNLIKELY(ret != UBSIO_KVC_OK)) {
            LOG_ERROR("Kvc batch free dram failed, ret:" << ret);
        }
    });
    return (aclRet == 0) ? UBSIO_KVC_OK : UBSIO_KVC_ERR;
}

KvcError KvcInstance::ReadLocal(ReadParams &params, int *results)
{
    if (params.keys.empty()) {
        return UBSIO_KVC_OK;
    }
    // 1. 优先选择NDS（不分批）
    auto &keysVector = params.keys;
    auto &npuAddrsVector = params.npuAddrs;
    auto &lengthsVector = params.lengths;
    auto &oriIndex = params.oriIndex;
    uint32_t keysCount = params.keys.size();
    auto ret = NdsManager::Instance().BatchDirectRead(keysVector, npuAddrsVector, lengthsVector);
    if (ret == UBSIO_KVC_OK) {
        for (uint32_t i = 0; i < keysCount; ++i) {
            results[oriIndex[i]] = UBSIO_KVC_OK;
        }
        return UBSIO_KVC_OK;
    }
    if (ret != UBSIO_KVC_NO_NDS) {
        LOG_WARN("nds read failed, ret: " << ret << ", try to read from disk");
    }

    // 2. NDS失败走原生读，分批并发
    return ReadLocalBatch(params, results);
}

KvcError KvcInstance::ReadLocalBatch(ReadParams &params, int *results)
{
    auto &keysVector = params.keys;
    auto &npuAddrsVector = params.npuAddrs;
    auto &lengthsVector = params.lengths;
    auto &oriIndex = params.oriIndex;
    uint32_t keysCount = keysVector.size();

    uint32_t batchCount = (keysCount + MAX_READ_BATCH_SIZE - 1) / MAX_READ_BATCH_SIZE;
    std::vector<BatchReadResult> batchResults(batchCount);

    sem_t sem;
    sem_init(&sem, 0, 0);
    std::atomic<uint32_t> completedCount{0};

    // 并发读取DRAM
    for (uint32_t b = 0; b < batchCount; ++b) {
        uint32_t start = b * MAX_READ_BATCH_SIZE;
        uint32_t end = std::min(start + MAX_READ_BATCH_SIZE, keysCount);
        uint32_t batchSize = end - start;

        m_readExecutor->Execute([this, &batchResults, b, start, end, batchSize,
                                  &keysVector, &npuAddrsVector, &lengthsVector, &oriIndex,
                                  &sem, &completedCount, &batchCount]()->void {
            auto &br = batchResults[b];
            br.batchResult.assign(batchSize, UBSIO_KVC_ERR);
            br.dramAddrsVector.resize(batchSize);

            // 用const char*指向原key，避免string拷贝
            std::vector<const char *> batchKeys(batchSize);
            for (uint32_t i = 0; i < batchSize; ++i) {
                batchKeys[i] = keysVector[start + i].c_str();
            }
            std::vector<size_t> dramAddrsLenVector;
            dramAddrsLenVector.reserve(batchSize);
            for (uint32_t i = 0; i < batchSize; ++i) {
                dramAddrsLenVector.emplace_back(
                    std::accumulate(lengthsVector[start + i].begin(), lengthsVector[start + i].end(), 0LL));
            }

            auto ret = static_cast<KvcError>(KvBatchGetLocalData(
                batchKeys.data(), batchSize, br.dramAddrsVector.data(), dramAddrsLenVector, br.batchResult, 0));
            if (ret == UBSIO_KVC_OK) {
                br.needH2D = true;
                br.h2dParams = H2DParams(
                    std::vector<std::vector<uintptr_t>>(npuAddrsVector.begin() + start, npuAddrsVector.begin() + end),
                    std::vector<std::vector<size_t>>(lengthsVector.begin() + start, lengthsVector.begin() + end),
                    std::move(br.dramAddrsVector));
                br.batchOriIndex.assign(oriIndex.begin() + start, oriIndex.begin() + end);
            } else {
                br.readRet = ret;
                LOG_ERROR("Kv cache batch get local data failed, ret:" << ret
                          << ", batch[" << start << "," << end << "]");
            }

            if (completedCount.fetch_add(1) + 1 == batchCount) {
                sem_post(&sem);
            }
        });
    }

    // 等待所有批次读取完成
    sem_wait(&sem);
    sem_destroy(&sem);

    
    KvcError finalRet = UBSIO_KVC_OK;
    for (uint32_t b = 0; b < batchCount; ++b) {
        auto &br = batchResults[b];
        if (br.needH2D) {
            auto h2dRet = CopyDataH2D(br.h2dParams, br.batchResult, br.batchOriIndex, results);
            if (h2dRet != UBSIO_KVC_OK) {
                finalRet = UBSIO_KVC_ERR;
            }
        } else if (br.readRet != UBSIO_KVC_OK) {
            finalRet = UBSIO_KVC_ERR;
        }
    }
    return finalRet;
}

KvcError KvcInstance::ReadRemote(ReadParams &params, int *results)
{
    if (params.keys.empty()) {
        return UBSIO_KVC_OK;
    }
    return ReadRemoteBatch(params, results);
}

KvcError KvcInstance::ReadRemoteBatch(ReadParams &params, int *results)
{
    auto &keysVector = params.keys;
    auto &npuAddrsVector = params.npuAddrs;
    auto &lengthsVector = params.lengths;
    auto &oriIndex = params.oriIndex;
    uint32_t keysCount = keysVector.size();

    uint32_t batchCount = (keysCount + MAX_READ_BATCH_SIZE - 1) / MAX_READ_BATCH_SIZE;
    std::vector<BatchReadResult> batchResults(batchCount);

    sem_t sem;
    sem_init(&sem, 0, 0);
    std::atomic<uint32_t> completedCount{0};

    // 并发读取
    for (uint32_t b = 0; b < batchCount; ++b) {
        uint32_t start = b * MAX_READ_BATCH_SIZE;
        uint32_t end = std::min(start + MAX_READ_BATCH_SIZE, keysCount);
        uint32_t batchSize = end - start;

        m_readExecutor->Execute([this, &batchResults, b, start, end, batchSize,
                                  &keysVector, &npuAddrsVector, &lengthsVector, &oriIndex,
                                  results, &sem, &completedCount, &batchCount]()->void {
            auto &br = batchResults[b];
            br.batchResult.assign(batchSize, UBSIO_KVC_ERR);
            br.dramAddrsVector.resize(batchSize);

            // 用const char*指向原key，避免string拷贝
            std::vector<const char *> batchKeys(batchSize);
            for (uint32_t i = 0; i < batchSize; ++i) {
                batchKeys[i] = keysVector[start + i].c_str();
            }
            std::vector<uintptr_t *> npuAddrs(batchSize);
            for (uint32_t i = 0; i < batchSize; ++i) {
                npuAddrs[i] = npuAddrsVector[start + i].data();
            }
            std::vector<std::vector<size_t>> batchLengths(lengthsVector.begin() + start, lengthsVector.begin() + end);

            auto ret = static_cast<KvcError>(KvBatchGetRemoteData(
                batchKeys.data(), batchSize, npuAddrs.data(), batchLengths,
                reinterpret_cast<uintptr_t *>(br.dramAddrsVector.data()), br.batchResult, 0));

            if (ret == UBSIO_KVC_OK) {
                // rh2d: 数据已直接写入NPU
                br.rh2dSuccess = true;
                for (uint32_t i = 0; i < batchSize; ++i) {
                    results[oriIndex[start + i]] = br.batchResult[i];
                }
            } else if (ret == static_cast<KvcError>(CResult::RET_CACHE_IN_DRAM)) {
                // rh2h: 数据在DRAM，需要H2D拷贝
                br.needH2D = true;
                br.h2dParams = H2DParams(
                    std::vector<std::vector<uintptr_t>>(npuAddrsVector.begin() + start, npuAddrsVector.begin() + end),
                    std::vector<std::vector<size_t>>(lengthsVector.begin() + start, lengthsVector.begin() + end),
                    std::move(br.dramAddrsVector));
                br.batchOriIndex.assign(oriIndex.begin() + start, oriIndex.begin() + end);
            } else {
                br.readRet = ret;
                LOG_ERROR("Kv cache batch get remote data failed, ret:" << ret
                          << ", batch[" << start << "," << end << "]");
            }

            if (completedCount.fetch_add(1) + 1 == batchCount) {
                sem_post(&sem);
            }
        });
    }

    // 等待所有批次完成
    sem_wait(&sem);
    sem_destroy(&sem);

    // 串行H2D拷贝
    KvcError finalRet = UBSIO_KVC_OK;
    for (uint32_t b = 0; b < batchCount; ++b) {
        auto &br = batchResults[b];
        if (br.needH2D) {
            auto h2dRet = CopyDataH2D(br.h2dParams, br.batchResult, br.batchOriIndex, results);
            if (h2dRet != UBSIO_KVC_OK) {
                finalRet = UBSIO_KVC_ERR;
            }
        } else if (br.readRet != UBSIO_KVC_OK) {
            finalRet = UBSIO_KVC_ERR;
        }
    }
    return finalRet;
}

KvcError KvcInstance::Read(const std::vector<std::string> &keyVector,
                           std::vector<std::vector<uintptr_t>> &npuAddrsVector,
                           const std::vector<std::vector<size_t>> &lengthsVector, int *results)
{
    std::fill(results, results + keyVector.size(), UBSIO_KVC_ERR);
    uint32_t keysCount = keyVector.size();
    std::vector<uint8_t> positions(keysCount);
    auto ret = static_cast<KvcError>(KvcGetPositions(keyVector, positions));
    if (UNLIKELY(ret != UBSIO_KVC_OK)) {
        LOG_ERROR("Kv cache get positions failed, ret:" << ret);
        return ret;
    }

    ReadParams localParams(keysCount);
    ReadParams remoteParams(keysCount);

    for (uint32_t i = 0; i < keysCount; ++i) {
        if (positions[i] == 0) {
            localParams.keys.emplace_back(keyVector[i]);
            localParams.npuAddrs.emplace_back(npuAddrsVector[i]);
            localParams.lengths.emplace_back(lengthsVector[i]);
            localParams.oriIndex.emplace_back(i);
        } else if (positions[i] == 1) {
            remoteParams.keys.emplace_back(keyVector[i]);
            remoteParams.npuAddrs.emplace_back(npuAddrsVector[i]);
            remoteParams.lengths.emplace_back(lengthsVector[i]);
            remoteParams.oriIndex.emplace_back(i);
        }
    }
    sem_t sem;
    sem_init(&sem, 0, 0);
    KvcError remoteRet = UBSIO_KVC_OK;
    // read remote //增加远端判断
    if (!remoteParams.keys.empty()) {
        m_readExecutor->Execute([this, &remoteParams, results, &sem, &remoteRet]()->void {
            remoteRet = ReadRemote(remoteParams, results);
            if (UNLIKELY(remoteRet != UBSIO_KVC_OK)) {
                LOG_ERROR("Kvc batch get reomte data failed, ret:" << remoteRet);
            }
            sem_post(&sem);
        });
    } else {
        sem_post(&sem);
    }

    // read local
    ret = ReadLocal(localParams, results);
    if (UNLIKELY(ret != UBSIO_KVC_OK)) {
        LOG_ERROR("Kvc batch get local data failed, ret:" << ret);
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    return (ret == UBSIO_KVC_OK && remoteRet == UBSIO_KVC_OK) ? UBSIO_KVC_OK : UBSIO_KVC_ERR;
}

} // ubsio
} // ock