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

#ifndef UBS_IO_TRACEPOINT_TYPE_H
#define UBS_IO_TRACEPOINT_TYPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TP_TYPES
#define TP_TYPES
typedef char TpChar;
typedef int32_t TpInt32;
typedef uint32_t TpUint32;
typedef uint64_t TpUint64;
#endif

#define PID_OSP_NULL 0
#define BSP_SOFT_REBOOT 0

void DpaxReboot(unsigned long resetReason, unsigned int pid, char *message);

#ifdef __cplusplus
}
#endif

#endif // UBS_IO_TRACEPOINT_TYPE_H
