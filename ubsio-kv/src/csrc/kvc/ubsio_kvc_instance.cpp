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

DFCError KvcInstance::Initialize(int32_t device) noexcept
{
    readPool_ = ExecutorService::Create(DFC_IO_INSTANCE_THREAD_NUM);
    if (readPool_ == nullptr) {
        LOG_ERROR("Nds thread pool init failed.");
        return DFC_ERR;
    }
    auto success = readPool_->Start();
    if (!success) {
        readPool_ = nullptr;
        LOG_ERROR("kv instance thread pool start failed.");
        return DFC_ERR;
    }
    deviceId_ = device;
    return DFC_OK;
}

DFCError KvcInstance::Read(const std::vector<std::string> &keyVector,
                           std::vector<std::vector<uintptr_t>> &npuAddrsVector,
                           const std::vector<std::vector<size_t>> &lengthsVector, int *results) noexcept
{
    // 先走nds
    auto ndsRet = DfcNdsManager::Instance().BatchDirectRead(keyVector, npuAddrsVector, lengthsVector);
    uint32_t keysCount = keyVector.size();
    if (ndsRet == DFC_OK) {
        std::fill(results, results + keysCount, DFC_OK);
        return DFC_OK;
    }
    if (ndsRet != DFC_NO_NDS) {
        LOG_WARN("nds read failed, ret: " << ndsRet << ", try to read from disk");
    }
    // 失败则从盘上加载
    // 1.读到dram
    std::vector<int> batchResult(keysCount, DFC_ERR);
    std::vector<void *> dramAddrsVector;
    std::vector<size_t> dramAddrsLenVector;
    dramAddrsVector.resize(keysCount);
    dramAddrsLenVector.reserve(keysCount);
    for (uint32_t i = 0; i < keysCount; ++i) {
        dramAddrsLenVector.emplace_back(std::accumulate(lengthsVector[i].begin(), lengthsVector[i].end(), 0LL));
    }
    auto ret = DfcBatchGetData(keyVector, dramAddrsVector.data(), dramAddrsLenVector, batchResult, 0);
    if (ret != DFC_OK) {
        LOG_ERROR("Dfc batch get failed");
        return DFC_ERR;
    }

    // 2.dram->hbm
    void* stream = DfcStreamManager::GetAclStream();
    if (stream == nullptr) {
        LOG_ERROR("stream is null, can not do memcpy");
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
            ret = ACLApi::AclrtMemcpyAsync(dst, lengthsVector[i][j], src, lengthsVector[i][j],
                ACL_MEMCPY_HOST_TO_DEVICE, stream);
            if (ret != 0) {
                LOG_ERROR("AclrtMemcpyAsync failed, ret: " << ret);
                results[i] = DFC_ERR;
                break;
            }
            offset += lengthsVector[i][j];
        }
    }
    ret = ACLApi::AclrtSynchronizeStream(stream);
    if (ret != 0) {
        LOG_ERROR("AclrtSynchronizeStream failed, ret: " << ret);
    }
    // 3.释放dram地址
    readPool_->Execute([dramAddrsVector, keysCount]()->void {
        auto dfcRet = DfcBatchFreeGetAddress(const_cast<void **>(dramAddrsVector.data()), keysCount);
        if (dfcRet != DFC_OK) {
            LOG_ERROR("Dfc batch free dram address failed");
        }
    });
    return ret == 0 ? DFC_OK : DFC_ERR;
}

} // ubsio
} // ock