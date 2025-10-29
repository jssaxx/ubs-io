/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
*/

#ifndef BOOSTIO_UNDERFS_C_H
#define BOOSTIO_UNDERFS_C_H

#include <stdint.h>
#include <time.h>
#include "file_system.h"
#include "underfs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_KEY_SIZE (256)
typedef struct {
    char key[MAX_KEY_SIZE];
    uint32_t size;
    time_t time;
} ObjStatInfo;

typedef struct {
    const char *cfgPath;
    const char *cluster;
    const char *user;
    const char *poolName;
}CephConfig;

typedef struct {
    const char *nameNode;
    const char *workingPath;
}HdfsConfig;

typedef struct {
    const char *underFsType;
    CephConfig cephConfig;
    HdfsConfig hdfsConfig;
}UnderFsConfigInfo;

int32_t UfsInit();

void UfsStop();

int32_t UfsPut(const char *key, const char *value, const size_t len);

int32_t UfsGet(const char *key, char *value, const size_t len, const uint64_t off);

int32_t UfsDelete(const char *key);

int32_t UfsStat(const char *key, ObjStatInfo *objStat);

int32_t UfsList(const char *prefix, ObjStatInfo **objStat, int *objNum);

void UfsInitUnderFsConfig(UnderFsConfigInfo config);


#ifdef __cplusplus
}
#endif
#endif // BOOSTIO_UNDERFS_C_H
