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

#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <new>
#include <semaphore.h>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <vector>
#include "htracer.h"
#include "bio_client.h"
#include "bio_config_instance.h"
#include "bio_log.h"
#include "bdm_core.h"
#include "bio_server.h"
#include "bio_functions.h"
#include "cache_overload_ctrl.h"
#include "wcache_statistic.h"
#include "server_diagnose.h"

using namespace ock::bio;

namespace {
constexpr uint64_t BDM_BENCH_KB = 1024ULL;
constexpr uint64_t BDM_BENCH_MB = 1024ULL * 1024ULL;
constexpr uint64_t BDM_BENCH_NS_PER_US = 1000ULL;
constexpr uint64_t BDM_BENCH_NS_PER_SEC = 1000000000ULL;
constexpr uint64_t BDM_BENCH_BDM_ID_SHIFT = 48ULL;
constexpr uint64_t BDM_BENCH_BDM_ID_MASK = 0xFFFFULL;

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

uint64_t BdmBenchNowNs()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

const char *BdmBenchEngineName()
{
    switch (BdmGetIoEngine()) {
        case BDM_IO_ENGINE_IO_URING:
            return "io_uring";
        case BDM_IO_ENGINE_SYNC:
            return "sync";
        default:
            return "unknown";
    }
}

uint32_t BdmBenchChunkBdmId(uint64_t chunkId)
{
    return static_cast<uint32_t>((chunkId >> BDM_BENCH_BDM_ID_SHIFT) & BDM_BENCH_BDM_ID_MASK);
}

bool BdmBenchDropCaches()
{
    sync();
    int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
    if (fd < 0) {
        return false;
    }

    const char dropCmd[] = "3\n";
    ssize_t ret = write(fd, dropCmd, sizeof(dropCmd) - 1);
    close(fd);
    return ret == static_cast<ssize_t>(sizeof(dropCmd) - 1);
}

struct BdmBenchRequest {
    BdmIoCtx ioCtx = {};
    sem_t sem = {};
    char *buf = nullptr;
    uint64_t startNs = 0;
    uint64_t index = 0;
    int32_t ret = BDM_CODE_ERR;
    std::atomic<bool> inFlight { false };
    std::atomic<bool> done { false };
    sem_t *completionSem = nullptr;
    std::vector<uint64_t> *latencies = nullptr;
};

struct BdmBenchResult {
    uint64_t elapsedNs = 0;
    uint64_t ioCount = 0;
    uint64_t bytes = 0;
    uint64_t errors = 0;
    double mbps = 0;
    double iops = 0;
    double avgUs = 0;
    double p50Us = 0;
    double p99Us = 0;
};

void BdmBenchCallback(void *ctx, int32_t ret)
{
    auto *req = static_cast<BdmBenchRequest *>(ctx);
    if (req == nullptr) {
        return;
    }

    req->ret = ret;
    if (req->latencies != nullptr && req->index < req->latencies->size()) {
        (*req->latencies)[req->index] = BdmBenchNowNs() - req->startNs;
    }
    req->done.store(true, std::memory_order_release);
    sem_post(&req->sem);
    if (req->completionSem != nullptr) {
        sem_post(req->completionSem);
    }
}

void BdmBenchWaitRequest(BdmBenchRequest &req, uint64_t &errors)
{
    while (!req.done.load(std::memory_order_acquire)) {
        if (sem_wait(&req.sem) == 0 || errno == EINTR) {
            continue;
        }
        req.ret = BDM_CODE_ERR;
        req.done.store(true, std::memory_order_release);
        break;
    }

    if (req.ret != BDM_CODE_OK) {
        errors++;
    }
    req.inFlight.store(false, std::memory_order_release);
}

uint32_t BdmBenchCollectCompleted(BdmBenchRequest *reqs, uint32_t ioDepth, uint64_t &errors)
{
    uint32_t completed = 0;
    for (uint32_t i = 0; i < ioDepth; i++) {
        BdmBenchRequest &req = reqs[i];
        if (!req.inFlight.load(std::memory_order_acquire) || !req.done.load(std::memory_order_acquire)) {
            continue;
        }
        if (req.ret != BDM_CODE_OK) {
            errors++;
        }
        req.inFlight.store(false, std::memory_order_release);
        completed++;
    }
    return completed;
}

uint32_t BdmBenchCollectFreeSlots(BdmBenchRequest *reqs, uint32_t ioDepth, uint32_t batchSize,
    std::vector<uint32_t> &slots)
{
    slots.clear();
    slots.reserve(batchSize);
    for (uint32_t i = 0; i < ioDepth; i++) {
        if (!reqs[i].inFlight.load(std::memory_order_acquire)) {
            slots.emplace_back(i);
            if (slots.size() == batchSize) {
                break;
            }
        }
    }
    return static_cast<uint32_t>(slots.size());
}

void BdmBenchCalcStats(BdmBenchResult &result, const std::vector<uint64_t> &latencies)
{
    std::vector<uint64_t> sorted;
    sorted.reserve(latencies.size());
    for (auto latency : latencies) {
        if (latency != 0) {
            sorted.emplace_back(latency);
        }
    }
    if (sorted.empty()) {
        return;
    }

    std::sort(sorted.begin(), sorted.end());
    uint64_t totalLatency = 0;
    for (auto latency : sorted) {
        totalLatency += latency;
    }

    result.avgUs = static_cast<double>(totalLatency) / sorted.size() / BDM_BENCH_NS_PER_US;
    result.p50Us = static_cast<double>(sorted[sorted.size() / 2]) / BDM_BENCH_NS_PER_US;
    size_t p99Index = (sorted.size() * 99) / 100;
    if (p99Index >= sorted.size()) {
        p99Index = sorted.size() - 1;
    }
    result.p99Us = static_cast<double>(sorted[p99Index]) / BDM_BENCH_NS_PER_US;
}

int32_t BdmBenchSubmitOne(const std::vector<uint64_t> &chunks, uint64_t bs, uint64_t chunkSize, bool isRead,
    uint64_t index, BdmBenchRequest &req)
{
    uint64_t byteOffset = index * bs;
    uint64_t chunkIndex = byteOffset / chunkSize;
    uint64_t chunkOffset = byteOffset % chunkSize;
    req.index = index;
    req.ret = BDM_CODE_ERR;
    req.done.store(false, std::memory_order_release);
    req.inFlight.store(true, std::memory_order_release);
    req.ioCtx.cb = BdmBenchCallback;
    req.ioCtx.ctx = &req;
    req.startNs = BdmBenchNowNs();

    return isRead ? BdmReadAsync(chunks[chunkIndex], chunkOffset, req.buf, bs, &req.ioCtx) :
                    BdmWriteAsync(chunks[chunkIndex], chunkOffset, req.buf, bs, &req.ioCtx);
}

int32_t BdmBenchSubmitBatchSlots(const std::vector<uint64_t> &chunks, uint64_t bs, uint64_t chunkSize, bool isRead,
    uint64_t startIndex, uint32_t reqNum, BdmBenchRequest *reqs, const std::vector<uint32_t> &slots,
    std::vector<BdmBatchIo> &ios)
{
    ios.clear();
    ios.reserve(reqNum);
    for (uint32_t i = 0; i < reqNum; i++) {
        uint64_t index = startIndex + i;
        uint64_t byteOffset = index * bs;
        uint64_t chunkIndex = byteOffset / chunkSize;
        uint64_t chunkOffset = byteOffset % chunkSize;
        BdmBenchRequest &req = reqs[slots[i]];
        req.index = index;
        req.ret = BDM_CODE_ERR;
        req.done.store(false, std::memory_order_release);
        req.inFlight.store(true, std::memory_order_release);
        req.ioCtx.cb = BdmBenchCallback;
        req.ioCtx.ctx = &req;
        req.startNs = BdmBenchNowNs();

        BdmBatchIo io = { chunks[chunkIndex], chunkOffset, req.buf, bs, &req.ioCtx };
        ios.emplace_back(io);
    }

    return isRead ? BdmReadBatchAsync(ios.data(), reqNum) : BdmWriteBatchAsync(ios.data(), reqNum);
}

void BdmBenchCompleteSubmitErrorSlots(BdmBenchRequest *reqs, const std::vector<uint32_t> &slots, uint32_t reqNum,
    int32_t ret)
{
    for (uint32_t i = 0; i < reqNum; i++) {
        BdmBenchRequest &req = reqs[slots[i]];
        req.ret = ret;
        req.done.store(true, std::memory_order_release);
    }
}

int32_t BdmBenchRunAsyncIo(const std::vector<uint64_t> &chunks, uint64_t bs, uint64_t totalBytes, uint64_t chunkSize,
    uint32_t ioDepth, bool isRead, bool collectStats, BdmBenchResult &result)
{
    if (chunks.empty() || ioDepth == 0) {
        return BDM_CODE_INVALID_PARAM;
    }

    uint64_t ioCount = totalBytes / bs;
    std::vector<uint64_t> latencies(collectStats ? ioCount : 0);
    std::unique_ptr<BdmBenchRequest[]> reqs(new (std::nothrow) BdmBenchRequest[ioDepth]);
    if (reqs == nullptr) {
        return BDM_CODE_ERR;
    }

    uint32_t initedReqNum = 0;
    for (uint32_t i = 0; i < ioDepth; i++) {
        if (posix_memalign(reinterpret_cast<void **>(&reqs[i].buf), 4096, bs) != 0) {
            for (uint32_t j = 0; j < initedReqNum; j++) {
                sem_destroy(&reqs[j].sem);
                free(reqs[j].buf);
            }
            return BDM_CODE_ERR;
        }
        memset(reqs[i].buf, isRead ? 0 : 0x5a, bs);
        reqs[i].latencies = collectStats ? &latencies : nullptr;
        if (sem_init(&reqs[i].sem, 0, 0) != 0) {
            free(reqs[i].buf);
            for (uint32_t j = 0; j < initedReqNum; j++) {
                sem_destroy(&reqs[j].sem);
                free(reqs[j].buf);
            }
            return BDM_CODE_ERR;
        }
        initedReqNum++;
    }

    uint64_t errors = 0;
    uint64_t startNs = BdmBenchNowNs();
    for (uint64_t i = 0; i < ioCount; i++) {
        BdmBenchRequest &req = reqs[i % ioDepth];
        if (req.inFlight.load(std::memory_order_acquire)) {
            BdmBenchWaitRequest(req, errors);
        }

        int32_t ret = BdmBenchSubmitOne(chunks, bs, chunkSize, isRead, i, req);
        if (ret != BDM_CODE_OK) {
            req.ret = ret;
            req.done.store(true, std::memory_order_release);
            req.inFlight.store(false, std::memory_order_release);
            errors++;
        }
    }

    for (uint32_t i = 0; i < ioDepth; i++) {
        if (reqs[i].inFlight.load(std::memory_order_acquire)) {
            BdmBenchWaitRequest(reqs[i], errors);
        }
    }
    uint64_t endNs = BdmBenchNowNs();

    for (uint32_t i = 0; i < ioDepth; i++) {
        sem_destroy(&reqs[i].sem);
        free(reqs[i].buf);
    }

    result.elapsedNs = endNs - startNs;
    result.ioCount = ioCount;
    result.bytes = totalBytes;
    result.errors = errors;
    if (result.elapsedNs != 0) {
        result.mbps = (static_cast<double>(totalBytes) / BDM_BENCH_MB) * BDM_BENCH_NS_PER_SEC / result.elapsedNs;
        result.iops = static_cast<double>(ioCount) * BDM_BENCH_NS_PER_SEC / result.elapsedNs;
    }
    if (collectStats) {
        BdmBenchCalcStats(result, latencies);
    }
    return errors == 0 ? BDM_CODE_OK : BDM_CODE_ERR;
}

int32_t BdmBenchRunBatchIo(const std::vector<uint64_t> &chunks, uint64_t bs, uint64_t totalBytes, uint64_t chunkSize,
    uint32_t ioDepth, uint32_t batchSize, bool isRead, bool collectStats, BdmBenchResult &result)
{
    if (chunks.empty() || ioDepth == 0 || batchSize == 0 || batchSize > ioDepth) {
        return BDM_CODE_INVALID_PARAM;
    }

    uint64_t ioCount = totalBytes / bs;
    std::vector<uint64_t> latencies(collectStats ? ioCount : 0);
    std::unique_ptr<BdmBenchRequest[]> reqs(new (std::nothrow) BdmBenchRequest[ioDepth]);
    if (reqs == nullptr) {
        return BDM_CODE_ERR;
    }

    sem_t completionSem = {};
    if (sem_init(&completionSem, 0, 0) != 0) {
        return BDM_CODE_ERR;
    }
    uint32_t initedReqNum = 0;
    for (uint32_t i = 0; i < ioDepth; i++) {
        if (posix_memalign(reinterpret_cast<void **>(&reqs[i].buf), 4096, bs) != 0) {
            for (uint32_t j = 0; j < initedReqNum; j++) {
                sem_destroy(&reqs[j].sem);
                free(reqs[j].buf);
            }
            sem_destroy(&completionSem);
            return BDM_CODE_ERR;
        }
        memset(reqs[i].buf, isRead ? 0 : 0x5a, bs);
        reqs[i].latencies = collectStats ? &latencies : nullptr;
        reqs[i].completionSem = &completionSem;
        if (sem_init(&reqs[i].sem, 0, 0) != 0) {
            free(reqs[i].buf);
            for (uint32_t j = 0; j < initedReqNum; j++) {
                sem_destroy(&reqs[j].sem);
                free(reqs[j].buf);
            }
            sem_destroy(&completionSem);
            return BDM_CODE_ERR;
        }
        initedReqNum++;
    }

    uint64_t errors = 0;
    std::vector<BdmBatchIo> ios;
    std::vector<uint32_t> slots;
    uint64_t startNs = BdmBenchNowNs();
    uint64_t nextSubmit = 0;
    uint64_t completed = 0;
    uint32_t inFlight = 0;
    while (completed < ioCount) {
        while (nextSubmit < ioCount && inFlight < ioDepth) {
            uint32_t reqNum = BdmBenchCollectFreeSlots(reqs.get(), ioDepth, batchSize, slots);
            if (reqNum == 0) {
                break;
            }
            uint64_t ioLeft = ioCount - nextSubmit;
            if (reqNum < batchSize && ioLeft > reqNum) {
                break;
            }
            reqNum = static_cast<uint32_t>(std::min<uint64_t>(reqNum, ioLeft));

            int32_t ret =
                BdmBenchSubmitBatchSlots(chunks, bs, chunkSize, isRead, nextSubmit, reqNum, reqs.get(), slots, ios);
            if (ret != BDM_CODE_OK) {
                BdmBenchCompleteSubmitErrorSlots(reqs.get(), slots, reqNum, ret);
            }
            nextSubmit += reqNum;
            inFlight += reqNum;
        }

        if (completed < nextSubmit) {
            uint32_t completedNow = 0;
            while ((completedNow = BdmBenchCollectCompleted(reqs.get(), ioDepth, errors)) == 0) {
                if (sem_wait(&completionSem) == 0 || errno == EINTR) {
                    continue;
                }
                break;
            }
            completed += completedNow;
            inFlight -= completedNow;
        }
    }
    uint64_t endNs = BdmBenchNowNs();

    for (uint32_t i = 0; i < ioDepth; i++) {
        sem_destroy(&reqs[i].sem);
        free(reqs[i].buf);
    }
    sem_destroy(&completionSem);

    result.elapsedNs = endNs - startNs;
    result.ioCount = ioCount;
    result.bytes = totalBytes;
    result.errors = errors;
    if (result.elapsedNs != 0) {
        result.mbps = (static_cast<double>(totalBytes) / BDM_BENCH_MB) * BDM_BENCH_NS_PER_SEC / result.elapsedNs;
        result.iops = static_cast<double>(ioCount) * BDM_BENCH_NS_PER_SEC / result.elapsedNs;
    }
    if (collectStats) {
        BdmBenchCalcStats(result, latencies);
    }
    return errors == 0 ? BDM_CODE_OK : BDM_CODE_ERR;
}

int32_t BdmBenchRunIo(const std::vector<uint64_t> &chunks, uint64_t bs, uint64_t totalBytes, uint64_t chunkSize,
    uint32_t ioDepth, bool isRead, bool collectStats, bool useBatch, uint32_t batchSize, BdmBenchResult &result)
{
    if (useBatch) {
        return BdmBenchRunBatchIo(chunks, bs, totalBytes, chunkSize, ioDepth, batchSize, isRead, collectStats, result);
    }
    return BdmBenchRunAsyncIo(chunks, bs, totalBytes, chunkSize, ioDepth, isRead, collectStats, result);
}

std::vector<uint64_t> BdmVerifyLengths()
{
    return { 4097ULL, 12345ULL, BDM_BENCH_MB + 1ULL, 4ULL * BDM_BENCH_MB - 1ULL,
        4ULL * BDM_BENCH_MB + 1ULL, 8ULL * BDM_BENCH_MB - 1ULL };
}

std::vector<uint32_t> BdmVerifyBatches()
{
    return { 1U, 32U, 600U, 16U * 1024U };
}

std::vector<uint64_t> BdmVerifyOffsets()
{
    return { 0ULL, 1ULL, 123ULL, 4097ULL };
}

template <typename T>
bool BdmVerifyParseList(const std::vector<std::string> &cmds, size_t index, const std::vector<T> &defaults,
    std::vector<T> &values)
{
    if (cmds.size() <= index || cmds[index] == "all") {
        values = defaults;
        return true;
    }
    if (!IsUnsignedInteger(cmds[index])) {
        return false;
    }
    try {
        values = { static_cast<T>(std::stoull(cmds[index])) };
    } catch (std::exception &) {
        return false;
    }
    return true;
}

void BdmVerifyFill(char *buf, uint64_t len, uint64_t seed)
{
    for (uint64_t i = 0; i < len; i++) {
        buf[i] = static_cast<char>((i * 131ULL + seed * 17ULL + 0x5aULL) & 0xffULL);
    }
}

int32_t BdmVerifyWait(BdmBenchRequest *reqs, uint32_t count, sem_t &completionSem, uint64_t &errors)
{
    uint32_t completed = 0;
    while (completed < count) {
        uint32_t completedNow = 0;
        while ((completedNow = BdmBenchCollectCompleted(reqs, count, errors)) == 0) {
            if (sem_wait(&completionSem) == 0 || errno == EINTR) {
                continue;
            }
            return BDM_CODE_ERR;
        }
        completed += completedNow;
    }
    return errors == 0 ? BDM_CODE_OK : BDM_CODE_ERR;
}

int32_t BdmVerifyBatchRead(uint64_t chunkId, uint64_t offset, uint64_t len, uint32_t batch, char *expected,
    uint32_t &bufferSlots, uint64_t &errors)
{
    constexpr uint64_t maxVerifyBufferBytes = 512ULL * BDM_BENCH_MB;
    bufferSlots = static_cast<uint32_t>(std::max<uint64_t>(1ULL, maxVerifyBufferBytes / len));
    bufferSlots = std::min(bufferSlots, batch);

    std::vector<char *> buffers(bufferSlots, nullptr);
    for (uint32_t i = 0; i < bufferSlots; i++) {
        if (posix_memalign(reinterpret_cast<void **>(&buffers[i]), 4096, len) != 0) {
            for (auto *buf : buffers) {
                free(buf);
            }
            return BDM_CODE_ERR;
        }
        memset(buffers[i], 0, len);
    }

    std::vector<BdmBenchRequest> reqs(batch);
    std::vector<BdmBatchIo> ios(batch);
    sem_t completionSem = {};
    if (sem_init(&completionSem, 0, 0) != 0) {
        for (auto *buf : buffers) {
            free(buf);
        }
        return BDM_CODE_ERR;
    }

    uint32_t initedReqs = 0;
    for (uint32_t i = 0; i < batch; i++) {
        BdmBenchRequest &req = reqs[i];
        req.buf = buffers[i % bufferSlots];
        req.ret = BDM_CODE_ERR;
        req.done.store(false, std::memory_order_release);
        req.inFlight.store(true, std::memory_order_release);
        req.completionSem = &completionSem;
        req.ioCtx.cb = BdmBenchCallback;
        req.ioCtx.ctx = &req;
        if (sem_init(&req.sem, 0, 0) != 0) {
            for (uint32_t j = 0; j < initedReqs; j++) {
                sem_destroy(&reqs[j].sem);
            }
            sem_destroy(&completionSem);
            for (auto *buf : buffers) {
                free(buf);
            }
            return BDM_CODE_ERR;
        }
        initedReqs++;
        ios[i] = { chunkId, offset, req.buf, len, &req.ioCtx };
    }

    int32_t ret = BdmReadBatchAsync(ios.data(), batch);
    int32_t waitRet = BdmVerifyWait(reqs.data(), batch, completionSem, errors);
    if (ret == BDM_CODE_OK) {
        ret = waitRet;
    }

    if (ret == BDM_CODE_OK) {
        for (uint32_t i = 0; i < bufferSlots; i++) {
            if (memcmp(buffers[i], expected, len) != 0) {
                errors++;
                ret = BDM_CODE_ERR;
                break;
            }
        }
    }

    for (uint32_t i = 0; i < initedReqs; i++) {
        sem_destroy(&reqs[i].sem);
    }
    sem_destroy(&completionSem);
    for (auto *buf : buffers) {
        free(buf);
    }
    return ret;
}
}

bool ock::bio::diagnose::BioServerCommand::mInited = false;
void* ock::bio::diagnose::BioServerCommand::mHandler = nullptr;
CliRegCmdFuncPtr ock::bio::diagnose::BioServerCommand::mRegOp = nullptr;
CliUnRegCmdFuncPtr ock::bio::diagnose::BioServerCommand::mUnRegOp = nullptr;
CliPrintBufFuncPtr ock::bio::diagnose::BioServerCommand::mPrintOp = nullptr;
CliSendBufFuncPtr ock::bio::diagnose::BioServerCommand::mSendOp = nullptr;

int32_t diagnose::BioServerCommand::LoadSymbols()
{
    const char* soFileName = "libcli_agent.so";
    mHandler = dlopen(soFileName, RTLD_NOW);
    if (mHandler == nullptr) {
        LOG_ERROR("Failed to open library() " << soFileName << " dlopen, error " << dlerror());
        return BIO_INNER_ERR;
    }

    mRegOp = reinterpret_cast<CliRegCmdFuncPtr>(dlsym(mHandler, "cli_register_command"));
    mUnRegOp = reinterpret_cast<CliUnRegCmdFuncPtr>(dlsym(mHandler, "cli_unregister_command"));
    mPrintOp = reinterpret_cast<CliPrintBufFuncPtr>(dlsym(mHandler, "cli_print_buffer"));
    mSendOp = reinterpret_cast<CliSendBufFuncPtr>(dlsym(mHandler, "cli_send_buffer"));
    if (mRegOp == nullptr || mUnRegOp == nullptr || mPrintOp == nullptr || mSendOp == nullptr) {
        LOG_ERROR("Failed to load function.");
        dlclose(mHandler);
        return BIO_INNER_ERR;
    }

    return BIO_OK;
}

void diagnose::BioServerCommand::PrintLongText(const std::string &text)
{
    if (text.empty()) {
        return;
    }
    mSendOp(text.c_str(), static_cast<uint32_t>(text.size()));
}

int diagnose::BioServerCommand::Initialize() noexcept
{
    if (mInited) {
        return 0;
    }

    auto ret = LoadSymbols();
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to load symbols.");
        return ret;
    }

    CliCommand command;
    strncpy(command.command, "bioServer", CLI_MAX_COMMAND_LEN);
    strncpy(command.description, "bioServer commands.", CLI_MAX_CMD_DESC_LEN);
    command.handler = BioServerDebugProcess;
    command.help_handler = BioServerDebugHelp;
    auto result = mRegOp(&command);
    if (result == 0) {
        mInited = true;
    }
    return result;
}

void diagnose::BioServerCommand::Destroy() noexcept
{
    if (mInited && mUnRegOp) {
        mUnRegOp((char *)"bioServer");
        mInited = false;
    }

    if (mHandler) {
        dlclose(mHandler);
        mHandler = nullptr;
    }
}

void diagnose::BioServerCommand::HandleModifyEvictWaterLevel(uint8_t tier, uint64_t level)
{
    auto ori = BioConfig::Instance()->ModifyConfigEvictWaterLevel(tier, level);
    mPrintOp("config changed tier:%u EvictWaterLevel, %lu => %lu\n", tier, ori, level);
}

void diagnose::BioServerCommand::HandleModifyMemReadWriteRatio(const std::string &ratios)
{
    auto ori = BioConfig::Instance()->ModifyConfigMemReadWriteRatio(ratios);
    mPrintOp("config changed: MemReadWriteRatio, %s => %s\n", ori.c_str(), ratios.c_str());
}

void diagnose::BioServerCommand::HandleModifyDiskReadWriteRatio(const std::string &ratios)
{
    auto ori = BioConfig::Instance()->ModifyConfigDiskReadWriteRatio(ratios);
    mPrintOp("config changed: MemReadWriteRatio, %s => %s\n", ori.c_str(), ratios.c_str());
}

void diagnose::BioServerCommand::BioServerHandleShow(const std::vector<std::string> &cmds)
{
    auto cType = cmds[1].c_str();
    std::string cmdType(cType);
    if (cmdType == "disk") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        auto &daemonConfig = BioConfig::Instance()->GetDaemonConfig();
        CmDiskStatus diskStatus;
        mPrintOp("Disk Info:\n");
        mPrintOp("id        name                status    totalCapacity       usedCapacity \n");
        for (uint32_t i = 0; i < daemonConfig.diskList.size(); i++) {
            if (BioServer::Instance()->GetDiskStatusFromNodeView(i, diskStatus) != BIO_OK) {
                continue;
            }
            if (diskStatus == CM_DISK_FAULT) {
                mPrintOp("%-10d%-20s%-10s \n", i, daemonConfig.diskList[i].c_str(), "fault");
                continue;
            }
            uint64_t totalCap = 0;
            uint64_t usedCap = 0;
            BdmGetCapacity(i, &totalCap, &usedCap);
            mPrintOp("%-10d%-20s%-10s%-20llu%-20llu \n",
                i, daemonConfig.diskList[i].c_str(), "normal", totalCap, usedCap);
        }
        return;
    } else if (cmdType == "net") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        std::string protoStr[4U] = { "RDMA", "TCP", "UDS", "SHM" };
        std::string modeStr[2U] = { "BUSY_POLLING", "EVENT_POLLING" };
        uint32_t executorNum = 0;
        NetOptions option;
        BioServer::Instance()->GetNetEngine()->Show(executorNum, option);
        mPrintOp("Boostio rpc info: \n");
        mPrintOp("  ip: %s:%u, protocol:%s, mode:%s, workers_count:%u, request_executor:%u, memory_size:%luGB\n",
            option.ipMask.c_str(), option.port, protoStr[option.protocol].c_str(),
            (option.isBusyLoop) ? modeStr[0].c_str() : modeStr[1].c_str(), option.handlerCount, executorNum,
            (option.memorySize / NO_1024 / NO_1024 / NO_1024));
    } else if (cmdType == "resources") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        CacheResDescription desc;
        Cache::Instance().GetCacheResources(desc, WRITE_CACHE);
        mPrintOp("WCACHE(MB): mem %lu used %lu disk %lu used %lu \n", desc.memCapacity / NO_1048576,
            desc.memUsedSize / NO_1048576, desc.diskCapacity / NO_1048576, desc.diskUsedSize / NO_1048576);
        Cache::Instance().GetCacheResources(desc, READ_CACHE);
        mPrintOp("RCACHE(MB): mem %lu used %lu disk %lu used %lu \n", desc.memCapacity / NO_1048576,
            desc.memUsedSize / NO_1048576, desc.diskCapacity / NO_1048576, desc.diskUsedSize / NO_1048576);
    } else if (cmdType == "pt") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint64_t curPtTimes;
        std::map<uint16_t, CmPtInfo> ptView = BioServer::Instance()->GetPtView(&curPtTimes);
        std::string output = "Pt view:\n";
        for (auto &ptEntry : ptView) {
            output += ptEntry.second.ToString();
            output += "\n";
        }
        PrintLongText(output);
    } else if (cmdType == "node") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint64_t curNodeTimes;
        std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioServer::Instance()->GetNodeView(&curNodeTimes);
        std::string output = "Node view:\n";
        for (auto &nodeEntry : nodeView) {
            output += nodeEntry.second.ToString();
            output += "\n";
        }
        output += "Local Node:";
        CmNodeId localNode = BioServer::Instance()->GetLocalNid();
        output += localNode.ToString();
        output += "\n";
        PrintLongText(output);
    } else if (cmdType == "olc") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint64_t vmVec = 0;
        uint64_t totalQuota = 0;
        uint64_t currentQuota = 0;
        std::unordered_map<QuotaHolder, uint64_t, QuotaHolderHash, QuotaHolderEqual> holders;
        CacheOverloadCtrl::Instance().Show(vmVec, totalQuota, currentQuota, holders);
        mPrintOp("  Boostio overload ctrl info: \n");
        mPrintOp("  Water level:%lu, Total quota:%lu, Remain Quota:%lu\n", vmVec, totalQuota, currentQuota);
        for (auto iter = holders.begin(); iter != holders.end(); iter++) {
            mPrintOp("  Holder %u-%lu: %lu \n", iter->first.nodeId, iter->first.clientId, iter->second);
        }
    } else if (cmdType == "existHit") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint64_t existTotol = WCacheStatistic::Instance().GetExistTotalCount();
        uint64_t existHit = WCacheStatistic::Instance().GetExistHitCount();
        if (existTotol == 0) {
            mPrintOp("Did not execute exist.\n");
            return;
        }
        mPrintOp("Exist times:%lu\n", existTotol);
        mPrintOp("Exist hit times:%lu\n", existHit);
        mPrintOp("Exist hit ratio:%2f%%\n", static_cast<float>(existHit) * NO_100 / static_cast<float>(existTotol));
    } else {
        mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
    }
}

void diagnose::BioServerCommand::HandleServerTrace(const std::vector<std::string> &cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "show") {
        auto info = ock::htracer::GetTraceInfo();
        if (cmds.size() == 3) {
            std::stringstream input(info);
            std::string line;
            bool isHeader = true;
            std::string output;
            while (std::getline(input, line)) {
                if (isHeader || line.find(cmds[2]) != std::string::npos) {
                    output += line;
                    output += "\n";
                }
                isHeader = false;
            }
            PrintLongText(output);
            return;
        }
        PrintLongText(info);
    } else if (viewType == "clear") {
        ock::htracer::ClearTraceInfo();
        mPrintOp("clearing statistics server records succeeded.\n");
    } else if (viewType == "open") {
        ock::htracer::HTracerSetEnable(true);
        mPrintOp("open statistics sdk records succeeded.\n");
    } else if (viewType == "close") {
        ock::htracer::HTracerSetEnable(false);
        mPrintOp("close statistics sdk records succeeded.\n");
    }
}

void diagnose::BioServerCommand::BioServerDebugHelp(char *command, int detail) noexcept
{
    mPrintOp("\tchange water level: bioServer chgwlv [tier] [water_level]\n");
    mPrintOp("\tchange memory read write ratio: bioServer chgmr [memory ratio]\n");
    mPrintOp("\tchange disk read write ratio: bioServer chgdr [disk ratio]\n");
    mPrintOp("\tshow: bioServer show [disk/net/olc/evict/existHit]\n");
    mPrintOp("\ttrace: bioServer trace [show/clear]\n");
    mPrintOp("\tBDM perf: bioServer BdmPerf [read/write] [bsKb] [ioDepth] [sizeMb] [rounds] [dropCaches] "
             "[batch:0/1] [batchSize] [bdmStart] [bdmCount]\n");
    mPrintOp("\tBDM verify: bioServer BdmPerf verify [lenBytes|all] [batch|all] [offset|all] "
             "[rounds] [bdmStart] [bdmCount]\n");
    mPrintOp("\tRCache put: bioServer RCachePut [key] [filePath] [ptId] [length]\n");
    mPrintOp("\tRCache get: bioServer RCacheGet [key] [ptId] [offset] [length] [filePath]\n");
    mPrintOp("\tDelete rCache: bioServer RCacheDelete [ptId] [key]\n");
    mPrintOp("\texit: exit console\n");
}

bool CanConvertToUint64(const std::string &str, uint64_t &val)
{
    try {
        std::size_t pos;
        val = std::stoull(str, &pos);
        if (pos < str.size() && str.find_first_not_of(" \t\n\v\f\r", pos) != std::string::npos) {
            return false;
        }
        return true;
    } catch (const std::invalid_argument &ia) {
        return false;
    } catch (const std::out_of_range &oor) {
        return false;
    }
}

void diagnose::BioServerCommand::HandleRCachePut(const std::vector<std::string> &cmds)
{
    if (!IsUnsignedInteger(cmds[3])) {
        mPrintOp("Invalid input.\n");
        return;
    }

    auto key = const_cast<Key>(cmds[1].c_str());
    auto filePath = cmds[2].c_str();
    uint64_t ptId = 0;
    uint64_t length = 0;
    try {
        ptId = std::stoull(cmds[3]);
        length = std::stoull(cmds[4]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }

    uint64_t curPtTimes;
    std::map<uint16_t, CmPtInfo> ptView = BioServer::Instance()->GetPtView(&curPtTimes);
    if (ptId >= ptView.size()) {
        mPrintOp("Failed to put value to rCache, PtId exceed%llu.\n", ptId);
        return;
    }

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

    WCacheSlicePtr writeSlice = nullptr;
    RCacheManagerPtr rCacheManager = RCacheManager::Instance();
    rCacheManager->AllocResources(ptId, length, writeSlice);
    if (writeSlice == nullptr) {
        mPrintOp("Write cache put to read cache alloc fail.");
        delete[] value;
        fclose(fp);
        return;
    }

    CacheSliceOperator sliceOperator;
    sliceOperator.Copy(value, writeSlice.Get());
    uint32_t dataCrc = 0;
    writeSlice->CalculateDataCrc(dataCrc, 0, writeSlice->GetLength());
    writeSlice->SetDataCrc(dataCrc);
    auto ret = rCacheManager->Put(ptId, key, writeSlice);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to put value to rCache, result:%d.\n", ret);
    } else {
        mPrintOp("Put value to rCache successfully, key:%s, ptId:%llu, length:%llu.\n", key, ptId, length);
    }

    delete[] value;
    fclose(fp);
}

void diagnose::BioServerCommand::HandleRCacheGet(const std::vector<std::string> &cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("Invalid input.\n");
            return;
        }
    }

    uint64_t ptId = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
    auto const_key = cmds[1].c_str();
    char* key = const_cast<char*>(const_key);
    try {
        ptId = std::stoull(cmds[2]);
        offset = std::stoull(cmds[3]);
        length = std::stoull(cmds[4]);
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

    MrInfo mrInfo;
    char *ptr = (char *)malloc(length);
    if (ptr == nullptr) {
        mPrintOp("Malloc fail.\n");
        fclose(fp);
        return;
    }
    mrInfo.address = reinterpret_cast<uint64_t>(ptr);
    mrInfo.size = length;
    FlowAddr flowAddr(mrInfo);
    std::vector<FlowAddr> flowAddrs{flowAddr};
    RCacheSlicePtr rCacheSlice = MakeRef<RCacheSlice>(ptId, length, flowAddrs);
    static auto writer = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
        CacheSliceOperator sliceOperator;
        auto ret = sliceOperator.Copy(from, to);
        return ret;
    };

    uint64_t realLength;
    RCacheManagerPtr rCacheManager = RCacheManager::Instance();
    auto ret = rCacheManager->Get(ptId, key, offset, rCacheSlice, writer, realLength);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Get key from cache failed, ret:%d, key:%s\n", ret, key);
    } else {
        mPrintOp("Get value success, key:%s, ptId:%llu, offset:%llu, length:%llu, realLen:%llu.\n",
                     key, ptId, offset, length, realLength);
        if (fwrite(ptr, sizeof(char), realLength, fp) != realLength) {
            mPrintOp("fwrite value to file failed, errno:%d.\n", errno);
        }
    }

    free(ptr);
    fclose(fp);
}

void diagnose::BioServerCommand::HandleRCacheDelete(const std::vector<std::string> &cmds)
{
    if (!IsUnsignedInteger(cmds[1])) {
        mPrintOp("Invalid input.\n");
        return;
    }

    uint64_t ptId = 0;
    auto const_key = cmds[2].c_str();
    char* key = const_cast<char*>(const_key);
    try {
        ptId = std::stoull(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }

    RCacheManagerPtr rCacheManager = RCacheManager::Instance();
    auto ret = rCacheManager->Delete(ptId, key);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to delete key: %s, result:%d.\n", key, ret);
    } else {
        mPrintOp("Delete key success, key: %s.\n", key);
    }
}

void diagnose::BioServerCommand::HandleBdmPerf(const std::vector<std::string> &cmds)
{
    for (size_t i = 1; i < cmds.size(); i++) {
        if (i == 1) {
            continue;
        }
        if (!IsUnsignedInteger(cmds[i])) {
            mPrintOp("Invalid input.\n");
            return;
        }
    }

    bool isRead = false;
    if (cmds[1] == "read") {
        isRead = true;
    } else if (cmds[1] != "write") {
        mPrintOp("Invalid rw type:%s.\n", cmds[1].c_str());
        return;
    }

    uint64_t bs = 0;
    uint32_t ioDepth = 0;
    uint64_t totalBytes = 0;
    uint32_t rounds = 0;
    bool dropCaches = false;
    bool useBatch = false;
    uint32_t batchSize = 0;
    uint32_t bdmStart = 0;
    uint32_t bdmCount = 1;
    try {
        bs = std::stoull(cmds[2]) * BDM_BENCH_KB;
        ioDepth = static_cast<uint32_t>(std::stoul(cmds[3]));
        totalBytes = std::stoull(cmds[4]) * BDM_BENCH_MB;
        rounds = static_cast<uint32_t>(std::stoul(cmds[5]));
        dropCaches = std::stoul(cmds[6]) != 0;
        useBatch = (cmds.size() > 7) && (std::stoul(cmds[7]) != 0);
        if (useBatch) {
            batchSize = (cmds.size() > 8) ? static_cast<uint32_t>(std::stoul(cmds[8])) : ioDepth;
        }
        if (cmds.size() > 9) {
            bdmStart = static_cast<uint32_t>(std::stoul(cmds[9]));
        }
        if (cmds.size() > 10) {
            bdmCount = static_cast<uint32_t>(std::stoul(cmds[10]));
        }
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }

    auto &daemonConfig = BioConfig::Instance()->GetDaemonConfig();
    uint64_t chunkSize = daemonConfig.segment;
    if (bs == 0 || ioDepth == 0 || totalBytes == 0 || rounds == 0 || chunkSize == 0) {
        mPrintOp("Invalid input, bs:%llu, ioDepth:%u, totalBytes:%llu, rounds:%u, chunkSize:%llu.\n", bs, ioDepth,
            totalBytes, rounds, chunkSize);
        return;
    }
    if (bs > chunkSize || (chunkSize % bs) != 0 || (bs % 4096ULL) != 0 || (totalBytes % bs) != 0) {
        mPrintOp("Invalid input, require bs <= chunkSize, chunkSize %% bs == 0, bs aligned to 4KB, "
                 "and size %% bs == 0. bs:%llu, chunkSize:%llu, totalBytes:%llu.\n",
            bs, chunkSize, totalBytes);
        return;
    }
    if (useBatch && (batchSize == 0 || batchSize > ioDepth)) {
        mPrintOp("Invalid batchSize:%u, require 0 < batchSize <= ioDepth:%u.\n", batchSize, ioDepth);
        return;
    }
    if (BdmGetDiskCount() == 0) {
        mPrintOp("BDM has no disk.\n");
        return;
    }
    if (bdmCount == 0 || bdmStart >= BdmGetDiskCount() || bdmStart + bdmCount > BdmGetDiskCount()) {
        mPrintOp("Invalid bdm range, bdmStart:%u, bdmCount:%u, diskCount:%u.\n", bdmStart, bdmCount,
            BdmGetDiskCount());
        return;
    }

    mPrintOp("BDM perf start, engine:%s, rw:%s, bs:%llu, ioDepth:%u, size:%llu, rounds:%u, dropCaches:%u, "
             "batch:%u, batchSize:%u, "
             "chunkSize:%llu, bdmStart:%u, bdmCount:%u.\n",
        BdmBenchEngineName(), cmds[1].c_str(), bs, ioDepth, totalBytes, rounds, dropCaches ? 1U : 0U,
        useBatch ? 1U : 0U, batchSize, chunkSize, bdmStart, bdmCount);

    for (uint32_t round = 0; round < rounds; round++) {
        uint64_t chunkNum = (totalBytes + chunkSize - 1) / chunkSize;
        std::vector<uint64_t> chunks;
        chunks.reserve(chunkNum);
        uint64_t bucketId = (static_cast<uint64_t>(getpid()) << 32U) | round;
        int32_t ret = BDM_CODE_OK;
        for (uint64_t i = 0; i < chunkNum; i++) {
            uint64_t chunkId = 0;
            uint32_t bdmId = bdmStart + static_cast<uint32_t>(i % bdmCount);
            ret = BdmAlloc(bdmId, bucketId, i * chunkSize, chunkSize, &chunkId);
            if (ret != BDM_CODE_OK) {
                mPrintOp("BDM alloc failed, round:%u, index:%llu, bdmId:%u, ret:%d.\n", round, i, bdmId, ret);
                break;
            }
            chunks.emplace_back(chunkId);
        }
        if (ret != BDM_CODE_OK) {
            for (auto chunkId : chunks) {
                BdmFree(BdmBenchChunkBdmId(chunkId), chunkSize, chunkId);
            }
            return;
        }

        if (isRead) {
            BdmBenchResult fillResult;
            ret = BdmBenchRunIo(chunks, bs, totalBytes, chunkSize, ioDepth, false, false, useBatch, batchSize,
                fillResult);
            if (ret != BDM_CODE_OK) {
                mPrintOp("BDM read prefill failed, round:%u, ret:%d, errors:%llu.\n", round, ret, fillResult.errors);
                for (auto chunkId : chunks) {
                    BdmFree(BdmBenchChunkBdmId(chunkId), chunkSize, chunkId);
                }
                return;
            }
        }

        if (dropCaches && !BdmBenchDropCaches()) {
            mPrintOp("Drop caches failed, errno:%d.\n", errno);
        }

        BdmBenchResult result;
        ret = BdmBenchRunIo(chunks, bs, totalBytes, chunkSize, ioDepth, isRead, true, useBatch, batchSize, result);
        mPrintOp("BDM perf round:%u result:%d, engine:%s, rw:%s, batch:%u, batchSize:%u, elapsed:%.2f ms, "
                 "throughput:%.4f MB/s, IOPS:%.2f, avg:%.2f us, p50:%.2f us, p99:%.2f us, errors:%llu.\n",
            round, ret, BdmBenchEngineName(), cmds[1].c_str(), useBatch ? 1U : 0U, batchSize,
            static_cast<double>(result.elapsedNs) / 1000000.0, result.mbps, result.iops, result.avgUs, result.p50Us,
            result.p99Us, result.errors);

        for (auto chunkId : chunks) {
            BdmFree(BdmBenchChunkBdmId(chunkId), chunkSize, chunkId);
        }

        if (ret != BDM_CODE_OK) {
            return;
        }
    }
    mPrintOp("BDM perf end.\n");
}

void diagnose::BioServerCommand::HandleBdmVerify(const std::vector<std::string> &cmds)
{
    std::vector<uint64_t> lengths;
    std::vector<uint32_t> batches;
    std::vector<uint64_t> offsets;
    if (!BdmVerifyParseList(cmds, 2, BdmVerifyLengths(), lengths) ||
        !BdmVerifyParseList(cmds, 3, BdmVerifyBatches(), batches) ||
        !BdmVerifyParseList(cmds, 4, BdmVerifyOffsets(), offsets)) {
        mPrintOp("Invalid input.\n");
        return;
    }

    uint32_t rounds = 1;
    uint32_t bdmStart = 0;
    uint32_t bdmCount = 1;
    try {
        if (cmds.size() > 5) {
            rounds = static_cast<uint32_t>(std::stoul(cmds[5]));
        }
        if (cmds.size() > 6) {
            bdmStart = static_cast<uint32_t>(std::stoul(cmds[6]));
        }
        if (cmds.size() > 7) {
            bdmCount = static_cast<uint32_t>(std::stoul(cmds[7]));
        }
    } catch (std::exception &) {
        mPrintOp("Invalid input.\n");
        return;
    }

    auto &daemonConfig = BioConfig::Instance()->GetDaemonConfig();
    uint64_t chunkSize = daemonConfig.segment;
    if (rounds == 0 || chunkSize == 0 || BdmGetDiskCount() == 0) {
        mPrintOp("Invalid verify environment, rounds:%u, chunkSize:%llu, diskCount:%u.\n", rounds, chunkSize,
            BdmGetDiskCount());
        return;
    }
    if (bdmCount == 0 || bdmStart >= BdmGetDiskCount() || bdmStart + bdmCount > BdmGetDiskCount()) {
        mPrintOp("Invalid bdm range, bdmStart:%u, bdmCount:%u, diskCount:%u.\n", bdmStart, bdmCount,
            BdmGetDiskCount());
        return;
    }

    mPrintOp("BDM verify start, engine:%s, chunkSize:%llu, rounds:%u, bdmStart:%u, bdmCount:%u.\n",
        BdmBenchEngineName(), chunkSize, rounds, bdmStart, bdmCount);
    for (uint32_t round = 0; round < rounds; round++) {
        for (uint64_t keyLen : lengths) {
            if (keyLen == 0 || keyLen > chunkSize) {
                mPrintOp("BDM verify skip, round:%u, keyLen:%llu, chunkSize:%llu.\n", round, keyLen, chunkSize);
                continue;
            }

            char *writeBuf = nullptr;
            if (posix_memalign(reinterpret_cast<void **>(&writeBuf), 4096, keyLen) != 0) {
                mPrintOp("BDM verify alloc write buffer failed, keyLen:%llu.\n", keyLen);
                return;
            }
            BdmVerifyFill(writeBuf, keyLen, keyLen + round);

            uint64_t chunkId = 0;
            uint32_t bdmId = bdmStart + static_cast<uint32_t>(round % bdmCount);
            uint64_t bucketId = (static_cast<uint64_t>(getpid()) << 32U) | (round << 16U) | (keyLen & 0xffffU);
            int32_t ret = BdmAlloc(bdmId, bucketId, 0, chunkSize, &chunkId);
            if (ret != BDM_CODE_OK) {
                mPrintOp("BDM verify alloc failed, round:%u, keyLen:%llu, bdmId:%u, ret:%d.\n", round, keyLen, bdmId,
                    ret);
                free(writeBuf);
                return;
            }

            BdmBenchRequest writeReq;
            if (sem_init(&writeReq.sem, 0, 0) != 0) {
                mPrintOp("BDM verify init write sem failed.\n");
                BdmFree(BdmBenchChunkBdmId(chunkId), chunkSize, chunkId);
                free(writeBuf);
                return;
            }
            writeReq.ioCtx.cb = BdmBenchCallback;
            writeReq.ioCtx.ctx = &writeReq;
            writeReq.done.store(false, std::memory_order_release);
            writeReq.inFlight.store(true, std::memory_order_release);
            ret = BdmWriteAsync(chunkId, 0, writeBuf, keyLen, &writeReq.ioCtx);
            uint64_t writeErrors = 0;
            if (ret == BDM_CODE_OK) {
                BdmBenchWaitRequest(writeReq, writeErrors);
                ret = writeReq.ret;
            }
            sem_destroy(&writeReq.sem);
            if (ret != BDM_CODE_OK || writeErrors != 0) {
                mPrintOp("BDM verify write failed, round:%u, engine:%s, keyLen:%llu, ret:%d, errors:%llu.\n", round,
                    BdmBenchEngineName(), keyLen, ret, writeErrors);
                BdmFree(BdmBenchChunkBdmId(chunkId), chunkSize, chunkId);
                free(writeBuf);
                return;
            }

            for (uint64_t offset : offsets) {
                if (offset >= keyLen) {
                    mPrintOp("BDM verify skip offset, round:%u, keyLen:%llu, offset:%llu.\n", round, keyLen, offset);
                    continue;
                }
                uint64_t readLen = keyLen - offset;
                for (uint32_t batch : batches) {
                    uint32_t bufferSlots = 0;
                    uint64_t errors = 0;
                    uint64_t startNs = BdmBenchNowNs();
                    ret = BdmVerifyBatchRead(chunkId, offset, readLen, batch, writeBuf + offset, bufferSlots, errors);
                    uint64_t elapsedNs = BdmBenchNowNs() - startNs;
                    mPrintOp("BDM verify case round:%u engine:%s keyLen:%llu offset:%llu readLen:%llu batch:%u "
                             "bufferSlots:%u ret:%d memcmp:%s elapsed:%.2f ms errors:%llu.\n",
                        round, BdmBenchEngineName(), keyLen, offset, readLen, batch, bufferSlots, ret,
                        (ret == BDM_CODE_OK && errors == 0) ? "OK" : "FAIL",
                        static_cast<double>(elapsedNs) / 1000000.0, errors);
                    if (ret != BDM_CODE_OK || errors != 0) {
                        BdmFree(BdmBenchChunkBdmId(chunkId), chunkSize, chunkId);
                        free(writeBuf);
                        return;
                    }
                }
            }

            BdmFree(BdmBenchChunkBdmId(chunkId), chunkSize, chunkId);
            free(writeBuf);
        }
    }
    mPrintOp("BDM verify end.\n");
}

void diagnose::BioServerCommand::BioServerDebugProcess(int argc, char *argv[]) noexcept
{
    if (argc <= 1) {
        BioServerDebugHelp(argv[0], 1);
        return;
    }
    std::vector<std::string> cmds;
    for (int i = 1; i < argc; i++) {
        std::string str(argv[i]);
        cmds.emplace_back(str);
    }

    std::string cmdType = cmds[0];
    std::string ratios;
    std::string errMsg;
    if (cmdType == "chgwlv") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }

        uint64_t tier = 0;
        if (!CanConvertToUint64(cmds[1], tier)) {
            mPrintOp("Input tier parameters failed!, values %s is not number\n", cmds[1].c_str());
            return;
        }

        uint64_t value = 0;
        if (!CanConvertToUint64(cmds[2], value)) {
            mPrintOp("Input parameters failed!, values %s is not number\n", cmds[2].c_str());
            return;
        }

        if ((tier != 0 && tier != 1) || (value < 0 || value > 100)) {
            mPrintOp("Input parameters failed!, water level tier:%s %s should in range(0-100)\n", cmds[1].c_str(),
                         cmds[2].c_str());
            return;
        }
        HandleModifyEvictWaterLevel(tier, value);
    } else if (cmdType == "chgmr") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%d\n", cmds.size());
        }
        if (!ValidateRatios("ubsio.cache.mem_read_write_ratio", cmds[1], errMsg)) {
            mPrintOp("Input parameters failed!, %s, values %s\n", errMsg.c_str(), cmds[1].c_str());
            return;
        }
        HandleModifyMemReadWriteRatio(cmds[1]);
    } else if (cmdType == "chgdr") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        if (!ValidateRatios("ubsio.cache.disk_read_write_ratio", cmds[1], errMsg)) {
            mPrintOp("Input parameters failed!, %s, values %s\n", errMsg.c_str(), cmds[1].c_str());
            return;
        }
        HandleModifyDiskReadWriteRatio(cmds[1]);
    } else if (cmdType == "show") {
        if (cmds.size() < 2) {
            mPrintOp("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        BioServerHandleShow(cmds);
    } else if (cmdType == "trace") {
        if (cmds.size() != 2 && cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleServerTrace(cmds);
    } else if (cmdType == "RCachePut"){
        if (cmds.size() != 5) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleRCachePut(cmds);
    } else if (cmdType == "RCacheGet"){
        if (cmds.size() != 6) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleRCacheGet(cmds);
    } else if (cmdType == "RCacheDelete") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleRCacheDelete(cmds);
    } else if (cmdType == "BdmPerf") {
        if (cmds.size() > 1 && cmds[1] == "verify") {
            if (cmds.size() > 8) {
                mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
                return;
            }
            HandleBdmVerify(cmds);
            return;
        }
        if (cmds.size() < 7 || cmds.size() > 11) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleBdmPerf(cmds);
    } else if (cmdType == "exit") {
        return;
    } else {
        BioServerDebugHelp(argv[0], 1);
    }
}

int ServerDiagnoseInit()
{
    return ock::bio::diagnose::BioServerCommand::Initialize();
}
