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

#include "cli_server.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static CliConn g_conn[CLI_SERVER_MAX_CONN];
static uint32_t g_next_client_id = 1;

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

static void server_usage(void)
{
    printf("Usage: cli_server [--server-port=<port>]\n");
}

static void conn_reset(CliConn *conn)
{
    if (conn->fd >= 0) {
        close(conn->fd);
    }
    memset(conn, 0, sizeof(*conn));
    conn->fd = -1;
}

static void conn_table_init(void)
{
    for (int i = 0; i < CLI_SERVER_MAX_CONN; i++) {
        g_conn[i].fd = -1;
    }
}

static CliConn *conn_alloc(int fd)
{
    for (int i = 0; i < CLI_SERVER_MAX_CONN; i++) {
        if (g_conn[i].fd < 0) {
            memset(&g_conn[i], 0, sizeof(g_conn[i]));
            g_conn[i].fd = fd;
            g_conn[i].kind = CONN_UNKNOWN;
            return &g_conn[i];
        }
    }
    return NULL;
}

static CliConn *find_client(uint32_t id)
{
    for (int i = 0; i < CLI_SERVER_MAX_CONN; i++) {
        if (g_conn[i].fd >= 0 && g_conn[i].kind == CONN_CLIENT && g_conn[i].id == id) {
            return &g_conn[i];
        }
    }
    return NULL;
}

static CliConn *find_agent(uint32_t id)
{
    for (int i = 0; i < CLI_SERVER_MAX_CONN; i++) {
        if (g_conn[i].fd >= 0 && g_conn[i].kind == CONN_AGENT && g_conn[i].id == id) {
            return &g_conn[i];
        }
    }
    return NULL;
}

static void drop_duplicate_agent(CliConn *self, uint32_t id)
{
    for (int i = 0; i < CLI_SERVER_MAX_CONN; i++) {
        if (&g_conn[i] != self && g_conn[i].fd >= 0 && g_conn[i].kind == CONN_AGENT &&
            g_conn[i].id == id) {
            conn_reset(&g_conn[i]);
        }
    }
}

static void send_done(CliConn *client)
{
    if (cli_send_frame(client->fd, CLI_FRAME_DONE, client->id, client->attached_id, NULL, 0) != RETURN_OK) {
        return;
    }
}

static void send_client_text(CliConn *client, const char *text)
{
    if (cli_send_text(client->fd, CLI_FRAME_DATA, client->id, client->attached_id, text) != RETURN_OK) {
        return;
    }
}

static void handle_ls(CliConn *client)
{
    send_client_text(client, "<AppId>        <State>            <AppName>\n");
    for (int i = 0; i < CLI_SERVER_MAX_CONN; i++) {
        if (g_conn[i].fd >= 0 && g_conn[i].kind == CONN_AGENT) {
            char line[160];
            format_text(line, sizeof(line), " %-16u %-16s %s\n",
                        g_conn[i].id, "online", g_conn[i].name);
            send_client_text(client, line);
        }
    }
    send_done(client);
}

static void handle_server_help(CliConn *client)
{
    send_client_text(client,
        "<command>        <description>\n"
        " ls              list registered Apps\n"
        " attach          attach to AppId, use attach 0 to detach\n"
        " help            show server help, or agent help after attach\n");
    send_done(client);
}

static void handle_attach(CliConn *client, CliArgs *args)
{
    if (args->argc != 2) {
        send_client_text(client, "Usage: attach AppId\n");
        send_done(client);
        return;
    }

    uint32_t id = 0;
    if (cli_parse_u32(args->argv[1], &id) != RETURN_OK) {
        send_client_text(client, "Invalid AppId.\n");
        send_done(client);
        return;
    }

    if (id != CLI_INVALID_ID && find_agent(id) == NULL) {
        send_client_text(client,
            "Error attach: beyond domanial argument.\nUsage: attach AppId.\nTry `ls` to find available AppId.\n");
        send_done(client);
        return;
    }

    client->attached_id = id;
    char line[96];
    if (id == CLI_INVALID_ID) {
        format_text(line, sizeof(line), "Detach success\n");
    } else {
        format_text(line, sizeof(line), "Attach AppId<%u> success\n", id);
    }
    send_client_text(client, line);
    send_done(client);
}

static void forward_to_agent(CliConn *client, const CliFrame *frame)
{
    if (client->attached_id == CLI_INVALID_ID) {
        send_client_text(client, "Please exec `attach <AppId>` first.\n");
        send_done(client);
        return;
    }

    CliConn *agent = find_agent(client->attached_id);
    if (agent == NULL) {
        char line[128];
        format_text(line, sizeof(line),
                    "Can't find attached AppId<%u>. make sure the agent is alive, and try again.\n",
                    client->attached_id);
        send_client_text(client, line);
        send_done(client);
        return;
    }

    if (cli_send_frame(agent->fd, frame->header.type, client->id, agent->id,
                       frame->data, frame->header.length) != RETURN_OK) {
        send_client_text(client, "Socket send failed\n");
        send_done(client);
    }
}

static void handle_client_cmd(CliConn *client, const CliFrame *frame)
{
    char line[CLI_MAX_LINE];
    size_t len = frame->header.length;
    if (len >= sizeof(line)) {
        len = sizeof(line) - 1;
    }
    memcpy(line, frame->data, len);
    line[len] = '\0';

    CliArgs args;
    if (cli_split_args(line, &args) != RETURN_OK || args.argc == 0) {
        send_done(client);
        return;
    }

    char command[CLI_MAX_LINE];
    format_text(command, sizeof(command), "%s", args.argv[0]);
    cli_lower(command);

    if (strcmp(command, "ls") == 0) {
        handle_ls(client);
        return;
    }
    if (strcmp(command, "attach") == 0) {
        handle_attach(client, &args);
        return;
    }
    if (strcmp(command, "help") == 0 && client->attached_id == CLI_INVALID_ID) {
        handle_server_help(client);
        return;
    }

    forward_to_agent(client, frame);
}

static void forward_to_client(const CliFrame *frame)
{
    CliConn *client = find_client(frame->header.client_id);
    if (client == NULL) {
        return;
    }
    if (cli_send_frame(client->fd, frame->header.type, client->id, frame->header.agent_id,
                       frame->data, frame->header.length) != RETURN_OK) {
        return;
    }
}

static void handle_hello_client(CliConn *conn, const CliFrame *frame)
{
    conn->kind = CONN_CLIENT;
    conn->id = g_next_client_id++;
    if (g_next_client_id == CLI_INVALID_ID) {
        g_next_client_id++;
    }
    conn->attached_id = frame->header.agent_id;
    if (conn->attached_id != CLI_INVALID_ID && find_agent(conn->attached_id) == NULL) {
        conn->attached_id = CLI_INVALID_ID;
    }
    if (cli_send_text(conn->fd, CLI_FRAME_CONTROL, conn->id, conn->attached_id, "client ready") != RETURN_OK) {
        conn_reset(conn);
    }
}

static void handle_hello_agent(CliConn *conn, const CliFrame *frame)
{
    if (frame->header.agent_id == CLI_INVALID_ID) {
        conn_reset(conn);
        return;
    }
    drop_duplicate_agent(conn, frame->header.agent_id);
    conn->kind = CONN_AGENT;
    conn->id = frame->header.agent_id;
    format_text(conn->name, sizeof(conn->name), "%s", frame->data[0] == '\0' ? "unknown" : frame->data);
    if (cli_send_text(conn->fd, CLI_FRAME_CONTROL, 0, conn->id, "agent ready") != RETURN_OK) {
        conn_reset(conn);
    }
}

static void handle_frame(CliConn *conn, const CliFrame *frame)
{
    switch (frame->header.type) {
        case CLI_FRAME_HELLO_CLIENT:
            handle_hello_client(conn, frame);
            break;
        case CLI_FRAME_HELLO_AGENT:
            handle_hello_agent(conn, frame);
            break;
        case CLI_FRAME_CMD:
            if (conn->kind == CONN_CLIENT) {
                handle_client_cmd(conn, frame);
            }
            break;
        case CLI_FRAME_PROMPT_REPLY:
            if (conn->kind == CONN_CLIENT) {
                forward_to_agent(conn, frame);
            }
            break;
        case CLI_FRAME_DATA:
        case CLI_FRAME_DONE:
        case CLI_FRAME_PROMPT:
            if (conn->kind == CONN_AGENT) {
                forward_to_client(frame);
            }
            break;
        case CLI_FRAME_BYE:
            conn_reset(conn);
            break;
        default:
            break;
    }
}

static uint16_t parse_server_port(int argc, char *argv[])
{
    uint16_t port = CLI_DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            server_usage();
            exit(0);
        }
        if (strncmp(argv[i], "--server-port=", 14) == 0) {
            port = cli_parse_port_arg(argv[i] + 14, port);
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = cli_parse_port_arg(argv[++i], port);
        }
    }
    return port;
}

int main(int argc, char *argv[])
{
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        fprintf(stderr, "cli_server: failed to ignore SIGPIPE\n");
    }
    conn_table_init();

    uint16_t port = parse_server_port(argc, argv);
    int listen_fd = cli_open_server(port);
    if (listen_fd < 0) {
        fprintf(stderr, "cli_server: failed to listen on 127.0.0.1:%u: %s\n", port, strerror(errno));
        return 1;
    }

    for (;;) {
        struct pollfd pfds[CLI_SERVER_MAX_CONN + 1];
        CliConn *map[CLI_SERVER_MAX_CONN + 1];
        int nfds = 1;
        pfds[0].fd = listen_fd;
        pfds[0].events = POLLIN;
        map[0] = NULL;

        for (int i = 0; i < CLI_SERVER_MAX_CONN; i++) {
            if (g_conn[i].fd >= 0) {
                pfds[nfds].fd = g_conn[i].fd;
                pfds[nfds].events = POLLIN;
                map[nfds] = &g_conn[i];
                nfds++;
            }
        }

        int ret = poll(pfds, (nfds_t)nfds, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if ((pfds[0].revents & POLLIN) != 0) {
            int fd = accept(listen_fd, NULL, NULL);
            if (fd >= 0) {
                CliConn *conn = conn_alloc(fd);
                if (conn == NULL) {
                    close(fd);
                }
            }
        }

        for (int i = 1; i < nfds; i++) {
            CliConn *conn = map[i];
            if (conn == NULL || conn->fd < 0) {
                continue;
            }
            if ((pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                conn_reset(conn);
                continue;
            }
            if ((pfds[i].revents & POLLIN) != 0) {
                CliFrame frame;
                if (cli_recv_frame(conn->fd, &frame) != RETURN_OK) {
                    conn_reset(conn);
                } else {
                    handle_frame(conn, &frame);
                }
            }
        }
    }

    close(listen_fd);
    return 1;
}
