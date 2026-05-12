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
