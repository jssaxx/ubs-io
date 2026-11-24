/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include "bio_c.h"
#include "bio_err.h"
#include "stub_control.h"

namespace {
struct BioStubState {
    std::mutex mutex;
    std::unordered_map<std::string, int> results;
    std::unordered_map<std::string, int> calls;
    uint32_t statSize{64};
    int batchItemResult{RET_CACHE_OK};
    uint16_t resourceDiskCount{2};
    uint32_t scanCount{2};
    BioLogLevel logLevel{BIO_LOG_LEVEL_INFO};
    BioUnderFsType underFsType{BIO_UNDERFS_NONE};
    WorkerMode mode{STANDALONE};
    bool initialized{false};
};

BioStubState &State()
{
    static BioStubState state;
    return state;
}

int ResultLocked(BioStubState &state, const char *name)
{
    ++state.calls[name];
    auto found = state.results.find(name);
    return found == state.results.end() ? RET_CACHE_OK : found->second;
}
}

extern "C" void FakeBioReset()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.results.clear();
    state.calls.clear();
    state.statSize = 64;
    state.batchItemResult = RET_CACHE_OK;
    state.resourceDiskCount = 2;
    state.scanCount = 2;
    state.logLevel = BIO_LOG_LEVEL_INFO;
    state.underFsType = BIO_UNDERFS_NONE;
    state.mode = STANDALONE;
    state.initialized = false;
}

extern "C" void FakeBioSetResult(const char *name, int result)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.results[name] = result;
}

extern "C" int FakeBioGetCallCount(const char *name)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.calls[name];
}

extern "C" void FakeBioSetStatSize(uint32_t size)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.statSize = size;
}

extern "C" void FakeBioSetBatchItemResult(int result)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.batchItemResult = result;
}

extern "C" void FakeBioSetResourceDiskCount(uint16_t count)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.resourceDiskCount = count;
}

extern "C" void FakeBioSetScanCount(uint32_t count)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.scanCount = count;
}

extern "C" void FakeBioSetLogLevel(int32_t level)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.logLevel = static_cast<BioLogLevel>(level);
}

extern "C" CResult BioInitialize(WorkerMode mode, ClientOptionsConfig *)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto ret = static_cast<CResult>(ResultLocked(state, "BioInitialize"));
    state.initialized = ret == RET_CACHE_OK;
    state.mode = mode;
    return ret;
}

#ifndef UBSIO_KV_UT_OMIT_CONFIG_INFO
extern "C" CResult BioGetConfigInfo(BioConfigInfo *info)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto ret = static_cast<CResult>(ResultLocked(state, "BioGetConfigInfo"));
    if (ret != RET_CACHE_OK || !state.initialized) {
        return ret != RET_CACHE_OK ? ret : RET_CACHE_NOT_READY;
    }
    if (info == nullptr || info->structSize < BioConfigInfoMinSize(info->fields)) {
        return RET_CACHE_EPERM;
    }
    BioConfigInfo result{};
    result.structSize = std::min<uint32_t>(info->structSize, sizeof(result));
    result.fields = info->fields;
    if ((result.fields & BIO_CONFIG_NDS_ENABLED) != 0) {
        result.ndsEnabled = state.mode == STANDALONE && state.underFsType == BIO_UNDERFS_LOCAL &&
            ResultLocked(state, "ubsio.nds.enabled") != 0;
        if (result.ndsEnabled) {
            result.localFileSlot = static_cast<uint8_t>(ResultLocked(state, "localFileSlot"));
        }
    }
    if ((result.fields & BIO_CONFIG_UNDERFS_TYPE) != 0) {
        result.underFsType = state.underFsType;
    }
    std::memcpy(info, &result, result.structSize);
    return RET_CACHE_OK;
}
#endif

extern "C" int32_t UfsResolveFilePath(const char *key, uint32_t slot, char *path, size_t capacity)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (slot != static_cast<uint32_t>(ResultLocked(state, "UfsResolveFilePath"))) {
        return ock::bio::BIO_INVALID_PARAM;
    }
    const int size = std::snprintf(path, capacity, "%s", key);
    return size >= 0 && static_cast<size_t>(size) < capacity ? ock::bio::BIO_OK : ock::bio::BIO_INVALID_PARAM;
}

extern "C" CResult BioGetLogLevel(BioLogLevel *level)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "BioGetLogLevel");
    if (result == RET_CACHE_OK && level != nullptr) {
        *level = state.logLevel;
    }
    return static_cast<CResult>(result);
}

extern "C" void FakeBioSetUnderFsType(BioUnderFsType type)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.underFsType = type;
}

extern "C" CResult BioCreateCache(CacheDescriptor)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return static_cast<CResult>(ResultLocked(state, "BioCreateCache"));
}

extern "C" CResult BioCalcLocation(uint64_t, uint64_t objectId, ObjLocation *location)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "BioCalcLocation");
    if (result == RET_CACHE_OK && location != nullptr) {
        location->location[0] = objectId;
        location->location[1] = objectId ^ 0x5a5a5a5aUL;
    }
    return static_cast<CResult>(result);
}

extern "C" CResult BioGet(uint64_t, const char *, uint64_t, uint64_t length, ObjLocation,
                           char *value, uint64_t *realLength)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "BioGet");
    if (result == RET_CACHE_OK) {
        if (value != nullptr) {
            std::memset(value, 'G', static_cast<size_t>(length));
        }
        if (realLength != nullptr) {
            *realLength = length;
        }
    }
    return static_cast<CResult>(result);
}

extern "C" CResult BioPut(uint64_t, const char *, const char *, uint64_t, ObjLocation)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return static_cast<CResult>(ResultLocked(state, "BioPut"));
}

extern "C" CResult BioStat(uint64_t, const char *, ObjLocation, ObjStat *stat)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "BioStat");
    if (stat != nullptr) {
        stat->size = state.statSize;
    }
    return static_cast<CResult>(result);
}

extern "C" CResult BioBatchStat(uint64_t, const char **keys, ObjLocation *, uint32_t count, BatchObjStat *stats)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "BioBatchStat");
    if (result != RET_CACHE_OK) {
        return static_cast<CResult>(result);
    }
    for (uint32_t index = 0; index < count; ++index) {
        std::snprintf(stats[index].key, sizeof(stats[index].key), "%s", keys[index]);
        stats[index].size = state.statSize;
        stats[index].result = state.batchItemResult;
    }
    return RET_CACHE_OK;
}

extern "C" CResult BioBatchGet(uint64_t, const char **, const uint32_t count, uint64_t *,
                                uint64_t *lengths, ObjLocation *, uintptr_t *valueAddrs,
                                uint64_t *realLengths, int32_t *results)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "BioBatchGet");
    if (result != RET_CACHE_OK) {
        return static_cast<CResult>(result);
    }
    for (uint32_t index = 0; index < count; ++index) {
        results[index] = state.batchItemResult;
        realLengths[index] = lengths[index];
        if (state.batchItemResult == RET_CACHE_OK) {
            void *buffer = std::malloc(static_cast<size_t>(lengths[index]));
            if (buffer == nullptr) {
                results[index] = RET_CACHE_NO_SPACE;
                valueAddrs[index] = 0;
                continue;
            }
            std::memset(buffer, 'B', static_cast<size_t>(lengths[index]));
            valueAddrs[index] = reinterpret_cast<uintptr_t>(buffer);
        } else {
            valueAddrs[index] = 0;
        }
    }
    return RET_CACHE_OK;
}

extern "C" CResult BioBatchExist(uint64_t, const char *[], ObjLocation [], uint32_t count, bool result[])
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto ret = ResultLocked(state, "BioBatchExist");
    for (uint32_t index = 0; index < count; ++index) {
        result[index] = (index % 2U) == 0;
    }
    return static_cast<CResult>(ret);
}

extern "C" CResult BioBatchGetFree(uint64_t, uintptr_t *valueAddrs, const uint32_t count)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "BioBatchGetFree");
    if (result == RET_CACHE_OK && valueAddrs != nullptr) {
        for (uint32_t index = 0; index < count; ++index) {
            std::free(reinterpret_cast<void *>(valueAddrs[index]));
            valueAddrs[index] = 0;
        }
    }
    return static_cast<CResult>(result);
}

extern "C" CResult BioDelete(uint64_t, const char *, ObjLocation)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return static_cast<CResult>(ResultLocked(state, "BioDelete"));
}

extern "C" CResult BioRegisterMetaEventCallback(UbsioMetaEventCallbackC callback, void *context)
{
    auto &state = State();
    int result;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        result = ResultLocked(state, "BioRegisterMetaEventCallback");
    }
    if (result == RET_CACHE_OK && callback != nullptr) {
        const char key[] = "stub-key";
        UbsioMetaEventC event{UBSIO_META_RECOVER_C, key, static_cast<uint32_t>(sizeof(key) - 1)};
        callback(context, &event, 1);
    }
    return static_cast<CResult>(result);
}

extern "C" CResult BioShowLocalCacheResource(CacheResourcesDesc *resource)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "BioShowLocalCacheResource");
    if (result == RET_CACHE_OK && resource != nullptr) {
        std::memset(resource, 0, sizeof(*resource));
        resource->wCacheDiskCapacity = 1000;
        resource->wCacheDiskUsedSize = 400;
        resource->wCacheMemCapacity = 2000;
        resource->wCacheMemUsedSize = 500;
        resource->diskNum = state.resourceDiskCount;
        auto fillCount = std::min<uint16_t>(state.resourceDiskCount, DISK_RESOURCE_MAX_NUM);
        for (uint16_t index = 0; index < fillCount; ++index) {
            auto &disk = resource->disks[index];
            disk.status = index % 2U;
            disk.readBandwidth = 100 + index;
            disk.writeBandwidth = 200 + index;
            disk.totalBandwidth = 300 + index;
            disk.bandwidthValid = 1;
            std::snprintf(disk.path, sizeof(disk.path), "/fake/disk%u", index);
        }
    }
    return static_cast<CResult>(result);
}

extern "C" CResult BioScanKey(uint64_t, const UbsioKvKeyInfo **items, uint64_t *count, bool *hasMore)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    *hasMore = false;
    auto result = ResultLocked(state, "BioScanKey");
    if (result != RET_CACHE_OK) {
        return static_cast<CResult>(result);
    }
    *count = state.scanCount;
    if (state.scanCount == 0) {
        *items = nullptr;
        return RET_CACHE_OK;
    }
    auto *output = static_cast<UbsioKvKeyInfo *>(std::calloc(state.scanCount, sizeof(UbsioKvKeyInfo)));
    if (output == nullptr) {
        return RET_CACHE_NO_SPACE;
    }
    for (uint32_t index = 0; index < state.scanCount; ++index) {
        std::snprintf(output[index].key, sizeof(output[index].key), "key-%u", index);
        output[index].keyLen = static_cast<uint32_t>(std::strlen(output[index].key));
        output[index].valueLen = 1000 + index;
    }
    *items = output;
    return RET_CACHE_OK;
}

extern "C" void BioFreeScanKeyResult(const UbsioKvKeyInfo **items)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    ++state.calls["BioFreeScanKeyResult"];
    if (items != nullptr) {
        std::free(const_cast<UbsioKvKeyInfo *>(*items));
        *items = nullptr;
    }
}

extern "C" void BioExit()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    ++state.calls["BioExit"];
}
