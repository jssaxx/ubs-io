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

#ifndef BIO_TP_COMMON_H
#define BIO_TP_COMMON_H

#include <utility>

#ifdef __aarch64__
#include "tracepoint.h"
#else
#define BIO_TRACEP_PARAM_SIZE 32UL
typedef struct {
    char tpParamData[BIO_TRACEP_PARAM_SIZE];
} BioTracepointParam;
#endif

namespace ock {
namespace bio {
namespace tp {
class CommonTp {
public:
    static void Register() noexcept;
    static void Deregister() noexcept;

public:
    static void IntValueCallback(BioTracepointParam *user, int *value, int set) noexcept;
    static void IntValueResetCallback(BioTracepointParam *user, int *value) noexcept;
    static void IntValueErrnoCallback(BioTracepointParam *user, int *value, int set, int err) noexcept;
    static void LongValueCallback(BioTracepointParam *user, long *value, long set) noexcept;
    static void LongValueErrnoCallback(BioTracepointParam *user, long *value, long set, int err) noexcept;
    static void BoolValueCallback(BioTracepointParam *user, bool *value, bool set) noexcept;
    static void NoProcessCallback(BioTracepointParam *user) noexcept;
    static void PointerValueCallback(BioTracepointParam *user, void **value, void *set) noexcept;
    static void PointerValueResetCallback(BioTracepointParam *user, void **value) noexcept;
};
} // namespace tp
} // namespace bio
} // namespace ock

#endif // BIO_TP_COMMON_H
