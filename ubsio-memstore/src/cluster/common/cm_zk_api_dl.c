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

#include "cm_zk_api_dl.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>

#include "cm_comm.h"
#include "cm_log.h"

ZooCreateFn ZooCreate = NULL;
ZooDeleteFn ZooDelete = NULL;
ZooGetFn ZooGet = NULL;
ZooSetFn ZooSet = NULL;
ZooExistsFn ZooExists = NULL;
ZooWgetFn ZooWget = NULL;
ZooWgetChildrenFn ZooWgetChildren = NULL;
ZookeeperInitFn ZookeeperInit = NULL;
ZookeeperCloseFn ZookeeperClose = NULL;
ZooStateFn ZooState = NULL;
ZooRecvTimeoutFn ZooRecvTimeout = NULL;
ZooSetDebugLevelFn ZooSetDebugLevel = NULL;
DeallocateStringVectorFn DeallocateStringVector = NULL;

#define DLSYM(handle, type, ptr, sym)                     \
    do {                                                  \
        void *symbol = dlsym((handle), (sym));            \
        if (symbol == NULL) {                             \
            CM_LOGERROR("Failed to load symbol %s", sym); \
            return CM_ERR;                                \
        }                                                 \
        (ptr) = (type)symbol;                             \
    } while (0)

static void *gZkHandle = NULL;
static bool gLoaded = false;

static int LoadZookeeperSymbols(void *zkHandle)
{
    DLSYM(zkHandle, ZooCreateFn, ZooCreate, "zoo_create");
    DLSYM(zkHandle, ZooDeleteFn, ZooDelete, "zoo_delete");
    DLSYM(zkHandle, ZooGetFn, ZooGet, "zoo_get");
    DLSYM(zkHandle, ZooSetFn, ZooSet, "zoo_set");
    DLSYM(zkHandle, ZooExistsFn, ZooExists, "zoo_exists");
    DLSYM(zkHandle, ZooWgetFn, ZooWget, "zoo_wget");
    DLSYM(zkHandle, ZooWgetChildrenFn, ZooWgetChildren, "zoo_wget_children");
    DLSYM(zkHandle, ZookeeperInitFn, ZookeeperInit, "zookeeper_init");
    DLSYM(zkHandle, ZookeeperCloseFn, ZookeeperClose, "zookeeper_close");
    DLSYM(zkHandle, ZooStateFn, ZooState, "zoo_state");
    DLSYM(zkHandle, ZooRecvTimeoutFn, ZooRecvTimeout, "zoo_recv_timeout");
    DLSYM(zkHandle, ZooSetDebugLevelFn, ZooSetDebugLevel, "zoo_set_debug_level");
    DLSYM(zkHandle, DeallocateStringVectorFn, DeallocateStringVector, "deallocate_String_vector");
    return CM_OK;
}

int ZookeeperApiLoad(void)
{
#ifdef DEBUG_UT
    return CM_OK;
#endif
    if (gLoaded) {
        return CM_OK;
    }

    const char *zkLibName = "libzookeeper_mt.so";
    gZkHandle = dlopen(zkLibName, RTLD_LAZY | RTLD_GLOBAL);
    if (gZkHandle == NULL) {
        CM_LOGERROR("Failed to dlopen %s: %s", zkLibName, dlerror());
        return CM_ERR;
    }

    int ret = LoadZookeeperSymbols(gZkHandle);
    if (ret != CM_OK) {
        CM_LOGERROR("Failed to load zookeeper symbols");
        dlclose(gZkHandle);
        gZkHandle = NULL;
        return ret;
    }

    gLoaded = true;
    return CM_OK;
}
