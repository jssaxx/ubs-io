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

#ifndef UBSIO_KVC_ERR_H
#define UBSIO_KVC_ERR_H

#include <cstdint>

namespace ock {
namespace ubsio {

enum DFCError : int32_t {
    DFC_OK = 0,
    DFC_ERR = 1,
    DFC_INNER_ERR = 2,
    DFC_INVALID_PARAM = 3,
    DFC_ALLOC_FAIL = 4,
    DFC_EAGAIN = 5,
    DFC_BIO_ERR = 6,
    DFC_NO_NDS = 7,

    DFC_MAX,
};

}
}
#endif // UBSIO_KVC_ERR_H