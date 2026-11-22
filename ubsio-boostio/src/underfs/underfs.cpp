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

#include "underfs.h"

using namespace ock::bio;

int32_t UfsInit()
{
    UnderFsPtr underFsPtr = UnderFs::Instance();
    if (underFsPtr == nullptr) {
        LOG_ERROR("Create underfs instance fail.");
        return BIO_ERR;
    }
    return underFsPtr->Init();
}

void UfsStop()
{
    UnderFs::Instance()->Stop();
}

int32_t UfsPut(const char *key, const char *value, const size_t len)
{
    return UnderFs::Instance()->Put(key, value, len);
}

int32_t UfsPutV(const char *key, const struct iovec *iov, size_t iovcnt)
{
    const auto type = UnderFsConfig::Instance()->GetUnderFsConfig().underFsType;
    if (UNLIKELY(type != BIO_UNDERFS_LOCAL && type != BIO_UNDERFS_SHARED)) {
        return BIO_INVALID_PARAM;
    }
    auto posix = std::dynamic_pointer_cast<LocalSystem>(UnderFs::Instance());
    if (UNLIKELY(posix == nullptr)) {
        return BIO_NOT_READY;
    }
    return posix->PutV(key, iov, iovcnt);
}

int32_t UfsSetLocalMetaEventCallback(UfsLocalMetaEventCallback callback, void *context)
{
    if (UNLIKELY(UnderFsConfig::Instance()->GetUnderFsConfig().underFsType != BIO_UNDERFS_LOCAL)) {
        return BIO_INVALID_PARAM;
    }
    auto local = std::dynamic_pointer_cast<LocalSystem>(UnderFs::Instance());
    if (UNLIKELY(local == nullptr)) {
        return BIO_NOT_READY;
    }
    local->SetMetaEventCallback(callback, context);
    return BIO_OK;
}

int32_t UfsScanLocalKeys(UfsLocalKeyInfo **items, uint64_t *count)
{
    if (UNLIKELY(items == nullptr || count == nullptr)) {
        return BIO_INVALID_PARAM;
    }
    *items = nullptr;
    *count = 0;
    if (UNLIKELY(UnderFsConfig::Instance()->GetUnderFsConfig().underFsType != BIO_UNDERFS_LOCAL)) {
        return BIO_INVALID_PARAM;
    }
    auto local = std::dynamic_pointer_cast<LocalSystem>(UnderFs::Instance());
    if (UNLIKELY(local == nullptr)) {
        return BIO_NOT_READY;
    }
    return local->ScanKeys(items, count);
}

int32_t UfsGetStorageMetrics(UfsStorageMetrics *metrics)
{
    if (UNLIKELY(metrics == nullptr ||
        UnderFsConfig::Instance()->GetUnderFsConfig().underFsType != BIO_UNDERFS_LOCAL)) {
        return BIO_INVALID_PARAM;
    }
    auto local = std::dynamic_pointer_cast<LocalSystem>(UnderFs::Instance());
    if (UNLIKELY(local == nullptr)) {
        return BIO_NOT_READY;
    }
    return local->GetStorageMetrics(*metrics);
}

int32_t UfsGet(const char *key, char *value, const size_t len, const uint64_t off)
{
    return UnderFs::Instance()->Get(key, value, len, off);
}

int32_t UfsGetWithRealLen(const char *key, char *value, const size_t len, const uint64_t off, size_t *realLen)
{
    if (UNLIKELY(realLen == nullptr)) {
        return BIO_INVALID_PARAM;
    }
    return UnderFs::Instance()->GetWithRealLen(key, value, len, off, *realLen);
}

int32_t UfsDelete(const char *key)
{
    return UnderFs::Instance()->Delete(key);
}

int32_t UfsExist(const char *key)
{
    return UnderFs::Instance()->Exist(key);
}

int32_t UfsStat(const char *key, ObjStatInfo *objStat)
{
    FileSystem::ObjStat statInfo;
    auto ret = UnderFs::Instance()->Stat(key, statInfo);
    if (LIKELY(ret == BIO_OK)) {
        objStat->size = statInfo.size;
        objStat->time = statInfo.time;
    }
    return ret;
}

int32_t UfsList(const char *prefix, ObjStatInfo **objStat, int *objNum)
{
    std::unordered_map<std::string, FileSystem::ObjStat> listObjs;
    auto ret = UnderFs::Instance()->List(prefix, listObjs);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    *objStat = (ObjStatInfo *) malloc(sizeof(ObjStatInfo) * (listObjs.size()));
    if (*objStat == nullptr) {
        return BIO_UFS_IOERR;
    }
    uint32_t index = 0;
    for (auto &obj: listObjs) {
        CopyKey((*objStat)[index].key, obj.first.c_str(), MAX_KEY_SIZE);
        (*objStat)[index].size = obj.second.size;
        (*objStat)[index].time = obj.second.time;
        index++;
    }
    *objNum = index;
    return ret;
}

void UfsInitUnderFsConfig(UnderFsConfigInfo config)
{
    BioConfig::UnderFsConfig bioConfig;
    bioConfig.underFsType = ParseUnderFsType(config.underFsType == nullptr ? "" : config.underFsType);
    bioConfig.cephConfig.cfgPath = config.cephConfig.cfgPath;
    bioConfig.cephConfig.cluster = config.cephConfig.cluster;
    bioConfig.cephConfig.user = config.cephConfig.user;
    bioConfig.cephConfig.pools[0] = config.cephConfig.poolName;
    bioConfig.hdfsConfig.nameNode = config.hdfsConfig.nameNode;
    bioConfig.hdfsConfig.workingPath = config.hdfsConfig.workingPath;
    bioConfig.localConfig.rootPath = config.localConfig.rootPath;
    if (bioConfig.underFsType == BIO_UNDERFS_LOCAL) {
        bioConfig.localConfig.slotIndex = config.localConfig.slotIndex;
        bioConfig.localConfig.deviceCount = config.localConfig.deviceCount;
        bioConfig.localConfig.evictionPolicy = ParseFileEvictionPolicy(config.localConfig.evictionPolicy);
        bioConfig.localConfig.checkIntervalSeconds = config.localConfig.checkIntervalSeconds;
    }
    UnderFs::InitUnderFsConfig(bioConfig);
}
