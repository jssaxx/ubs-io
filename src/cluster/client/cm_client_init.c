/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "cm_client_init.h"
#include "cm_module.h"
#include "cm_config.h"
#include "cm_zkadapter.h"
#include "cm_client_schedule.h"
#include "cm_client_local.h"
#include "cm_client_event.h"

static MODULE_DEFINE_S g_cmClientModules[] = {
    { "cm_client_schedule",      CmClientScheduleInit,      CmClientScheduleExit     },
    { "cm_client_zkadapter",     CmClientZkInit,            NULL                     },
    { "cm_client_local",         CmClientLocalInit,         CmClientLocalExit        },
    { "cm_client_event",         CmClientEventInit,         CmClientEventExit        },
};

int32_t CM_ClientInit(void)
{
    int32_t ret;

    ret = CmConfigHasCfgPoolC();
    if (ret == FALSE) {
        return CM_OK; // no needed.
    }

    ret = INIT_MODULES(g_cmClientModules);
    return ret;
}

void CM_ClientExit(void)
{
    EXIT_MODULES(g_cmClientModules);
}

