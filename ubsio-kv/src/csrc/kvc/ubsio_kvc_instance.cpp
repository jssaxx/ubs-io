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
#include "ubsio_kvc_operation.h"
#include "ubsio_kvc_stream_manager.h"
#include "dl_acl_api.h"
#include "ubsio_nds_manager.h"
#include "ubsio_kvc_instance.h"

namespace ock {
namespace ubsio {

using namespace ock::ubsio::nds;
constexpr int KVC_INSTANCE_THREAD_NUM = 2;

KvcInstance &KvcInstance::Instance() noexcept
{
    static KvcInstance instance;
    return instance;
}

KvcError KvcInstance::Initialize(int32_t device) noexcept
{
    // 创建batch read的executor线程池.
    m_readExecutor = ExecutorService::Create(KVC_INSTANCE_THREAD_NUM);
    if (m_readExecutor == nullptr) {
        LOG_ERROR("Nds thread pool init failed.");
        return DFC_ERR;
    }
    auto success = m_readExecutor->Start();
    if (!success) {
        m_readExecutor = nullptr;
        LOG_ERROR("kv instance thread pool start failed.");
        return DFC_ERR;
    }

    m_deviceId = device;
    return DFC_OK;
}

KvcError KvcInstance::Read(const std::vector<std::string> &keyVector,
                           std::vector<std::vector<uintptr_t>> &npuAddrsVector,
                           const std::vector<std::vector<size_t>> &lengthsVector, int *results) noexcept
{
    // 1. 优先选择NDS.
    auto ret = DfcNdsManager::Instance().BatchDirectRead(keyVector, npuAddrsVector, lengthsVector);
    uint32_t keysCount = keyVector.size();
    if (LIKELY(ret == DFC_OK)) {
        std::fill(results, results + keysCount, DFC_OK);
        return DFC_OK;
    }
    if (ret != DFC_NO_NDS) {
        LOG_WARN("nds read failed, ret: " << ret << ", try to read from disk");
    }

    // 2. NDS失败则从走原生读.
    // 2.1 读到dram.
    std::vector<int> batchResult(keysCount, DFC_ERR);
    std::vector<void *> dramAddrsVector;
    std::vector<size_t> dramAddrsLenVector;
    dramAddrsVector.resize(keysCount);
    dramAddrsLenVector.reserve(keysCount);
    for (uint32_t i = 0; i < keysCount; ++i) {
        dramAddrsLenVector.emplace_back(std::accumulate(lengthsVector[i].begin(), lengthsVector[i].end(), 0LL));
    }
    ret = static_cast<KvcError>(KvcBatchGetData(keyVector, dramAddrsVector.data(), dramAddrsLenVector, batchResult, 0));
    if (UNLIKELY(ret != DFC_OK)) {
        LOG_ERROR("Kv cache batch get data failed, ret:" << ret);
        return DFC_ERR;
    }

    // 2.2 将数据从dram拷贝到hbm中.
    void* stream = KvcStreamManager::GetAclStream();
    if (UNLIKELY(stream == nullptr)) {
        LOG_ERROR("Kv cache stream is nullptr, can not do memcpy");
        return DFC_ERR;
    }
    for (uint32_t i = 0; i < keysCount; ++i) {
        results[i] = batchResult[i];
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
                results[i] = DFC_ERR;
                break;
            }
            offset += lengthsVector[i][j];
        }
    }
    int32_t aclRet = ACLApi::AclrtSynchronizeStream(stream);
    if (UNLIKELY(aclRet != 0)) {
        LOG_ERROR("Aclrt synchronize stream failed, ret: " << aclRet);
    }

    // 3.3 释放dram地址
    m_readExecutor->Execute([dramAddrsVector, keysCount]()->void {
        auto ret = KvcBatchFreeGetAddress(const_cast<void **>(dramAddrsVector.data()), keysCount);
        if (UNLIKELY(ret != DFC_OK)) {
            LOG_ERROR("Kvc batch free dram failed, ret:" << ret);
        }
    });
    return (aclRet == 0) ? DFC_OK : DFC_ERR;
}

} // ubsio
} // ock