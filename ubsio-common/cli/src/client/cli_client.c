#include "cli_client.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    char *items[CLI_HISTORY_MAX];
    int count;
} History;

static int g_raw_enabled = 0;
static struct termios g_old_termios;
static FILE *g_log_file = NULL;

static char *client_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = (char *)malloc(len);
    if (p != NULL) {
        memcpy(p, s, len);
    }
    return p;
}

static void log_write(const char *fmt, ...)
{
    if (g_log_file == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    (void)vfprintf(g_log_file, fmt, ap);
    va_end(ap);
    (void)fflush(g_log_file);
}

static void disable_raw_mode(void)
{
    if (g_raw_enabled) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_termios);
        g_raw_enabled = 0;
    }
}

static int enable_raw_mode(void)
{
    if (!isatty(STDIN_FILENO)) {
        return RETURN_ERROR;
    }
    if (tcgetattr(STDIN_FILENO, &g_old_termios) != 0) {
        return RETURN_ERROR;
    }
    struct termios raw = g_old_termios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return RETURN_ERROR;
    }
    g_raw_enabled = 1;
    atexit(disable_raw_mode);
    return RETURN_OK;
}

static void history_add(History *history, const char *line)
{
    if (history == NULL || line == NULL || line[0] == '\0') {
        return;
    }
    if (history->count > 0 && strcmp(history->items[history->count - 1], line) == 0) {
        return;
    }
    if (history->count == CLI_HISTORY_MAX) {
        free(history->items[0]);
        memmove(&history->items[0], &history->items[1], sizeof(history->items[0]) * (CLI_HISTORY_MAX - 1));
        history->count--;
    }
    history->items[history->count++] = client_strdup(line);
}

static void redraw_line(const char *prompt, const char *buf, size_t len, size_t cursor)
{
    (void)write(STDOUT_FILENO, "\r", 1);
    if (prompt != NULL) {
        (void)write(STDOUT_FILENO, prompt, strlen(prompt));
    }
    if (len != 0) {
        (void)write(STDOUT_FILENO, buf, len);
    }
    (void)write(STDOUT_FILENO, "\x1b[K", 3);
    if (len > cursor) {
        char seq[32];
        int n = snprintf(seq, sizeof(seq), "\x1b[%zuD", len - cursor);
        if (n > 0) {
            (void)write(STDOUT_FILENO, seq, (size_t)n);
        }
    }
}

static int read_byte(char *ch)
{
    ssize_t n;
    do {
        n = read(STDIN_FILENO, ch, 1);
    } while (n < 0 && errno == EINTR);
    return n == 1 ? RETURN_OK : RETURN_ERROR;
}

static void set_buffer(char *buf, size_t cap, size_t *len, size_t *cursor, const char *text)
{
    size_t n = strlen(text);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(buf, text, n);
    buf[n] = '\0';
    *len = n;
    *cursor = n;
}

static int read_line_raw(const char *prompt, char *out, size_t out_size, History *history, int save_history)
{
    char buf[CLI_MAX_LINE];
    size_t len = 0;
    size_t cursor = 0;
    int history_pos = -1;
    char editing_backup[CLI_MAX_LINE] = {0};

    if (prompt != NULL) {
        (void)write(STDOUT_FILENO, prompt, strlen(prompt));
    }

    for (;;) {
        char ch = 0;
        if (read_byte(&ch) != RETURN_OK) {
            return RETURN_ERROR;
        }

        if (ch == '\r' || ch == '\n') {
            (void)write(STDOUT_FILENO, "\n", 1);
            buf[len] = '\0';
            if (out_size != 0) {
                size_t n = len < out_size - 1 ? len : out_size - 1;
                memcpy(out, buf, n);
                out[n] = '\0';
            }
            if (save_history) {
                history_add(history, buf);
            }
            return RETURN_OK;
        }

        if (ch == 3) {
            (void)write(STDOUT_FILENO, "^C\n", 3);
            out[0] = '\0';
            return RETURN_ERROR;
        }

        if (ch == 1) {
            cursor = 0;
            redraw_line(prompt, buf, len, cursor);
            continue;
        }
        if (ch == 5) {
            cursor = len;
            redraw_line(prompt, buf, len, cursor);
            continue;
        }

        if (ch == 127 || ch == 8) {
            if (cursor > 0) {
                memmove(&buf[cursor - 1], &buf[cursor], len - cursor);
                cursor--;
                len--;
                buf[len] = '\0';
                redraw_line(prompt, buf, len, cursor);
            }
            continue;
        }

        if (ch == 27) {
            char seq[3] = {0};
            if (read_byte(&seq[0]) != RETURN_OK || read_byte(&seq[1]) != RETURN_OK) {
                continue;
            }
            if (seq[0] != '[') {
                continue;
            }
            if (seq[1] == 'A') {
                if (history != NULL && history->count > 0) {
                    if (history_pos == -1) {
                        memcpy(editing_backup, buf, len);
                        editing_backup[len] = '\0';
                        history_pos = history->count - 1;
                    } else if (history_pos > 0) {
                        history_pos--;
                    }
                    set_buffer(buf, sizeof(buf), &len, &cursor, history->items[history_pos]);
                    redraw_line(prompt, buf, len, cursor);
                }
            } else if (seq[1] == 'B') {
                if (history_pos != -1) {
                    if (history_pos < history->count - 1) {
                        history_pos++;
                        set_buffer(buf, sizeof(buf), &len, &cursor, history->items[history_pos]);
                    } else {
                        history_pos = -1;
                        set_buffer(buf, sizeof(buf), &len, &cursor, editing_backup);
                    }
                    redraw_line(prompt, buf, len, cursor);
                }
            } else if (seq[1] == 'C') {
                if (cursor < len) {
                    cursor++;
                    redraw_line(prompt, buf, len, cursor);
                }
            } else if (seq[1] == 'D') {
                if (cursor > 0) {
                    cursor--;
                    redraw_line(prompt, buf, len, cursor);
                }
            } else if (seq[1] == 'H') {
                cursor = 0;
                redraw_line(prompt, buf, len, cursor);
            } else if (seq[1] == 'F') {
                cursor = len;
                redraw_line(prompt, buf, len, cursor);
            } else if (seq[1] == '3') {
                char tail = 0;
                if (read_byte(&tail) == RETURN_OK && tail == '~' && cursor < len) {
                    memmove(&buf[cursor], &buf[cursor + 1], len - cursor - 1);
                    len--;
                    buf[len] = '\0';
                    redraw_line(prompt, buf, len, cursor);
                }
            }
            continue;
        }

        if (isprint((unsigned char)ch) && len + 1 < sizeof(buf)) {
            memmove(&buf[cursor + 1], &buf[cursor], len - cursor);
            buf[cursor] = ch;
            len++;
            cursor++;
            buf[len] = '\0';
            redraw_line(prompt, buf, len, cursor);
        }
    }
}

static int read_line_plain(FILE *input, const char *prompt, char *out, size_t out_size)
{
    if (prompt != NULL && isatty(STDIN_FILENO)) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    if (fgets(out, (int)out_size, input) == NULL) {
        return RETURN_ERROR;
    }
    out[strcspn(out, "\r\n")] = '\0';
    return RETURN_OK;
}

static int read_command_line(FILE *input, const char *prompt, char *out, size_t out_size,
                             History *history, int save_history)
{
    if (input == stdin && g_raw_enabled) {
        return read_line_raw(prompt, out, out_size, history, save_history);
    }
    return read_line_plain(input, prompt, out, out_size);
}

static void usage(void)
{
    printf("%s",
        "Usage: cli_client [options]\n"
        "  --help                : print help informations\n"
        "  --auto                : auto run mode\n"
        "  --script=<filename>   : read command from <filename> instead of stdin\n"
        "  --server-port=<port>  : connect to server port\n"
        "  --log-file=<filename> : write received output to file\n"
        "  --set-debug           : accepted for compatibility\n"
        "  --set-cli             : accepted for compatibility\n"
        "  --no-prompt           : do not show a command line prompt\n"
        "  --attach=<AppId>      : attach after login\n");
}

static int parse_args(ClientConfig *cfg, int argc, char *argv[])
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = CLI_DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            exit(0);
        } else if (strcmp(argv[i], "--auto") == 0) {
            cfg->auto_mode = 1;
        } else if (strcmp(argv[i], "--no-prompt") == 0) {
            cfg->no_prompt = 1;
        } else if (strcmp(argv[i], "--set-cli") == 0 || strcmp(argv[i], "--set-debug") == 0) {
            continue;
        } else if (strncmp(argv[i], "--script=", 9) == 0) {
            (void)snprintf(cfg->script_file, sizeof(cfg->script_file), "%s", argv[i] + 9);
        } else if (strncmp(argv[i], "--log-file=", 11) == 0) {
            (void)snprintf(cfg->log_file, sizeof(cfg->log_file), "%s", argv[i] + 11);
        } else if (strncmp(argv[i], "--server-port=", 14) == 0) {
            cfg->port = cli_parse_port_arg(argv[i] + 14, cfg->port);
        } else if (strncmp(argv[i], "--attach=", 9) == 0) {
            if (cli_parse_u32(argv[i] + 9, &cfg->attach_id) != RETURN_OK) {
                fprintf(stderr, "Invalid attach id: %s\n", argv[i] + 9);
                return RETURN_ERROR;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return RETURN_ERROR;
        }
    }
    return RETURN_OK;
}

static int client_wait_command(int fd, uint32_t *client_id, uint32_t *attach_id, History *history)
{
    for (;;) {
        CliFrame frame;
        if (cli_recv_frame(fd, &frame) != RETURN_OK) {
            fprintf(stderr, "cli_client: disconnected from server\n");
            return RETURN_ERROR;
        }

        if (frame.header.client_id != 0) {
            *client_id = frame.header.client_id;
        }
        *attach_id = frame.header.agent_id;

        switch (frame.header.type) {
            case CLI_FRAME_DATA:
                if (frame.header.length != 0) {
                    (void)fwrite(frame.data, 1, frame.header.length, stdout);
                    (void)fflush(stdout);
                    log_write("%.*s", (int)frame.header.length, frame.data);
                }
                break;
            case CLI_FRAME_PROMPT: {
                char input[CLI_MAX_LINE];
                const char *prompt = frame.data[0] == '\0' ? ":" : frame.data;
                if (read_command_line(stdin, prompt, input, sizeof(input), history, 0) != RETURN_OK) {
                    input[0] = '\0';
                }
                if (cli_send_text(fd, CLI_FRAME_PROMPT_REPLY, *client_id, *attach_id, input) != RETURN_OK) {
                    return RETURN_ERROR;
                }
                break;
            }
            case CLI_FRAME_DONE:
                return RETURN_OK;
            case CLI_FRAME_CONTROL:
                break;
            default:
                break;
        }
    }
}

static int run_one_command(int fd, uint32_t *client_id, uint32_t *attach_id, const char *line, History *history)
{
    if (cli_send_text(fd, CLI_FRAME_CMD, *client_id, *attach_id, line) != RETURN_OK) {
        fprintf(stderr, "cli_client: failed to send command\n");
        return RETURN_ERROR;
    }
    return client_wait_command(fd, client_id, attach_id, history);
}

static int run_script_command(int fd, uint32_t *client_id, uint32_t *attach_id,
                              const char *file_name, History *history)
{
    FILE *fp = fopen(file_name, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: Can't open file `%s` for read\n", file_name);
        return RETURN_OK;
    }

    char line[CLI_MAX_LINE];
    while (read_line_plain(fp, NULL, line, sizeof(line)) == RETURN_OK) {
        if (line[0] == '\0') {
            continue;
        }
        printf("%s\n", line);
        if (run_one_command(fd, client_id, attach_id, line, history) != RETURN_OK) {
            fclose(fp);
            return RETURN_ERROR;
        }
    }
    fclose(fp);
    return RETURN_OK;
}

static int handle_local_command(int fd, uint32_t *client_id, uint32_t *attach_id,
                                char *line, History *history, int *should_exit)
{
    char copy[CLI_MAX_LINE];
    (void)snprintf(copy, sizeof(copy), "%s", line);
    CliArgs args;
    if (cli_split_args(copy, &args) != RETURN_OK || args.argc == 0) {
        return RETURN_OK;
    }

    char command[CLI_MAX_LINE];
    (void)snprintf(command, sizeof(command), "%s", args.argv[0]);
    cli_lower(command);

    if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
        *should_exit = 1;
        return RETURN_OK;
    }

    if (strcmp(command, ".script") == 0) {
        if (args.argc != 2) {
            printf("Usage: .script <filename>\n");
            return RETURN_OK;
        }
        return run_script_command(fd, client_id, attach_id, args.argv[1], history);
    }

    return run_one_command(fd, client_id, attach_id, line, history);
}

int main(int argc, char *argv[])
{
    (void)signal(SIGPIPE, SIG_IGN);

    ClientConfig cfg;
    if (parse_args(&cfg, argc, argv) != RETURN_OK) {
        return 1;
    }

    if (cfg.log_file[0] != '\0') {
        g_log_file = fopen(cfg.log_file, "a");
        if (g_log_file == NULL) {
            fprintf(stderr, "cli_client: cannot open log file %s: %s\n", cfg.log_file, strerror(errno));
        }
    }

    int fd = cli_connect_server(cfg.port);
    if (fd < 0) {
        fprintf(stderr, "cli_client: cannot connect to cli_server on 127.0.0.1:%u\n", cfg.port);
        return 1;
    }

    if (cli_send_text(fd, CLI_FRAME_HELLO_CLIENT, 0, cfg.attach_id, "cli_client") != RETURN_OK) {
        close(fd);
        return 1;
    }

    uint32_t client_id = 0;
    uint32_t attach_id = cfg.attach_id;
    CliFrame hello;
    if (cli_recv_frame(fd, &hello) != RETURN_OK || hello.header.type != CLI_FRAME_CONTROL) {
        fprintf(stderr, "cli_client: login failed\n");
        close(fd);
        return 1;
    }
    client_id = hello.header.client_id;
    attach_id = hello.header.agent_id;

    FILE *input = stdin;
    if (cfg.script_file[0] != '\0') {
        input = fopen(cfg.script_file, "r");
        if (input == NULL) {
            fprintf(stderr, "Can't open script file<%s>\n", cfg.script_file);
            close(fd);
            return 1;
        }
    } else if (isatty(STDIN_FILENO)) {
        (void)enable_raw_mode();
    }

    History history;
    memset(&history, 0, sizeof(history));

    int should_exit = 0;
    char line[CLI_MAX_LINE];
    while (!should_exit) {
        char prompt[96];
        const char *prompt_text = NULL;
        if (!cfg.no_prompt && input == stdin) {
            if (attach_id == CLI_INVALID_ID) {
                prompt_text = "cli> ";
            } else {
                (void)snprintf(prompt, sizeof(prompt), "%u:/cli> ", attach_id);
                prompt_text = prompt;
            }
        }

        if (read_command_line(input, prompt_text, line, sizeof(line), &history, input == stdin) != RETURN_OK) {
            break;
        }
        if (input != stdin && line[0] != '\0') {
            printf("%s\n", line);
        }
        if (handle_local_command(fd, &client_id, &attach_id, line, &history, &should_exit) != RETURN_OK) {
            break;
        }
    }

    (void)cli_send_frame(fd, CLI_FRAME_BYE, client_id, attach_id, NULL, 0);
    close(fd);
    if (input != stdin) {
        fclose(input);
    }
    if (g_log_file != NULL) {
        fclose(g_log_file);
    }
    for (int i = 0; i < history.count; i++) {
        free(history.items[i]);
    }
    return 0;
}
