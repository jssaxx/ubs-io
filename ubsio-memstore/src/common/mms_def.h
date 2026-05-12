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

#ifndef MMSCORE_MMS_DEF_H
#define MMSCORE_MMS_DEF_H

namespace ock {
namespace mms {
#ifndef ROUND_UP
#define ROUND_UP(x, align) (((x) + (align)-1) & ~((align)-1))
#endif

#ifndef ROUND_DOWN
#define ROUND_DOWN(x, align) ((x) & ~((align)-1))
#endif

#ifndef LIKELY
#define LIKELY(x) (__builtin_expect(!!(x), 1) != 0)
#endif

#ifndef UNLIKELY
#define UNLIKELY(x) (__builtin_expect(!!(x), 0) != 0)
#endif

#if defined(__aarch64__) || defined(__arm__)
#define CPU_RELAX() __asm__ __volatile__("yield" ::: "memory")
#elif defined(__x86_64__) || defined(_M_X64)
#define CPU_RELAX() __asm__ __volatile__("pause" ::: "memory")
#else
#define CPU_RELAX() ((void)0)
#endif

#define KB_UNIT (1024)
}
}

#endif // MMSCORE_MMS_DEF_H

