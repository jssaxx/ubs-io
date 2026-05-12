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

#include "cm_server_init.h"
#include "cm_module.h"
#include "cm_config.h"
#include "cm_zkadapter.h"
#include "cm_server_schedule.h"
#include "cm_server_monitor.h"
#include "cm_server_view.h"

static MODULE_DEFINE_S g_cmServerModules[] = {
    { "cm_server_schedule",      CmServerScheduleInit,      CmServerScheduleExit     },
    { "cm_server_zkadapter",     CmServerZkInit,            NULL                     },
    { "cm_server_monitor",       CmServerMonitorInit,       CmServerMonitorExit      },
    { "cm_server_view",          CmServerViewInit,          CmServerViewExit         },
};

int32_t CM_ServerInit(void)
{
    int32_t ret;

    ret = CmConfigHasCfgPoolS();
    if (ret == FALSE) {
        return CM_OK; // no needed.
    }

    ret = INIT_MODULES(g_cmServerModules);
    return ret;
}

void CM_ServerExit(void)
{
    EXIT_MODULES(g_cmServerModules);
}

