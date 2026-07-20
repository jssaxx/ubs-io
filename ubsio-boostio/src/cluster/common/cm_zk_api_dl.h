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

#ifndef CM_ZK_API_DL_H
#define CM_ZK_API_DL_H

#include "zookeeper.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ZooCreateFn)(zhandle_t *zh, const char *path, const char *value, int valuelen,
                           const struct ACL_vector *acl, int mode, char *path_buffer, int path_buffer_len);
typedef int (*ZooDeleteFn)(zhandle_t *zh, const char *path, int version);
typedef int (*ZooGetFn)(zhandle_t *zh, const char *path, int watch, char *buffer, int *bufferLen, struct Stat *stat);
typedef int (*ZooSetFn)(zhandle_t *zh, const char *path, const char *buffer, int buflen, int version);
typedef int (*ZooExistsFn)(zhandle_t *zh, const char *path, int watch, struct Stat *stat);
typedef int (*ZooGetChildrenFn)(zhandle_t *zh, const char *path, int watch, struct String_vector *strings);
typedef int (*ZooWgetFn)(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx, char *buffer,
                         int *bufferLen, struct Stat *stat);
typedef int (*ZooWgetChildrenFn)(zhandle_t *zh, const char *path, watcher_fn watcher, void *watcherCtx,
                                 struct String_vector *strings);
typedef zhandle_t *(*ZookeeperInitFn)(const char *host, watcher_fn fn, int recv_timeout, const clientid_t *clientid,
                                      void *context, int flags);
typedef int (*ZookeeperCloseFn)(zhandle_t *zh);
typedef int (*ZooStateFn)(zhandle_t *zh);
typedef int (*ZooRecvTimeoutFn)(zhandle_t *zh);
typedef void (*ZooSetDebugLevelFn)(ZooLogLevel level);
typedef void (*DeallocateStringVectorFn)(struct String_vector *v);

extern ZooCreateFn ZooCreate;
extern ZooDeleteFn ZooDelete;
extern ZooGetFn ZooGet;
extern ZooSetFn ZooSet;
extern ZooExistsFn ZooExists;
extern ZooGetChildrenFn ZooGetChildren;
extern ZooWgetFn ZooWget;
extern ZooWgetChildrenFn ZooWgetChildren;
extern ZookeeperInitFn ZookeeperInit;
extern ZookeeperCloseFn ZookeeperClose;
extern ZooStateFn ZooState;
extern ZooRecvTimeoutFn ZooRecvTimeout;
extern ZooSetDebugLevelFn ZooSetDebugLevel;
extern DeallocateStringVectorFn DeallocateStringVector;

int ZookeeperApiLoad();

#ifdef __cplusplus
}
#endif

#endif // CM_ZK_API_DL_H