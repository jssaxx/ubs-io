/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
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

int32_t UfsGet(const char *key, char *value, const size_t len, const uint64_t off)
{
    return UnderFs::Instance()->Get(key, value, len, off);
}

int32_t UfsDelete(const char *key)
{
    return UnderFs::Instance()->Delete(key);
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
    bioConfig.underFsType = config.underFsType;
    bioConfig.cephConfig.cfgPath = config.cephConfig.cfgPath;
    bioConfig.cephConfig.cluster = config.cephConfig.cluster;
    bioConfig.cephConfig.user = config.cephConfig.user;
    bioConfig.cephConfig.pools[0] = config.cephConfig.poolName;
    bioConfig.hdfsConfig.nameNode = config.hdfsConfig.nameNode;
    bioConfig.hdfsConfig.workingPath = config.hdfsConfig.workingPath;
    UnderFs::InitUnderFsConfig(bioConfig);
}
