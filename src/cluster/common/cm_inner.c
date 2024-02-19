/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "cm_server_init.h"
#include "cm_client_init.h"
#include "cm_module.h"
#include "cm_config.h"
#include "cm_zkadapter.h"

static MODULE_DEFINE_S g_cmModules[] = {
    { "cm_server_init",          CM_ServerInit,             CM_ServerExit    },
    { "cm_client_init",          CM_ClientInit,             CM_ClientExit    },
};

int32_t CM_Init(ConfigRole role, PoolInfo *pools, uint16_t num, const CmCfgInfo *cfgInfo)
{
    int32_t ret;

    ret = CmConfigInit(role, pools, num, cfgInfo);
    if (ret != 0) {
        return ret;
    }

    ret = CmZkInit();
    if (ret != 0) {
        return ret;
    }

    ret = INIT_MODULES(g_cmModules);
    return ret;
}

void CM_Exit(void)
{
    EXIT_MODULES(g_cmModules);
}

