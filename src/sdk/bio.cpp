/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include <iostream>
#include <cstring>
#include "securec.h"
#include "bio_log.h"
#include "bio_trace.h"
#include "bio_config_instance.h"
#include "bio_server.h"
#include "bio_client.h"
#include "bio.h"

namespace ock {
namespace bio {
BioClientPtr gClient = BioClient::Instance();

constexpr size_t IO_SIZE_4K =  4 * 1024;
constexpr size_t IO_SIZE_8K = 8 * 1024;
constexpr size_t IO_SIZE_64K = 64 * 1024;
constexpr size_t IO_SIZE_128K = 128 * 1024;
constexpr size_t IO_SIZE_256K = 256 * 1024;
constexpr size_t IO_SIZE_1M = 1024 * 1024;
constexpr size_t IO_SIZE_2M = 2 * 1024 * 1024;
constexpr size_t IO_SIZE_4M = 4 * 1024 * 1024;

static CResult ToCResult(const BResult ret)
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
        default:
            return RET_CACHE_NEED_RETRY;
    }
}

static void StatisticIoSize(uint64_t length, uint8_t opType)
{
    SdkTracerType type = SDK_TRACE_START;
    if (length <= IO_SIZE_4K) {
        type = opType == 0 ? SDK_TRACE_W_S_1_4K : SDK_TRACE_R_S_1_4K;
    } else if (length <= IO_SIZE_8K) {
        type = opType == 0 ? SDK_TRACE_W_S_4_8K : SDK_TRACE_R_S_4_8K;
    } else if (length <= IO_SIZE_64K) {
        type = opType == 0 ? SDK_TRACE_W_S_8_64K : SDK_TRACE_R_S_8_64K;
    } else if (length <= IO_SIZE_128K) {
        type = opType == 0 ? SDK_TRACE_W_S_64_128K : SDK_TRACE_R_S_64_128K;
    } else if (length <= IO_SIZE_256K) {
        type = opType == 0 ? SDK_TRACE_W_S_128_256K : SDK_TRACE_R_S_128_256K;
    } else if (length <= IO_SIZE_1M) {
        type = opType == 0 ? SDK_TRACE_W_S_256K_1M : SDK_TRACE_R_S_256K_1M;
    } else if (length <= IO_SIZE_2M) {
        type = opType == 0 ? SDK_TRACE_W_S_1_2M : SDK_TRACE_R_S_1_2M;
    } else if (length <= IO_SIZE_4M) {
        type = opType == 0 ? SDK_TRACE_W_S_2_4M : SDK_TRACE_R_S_2_4M;
    } else {
        type = opType == 0 ? SDK_TRACE_W_S_4M : SDK_TRACE_R_S_4M;
    }
    BIO_TRACE_START(type);
    BIO_TRACE_END(type, BIO_OK);
}

static bool KeyValid(const char* key)
{
    if (key == nullptr || strlen(key) >= KEY_MAX_SIZE) {
        return false;
    }
    return true;
}

CResult Bio::CalculateLocation(uint64_t objectId, ObjLocation &location)
{
    if (UNLIKELY(!gClient->Ready())) {
        LOG_WARN("Boostio cache service not ready, please try again.");
        return RET_CACHE_NOT_READY;
    }
    return ToCResult(gClient->CalculateLocation(objectId, mAffinity, location));
}

CResult Bio::Put(const char *key, const char *value, uint64_t length, const ObjLocation &location)
{
    if (UNLIKELY(!gClient->Ready())) {
        LOG_WARN("Boostio cache service not ready, please try again.");
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key) || value == nullptr || length == 0 || length > NO_4194304)) {
        LOG_ERROR("Invalid put parameter, key or value pointers is nullptr, length:" << length << ", max length:" <<
            (NO_4194304/NO_1024/NO_1024) << "(Mb).");
        return RET_CACHE_EPERM;
    }

    StatisticIoSize(length, 0);

    BIO_TRACE_START(SDK_TRACE_PUT);
    MirrorClient::MirrorPut param = { { mTenantId, mAffinity, mStrategy }, key, value, length, location };
    BResult ret = gClient->Put(param);
    BIO_TRACE_END(SDK_TRACE_PUT, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Put value failed, ret:" << ret << ", key:" << key << ", length:" << length << ", location0:" <<
            location.location[0] << ", location1:" << location.location[1] << ", value:" << value << ".");
    } else {
        LOG_INFO("Put value success, key:" << key << ", length:" << length << ", location0:" << location.location[0] <<
            ", location1:" << location.location[1] << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Get(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location, char *value)
{
    if (UNLIKELY(!gClient->Ready())) {
        LOG_WARN("Boostio cache service not ready, please try again.");
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key) || value == nullptr || length == 0 || (offset + length) > NO_4194304)) {
        LOG_ERROR("Invalid get parameter, key or value pointers is nullptr, offset:" << offset << "length:" <<
            length << ", max length:" << (NO_4194304/NO_1024/NO_1024) << "(Mb).");
        return RET_CACHE_EPERM;
    }

    StatisticIoSize(length, 1);

    BIO_TRACE_START(SDK_TRACE_GET);
    MirrorClient::MirrorGet param{ { mTenantId, mAffinity, mStrategy }, key, value, offset, length, location };
    uint64_t realLen = 0;
    BResult ret = gClient->Get(param, realLen);
    BIO_TRACE_END(SDK_TRACE_GET, ret);
    if (UNLIKELY(ret != BIO_OK || realLen != length)) {
        ret = BIO_READ_EXCEED;
        LOG_ERROR("Get value failed, ret:" << ret << ", key:" << key << ", offset:" << offset << ", length:" <<
            length << ", realLen:" << realLen << ", location0:" << location.location[0] <<
            ", location1:" << location.location[1] << ".");
    } else {
        LOG_INFO("Get value success, key:" << key << ", offset:" << offset << ", length:" << length << ", location0:" <<
            location.location[0] << ", location1:" << location.location[1] << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Get(const char *key, const ObjLocation &location, char *value, uint64_t &length)
{
    if (UNLIKELY(!gClient->Ready())) {
        LOG_WARN("Boostio cache service not ready, please try again.");
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key) || value == nullptr || length == 0 || length > NO_4194304)) {
        LOG_ERROR("Invalid get parameter, key or value pointers is nullptr, " << "length:" <<
            length << ", max length:" << (NO_4194304/NO_1024/NO_1024) << "(Mb).");
        return RET_CACHE_EPERM;
    }

    StatisticIoSize(length, 1);

    BIO_TRACE_START(SDK_TRACE_GET);
    MirrorClient::MirrorGet param{ { mTenantId, mAffinity, mStrategy }, key, value, 0, length, location };
    BResult ret = gClient->Get(param, length);
    BIO_TRACE_END(SDK_TRACE_GET, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Get value failed, ret:" << ret << ", key:" << key << ", length:" <<
            length << ", location0:" << location.location[0] << ", location1:" << location.location[1] << ".");
    } else {
        LOG_INFO("Get value success, key:" << key << ", length:" << length << ", location0:" <<
            location.location[0] << ", location1:" << location.location[1] << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Delete(const char *key, const ObjLocation &location)
{
    if (UNLIKELY(!gClient->Ready())) {
        LOG_WARN("Boostio cache service not ready, please try again.");
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key))) {
        LOG_ERROR("Invalid delete parameter, key:" << key << ".");
        return RET_CACHE_EPERM;
    }

    BIO_TRACE_START(SDK_TRACE_DELETE);
    BResult ret = gClient->DeleteKey(key, location);
    BIO_TRACE_END(SDK_TRACE_DELETE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Delete key failed, ret:" << ret << ", key:" << key << ".");
    } else {
        LOG_INFO("Delete key success, key:" << key << ".");
    }
    return ToCResult(ret);
}

CResult Bio::Load(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location, const LoadCallback& callback, void *context)
{
    if (UNLIKELY(!gClient->Ready())) {
        LOG_WARN("Boostio cache service not ready, please try again.");
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(!KeyValid(key) || context == nullptr)) {
        LOG_ERROR("Invalid load parameter, key:" << key << ".");
        return RET_CACHE_EPERM;
    }

    BIO_TRACE_START(SDK_TRACE_LOAD);
    BResult ret = gClient->Load(key, offset, length, location, callback, context);
    BIO_TRACE_END(SDK_TRACE_LOAD, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Load value failed, ret:" << ret << ", key:" << key << ", location0:" << location.location[0] <<
            ", location1:" << location.location[1] << ".");
    } else {
        LOG_INFO("Load value success, key:" << key << ", location0:" << location.location[0] << ", location1:" <<
            location.location[1] << ".");
    }
    return ToCResult(ret);
}

CResult Bio::ListAll(const char *prefix, const std::vector<std::pair<char *, ObjStat>>& objs)
{
    if (UNLIKELY(!gClient->Ready())) {
        LOG_WARN("Boostio cache service not ready, please try again.");
        return RET_CACHE_NOT_READY;
    }

    if (UNLIKELY(prefix == nullptr)) {
        LOG_ERROR("Invalid ListAll parameter, prefix:" << prefix << ".");
        return RET_CACHE_EPERM;
    }

    BIO_TRACE_START(SDK_TRACE_LISTALL);
    BResult ret = gClient->ListAll(prefix, objs);
    BIO_TRACE_END(SDK_TRACE_LISTALL, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("List object failed, ret:" << ret << ", prefix:" << prefix << ".");
    } else {
        LOG_INFO("List object success, prefix:" << prefix << ", num:" << objs.size() << ".");
    }
    return ToCResult(ret);
}

Bio::ObjStat Bio::Stat(const char *key, const ObjLocation &location)
{
    if (UNLIKELY(!gClient->Ready())) {
        LOG_WARN("Boostio cache service not ready, please try again.");
        return { 0, 0 };
    }

    if (UNLIKELY(!KeyValid(key))) {
        LOG_ERROR("Invalid Stat parameter, key:" << key << ".");
        return { 0, 0 };
    }

    BIO_TRACE_START(SDK_TRACE_STAT);
    Bio::ObjStat stat = gClient->Stat(key, location);
    BResult ret = (stat.size == 0) ? BIO_ERR : BIO_OK;
    BIO_TRACE_END(SDK_TRACE_STAT, ret);
    return stat;
}

std::shared_ptr<Bio> BioService::CreateCache(const BioService::Descriptor &desc)
{
    if (UNLIKELY(desc.tenantId == 0 || desc.affinity >= AFFINITY_BUTT || desc.strategy >= STRATEGY_BUTT || desc.capacity == 0)) {
        LOG_ERROR("Invalid cache descriptor, tenantId:" << desc.tenantId << ", affinity:" << desc.affinity <<
            ", strategy:" << desc.strategy << ", capacity:" << desc.capacity << ".");
        return nullptr;
    }

    BIO_TRACE_START(SDK_TRACE_CREATE_CACHE);
    auto cache = std::make_shared<Bio>(desc.tenantId, desc.affinity, desc.strategy, desc.capacity);
    BResult ret = gClient->Insert(cache);
    BIO_TRACE_END(SDK_TRACE_CREATE_CACHE, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Client insert cache instance failed, ret:" << ret << ".");
        return nullptr;
    }

    static std::string affinityStr[] = { "INVALID", "LOCAL_AFFINITY", "GLOBAL_BALANCE", "BUTT" };
    static std::string strategyStr[] = { "INVALID", "WRITE_BACK", "WRITE_THROUGH", "BUTT" };
    LOG_INFO("Create cache instance success, tenantId:" << desc.tenantId << ", affinity:" <<
        affinityStr[desc.affinity] << ", strategy:" << strategyStr[desc.strategy] << ", capacity:" <<
        desc.capacity << ".");
    return cache;
}

std::shared_ptr<Bio> BioService::GetCache(uint64_t tenantId)
{
    return gClient->Query(tenantId);
}

std::unordered_map<uint64_t, std::shared_ptr<Bio>> BioService::ListCache()
{
    return gClient->List();
}

void BioService::DestroyCache(uint64_t tenantId)
{
    BIO_TRACE_START(SDK_TRACE_DESTROY_CACHE);
    gClient->Delete(tenantId);
    BIO_TRACE_END(SDK_TRACE_DESTROY_CACHE, BIO_OK);
    LOG_INFO("Destroy cache instance success, tenantId:" << tenantId << ".");
}

CResult BioService::Initialize()
{
    CResult result = RET_CACHE_OK;
    do {
        auto config = BioConfig::Instance();
        if (UNLIKELY(config == nullptr)) {
            result = RET_CACHE_NOT_READY;
            break;
        }

        if (config->GetCmConfig().deployType == 1) {
            auto ret = BioServer::Instance()->Start();
            if (UNLIKELY(ret != BIO_OK)) {
                result = RET_CACHE_ERROR;
                break;
            }
        }

        auto ret = BioClient::Instance()->Start();
        if (UNLIKELY(ret != BIO_OK)) {
            result = RET_CACHE_ERROR;
            break;
        }
    } while (false);

    if (UNLIKELY(result != RET_CACHE_OK)) {
        std::cout << "BoostIO service initialize failed, result" << result << std::endl;
    }
    return result;
}

void BioService::Exit()
{
    auto config = BioConfig::Instance();
    if (UNLIKELY(config == nullptr)) {
        return;
    }

    if (config->GetCmConfig().deployType == 1) {
        BioServer::Instance()->Stop();
    }

    gClient->Stop();
    std::cout << "BoostIO service exit." << std::endl;
}
}
}