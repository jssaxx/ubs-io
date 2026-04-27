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
#include "ubsio_kvc_operation.h"
#include "ubsio_kvc_stream_manager.h"
#include "dl_acl_api.h"
#include "ubsio_nds_manager.h"
#include "ubsio_kvc_instance.h"

namespace ock {
namespace ubsio {

using namespace ock::ubsio::nds;
constexpr int KVC_INSTANCE_THREAD_NUM = 2;

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

 KvcError CopyDataH2D(H2DParams &params, std::vector<int32_t> &batchResult,
                         const std::vector<uint32_t> &origIndex, int *results)
{
    void* stream = KvcStreamManager::GetAclStream();
    if (UNLIKELY(stream == nullptr)) {
        LOG_ERROR("Kv cache stream is nullptr, can not do memcpy");
        return UBSIO_KVC_ERR;
    }
    uint32_t count = params.hostAddrs.size();
    auto &dramAddrsVector = params.hostAddrs;
    auto &npuAddrsVector = params.npuAddrs;
    auto &lengthsVector = params.lengths;
    for (uint32_t i = 0; i < count; ++i) {
        results[origIndex[i]] = batchResult[i];
        if (batchResult[i] != DFC_OK) {
            continue;
        }
        void* dramAddr = dramAddrsVector[i];
        uint32_t offset = 0;
        
        for (uint32_t j = 0; j < npuAddrsVector[i].size(); ++j) {
            void* dst = reinterpret_cast<void*>(npuAddrsVector[i][j]);
            void* src = reinterpret_cast<void*>(reinterpret_cast<char*>(dramAddr) + offset);
            int32_t aclRet = ACLApi::AclrtMemcpyAsync(dst, lengthsVector[i][j], src, lengthsVector[i][j],
                ACL_MEMCPY_HOST_TO_DEVICE, stream);
            if (UNLIKELY(aclRet != 0)) {
                LOG_ERROR("Aclrt memcpy async failed, ret: " << aclRet);
                results[origIndex[i]] = DFC_ERR;
                break;
            }
            offset += lengthsVector[i][j];
        }
    }
    int32_t aclRet = ACLApi::AclrtSynchronizeStream(stream);
    if (UNLIKELY(aclRet != 0)) {
        LOG_ERROR("Aclrt synchronize stream failed, ret: " << aclRet);
    }

    // 释放dram地址
    m_readExecutor->Execute([dramAddr = std::move(dramAddrsVector), count]()->void {
        auto ret = KvcBatchFreeGetAddress(const_cast<void **>(dramAddr.data()), count);
        if (UNLIKELY(ret != DFC_OK)) {
            LOG_ERROR("Kvc batch free dram failed, ret:" << ret);
        }
    });
    return (aclRet == 0) ? UBSIO_KVC_OK : UBSIO_KVC_ERR;
}

KvcError KvcInstance::ReadLocal(ReadParams &params, int *results)
{
    if (params.keys.empty()) {
        return DFC_OK;
    }
    // 1. 优先选择NDS.
    auto &keysVector = params.keys;
    auto &npuAddrsVector = params.npuAddrs;
    auto &lengthsVector = params.lengths;
    auto &oriIndex = params.oriIndex;
    uint32_t keysCount = params.keys.size();
    auto ret = DfcNdsManager::Instance().BatchDirectRead(keysVector, npuAddrsVector, lengthsVector);
    if (ret == DFC_OK) {
        for (uint32_t i = 0; i < keysCount; ++i) {
            results[oriIndex[i]] = DFC_OK;
        }
        return DFC_OK;
    }
    if (ret != DFC_NO_NDS) {
        LOG_WARN("nds read failed, ret: " << ret << ", try to read from disk");
    }

    // 2. NDS失败则从走原生读.
    // 2.1 读到dram.
    std::vector<int32_t> batchResult(keysCount, DFC_ERR);
    std::vector<void *> dramAddrsVector;
    std::vector<size_t> dramAddrsLenVector;
    dramAddrsVector.resize(keysCount);
    dramAddrsLenVector.reserve(keysCount);
    for (uint32_t i = 0; i < keysCount; ++i) {
        dramAddrsLenVector.emplace_back(std::accumulate(lengthsVector[i].begin(), lengthsVector[i].end(), 0LL));
    }
    ret = static_cast<KvcError>(KvBatchGetLocalData(keysVector, dramAddrsVector.data(), dramAddrsLenVector, batchResult, 0));
    if (UNLIKELY(ret != DFC_OK)) {
        LOG_ERROR("Kv cache batch get data failed, ret:" << ret);
        return DFC_ERR;
    }

    // 2.2 将数据从dram拷贝到hbm中.
    H2DParams copyParams(npuAddrsVector, lengthsVector, dramAddrsVector);
    return CopyDataH2D(copyParams, batchResult, oriIndex, results);
}

KvcError KvcInstance::ReadRemote(ReadParams &params, int *results)
{
    if (params.keys.empty()) {
        return DFC_OK;
    }
    auto &keysVector = params.keys;
    auto &npuAddrsVector = params.npuAddrs;
    auto &lengthsVector = params.lengths;
    auto &oriIndex = params.oriIndex;
    uint32_t keysCount = keysVector.size();
    
    std::vector<int32_t> batchResult(keysCount, DFC_ERR);
    std::vector<void *> dramAddrsVector;
    dramAddrsVector.resize(keysCount);
    std::vector<uintptr_t *> npuAddrs;
    npuAddrs.resize(keysCount);
    for (uint32_t i = 0; i < keysCount; ++i) {
        npuAddrs[i] = npuAddrsVector[i].data();
    }
    auto ret = static_cast<KvcError>(KvBatchGetRemoteData(keysVector, npuAddrs.data(), lengthsVector,
                                              dramAddrsVector.data(), batchResult, 0));
    // rh2d
    if (ret == DFC_OK) {
        for (uint32_t i = 0; i < keysCount; ++i) {
            results[oriIndex[i]] = batchResult[i];
        }
        return DFC_OK;
    } else if (ret != CResult::RET_CACHE_IN_DRAM) {
        LOG_ERROR("Kv cache batch get data failed, ret:" << ret);
        return ret;
    }
    // rh2h, need to copy
    H2DParams params(npuAddrsVector, lengthsVector, dramAddrsVector);
    return CopyDataH2D(params, batchResult, oriIndex, results);
}

KvcError KvcInstance::Read(const std::vector<std::string> &keyVector,
                           std::vector<std::vector<uintptr_t>> &npuAddrsVector,
                           const std::vector<std::vector<size_t>> &lengthsVector, int *results)
{
    std::fill(results, results + keyVector.size(), DFC_ERR);
    uint32_t keysCount = keyVector.size();
    std::vector<uint8_t> positions(keysCount);
    auto ret = static_cast<KvcError>(KvcGetPositions(keyVector, positions));
    if (UNLIKELY(ret != DFC_OK)) {
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
    KvcError remoteRet = DFC_OK;
    // read remote //增加远端判断
    if (!remoteParams.keys.empty()) {
        m_readExecutor->Execute([&remoteParams, results, &sem, &remoteRet]()->void {
            remoteRet = ReadRemote(remoteParams, results);
            if (UNLIKELY(remoteRet != DFC_OK)) {
                LOG_ERROR("Kvc batch get reomte data failed, ret:" << remoteRet);
            }
            sem_post(&sem);
        });
    } else {
        sem_post(&sem);
    }

    // read local
    ret = ReadLocal(localParams, results);
    if (UNLIKELY(ret != DFC_OK)) {
        LOG_ERROR("Kvc batch get local data failed, ret:" << ret);
    }
    sme_wait(&sem);
    sem_destroy(&sem);
    return (ret == DFC_OK && remoteRet == DFC_OK) ? DFC_OK : DFC_ERR;
}

} // ubsio
} // ock