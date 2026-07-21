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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sys/resource.h>
#include <semaphore.h>
#include <thread>
#include <vector>
#include "htracer.h"
#include "bio_client.h"
#include "bio_config_instance.h"
#include "bio_lock.h"
#include "bio_crc_util.h"
#include "sdk_diagnose.h"
#include "securec.h"

#ifdef __cplusplus
extern "C" {
#endif

int SdkDiagnoseInit()
{
    return ock::bio::diagnose::BioSdkCommand::Initialize();
}

#ifdef __cplusplus
}
#endif

using namespace ock::bio;
typedef void *(*perfTestRunner)(void *param);
uint64_t gTenantId = UINT64_MAX;

namespace {
bool IsUnsignedInteger(const std::string &value)
{
    if (value.empty()) {
        return false;
    }
    for (char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}
}

struct PerfTestParam {
    bool done;
    uint32_t tid;
    int32_t result;
    sem_t sem;
    uint32_t length;
    uint32_t count;
};
static std::unordered_map<std::string, ObjLocation> gLocation;
static ReadWriteLock gLocationLock;
static std::atomic<uint64_t> gBatchGetRunId{ 0 };

struct BatchGetAlignedDeleter {
    void operator()(char *ptr) const
    {
        free(ptr);
    }
};
using BatchGetAlignedBuffer = std::unique_ptr<char, BatchGetAlignedDeleter>;
constexpr size_t BATCH_GET_DIRECT_IO_ALIGN = 512;

static BatchGetAlignedBuffer AllocBatchGetAlignedBuffer(size_t size)
{
    void *ptr = nullptr;
    if (posix_memalign(&ptr, BATCH_GET_DIRECT_IO_ALIGN, size) != 0) {
        return BatchGetAlignedBuffer(nullptr);
    }
    return BatchGetAlignedBuffer(static_cast<char *>(ptr));
}

static void FillBatchGetValue(char *value, uint32_t length, uint64_t runId, uint32_t index)
{
    for (uint32_t offset = 0; offset < length; ++offset) {
        value[offset] = static_cast<char>((runId + index + offset) & 0xff);
    }
}

static CResult PutBatchGetPerfValue(const char *key, const char *value, uint32_t length,
                                    const ObjLocation &location)
{
    constexpr uint32_t maxRetries = 1000;
    CResult ret = RET_CACHE_ERROR;
    for (uint32_t retry = 0; retry < maxRetries; ++retry) {
        ret = BioPut(gTenantId, key, value, length, location);
        if (ret == RET_CACHE_OK || (ret != RET_CACHE_BUSY && ret != RET_CACHE_NEED_RETRY)) {
            return ret;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return ret;
}

static CResult BioBatchExistChunked(uint64_t tenantId, const char *keys[], ObjLocation locations[], uint32_t count,
                                    bool results[])
{
    uint32_t done = 0;
    while (done < count) {
        uint32_t current = std::min(KEY_MAX_COUNT, count - done);
        CResult ret = BioBatchExist(tenantId, keys + done, locations + done, current, results + done);
        if (ret != RET_CACHE_OK) {
            return ret;
        }
        done += current;
    }
    return RET_CACHE_OK;
}

struct BatchGetMixKey {
    std::string key;
    ObjLocation location{};
    uint32_t crc = 0;
    bool expectCache = false;
};

struct BatchGetMixThreadResult {
    CResult ret = RET_CACHE_OK;
    double totalGetUs = 0;
    double totalFreeUs = 0;
    uint32_t batches = 0;
    uint64_t failedItems = 0;
    int32_t firstItemRet = RET_CACHE_OK;
    uint64_t shortReads = 0;
};

struct BatchGetMixHitSnapshot {
    bool valid = false;
    uint64_t rCacheHitMemCount = 0;
    uint64_t rCacheHitDiskCount = 0;
    uint64_t rCacheHitCount = 0;
    uint64_t rCacheTotalCount = 0;
    uint64_t wCacheHitMemCount = 0;
    uint64_t wCacheHitDiskCount = 0;
    uint64_t wCacheHitCount = 0;
    uint64_t wCacheTotalCount = 0;
    uint64_t backendHitCount = 0;
};

struct BatchGetMixRunStats {
    uint64_t failedItems = 0;
    int32_t firstItemRet = RET_CACHE_OK;
    uint64_t shortReads = 0;
};

class BatchGetMixWaterLevelGuard {
public:
    explicit BatchGetMixWaterLevelGuard(uint64_t level)
    {
        mOldLevel = BioConfig::Instance()->ModifyConfigEvictWaterLevel(0, level);
        mActive = true;
    }

    ~BatchGetMixWaterLevelGuard()
    {
        if (mActive) {
            (void)BioConfig::Instance()->ModifyConfigEvictWaterLevel(0, mOldLevel);
        }
    }

    uint64_t OldLevel() const
    {
        return mOldLevel;
    }

private:
    uint64_t mOldLevel = 0;
    bool mActive = false;
};

static BatchGetMixHitSnapshot TakeBatchGetMixHitSnapshot()
{
    BatchGetMixHitSnapshot snapshot;
    CacheHitFinalDesc desc;
    CacheHitFinalDesc *nodeDesc = nullptr;
    uint64_t nodeNum = 0;
    auto ret = BioShowCacheHitRatio(&desc, &nodeDesc, &nodeNum);
    if (ret != RET_CACHE_OK) {
        BioFreeCacheHitPtr(&nodeDesc, nodeNum);
        return snapshot;
    }

    snapshot.valid = true;
    snapshot.rCacheHitMemCount = desc.rCacheHitMemCount;
    snapshot.rCacheHitDiskCount = desc.rCacheHitDiskCount;
    snapshot.rCacheHitCount = desc.rCacheHitCount;
    snapshot.rCacheTotalCount = desc.rCacheTotalCount;
    snapshot.wCacheHitMemCount = desc.wCacheHitMemCount;
    snapshot.wCacheHitDiskCount = desc.wCacheHitDiskCount;
    snapshot.wCacheHitCount = desc.wCacheHitCount;
    snapshot.wCacheTotalCount = desc.wCacheTotalCount;
    snapshot.backendHitCount = desc.backendHitCount;
    BioFreeCacheHitPtr(&nodeDesc, nodeNum);
    return snapshot;
}

static BatchGetMixHitSnapshot DiffBatchGetMixHitSnapshot(const BatchGetMixHitSnapshot &before,
                                                         const BatchGetMixHitSnapshot &after)
{
    BatchGetMixHitSnapshot diff;
    diff.valid = before.valid && after.valid;
    if (!diff.valid) {
        return diff;
    }

    diff.rCacheHitMemCount = after.rCacheHitMemCount - before.rCacheHitMemCount;
    diff.rCacheHitDiskCount = after.rCacheHitDiskCount - before.rCacheHitDiskCount;
    diff.rCacheHitCount = after.rCacheHitCount - before.rCacheHitCount;
    diff.rCacheTotalCount = after.rCacheTotalCount - before.rCacheTotalCount;
    diff.wCacheHitMemCount = after.wCacheHitMemCount - before.wCacheHitMemCount;
    diff.wCacheHitDiskCount = after.wCacheHitDiskCount - before.wCacheHitDiskCount;
    diff.wCacheHitCount = after.wCacheHitCount - before.wCacheHitCount;
    diff.wCacheTotalCount = after.wCacheTotalCount - before.wCacheTotalCount;
    diff.backendHitCount = after.backendHitCount - before.backendHitCount;
    return diff;
}

static double BatchGetMixRatio(uint64_t numerator, uint64_t denominator)
{
    return denominator == 0 ? 0.0 : static_cast<double>(numerator) * 100.0 / static_cast<double>(denominator);
}

static bool CalcStandaloneBatchGetPerfLocation(uint64_t objectId, ObjLocation &location)
{
    const auto &cmConfig = BioConfig::Instance()->GetCmConfig();
    uint32_t ptNum = static_cast<uint32_t>(std::max<int32_t>(cmConfig.ptNum, 1));
    if (ptNum > UINT16_MAX) {
        return false;
    }
    size_t ptIndex = std::hash<uint64_t>{}(objectId) % ptNum;
    location.location[0] = static_cast<uint64_t>(ptIndex);
    location.location[1] = 0ULL;
    return true;
}

static bool CalcBatchGetPerfLocation(const std::string &key, ObjLocation &location)
{
    uint64_t objectId = static_cast<uint64_t>(std::hash<std::string>{}(key));
    auto ret = BioCalcLocation(gTenantId, objectId, &location);
    if (ret == RET_CACHE_OK) {
        return true;
    }
    return CalcStandaloneBatchGetPerfLocation(objectId, location);
}

static bool VerifyBatchGetDiskKeys(const std::vector<BatchGetMixKey> &keys, uint32_t &verifiedCount,
                                   bool &verifySkipped)
{
    constexpr uint32_t maxQueryCount = 256;
    verifiedCount = 0;
    verifySkipped = false;
    if (BioClient::Instance()->GetMode() != SEPARATES) {
        verifySkipped = true;
        return true;
    }

    std::vector<const char *> queryKeys;
    std::vector<ObjLocation> locations;
    std::vector<KeyAddrInfo> diskInfos(maxQueryCount);
    queryKeys.reserve(maxQueryCount);
    locations.reserve(maxQueryCount);

    for (size_t base = 0; base < keys.size(); base += maxQueryCount) {
        uint32_t count = static_cast<uint32_t>(std::min<size_t>(maxQueryCount, keys.size() - base));
        queryKeys.clear();
        locations.clear();
        for (uint32_t i = 0; i < count; ++i) {
            queryKeys.emplace_back(keys[base + i].key.c_str());
            locations.emplace_back(keys[base + i].location);
        }
        auto ret = BioBatchGetKeyDiskAddr(gTenantId, queryKeys.data(), locations.data(), count, diskInfos.data());
        if (ret != RET_CACHE_OK) {
            verifySkipped = true;
            return true;
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (diskInfos[i].result != RET_CACHE_OK || diskInfos[i].count == 0 || diskInfos[i].path[0] == '\0') {
                return false;
            }
            ++verifiedCount;
        }
    }
    return true;
}

static CResult RunBatchGetMixOnce(const std::vector<BatchGetMixKey> &keys, uint32_t bs, bool checkCrc,
                                  std::vector<BatchGetAlignedBuffer> &buffers, double &getUs, double &freeUs,
                                  BatchGetMixRunStats *stats = nullptr)
{
    uint32_t batchNum = static_cast<uint32_t>(keys.size());
    std::vector<const char *> rawKeys(batchNum);
    std::vector<ObjLocation> locations(batchNum);
    std::vector<uint64_t> offsets(batchNum, 0);
    std::vector<uint64_t> lengths(batchNum, bs);
    std::vector<uintptr_t> valueAddrs(batchNum, 0);
    std::vector<uint64_t> realLengths(batchNum, 0);
    std::vector<int32_t> results(batchNum, 0);
    if (buffers.empty()) {
        buffers.reserve(batchNum);
        for (uint32_t i = 0; i < batchNum; ++i) {
            buffers.emplace_back(AllocBatchGetAlignedBuffer(bs));
            if (buffers.back().get() == nullptr) {
                return RET_CACHE_ERROR;
            }
        }
    }
    if (buffers.size() != batchNum) {
        return RET_CACHE_ERROR;
    }
    for (uint32_t i = 0; i < batchNum; ++i) {
        rawKeys[i] = keys[i].key.c_str();
        locations[i] = keys[i].location;
        valueAddrs[i] = reinterpret_cast<uintptr_t>(buffers[i].get());
    }

    auto getStart = std::chrono::steady_clock::now();
    auto ret = BioBatchGet(gTenantId, rawKeys.data(), batchNum, offsets.data(), lengths.data(), locations.data(),
        valueAddrs.data(), realLengths.data(), results.data());
    auto getEnd = std::chrono::steady_clock::now();
    if (ret != RET_CACHE_OK) {
        (void)BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
        return ret;
    }
    for (uint32_t i = 0; i < batchNum; ++i) {
        if (results[i] != RET_CACHE_OK || realLengths[i] != bs) {
            if (stats != nullptr) {
                stats->failedItems++;
                if (stats->firstItemRet == RET_CACHE_OK && results[i] != RET_CACHE_OK) {
                    stats->firstItemRet = results[i];
                }
                if (realLengths[i] != bs) {
                    stats->shortReads++;
                }
            }
            (void)BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
            return results[i] == RET_CACHE_OK ? RET_CACHE_ERROR : static_cast<CResult>(results[i]);
        }
        if (checkCrc && keys[i].crc != BioCrcUtil::Crc32(reinterpret_cast<void *>(valueAddrs[i]), bs)) {
            (void)BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
            return RET_CACHE_ERROR;
        }
    }
    auto freeStart = std::chrono::steady_clock::now();
    ret = BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
    auto freeEnd = std::chrono::steady_clock::now();
    if (ret != RET_CACHE_OK) {
        return ret;
    }
    getUs = std::chrono::duration<double, std::micro>(getEnd - getStart).count();
    freeUs = std::chrono::duration<double, std::micro>(freeEnd - freeStart).count();
    return RET_CACHE_OK;
}

static void BatchGetMixPerfThread(const std::vector<BatchGetMixKey> *keys, uint32_t bs, uint32_t rounds,
                                  sem_t *startSem, BatchGetMixThreadResult *result)
{
    (void)sem_wait(startSem);
    std::vector<BatchGetAlignedBuffer> buffers;
    for (uint32_t round = 0; round < rounds; ++round) {
        double getUs = 0;
        double freeUs = 0;
        BatchGetMixRunStats runStats;
        auto ret = RunBatchGetMixOnce(*keys, bs, false, buffers, getUs, freeUs, &runStats);
        if (ret != RET_CACHE_OK) {
            result->ret = ret;
            result->failedItems += runStats.failedItems;
            if (result->firstItemRet == RET_CACHE_OK) {
                result->firstItemRet = runStats.firstItemRet;
            }
            result->shortReads += runStats.shortReads;
            return;
        }
        result->failedItems += runStats.failedItems;
        if (result->firstItemRet == RET_CACHE_OK) {
            result->firstItemRet = runStats.firstItemRet;
        }
        result->shortReads += runStats.shortReads;
        result->totalGetUs += getUs;
        result->totalFreeUs += freeUs;
        ++result->batches;
    }
}

static void SpreadBatchGetMixKeys(std::vector<BatchGetMixKey> &keys)
{
    std::vector<BatchGetMixKey> diskKeys;
    std::vector<BatchGetMixKey> cacheKeys;
    diskKeys.reserve(keys.size());
    cacheKeys.reserve(keys.size());
    for (const auto &key : keys) {
        if (key.expectCache) {
            cacheKeys.emplace_back(key);
        } else {
            diskKeys.emplace_back(key);
        }
    }

    std::vector<BatchGetMixKey> mixed;
    mixed.reserve(keys.size());
    size_t cacheIndex = 0;
    size_t diskIndex = 0;
    for (size_t pos = 0; pos < keys.size(); ++pos) {
        size_t expectedCache = ((pos + 1) * cacheKeys.size() + keys.size() - 1) / keys.size();
        if (cacheIndex < expectedCache && cacheIndex < cacheKeys.size()) {
            mixed.emplace_back(cacheKeys[cacheIndex++]);
        } else if (diskIndex < diskKeys.size()) {
            mixed.emplace_back(diskKeys[diskIndex++]);
        } else if (cacheIndex < cacheKeys.size()) {
            mixed.emplace_back(cacheKeys[cacheIndex++]);
        }
    }
    keys.swap(mixed);
}

bool ock::bio::diagnose::BioSdkCommand::mInited = false;
void* ock::bio::diagnose::BioSdkCommand::mHandler = nullptr;
CliRegCmdFuncPtr ock::bio::diagnose::BioSdkCommand::mRegOp = nullptr;
CliUnRegCmdFuncPtr ock::bio::diagnose::BioSdkCommand::mUnRegOp = nullptr;
CliPrintBufFuncPtr ock::bio::diagnose::BioSdkCommand::mPrintOp = nullptr;
CliSendBufFuncPtr ock::bio::diagnose::BioSdkCommand::mSendOp = nullptr;

int32_t diagnose::BioSdkCommand::LoadSymbols()
{
    const char* soFileName = "libcli_agent.so";
    mHandler = dlopen(soFileName, RTLD_NOW);
    if (mHandler == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen, error " << dlerror());
        return BIO_INNER_ERR;
    }

    mRegOp = reinterpret_cast<CliRegCmdFuncPtr>(dlsym(mHandler, "cli_register_command"));
    mUnRegOp = reinterpret_cast<CliUnRegCmdFuncPtr>(dlsym(mHandler, "cli_unregister_command"));
    mPrintOp = reinterpret_cast<CliPrintBufFuncPtr>(dlsym(mHandler, "cli_print_buffer"));
    mSendOp = reinterpret_cast<CliSendBufFuncPtr>(dlsym(mHandler, "cli_send_buffer"));
    if (mRegOp == nullptr || mUnRegOp == nullptr || mPrintOp == nullptr || mSendOp == nullptr) {
        CLIENT_LOG_ERROR("Failed to load function.");
        dlclose(mHandler);
        return BIO_INNER_ERR;
    }

    return BIO_OK;
}

void diagnose::BioSdkCommand::PrintLongText(const std::string &text)
{
    if (text.empty()) {
        return;
    }
    mSendOp(text.c_str(), static_cast<uint32_t>(text.size()));
}

int diagnose::BioSdkCommand::Initialize() noexcept
{
    if (mInited) {
        return 0;
    }

    auto ret = LoadSymbols();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to load symbols.");
        return ret;
    }

    CliCommand command;
    strncpy(command.command, "sdk", CLI_MAX_COMMAND_LEN);
    strncpy(command.description, "sdk commands.", CLI_MAX_CMD_DESC_LEN);
    command.handler = BioSdkDebugProcess;
    command.help_handler = BioSdkDebugHelp;
    auto result = mRegOp(&command);
    if (result == 0) {
        mInited = true;
    }
    return result;
}

void diagnose::BioSdkCommand::Destroy() noexcept
{
    if (mInited && mUnRegOp) {
        mUnRegOp((char *)"sdk");
        mInited = false;
    }

    if (mHandler) {
        dlclose(mHandler);
        mHandler = nullptr;
    }
}

void diagnose::BioSdkCommand::HandleListCache()
{
    auto caches = BioService::ListCache();
    if (caches.empty()) {
        mPrintOp("No cache is available.\n");
        return;
    }
    uint32_t i = 0;
    for (const auto &cache : caches) {
        mPrintOp("Cache#%u\n", i++);
        mPrintOp("\tTenantId:%llu\n", cache.tenantId);
        mPrintOp("\tAffinity:%u\n", cache.affinity);
        mPrintOp("\tStrategy:%u\n", cache.strategy);
    }
}

void diagnose::BioSdkCommand::HandleCreate(const std::vector<std::string> &cmds)
{
    for (int i = 1; i <= 3; i++) {
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("Invalid input.\n");
            return;
        }
    }
    uint32_t tenantId = 0;
    uint32_t affinity = 0;
    uint32_t strategy = 0;
    try {
        tenantId = std::stoul(cmds[1]);
        affinity = std::stoul(cmds[2]);
        strategy = std::stoul(cmds[3]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    CacheDescriptor desc = { tenantId, static_cast<AffinityStrategy>(affinity),
                             static_cast<WriteStrategy>(strategy)};
    auto ret = BioCreateCache(desc);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Create cache failed, result:%d.\n", ret);
    } else {
        mPrintOp("Create cache success, tenantId:%u.\n", tenantId);
        gTenantId = tenantId;
    }
}

void diagnose::BioSdkCommand::HandleOpen(const std::vector<std::string> &cmds)
{
    if (!IsUnsignedInteger(cmds[1])) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint32_t tenantId = 0;
    try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    CacheDescriptor desc;
    auto ret = BioGetCache(tenantId, &desc);
    if (ret != RET_CACHE_OK) {
        mPrintOp("The cache does not exist, tenantId:%u\n", tenantId);
    } else {
        mPrintOp("Open cache success, tenantId:%u\n", tenantId);
        gTenantId = desc.tenantId;
    }
}

void diagnose::BioSdkCommand::HandleDestroy(const std::vector<std::string> &cmds)
{
    if (!IsUnsignedInteger(cmds[1])) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint32_t tenantId = 0;
    try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    auto ret = BioDestroyCache(tenantId);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Destroy cache failed, result:%d, tenantId:%u.\n", ret, tenantId);
    } else {
        mPrintOp("Destroy cache success, tenantId:%u\n", tenantId);
    }
}

void diagnose::BioSdkCommand::HandlePut(const std::vector<std::string> &cmds)
{
    for (int i = 3; i <= 4; i++) {
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("Invalid input.\n");
            return;
        }
    }
    auto key = cmds[1].c_str();
    auto filePath = cmds[2].c_str();
    uint64_t length = 0;
    uint32_t sliceId = 0;
    try {
        length = std::stoull(cmds[3]);
        sliceId = std::stoul(cmds[4]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }

    ObjLocation location{};
    auto ret = BioCalcLocation(gTenantId, sliceId, &location);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Calculate location failed, result:%d.\n", ret);
        return;
    }
    mPrintOp("Location info: %u.\n", BioClient::Instance()->ParseLocation(location));

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        mPrintOp("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        mPrintOp("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    ret = BioPut(gTenantId, key, value, length, location);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to put a value, result:%d.\n", ret);
    } else {
        mPrintOp("Put value success, key:%s, length:%llu.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

void diagnose::BioSdkCommand::HandleGet(const std::vector<std::string> &cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("Invalid input.\n");
            return;
        }
    }
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t location = 0;

    auto key = cmds[1].c_str();
    try {
        offset = std::stoull(cmds[2]);
        length = std::stoull(cmds[3]);
        location = std::stoull(cmds[4]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }

    auto filePath = cmds[5].c_str();
    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "w")) == nullptr) {
        mPrintOp("fopen file failed, file:%s.\n", filePath);
        return;
    }
    char *value = new char[length];
    ObjLocation locationInfo{ location, 0 };
    uint64_t realLen = length;
    auto ret = BioGet(gTenantId, key, offset, length, locationInfo, value, &realLen);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to get a value, result:%d.\n", ret);
    } else {
        mPrintOp("Get value success, key:%s, offset:%llu, length:%llu, realLen:%llu, location:%llu.\n",
            key, offset, length, realLen, locationInfo.location[0]);
        if (fwrite(value, sizeof(char), realLen, fp) != realLen) {
            mPrintOp("fwrite value to file failed, errno:%d.\n", errno);
        }
    }
    delete[] value;
    fclose(fp);
}

void diagnose::BioSdkCommand::HandleList(const std::vector<std::string> &cmds)
{
    uint64_t location = 0;
    auto prefix = cmds[1].c_str();
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(gTenantId, prefix, &objs, &objNum);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to list all, result:%d.\n", ret);
    } else {
        mPrintOp("List all success, obj num: %lu.\n", objNum);
        for (uint32_t idx = 0; idx < objNum; idx++) {
            mPrintOp("Object#%u: key:%s, size:%u, time:%s",
                         idx, objs[idx].key, objs[idx].size, ctime(&objs[idx].time));
        }
        BioFreeListResources(&objs, objNum);
    }
}

void diagnose::BioSdkCommand::HandleNotifyUpdatePrepare(const std::vector<std::string> &cmds)
{
    uint32_t tenantId = 0;
        try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    auto ret = BioNotifyUpgradePrepare(tenantId);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to notify update, result:%d.\n", ret);
    } else {
        mPrintOp("Notify update prepare success, result:%d.\n", ret);
    }
}

void diagnose::BioSdkCommand::HandleNotifyUpdateFinish(const std::vector<std::string> &cmds)
{
    uint32_t tenantId = 0;
        try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    auto ret = BioNotifyUpgradeFinish(tenantId);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to notify update, result:%d.\n", ret);
    } else {
        mPrintOp("Notify update finish success, result:%d.\n", ret);
    }
}

void diagnose::BioSdkCommand::HandleCheckUpdateReady(const std::vector<std::string> &cmds)
{
    uint32_t tenantId = 0;
    try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    auto ret = BioCheckUpgradeReady(tenantId);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to check update, result:%d.\n", ret);
    } else {
        mPrintOp("Check update ready success, result:%d.\n", ret);
    }
}

void diagnose::BioSdkCommand::HandleStat(const std::vector<std::string> &cmds)
{
    if (!IsUnsignedInteger(cmds[NO_2])) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        location = std::stoull(cmds[NO_2]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    ObjLocation locationInfo{ location, 0 };
    ObjStat keyStat;
    auto ret = BioStat(gTenantId, key, locationInfo, &keyStat);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to get key stat, result:%d.\n", ret);
    } else {
        mPrintOp("Get key stat success.\n");
        mPrintOp("key:%s, location:%lu, size:%u, time:%s\n", key, locationInfo.location[0],
                     keyStat.size, ctime(&keyStat.time));
    }
}

void diagnose::BioSdkCommand::HandleExist(const std::vector<std::string> &cmds)
{
    if (!IsUnsignedInteger(cmds[NO_2])) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        location = std::stoull(cmds[NO_2]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    const char *keys[1] = { key };
    ObjLocation locationInfo{ location, 0 };
    ObjLocation locations[1] = { locationInfo };
    bool existFlags[1] = { false };
    auto ret = BioBatchExist(gTenantId, keys, locations, 1, existFlags);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to exist key, result:%d.\n", ret);
    } else {
        mPrintOp("Exist key success, key:%s, location:%lu, exist:%s.\n", key, locations[0].location[0],
            existFlags[0] ? "true" : "false");
    }
}

typedef struct {
    sem_t sem;
    CResult result;
} LoadContext;

static void TestCallback(void *context, int32_t result)
{
    LoadContext* loadCtx = reinterpret_cast<LoadContext*>(context);
    loadCtx->result = static_cast<CResult>(result);
    sem_post(&(loadCtx->sem));
}

void diagnose::BioSdkCommand::HandleLoad(const std::vector<std::string> &cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("Invalid input.\n");
            return;
        }
    }
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        offset = std::stoull(cmds[2]);
        length = std::stoull(cmds[3]);
        location = std::stoull(cmds[4]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }

    ObjLocation locationInfo{ location, 0 };
    LoadContext loadCtx;
    sem_init(&(loadCtx.sem), 0, 0);
    loadCtx.result = RET_CACHE_OK;
    auto ret = BioLoad(gTenantId, key, offset, length, locationInfo, TestCallback, &loadCtx);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Load failed, key:%s, result:%d.\n", key, ret);
        return;
    } else {
        sem_wait(&(loadCtx.sem));
        sem_destroy(&(loadCtx.sem));
        if (loadCtx.result == RET_CACHE_OK) {
            mPrintOp("Load success, key:%s.\n", key);
        } else {
            mPrintOp("Load failed; key:%s, result:%d.\n", key, loadCtx.result);
        }
    }
}

void diagnose::BioSdkCommand::HandleDelete(const std::vector<std::string> &cmds)
{
    if (!IsUnsignedInteger(cmds[2])) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        location = std::stoull(cmds[2]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    ObjLocation locationInfo{ location, 0 };
    auto ret = BioDelete(gTenantId, key, locationInfo);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to delete, key%s, result:%d.\n", key, ret);
    } else {
        mPrintOp("Delete key success, key:%s, location:%lu.\n", key, locationInfo.location[0]);
    }
}

void diagnose::BioSdkCommand::HandleAddDisk(const std::vector<std::string> &cmds)
{
    auto diskPath = cmds[1].c_str();
    auto ret = BioAddDisk(diskPath);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to add a disk, result:%d.\n", ret);
    } else {
        mPrintOp("Add disk success, diskPath:%s, tenantId:%llu\n", diskPath, gTenantId);
    }
}

void diagnose::BioSdkCommand::HandleShow(const std::vector<std::string> &cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "pt") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        cType = cmds[2].c_str();
        std::string type(cType);
        if (type == "all") {
            std::map<uint16_t, CmPtInfo> ptView = BioClient::Instance()->GetMirror()->GetPtView();
            std::string output = "Pt view:\n";
            for (auto &ptEntry : ptView) {
                output += ptEntry.second.ToString();
                output += "\n";
            }
            PrintLongText(output);
        } else if (type == "affinity") {
            std::vector<uint16_t> ptList = BioClient::Instance()->GetMirror()->ListLocalAffinityPt();
            std::string output = "Local affinity pt list:\n";
            for (auto &entry : ptList) {
                output += " ";
                output += std::to_string(entry);
                output += "\n";
            }
            PrintLongText(output);
        }
    } else if (viewType == "node") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioClient::Instance()->GetMirror()->GetNodeView();
        std::string output = "Node view:\n";
        for (auto &nodeEntry : nodeView) {
            output += nodeEntry.second.ToString();
            output += "\n";
        }
        output += "Local Node:";
        CmNodeId localNode = BioClient::Instance()->GetMirror()->GetLocalNodeInfo();
        output += localNode.ToString();
        output += "\n";
        PrintLongText(output);
    } else if (viewType == "flow") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint16_t ptId = std::stoull(cmds[2]);
        auto flowInst = BioClient::Instance()->GetMirror()->Query(ptId);
        if (UNLIKELY(flowInst == nullptr)) {
            mPrintOp("flow is null pt:%u", ptId);
            return;
        }
        mPrintOp("flow id: %lu.\n", flowInst->FlowId());
        mPrintOp("flow status: %d.\n", flowInst->IsNormal() ? 1 : 0);
        mPrintOp("flow offset: %lu.\n", flowInst->GetOffset());
        mPrintOp("flow index: %lu.\n", flowInst->GetIndex());
    } else {
        mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
    }
}

void diagnose::BioSdkCommand::HandleShowCacheHit(const std::vector<std::string> &cmds)
{
    CacheHitFinalDesc desc;
    CacheHitFinalDesc *nodeDesc = NULL;
    uint64_t nodeNum = 0;
    auto ret = BioShowCacheHitRatio(&desc, &nodeDesc, &nodeNum);
    if (ret != RET_CACHE_OK) {
        BioFreeCacheHitPtr(&nodeDesc, nodeNum);
        mPrintOp("Show Cache Hit failed, result:%d.\n", ret);
        return;
    }

    double rCacheHitMemRatio = desc.wCacheTotalCount != 0 ?
                               (double)desc.rCacheHitMemCount / (double)desc.wCacheTotalCount : 0;
    double rCacheHitDiskRatio = desc.wCacheTotalCount != 0 ?
                               (double)desc.rCacheHitDiskCount / (double)desc.wCacheTotalCount : 0;
    double rCacheHitRatio = desc.wCacheTotalCount != 0 ?
                            (double)desc.rCacheHitCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitMemRatio = desc.wCacheHitMemCount != 0 ?
                               (double)desc.wCacheHitMemCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitDiskRatio = desc.wCacheHitDiskCount != 0 ?
                               (double)desc.wCacheHitDiskCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitRatio = desc.wCacheTotalCount != 0 ?
                            (double)desc.wCacheHitCount / (double)desc.wCacheTotalCount : 0;
    double backendHitRatio = desc.wCacheTotalCount != 0 ?
                          (double)desc.backendHitCount / (double)desc.wCacheTotalCount : 0;
    double totalCacheHitRatio = rCacheHitRatio + wCacheHitRatio;
    mPrintOp("--------------------------------\n");
    mPrintOp("all node totalCacheHitRatio :%.2f%%.\n", totalCacheHitRatio * 100);
    mPrintOp("all node rCacheHitMemRatio :%.2f%%.\n", rCacheHitMemRatio * 100);
    mPrintOp("all node rCacheHitDiskRatio :%.2f%%.\n", rCacheHitDiskRatio * 100);
    mPrintOp("all node rCacheHitRatio :%.2f%%.\n", rCacheHitRatio * 100);
    mPrintOp("all node wCacheHitMemRatio :%.2f%%.\n", wCacheHitMemRatio * 100);
    mPrintOp("all node wCacheHitDiskRatio :%.2f%%.\n", wCacheHitDiskRatio * 100);
    mPrintOp("all node wCacheHitRatio :%.2f%%.\n", wCacheHitRatio * 100);
    mPrintOp("all node backendHitRatio :%.2f%%.\n", backendHitRatio * 100);
    mPrintOp("----------------------------------\n");
    for (int i = 0; i < nodeNum; i++) {
        uint16_t nodeId = nodeDesc[i].nodeId;
        double nodeRCacheHitMemRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                   (double)nodeDesc[i].rCacheHitMemCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeRCacheHitDiskRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                       (double)nodeDesc[i].rCacheHitDiskCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeRCacheHitRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                    (double)nodeDesc[i].rCacheHitCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitMemRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                       (double)nodeDesc[i].wCacheHitMemCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitDiskRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                        (double)nodeDesc[i].wCacheHitDiskCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                    (double)nodeDesc[i].wCacheHitCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeBackendHitRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                    (double)nodeDesc[i].backendHitCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeTotalCacheHitRatio = nodeRCacheHitRatio + nodeWCacheHitRatio;
        mPrintOp("node: %d totalHitCacheRatio :%.2f%%.\n", nodeId, nodeTotalCacheHitRatio * 100);
        mPrintOp("node: %d rCacheHitMemRatio :%.2f%%.\n", nodeId, nodeRCacheHitMemRatio * 100);
        mPrintOp("node: %d rCacheHitDiskRatio :%.2f%%.\n", nodeId, nodeRCacheHitDiskRatio * 100);
        mPrintOp("node: %d rCacheHitRatio :%.2f%%.\n", nodeId, nodeRCacheHitRatio * 100);
        mPrintOp("node: %d wCacheHitMemRatio :%.2f%%.\n", nodeId, nodeWCacheHitMemRatio * 100);
        mPrintOp("node: %d wCacheHitDiskRatio :%.2f%%.\n", nodeId, nodeWCacheHitDiskRatio * 100);
        mPrintOp("node: %d wCacheHitRatio :%.2f%%.\n", nodeId, nodeWCacheHitRatio * 100);
        mPrintOp("node: %d backendHitRatio :%.2f%%.\n", nodeId, nodeBackendHitRatio * 100);
        mPrintOp("--------------------------------\n");
    }
    BioFreeCacheHitPtr(&nodeDesc, nodeNum);
}

void diagnose::BioSdkCommand::HandleShowCacheResource(const std::vector<std::string> &cmds)
{
    CacheResourcesDesc *nodeDesc = NULL;
    uint64_t nodeNum = 0;
    auto ret = BioShowCacheResource(&nodeDesc, &nodeNum);
    if (ret != RET_CACHE_OK) {
        BioFreeCacheResourcePtr(&nodeDesc, nodeNum);
        mPrintOp("Show Cache Resource failed, result:%d \n", ret);
        return;
    }
    mPrintOp("--------------------------------\n");
    for (int i = 0; i < nodeNum; i++) {
        uint16_t nodeId = nodeDesc[i].nodeId;
        if (nodeDesc[i].rCacheMemCapacity == 0 || nodeDesc[i].rCacheDiskCapacity == 0
            || nodeDesc[i].wCacheMemCapacity == 0 || nodeDesc[i].wCacheDiskCapacity == 0) {
            mPrintOp("node Capacity is zero, nodeId:%d  rCacheMemCapacity(MB):%llu  rCacheDiskCapacity(MB):%llu \n",
                         nodeId, nodeDesc[i].rCacheMemCapacity / NO_1048576,
                         nodeDesc[i].rCacheDiskCapacity / NO_1048576);
            mPrintOp("wCacheMemCapacity(MB):%llu  wCacheDiskCapacity(MB):%llu \n",
                         nodeDesc[i].wCacheMemCapacity / NO_1048576, nodeDesc[i].wCacheDiskCapacity / NO_1048576);
            continue;
        }
        mPrintOp("node: %d cache resources information(MB): \n", nodeId);
        double wCacheMemWaterLever = (double)nodeDesc[i].wCacheMemUsedSize / (double)nodeDesc[i].wCacheMemCapacity;
        double rCacheMemWaterLever = (double)nodeDesc[i].rCacheMemUsedSize / (double)nodeDesc[i].rCacheMemCapacity;
        double wCacheDiskWaterLever = (double)nodeDesc[i].wCacheDiskUsedSize / (double)nodeDesc[i].wCacheDiskCapacity;
        double rCacheDiskWaterLever = (double)nodeDesc[i].rCacheDiskUsedSize / (double)nodeDesc[i].rCacheDiskCapacity;
        mPrintOp("wCacheMemCapacity %llu   wCacheDiskCapacity %llu \n",
                     nodeDesc[i].wCacheMemCapacity / NO_1048576, nodeDesc[i].wCacheDiskCapacity / NO_1048576);
        mPrintOp("rCacheMemCapacity %llu   rCacheDiskCapacity %llu \n",
                     nodeDesc[i].rCacheMemCapacity / NO_1048576, nodeDesc[i].rCacheDiskCapacity / NO_1048576);
        mPrintOp("wCacheMemUsedSize %llu   wCacheDiskUsedSize %llu \n",
                     nodeDesc[i].wCacheMemUsedSize / NO_1048576, nodeDesc[i].wCacheDiskUsedSize / NO_1048576);
        mPrintOp("wCacheMemWaterLever %.4f%%   wCacheDiskWaterLever %.4f%% \n",
                     wCacheMemWaterLever * 100, wCacheDiskWaterLever * 100);
        mPrintOp("rCacheMemUsedSize %llu   rCacheDiskUsedSize %llu \n",
                     nodeDesc[i].rCacheMemUsedSize / NO_1048576, nodeDesc[i].rCacheDiskUsedSize / NO_1048576);
        mPrintOp("rCacheMemWaterLever %.4f%%   rCacheDiskWaterLever %.4f%% \n",
                     rCacheMemWaterLever * 100, rCacheDiskWaterLever * 100);
        mPrintOp("--------------------------------\n");
    }
    BioFreeCacheResourcePtr(&nodeDesc, nodeNum);
}

void diagnose::BioSdkCommand::HandleSdkTrace(const std::vector<std::string> &cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "show") {
        auto info = ock::htracer::GetTraceInfo();
        PrintLongText(info);
    } else if (viewType == "clear") {
        ock::htracer::ClearTraceInfo();
        mPrintOp("clearing statistics sdk records succeeded.\n");
    } else if (viewType == "open") {
        ock::htracer::HTracerSetEnable(true);
        mPrintOp("open statistics sdk records succeeded.\n");
    } else if (viewType == "close") {
        ock::htracer::HTracerSetEnable(false);
        mPrintOp("close statistics sdk records succeeded.\n");
    }
}

void* diagnose::BioSdkCommand::PerfTestPutImpl(void *param)
{
    auto *getParam = (PerfTestParam *)param;
    static std::atomic<uint32_t> sliceId(0);
    std::atomic<int32_t> keyIndex(1);

    ObjLocation location{};
    auto ret = BioCalcLocation(gTenantId, (++sliceId), &location);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Calculate location failed, result:%d.\n", ret);
        getParam->result = ret;
        getParam->done = true;
        return nullptr;
    }

    char *value = new char[getParam->length];
    memset(value, 66, getParam->length);
    char key[128];

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        sprintf(key, "file_%u_%d", getParam->tid, keyIndex.load());
        ret = BioPut(gTenantId, key, value, getParam->length, location);
        if (ret != RET_CACHE_OK) {
            getParam->result = ret;
            break;
        }
        keyIndex++;
        gLocationLock.LockWrite();
        gLocation.emplace(key, location);
        gLocationLock.UnLock();
    }

    delete[] value;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

void* diagnose::BioSdkCommand::PerfTestGetImpl(void *param)
{
    auto *getParam = (PerfTestParam *)param;
    char *value = new char[getParam->length];
    char key[128];
    std::atomic<int32_t> keyIndex(1);

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        sprintf(key, "file_%u_%d", getParam->tid, keyIndex.load());
        auto iter = gLocation.find(key);
        if (iter == gLocation.end()) {
            getParam->result = BIO_ERR;
            break;
        }
        uint64_t realLen = 0;
        auto ret = BioGet(gTenantId, key, 0, getParam->length, iter->second, value, &realLen);
        if (ret != RET_CACHE_OK) {
            getParam->result = ret;
            break;
        }
        keyIndex++;
    }

    delete[] value;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

void diagnose::BioSdkCommand::HandleBatchGet(const std::vector<std::string> &cmds)
{
    uint32_t bs = (std::stoul(cmds[1]) * 1024);
    uint32_t batchNum = std::stoul(cmds[2]);
    uint64_t runId = gBatchGetRunId.fetch_add(1, std::memory_order_relaxed);
    char key[MAX_KEY_SIZE];
    std::vector<std::string> prepareKeys;
    std::vector<ObjLocation> prepareLocations;
    std::vector<std::unique_ptr<char[]>> values;
    prepareKeys.reserve(batchNum);
    prepareLocations.reserve(batchNum);
    values.reserve(batchNum);
    uint32_t keyIndex = 0;
    for (uint32_t idx = 0; idx < batchNum; idx++) {
        int retKey = snprintf_s(key, sizeof(key), sizeof(key) - 1, "file_%u_%llu_%u", getpid(),
            static_cast<unsigned long long>(runId), keyIndex);
        if (retKey <= 0 || static_cast<size_t>(retKey) >= sizeof(key)) {
            mPrintOp("Generate key failed, index:%u.\n", keyIndex);
            return;
        }

        ObjLocation location{};
        if (!CalcBatchGetPerfLocation(key, location)) {
            mPrintOp("Calculate location failed.\n");
            return;
        }
        CResult ret = RET_CACHE_OK;
        std::unique_ptr<char[]> value(new (std::nothrow) char[bs]);
        if (value == nullptr) {
            mPrintOp("Malloc fail.");
            return;
        }
        FillBatchGetValue(value.get(), bs, runId, idx);
        ret = BioPut(gTenantId, key, value.get(), bs, location);
        if (ret != RET_CACHE_OK) {
            mPrintOp("Put key(%s) fail, result:%d\n", key, ret);
            return;
        }
        keyIndex++;
        prepareKeys.emplace_back(key);
        prepareLocations.emplace_back(location);
        values.emplace_back(std::move(value));
    }

    std::vector<const char *> keys(batchNum, nullptr);
    std::vector<uint64_t> offsets(batchNum, 0);
    std::vector<uint64_t> lengths(batchNum, bs);
    std::vector<ObjLocation> locations(batchNum);
    std::vector<uintptr_t> valueAddrs(batchNum, 0);
    std::vector<uint64_t> realLengths(batchNum, 0);
    std::vector<int32_t> results(batchNum, 0);
    std::vector<BatchGetAlignedBuffer> getBuffers;
    getBuffers.reserve(batchNum);
    std::unique_ptr<bool[]> existFlags(new (std::nothrow) bool[batchNum]());
    if (existFlags == nullptr) {
        mPrintOp("Malloc fail.");
        return;
    }
    for (uint32_t i = 0; i < batchNum; i++) {
        keys[i] = prepareKeys[i].c_str();
        locations[i] = prepareLocations[i];
        auto getBuffer = AllocBatchGetAlignedBuffer(bs);
        if (getBuffer == nullptr) {
            mPrintOp("Malloc fail.");
            return;
        }
        valueAddrs[i] = reinterpret_cast<uintptr_t>(getBuffer.get());
        getBuffers.emplace_back(std::move(getBuffer));
    }

    auto result = BioBatchExistChunked(gTenantId, keys.data(), locations.data(), batchNum, existFlags.get());
    if (result != 0) {
        mPrintOp("Bio batch exist fail, ret:%d.\n", result);
        return;
    }
    for (uint32_t i = 0; i < batchNum; i++) {
        if (!existFlags.get()[i]) {
            mPrintOp("Bio batch exit, key:%s is not exist.\n", keys[i]);
            return;
        }
    }

    result = BioBatchGet(gTenantId, keys.data(), batchNum, offsets.data(), lengths.data(), locations.data(),
                         valueAddrs.data(), realLengths.data(), results.data());

    if (result != 0) {
        mPrintOp("Bio batch get fail, ret:%d.\n", result);
        return;
    }
    for (uint32_t i = 0; i < batchNum; i++) {
        if (results[i] != 0) {
            mPrintOp("Bio batch get fail, key:%s, ret:%d.\n", keys[i], results[i]);
            (void)BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
            return;
        }
        if (BioCrcUtil::Crc32(reinterpret_cast<void*>(values[i].get()), bs) !=
            BioCrcUtil::Crc32(reinterpret_cast<void*>(valueAddrs[i]), bs)) {
            mPrintOp("Bio batch get fail, key:%s, crc check fail.\n", keys[i]);
            (void)BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
            return;
        }
    }
    if (BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum) != 0) {
        mPrintOp("Bio batch get free shm fail.\n");
    }
    mPrintOp("Bio batch get success!\n");
}

void diagnose::BioSdkCommand::HandleBatchGetPerf(const std::vector<std::string> &cmds)
{
    constexpr uint32_t maxBatchCount = STANDALONE_BATCH_GET_MAX_COUNT;
    constexpr uint32_t bytesPerMb = 1024U * 1024U;
    for (size_t i = 1; i < cmds.size(); ++i) {
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("Invalid input.\n");
            return;
        }
    }
    if (gTenantId == UINT64_MAX) {
        mPrintOp("Create and open a cache first!\n");
        return;
    }

    uint32_t bs = 0;
    uint32_t batchNum = 0;
    uint32_t rounds = 0;
    uint64_t fillMb = 0;
    try {
        uint64_t bsKb = std::stoull(cmds[1]);
        if (bsKb > UINT32_MAX / 1024U) {
            mPrintOp("Invalid block size.\n");
            return;
        }
        bs = static_cast<uint32_t>(bsKb * 1024U);
        batchNum = std::stoul(cmds[2]);
        rounds = std::stoul(cmds[3]);
        fillMb = std::stoull(cmds[4]);
    } catch (const std::exception &) {
        mPrintOp("Invalid input.\n");
        return;
    }
    if (bs == 0 || batchNum == 0 || batchNum > maxBatchCount || rounds == 0 || fillMb == 0) {
        mPrintOp("Invalid param, bs:%u, batchNum:%u, rounds:%u, fillMb:%llu.\n", bs, batchNum, rounds,
                 static_cast<unsigned long long>(fillMb));
        return;
    }

    uint64_t runId = gBatchGetRunId.fetch_add(1, std::memory_order_relaxed);
    std::vector<std::string> targetKeys;
    std::vector<ObjLocation> locations(batchNum);
    std::vector<uint32_t> expectedCrc(batchNum);
    targetKeys.reserve(batchNum);
    std::unique_ptr<char[]> value(new (std::nothrow) char[std::max(bs, bytesPerMb)]);
    if (value == nullptr) {
        mPrintOp("Malloc fail.\n");
        return;
    }

    char key[MAX_KEY_SIZE];
    mPrintOp("BatchGet perf prepare targets, bs:%u, batchNum:%u.\n", bs, batchNum);
    for (uint32_t i = 0; i < batchNum; ++i) {
        int keyLen = snprintf_s(key, sizeof(key), sizeof(key) - 1, "batchget_perf_%u_%llu_target_%u", getpid(),
            static_cast<unsigned long long>(runId), i);
        if (keyLen <= 0 || static_cast<size_t>(keyLen) >= sizeof(key)) {
            mPrintOp("Generate target key failed, index:%u.\n", i);
            return;
        }
        if (!CalcBatchGetPerfLocation(key, locations[i])) {
            mPrintOp("Calculate target location failed, index:%u.\n", i);
            return;
        }
        FillBatchGetValue(value.get(), bs, runId, i);
        expectedCrc[i] = BioCrcUtil::Crc32(value.get(), bs);
        auto ret = PutBatchGetPerfValue(key, value.get(), bs, locations[i]);
        if (ret != RET_CACHE_OK) {
            mPrintOp("Put target failed, index:%u, ret:%d.\n", i, ret);
            return;
        }
        targetKeys.emplace_back(key);
    }

    FillBatchGetValue(value.get(), bytesPerMb, runId, batchNum);
    mPrintOp("BatchGet perf write filler, size:%llu MB.\n", static_cast<unsigned long long>(fillMb));
    for (uint64_t i = 0; i < fillMb; ++i) {
        int keyLen = snprintf_s(key, sizeof(key), sizeof(key) - 1, "batchget_perf_%u_%llu_filler_%llu", getpid(),
            static_cast<unsigned long long>(runId), static_cast<unsigned long long>(i));
        if (keyLen <= 0 || static_cast<size_t>(keyLen) >= sizeof(key)) {
            mPrintOp("Generate filler key failed, index:%llu.\n", static_cast<unsigned long long>(i));
            return;
        }
        ObjLocation fillerLocation{};
        if (!CalcBatchGetPerfLocation(key, fillerLocation)) {
            mPrintOp("Calculate filler location failed, index:%llu.\n", static_cast<unsigned long long>(i));
            return;
        }
        auto ret = PutBatchGetPerfValue(key, value.get(), bytesPerMb, fillerLocation);
        if (ret != RET_CACHE_OK) {
            mPrintOp("Put filler failed, index:%llu, ret:%d.\n", static_cast<unsigned long long>(i), ret);
            return;
        }
    }

    std::vector<const char *> keys(batchNum);
    std::vector<KeyAddrInfo> diskInfos(batchNum);
    for (uint32_t i = 0; i < batchNum; ++i) {
        keys[i] = targetKeys[i].c_str();
    }
    mPrintOp("BatchGet perf verify target data is on BDM disk.\n");
    auto ret = BioBatchGetKeyDiskAddr(gTenantId, keys.data(), locations.data(), batchNum, diskInfos.data());
    if (ret != RET_CACHE_OK) {
        mPrintOp("Get target disk address failed, ret:%d.\n", ret);
        return;
    }
    for (uint32_t i = 0; i < batchNum; ++i) {
        if (diskInfos[i].result != RET_CACHE_OK || diskInfos[i].count == 0 || diskInfos[i].path[0] == '\0') {
            mPrintOp("Target is not on disk, index:%u, ret:%d, count:%u.\n", i, diskInfos[i].result,
                diskInfos[i].count);
            return;
        }
    }
    mPrintOp("BatchGet perf disk verified, path:%s.\n", diskInfos[0].path);

    std::vector<uint64_t> offsets(batchNum, 0);
    std::vector<uint64_t> lengths(batchNum, bs);
    std::vector<uintptr_t> valueAddrs(batchNum, 0);
    std::vector<uint64_t> realLengths(batchNum, 0);
    std::vector<int32_t> results(batchNum, 0);
    std::vector<double> getUs;
    std::vector<double> freeUs;
    getUs.reserve(rounds);
    freeUs.reserve(rounds);
    for (uint32_t round = 0; round <= rounds; ++round) {
        std::fill(valueAddrs.begin(), valueAddrs.end(), 0);
        std::fill(realLengths.begin(), realLengths.end(), 0);
        std::fill(results.begin(), results.end(), 0);
        auto getStart = std::chrono::steady_clock::now();
        ret = BioBatchGet(gTenantId, keys.data(), batchNum, offsets.data(), lengths.data(), locations.data(),
            valueAddrs.data(), realLengths.data(), results.data());
        auto getEnd = std::chrono::steady_clock::now();
        if (ret != RET_CACHE_OK) {
            mPrintOp("BatchGet failed, round:%u, ret:%d.\n", round, ret);
            return;
        }
        for (uint32_t i = 0; i < batchNum; ++i) {
            if (results[i] != RET_CACHE_OK || realLengths[i] != bs) {
                mPrintOp("BatchGet item failed, round:%u, index:%u, ret:%d, length:%llu.\n", round, i, results[i],
                    static_cast<unsigned long long>(realLengths[i]));
                (void)BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
                return;
            }
            if (round == 0 && expectedCrc[i] != BioCrcUtil::Crc32(reinterpret_cast<void *>(valueAddrs[i]), bs)) {
                mPrintOp("BatchGet crc check failed, index:%u.\n", i);
                (void)BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
                return;
            }
        }
        auto freeStart = std::chrono::steady_clock::now();
        ret = BioBatchGetFree(gTenantId, valueAddrs.data(), batchNum);
        auto freeEnd = std::chrono::steady_clock::now();
        if (ret != RET_CACHE_OK) {
            mPrintOp("BatchGet free failed, round:%u, ret:%d.\n", round, ret);
            return;
        }
        if (round == 0) {
            mPrintOp("BatchGet perf warmup and crc verification success.\n");
            continue;
        }
        double oneGetUs = std::chrono::duration<double, std::micro>(getEnd - getStart).count();
        double oneFreeUs = std::chrono::duration<double, std::micro>(freeEnd - freeStart).count();
        getUs.emplace_back(oneGetUs);
        freeUs.emplace_back(oneFreeUs);
        double dataMb = static_cast<double>(bs) * batchNum / bytesPerMb;
        mPrintOp("BatchGet perf round:%u get_us:%.2f free_us:%.2f e2e_us:%.2f e2e_bw_MBps:%.2f.\n", round,
            oneGetUs, oneFreeUs, oneGetUs + oneFreeUs, dataMb * 1000000.0 / (oneGetUs + oneFreeUs));
    }

    double totalGetUs = 0;
    double totalFreeUs = 0;
    for (uint32_t i = 0; i < rounds; ++i) {
        totalGetUs += getUs[i];
        totalFreeUs += freeUs[i];
    }
    double avgGetUs = totalGetUs / rounds;
    double avgFreeUs = totalFreeUs / rounds;
    double avgE2eUs = avgGetUs + avgFreeUs;
    double dataMb = static_cast<double>(bs) * batchNum / bytesPerMb;
    mPrintOp("BatchGet perf result bs:%u batch:%u rounds:%u fill_MB:%llu get_us:%.2f free_us:%.2f e2e_us:%.2f "
        "get_bw_MBps:%.2f e2e_bw_MBps:%.2f key_IOPS:%.2f disk:%s.\n",
        bs, batchNum, rounds, static_cast<unsigned long long>(fillMb), avgGetUs, avgFreeUs, avgE2eUs,
        dataMb * 1000000.0 / avgGetUs, dataMb * 1000000.0 / avgE2eUs,
        batchNum * 1000000.0 / avgE2eUs, diskInfos[0].path);
}

void diagnose::BioSdkCommand::HandleBatchGetMixPerf(const std::vector<std::string> &cmds)
{
    constexpr uint32_t maxBatchCount = STANDALONE_BATCH_GET_MAX_COUNT;
    constexpr uint32_t bytesPerMb = 1024U * 1024U;
    for (size_t i = 1; i < cmds.size(); ++i) {
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("Invalid input.\n");
            return;
        }
    }
    if (gTenantId == UINT64_MAX) {
        mPrintOp("Create and open a cache first!\n");
        return;
    }

    uint32_t bs = 0;
    uint32_t batchNum = 0;
    uint32_t concurrency = 0;
    uint32_t hitPercent = 0;
    uint32_t rounds = 0;
    uint64_t fillMb = 0;
    try {
        uint64_t bsKb = std::stoull(cmds[1]);
        if (bsKb > UINT32_MAX / 1024U) {
            mPrintOp("Invalid block size.\n");
            return;
        }
        bs = static_cast<uint32_t>(bsKb * 1024U);
        batchNum = std::stoul(cmds[2]);
        concurrency = std::stoul(cmds[3]);
        hitPercent = std::stoul(cmds[4]);
        rounds = std::stoul(cmds[5]);
        fillMb = std::stoull(cmds[6]);
    } catch (const std::exception &) {
        mPrintOp("Invalid input.\n");
        return;
    }
    if (bs == 0 || batchNum == 0 || batchNum > maxBatchCount || concurrency == 0 || concurrency > 64 ||
        hitPercent > 100 || rounds == 0 || fillMb == 0) {
        mPrintOp("Invalid param, bs:%u, batch:%u, concurrency:%u, hit_percent:%u, rounds:%u, fill_MB:%llu.\n",
            bs, batchNum, concurrency, hitPercent, rounds, static_cast<unsigned long long>(fillMb));
        return;
    }

    uint32_t cacheKeysPerBatch = (batchNum * hitPercent + 50U) / 100U;
    if (cacheKeysPerBatch > batchNum) {
        cacheKeysPerBatch = batchNum;
    }
    uint32_t diskKeysPerBatch = batchNum - cacheKeysPerBatch;
    uint32_t totalCacheKeys = cacheKeysPerBatch * concurrency;
    uint32_t totalDiskKeys = diskKeysPerBatch * concurrency;
    uint64_t runId = gBatchGetRunId.fetch_add(1, std::memory_order_relaxed);
    std::unique_ptr<char[]> value(new (std::nothrow) char[std::max(bs, bytesPerMb)]);
    if (value == nullptr) {
        mPrintOp("Malloc fail.\n");
        return;
    }

    std::vector<std::vector<BatchGetMixKey>> workerKeys(concurrency);
    std::vector<BatchGetMixKey> diskKeys;
    std::vector<BatchGetMixKey> cacheKeys;
    diskKeys.reserve(totalDiskKeys);
    cacheKeys.reserve(totalCacheKeys);

    char key[MAX_KEY_SIZE];
    mPrintOp("BatchGet mix perf prepare disk targets, bs:%u, batch:%u, concurrency:%u, hit_percent:%u, "
             "cache_per_batch:%u, disk_per_batch:%u.\n",
        bs, batchNum, concurrency, hitPercent, cacheKeysPerBatch, diskKeysPerBatch);
    auto prepareDiskStart = std::chrono::steady_clock::now();
    for (uint32_t worker = 0; worker < concurrency; ++worker) {
        for (uint32_t i = 0; i < diskKeysPerBatch; ++i) {
            int keyLen = snprintf_s(key, sizeof(key), sizeof(key) - 1,
                "batchget_mix_%u_%llu_w%u_disk_%u", getpid(), static_cast<unsigned long long>(runId), worker, i);
            if (keyLen <= 0 || static_cast<size_t>(keyLen) >= sizeof(key)) {
                mPrintOp("Generate disk key failed, worker:%u, index:%u.\n", worker, i);
                return;
            }
            BatchGetMixKey item;
            item.key = key;
            item.expectCache = false;
            if (!CalcBatchGetPerfLocation(item.key, item.location)) {
                mPrintOp("Calculate disk key location failed, worker:%u, index:%u.\n", worker, i);
                return;
            }
            FillBatchGetValue(value.get(), bs, runId, worker * batchNum + i);
            item.crc = BioCrcUtil::Crc32(value.get(), bs);
            auto ret = PutBatchGetPerfValue(item.key.c_str(), value.get(), bs, item.location);
            if (ret != RET_CACHE_OK) {
                mPrintOp("Put disk target failed, worker:%u, index:%u, ret:%d.\n", worker, i, ret);
                return;
            }
            diskKeys.emplace_back(item);
            workerKeys[worker].emplace_back(item);
        }
    }
    auto prepareDiskEnd = std::chrono::steady_clock::now();

    FillBatchGetValue(value.get(), bytesPerMb, runId, batchNum + concurrency);
    mPrintOp("BatchGet mix perf write filler, size:%llu MB.\n", static_cast<unsigned long long>(fillMb));
    auto fillerStart = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < fillMb; ++i) {
        int keyLen = snprintf_s(key, sizeof(key), sizeof(key) - 1, "batchget_mix_%u_%llu_filler_%llu", getpid(),
            static_cast<unsigned long long>(runId), static_cast<unsigned long long>(i));
        if (keyLen <= 0 || static_cast<size_t>(keyLen) >= sizeof(key)) {
            mPrintOp("Generate filler key failed, index:%llu.\n", static_cast<unsigned long long>(i));
            return;
        }
        ObjLocation fillerLocation{};
        if (!CalcBatchGetPerfLocation(key, fillerLocation)) {
            mPrintOp("Calculate filler location failed, index:%llu.\n", static_cast<unsigned long long>(i));
            return;
        }
        auto ret = PutBatchGetPerfValue(key, value.get(), bytesPerMb, fillerLocation);
        if (ret != RET_CACHE_OK) {
            mPrintOp("Put filler failed, index:%llu, ret:%d.\n", static_cast<unsigned long long>(i), ret);
            return;
        }
    }
    auto fillerEnd = std::chrono::steady_clock::now();

    mPrintOp("BatchGet mix perf wait for async eviction.\n");
    auto evictWaitStart = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(3));
    auto evictWaitEnd = std::chrono::steady_clock::now();

    uint32_t verifiedDiskKeys = 0;
    bool diskVerifySkipped = false;
    if (!VerifyBatchGetDiskKeys(diskKeys, verifiedDiskKeys, diskVerifySkipped)) {
        mPrintOp("BatchGet mix disk verification failed, expected:%u, verified:%u.\n", totalDiskKeys,
            verifiedDiskKeys);
        return;
    }
    if (diskVerifySkipped) {
        mPrintOp("BatchGet mix disk verification skipped, current deployment does not support disk address query.\n");
    }

    BatchGetMixWaterLevelGuard memHitGuard(100);
    mPrintOp("BatchGet mix perf prepare cache targets, raise wcache mem water level:%llu => 100.\n",
        static_cast<unsigned long long>(memHitGuard.OldLevel()));
    auto prepareCacheStart = std::chrono::steady_clock::now();
    for (uint32_t worker = 0; worker < concurrency; ++worker) {
        for (uint32_t i = 0; i < cacheKeysPerBatch; ++i) {
            int keyLen = snprintf_s(key, sizeof(key), sizeof(key) - 1,
                "batchget_mix_%u_%llu_w%u_cache_%u", getpid(), static_cast<unsigned long long>(runId), worker, i);
            if (keyLen <= 0 || static_cast<size_t>(keyLen) >= sizeof(key)) {
                mPrintOp("Generate cache key failed, worker:%u, index:%u.\n", worker, i);
                return;
            }
            BatchGetMixKey item;
            item.key = key;
            item.expectCache = true;
            if (!CalcBatchGetPerfLocation(item.key, item.location)) {
                mPrintOp("Calculate cache key location failed, worker:%u, index:%u.\n", worker, i);
                return;
            }
            FillBatchGetValue(value.get(), bs, runId, totalDiskKeys + worker * batchNum + i);
            item.crc = BioCrcUtil::Crc32(value.get(), bs);
            auto ret = PutBatchGetPerfValue(item.key.c_str(), value.get(), bs, item.location);
            if (ret != RET_CACHE_OK) {
                mPrintOp("Put cache target failed, worker:%u, index:%u, ret:%d.\n", worker, i, ret);
                return;
            }
            cacheKeys.emplace_back(item);
            workerKeys[worker].emplace_back(item);
        }
    }
    auto prepareCacheEnd = std::chrono::steady_clock::now();

    for (auto &keysForWorker : workerKeys) {
        SpreadBatchGetMixKeys(keysForWorker);
    }

    auto warmupStart = std::chrono::steady_clock::now();
    for (uint32_t worker = 0; worker < concurrency; ++worker) {
        double getUs = 0;
        double freeUs = 0;
        std::vector<BatchGetAlignedBuffer> buffers;
        BatchGetMixRunStats warmupStats;
        auto ret = RunBatchGetMixOnce(workerKeys[worker], bs, true, buffers, getUs, freeUs, &warmupStats);
        if (ret != RET_CACHE_OK) {
            mPrintOp("BatchGet mix warmup failed, worker:%u, ret:%d, failed_items:%llu, first_item_ret:%d, "
                     "short_reads:%llu.\n",
                worker, ret, static_cast<unsigned long long>(warmupStats.failedItems), warmupStats.firstItemRet,
                static_cast<unsigned long long>(warmupStats.shortReads));
            return;
        }
    }
    auto warmupEnd = std::chrono::steady_clock::now();
    mPrintOp("BatchGet mix perf warmup success, disk_verified:%u, disk_verify:%s, cache_disk_check:skipped.\n",
        verifiedDiskKeys, diskVerifySkipped ? "skipped" : "verified");

    sem_t startSem;
    if (sem_init(&startSem, 0, 0) != 0) {
        mPrintOp("BatchGet mix init semaphore failed, errno:%d.\n", errno);
        return;
    }
    std::vector<BatchGetMixThreadResult> results(concurrency);
    std::vector<std::thread> threads;
    threads.reserve(concurrency);
    auto hitBefore = TakeBatchGetMixHitSnapshot();
    auto start = std::chrono::steady_clock::now();
    for (uint32_t worker = 0; worker < concurrency; ++worker) {
        threads.emplace_back(BatchGetMixPerfThread, &workerKeys[worker], bs, rounds, &startSem, &results[worker]);
    }
    for (uint32_t worker = 0; worker < concurrency; ++worker) {
        (void)sem_post(&startSem);
    }
    for (auto &thread : threads) {
        thread.join();
    }
    auto end = std::chrono::steady_clock::now();
    auto hitAfter = TakeBatchGetMixHitSnapshot();
    auto hitDelta = DiffBatchGetMixHitSnapshot(hitBefore, hitAfter);
    sem_destroy(&startSem);

    double totalGetUs = 0;
    double totalFreeUs = 0;
    uint32_t totalBatches = 0;
    uint64_t failedItems = 0;
    uint64_t shortReads = 0;
    int32_t firstItemRet = RET_CACHE_OK;
    for (uint32_t worker = 0; worker < concurrency; ++worker) {
        if (results[worker].ret != RET_CACHE_OK) {
            mPrintOp("BatchGet mix worker failed, worker:%u, ret:%d, finished_batches:%u, failed_items:%llu, "
                     "first_item_ret:%d, short_reads:%llu.\n",
                worker, results[worker].ret, results[worker].batches,
                static_cast<unsigned long long>(results[worker].failedItems), results[worker].firstItemRet,
                static_cast<unsigned long long>(results[worker].shortReads));
            return;
        }
        totalGetUs += results[worker].totalGetUs;
        totalFreeUs += results[worker].totalFreeUs;
        totalBatches += results[worker].batches;
        failedItems += results[worker].failedItems;
        shortReads += results[worker].shortReads;
        if (firstItemRet == RET_CACHE_OK) {
            firstItemRet = results[worker].firstItemRet;
        }
    }
    double wallUs = std::chrono::duration<double, std::micro>(end - start).count();
    double dataMbPerBatch = static_cast<double>(bs) * batchNum / bytesPerMb;
    double totalDataMb = dataMbPerBatch * totalBatches;
    double avgGetUs = totalBatches == 0 ? 0 : totalGetUs / totalBatches;
    double avgFreeUs = totalBatches == 0 ? 0 : totalFreeUs / totalBatches;
    double avgE2eUs = avgGetUs + avgFreeUs;
    double actualHitPercent = batchNum == 0 ? 0 : static_cast<double>(cacheKeysPerBatch) * 100.0 / batchNum;
    double prepareDiskUs = std::chrono::duration<double, std::micro>(prepareDiskEnd - prepareDiskStart).count();
    double fillerUs = std::chrono::duration<double, std::micro>(fillerEnd - fillerStart).count();
    double evictWaitUs = std::chrono::duration<double, std::micro>(evictWaitEnd - evictWaitStart).count();
    double prepareCacheUs = std::chrono::duration<double, std::micro>(prepareCacheEnd - prepareCacheStart).count();
    double warmupUs = std::chrono::duration<double, std::micro>(warmupEnd - warmupStart).count();
    mPrintOp("BatchGet mix perf result bs:%u batch:%u concurrency:%u hit_percent:%u actual_hit_percent:%.2f "
             "cache_keys_per_batch:%u disk_keys_per_batch:%u rounds:%u fill_MB:%llu total_batches:%u "
             "total_data_MB:%.2f disk_verified:%u disk_verify:%s cache_disk_check:skipped avg_get_us:%.2f "
             "avg_free_us:%.2f "
             "avg_e2e_us:%.2f wall_us:%.2f e2e_bw_MBps:%.2f key_IOPS:%.2f.\n",
        bs, batchNum, concurrency, hitPercent, actualHitPercent, cacheKeysPerBatch, diskKeysPerBatch, rounds,
        static_cast<unsigned long long>(fillMb), totalBatches, totalDataMb, verifiedDiskKeys,
        diskVerifySkipped ? "skipped" : "verified", avgGetUs, avgFreeUs, avgE2eUs, wallUs,
        totalDataMb * 1000000.0 / wallUs,
        static_cast<double>(batchNum) * totalBatches * 1000000.0 / wallUs);
    mPrintOp("BatchGet mix result_detail failed_items:%llu first_item_ret:%d short_reads:%llu.\n",
        static_cast<unsigned long long>(failedItems), firstItemRet, static_cast<unsigned long long>(shortReads));
    mPrintOp("BatchGet mix phase_us prepare_disk:%.2f filler:%.2f evict_wait:%.2f prepare_cache:%.2f "
             "warmup:%.2f run_wall:%.2f.\n",
        prepareDiskUs, fillerUs, evictWaitUs, prepareCacheUs, warmupUs, wallUs);
    if (hitDelta.valid) {
        mPrintOp("BatchGet mix observed_hit query_total:%llu wcache_total:%llu wcache_hit:%llu wcache_mem:%llu "
                 "wcache_disk:%llu rcache_total:%llu rcache_hit:%llu rcache_mem:%llu rcache_disk:%llu "
                 "backend:%llu mem_hit_percent:%.2f disk_hit_percent:%.2f wcache_hit_percent:%.2f "
                 "backend_percent:%.2f.\n",
            static_cast<unsigned long long>(hitDelta.wCacheTotalCount),
            static_cast<unsigned long long>(hitDelta.wCacheTotalCount),
            static_cast<unsigned long long>(hitDelta.wCacheHitCount),
            static_cast<unsigned long long>(hitDelta.wCacheHitMemCount),
            static_cast<unsigned long long>(hitDelta.wCacheHitDiskCount),
            static_cast<unsigned long long>(hitDelta.rCacheTotalCount),
            static_cast<unsigned long long>(hitDelta.rCacheHitCount),
            static_cast<unsigned long long>(hitDelta.rCacheHitMemCount),
            static_cast<unsigned long long>(hitDelta.rCacheHitDiskCount),
            static_cast<unsigned long long>(hitDelta.backendHitCount),
            BatchGetMixRatio(hitDelta.wCacheHitMemCount, hitDelta.wCacheTotalCount),
            BatchGetMixRatio(hitDelta.wCacheHitDiskCount, hitDelta.wCacheTotalCount),
            BatchGetMixRatio(hitDelta.wCacheHitCount, hitDelta.wCacheTotalCount),
            BatchGetMixRatio(hitDelta.backendHitCount, hitDelta.wCacheTotalCount));
    } else {
        mPrintOp("BatchGet mix observed_hit unavailable.\n");
    }
}

void diagnose::BioSdkCommand::HandlePerf(const std::vector<std::string> &cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("invalid input.\n");
            return;
        }
    }
    if (std::stoul(cmds[2]) == 0) {
        mPrintOp("Invalid param, bs:%s\n", cmds[2].c_str());
        return;
    }
    if (gTenantId == UINT64_MAX) {
        mPrintOp("Create and open a cache first!\n");
        return;
    }

    uint32_t bs = 0;
    uint32_t ioDepth = 0;
    uint64_t size = 0;
    auto rw = cmds[1].c_str();
    try {
        bs = (std::stoul(cmds[2]) * 1024);
        ioDepth = std::stoul(cmds[3]);
        size = (std::stoul(cmds[4]) * 1024 * 1024);
    } catch (std::exception e) {
        mPrintOp("invalid input.\n");
        return;
    }
    auto count = size / bs;

    perfTestRunner runner = nullptr;
    if (memcmp(rw, "read", sizeof("read")) == 0) {
        runner = PerfTestGetImpl;
    } else if (memcmp(rw, "write", sizeof("write")) == 0) {
        runner = PerfTestPutImpl;
    } else {
        mPrintOp("Invalid rw type:%s.\n", rw);
        return;
    }

    mPrintOp("Perf test start, rw:%s, bs:%u, ioDepth:%u, size:%u, count:%u.\n", rw, bs, ioDepth, size, count);
    auto *th = (pthread_t *)malloc(sizeof(pthread_t) * ioDepth);
    auto *param = (PerfTestParam *)malloc(sizeof(PerfTestParam) * ioDepth);
    if (th == nullptr || param == nullptr) {
        mPrintOp("Malloc memory failed.\n");
        return;
    }
    for (uint32_t i = 0; i < ioDepth; i++) {
        param[i].done = false;
        param[i].tid = i;
        param[i].result = RET_CACHE_OK;
        sem_init(&param[i].sem, 0, 0);
        param[i].length = bs;
        param[i].count = count;
    }

    struct timeval startT, stopT;
    gettimeofday(&startT, nullptr);
    for (uint32_t i = 0; i < ioDepth; i++) {
        int ret = pthread_create(&th[i], nullptr, runner, &param[i]);
        if (ret != 0) {
            mPrintOp("Perf test create pthread failed, ret:%d.\n", ret);
            free(param);
            free(th);
            return;
        }
    }

    for (uint32_t j = 0; j < ioDepth; j++) {
        while (!param[j].done) {
            sem_wait(&param[j].sem);
            j = 0;
        }
    }

    gettimeofday(&stopT, nullptr);
    for (uint32_t k = 0; k < ioDepth; k++) {
        if (param[k].result != 0) {
            mPrintOp("Perf test return failed, tid:%u, ret:%d.\n", k, param[k].result);
            free(param);
            free(th);
            return;
        }
    }

    float cost_sec = stopT.tv_sec - startT.tv_sec;
    float cost_usec = stopT.tv_usec - startT.tv_usec;
    float time_use = cost_sec * 1000000U + cost_usec;
    auto totalCount = static_cast<double>(count * ioDepth) ;
    auto totalSize = static_cast<double>(count * bs);
    double dataPerf = static_cast<double>(((totalSize / 1048576U) * 1000000U / time_use) * ioDepth);
    double iops = static_cast<double>(totalCount * 1000000U) / time_use;
    int bwFactor = 1;

    time_t rawtime;
    struct tm *timeinfo = nullptr;
    struct tm timebuf{};
    rawtime = time(nullptr);
    timeinfo = localtime_r(&rawtime, &timebuf);
    mPrintOp("Perf Test Result: @ %s\n", asctime(timeinfo));
    mPrintOp("  IO depth                   : %lu\n", ioDepth);
    mPrintOp("  IO size                    : %lu\n", bs);
    mPrintOp("  total IO count             : %d\n", (int)totalCount);
    mPrintOp("  total spent                : %.2f ms\n", time_use / 1000U);
    mPrintOp("  throughput                 : %.4f MB/s\n", dataPerf * bwFactor);
    mPrintOp("  IOPS                       : %.2f /s\n", iops);
    mPrintOp("  latency                    : %.2f (us)\n", time_use / count);
    mPrintOp("Perf Test End.\n");

    free(param);
    free(th);
}

void diagnose::BioSdkCommand::BioSdkDebugHelp(char *command, int detail) noexcept
{
    mPrintOp("\tlist caches: sdk list\n");
    mPrintOp("\tcreate cache: sdk create [tenantId] [affinity] [strategy]\n");
    mPrintOp("\topen cache: sdk open [tenantId]\n");
    mPrintOp("\tdestroy cache: sdk destroy [tenantId]\n");
    mPrintOp("\tshow flow: sdk show flow [ptId]\n");
    mPrintOp("\tput value: sdk put [key] [filePath] [length] [sliceId]\n");
    mPrintOp("\tget value: sdk get [key] [offset] [length] [location] [filePath]\n");
    mPrintOp("\tstate object: sdk stat [key] [location]\n");
    mPrintOp("\texist object: sdk exist [key] [location]\n");
    mPrintOp("\tlist all object: sdk listall [prefix]\n");
    mPrintOp("\tload object: sdk load [key] [offset] [length] [location]\n");
    mPrintOp("\tdelete object: sdk delete [key] [location]\n");
    mPrintOp("\tshow view: sdk show [pt/node] [all/affinity]\n");
    mPrintOp("\ttrace: sdk trace [show/clear]\n");
    mPrintOp("\tCache hit: sdk cachehit\n");
    mPrintOp("\tAdd disk: sdk adddisk [diskPath]\n");
    mPrintOp("\tCache resource: sdk cacheresource\n");
    mPrintOp("\tperf test: sdk perf [rw] [bs(Kb)] [ioDepth] [size(Mb)]\n");
    mPrintOp("\tperf test: sdk batchget [bs(Kb)] [batchNUM]\n");
    mPrintOp("\tBDM batch get perf: sdk batchgetperf [bs(Kb)] [batchNUM] [rounds] [fill(Mb)]\n");
    mPrintOp("\tMixed BatchGet perf: sdk batchgetmixperf [bs(Kb)] [batchNUM] [concurrency] [hitPercent] "
             "[rounds] [fill(Mb)]\n");
    mPrintOp("\tupdate prepare: sdk notifyupdate [tenantId]\n");
    mPrintOp("\tupdate check: sdk checkupdate [tenantId]\n");
    mPrintOp("\tupdate finish: sdk finishupdate [tenantId]\n");
    mPrintOp("\texit: exit console\n");
}

void diagnose::BioSdkCommand::BioSdkDebugProcess(int argc, char *argv[]) noexcept
{
    if (argc <= 1) {
        BioSdkDebugHelp(argv[0], 1);
        return;
    }

    std::vector<std::string> cmds;
    for (int i = 1; i < argc; i++) {
        std::string str(argv[i]);
        cmds.emplace_back(str);
    }

    std::string cmdType = cmds[0];
    if (cmdType == "list") {
        HandleListCache();
    }  else if (cmdType == "create") {
        if (cmds.size() != 4) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleCreate(cmds);
    } else if (cmdType == "open") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleOpen(cmds);
    } else if (cmdType == "destroy") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleDestroy(cmds);
    } else if (cmdType == "put") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 5) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandlePut(cmds);
    } else if (cmdType == "get") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 6) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleGet(cmds);
    } else if (cmdType == "stat") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleStat(cmds);
    } else if (cmdType == "exist") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleExist(cmds);
    }  else if (cmdType == "listall") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleList(cmds);
    } else if (cmdType == "load") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 5) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleLoad(cmds);
    } else if (cmdType == "delete") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleDelete(cmds);
    } else if (cmdType == "adddisk") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleAddDisk(cmds);
    } else if (cmdType == "show") {
        if (cmds.size() < 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleShow(cmds);
    } else if (cmdType == "trace") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleSdkTrace(cmds);
    } else if (cmdType == "perf") {
        if (cmds.size() != 5) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandlePerf(cmds);
    } else if (cmdType == "batchget") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleBatchGet(cmds);
    } else if (cmdType == "batchgetperf") {
        if (cmds.size() != 5) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleBatchGetPerf(cmds);
    } else if (cmdType == "batchgetmixperf") {
        if (cmds.size() != 7) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleBatchGetMixPerf(cmds);
    } else if (cmdType == "notifyupdate") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleNotifyUpdatePrepare(cmds);
    } else if (cmdType == "finishupdate") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleNotifyUpdateFinish(cmds);
    } else if (cmdType == "checkupdate") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleCheckUpdateReady(cmds);
    } else if (cmdType == "cachehit") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 1) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleShowCacheHit(cmds);
    } else if (cmdType == "cacheresource") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 1) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleShowCacheResource(cmds);
    } else if (cmdType == "exit") {
        return;
    } else {
        BioSdkDebugHelp(argv[0], 1);
    }
}
