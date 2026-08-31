/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
*
* ubs-io is licensed under the Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*      http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
*/

#ifndef MMS_CLI_AGENT_H
#define MMS_CLI_AGENT_H

#include <pthread.h>

#include "cli.h"
#include "cli_common.h"

#define CLI_AGENT_MAX_COMMANDS 256
#define CLI_AGENT_PRINT_BUFFER (512 * 1024)

typedef struct {
    int used;
    CliCommand cmd;
} AgentCommand;

typedef struct {
    uint32_t client_id;
    uint32_t agent_id;
    CliCommand *current_cmd;
    char print_buf[CLI_AGENT_PRINT_BUFFER];
    size_t print_len;
} AgentCmdContext;

typedef struct {
    int fd;
    uint32_t id;
    char name[64];
    int running;
    int thread_started;
    pthread_t thread;
    pthread_mutex_t lock;
    AgentCommand commands[CLI_AGENT_MAX_COMMANDS];
} AgentState;

#endif
