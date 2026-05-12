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

#ifndef MMS_CACHE_LOCK_H
#define MMS_CACHE_LOCK_H

#include <atomic>
#include "mms_def.h"

namespace ock {
namespace mms {

// 状态位布局 (32-bit):
// [Bit 31]      : 1 bit,  是否被写锁独占, 1:有写锁独占, 0:没有线程持有写锁
// [Bits 16-30]  : 15 bit, 正在等待的写锁数量 - 最大 32767 个写线程等待
// [Bits 0-15]   : 16 bit, 当前持有的读锁数量 - 最大 65535 个读线程并发

constexpr uint32_t RW_W_LOCKED = 1U << 31;
constexpr uint32_t RW_W_WAIT_INC = 1U << 16;
constexpr uint32_t RW_W_WAIT_MASK = 0x7FFF0000;
constexpr uint32_t RW_R_INC = 1U;
constexpr uint32_t RW_R_MASK = 0x0000FFFF;

struct alignas(64) RwLockStatus {
    uint32_t state{0};
};

static void CacheReadLock(struct RwLockStatus *status)
{
    uint32_t expected = __atomic_load_n(&status->state, __ATOMIC_RELAXED);

    while (true) {
        if (expected & (RW_W_LOCKED | RW_W_WAIT_MASK)) {
            expected = __atomic_load_n(&status->state, __ATOMIC_RELAXED);
            continue;
        }

        uint32_t desired = expected + RW_R_INC;
        if (__atomic_compare_exchange_n(&status->state, &expected, desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            break;
        }
    }
}

static void CacheWriteLock(struct RwLockStatus *status)
{
    __atomic_add_fetch(&status->state, RW_W_WAIT_INC, __ATOMIC_RELAXED);
    uint32_t expected = __atomic_load_n(&status->state, __ATOMIC_RELAXED);

    while (true) {
        if ((expected & RW_W_LOCKED) == 0 && (expected & RW_R_MASK) == 0) {
            uint32_t desired = (expected - RW_W_WAIT_INC) | RW_W_LOCKED;
            if (__atomic_compare_exchange_n(&status->state, &expected, desired, true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_RELAXED)) {
                break;
            }
        } else {
            expected = __atomic_load_n(&status->state, __ATOMIC_RELAXED);
        }
    }
}

static void CacheReadUnLock(struct RwLockStatus *status)
{
    __atomic_sub_fetch(&status->state, RW_R_INC, __ATOMIC_RELEASE);
}

static void CacheWriteUnLock(struct RwLockStatus *status)
{
    __atomic_and_fetch(&status->state, ~RW_W_LOCKED, __ATOMIC_RELEASE);
}

}
}
#endif  // MMS_CACHE_LOCK_H
