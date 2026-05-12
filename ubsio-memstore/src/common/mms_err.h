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

#ifndef MMSCORE_MMS_ERR_H
#define MMSCORE_MMS_ERR_H

#include <cstdint>

namespace ock {
namespace mms {
using BResult = int32_t;
enum Error : int32_t {
    MMS_OK = 0,
    MMS_ERR = 1,
    MMS_INNER_ERR = 2,
    MMS_INVALID_PARAM = 3,
    MMS_NOT_READY = 4,
    MMS_ALLOC_FAIL = 5,
    MMS_NOT_INITIALIZED = 6,
    MMS_NOT_EXISTS = 7,
    MMS_ALREADY_EXISTS = 8,
    MMS_ALREADY_DONE = 9,
    MMS_EXISTS = 10,
    MMS_CHECK_PT_FAIL = 11,
    MMS_KEY_CONFLICT = 12,
    MMS_READ_EXCEED = 13,
    MMS_INNER_RETRY = 14,
    MMS_NEED_WAIT = 15,
    MMS_CRC_ERR = 16,
    MMS_NET_RETRY = 17,
    MMS_PUT_REPEAT = 18,
    MMS_KEY_NOT_EXISTS = 19,
    MMS_NEED_UPDATE_PT_VERSION = 20,
    MMS_MAX,
};
}
}

#endif // MMSCORE_MMS_ERR_H

