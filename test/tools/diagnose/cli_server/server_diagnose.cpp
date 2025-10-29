/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <csignal>
#include <sys/resource.h>
#include "htracer.h"
#include "bio_client.h"
#include "cli.h"
#include "bio_config_instance.h"
#include "bio_log.h"
#include "bdm_core.h"
#include "bio_server.h"
#include "server_diagnose.h"
#include "bio_functions.h"
#include "cache_overload_ctrl.h"

using namespace ock::bio;
std::regex serverPattern("[0-9]+");
static void BioServerDebugProcess(int argc, char *argv[]) noexcept;
static void BioServerDebugHelp(char *command, int detail) noexcept;
static void HandleModifyEvictWaterLevel(uint8_t tier, uint64_t level);
static void HandleModifyEvictMemQuantity(uint64_t quantity);
static void HandleModifyEvictDiskQuantity(uint64_t quantity);

static bool mInited = false;
int diagnose::BioServerCommand::Initialize() noexcept
{
    if (mInited) {
        return 0;
    }

    CLI_CMD_S command;
    strncpy(command.szCommand, "bioServer", CLI_MAX_COMMAND_LEN);
    strncpy(command.szDescription, "bioServer commands.", CLI_MAX_CMD_DESC_LEN);
    command.fnCmdDo = BioServerDebugProcess;
    command.fnPrintCmdHelp = BioServerDebugHelp;
    auto result = CLI_RegCmd(&command);
    if (result != 0) {
        printf("register BioServer diagnose failed.");
    }
    mInited = true;
    return result;
}

void diagnose::BioServerCommand::Destroy() noexcept
{
    CLI_UnRegCmd((char *)"bioServer");
}

static void HandleModifyEvictWaterLevel(uint8_t tier, uint64_t level)
{
    auto ori = BioConfig::Instance()->ModifyConfigEvictWaterLevel(tier, level);
    CLI_PrintBuf("config changed tier:%u EvictWaterLevel, %lu => %lu\n", tier, ori, level);
}

static void HandleModifyMemReadWriteRatio(const std::string &ratios)
{
    auto ori = BioConfig::Instance()->ModifyConfigMemReadWriteRatio(ratios);
    CLI_PrintBuf("config changed: MemReadWriteRatio, %s => %s\n", ori.c_str(), ratios.c_str());
}

static void HandleModifyDiskReadWriteRatio(const std::string &ratios)
{
    auto ori = BioConfig::Instance()->ModifyConfigDiskReadWriteRatio(ratios);
    CLI_PrintBuf("config changed: MemReadWriteRatio, %s => %s\n", ori.c_str(), ratios.c_str());
}

static void BioServerHandleShow(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string cmdType(cType);
    if (cmdType == "disk") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        auto &daemonConfig = BioConfig::Instance()->GetDaemonConfig();
        CmDiskStatus diskStatus;
        CLI_PrintBuf("Disk Info:\n");
        CLI_PrintBuf("id        name                status    totalCapacity       usedCapacity \n");
        for (uint32_t i = 0; i < daemonConfig.diskList.size(); i++) {
            if (BioServer::Instance()->GetDiskStatusFromNodeView(i, diskStatus) != BIO_OK) {
                continue;
            }
            if (diskStatus == CM_DISK_FAULT) {
                CLI_PrintBuf("%-10d%-20s%-10s \n", i, daemonConfig.diskList[i].c_str(), "fault");
                continue;
            }
            uint64_t totalCap = 0;
            uint64_t usedCap = 0;
            BdmGetCapacity(i, &totalCap, &usedCap);
            CLI_PrintBuf("%-10d%-20s%-10s%-20llu%-20llu \n",
                i, daemonConfig.diskList[i].c_str(), "normal", totalCap, usedCap);
        }
        return;
    } else if (cmdType == "net") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        std::string protoStr[4U] = { "RDMA", "TCP", "UDS", "SHM" };
        std::string modeStr[2U] = { "BUSY_POLLING", "EVENT_POLLING" };
        uint32_t executorNum = 0;
        NetOptions option;
        BioServer::Instance()->GetNetEngine()->Show(executorNum, option);
        CLI_PrintBuf("Boostio rpc info: \n");
        CLI_PrintBuf("  ip: %s:%u, protocol:%s, mode:%s, workers_count:%u, request_executor:%u, memory_size:%luGB\n",
            option.ipMask.c_str(), option.port, protoStr[option.protocol].c_str(),
            (option.isBusyLoop) ? modeStr[0].c_str() : modeStr[1].c_str(), option.handlerCount, executorNum,
            (option.memoryPoolSize / NO_1024 / NO_1024 / NO_1024));
    } else if (cmdType == "resources") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        CacheResDescription desc;
        Cache::Instance().GetCacheResources(desc, WRITE_CACHE);
        CLI_PrintBuf("WCACHE(MB): mem %lu used %lu disk %lu used %lu \n", desc.memCapacity / NO_1048576,
            desc.memUsedSize / NO_1048576, desc.diskCapacity / NO_1048576, desc.diskUsedSize / NO_1048576);
        Cache::Instance().GetCacheResources(desc, READ_CACHE);
        CLI_PrintBuf("RCACHE(MB): mem %lu used %lu disk %lu used %lu \n", desc.memCapacity / NO_1048576,
            desc.memUsedSize / NO_1048576, desc.diskCapacity / NO_1048576, desc.diskUsedSize / NO_1048576);
    } else if (cmdType == "pt") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint64_t curPtTimes;
        std::map<uint16_t, CmPtInfo> ptView = BioServer::Instance()->GetPtView(&curPtTimes);
        CLI_PrintBuf("Pt view:\n");
        for (auto &ptEntry : ptView) {
            CLI_PrintBuf("%s\n", ptEntry.second.ToString().c_str());
        }
    } else if (cmdType == "node") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint64_t curNodeTimes;
        std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioServer::Instance()->GetNodeView(&curNodeTimes);
        CLI_PrintBuf("Node view:\n");
        for (auto &nodeEntry : nodeView) {
            CLI_PrintBuf("%s\n", nodeEntry.second.ToString().c_str());
        }
        CLI_PrintBuf("Local Node:");
        CmNodeId localNode = BioServer::Instance()->GetLocalNid();
        CLI_PrintBuf("%s\n", localNode.ToString().c_str());
    } else if (cmdType == "olc") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint64_t vmVec = 0;
        uint64_t totalQuota = 0;
        uint64_t currentQuota = 0;
        std::unordered_map<QuotaHolder, uint64_t, QuotaHolderHash, QuotaHolderEqual> holders;
        CacheOverloadCtrl::Instance().Show(vmVec, totalQuota, currentQuota, holders);
        CLI_PrintBuf("  Boostio overload ctrl info: \n");
        CLI_PrintBuf("  Water level:%lu, Total quota:%lu, Remain Quota:%lu\n", vmVec, totalQuota, currentQuota);
        for (auto iter = holders.begin(); iter != holders.end(); iter++) {
            CLI_PrintBuf("  Holder %u-%lu: %lu \n", iter->first.nodeId, iter->first.clientId, iter->second);
        }
    } else {
        CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
    }
}

static void HandleServerTrace(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "show") {
        auto info = ock::htracer::GetTraceInfo();
        CLI_PrintBuf(info.c_str());
    } else if (viewType == "clear") {
        ock::htracer::ClearTraceInfo();
        CLI_PrintBuf("clearing statistics server records succeeded.\n");
    } else if (viewType == "open") {
        ock::htracer::HTracerSetEnable(true);
        CLI_PrintBuf("open statistics sdk records succeeded.\n");
    } else if (viewType == "close") {
        ock::htracer::HTracerSetEnable(false);
        CLI_PrintBuf("close statistics sdk records succeeded.\n");
    }
}

static void BioServerDebugHelp(char *command, int detail) noexcept
{
    CLI_PrintBuf("\tchange water level: bioserver chgwlv [tier] [water_level]\n");
    CLI_PrintBuf("\tchange memory read write ratio: bioserver chgmr [memory ratio]\n");
    CLI_PrintBuf("\tchange disk read write ratio: bioserver chgdr [disk ratio]\n");
    CLI_PrintBuf("\tshow: bioserver show [disk/net/olc/evict]\n");
    CLI_PrintBuf("\ttrace: bioserver trace [show/clear]\n");
    CLI_PrintBuf("\tRCache put: bioserver RCachePut [key] [filePath] [ptId] [length]\n");
    CLI_PrintBuf("\tRCache get: bioserver RCacheGet [key] [ptId] [offset] [length] [filePath]\n");
    CLI_PrintBuf("\tDelete rCache: bioserver RCacheDelete [ptId] [key]\n");
    CLI_PrintBuf("\texit: exit console\n");
}

static bool CanConvertToUint64(const std::string &str, uint64_t &val)
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

static void HandleRCachePut(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[3], serverPattern)) {
        CLI_PrintBuf("Invalid input.\n");
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
        CLI_PrintBuf("Invalid input.\n");
        return;
    }

    uint64_t curPtTimes;
    std::map<uint16_t, CmPtInfo> ptView = BioServer::Instance()->GetPtView(&curPtTimes);
    if (ptId >= ptView.size()) {
        CLI_PrintBuf("Failed to put value to rCache, PtId exceed%d.\n");
        return;
    }

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        CLI_PrintBuf("fopen file failed, file: %s.\n", filePath);
        return;
    }

    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        CLI_PrintBuf("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    WCacheSlicePtr writeSlice = nullptr;
    RCacheManagerPtr rCacheManager = RCacheManager::Instance();
    rCacheManager->AllocResources(ptId, length, writeSlice);
    if (writeSlice == nullptr) {
        CLI_PrintBuf("Write cache put to read cache alloc fail.");
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
        CLI_PrintBuf("Failed to put value to rCache, result:%d.\n", ret);
    } else {
        CLI_PrintBuf("Put value to rCache successfully, key:%s, ptId:%llu, length:%llu.\n", key, ptId, length);
    }

    delete[] value;
    fclose(fp);
}

static void HandleRCacheGet(std::vector<std::string> cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], serverPattern)) {
            CLI_PrintBuf("Invalid input.\n");
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
        CLI_PrintBuf("Invalid input.\n");
        return;
    }

    auto filePath = cmds[5].c_str();
    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "w")) == nullptr) {
        CLI_PrintBuf("fopen file failed, file:%s.\n", filePath);
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
        CLI_PrintBuf("Get key from cache failed, ret:%d, key:%s\n", ret, key);
    } else {
        CLI_PrintBuf("Get value success, key:%s, ptId:%llu, offset:%llu, length:%llu, realLen:%llu.\n",
                     key, ptId, offset, length, realLength);
        if (fwrite(ptr, sizeof(char), realLength, fp) != realLength) {
            CLI_PrintBuf("fwrite value to file failed, errno:%d.\n", errno);
        }
    }

    delete[] ptr;
    fclose(fp);
}

static void HandleRCacheDelete(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[1], serverPattern)) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }

    uint64_t ptId = 0;
    auto const_key = cmds[2].c_str();
    char* key = const_cast<char*>(const_key);
    try {
        ptId = std::stoull(cmds[1]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }

    RCacheManagerPtr rCacheManager = RCacheManager::Instance();
    auto ret = rCacheManager->Delete(ptId, key);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to delete key: %s, result:%d.\n", key, ret);
    } else {
        CLI_PrintBuf("Delete key success, key: %s.\n", key);
    }
}

static void BioServerDebugProcess(int argc, char *argv[]) noexcept
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
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }

        uint64_t tier = 0;
        if (!CanConvertToUint64(cmds[1], tier)) {
            CLI_PrintBuf("Input tier parameters failed!, values %s is not number\n", cmds[1].c_str());
            return;
        }

        uint64_t value = 0;
        if (!CanConvertToUint64(cmds[2], value)) {
            CLI_PrintBuf("Input parameters failed!, values %s is not number\n", cmds[2].c_str());
            return;
        }

        if ((tier != 0 && tier != 1) || (value < 0 || value > 100)) {
            CLI_PrintBuf("Input parameters failed!, water level tier:%s %s should in range(0-100)\n", cmds[1].c_str(),
                         cmds[2].c_str());
            return;
        }
        HandleModifyEvictWaterLevel(tier, value);
    } else if (cmdType == "chgmr") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
        }
        if (!ValidateRatios("bio.cache.mem_read_write_ratio", cmds[1], errMsg)) {
            CLI_PrintBuf("Input parameters failed!, %s, values %s\n", errMsg.c_str(), cmds[1].c_str());
            return;
        }
        HandleModifyMemReadWriteRatio(cmds[1]);
    } else if (cmdType == "chgdr") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        if (!ValidateRatios("bio.cache.disk_read_write_ratio", cmds[1], errMsg)) {
            CLI_PrintBuf("Input parameters failed!, %s, values %s\n", errMsg.c_str(), cmds[1].c_str());
            return;
        }
        HandleModifyDiskReadWriteRatio(cmds[1]);
    } else if (cmdType == "show") {
        if (cmds.size() < 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        BioServerHandleShow(cmds);
    } else if (cmdType == "trace") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleServerTrace(cmds);
    } else if (cmdType == "RCachePut"){
        if (cmds.size() != 5) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleRCachePut(cmds);
    } else if (cmdType == "RCacheGet"){
        if (cmds.size() != 6) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleRCacheGet(cmds);
    } else if (cmdType == "RCacheDelete") {
        if (cmds.size() != 3) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
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