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

#ifndef MMS_MEM_COMM_H
#define MMS_MEM_COMM_H

#include <map>
#include <functional>
#include <utility>

#include "mms_err.h"
#include "mms_types.h"
#include "mms_ref.h"
#include "mms_def.h"
#include "mms_str_util.h"

namespace ock {
namespace mms {

constexpr uint64_t META_SHM_IOCTX_SIZE = 256 * 1024 * 1024;
constexpr uint64_t META_SHM_INDEX_SIZE = 64 * 1024 * 1024;
constexpr uint16_t MAX_BLOCK_NUM = 4;

enum MmapMode : uint16_t {
    MMAP_MODE_RW = 0,
    MMAP_MODE_WRITE = 1,
    MMAP_MODE_READ = 2,
};

enum MmapArea : uint16_t {
    MMAP_AREA_IOCTX = 0,
    MMAP_AREA_INDEX = 1,
    MMAP_AREA_VALUE = 2,
    MMAP_AREA_BUCKET = 3,
    MMAP_AREA_BUTT
};

struct MemList {
    uint16_t numaNum;
    uint16_t numaId[MAX_NUMAS_NUM];
    uint64_t numaSize[MAX_NUMAS_NUM];
    uint64_t numaAddress[MAX_NUMAS_NUM];
    int32_t numaFd[MAX_NUMAS_NUM];
};

struct MemMgrOptions {
    uint16_t numaNum;
    uint16_t numaId[MAX_NUMAS_NUM];
    uint64_t numaSize[MAX_NUMAS_NUM];

    uint64_t areaSize[MMAP_AREA_BUTT][MAX_NUMAS_NUM];
    int32_t areaFd[MMAP_AREA_BUTT];
    MmapMode areaMode[MMAP_AREA_BUTT];

    MemMgrOptions() = default;
    ~MemMgrOptions() = default;
};

struct MemAllocOptions {
    uint16_t numaNum;
    uint16_t numaId[MAX_NUMAS_NUM];
    uint64_t numaSize[MAX_NUMAS_NUM];
    uint64_t numaAddress[MAX_NUMAS_NUM];

    uint32_t blockNum;
    uint32_t blockRate[MAX_BLOCK_NUM];
    uint32_t blockSize[MAX_BLOCK_NUM];

    MemAllocOptions() = default;
    ~MemAllocOptions() = default;
};
}
}

#endif // MMS_MEM_COMM_H

