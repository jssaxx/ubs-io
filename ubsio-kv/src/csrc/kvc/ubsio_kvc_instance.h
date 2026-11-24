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

#ifndef UBSIO_KVC_INSTANCE_H
#define UBSIO_KVC_INSTANCE_H

#include "ubsio_kvc.h"
#include "ubsio_kvc_err.h"

namespace ock {
namespace ubsio {

class KvcInstance {
public:
    static int32_t GetInitInfo(UbsioInitInfo &info);

    static KvcError ReadLocalFiles(const char *const *keys, uint32_t count, const uint8_t *ownerSlots,
                                   void *const *const *buffers, const size_t *const *lengths,
                                   const uint32_t *iovCounts, int *results);
};
} // namespace ubsio
} // namespace ock
#endif // UBSIO_KVC_INSTANCE_H
