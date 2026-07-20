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
#include <time.h>
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
    RET_MMS_NOT_READY = 6,     // retry is not required
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
    char caCrlPath[PATH_MAX];              // caCer path
    char privateKeyPath[PATH_MAX];         // private key path
    char privateKeyPasswordPath[PATH_MAX]; // private key password
    char decrypterLibPath[PATH_MAX];       // decrypter lib path
    char opensslLibDir[PATH_MAX];          // openssl lib dir path
} MmsOptions;

typedef struct {
    char *key;
    char *value;
    uint64_t length;
    uint64_t version;
} PutItems;

typedef struct {
    char *key;
    uint64_t offset;
    uint64_t length;
    char *value;
    uint64_t *realLength;
} GetItems;

typedef struct {
    char *key;
    char *value;
    uint64_t offset;
    uint64_t length;
    uint64_t version;
} UpdateItems, ReplaceItems;

typedef struct {
    char *key;
    uint64_t version;
} DeleteItems;

typedef void (*ServiceCallback)(bool serviceable);

/**
 * @brief: Initialize mms service
 *
 * @param[in]: option: log options
 * @param[in]: service: service status check callback
 * @return: return RET_MMS_OK mean success, others, return non-zero value
 */
CResult MmsInitialize(MmsOptions &options, ServiceCallback service);

/**
 * @brief: Exit mms service
 *
 * @return: void
 */
void MmsExit(void);

/**
 * @brief: Put value
 *
 * @param[in]: userId: user id
 * @param[in]: itemList: key/value desc list
 * @param[in]: itemNum: batch num
 * @return: return RET_MMS_OK mean success, others, return non-zero value
 */
CResult MmsPut(uint64_t userId, PutItems *itemList, uint32_t itemNum);

/**
 * @brief: Get value
 *
 * @param[in]: userId: user id
 * @param[in/out]: itemList: key/value desc list
 * @param[in]: itemNum: batch num
 * @return: return RET_MMS_OK mean success, others, return non-zero value, item's realLength == 0: key not found
 */
CResult MmsGet(uint64_t userId, GetItems *itemList, uint32_t itemNum);

/**
 *
 * @param[in]: userId: user id
 * @param[in]: itemList: key/value desc list
 * @param[in]: itemNum: batch num
 * @return: return RET_MMS_OK mean success, others, return non-zero value
 */
CResult MmsUpdate(uint64_t userId, UpdateItems *itemList, uint32_t itemNum);

/**
 * @brief: Delete object
 *
 * @param[in]: userId: user id
 * @param[in]: itemList: key/value desc list
 * @param[in]: itemNum: batch num
 * @return: return RET_MMS_OK mean ok, others, return non-zero value
 */
CResult MmsDelete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum);

/**
 *
 * @param[in]: userId: user id
 * @param[in]: itemList: key/value desc list
 * @param[in]: itemNum: batch num
 * @return: return RET_MMS_OK mean success, others, return non-zero value
 */
CResult MmsReplace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum);

/**
 * @brief: start catch up task to recover
 *
 * @return: return RET_MMS_OK mean ok, others, return non-zero value
 */
CResult MmsStartCatchUpTask(void);

#ifdef __cplusplus
}
#endif
#endif // MMS_C_H
