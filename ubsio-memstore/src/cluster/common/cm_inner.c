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

