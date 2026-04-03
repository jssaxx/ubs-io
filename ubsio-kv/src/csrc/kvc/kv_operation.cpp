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
    std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
    if (status != CResult::RET_CACHE_OK) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return DFC_ERR;
    }
    return DlBioSdkApi::Put(tenantId, key.c_str(), reinterpret_cast<char*>(value), (uint64_t)len, *location);
}

int32_t KvOperation::KvGetData(const std::string &key, void *value, size_t len)
{
    std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
    if (status != CResult::RET_CACHE_OK) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return DFC_ERR;
    }
    uint64_t outLength = 0;
    return DlBioSdkApi::Get(tenantId, key.c_str(), 0, (uint64_t)len, *location, reinterpret_cast<char*>(value), &outLength);
}

int32_t KvOperation::KvDeleteKey(const std::string &key)
{
    std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
    if (status != CResult::RET_CACHE_OK) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return DFC_ERR;
    }
    return DlBioSdkApi::Delete(tenantId, key.c_str(), *location);
}

bool KvOperation::KvExistKey(const std::string &key)
{
    std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
    if (status != CResult::RET_CACHE_OK) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return false;
    }
    ObjStat stat;
    stat.size = 0;
    auto ret = DlBioSdkApi::Stat(tenantId, key.c_str(), *location, &stat);
    if ((ret != CResult::RET_CACHE_OK) || (stat.size == 0)) {
        return false;
    }
    return true;
}

int32_t KvOperation::KvGetLengthKey(const std::string &key, uint32_t &length)
{
    std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
    if (status != CResult::RET_CACHE_OK) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return -1;
    }
    ObjStat stat;
    stat.size = 0;
    auto ret = DlBioSdkApi::Stat(tenantId, key.c_str(), *location, &stat);
    if ((ret != CResult::RET_CACHE_OK) || (stat.size == 0)) {
        LOG_ERROR("Exist data from meta failed, ret: " << ret);
        return DFC_ERR;
    }
    length = stat.size;
    return DFC_OK;
}

int32_t KvOperation::BatchKvGetData(const std::vector<std::string> &key, void **bufs,
    std::vector<size_t> &lengths, std::vector<int> &results)
{
    uint32_t keys_count = key.size();
    std::vector<ObjLocation> locationVec;
    std::vector<uint64_t> realLength;
    std::vector<uint64_t> offsets(keys_count, 0);
    std::vector<const char*> keys;
    locationVec.reserve(keys_count);
    realLength.reserve(keys_count);
    keys.reserve(keys_count);
    
    for (size_t i = 0; i < keys_count; i++) {
        std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
        CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key[i])), location.get());
        if (status != CResult::RET_CACHE_OK) {
            LOG_ERROR("CalcLocation failed with returned status " << status);
            return -1;
        }
        locationVec.emplace_back(*location);
        keys.emplace_back(key[i].c_str());
    }

    return DlBioSdkApi::BatchGet(tenantId, keys.data(), keys_count, offsets.data(), lengths.data(), locationVec.data(),
        reinterpret_cast<uintptr_t *>(bufs), realLength.data(), results.data());
}

int32_t KvOperation::BatchKvPutData(const std::vector<std::string> &key, std::vector<void*> &value,
    std::vector<size_t> &lengths, std::vector<int> &results)
{
    sem_t sem;
    sem_init(&sem, 0, 0);
    std::atomic<uint32_t> keySize(key.size());
    for (auto i = 0; i < key.size(); i++) {
        auto index = i;
        std::function<void()> func = [&, index]() {
            auto ret = KvPutData(key[index], value[index], lengths[index]);
            if (ret != DFC_OK) {
                LOG_ERROR("Batch put data failed, ret: " << ret << " batch num: " << key.size() << " i:" << index);
            }
            results[index] = ret;
            if (keySize.fetch_sub(1) == 1) {
                // 最后一个任务唤醒主线程
                sem_post(&sem);
            }
        };

        auto result = mKvExecutor->Execute(func);
        if (!result) {
            LOG_ERROR("Execute batch put data, batch num: " << key.size() << " i:" << i);
            sem_destroy(&sem);
            return DFC_ERR;
        }
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    return DFC_OK;
}

int32_t KvOperation::BatchKvExistKey(const std::vector<std::string> &key, bool *results)
{
    uint32_t keys_count = key.size();
    std::vector<ObjLocation> locationVec;
    std::vector<const char *> keys;
    locationVec.reserve(keys_count);
    keys.reserve(keys_count);

    for (size_t i = 0; i < keys_count; i++) {
        std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
        CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key[i])), location.get());
        if (status != CResult::RET_CACHE_OK) {
            LOG_ERROR("CalcLocation failed with returned status " << status);
            return -1;
        }
        locationVec.emplace_back(*location);
        keys.emplace_back(key[i].c_str());
    }
    int ret = DlBioSdkApi::BatchExist(tenantId, keys.data(), locationVec.data(), keys_count, results);
    if (ret != DFC_OK) {
        LOG_ERROR("BioBatchExist failed with returned status " << ret);
        return -1;
    }

    return DFC_OK;
}

int32_t KvOperation::BatchKvDeleteKey(const std::vector<std::string> &key, std::vector<int> &results)
{
    sem_t sem;
    sem_init(&sem, 0, 0);
    std::atomic<uint32_t> keySize(key.size());
    for (auto i = 0; i < key.size(); i++) {
        auto index = i;
        std::function<void()> func = [&, index]() {
            auto ret = ConKvDeleteKey(key[index]);
            if (ret != DFC_OK) {
                LOG_ERROR("Batch delete key failed, ret: " << ret << " batch num: " << key.size() << " i:" << index);
            }
            results[index] = ret;
            if (keySize.fetch_sub(1) == 1) {
                // 最后一个任务唤醒主线程
                sem_post(&sem);
            }
        };

        auto result = mKvExecutor->Execute(func);
        if (!result) {
            LOG_ERROR("Execute batch delete key, batch num: " << key.size() << " i:" << i);
            sem_destroy(&sem);
            return DFC_ERR;
        }
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    return DFC_OK;
}

int32_t KvOperation::BatchGetLengthKey(const std::vector<std::string> &key, std::vector<uint32_t> &lengths,
                                       std::vector<int> &results)
{
    sem_t sem;
    sem_init(&sem, 0, 0);
    std::atomic<uint32_t> keySize(key.size());
    for (auto i = 0; i < key.size(); i++) {
        auto index = i;
        std::function<void()> func = [&, index]() {
            auto ret = ConKvGetLengthKey(key[index], lengths[index]);
            if (ret != DFC_OK) {
                LOG_ERROR("Batch get length failed, ret: " << ret << " batch num: " << key.size() << " i:" << index);
            }
            results[index] = ret;
            if (keySize.fetch_sub(1) == 1) {
                // 最后一个任务唤醒主线程
                sem_post(&sem);
            }
        };

        auto result = mKvExecutor->Execute(func);
        if (!result) {
            LOG_ERROR("Execute batch get length, batch num: " << key.size() << " i:" << i);
            sem_destroy(&sem);
            return DFC_ERR;
        }
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    return DFC_OK;
}

}; // namespace dfc
} // namespace ock