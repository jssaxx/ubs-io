/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
*/

#ifndef BOOSTIO_UNDERFS_C_H
#define BOOSTIO_UNDERFS_C_H

#include <stdint.h>
#include <time.h>
#include <sys/uio.h>
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
    const char *rootPath;
    uint32_t slotIndex;
    uint32_t deviceCount;
    const char *evictionPolicy;
    uint32_t checkIntervalSeconds;
}LocalConfig;

typedef struct {
    const char *underFsType;
    CephConfig cephConfig;
    HdfsConfig hdfsConfig;
    LocalConfig localConfig;
}UnderFsConfigInfo;

typedef enum {
    UFS_LOCAL_META_DELETE = 1,
} UfsLocalMetaEventType;

typedef struct {
    char key[MAX_KEY_SIZE];
    uint64_t valueLen;
} UfsLocalKeyInfo;

typedef struct {
    uint64_t totalCapacity;
    uint64_t usedBytes;
} UfsStorageMetrics;

typedef void (*UfsLocalMetaEventCallback)(void *context, UfsLocalMetaEventType type, const char *key);

int32_t UfsInit();

void UfsStop();

int32_t UfsPut(const char *key, const char *value, const size_t len);

int32_t UfsPutV(const char *key, const struct iovec *iov, size_t iovcnt);

int32_t UfsGet(const char *key, char *value, const size_t len, const uint64_t off);

int32_t UfsGetWithRealLen(const char *key, char *value, const size_t len, const uint64_t off, size_t *realLen);

int32_t UfsDelete(const char *key);

int32_t UfsExist(const char *key);

int32_t UfsStat(const char *key, ObjStatInfo *objStat);

int32_t UfsList(const char *prefix, ObjStatInfo **objStat, int *objNum);

void UfsInitUnderFsConfig(UnderFsConfigInfo config);

int32_t UfsSetLocalMetaEventCallback(UfsLocalMetaEventCallback callback, void *context);

int32_t UfsScanLocalKeys(UfsLocalKeyInfo **items, uint64_t *count);

int32_t UfsGetStorageMetrics(UfsStorageMetrics *metrics);


#ifdef __cplusplus
}
#endif
#endif
