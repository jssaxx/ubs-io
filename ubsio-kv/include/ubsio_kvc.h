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

#include <cstdint>
#include <cstdio>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize UBS-IO KV Cache
 *
 * @param devId            [in] device id, -1 means no ACL device binding and uses standalone device 0
 * @param ssdSize          [in] reserved, currently ignored
 * @return 0 if successful
 */
int32_t UbsioKvCacheInit(int32_t devId, uint64_t ssdSize);

 /** 
  * @brief Exit UBS-IO KV Cache 
  */ 
 void UbsioKvCacheExit(void);

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
 * @brief Get multiple data objects by keys from UBS-IO KV Cache
 * This data operation default supports async
 *
 * @param keys           [in] Array of keys for the data objects
 * @param keysCount      [in] Number of keys in the array
 * @param bufs           [out] Array of npu data buffers to be get
    bufs：{
            {key1_npu_buflayer_0, key1_npu_buflayer_1, ..., {key1_npu_buflayer_N}},
            {key2_npu_buflayer_0, key2_npu_buflayer_1},..., {key2_npu_buflayer_N}},
            ...,
            {keyM_npu_buflayer_0, keyM_npu_buflayer_1},..., {keyM_npu_buflayer_N}}
          }
 * @param length         [in] Size of data buf in data buffers
 * @param lengthsRows    [in] Number of rows in the lengths matrix
 * @param lengthsCols    [in] Number of columns in the lengths matrix
 * @param results        [out] result of data to be get in data buffers
 * @param flags          [in] Optional flags, reserved
 * @return 0 if successful
 */
int32_t UbsioKvCacheBatchGetDirect(const char **keys,
                                   uint32_t keysCount,
                                   void ***bufs,
                                   size_t **lengths,
                                   uint32_t lengthsRows,
                                   uint32_t lengthsCols,
                                   int *results,
                                   uint32_t flags);

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
  * @param keys             [in] List of keys to be deteled from the UBS-IO KV Cache 
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
  * @param keys             [in] List of keys to be deteled from the UBS-IO KV Cache 
  * @param keysCount        [in] Count of keys 
  * @param lengths          [out] length of each get key 
  * @param results          [out] Results of each get length operation 
  * @param flags            [in] Flags for the operation 
  * @return 0 if successfully, positive value if error happens 
  */ 
 int32_t UbsioKvCacheBatchGetLength(const char **keys, uint32_t keysCount, size_t *lengths, int32_t *results, uint32_t flags);

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
