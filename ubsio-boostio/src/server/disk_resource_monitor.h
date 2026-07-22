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

#ifndef DISK_RESOURCE_MONITOR_H
#define DISK_RESOURCE_MONITOR_H

#include <cstdint>
#include "bio_c.h"

namespace ock {
namespace bio {

class DiskResourceMonitor {
public:
    DiskResourceMonitor() = delete;

    static void Query(DiskResourcesDesc *disks, uint16_t &diskNum);
};

} // namespace bio
} // namespace ock

#endif // DISK_RESOURCE_MONITOR_H
