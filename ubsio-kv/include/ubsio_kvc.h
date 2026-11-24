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

#ifndef UBSIO_KVC_H
#define UBSIO_KVC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UBSIO_UNDERFS_TYPE_DEFINED
#define UBSIO_UNDERFS_TYPE_DEFINED
typedef enum {
    UBSIO_UNDERFS_NONE = 0,
    UBSIO_UNDERFS_CEPH = 1,
    UBSIO_UNDERFS_HDFS = 2,
    UBSIO_UNDERFS_LOCAL = 3,
    UBSIO_UNDERFS_SHARED = 4,
    UBSIO_UNDERFS_BUTT
} UbsioUnderFsType;
#endif

#ifndef UBSIO_INIT_INFO_DEFINED
#define UBSIO_INIT_INFO_DEFINED
typedef struct {
    UbsioUnderFsType underFsType;
    uint8_t slot;      /* LocalFS slot ID (1..16); 0 if there is no file route. */
    uint8_t ndsEnable; /* 1 if the initialized backend offers an NDS read path; 0 otherwise. */
} UbsioInitInfo;
#endif

#define UBSIO_RESOURCE_DISK_PATH_MAX_SIZE (256)
#define UBSIO_RESOURCE_MAX_DISK_NUM (16)

#ifndef UBSIO_KV_KEY_INFO_C_DEFINED
#define UBSIO_KV_KEY_INFO_C_DEFINED
#define UBSIO_KV_MAX_KEY_SIZE (256)
typedef struct {
    char key[UBSIO_KV_MAX_KEY_SIZE]; /* Null-terminated key; maximum key length is 255 bytes. */
    uint32_t keyLen;                 /* Key length in bytes, excluding the trailing null byte. */
    uint32_t reserved;               /* Reserved for ABI extension; callers must ignore it. */
    uint64_t valueLen;               /* Object value length in bytes. */
} UbsioKvKeyInfo;
#endif

#ifndef UBSIO_KV_BATCH_STAT_DEFINED
#define UBSIO_KV_BATCH_STAT_DEFINED
typedef struct {
    char key[UBSIO_KV_MAX_KEY_SIZE];
    uint32_t size;
    int32_t result;
} UbsioKvBatchStat;
#endif

typedef struct {
    uint16_t status;
    char path[UBSIO_RESOURCE_DISK_PATH_MAX_SIZE];
    uint64_t readBandwidth;
    uint64_t writeBandwidth;
    uint64_t totalBandwidth;
    uint8_t bandwidthValid;
    uint8_t reserved[7];
} UbsioDiskInfo;

typedef struct {
    uint64_t diskCap;
    uint64_t diskUsed;
    uint64_t memCap;
    uint64_t memUsed;
    uint32_t diskNum;
    uint32_t faultDiskNum;
    UbsioDiskInfo disks[UBSIO_RESOURCE_MAX_DISK_NUM];
} UbsioResourceInfo;

#ifndef UBSIO_META_EVENT_C_DEFINED
#define UBSIO_META_EVENT_C_DEFINED
typedef enum {
    UBSIO_META_RECOVER_C = 0,
    UBSIO_META_DELETE_C = 1,
} UbsioMetaEventTypeC;

typedef struct {
    int32_t type;
    const char *key;
    uint32_t keyLen;
} UbsioMetaEventC;

typedef void (*UbsioMetaEventCallbackC)(void *context, const UbsioMetaEventC *events, uint32_t count);
#endif

/**
 * @brief Initialize UBS-IO KV Cache
 *
 * @param devId            [in] device id, -1 means no ACL device binding and uses standalone device 0
 * @return 0 if successful
 */
int32_t UbsioKvCacheInit(int32_t devId);

/** Query the initialized storage backend. On error, info is unchanged. */
int32_t UbsioGetInitInfo(UbsioInitInfo *info);

/**
 * @brief Get write cache and disk resource information managed by the local process.
 *
 * Configured block-device bandwidth is sampled for 200 ms from Linux block statistics and is reported in bytes
 * per second. When a partition is configured, only that partition's bandwidth is reported.
 * bandwidthValid distinguishes a valid zero value from a collection failure.
 * Disk status is 0 for normal and 1 for fault.
 *
 * @param info             [out] Write cache capacity/usage and per-disk information
 * @return 0 if successful
 */
int32_t UbsioGetResourceInfo(UbsioResourceInfo *info);

/**
 * @brief Register metadata events for MemCache Master metadata synchronization.
 *
 * DELETE events report final cache eviction without UnderFS, or final local UnderFS file eviction.
 * Cache migration into UnderFS does not report DELETE. Register before init to receive recovery events.
 * The events buffer and key pointers are only valid during the callback; the callee must copy keys if needed.
 * Passing nullptr unregisters the callback.
 */
int32_t UbsioKvCacheRegisterMetaEventCallback(UbsioMetaEventCallbackC callback, void *context);

/**
 * @brief Scan all valid objects currently stored in the UBS-IO write-cache disk tier.
 *
 * The snapshot includes objects recovered from BDM and objects written to disk after startup. Objects that only
 * reside in memory, have been deleted, or have been evicted from the disk tier are not returned. The returned array
 * is read-only and must be released with UbsioKvCacheFreeScanKeyResult before UbsioKvCacheExit.
 * Result ordering is unspecified. If no object is found, *items is NULL and *count is 0.
 * If *hasMore is true, release the current result and call this interface again to obtain the remaining objects.
 *
 * @param items            [out] Read-only snapshot allocated by UBS-IO
 * @param count            [out] Number of entries in items
 * @param hasMore          [out] true if more keys remain and the interface must be called again; otherwise false
 * @return 0 if successful
 */
int32_t UbsioKvCacheScanKey(const UbsioKvKeyInfo **items, uint64_t *count, bool *hasMore);

/**
 * @brief Release a snapshot returned by UbsioKvCacheScanKey.
 *
 * Passing NULL or an already-null result is allowed. On return, *items is set to NULL.
 */
void UbsioKvCacheFreeScanKeyResult(const UbsioKvKeyInfo **items);

/**
 * @brief Exit UBS-IO KV Cache
 */
void UbsioKvCacheExit(void);

/**
 * @brief Register a KV-cache HBM region with the NDS module for P2P direct-to-HBM reads.
 *
 * Call after UbsioKvCacheInit (which initializes NDS when ubsio.nds.enabled=true).
 * By default, this call registers the region with XDS before returning. Repeating
 * a successful registration of the same address and length is idempotent.
 * Each IOV in a registered read must fit inside one registered region.
 * Set UBSIO_NDS_SKIP_MEM_REGISTER=1 before initialization to skip registration.
 * In registered mode addr and length must be 512-byte aligned and the region must
 * remain accessible until successful UbsioKvCacheUnregisterMem. In either mode, read buffers
 * must remain accessible until the synchronous read returns.
 *
 * @param addr    [in] HBM region base (caller-owned)
 * @param length  [in] region size in bytes
 * @return 0 if successful, non-zero if NDS is unavailable/not-initialized or params invalid
 */
int32_t UbsioKvCacheRegisterMem(void *addr, size_t length);

/**
 * @brief Unregister a previously registered KV-cache HBM region from the NDS module.
 *
 * With UBSIO_NDS_SKIP_MEM_REGISTER=1, validates addr/length and returns success
 * without calling NDS unregistration.
 * In registered mode, unregisters the exact address and length with XDS. Other
 * regions stay registered. On failure the caller can retry the same unregister.
 *
 * @param addr    [in] base address previously passed to UbsioKvCacheRegisterMem
 * @param length  [in] region size, must match the registered length
 * @return 0 if successful
 */
int32_t UbsioKvCacheUnregisterMem(void *addr, size_t length);

/**
 * @brief Put data of object with key into UBS-IO KV Cache
 * This data operation default supports async
 *
 * @param key              [in] key of data, less than 256
 * @param buf              [in] data to be put
 * @param length           [in] data size
 * @param flags            [in] optional flags, reserved
 * @return 0 if successful
 */
int32_t UbsioKvCachePut(const char *key, void *buf, size_t length, uint32_t flags);

/**
 * @brief Put multiple data objects into UBS-IO KV Cache
 * This data operation default supports async
 *
 * @param keys           [in] Array of keys for the data objects
 * @param keysCount      [in] Number of keys in the array
 * @param bufs           [in] Array of data buffers to be put
 * @param length         [in] Size of data buf in data buffers
 * @param results        [out] result of data to be put in data buffers
 * @param flags          [in] Optional flags, reserved
 * @return 0 if successful
 */
int32_t UbsioKvCacheBatchPut(const char **keys,
                             uint32_t keysCount,
                             void **bufs,
                             size_t *lengths,
                             int *results,
                             uint32_t flags);


/**
 * @brief Get data of object by key from UBS-IO KV Cache
 * This data operation default supports async
 *
 * @param key              [in] key of data, less than 256
 * @param buf              [in] data to be gotten
 * @param length           [in] data size
 * @param options          [in] options for get policy
 * @return 0 if successful
 */
int32_t UbsioKvCacheGet(const char *key, void *buf, size_t length, uint32_t flags);

/**
 * @brief Get multiple data objects by keys from UBS-IO KV Cache
 * This data operation default supports async
 *
 * @param keys           [in] Array of keys for the data objects
 * @param keysCount      [in] Number of keys in the array
 * @param bufs           [out] Array of data buffers to be get
 * @param length         [in] Size of data buf in data buffers
 * @param results        [out] result of data to be get in data buffers
 * @param flags          [in] Optional flags, reserved
 * @return 0 if successful
 */
int32_t UbsioKvCacheBatchGet(const char **keys,
                             uint32_t keysCount,
                             void **bufs,
                             size_t *lengths,
                             int *results,
                             uint32_t flags);

/**
 * @brief Read LocalFS values into HBM. UBSIO can fall back to pread and H2D on the same file.
 *
 * @param keys        [in] Array of cache keys
 * @param keysCount   [in] Number of keys in the batch (1-16384)
 * @param ownerSlots  [in] Owner slot per key (1-16, as reported by UbsioGetInitInfo)
 * @param buffers     [in] HBM buffer addresses per key
 * @param lengths     [in] Read length in bytes per buffer. The sum for a key is read from file offset 0.
 * @param iovCounts   [in] Number of buffers and lengths for each key
 * @param results     [out] One BoostIO CResult per key when the call succeeds
 * @return 0 if the batch was processed
 */
int32_t UbsioKvCacheBatchGetDirect(const char *const *keys, uint32_t keysCount, const uint8_t *ownerSlots,
                                 void *const *const *buffers, const size_t *const *lengths,
                                 const uint32_t *iovCounts, int *results);

/**
 * @brief Determine whether the key is within the UBS-IO KV Cache
 *
 * @param key              [in] key of data, less than 256
 * @param flags            [in] optional flags, reserved
 * @return 0 if successfully
 */
bool UbsioKvCacheExist(const char *key, uint32_t flags);

/**
 * @brief Determine whether the list of keys is within the UBS-IO KV Cache
 *
 * @param keys             [in] keys of data, the length of key is less than 256
 * @param keysCount        [in] Count of keys
 * @param results          [out] existence status list of keys in UBS-IO KV Cache
 * @return 0 if successfully
 */
int32_t UbsioKvCacheBatchExist(const char **keys, uint32_t keysCount, bool *results, uint32_t flags);

/**
 * @brief Delete the object with key from UBS-IO KV Cache
 *
 * @param key              [in] key of data, less than 256
 * @param flags            [in] optional flags, reserved
 * @return  0 if successful
 */
int32_t UbsioKvCacheDelete(const char *key, uint32_t flags);

/**
 * @brief Delete multiple keys from the UBS-IO KV Cache
 *
 * @param keys             [in] List of keys to be deleted from the UBS-IO KV Cache
 * @param keysCount        [in] Count of keys
 * @param results          [out] Results of each delete operation
 * @param flags            [in] Flags for the operation
 * @return 0 if successfully, positive value if error happens
 */
int32_t UbsioKvCacheBatchDelete(const char **keys, uint32_t keysCount, int32_t *results, uint32_t flags);

/**
 * @brief Get the length of object
 *
 * @param key              [in]  key of data, less than 256
 * @param length           [out] size of data
 * @param flags            [in]  optional flags, reserved
 * @return  0 if successful
 */
int32_t UbsioKvCacheGetLength(const char *key, size_t *length, uint32_t flags);

/**
 * @brief Get multiple keys length from the UBS-IO KV Cache
 *
 * @param keys             [in] List of keys whose lengths are queried from the UBS-IO KV Cache
 * @param keysCount        [in] Count of keys
 * @param lengths          [out] length of each get key
 * @param results          [out] Results of each get length operation
 * @param flags            [in] Flags for the operation
 * @return 0 if successfully, positive value if error happens
 */
int32_t UbsioKvCacheBatchGetLength(const char **keys, uint32_t keysCount, size_t *lengths, int32_t *results, uint32_t flags);

/**
 * @brief Query metadata for multiple keys from the UBS-IO KV Cache
 *
 * The request is forwarded to UBS-IO BioBatchStat in one batch. Each output entry contains the key, value size,
 * and per-key result. This API currently supports standalone mode only.
 *
 * @param keys             [in] Keys to query; each key must contain 1 to 255 bytes
 * @param keysCount        [in] Number of keys, in the range [1, 16384]
 * @param stats            [out] Key, value size, and result for each input key
 * @param flags            [in] Reserved; must be 0
 * @return 0 if the batch request is processed successfully, positive value if a batch-level error occurs
 */
int32_t UbsioKvCacheBatchStat(const char **keys, uint32_t keysCount, UbsioKvBatchStat *stats, uint32_t flags);

/**
 * @brief Free shm address within the UBS-IO KV Cache
 *
 * @param bufs             [in] Array of data buffers to be free
 * @param keys_count       [in] Count of keys
 * @return 0 if successfully
 */
int32_t UbsioKvCacheBatchFree(void **bufs, uint32_t keysCount);

#ifdef __cplusplus
}
#endif
#endif // UBSIO_KVC_H
