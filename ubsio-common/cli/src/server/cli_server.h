#ifndef MMS_CLI_SERVER_H
#define MMS_CLI_SERVER_H

#include "cli_common.h"

#define CLI_SERVER_MAX_CONN 128

typedef enum {
    CONN_EMPTY = 0,
    CONN_UNKNOWN,
    CONN_CLIENT,
    CONN_AGENT
} ConnKind;

typedef struct {
    int fd;
    ConnKind kind;
    uint32_t id;
    uint32_t attached_id;
    char name[64];
} CliConn;

#endif
