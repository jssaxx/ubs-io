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

#ifndef CM_MODULE_H
#define CM_MODULE_H

#include "cm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODULE_NAME_MAX_LEN (50)

typedef struct tagMODULE_DEFINE_S {
    char name[MODULE_NAME_MAX_LEN];
    int32_t (*initModule)(void);
    void (*exitModule)(void);
} MODULE_DEFINE_S;

typedef struct tagMODULE_DEFINE_WITH_PARAM_S {
    char name[MODULE_NAME_MAX_LEN];
    int32_t (*initModule)(void *);
    void (*exitModule)(void);
} MODULE_DEFINE_WITH_PARAM_S;

static inline void rollbackModules(MODULE_DEFINE_S *modules, int32_t curSteps)
{
    MODULE_DEFINE_S *m = NULL;
    int32_t i;

    if (curSteps != 0) {
        for (i = curSteps - 1; i >= 0; i--) {
            m = &modules[i];
            if (NULL != m->exitModule) {
                m->exitModule();
                CM_LOGINFO("rollback %s succeed.", m->name);
            }
        }
    }
}

static inline void rollbackModulesWithParam(MODULE_DEFINE_WITH_PARAM_S *modules, int32_t curSteps)
{
    MODULE_DEFINE_WITH_PARAM_S *m = NULL;
    int32_t i;

    if (curSteps != 0) {
        for (i = curSteps - 1; i >= 0; i--) {
            m = &modules[i];
            if (NULL != m->exitModule) {
                m->exitModule();
                CM_LOGINFO("rollback %s succeed.", m->name);
            }
        }
    }
}

static inline int32_t initModules(MODULE_DEFINE_S *modules, int32_t steps)
{
    MODULE_DEFINE_S *m = modules;
    int32_t i;

    for (i = 0; i < steps; i++, m++) {
        if (NULL == m->initModule) {
            continue;
        }

        if (m->initModule() != 0) {
            CM_LOGERROR("init %s failed.", m->name);
            rollbackModules(modules, i);
            return -1;
        }

        CM_LOGINFO("init %s succeed.", m->name);
    }

    return 0;
}

static inline int32_t initModulesWithParam(MODULE_DEFINE_WITH_PARAM_S *modules, int32_t steps, void *initParam)
{
    MODULE_DEFINE_WITH_PARAM_S *m = modules;
    int32_t i;

    for (i = 0; i < steps; i++, m++) {
        if (NULL == m->initModule) {
            continue;
        }

        if (m->initModule(initParam) != 0) {
            CM_LOGERROR("init %s failed.", m->name);
            rollbackModulesWithParam(modules, i);
            return -1;
        }

        CM_LOGINFO("init %s succeed.", m->name);
    }

    return 0;
}

#define INIT_MODULES(modules) initModules(modules, ARRAY_LEN(modules))

#define INIT_MODULES_WITH_PARAM(modules, initParam) initModulesWithParam(modules, ARRAY_LEN(modules), initParam)

#define EXIT_MODULES(modules) rollbackModules(modules, ARRAY_LEN(modules))

#define EXIT_MODULES_WITH_PARAM(modules) rollbackModulesWithParam(modules, ARRAY_LEN(modules))

#ifdef __cplusplus
}
#endif

#endif

