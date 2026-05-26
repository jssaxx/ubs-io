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

#ifndef BOOSTIO_BIO_ERR_H
#define BOOSTIO_BIO_ERR_H

#include <cstdint>

namespace ock {
namespace bio {
using BResult = int32_t;
enum Error : int32_t{
    BIO_OK = 0,
    BIO_ERR = 1,
    BIO_INNER_ERR = 2,
    BIO_INVALID_PARAM = 3,
    BIO_NOT_READY = 4,
    BIO_ALLOC_FAIL = 5,
    BIO_NOT_INITIALIZED = 6,
    BIO_NOT_EXISTS = 7,
    BIO_ALREADY_EXISTS = 8,
    BIO_ALREADY_DONE = 9,
    BIO_EXISTS = 10,
    BIO_CHECK_PT_FAIL = 11,
    BIO_KEY_CONFLICT = 12,
    BIO_READ_EXCEED = 13,
    BIO_INNER_RETRY = 14,
    BIO_NEED_WAIT = 15,
    BIO_CRC_ERR = 16,
    BIO_QUOTA_NOT_ENOUGH = 17,
    BIO_QUOTA_TIMEOUT = 18,
    BIO_LOAD_ALLOC_FAIL = 19,
    BIO_NET_RETRY = 100,
    BIO_DISK_IOERR = 201,
    BIO_UFS_IOERR = 301,

    BIO_MAX,
};
} // namespace bio
} // namespace ock

#endif // BOOSTIO_BIO_ERR_H
