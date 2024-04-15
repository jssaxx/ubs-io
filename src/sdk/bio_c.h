/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BIO_C_H
#define BIO_C_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    RET_CACHE_OK = 0,            // successful
    RET_CACHE_PROTECTED = 1,     // cache write protected
    RET_CACHE_ERROR = 2,         // unknown error code
    RET_CACHE_EPERM = 3,         // input parameter is incorrect
    RET_CACHE_BUSY = 4,          // cache busy, need outer retry
    RET_CACHE_NEED_RETRY = 5,    // need retry
    RET_CACHE_NOT_READY = 6,     // retry is not required
    RET_CACHE_NOT_FOUND = 7,     // not found this key
    RET_CACHE_CONFLICT = 8,      // key conflict
    RET_CACHE_MISS = 9,          // cache miss
    RET_CACHE_NO_SPACE = 10,     // cache capacity not enough
    RET_CACHE_UNAVAILABLE = 11,  // cache service unavailable
    RET_CACHE_EXCEED_QUOTA = 12, // exceed cache quota limit
    RET_CACHE_PT_FAULT = 13,     // cache partition fault
    RET_CACHE_READ_EXCEED = 14,  // read limit is exceeded
    RET_CACHE_EXISTS = 15,       // cache already exists
    RET_CACHE_BUTT
} CResult;

typedef enum {
    LOCAL_AFFINITY = 1, // data local affinity
    GLOBAL_BALANCE = 2, // data global balance
    AFFINITY_BUTT
} AffinityStrategy;

typedef enum {
    WRITE_BACK = 1,
    WRITE_THROUGH = 2,
    STRATEGY_BUTT
} WriteStrategy;

typedef enum {
    CONVERGENCE,
    SEPARATES
} WorkerMode;

#define MAX_KEY_SIZE (256)
#define LOCATION_SIZE (2)
typedef void (*BioLoadCallback)(void *context, int32_t result);

typedef struct {
    char key[MAX_KEY_SIZE];
    uint32_t size;
    time_t time;
} ObjStat;

typedef struct {
    uint64_t location[LOCATION_SIZE];
} ObjLocation;

typedef struct {
    uint64_t tenantId;
    AffinityStrategy affinity;
    WriteStrategy strategy;
} CacheDescriptor;

#define CACHE_SPACE_ADDRESS_SIZE (2)
#define CACHE_SPACE_DEC_SIZE (64)

typedef struct {
    uint64_t address;
    uint32_t size;
} CacheAddress;

typedef struct {
    uint8_t allocLoc;
    uint16_t addressNum;
    uint16_t descriptorSize;
    ObjLocation loc;
    CacheAddress address[CACHE_SPACE_ADDRESS_SIZE];
    char descriptorInfo[CACHE_SPACE_DEC_SIZE];
} CacheSpaceInfo;

/**
 * @brief: Initialize bio service
 *
 * @param[in]: mode: working mode
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioInitialize(WorkerMode mode);

/**
 * @brief: Exit bio service
 *
 * @return: void
 */
void BioExit();

/**
 * @brief: Create cache instance
 *
 * @param[in]: desc: cache descriptor
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioCreateCache(CacheDescriptor desc);

/**
 * @brief: Get cache instance
 *
 * @param[in]: tenantId: tenant id
 * @param[out]: desc: cache descriptor
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioGetCache(uint64_t tenantId, CacheDescriptor *desc);

/**
 * @brief: Destroy cache instance
 *
 * @param[in]: tenantId: tenant id
 * @return: void
 */
CResult BioDestroyCache(uint64_t tenantId);

/**
 * @brief: Calculate location
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: objectId: object id
 * @param[out]: location: location info
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioCalcLocation(uint64_t tenantId, uint64_t objectId, ObjLocation *location);

/**
 * @brief: Put value
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: key: key
 * @param[in]: value: value
 * @param[in]: length: value length
 * @param[in]: location: location info
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioPut(uint64_t tenantId, const char *key, const char *value, uint64_t length, ObjLocation location);

/**
 * @brief: Get value
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: key: key
 * @param[in]: offset: offset of the get value
 * @param[in]: length : length of the get value
 * @param[in]: location : location info
 * @param[out]: value : value
 * @param[out]: realLength : real length
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioGet(uint64_t tenantId, const char *key, uint64_t offset, uint64_t length, ObjLocation location, char *value,
    uint64_t *realLength);

/**
 * @brief: Delete key
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: key: key
 * @param[in]: location : location info
 * @return: return RETURN_CACHE_OK mean ok, others, return non-zero value
 */
CResult BioDelete(uint64_t tenantId, const char *key, ObjLocation location);

/**
 * @brief: Load value
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: key: key
 * @param[in]: offset: offset
 * @param[in]: length: length
 * @param[in]: location: location info
 * @param[in]: callback: load callback function
 * @param[in]: context: callback context
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioLoad(uint64_t tenantId, const char *key, uint64_t offset, uint64_t length, ObjLocation location,
    BioLoadCallback callback, void *context);

/**
 * @brief: List all key that meets the prefix condition
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: prefix: key prefix
 * @param[out]: objs: object array
 * @param[out]: objNum: object number
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioListAll(uint64_t tenantId, const char *prefix, ObjStat **objs, uint64_t *objNum);

/**
 * @brief: Free list all returned resources
 *
 * @param[out]: objs: object array
 * @param[out]: objNum: object number
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
void BioFreeListResources(ObjStat *objs, uint64_t objNum);

/**
 * @brief: Get object info
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: key: key
 * @param[in]: location : location info
 * @param[out]: stat: object stat info
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioStat(uint64_t tenantId, const char *key, ObjLocation location, ObjStat *stat);

/**
 * @brief: alloc write space for write copy free
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: objectId: object id for generate location
 * @param[in]: length : alloc space length
 * @param[out]: stat: object stat info
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioAllocSpace(uint64_t tenantId, uint64_t objectId, uint64_t length, CacheSpaceInfo *spaceInfo);

/**
 * @brief: put with space
 *
 * @param[in]: tenantId: tenant id
 * @param[in]: key: write key
 * @param[in]: addressInfo : write alloc space
 * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
 */
CResult BioPutWithSpace(uint64_t tenantId, const char *key, CacheSpaceInfo *spaceInfo);

typedef uint64_t (*ReadHook)(uint64_t, char *, uint64_t, uint64_t, int *);
typedef uint64_t (*WriteHook)(uint64_t, char *, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*WriteCopyFreeHook)(uint64_t, uint64_t, uint64_t, CacheSpaceInfo *);

uint64_t BioReadHook(uint64_t inode, char *buff, uint64_t count, uint64_t offset, int *readLen);
uint64_t BioWriteHook(uint64_t inode, char *buff, uint64_t count, uint64_t offset, uint64_t fh);
uint64_t BioWriteCopyFreeHook(uint64_t inode, uint64_t offset, uint64_t count, CacheSpaceInfo *spaceInfo);

void BioRegisterJuiceFSRead(ReadHook rh);
void BioRegisterJuiceFSWrite(WriteHook wh);
void BioRegisterJuiceFSWriteCopyFree(WriteCopyFreeHook wh);

#ifdef __cplusplus
}
#endif
#endif // BIO_C_H