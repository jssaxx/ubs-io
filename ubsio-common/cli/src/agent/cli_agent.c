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

#include "cli_agent.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static AgentState g_agent = {
    .fd = -1,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

static __thread AgentCmdContext *g_ctx = NULL;

static size_t format_text(char *buf, size_t size, const char *fmt, ...)
{
    if (buf == NULL || size == 0 || fmt == NULL) {
        return 0;
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    if ((size_t)n >= size) {
        return size - 1;
    }
    return (size_t)n;
}

static int write_file_all(FILE *fp, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        size_t n = fwrite(p, 1, len, fp);
        if (n == 0) {
            if (ferror(fp)) {
                clearerr(fp);
            }
            return RETURN_ERROR;
        }
        p += n;
        len -= n;
    }
    return RETURN_OK;
}

static int command_name_valid(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    size_t len = strlen(name);
    if (len <= 1 || len >= CLI_MAX_COMMAND_LEN) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (!isalnum(ch) && ch != '_') {
            return 0;
        }
    }

    static const char *reserved[] = {"help", "ls", "quit", "exit", "attach", NULL};
    for (int i = 0; reserved[i] != NULL; i++) {
        if (strcmp(reserved[i], name) == 0) {
            return 0;
        }
    }
    return 1;
}

static AgentCommand *find_command(const char *name)
{
    for (int i = 0; i < CLI_AGENT_MAX_COMMANDS; i++) {
        if (g_agent.commands[i].used && strcmp(g_agent.commands[i].cmd.command, name) == 0) {
            return &g_agent.commands[i];
        }
    }
    return NULL;
}

static AgentCommand *find_free_command(void)
{
    for (int i = 0; i < CLI_AGENT_MAX_COMMANDS; i++) {
        if (!g_agent.commands[i].used) {
            return &g_agent.commands[i];
        }
    }
    return NULL;
}

static int agent_send(uint16_t type, uint32_t client_id, const void *data, uint32_t len)
{
    int ret;
    pthread_mutex_lock(&g_agent.lock);
    if (g_agent.fd < 0) {
        pthread_mutex_unlock(&g_agent.lock);
        return RETURN_ERROR;
    }
    ret = cli_send_frame(g_agent.fd, type, client_id, g_agent.id, data, len);
    pthread_mutex_unlock(&g_agent.lock);
    return ret;
}

static int agent_send_text(uint16_t type, uint32_t client_id, const char *text)
{
    if (text == NULL) {
        text = "";
    }
    size_t len = strlen(text);
    if (len == 0) {
        return agent_send(type, client_id, NULL, 0);
    }
    while (len > 0) {
        uint32_t chunk = len > CLI_MAX_PAYLOAD ? CLI_MAX_PAYLOAD : (uint32_t)len;
        if (agent_send(type, client_id, text, chunk) != RETURN_OK) {
            return RETURN_ERROR;
        }
        text += chunk;
        len -= chunk;
    }
    return RETURN_OK;
}

static void flush_print_buffer(AgentCmdContext *ctx)
{
    if (ctx == NULL || ctx->print_len == 0) {
        return;
    }

    char *p = ctx->print_buf;
    size_t left = ctx->print_len;
    while (left > 0) {
        uint32_t chunk = left > CLI_MAX_PAYLOAD ? CLI_MAX_PAYLOAD : (uint32_t)left;
        if (agent_send(CLI_FRAME_DATA, ctx->client_id, p, chunk) != RETURN_OK) {
            ctx->print_len = 0;
            return;
        }
        p += chunk;
        left -= chunk;
    }
    ctx->print_len = 0;
}

static void append_print_buffer(AgentCmdContext *ctx, const char *text, size_t len)
{
    if (ctx == NULL || text == NULL || len == 0) {
        return;
    }
    while (len > 0) {
        size_t space = sizeof(ctx->print_buf) - ctx->print_len;
        if (space == 0) {
            flush_print_buffer(ctx);
            space = sizeof(ctx->print_buf);
        }
        size_t chunk = len < space ? len : space;
        memcpy(ctx->print_buf + ctx->print_len, text, chunk);
        ctx->print_len += chunk;
        text += chunk;
        len -= chunk;
    }
}

static void print_agent_help(AgentCmdContext *ctx)
{
    append_print_buffer(ctx, "<command>        <description>\n", strlen("<command>        <description>\n"));
    append_print_buffer(ctx, " help            show help information\n",
        strlen(" help            show help information\n"));
    for (int i = 0; i < CLI_AGENT_MAX_COMMANDS; i++) {
        if (!g_agent.commands[i].used) {
            continue;
        }
        char line[160];
        size_t n = format_text(line, sizeof(line), " %-16s %s\n",
                               g_agent.commands[i].cmd.command,
                               g_agent.commands[i].cmd.description);
        if (n != 0) {
            append_print_buffer(ctx, line, n);
        }
    }
}

static void execute_command(const CliFrame *frame)
{
    char line[CLI_MAX_LINE];
    size_t len = frame->header.length;
    if (len >= sizeof(line)) {
        len = sizeof(line) - 1;
    }
    memcpy(line, frame->data, len);
    line[len] = '\0';

    CliArgs args;
    AgentCmdContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.client_id = frame->header.client_id;
    ctx.agent_id = g_agent.id;
    g_ctx = &ctx;

    if (cli_split_args(line, &args) != RETURN_OK || args.argc == 0) {
        goto done;
    }

    if (strcmp(args.argv[0], "help") == 0) {
        print_agent_help(&ctx);
        goto done;
    }

    AgentCommand *cmd = find_command(args.argv[0]);
    if (cmd == NULL) {
        char msg[128];
        size_t n = format_text(msg, sizeof(msg), "Unknown command: %s\nTry `help`.\n", args.argv[0]);
        if (n != 0) {
            append_print_buffer(&ctx, msg, n);
        }
        goto done;
    }

    ctx.current_cmd = &cmd->cmd;
    cmd->cmd.handler(args.argc, args.argv);

done:
    flush_print_buffer(&ctx);
    if (agent_send(CLI_FRAME_DONE, frame->header.client_id, NULL, 0) != RETURN_OK) {
        g_ctx = NULL;
        return;
    }
    g_ctx = NULL;
}

static void *agent_thread_main(void *arg)
{
    UNREFERENCE_PARAM(arg);

    while (g_agent.running) {
        int fd = cli_connect_server(CLI_DEFAULT_PORT);
        if (fd < 0) {
            cli_sleep_ms(1000);
            continue;
        }

        pthread_mutex_lock(&g_agent.lock);
        if (!g_agent.running) {
            pthread_mutex_unlock(&g_agent.lock);
            close(fd);
            break;
        }
        g_agent.fd = fd;
        pthread_mutex_unlock(&g_agent.lock);

        if (cli_send_text(fd, CLI_FRAME_HELLO_AGENT, 0, g_agent.id, g_agent.name) != RETURN_OK) {
            pthread_mutex_lock(&g_agent.lock);
            if (g_agent.fd == fd) {
                g_agent.fd = -1;
            }
            pthread_mutex_unlock(&g_agent.lock);
            close(fd);
            cli_sleep_ms(1000);
            continue;
        }

        CliFrame ack;
        if (cli_recv_frame(fd, &ack) != RETURN_OK || ack.header.type != CLI_FRAME_CONTROL) {
            pthread_mutex_lock(&g_agent.lock);
            if (g_agent.fd == fd) {
                g_agent.fd = -1;
            }
            pthread_mutex_unlock(&g_agent.lock);
            close(fd);
            cli_sleep_ms(1000);
            continue;
        }

        while (g_agent.running) {
            CliFrame frame;
            if (cli_recv_frame(fd, &frame) != RETURN_OK) {
                break;
            }
            if (frame.header.type == CLI_FRAME_CMD) {
                execute_command(&frame);
            } else if (frame.header.type == CLI_FRAME_BYE) {
                break;
            }
        }

        pthread_mutex_lock(&g_agent.lock);
        if (g_agent.fd == fd) {
            g_agent.fd = -1;
        }
        pthread_mutex_unlock(&g_agent.lock);
        close(fd);
        cli_sleep_ms(1000);
    }

    pthread_mutex_lock(&g_agent.lock);
    g_agent.thread_started = 0;
    pthread_mutex_unlock(&g_agent.lock);
    return NULL;
}

int32_t cli_agent_init(uint32_t current_pid, char *app_name)
{
    if (current_pid == CLI_INVALID_ID || app_name == NULL) {
        return RETURN_ERROR;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return RETURN_ERROR;
    }
    pthread_mutex_lock(&g_agent.lock);
    if (g_agent.running) {
        pthread_mutex_unlock(&g_agent.lock);
        return RETURN_OK;
    }

    g_agent.fd = -1;
    g_agent.id = current_pid;
    format_text(g_agent.name, sizeof(g_agent.name), "%s", app_name);
    g_agent.running = 1;

    int ret = pthread_create(&g_agent.thread, NULL, agent_thread_main, NULL);
    if (ret != 0) {
        g_agent.fd = -1;
        g_agent.running = 0;
        pthread_mutex_unlock(&g_agent.lock);
        return RETURN_ERROR;
    }
    g_agent.thread_started = 1;
    ret = pthread_detach(g_agent.thread);
    if (ret != 0) {
        g_agent.running = 0;
        g_agent.thread_started = 0;
        pthread_mutex_unlock(&g_agent.lock);
        return RETURN_ERROR;
    }
    pthread_mutex_unlock(&g_agent.lock);
    return RETURN_OK;
}

void cli_agent_sync_status(void)
{
}

int32_t cli_agent_destroy(uint32_t current_pid)
{
    pthread_mutex_lock(&g_agent.lock);
    if (!g_agent.running || g_agent.id != current_pid) {
        pthread_mutex_unlock(&g_agent.lock);
        return RETURN_OK;
    }
    g_agent.running = 0;
    if (g_agent.fd >= 0) {
        if (cli_send_frame(g_agent.fd, CLI_FRAME_BYE, 0, g_agent.id, NULL, 0) != RETURN_OK) {
            shutdown(g_agent.fd, SHUT_RDWR);
            close(g_agent.fd);
            g_agent.fd = -1;
            pthread_mutex_unlock(&g_agent.lock);
            return RETURN_OK;
        }
        shutdown(g_agent.fd, SHUT_RDWR);
        close(g_agent.fd);
        g_agent.fd = -1;
    }
    pthread_mutex_unlock(&g_agent.lock);
    return RETURN_OK;
}

void cli_print(const char *format, ...)
{
    if (format == NULL) {
        return;
    }
    char buffer[CLI_MAX_PAYLOAD];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    size_t len = (size_t)n;
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    if (g_ctx == NULL) {
        if (write_file_all(stderr, buffer, len) != RETURN_OK) {
            return;
        }
        return;
    }
    flush_print_buffer(g_ctx);
    if (agent_send(CLI_FRAME_DATA, g_ctx->client_id, buffer, (uint32_t)len) != RETURN_OK) {
        return;
    }
}

void cli_print_buffer(const char *format, ...)
{
    if (format == NULL) {
        return;
    }
    char buffer[8192];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    size_t len = (size_t)n;
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    if (g_ctx == NULL) {
        if (write_file_all(stderr, buffer, len) != RETURN_OK) {
            return;
        }
        return;
    }
    append_print_buffer(g_ctx, buffer, len);
}

void cli_send_buffer(const char *buffer, uint32_t size)
{
    if (buffer == NULL || size == 0 || g_ctx == NULL) {
        return;
    }
    flush_print_buffer(g_ctx);
    const char *p = buffer;
    uint32_t left = size;
    while (left > 0) {
        uint32_t chunk = left > CLI_MAX_PAYLOAD ? CLI_MAX_PAYLOAD : left;
        if (agent_send(CLI_FRAME_DATA, g_ctx->client_id, p, chunk) != RETURN_OK) {
            return;
        }
        p += chunk;
        left -= chunk;
    }
}

void cli_show_usage_and_error(const char *format, ...)
{
    if (format != NULL) {
        char buffer[1024];
        va_list ap;
        va_start(ap, format);
        int n = vsnprintf(buffer, sizeof(buffer), format, ap);
        va_end(ap);
        if (n > 0) {
            size_t len = (size_t)n >= sizeof(buffer) ? sizeof(buffer) - 1 : (size_t)n;
            append_print_buffer(g_ctx, buffer, len);
            if (len == 0 || buffer[len - 1] != '\n') {
                append_print_buffer(g_ctx, "\n", 1);
            }
        }
    }

    if (g_ctx != NULL && g_ctx->current_cmd != NULL && g_ctx->current_cmd->help_handler != NULL) {
        g_ctx->current_cmd->help_handler(g_ctx->current_cmd->command, 0);
    }
}

int32_t cli_get_input(char *prompt, char *input, uint32_t max_input_len)
{
    if (input == NULL || max_input_len == 0 || g_ctx == NULL) {
        return RETURN_ERROR;
    }
    flush_print_buffer(g_ctx);
    if (agent_send_text(CLI_FRAME_PROMPT, g_ctx->client_id, prompt == NULL ? ":" : prompt) != RETURN_OK) {
        return RETURN_ERROR;
    }

    for (;;) {
        CliFrame frame;
        if (cli_recv_frame(g_agent.fd, &frame) != RETURN_OK) {
            return RETURN_ERROR;
        }
        if (frame.header.type != CLI_FRAME_PROMPT_REPLY || frame.header.client_id != g_ctx->client_id) {
            continue;
        }
        uint32_t len = frame.header.length;
        if (len >= max_input_len) {
            len = max_input_len - 1;
        }
        memcpy(input, frame.data, len);
        input[len] = '\0';
        return RETURN_OK;
    }
}

int32_t cli_register_command(CliCommand *cmd_info)
{
    if (cmd_info == NULL || cmd_info->handler == NULL || !command_name_valid(cmd_info->command)) {
        return RETURN_ERROR;
    }

    pthread_mutex_lock(&g_agent.lock);
    if (find_command(cmd_info->command) != NULL) {
        pthread_mutex_unlock(&g_agent.lock);
        return RETURN_ERROR;
    }
    AgentCommand *slot = find_free_command();
    if (slot == NULL) {
        pthread_mutex_unlock(&g_agent.lock);
        return RETURN_ERROR;
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->cmd = *cmd_info;
    slot->cmd.command[CLI_MAX_COMMAND_LEN - 1] = '\0';
    slot->cmd.description[CLI_MAX_CMD_DESC_LEN - 1] = '\0';
    pthread_mutex_unlock(&g_agent.lock);
    return RETURN_OK;
}

void cli_unregister_command(char *command_name)
{
    if (command_name == NULL) {
        return;
    }
    pthread_mutex_lock(&g_agent.lock);
    AgentCommand *cmd = find_command(command_name);
    if (cmd != NULL) {
        memset(cmd, 0, sizeof(*cmd));
    }
    pthread_mutex_unlock(&g_agent.lock);
}

int32_t cli_get_option(int32_t argc, char *argv[], const char *option_string, CliOption *opt_info)
{
    if (argv == NULL || option_string == NULL || opt_info == NULL) {
        return -1;
    }

    int index = opt_info->option_index <= 0 ? 1 : opt_info->option_index;
    opt_info->option_arg = NULL;
    opt_info->option = '\0';
    opt_info->error_msg[0] = '\0';

    if (index >= argc) {
        opt_info->option_index = index;
        return -1;
    }

    char *arg = argv[index];
    if (arg == NULL || arg[0] != '-' || arg[1] == '\0') {
        opt_info->option_arg = arg;
        opt_info->option_index = index + 1;
        return CLI_RET_UNKNOWN_ARG;
    }

    char opt = arg[1];
    const char *found = strchr(option_string, opt);
    if (found == NULL) {
        opt_info->option = opt;
        format_text(opt_info->error_msg, sizeof(opt_info->error_msg), "invalid option -- '%c'", opt);
        opt_info->option_index = index + 1;
        return '?';
    }

    if (found[1] == ':') {
        if (arg[2] != '\0') {
            opt_info->option_arg = &arg[2];
            opt_info->option_index = index + 1;
        } else if (index + 1 < argc) {
            opt_info->option_arg = argv[index + 1];
            opt_info->option_index = index + 2;
        } else {
            opt_info->option = opt;
            format_text(opt_info->error_msg, sizeof(opt_info->error_msg),
                        "option requires an argument -- '%c'", opt);
            opt_info->option_index = index + 1;
            return '?';
        }
    } else {
        opt_info->option_index = index + 1;
    }

    return opt;
}

void cli_set_option(uint32_t *opt_bits, int32_t option_value)
{
    if (opt_bits == NULL || option_value < 'a' || option_value > 'z') {
        return;
    }
    *opt_bits |= (uint32_t)1u << (uint32_t)(option_value - 'a');
}

int32_t cli_test_option(uint32_t opt_bits, int32_t option_value)
{
    if (option_value < 'a' || option_value > 'z') {
        return 0;
    }
    return (opt_bits & ((uint32_t)1u << (uint32_t)(option_value - 'a'))) != 0;
}

int32_t cli_get_param_u64(const char *param, uint64_t *data)
{
    if (param == NULL || data == NULL || param[0] == '\0') {
        return RETURN_ERROR;
    }
    char *end = NULL;
    errno = 0;
    uint64_t value = strtoull(param, &end, 0);
    if (errno != 0 || end == param || *end != '\0') {
        return RETURN_ERROR;
    }
    *data = value;
    return RETURN_OK;
}

int32_t cli_get_param_u32(const char *param, uint32_t *data)
{
    uint64_t value = 0;
    if (data == NULL || cli_get_param_u64(param, &value) != RETURN_OK || value > UINT32_MAX) {
        return RETURN_ERROR;
    }
    *data = (uint32_t)value;
    return RETURN_OK;
}

int32_t cli_get_param_pointer(const char *param, void **pointer)
{
    uint64_t value = 0;
    if (pointer == NULL || cli_get_param_u64(param, &value) != RETURN_OK) {
        return RETURN_ERROR;
    }
    *pointer = (void *)(uintptr_t)value;
    return RETURN_OK;
}

void cli_print_memory(void *addr, uint32_t len)
{
    if (addr == NULL || len == 0) {
        return;
    }

    const unsigned char *p = (const unsigned char *)addr;
    for (uint32_t offset = 0; offset < len; offset += 16) {
        cli_print_buffer("%08x: ", offset);
        uint32_t line_len = len - offset;
        if (line_len > 16) {
            line_len = 16;
        }
        for (uint32_t i = 0; i < line_len; i++) {
            cli_print_buffer("%02x ", p[offset + i]);
        }
        cli_print_buffer("\n");
    }
}
