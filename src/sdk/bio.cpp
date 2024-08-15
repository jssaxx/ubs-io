/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include <iostream>
#include <cstring>
#include "securec.h"
#include "message.h"
#include "bio_functions.h"
#include "bio_client_log.h"
#include "bio_trace.h"
#include "bio_client.h"
#include "bio.h"

namespace ock {
namespace bio {
BioClientPtr gClient = BioClient::Instance();

inline static CResult ToCResult(const BResult ret)
{
    switch (ret) {
        case BIO_OK:
            return RET_CACHE_OK;
        case BIO_ERR:
        case BIO_INNER_ERR:
        case BIO_NOT_READY:
            return RET_CACHE_ERROR;
        case BIO_INVALID_PARAM:
            return RET_CACHE_EPERM;
        case BIO_ALLOC_FAIL:
            return RET_CACHE_NEED_RETRY;
        case BIO_NOT_INITIALIZED:
            return RET_CACHE_NOT_READY;
        case BIO_NOT_EXISTS:
            return RET_CACHE_NOT_FOUND;
        case BIO_CHECK_PT_FAIL:
            return RET_CACHE_PT_FAULT;
        case BIO_READ_EXCEED:
            return RET_CACHE_READ_EXCEED;
        case BIO_KEY_CONFLICT:
            return RET_CACHE_CONFLICT;
        case BIO_DISK_IOERR:
            return RET_CACHE_DISK_FAULT;
        case BIO_UFS_IOERR:
            return RET_CACHE_UFS_FAULT;
        default:
            return RET_CACHE_NEED_RETRY;
    }
}

inline static void StatisticPutIoSize(uint64_t length)
{
    if (length <= IO_SIZE_4K) {
        BIO_TRACE_START(SDK_TRACE_W_S_1_4K);
        BIO_TRACE_END(SDK_TRACE_W_S_1_4K, BIO_OK);
    } else if (length <= IO_SIZE_8K) {
        BIO_TRACE_START(SDK_TRACE_W_S_4_8K);
        BIO_TRACE_END(SDK_TRACE_W_S_4_8K, BIO_OK);
    } else if (length <= IO_SIZE_64K) {
        BIO_TRACE_START(SDK_TRACE_W_S_8_64K);
        BIO_TRACE_END(SDK_TRACE_W_S_8_64K, BIO_OK);
    } else if (length <= IO_SIZE_128K) {
        BIO_TRACE_START(SDK_TRACE_W_S_64_128K);
        BIO_TRACE_END(SDK_TRACE_W_S_64_128K, BIO_OK);
    } else if (length <= IO_SIZE_256K) {
        BIO_TRACE_START(SDK_TRACE_W_S_128_256K);
        BIO_TRACE_END(SDK_TRACE_W_S_128_256K, BIO_OK);
    } else if (length <= IO_SIZE_1M) {
        BIO_TRACE_START(SDK_TRACE_W_S_256K_1M);
        BIO_TRACE_END(SDK_TRACE_W_S_256K_1M, BIO_OK);
    } else if (length <= IO_SIZE_2M) {
        BIO_TRACE_START(SDK_TRACE_W_S_1_2M);
        BIO_TRACE_END(SDK_TRACE_W_S_1_2M, BIO_OK);
    } else if (length <= IO_SIZE_4M) {
        BIO_TRACE_START(SDK_TRACE_W_S_2_4M);
        BIO_TRACE_END(SDK_TRACE_W_S_2_4M, BIO_OK);
    } else {
        BIO_TRACE_START(SDK_TRACE_W_S_4M);
        BIO_TRACE_END(SDK_TRACE_W_S_4M, BIO_OK);
    }
}

inline static void StatisticGetIoSize(uint64_t length)
{
    if (length <= IO_SIZE_4K) {
        BIO_TRACE_START(SDK_TRACE_R_S_1_4K);
        BIO_TRACE_END(SDK_TRACE_R_S_1_4K, BIO_OK);
    } else if (length <= IO_SIZE_8K) {
        BIO_TRACE_START(SDK_TRACE_R_S_4_8K);
        BIO_TRACE_END(SDK_TRACE_R_S_4_8K, BIO_OK);
    } else if (length <= IO_SIZE_64K) {
        BIO_TRACE_START(SDK_TRACE_R_S_8_64K);
        BIO_TRACE_END(SDK_TRACE_R_S_8_64K, BIO_OK);
    } else if (length <= IO_SIZE_128K) {
        BIO_TRACE_START(SDK_TRACE_R_S_64_128K);
        BIO_TRACE_END(SDK_TRACE_R_S_64_128K, BIO_OK);
    } else if (length <= IO_SIZE_256K) {
        BIO_TRACE_START(SDK_TRACE_R_S_128_256K);
        BIO_TRACE_END(SDK_TRACE_R_S_128_256K, BIO_OK);
    } else if (length <= IO_SIZE_1M) {
        BIO_TRACE_START(SDK_TRACE_R_S_256K_1M);
        BIO_TRACE_END(SDK_TRACE_R_S_256K_1M, BIO_OK);
    } else if (length <= IO_SIZE_2M) {
        BIO_TRACE_START(SDK_TRACE_R_S_1_2M);
        BIO_TRACE_END(SDK_TRACE_R_S_1_2M, BIO_OK);
    } else if (length <= IO_SIZE_4M) {
        BIO_TRACE_START(SDK_TRACE_R_S_2_4M);
        BIO_TRACE_END(SDK_TRACE_R_S_2_4M, BIO_OK);
    } else {
        BIO_TRACE_START(SDK_TRACE_R_S_4M);
        BIO_TRACE_END(SDK_TRACE_R_S_4M, BIO_OK);
    }
}

inline static bool KeyValid(const char *key)
{
    if (UNLIKELY(key == nullptr || strlen(key) >= KEY_MAX_SIZE)) {
        return false;
    }
    return true;
}

CResult Bio::CalculateLocation(uint64_t objectId, ObjLocation &location)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }
    return ToCResult(gClient->CalculateLocation(objectId, mAffinity, location));
}

CResult Bio::Put(const char *key, const char *value, uint64_t length, const ObjLocation &location)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key) || value == nullptr || length == 0 || length > BIO_IO_MAX_LEN)) {
        CLIENT_LOG_ERROR("Invalid put parameter, key or value pointers is nullptr, length:" << length <<
            ", max length:" << (BIO_IO_MAX_LEN / NO_1024 / NO_1024) << "(Mb).");
        return RET_CACHE_EPERM;
    }

    StatisticPutIoSize(length);
    BIO_TRACE_START(SDK_TRACE_PUT);
    MirrorClient::MirrorPut param = { { mTenantId, mAffinity, mStrategy }, const_cast<char *>(key),
        const_cast<char *>(value), length, location, 0 };
    BResult ret = gClient->Put(param);
    BIO_TRACE_END(SDK_TRACE_PUT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Put value failed, ret:" << ret << ", key:" << key << ", length:" << length <<
            ", location0:" << location.location[0] << ", location1:" << location.location[1] << ".");
    } else {
        CLIENT_LOG_DEBUG("Put value success, key:" << key << ", length:" << length << ", location0:" <<
            location.location[0] << ", location1:" << location.location[1] << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Put(const char *key, CacheSpaceDesc &spaceInfo)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key) || spaceInfo.addressNum == 0 || spaceInfo.descriptorSize == 0)) {
        return RET_CACHE_EPERM;
    }

    CLIENT_LOG_DEBUG("Put value with space key:" << key << ", location0:" << spaceInfo.loc.location[0] <<
        ", location1:" << spaceInfo.loc.location[1] << ", address num:" << spaceInfo.addressNum << ", address0:" <<
        spaceInfo.address[0].address << ", address0 size:" << spaceInfo.address[0].size << ", address1:" <<
        spaceInfo.address[1].address << ", address1 size:" << spaceInfo.address[1].size << ".");

    uint32_t length = spaceInfo.address[0].size + spaceInfo.address[1].size;
    MirrorClient::MirrorPut param = { { mTenantId, mAffinity, mStrategy }, const_cast<char *>(key),
        nullptr, length, spaceInfo.loc, 0 };
    StatisticPutIoSize(length);
    BIO_TRACE_START(SDK_TRACE_PUT);
    BResult ret = gClient->Put(param, spaceInfo);
    BIO_TRACE_END(SDK_TRACE_PUT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Put copy free value failed, ret:" << ret << ", key:" << key << ", length:" << length <<
            ", location0:" << spaceInfo.loc.location[0] << ", location1:" << spaceInfo.loc.location[1] << ".");
    } else {
        CLIENT_LOG_DEBUG("Put copy free value success, key:" << key << ", length:" << length << ", location0:" <<
            spaceInfo.loc.location[0] << ", location1:" << spaceInfo.loc.location[1] << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Get(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location, char *value,
    uint64_t &realLength)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key) || value == nullptr || length == 0)) {
        CLIENT_LOG_ERROR("Invalid get parameter, key or value pointers is nullptr, length:" << length << ".");
        return RET_CACHE_EPERM;
    }
    if (UNLIKELY((offset + length) > BIO_IO_MAX_LEN)) {
        CLIENT_LOG_ERROR("Read length exceed limit, offset" << offset << "length:" << length << ", limits:" <<
            (BIO_IO_MAX_LEN / NO_1024 / NO_1024) << "(Mb).");
        return RET_CACHE_READ_EXCEED;
    }

    StatisticGetIoSize(length);
    BIO_TRACE_START(SDK_TRACE_GET);
    MirrorClient::MirrorGet param{ { mTenantId, mAffinity, mStrategy }, key, value, offset, length, location };
    BResult ret = gClient->Get(param, realLength);
    BIO_TRACE_END(SDK_TRACE_GET, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Get value failed, ret:" << ret << ", key:" << key << ", offset:" << offset << ", length:" <<
            length << ", location0:" << location.location[0] << ", location1:" << location.location[1] << ".");
    } else {
        CLIENT_LOG_DEBUG("Get value success, key:" << key << ", offset:" << offset << ", length:" << length <<
            ", realLen:" << realLength << ", location0:" << location.location[0] << ", location1:" <<
            location.location[1] << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Delete(const char *key, const ObjLocation &location)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key))) {
        CLIENT_LOG_ERROR("Invalid delete parameter, key:" << key << ".");
        return RET_CACHE_EPERM;
    }

    BIO_TRACE_START(SDK_TRACE_DELETE);
    BResult ret = gClient->DeleteKey(key, location);
    BIO_TRACE_END(SDK_TRACE_DELETE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Delete key failed, ret:" << ret << ", key:" << key << ".");
    } else {
        CLIENT_LOG_DEBUG("Delete key success, key:" << key << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Load(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
    const BioLoadCallback callback, void *context)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key) || context == nullptr || offset != 0 || length == 0 ||
        (offset + length) > BIO_IO_MAX_LEN)) {
        CLIENT_LOG_ERROR("Invalid load parameter, key:" << key << ".");
        return RET_CACHE_EPERM;
    }

    LoadCallback cb = [key, callback](void *context, BResult result) {
        if (result != BIO_OK) {
            CLIENT_LOG_ERROR("Load failed, ret:" << result << ", key:" << key << ".");
        } else {
            CLIENT_LOG_DEBUG("Load success, key:" << key << ".");
        }
        if (callback != nullptr) {
            callback(context, ToCResult(result));
        }
    };
    BIO_TRACE_START(SDK_TRACE_LOAD);
    BResult ret = gClient->Load(key, offset, length, location, cb, context);
    BIO_TRACE_END(SDK_TRACE_LOAD, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Load failed, ret:" << ret << ", key:" << key << ", offset:" << offset << ", length:" <<
            length << ", location:" << location.location[0] << ".");
    }
    return ToCResult(ret);
}

CResult Bio::ListAll(const char *prefix, std::unordered_map<std::string, ObjStat> &objs)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(prefix == nullptr)) {
        CLIENT_LOG_ERROR("Invalid list parameter, prefix:" << prefix << ".");
        return RET_CACHE_EPERM;
    }

    BIO_TRACE_START(SDK_TRACE_LISTALL);
    BResult ret = gClient->ListAll(prefix, objs);
    BIO_TRACE_END(SDK_TRACE_LISTALL, ret);
    if (LIKELY(ret == BIO_OK)) {
        CLIENT_LOG_DEBUG("List all success, prefix:" << prefix << ", num:" << objs.size() << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Stat(const char *key, const ObjLocation &location, ObjStat &stat)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key))) {
        CLIENT_LOG_ERROR("Invalid Stat parameter, key:" << key << ".");
        return RET_CACHE_EPERM;
    }

    BIO_TRACE_START(SDK_TRACE_STAT);
    auto ret = gClient->Stat(key, location, stat);
    BIO_TRACE_END(SDK_TRACE_STAT, ret);
    return ToCResult(ret);
}

CResult Bio::NotifyUpdatePrepare()
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    bool flag = true;
    BIO_TRACE_START(SDK_TRACE_NOTIFYUPDATE);
    auto ret = gClient->NotifyUpdate(flag);
    BIO_TRACE_END(SDK_TRACE_NOTIFYUPDATE, ret);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Boostio notify upgrade prepare failed, ret:" << ret << ".");
    } else {
        CLIENT_LOG_DEBUG("Boostio notify upgrade prepare success.");
    }
    return ToCResult(ret);
}

CResult Bio::NotifyUpdateFinish()
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    bool flag = false;
    BIO_TRACE_START(SDK_TRACE_UPDATEFINISH);
    auto ret = gClient->NotifyUpdate(flag);
    BIO_TRACE_END(SDK_TRACE_UPDATEFINISH, ret);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Boostio notify upgrade finish failed, ret:" << ret << ".");
    } else {
        CLIENT_LOG_INFO("Boostio notify upgrade finish success.");
    }
    return ToCResult(ret);
}

CResult Bio::CheckUpdateReady()
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    BIO_TRACE_START(SDK_TRACE_CHECKREADY);
    auto ret = gClient->CheckUpdateReady();
    BIO_TRACE_END(SDK_TRACE_CHECKREADY, ret);
    if (ret != BIO_OK) {
        CLIENT_LOG_WARN("Boostio upgrade check not ready, ret:" << ret << ".");
    } else {
        CLIENT_LOG_DEBUG("Boostio upgrade check is ready.");
    }
    return ToCResult(ret);
}

CResult Bio::AllocSpace(uint64_t objectId, uint64_t length, CacheSpaceDesc &spaceInfo)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    ObjLocation location;
    if (spaceInfo.allocLoc != 0) {
        auto ret = gClient->CalculateLocation(objectId, LOCAL_AFFINITY, location);
        if (UNLIKELY(ret != BIO_OK)) {
            return RET_CACHE_NOT_READY;
        }
        spaceInfo.loc = location;
    } else {
        location = spaceInfo.loc;
    }

    MirrorClient::MirrorPut param = { { mTenantId, mAffinity, mStrategy }, nullptr, nullptr,
        length, location, 0 };
    BIO_TRACE_START(SDK_TRACE_ALLOC_SPACE);
    auto ret = gClient->AllocSpace(param, spaceInfo);
    BIO_TRACE_END(SDK_TRACE_ALLOC_SPACE, ret);
    return ToCResult(ret);
}

std::shared_ptr<Bio> BioService::CreateCache(const CacheDescriptor &desc)
{
    if (UNLIKELY(desc.affinity >= AFFINITY_BUTT || desc.strategy >= STRATEGY_BUTT ||
        desc.affinity < LOCAL_AFFINITY || desc.strategy < WRITE_BACK)) {
        CLIENT_LOG_ERROR("Invalid cache descriptor, tenantId:" << desc.tenantId << ", affinity:" << desc.affinity <<
            ", strategy:" << desc.strategy << ".");
        return nullptr;
    }

    BIO_TRACE_START(SDK_TRACE_CREATE_CACHE);
    auto cache = std::make_shared<Bio>(desc.tenantId, desc.affinity, desc.strategy);
    BResult ret = gClient->Insert(cache);
    BIO_TRACE_END(SDK_TRACE_CREATE_CACHE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Client insert cache instance failed, ret:" << ret << ".");
        return nullptr;
    }

    static std::string affinityStr[] = { "INVALID", "LOCAL_AFFINITY", "GLOBAL_BALANCE", "BUTT" };
    static std::string strategyStr[] = { "INVALID", "WRITE_BACK", "WRITE_THROUGH", "BUTT" };
    CLIENT_LOG_INFO("Create cache instance success, tenantId:" << desc.tenantId << ", affinity:" <<
        affinityStr[desc.affinity] << ", strategy:" << strategyStr[desc.strategy] << ".");
    return cache;
}

CacheDescriptor BioService::GetCache(uint64_t tenantId)
{
    return gClient->Query(tenantId);
}

std::vector<CacheDescriptor> BioService::ListCache()
{
    return gClient->List();
}

void BioService::DestroyCache(uint64_t tenantId)
{
    BIO_TRACE_START(SDK_TRACE_DESTROY_CACHE);
    gClient->Delete(tenantId);
    BIO_TRACE_END(SDK_TRACE_DESTROY_CACHE, BIO_OK);
    CLIENT_LOG_INFO("Destroy cache instance success, tenantId:" << tenantId << ".");
}

CResult BioService::Initialize(WorkerMode mode, const ClientOptionsConfig &optConf)
{
    return ToCResult(BioClient::Instance()->Start(mode, optConf));
}

void BioService::Exit()
{
    gClient->Exit();
}
}
}

/* ******************************** Boostio api implementation in C language ********************* */
using namespace ock::bio;

static std::unordered_map<uint64_t, std::shared_ptr<Bio>> gBioCacheMap;
static std::mutex g_lock;

CResult BioInitialize(WorkerMode mode, ClientOptionsConfig *optConf)
{
    if (optConf == nullptr) {
        auto clientConf = new (std::nothrow)ClientOptionsConfig();
        if (clientConf == nullptr) {
            return RET_CACHE_NO_SPACE;
        }
        clientConf->enable = false;
        auto ret = BioService::Initialize(mode, *clientConf);
        delete clientConf;
        return ret;
    }
    return BioService::Initialize(mode, *optConf);
}

void BioExit()
{
    BioService::Exit();
}

CResult BioCreateCache(CacheDescriptor desc)
{
    std::unique_lock<std::mutex> locker(g_lock);
    auto iter = gBioCacheMap.find(desc.tenantId);
    if (UNLIKELY(iter != gBioCacheMap.end())) {
        return RET_CACHE_EXISTS;
    }
    auto bioInstance = BioService::CreateCache(desc);
    if (UNLIKELY(bioInstance == nullptr)) {
        return RET_CACHE_EPERM;
    }
    gBioCacheMap.insert({ desc.tenantId, bioInstance });
    return RET_CACHE_OK;
}

CResult BioGetCache(uint64_t tenantId, CacheDescriptor *desc)
{
    if (UNLIKELY(desc == nullptr)) {
        return RET_CACHE_EPERM;
    }
    std::unique_lock<std::mutex> locker(g_lock);
    auto iter = gBioCacheMap.find(tenantId);
    if (UNLIKELY(iter == gBioCacheMap.end())) {
        return RET_CACHE_NOT_FOUND;
    }
    *desc = { iter->second->mTenantId, iter->second->mAffinity, iter->second->mStrategy };
    return RET_CACHE_OK;
}

CResult BioDestroyCache(uint64_t tenantId)
{
    std::unique_lock<std::mutex> locker(g_lock);
    auto iter = gBioCacheMap.find(tenantId);
    if (UNLIKELY(iter == gBioCacheMap.end())) {
        return RET_CACHE_NOT_FOUND;
    }
    BioService::DestroyCache(tenantId);
    gBioCacheMap.erase(iter);
    return RET_CACHE_OK;
}

CResult BioCalcLocation(uint64_t tenantId, uint64_t objectId, ObjLocation *location)
{
    if (UNLIKELY(location == nullptr)) {
        return RET_CACHE_EPERM;
    }
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    ObjLocation outLocation = { 0 };
    auto ret = bioInstance->CalculateLocation(objectId, outLocation);
    location->location[0] = outLocation.location[0];
    location->location[1] = outLocation.location[1];
    return ret;
}

CResult BioAllocCacheSpace(uint64_t tenantId, uint64_t objectId, uint64_t length, CacheSpaceDesc *space)
{
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }

    return bioInstance->AllocSpace(objectId, length, *space);
}

CResult BioPutWithCopyFree(uint64_t tenantId, const char *key, CacheSpaceDesc *space)
{
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    return bioInstance->Put(key, *space);
}

CResult BioPut(uint64_t tenantId, const char *key, const char *value, uint64_t length, ObjLocation location)
{
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    return bioInstance->Put(key, value, length, location);
}

CResult BioGet(uint64_t tenantId, const char *key, uint64_t offset, uint64_t length, ObjLocation location, char *value,
    uint64_t *realLength)
{
    if (UNLIKELY(realLength == nullptr)) {
        return RET_CACHE_EPERM;
    }
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    uint64_t outLen = 0;
    auto ret = bioInstance->Get(key, offset, length, location, value, outLen);
    *realLength = outLen;
    return ret;
}

CResult BioDelete(uint64_t tenantId, const char *key, ObjLocation location)
{
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    return bioInstance->Delete(key, location);
}

CResult BioLoad(uint64_t tenantId, const char *key, uint64_t offset, uint64_t length, ObjLocation location,
    BioLoadCallback callback, void *context)
{
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    return bioInstance->Load(key, offset, length, location, callback, context);
}

CResult BioListAll(uint64_t tenantId, const char *prefix, ObjStat **objs, uint64_t *objNum)
{
    if (UNLIKELY(objNum == nullptr)) {
        return RET_CACHE_EPERM;
    }
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    std::unordered_map<std::string, ObjStat> listObjs;
    auto ret = bioInstance->ListAll(prefix, listObjs);
    if (UNLIKELY(ret != RET_CACHE_OK)) {
        return ret;
    }

    *objNum = listObjs.size();
    *objs = (ObjStat *)malloc(sizeof(ObjStat) * (*objNum));
    if (*objs == nullptr) {
        return RET_CACHE_NO_SPACE;
    }
    uint32_t index = 0;
    for (auto &obj : listObjs) {
        CopyKey((*objs)[index].key, obj.second.key, MAX_KEY_SIZE);
        (*objs)[index].size = obj.second.size;
        (*objs)[index].time = obj.second.time;
        index++;
    }
    return RET_CACHE_OK;
}

void BioFreeListResources(ObjStat *objs, uint64_t objNum)
{
    if (objNum == 0) {
        return;
    }
    free(objs);
    objs = nullptr;
}

CResult BioStat(uint64_t tenantId, const char *key, ObjLocation location, ObjStat *stat)
{
    if (UNLIKELY(stat == nullptr)) {
        return RET_CACHE_EPERM;
    }
    if (UNLIKELY(key == nullptr || stat == nullptr)) {
        return RET_CACHE_ERROR;
    }
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }

    ObjStat statInfo;
    auto ret = bioInstance->Stat(key, location, statInfo);
    if (LIKELY(ret == RET_CACHE_OK)) {
        *stat = statInfo;
    }
    return ret;
}


CResult BioNotifyUpgradePrepare(uint64_t tenantId)
{
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    return bioInstance->NotifyUpdatePrepare();
}

CResult BioNotifyUpgradeFinish(uint64_t tenantId)
{
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    return bioInstance->NotifyUpdateFinish();
}

CResult BioCheckUpgradeReady(uint64_t tenantId)
{
    std::shared_ptr<Bio> bioInstance = nullptr;
    {
        std::unique_lock<std::mutex> locker(g_lock);
        auto iter = gBioCacheMap.find(tenantId);
        if (UNLIKELY(iter == gBioCacheMap.end())) {
            return RET_CACHE_NOT_FOUND;
        }
        bioInstance = iter->second;
    }
    return bioInstance->CheckUpdateReady();
}

ReadHook g_readHook = nullptr;
WriteHook g_writeHook = nullptr;
WriteCopyFreeHook g_writeCopyFreeHook = nullptr;

int BioReadHook(uint64_t inode, char *buff, uint64_t count, uint64_t offset, int *readLen)
{
    return g_readHook(inode, buff, count, offset, readLen);
}

int BioWriteHook(uint64_t inode, char *buff, uint64_t count, uint64_t offset, uint64_t fh)
{
    return g_writeHook(inode, buff, count, offset, fh);
}

int BioWriteCopyFreeHook(uint64_t inode, uint64_t offset, uint64_t count, CacheSpaceDesc *space)
{
    return g_writeCopyFreeHook(inode, offset, count, space);
}

void BioRegisterInterceptorRead(ReadHook rh)
{
    g_readHook = rh;
}

void BioRegisterInterceptorWrite(WriteHook wh)
{
    g_writeHook = wh;
}

void BioRegisterInterceptorWriteCopyFree(WriteCopyFreeHook wh)
{
    g_writeCopyFreeHook = wh;
}

CResult BioConvertLocation(ObjLocation location, ObjLocationDetail *detailLoc)
{
    if (UNLIKELY(!gClient->Ready())) {
        return RET_CACHE_NOT_READY;
    }

    auto ptId = static_cast<uint16_t>(location.location[0]);
    ock::bio::CmPtInfo info;
    BResult ret = gClient->GetMirror()->GetPtEntry(ptId, info);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to get pt entry, ptId:" << ptId << ".");
        return RET_CACHE_EPERM;
    }

    FileLocationQueryRsp rsp;
    uint16_t slaveId = 0;
    uint32_t idx = 0;
    for (; idx < info.copys.size(); ++idx) {
        if (info.copys[idx].nodeId != info.masterNodeId) {
            slaveId = info.copys[idx].nodeId;
            break;
        }
    }

    if (idx == info.copys.size()) {
        CLIENT_LOG_ERROR("Failed to get slave node, ptId:" << ptId << ".");
        return RET_CACHE_ERROR;
    }

    ret = gClient->GetMirror()->GetFileLocation(info.masterNodeId, slaveId, rsp);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed get file location, ret:" << ret << ", slaveId:"<< slaveId << ".");
        return RET_CACHE_ERROR;
    }

    memcpy_s(detailLoc->hostMaster, NODE_DESC_SIZE, rsp.hostMaster, NODE_DESC_SIZE);
    memcpy_s(detailLoc->hostSlave, NODE_DESC_SIZE, rsp.hostSlave, NODE_DESC_SIZE);
    detailLoc->portMaster = rsp.portMaster;
    detailLoc->portSlave = rsp.portSlave;
    return RET_CACHE_OK;
}