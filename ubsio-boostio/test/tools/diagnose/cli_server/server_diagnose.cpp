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
#include <iostream>
#include <memory>
#include "htracer.h"
#include "bio_client.h"
#include "bio_config_instance.h"
#include "bio_log.h"
#include "bio_trace.h"
#include "bdm_core.h"
#include "bio_server.h"
#include "bio_functions.h"
#include "cache_overload_ctrl.h"
#include "server_diagnose.h"
#include "securec.h"

using namespace ock::bio;
std::regex serverPattern("[0-9]+");

namespace {
constexpr uint64_t DIAG_ALIGN_SIZE = 4096;
constexpr uint64_t DIAG_KB_SIZE = 1024;
constexpr double DIAG_MB_SIZE = 1024.0 * 1024.0;

char *AllocAlignedBuf(uint64_t len)
{
    void *ptr = nullptr;
    if (posix_memalign(&ptr, DIAG_ALIGN_SIZE, len) != 0) {
        return nullptr;
    }
    return static_cast<char *>(ptr);
}

double ToUs(const std::chrono::steady_clock::duration &duration)
{
    return std::chrono::duration<double, std::micro>(duration).count();
}
}

bool ock::bio::diagnose::BioServerCommand::mInited = false;
void* ock::bio::diagnose::BioServerCommand::mHandler = nullptr;
CliRegCmdFuncPtr ock::bio::diagnose::BioServerCommand::mRegOp = nullptr;
CliUnRegCmdFuncPtr ock::bio::diagnose::BioServerCommand::mUnRegOp = nullptr;
CliPrintBufFuncPtr ock::bio::diagnose::BioServerCommand::mPrintOp = nullptr;

int32_t diagnose::BioServerCommand::LoadSymbols()
{
    const char* soFileName = "libcli_agent.so";
    mHandler = dlopen(soFileName, RTLD_NOW);
    if (mHandler == nullptr) {
        LOG_ERROR("Failed to open library() " << soFileName << " dlopen, error " << dlerror());
        return BIO_INNER_ERR;
    }

    mRegOp = reinterpret_cast<CliRegCmdFuncPtr>(dlsym(mHandler, "CLI_RegCmd"));
    mUnRegOp = reinterpret_cast<CliUnRegCmdFuncPtr>(dlsym(mHandler, "CLI_UnRegCmd"));
    mPrintOp = reinterpret_cast<CliPrintBufFuncPtr>(dlsym(mHandler, "CLI_PrintBuf"));
    if (mRegOp == nullptr || mUnRegOp == nullptr || mPrintOp == nullptr) {
        LOG_ERROR("Failed to load function.");
        dlclose(mHandler);
        return BIO_INNER_ERR;
    }

    return BIO_OK;
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

    CLI_CMD_S command;
    strncpy(command.szCommand, "bioServer", CLI_MAX_COMMAND_LEN);
    strncpy(command.szDescription, "bioServer commands.", CLI_MAX_CMD_DESC_LEN);
    command.fnCmdDo = BioServerDebugProcess;
    command.fnPrintCmdHelp = BioServerDebugHelp;
    auto result = mRegOp(&command);
    if (result == 0) {
        mInited = true;
    }
    return result;
}

void diagnose::BioServerCommand::Destroy() noexcept
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
        mPrintOp("Pt view:\n");
        for (auto &ptEntry : ptView) {
            mPrintOp("%s\n", ptEntry.second.ToString().c_str());
        }
    } else if (cmdType == "node") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint64_t curNodeTimes;
        std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioServer::Instance()->GetNodeView(&curNodeTimes);
        mPrintOp("Node view:\n");
        for (auto &nodeEntry : nodeView) {
            mPrintOp("%s\n", nodeEntry.second.ToString().c_str());
        }
        mPrintOp("Local Node:");
        CmNodeId localNode = BioServer::Instance()->GetLocalNid();
        mPrintOp("%s\n", localNode.ToString().c_str());
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
    } else if (cmdType == "evict") {
        Cache::Instance().ShowEvictNegotiateQueue();
        mPrintOp("Show evict negotiate queue success, please see log file.\n");
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
        mPrintOp(info.c_str());
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
    mPrintOp("\tchange water level: bioserver chgwlv [tier] [water_level]\n");
    mPrintOp("\tchange memory read write ratio: bioserver chgmr [memory ratio]\n");
    mPrintOp("\tchange disk read write ratio: bioserver chgdr [disk ratio]\n");
    mPrintOp("\tshow: bioserver show [disk/net/olc/evict]\n");
    mPrintOp("\ttrace: bioserver trace [show/clear]\n");
    mPrintOp("\traw perf: bioserver rawperf [diskId] [bsKB] [count]\n");
    mPrintOp("\tRCache put: bioserver RCachePut [key] [filePath] [ptId] [length]\n");
    mPrintOp("\tRCache get: bioserver RCacheGet [key] [ptId] [offset] [length] [filePath]\n");
    mPrintOp("\tDelete rCache: bioserver RCacheDelete [ptId] [key]\n");
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

void diagnose::BioServerCommand::HandleRawPerf(const std::vector<std::string> &cmds)
{
    uint64_t diskId = 0;
    uint64_t bsKb = 0;
    uint64_t count = 0;
    if (!CanConvertToUint64(cmds[1], diskId) || !CanConvertToUint64(cmds[2], bsKb) ||
        !CanConvertToUint64(cmds[3], count) || bsKb == 0 || count == 0) {
        mPrintOp("Input parameters failed, usage: bioserver rawperf [diskId] [bsKB] [count]\n");
        return;
    }

    if (diskId >= BdmGetDiskCount()) {
        mPrintOp("Invalid diskId:%llu, diskCount:%u.\n", diskId, BdmGetDiskCount());
        return;
    }

    uint64_t ioLen = bsKb * DIAG_KB_SIZE;
    if (ioLen == 0 || ioLen > BDM_MAX_CHUNK_LENGTH) {
        mPrintOp("Invalid bsKB:%llu, ioLen:%llu, max:%lu.\n", bsKb, ioLen, BDM_MAX_CHUNK_LENGTH);
        return;
    }

    char *src = AllocAlignedBuf(ioLen);
    char *dst = AllocAlignedBuf(ioLen);
    if (src == nullptr || dst == nullptr) {
        free(src);
        free(dst);
        mPrintOp("Alloc buffer failed, ioLen:%llu.\n", ioLen);
        return;
    }

    if (memset_s(src, ioLen, 0x5A, ioLen) != BDM_CODE_OK || memset_s(dst, ioLen, 0, ioLen) != BDM_CODE_OK) {
        free(src);
        free(dst);
        mPrintOp("Init buffer failed, ioLen:%llu.\n", ioLen);
        return;
    }

    uint64_t chunkId = 0;
    uint64_t chunkLen = BDM_MAX_CHUNK_LENGTH;
    auto ret = BdmAlloc(static_cast<uint32_t>(diskId), 0, 0, chunkLen, &chunkId);
    if (ret != BDM_CODE_OK) {
        free(src);
        free(dst);
        mPrintOp("BdmAlloc failed, diskId:%llu, len:%llu, ret:%d.\n", diskId, chunkLen, ret);
        return;
    }

    auto totalBegin = std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration copyCost = std::chrono::steady_clock::duration::zero();
    std::chrono::steady_clock::duration writeCost = std::chrono::steady_clock::duration::zero();
    uint64_t done = 0;
    uint64_t slotNum = chunkLen / ioLen;
    for (; done < count; ++done) {
        uint64_t offset = (done % slotNum) * ioLen;
        BIO_TRACE_START(BDM_TRACE_DIAG_MEMCPY_WRITE);

        auto copyBegin = std::chrono::steady_clock::now();
        BIO_TRACE_START(BDM_TRACE_DIAG_MEMCPY);
        ret = memcpy_s(dst, ioLen, src, ioLen);
        BIO_TRACE_END(BDM_TRACE_DIAG_MEMCPY, ret);
        copyCost += std::chrono::steady_clock::now() - copyBegin;
        if (ret != BDM_CODE_OK) {
            BIO_TRACE_END(BDM_TRACE_DIAG_MEMCPY_WRITE, ret);
            break;
        }

        auto writeBegin = std::chrono::steady_clock::now();
        BIO_TRACE_START(BDM_TRACE_DIAG_WRITE);
        ret = BdmWrite(chunkId, offset, dst, ioLen);
        BIO_TRACE_END(BDM_TRACE_DIAG_WRITE, ret);
        writeCost += std::chrono::steady_clock::now() - writeBegin;
        BIO_TRACE_END(BDM_TRACE_DIAG_MEMCPY_WRITE, ret);
        if (ret != BDM_CODE_OK) {
            break;
        }
    }
    auto totalCost = std::chrono::steady_clock::now() - totalBegin;

    auto freeRet = BdmFree(static_cast<uint32_t>(diskId), chunkLen, chunkId);
    free(src);
    free(dst);

    double dataMb = static_cast<double>(ioLen) * static_cast<double>(done) / DIAG_MB_SIZE;
    double copyUs = ToUs(copyCost);
    double writeUs = ToUs(writeCost);
    double totalUs = ToUs(totalCost);
    mPrintOp("Raw Perf Result:\n");
    mPrintOp("  diskId:%llu, bs:%llu bytes, count:%llu/%llu, chunkId:%llu, chunkLen:%llu\n",
        diskId, ioLen, done, count, chunkId, chunkLen);
    mPrintOp("  memcpy   total:%.3f us, avg:%.3f us, throughput:%.3f MB/s\n",
        copyUs, done == 0 ? 0.0 : copyUs / static_cast<double>(done),
        copyUs <= 0 ? 0.0 : dataMb * 1000000.0 / copyUs);
    mPrintOp("  bdmWrite total:%.3f us, avg:%.3f us, throughput:%.3f MB/s\n",
        writeUs, done == 0 ? 0.0 : writeUs / static_cast<double>(done),
        writeUs <= 0 ? 0.0 : dataMb * 1000000.0 / writeUs);
    mPrintOp("  end2end  total:%.3f us, avg:%.3f us, throughput:%.3f MB/s\n",
        totalUs, done == 0 ? 0.0 : totalUs / static_cast<double>(done),
        totalUs <= 0 ? 0.0 : dataMb * 1000000.0 / totalUs);
    mPrintOp("  ret:%d, freeRet:%d\n", ret, freeRet);
}

void diagnose::BioServerCommand::HandleRCachePut(const std::vector<std::string> &cmds)
{
    if (!std::regex_match(cmds[3], serverPattern)) {
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
        mPrintOp("Failed to put value to rCache, PtId exceed%d.\n");
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
        if (!std::regex_match(cmds[i], serverPattern)) {
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

    delete[] ptr;
    fclose(fp);
}

void diagnose::BioServerCommand::HandleRCacheDelete(const std::vector<std::string> &cmds)
{
    if (!std::regex_match(cmds[1], serverPattern)) {
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
        if (!ValidateRatios("bio.cache.mem_read_write_ratio", cmds[1], errMsg)) {
            mPrintOp("Input parameters failed!, %s, values %s\n", errMsg.c_str(), cmds[1].c_str());
            return;
        }
        HandleModifyMemReadWriteRatio(cmds[1]);
    } else if (cmdType == "chgdr") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        if (!ValidateRatios("bio.cache.disk_read_write_ratio", cmds[1], errMsg)) {
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
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleServerTrace(cmds);
    } else if (cmdType == "rawperf") {
        if (cmds.size() != 4) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleRawPerf(cmds);
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
