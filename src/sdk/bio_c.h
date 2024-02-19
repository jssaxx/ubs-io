/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BOOSTIO_BIO_C_H
#define BOOSTIO_BIO_C_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *BioNewService();

void *BioCreateCache(void *bioHandle);

int32_t BioCalculateLocation(void *bioHandle, uint32_t objectId, uint64_t *location1, uint64_t *location2);

int32_t BioPut(void *bioHandle, const char *key, const char *value, uint64_t length, uint64_t location1,
    uint64_t location2);

int32_t BioGet(void *bioHandle, const char *key, uint64_t offset, uint64_t length, char *value, uint64_t location1,
    uint64_t location2, uint64_t *realLength);

int32_t BioDelete(void *bioHandle, const char *key, uint64_t location1, uint64_t location2);

#ifdef __cplusplus
}
#endif

#endif // BOOSTIO_BIO_C_H
