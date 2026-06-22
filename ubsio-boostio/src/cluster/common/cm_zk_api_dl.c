/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <dlfcn.h>
#include "cm_log.h"
#include "cm_zk_api_dl.h"

// Function pointers
ZooCreateFn ZooCreate = NULL;
ZooDeleteFn ZooDelete = NULL;
ZooGetFn ZooGet = NULL;
ZooSetFn ZooSet = NULL;
ZooExistsFn ZooExists = NULL;
ZooGetChildrenFn ZooGetChildren = NULL;
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
        void *ptr1 = dlsym((handle), (sym));              \
        if (ptr1 == NULL) {                               \
            CM_LOGERROR("Failed to load symbol %s", sym); \
            return -1;                                    \
        }                                                 \
        (ptr) = (type)ptr1;                               \
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
    DLSYM(zkHandle, ZooGetChildrenFn, ZooGetChildren, "zoo_get_children");
    DLSYM(zkHandle, ZooWgetFn, ZooWget, "zoo_wget");
    DLSYM(zkHandle, ZooWgetChildrenFn, ZooWgetChildren, "zoo_wget_children");
    DLSYM(zkHandle, ZookeeperInitFn, ZookeeperInit, "zookeeper_init");
    DLSYM(zkHandle, ZookeeperCloseFn, ZookeeperClose, "zookeeper_close");
    DLSYM(zkHandle, ZooStateFn, ZooState, "zoo_state");
    DLSYM(zkHandle, ZooRecvTimeoutFn, ZooRecvTimeout, "zoo_recv_timeout");
    DLSYM(zkHandle, ZooSetDebugLevelFn, ZooSetDebugLevel, "zoo_set_debug_level");
    DLSYM(zkHandle, DeallocateStringVectorFn, DeallocateStringVector, "deallocate_String_vector");
    return 0;
}

int ZookeeperApiLoad()
{
#ifdef DEBUG_UT
    return 0;
#endif
    if (gLoaded) {
        CM_LOGINFO("Zookeeper library already loaded");
        return 0;
    }

    const char *zkLibName = "libzookeeper_mt.so.2";
    gZkHandle = dlopen(zkLibName, RTLD_LAZY | RTLD_GLOBAL);
    if (gZkHandle == NULL) {
        CM_LOGERROR("Failed to dlopen %s: %s", zkLibName, dlerror());
        return -1;
    }

    int ret = LoadZookeeperSymbols(gZkHandle);
    if (ret != 0) {
        CM_LOGERROR("Failed to load zookeeper symbols");
        dlclose(gZkHandle);
        gZkHandle = NULL;
        return ret;
    }

    CM_LOGINFO("Successfully loaded all zookeeper symbols");
    gLoaded = true;
    return 0;
}
