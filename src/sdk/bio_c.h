/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BOOSTIO_BIO_C_H
#define BOOSTIO_BIO_C_H
#include <stdint.h>
#include <ctime>

#ifdef __cplusplus
extern "C" {
#endif
typedef uint64_t (*ReadHook)(uint64_t, char *, uint64_t, uint64_t, uint64_t, int *);
typedef uint64_t (*WriteHook)(uint64_t, char *, uint64_t, uint64_t, uint64_t);

extern ReadHook g_readHook;
extern WriteHook g_writeHook;

void BioReigsterJuiceFSRead(ReadHook rh);

void BioReigsterJuiceFSWrite(WriteHook wh);

#define LOCATION_LEN 2
#define MAX_KEY_SIZE 256
typedef void (*LoadCallback)(void *context, int32_t result);

typedef struct {
    uint32_t size; // value size
    time_t time;   // modify time;
} CobjStat;

typedef struct {
    char key[MAX_KEY_SIZE];
    CobjStat stat;
} PairStat;

typedef struct {
    uint64_t tenantId;
    void *bio;
} PairCache;

typedef struct {
    uint64_t location[LOCATION_LEN];
} CobjLocation;

typedef enum {
    LOCAL_AFFINITY = 1, // data local affinity
    GLOBAL_BALANCE = 2, // data global balance
    AFFINITY_BUTT
} CAffinityStrategy;

typedef enum {
    WRITE_BACK = 1,
    WRITE_THROUGH = 2,
    STRATEGY_BUTT
} CWriteStrategy;

/**
 * @brief: Initialize bio service
 *
 * @return: return BioServer instance point
 */
void *BioNewService();

/**
 * @brief: Calculate location information
 *
 * @param[in]: bioHandle: cache instance point
 * @param[in]: objectId: object id
 * @param[out]: location: location info
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
int32_t BioCalculateLocation(void *bioHandle, uint32_t objectId, CobjLocation *objLocation);

/**
 * @brief: Put value
 *
 * @param[in]: bioHandle: cache instance point
 * @param[in]: key: object key
 * @param[in]: value: object value
 * @param[in]: length: value length
 * @param[in]: location: location info
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
int32_t BioPut(void *bioHandle, const char *key, const char *value, uint64_t length, CobjLocation objLocation);

/**
 * @brief: Get value
 *
 * @param[in]: bioHandle: cache instance point
 * @param[in]: key: object key
 * @param[in]: offset: offset of the get value
 * @param[in]: length : length of the get value
 * @param[in]: location : location info
 * @param[out]: value : object value
 * @param[out]: realLength : object value realLength
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
int32_t BioGet(void *bioHandle, const char *key, uint64_t offset, uint64_t length, char *value,
    CobjLocation objLocation, uint64_t *realLength);

/**
 * @brief: Delete key
 *
 * @param[in]: bioHandle: cache instance point
 * @param[in]: key: object key
 * @param[in]: location : location info
 * @return: return RETURN_CACHE_OK mean ok, others, return non-zero value
 */
int32_t BioDelete(void *bioHandle, const char *key, CobjLocation objLocation);

/**
 * @brief: Load value
 *
 * @param[in]: bioHandle: cache instance point
 * @param[in]: key: object key
 * @param[in]: location: object location info
 * @param[in]: callback: callback function
 * @param[in]: context: callback context
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
int32_t BioLoad(void *bioHandle, const char *key, uint64_t offset, uint64_t length, CobjLocation objLocation,
    LoadCallback callback, void *context);

/**
 * @brief: List all key that meets the prefix condition
 *
 * @param[in]: bioHandle: cache instance point
 * @param[in]: prefix: Matching prefix
 * @param[out]: objs: Listed objects
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
int32_t BioListAll(void *bioHandle, const char *prefix, PairStat **allObj, uint64_t *objNum);

/**
 * @brief: Get object stat info
 *
 * @param[in]: bioHandle: cache instance point
 * @param[in]: key: object key
 * @param[in]: location : location info
 * @return: return object stat info
 */
CobjStat BioStat(void *bioHandle, const char *key, CobjLocation objLocation);

/**
 * @brief: Get bio instance tenant id
 *
 * @param[in]: bioHandle: cache instance point
 * @return: tenant id
 */
uint64_t BioGetTenantId(void *bioHandle);

/**
 * @brief: Get bio instance affinity policy
 *
 * @param[in]: bioHandle: cache instance point
 * @return: affinity policy
 */
CAffinityStrategy BioGetAffinityPolicy(void *bioHandle);

/**
 * @brief: Get bio instance write strategy
 *
 * @param[in]: bioHandle: cache instance point
 * @return: write strategy
 */
CWriteStrategy BioGetWriteStrategy(void *bioHandle);

/**
 * @brief: Create bio instance
 *
 * @param[in]: desc: cache descriptor
 * @return: return cache instance shared point
 */
void *BioCreateCache(uint64_t tenantId, CAffinityStrategy affinityStrategy, CWriteStrategy writeStrategy);

/**
 * @brief: Exit boostio bio service
 *
 * @return: void
 */
void BioFreeService();

/**
 * @brief: Get bio instance
 *
 * @param[in]: tenantId: tenant id
 * @return: return cache instance shared point
 */
void *BioGetCache(uint64_t tenantId);

/**
 * @brief: List bio instance
 *
 * @return: return all caches
 */
PairCache *BioListCache(uint64_t *cacheNum);

/**
 * @brief: Destroy bio instance
 *
 * @param[in]: tenantId: tenant id
 * @return: void
 */
void BioDestroyCache(uint64_t tenantId);
#ifdef __cplusplus
}
#endif

#endif // BOOSTIO_BIO_C_H
