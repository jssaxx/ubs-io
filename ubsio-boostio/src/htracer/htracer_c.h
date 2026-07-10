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

#ifndef HTRACER_C_H
#define HTRACER_C_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool HTracerIsEnableC(void);
uint64_t HTracerNowNsC(void);
void HTracerDelayBeginC(int32_t tpId, const char *tpName);
void HTracerDelayEndC(int32_t tpId, uint64_t startNs, int32_t retCode);

#ifdef __cplusplus
}
#endif

#define HTRACER_C_TRACE_ID(serviceId, innerId) ((((serviceId) & 0xFFFF) << 16) | ((innerId) & 0xFFFF))

#define BDM_DISK_TRACE_WRITE_SYNC_C HTRACER_C_TRACE_ID(0, 200)
#define BDM_DISK_TRACE_READ_SYNC_C HTRACER_C_TRACE_ID(0, 201)
#define BDM_DISK_TRACE_WRITE_ASYNC_C HTRACER_C_TRACE_ID(0, 202)
#define BDM_DISK_TRACE_READ_ASYNC_C HTRACER_C_TRACE_ID(0, 203)

#define HTRACER_C_DELAY_BEGIN(tpId, tpName, startVar) \
    uint64_t startVar = 0;                            \
    if (HTracerIsEnableC()) {                         \
        startVar = HTracerNowNsC();                   \
        HTracerDelayBeginC((tpId), (tpName));         \
    }

#define HTRACER_C_DELAY_END(tpId, startVar, retCode)         \
    do {                                                     \
        if ((startVar) != 0) {                               \
            HTracerDelayEndC((tpId), (startVar), (retCode)); \
        }                                                    \
    } while (0)

#endif // HTRACER_C_H
