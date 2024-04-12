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

using namespace ock::bio;

static void BioServerDebugProcess(int argc, char *argv[]) noexcept;
static void BioServerDebugHelp(char *command, int detail) noexcept;
static void HandleModifyEvictWaterLevel(uint8_t tier, uint64_t level);
static void HandleModifyEvictMemQuantity(uint64_t quantity);
static void HandleModifyEvictDiskQuantity(uint64_t quantity);

int diagnose::BioServerCommand::Initialize() noexcept
{
    CLI_CMD_S command;
    strncpy(command.szCommand, "bioServer", CLI_MAX_COMMAND_LEN);
    strncpy(command.szDescription, "bioServer commands.", CLI_MAX_CMD_DESC_LEN);
    command.fnCmdDo = BioServerDebugProcess;
    command.fnPrintCmdHelp = BioServerDebugHelp;
    auto result = CLI_RegCmd(&command);
    if (result != 0) {
        printf("register BioServer diagnose failed.");
    }
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
        NetOptions option = BioServer::Instance()->GetNetEngine()->Show();
        CLI_PrintBuf("  Boostio rpc net info: \n");
        CLI_PrintBuf("  Node_IP: %s:%u, Net_protocol:%s, RPC_Mode:%s, Request_executor_num:%u, Workers_count:%u,"
                     " RDMA_memory_size:%lu GB \n",
            option.ipMask.c_str(), option.port, protoStr[option.protocol].c_str(),
            (option.isBusyLoop) ? modeStr[0].c_str() : modeStr[1].c_str(), option.handlerCount, option.connCount,
            (option.memorySize / NO_1024 / NO_1024 / NO_1024));
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
    }
}

static void BioServerDebugHelp(char *command, int detail) noexcept
{
    CLI_PrintBuf("change water level: bioserver chgwlv [tier] [water_level]\n");
    CLI_PrintBuf("change memory read write ratio: bioserver chgmr [memory ratio]\n");
    CLI_PrintBuf("change disk read write ratio: bioserver chgdr [disk ratio]\n");
    CLI_PrintBuf("show: bioserver show [disk/net]\n");
    CLI_PrintBuf("trace: bioserver trace [show/clear]\n");
    CLI_PrintBuf("exit: exit console\n");
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