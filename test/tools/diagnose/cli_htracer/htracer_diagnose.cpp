/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 */

#include "cli.h"
#include "htracer.h"
#include "htracer_diagnose.h"

#ifdef __cplusplus
extern "C" {
#endif

int HtracerDiagnoseInit()
{
    return ock::htracer::diagnose::HtracerCommand::Initialize();
}

#ifdef __cplusplus
}
#endif

using namespace ock::htracer;

static void HtracerDebugProcess(int argc, char *argv[]) noexcept;
static void HtracerDebugHelp(char *command, int detail) noexcept;
static void HtracerShow() noexcept;
static void HtracerClear() noexcept;

int diagnose::HtracerCommand::Initialize() noexcept
{
    CLI_CMD_S command;
    strncpy(command.szCommand, "htracer", CLI_MAX_COMMAND_LEN);
    strncpy(command.szDescription, "htracer commands.", CLI_MAX_CMD_DESC_LEN);
    command.fnCmdDo = HtracerDebugProcess;
    command.fnPrintCmdHelp = HtracerDebugHelp;
    auto ret = CLI_RegCmd(&command);
    if (ret != 0) {
        printf("register htracer diagnose failed.");
    }
    return ret;
}

void diagnose::HtracerCommand::Destroy() noexcept
{
    CLI_UnRegCmd((char *)"htracer");
}

static void HtracerDebugProcess(int argc, char *argv[]) noexcept
{
    if (argc <= 1) {
        HtracerDebugHelp(argv[0], 1);
        return;
    }

    if (strcmp(argv[1], "show") == 0) {
        HtracerShow();
    } else if (strcmp(argv[1], "clear") == 0) {
        HtracerClear();
    } else {
        HtracerDebugHelp(argv[0], 1);
    }
}

static void HtracerDebugHelp(char *command, int detail) noexcept
{
    CLI_PrintBuf("htracer show : print all performance statistics information.\n");
    CLI_PrintBuf("htracer clear : reset all performance statistics information.\n");
}

static void HtracerShow() noexcept
{
    auto info = ock::htracer::GetTraceInfo();
    CLI_PrintBuf(info.c_str());
}

static void HtracerClear() noexcept
{
    ock::htracer::ClearTraceInfo();
    CLI_PrintBuf("clearing statistics records succeeded.\n");
}