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

#include "cli_common.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int cli_read_exact(int fd, void *buf, size_t len)
{
    char *p = (char *)buf;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n == 0) {
            return RETURN_ERROR;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return RETURN_ERROR;
        }
        p += n;
        len -= (size_t)n;
    }
    return RETURN_OK;
}

static int cli_write_exact(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return RETURN_ERROR;
        }
        if (n == 0) {
            return RETURN_ERROR;
        }
        p += n;
        len -= (size_t)n;
    }
    return RETURN_OK;
}

int cli_open_server(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return RETURN_ERROR;
    }

    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
        close(fd);
        return RETURN_ERROR;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return RETURN_ERROR;
    }
    if (listen(fd, 64) != 0) {
        close(fd);
        return RETURN_ERROR;
    }
    return fd;
}

int cli_connect_server(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return RETURN_ERROR;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return RETURN_ERROR;
    }
    return fd;
}

int cli_send_frame(int fd, uint16_t type, uint32_t client_id, uint32_t agent_id,
                   const void *data, uint32_t length)
{
    if (length > CLI_MAX_PAYLOAD) {
        return RETURN_ERROR;
    }

    CliFrameHeader header;
    header.magic = htonl(CLI_MAGIC);
    header.version = htons((uint16_t)CLI_VERSION);
    header.type = htons(type);
    header.client_id = htonl(client_id);
    header.agent_id = htonl(agent_id);
    header.length = htonl(length);

    if (cli_write_exact(fd, &header, sizeof(header)) != RETURN_OK) {
        return RETURN_ERROR;
    }
    if (length != 0 && data != NULL) {
        return cli_write_exact(fd, data, length);
    }
    return RETURN_OK;
}

int cli_send_text(int fd, uint16_t type, uint32_t client_id, uint32_t agent_id,
                  const char *text)
{
    if (text == NULL) {
        text = "";
    }
    size_t len = strlen(text);
    if (len > CLI_MAX_PAYLOAD) {
        len = CLI_MAX_PAYLOAD;
    }
    return cli_send_frame(fd, type, client_id, agent_id, text, (uint32_t)len);
}

int cli_recv_frame(int fd, CliFrame *frame)
{
    if (frame == NULL) {
        return RETURN_ERROR;
    }
    if (cli_read_exact(fd, &frame->header, sizeof(frame->header)) != RETURN_OK) {
        return RETURN_ERROR;
    }

    frame->header.magic = ntohl(frame->header.magic);
    frame->header.version = ntohs(frame->header.version);
    frame->header.type = ntohs(frame->header.type);
    frame->header.client_id = ntohl(frame->header.client_id);
    frame->header.agent_id = ntohl(frame->header.agent_id);
    frame->header.length = ntohl(frame->header.length);

    if (frame->header.magic != CLI_MAGIC || frame->header.version != CLI_VERSION ||
        frame->header.length > CLI_MAX_PAYLOAD) {
        return RETURN_ERROR;
    }

    if (frame->header.length != 0) {
        if (cli_read_exact(fd, frame->data, frame->header.length) != RETURN_OK) {
            return RETURN_ERROR;
        }
    }
    frame->data[frame->header.length] = '\0';
    return RETURN_OK;
}

int cli_split_args(char *line, CliArgs *args)
{
    if (line == NULL || args == NULL) {
        return RETURN_ERROR;
    }
    args->argc = 0;

    char *src = line;
    while (*src != '\0') {
        while (isspace((unsigned char)*src)) {
            src++;
        }
        if (*src == '\0') {
            break;
        }
        if (args->argc >= CLI_MAX_ARGS) {
            return RETURN_ERROR;
        }

        args->argv[args->argc++] = src;
        char quote = '\0';
        char *dst = src;
        while (*src != '\0') {
            char ch = *src++;
            if (quote != '\0') {
                if (ch == quote) {
                    quote = '\0';
                    continue;
                }
                if (ch == '\\' && *src != '\0') {
                    ch = *src++;
                }
                *dst++ = ch;
                continue;
            }
            if (ch == '\'' || ch == '"') {
                quote = ch;
                continue;
            }
            if (isspace((unsigned char)ch)) {
                break;
            }
            if (ch == '\\' && *src != '\0') {
                ch = *src++;
            }
            *dst++ = ch;
        }
        *dst = '\0';
        while (isspace((unsigned char)*src)) {
            src++;
        }
    }
    return RETURN_OK;
}

void cli_lower(char *s)
{
    if (s == NULL) {
        return;
    }
    while (*s != '\0') {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

int cli_parse_u32(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || *text == '\0') {
        return RETURN_ERROR;
    }
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || v > UINT32_MAX) {
        return RETURN_ERROR;
    }
    *value = (uint32_t)v;
    return RETURN_OK;
}

uint16_t cli_parse_port_arg(const char *arg, uint16_t fallback)
{
    uint32_t value = 0;
    if (cli_parse_u32(arg, &value) != RETURN_OK || value == 0 || value > UINT16_MAX) {
        return fallback;
    }
    return (uint16_t)value;
}

void cli_sleep_ms(unsigned int ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}
