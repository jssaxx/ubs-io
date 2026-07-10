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
#define CLI_MAX_ERR_MSG_LEN   80

typedef void (*CliCommandHandler)(int32_t argc, char *argv[]);
typedef void (*CliCommandHelpHandler)(char *command, int32_t show_detail);
typedef struct {
    char command[CLI_MAX_COMMAND_LEN];
    char description[CLI_MAX_CMD_DESC_LEN];
    CliCommandHandler handler;
    CliCommandHelpHandler help_handler;
} CliCommand;

using CliRegCmdFuncPtr = int32_t (*)(CliCommand*);
using CliUnRegCmdFuncPtr = void (*)(char*);
using CliPrintBufFuncPtr = void (*)(const char*, ...);

#endif

