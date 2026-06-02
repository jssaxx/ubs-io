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

#ifndef MMS_C_H
#define MMS_C_H

#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RET_MMS_OK = 0,            // successful
    RET_MMS_PROTECTED = 1,     // cache write protected
    RET_MMS_ERROR = 2,         // unknown error code
    RET_MMS_EPERM = 3,         // input parameter is incorrect
    RET_MMS_BUSY = 4,          // cache busy, need outer retry
    RET_MMS_NEED_RETRY = 5,    // need retry
    RET_MMS_NOT_READY = 6,     // cache service is not ready
    RET_MMS_NOT_FOUND = 7,     // not found this key
    RET_MMS_CONFLICT = 8,      // key conflict
    RET_MMS_MISS = 9,          // cache miss
    RET_MMS_NO_SPACE = 10,     // cache capacity not enough
    RET_MMS_UNAVAILABLE = 11,  // cache service unavailable
    RET_MMS_EXCEED_QUOTA = 12, // exceed cache quota limit
    RET_MMS_PT_FAULT = 13,     // cache partition fault
    RET_MMS_READ_EXCEED = 14,  // read limit is exceeded
    RET_MMS_EXISTS = 15,       // cache already exists
    RET_MMS_BUTT
} CResult;

typedef struct {
    uint16_t netConnectCnt;                // net connect cnts of each channel
    uint16_t netGroupNum;                  // net worker groups nums
    uint8_t netIsBusyPolling;              // busy polling or event polling
    uint8_t tlsEnable;                     // Tls switch
    char certificationPath[PATH_MAX];      // certification path
    char caCerPath[PATH_MAX];              // caCer path
    char caCrlPath[PATH_MAX];              // CA CRL path
    char privateKeyPath[PATH_MAX];         // private key path
    char privateKeyPasswordPath[PATH_MAX]; // private key password
    char decrypterLibPath[PATH_MAX];       // decrypter lib path
    char opensslLibDir[PATH_MAX];          // openssl lib dir path
} MmsOptions;

typedef struct {
    const char *key;
    const char *value;
    uint32_t valueLen;
    uint16_t keyLen;
    uint16_t isNotify; // Whether to notify data changes. 0: false; non-zero: true.

    char **valueAddr; // Output: memory address where the data is written.
    int32_t *result;  // Output: execution result of this key/value item.
} PutItems;

typedef struct {
    const char *key;
    uint16_t keyLen;
    uint32_t offset;
    uint32_t length;

    char **value; // Output: if *value is null, returns the internal value address; otherwise copies data into *value.
    uint32_t *realLength; // Output: real length of the returned value.
    int32_t *result;
} GetItems;

typedef struct {
    const char *key;
    const char *value;
    uint16_t keyLen;
    uint32_t valueLen;
    uint32_t offset;

    int32_t *result;
} UpdateItems, ReplaceItems;

typedef struct {
    const char *key;
    uint16_t keyLen;
    uint16_t isNotify;

    int32_t *result;
} DeleteItems;

typedef struct {
    char *key;
    char *value;
    uint64_t length;
} ValueInfo;

typedef enum {
    OP_PUT = 0,
    OP_DELETE = 1,
    OP_BUTT
} OperateType;

typedef void (*NotifyCallback)(const char *key, OperateType opType);
typedef void (*ServiceCallback)(uint8_t serviceable); // 0: false; non-zero: true.

/**
 * @brief: Initialize the MMS service.
 *
 * @param[in]: options: MMS options.
 * @param[in]: service: Service status callback.
 * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
 */
CResult MmsInitialize(const MmsOptions *options, ServiceCallback service);

/**
 * @brief: Register the data change notification callback.
 *
 * @param[in]: callback: Data change notification callback.
 * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
 */
CResult MmsRegisterNotifyCallback(NotifyCallback callback);

/**
 * @brief: Exit the MMS service.
 *
 * @return: void.
 */
void MmsExit(void);

/**
 * @brief: Put key/value items.
 *
 * For each item, PutItems::valueAddr is used to return the memory address where the data is written, and
 * PutItems::result is used to return the per-item execution result.
 *
 * @param[in/out]: itemList: Key/value descriptor list.
 * @param[in]: itemNum: Number of items in itemList.
 * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code.
 */
CResult MmsPut(PutItems *itemList, uint32_t itemNum);

/**
 * @brief: Get key/value items.
 *
 * If GetItems::value points to a null pointer, the returned data address is managed internally and must not be freed
 * by the caller. The caller must ensure that the key is not modified while reading data through that address.
 *
 * For each item, GetItems::realLength is used to return the real value length, and GetItems::result is used to return
 * the per-item execution result.
 *
 * @param[in/out]: itemList: Key/value descriptor list.
 * @param[in]: itemNum: Number of items in itemList.
 * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code. If an item's realLength is
 *          0, the key was not found.
 */
CResult MmsGet(GetItems *itemList, uint32_t itemNum);

/**
 * @brief: Get key/value items by key prefix.
 *
 * @param[in]: prefix: Key prefix.
 * @param[out]: valueInfoItems: Matched value info list.
 * @param[out]: itemNum: Number of matched items.
 * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
 */
CResult MmsGetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum);

/**
 * @brief: Get key/value items by key range.
 *
 * @param[in]: start: Start key of the range query.
 * @param[in]: end: End key of the range query.
 * @param[out]: valueInfoItems: Matched value info list.
 * @param[out]: itemNum: Number of matched items.
 * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
 */
CResult MmsGetValuesByRange(const char *start, const char *end, ValueInfo **valueInfoItems, uint64_t *itemNum);

/**
 * @brief: Delete key/value items by key range.
 *
 * @param[in]: start: Start key of the range delete.
 * @param[in]: end: End key of the range delete.
 * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
 */
CResult MmsBatchDeleteByRange(const char *start, const char *end);

/**
 * @brief: Release resources returned by prefix queries or range queries.
 *
 * @param[in/out]: valueInfoItems: Value info list returned by prefix queries or range queries.
 * @param[in]: itemNum: Number of value info items.
 * @return: void.
 */
void MmsFreeResources(ValueInfo **valueInfoItems, uint64_t itemNum);

/**
 * @brief: Update key/value items.
 *
 * For each item, UpdateItems::result is used to return the per-item execution result.
 *
 * @param[in/out]: itemList: Key/value descriptor list.
 * @param[in]: itemNum: Number of items in itemList.
 * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code.
 */
CResult MmsUpdate(UpdateItems *itemList, uint32_t itemNum);

/**
 * @brief: Delete key/value items.
 *
 * For each item, DeleteItems::result is used to return the per-item execution result.
 *
 * @param[in/out]: itemList: Key descriptor list.
 * @param[in]: itemNum: Number of items in itemList.
 * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code.
 */
CResult MmsDelete(DeleteItems *itemList, uint32_t itemNum);

/**
 * @brief: Replace key/value items.
 *
 * For each item, ReplaceItems::result is used to return the per-item execution result.
 *
 * @param[in/out]: itemList: Key/value descriptor list.
 * @param[in]: itemNum: Number of items in itemList.
 * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code.
 */
CResult MmsReplace(ReplaceItems *itemList, uint32_t itemNum);

/**
 * @brief: Start a catch-up task for recovery.
 *
 * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
 */
CResult MmsStartCatchUpTask(void);

#ifdef __cplusplus
}
#endif
#endif // MMS_C_H