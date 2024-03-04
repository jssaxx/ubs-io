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
#include "server_diagnose.h"
#include "cli.h"
#include "bio_config_instance.h"
#include "bio_log.h"

using namespace ock::bio;

static void BioServerDebugProcess(int argc, char *argv[]) noexcept;
static void BioServerDebugHelp(char *command, int detail) noexcept;
static void HandleModifyEvictWaterLevel(uint64_t level);
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
        return -1;
    } else {
        printf("register BioServer diagnose succeeded\n");
    }

    return 0;
}

void diagnose::BioServerCommand::Destroy() noexcept
{
    CLI_UnRegCmd((char *)"bioServer");
}

static void HandleModifyEvictWaterLevel(uint64_t level)
{
    auto ori = BioConfig::Instance()->ModifyConfigEvictWaterLevel(level);
    CLI_PrintBuf("config changed: EvictWaterLevel, %lu => %lu\n",ori,level);
}

static void HandleModifyEvictMemQuantity(uint64_t quantity)
{
    auto ori = BioConfig::Instance()->ModifyConfigMemResourceQuantity(quantity);
    CLI_PrintBuf("config changed: MemResourceQuantity(GB), %lu => %lu\n",ori,quantity);
}

static void HandleModifyEvictDiskQuantity(uint64_t quantity)
{
    auto ori = BioConfig::Instance()->ModifyConfigDiskResourceQuantity(quantity);
    CLI_PrintBuf("config changed: DiskResourceQuantity(GB), %lu => %lu\n",ori,quantity);
}

static void BioServerDebugHelp(char *command, int detail) noexcept
{
    CLI_PrintBuf("change water level:bioserver chgwlv [water_level]\n");
    CLI_PrintBuf("change memory resource quantity(GB):bioserver chgmq [memory_quantity]\n");
    CLI_PrintBuf("change disk resource quantity(GB):bioserver chgdq [disk_quantity]\n");
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
    std::vector<std::string> cmds;
    for (int i = 1; i < argc; i++) {
        std::string str(argv[i]);
        cmds.emplace_back(str);
    }

    std::string cmdType = cmds[0];
    uint64_t value = 0;
    if (cmdType == "chgwlv") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
        }
        if (!CanConvertToUint64(cmds[1], value)) {
            CLI_PrintBuf("Input parameters failed!, values %s is not number\n", cmds[1]);
        }
        HandleModifyEvictWaterLevel(value);
    } else if (cmdType == "chgmq") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
        }
        if (!CanConvertToUint64(cmds[1], value)) {
            CLI_PrintBuf("Input parameters failed!, values %s is not number\n", cmds[1]);
        }
        HandleModifyEvictMemQuantity(value);
    } else if (cmdType == "chgdq") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
        }
        if (!CanConvertToUint64(cmds[1], value)) {
            CLI_PrintBuf("Input parameters failed!, values %s is not number\n", cmds[1]);
        }
        HandleModifyEvictDiskQuantity(value);
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