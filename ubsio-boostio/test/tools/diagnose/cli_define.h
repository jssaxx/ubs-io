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

#ifndef BOOSTIO_CLI_DEFINE_H
#define BOOSTIO_CLI_DEFINE_H

#include <cstdint>
#include <string>
#include <vector>

#define CLI_MAX_COMMAND_LEN 20
#define CLI_MAX_CMD_DESC_LEN 64

typedef void (*FN_CLI_CMD_PROC)(int32_t v_iArgc, char *v_szArgv[]);
typedef void (*FN_CLI_CMD_HELP_PROC)(char *v_szCommand, int32_t iShowDetail);
typedef struct
{
    char szCommand[CLI_MAX_COMMAND_LEN];
    char szDescription[CLI_MAX_CMD_DESC_LEN];
    FN_CLI_CMD_PROC fnCmdDo;
    FN_CLI_CMD_HELP_PROC fnPrintCmdHelp;
} CLI_CMD_S;

using CliRegCmdFuncPtr = int32_t (*)(CLI_CMD_S*);
using CliUnRegCmdFuncPtr = void (*)(char*);
using CliPrintBufFuncPtr = void (*)(const char*, ...);

#endif

