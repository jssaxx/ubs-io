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

#ifndef MMS_CLI_H
#define MMS_CLI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RETURN_OK
#define RETURN_OK 0
#endif

#ifndef RETURN_ERROR
#define RETURN_ERROR (-1)
#endif

int32_t cli_agent_init(uint32_t current_pid, char *app_name);
void cli_agent_sync_status(void);
int32_t cli_agent_destroy(uint32_t current_pid);

#define CLI_RET_UNKNOWN_ARG   1

#define CLI_MAX_COMMAND_LEN   20
#define CLI_MAX_CMD_DESC_LEN  64
#define CLI_MAX_ERR_MSG_LEN   80

typedef void (*CliCommandHandler)(int32_t argc, char *argv[]);
typedef void (*CliCommandHelpHandler)(char *command, int32_t show_detail);

typedef struct {
    char command[CLI_MAX_COMMAND_LEN];
    char description[CLI_MAX_CMD_DESC_LEN];
    CliCommandHandler handler;
    CliCommandHelpHandler help_handler;
} CliCommand;

typedef struct {
    char *option_arg;
    int32_t option_index;
    char option;
    char error_msg[CLI_MAX_ERR_MSG_LEN];
} CliOption;

void cli_print(const char *format, ...);
void cli_print_buffer(const char *format, ...);
void cli_send_buffer(const char *buffer, uint32_t size);
void cli_show_usage_and_error(const char *format, ...);
int32_t cli_get_input(char *prompt, char *input, uint32_t max_input_len);
int32_t cli_register_command(CliCommand *cmd_info);
void cli_unregister_command(char *command_name);

int32_t cli_get_option(int32_t argc, char *argv[], const char *option_string, CliOption *opt_info);
void cli_set_option(uint32_t *opt_bits, int32_t option_value);
int32_t cli_test_option(uint32_t opt_bits, int32_t option_value);
int32_t cli_get_param_u64(const char *param, uint64_t *data);
int32_t cli_get_param_u32(const char *param, uint32_t *data);
int32_t cli_get_param_pointer(const char *param, void **pointer);
void cli_print_memory(void *addr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
