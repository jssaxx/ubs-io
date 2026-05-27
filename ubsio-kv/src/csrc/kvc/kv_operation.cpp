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
#include <memory>
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
        return UBSIO_KVC_OK;
    }
    mInited = true;
    return UBSIO_KVC_OK;
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
        return UBSIO_KVC_ERR;
    }
    std::string keyCopy = key;
    return DlBioSdkApi::Put(tenantId, keyCopy.c_str(), reinterpret_cast<char*>(value), (uint64_t)len, location);
}

int32_t KvOperation::KvGetData(const std::string &key, void *value, size_t len)
{
    ObjLocation location;
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), &location);
    if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return UBSIO_KVC_ERR;
    }
    uint64_t outLength = 0;
    return DlBioSdkApi::Get(tenantId, key.c_str(), 0, (uint64_t)len, location, reinterpret_cast<char*>(value), &outLength);
}

int32_t KvOperation::KvDeleteKey(const std::string &key)
{
    ObjLocation location;
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), &location);
    if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return UBSIO_KVC_ERR;
    }
    return DlBioSdkApi::Delete(tenantId, key.c_str(), location);
}

bool KvOperation::KvExistKey(const std::string &key)
{
    ObjLocation location;
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), &location);
    if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return false;
    }
    ObjStat stat;
    stat.size = 0;
    auto ret = DlBioSdkApi::Stat(tenantId, key.c_str(), location, &stat);
    if ((ret != CResult::RET_CACHE_OK) || (stat.size == 0)) {
        return false;
    }
    return true;
}

int32_t KvOperation::KvGetLengthKey(const std::string &key, uint32_t &length)
{
    ObjLocation location;
    CResult status = DlBioSdkApi::CalcLocation(tenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), &location);
    if (UNLIKELY(status != CResult::RET_CACHE_OK)) {
        LOG_ERROR("CalcLocation failed with returned status " << status);
        return UBSIO_KVC_ERR;
    }
    ObjStat stat;
    stat.size = 0;
    auto ret = DlBioSdkApi::Stat(tenantId, key.c_str(), location, &stat);
    if ((ret != CResult::RET_CACHE_OK) || (stat.size == 0)) {
        LOG_ERROR("Exist data from meta failed, ret: " << ret);
        return UBSIO_KVC_ERR;
    }
    length = stat.size;
    return UBSIO_KVC_OK;
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
            return UBSIO_KVC_ERR;
        }
        locationVec[i] = location;
        keys[i] = key[i].c_str();
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
            return UBSIO_KVC_ERR;
        }
        locationVec[i] = location;
        keys[i] = key[i].c_str();
    }

    int ret = DlBioSdkApi::BatchExist(tenantId, keys.data(), locationVec.data(), keysCount, results);
    if (ret != UBSIO_KVC_OK) {
        LOG_ERROR("BioBatchExist failed with returned status " << ret);
        return UBSIO_KVC_ERR;
    }
    return UBSIO_KVC_OK;
}

int32_t KvOperation::BatchKvPutData(const std::vector<std::string> &key, std::vector<void*> &value,
    std::vector<size_t> &lengths, std::vector<int> &results)
{
    LOG_WARN("wxt BatchKvPutData");
    sem_t sem;
    sem_init(&sem, 0, 0);
    auto batchSize = key.size();
    auto keySize = std::make_shared<std::atomic<uint32_t>>(batchSize);
    for (auto i = 0; i < batchSize; i++) {
        auto index = i;
        auto keyCopy = key[i];
        auto valueCopy = value[i];
        auto lengthCopy = lengths[i];
        std::function<void()> func = [this, index, keyCopy, valueCopy, lengthCopy, &results, keySize, &sem, batchSize]() {
            auto ret = KvPutData(keyCopy, valueCopy, lengthCopy);
            if (ret != UBSIO_KVC_OK) {
                LOG_ERROR("Batch put data failed, ret: " << ret << " batch num: " << batchSize << " i:" << index);
            }
            results[index] = ret;
            if (keySize->fetch_sub(1) == 1) {
                sem_post(&sem);
            }
        };

        auto result = mKvExecutor->Execute(func);
        if (!result) {
            LOG_ERROR("Execute batch put data, batch num: " << key.size() << " i:" << i);
            sem_destroy(&sem);
            return UBSIO_KVC_ERR;
        }
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    return UBSIO_KVC_OK;
}

int32_t KvOperation::BatchKvDeleteKey(const std::vector<std::string> &key, std::vector<int> &results)
{
    sem_t sem;
    sem_init(&sem, 0, 0);
    std::atomic<uint32_t> keySize(key.size());
    for (auto i = 0; i < key.size(); i++) {
        auto index = i;
        std::function<void()> func = [&, index]() {
            auto ret = KvDeleteKey(key[index]);
            if (ret != UBSIO_KVC_OK) {
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
            return UBSIO_KVC_ERR;
        }
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    return UBSIO_KVC_OK;
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
            auto ret = KvGetLengthKey(key[index], lengths[index]);
            if (ret != UBSIO_KVC_OK) {
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
            return UBSIO_KVC_ERR;
        }
    }
    sem_wait(&sem);
    sem_destroy(&sem);
    return UBSIO_KVC_OK;
}
}; // namespace ubsio
} // namespace ock