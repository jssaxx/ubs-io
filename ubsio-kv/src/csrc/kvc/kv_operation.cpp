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

#include <fcntl.h>
#include <sys/mman.h>
#include <cstring>
#include <vector>
#include "bio_c.h"
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_err.h"
#include "dl_biosdk_api.h"
#include "kv_operation.h"

namespace ock {
namespace ubsio {

KvOperation *KvOperation::gInstance = nullptr;
std::mutex KvOperation::gLock;

int32_t KvOperation::Initialize(const std::string &path)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mInited) {
        LOG_INFO("Kv operation has been initialized");
        return DFC_OK;
    }
    mInited = true;
    return DFC_OK;
}

void KvOperation::UnInitialize(void)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mInited) {
        LOG_INFO("KvOperation has not been initialized");
        return;
    }
    mInited = false;
}

int32_t KvOperation::KvPutData(const std::string &key, void *value, size_t len)
{
    ObjLocation location;
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), &location);
    if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
        LOG_ERROR("Calc location failed, status:" << status);
        return DFC_ERR;
    }
    return DlBioSdkApi::Put(tenantId, key.c_str(), reinterpret_cast<char*>(value), (uint64_t)len, location);
}

int32_t KvOperation::BatchKvGetData(const std::vector<std::string> &key, void **bufs,
    std::vector<size_t> &lengths, std::vector<int> &results)
{
    uint32_t keysCount = key.size();
    std::vector<ObjLocation> locationVec(keysCount);
    std::vector<uint64_t> realLength(keysCount);
    std::vector<uint64_t> offsets(keysCount, 0);
    std::vector<const char*> keys(keysCount);
    for (size_t i = 0; i < keysCount; i++) {
        ObjLocation location;
        CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key[i])), &location);
        if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
            LOG_ERROR("Calc location failed, status:" << status);
            return DFC_ERR;
        }
        locationVec.emplace_back(location);
        keys.emplace_back(key[i].c_str());
    }

    return DlBioSdkApi::BatchGet(tenantId, keys.data(), keysCount, offsets.data(), lengths.data(), locationVec.data(),
        reinterpret_cast<uintptr_t *>(bufs), realLength.data(), results.data());
}

int32_t KvOperation::BatchKvExistKey(const std::vector<std::string> &key, bool *results)
{
    uint32_t keysCount = key.size();
    std::vector<ObjLocation> locationVec(keysCount);
    std::vector<const char *> keys(keysCount);
    for (size_t i = 0; i < keysCount; i++) {
        ObjLocation location;
        CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key[i])), &location);
        if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
            LOG_ERROR("Calc location failed, status:" << status);
            return DFC_ERR;
        }
        locationVec.emplace_back(location);
        keys.emplace_back(key[i].c_str());
    }

    int ret = DlBioSdkApi::BatchExist(tenantId, keys.data(), locationVec.data(), keysCount, results);
    if (ret != DFC_OK) {
        LOG_ERROR("BioBatchExist failed with returned status " << ret);
        return DFC_ERR;
    }
    return DFC_OK;
}

}; // namespace ubsio
} // namespace ock