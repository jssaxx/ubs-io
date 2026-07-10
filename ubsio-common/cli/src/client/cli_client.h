#ifndef MMS_CLI_CLIENT_H
#define MMS_CLI_CLIENT_H

#include "cli_common.h"

#define CLI_HISTORY_MAX 100

typedef struct {
    int no_prompt;
    int auto_mode;
    uint16_t port;
    uint32_t attach_id;
    char script_file[512];
    char log_file[512];
} ClientConfig;

#endif
