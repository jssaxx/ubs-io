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

#ifndef MMS_CLI_COMMON_H
#define MMS_CLI_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "cli_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_DEFAULT_PORT 7002
#define CLI_MAGIC 0x55425343u
#define CLI_VERSION 1u
#define CLI_MAX_PAYLOAD 65536u
#define CLI_MAX_LINE 4096
#define CLI_MAX_ARGS 128
#define CLI_INVALID_ID 0u

typedef enum {
    CLI_FRAME_HELLO_CLIENT = 1,
    CLI_FRAME_HELLO_AGENT = 2,
    CLI_FRAME_CONTROL = 3,
    CLI_FRAME_CMD = 4,
    CLI_FRAME_DATA = 5,
    CLI_FRAME_DONE = 6,
    CLI_FRAME_PROMPT = 7,
    CLI_FRAME_PROMPT_REPLY = 8,
    CLI_FRAME_BYE = 9
} CliFrameType;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t client_id;
    uint32_t agent_id;
    uint32_t length;
} CliFrameHeader;

typedef struct {
    CliFrameHeader header;
    char data[CLI_MAX_PAYLOAD + 1];
} CliFrame;

typedef struct {
    int argc;
    char *argv[CLI_MAX_ARGS];
} CliArgs;

int cli_open_server(uint16_t port);
int cli_connect_server(uint16_t port);
int cli_send_frame(int fd, uint16_t type, uint32_t client_id, uint32_t agent_id,
                   const void *data, uint32_t length);
int cli_send_text(int fd, uint16_t type, uint32_t client_id, uint32_t agent_id,
                  const char *text);
int cli_recv_frame(int fd, CliFrame *frame);
int cli_split_args(char *line, CliArgs *args);
void cli_lower(char *s);
int cli_parse_u32(const char *text, uint32_t *value);
uint16_t cli_parse_port_arg(const char *arg, uint16_t fallback);
void cli_sleep_ms(unsigned int ms);

#ifdef __cplusplus
}
#endif

#endif
